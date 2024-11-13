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

#include "spawn_sync.h"
#include "debug_utils-inl.h"
#include "env-inl.h"
#include "node_external_reference.h"
#include "node_internals.h"
#include "string_bytes.h"
#include "util-inl.h"

#include <cstring>
#include "nbytes.h"

namespace node {

using v8::Array;
using v8::Context;
using v8::EscapableHandleScope;
using v8::FunctionCallbackInfo;
using v8::HandleScope;
using v8::Int32;
using v8::Integer;
using v8::Isolate;
using v8::Just;
using v8::JustVoid;
using v8::Local;
using v8::Maybe;
using v8::MaybeLocal;
using v8::Nothing;
using v8::Null;
using v8::Number;
using v8::Object;
using v8::String;
using v8::Value;

void SyncProcessOutputBuffer::OnAlloc(size_t suggested_size,
                                      uv_buf_t* buf) const {
  if (used() == kBufferSize)
    *buf = uv_buf_init(nullptr, 0);
  else
    *buf = uv_buf_init(data_ + used(), available());
}


void SyncProcessOutputBuffer::OnRead(const uv_buf_t* buf, size_t nread) {
  // If we hand out the same chunk twice, this should catch it.
  CHECK_EQ(buf->base, data_ + used());
  used_ += static_cast<unsigned int>(nread);
}


size_t SyncProcessOutputBuffer::Copy(char* dest) const {
  if (dest != nullptr) memcpy(dest, data_, used());
  return used();
}


unsigned int SyncProcessOutputBuffer::available() const {
  return sizeof data_ - used();
}


unsigned int SyncProcessOutputBuffer::used() const {
  return used_;
}


SyncProcessOutputBuffer* SyncProcessOutputBuffer::next() const {
  return next_;
}


void SyncProcessOutputBuffer::set_next(SyncProcessOutputBuffer* next) {
  next_ = next;
}


SyncProcessStdioPipe::SyncProcessStdioPipe(SyncProcessRunner* process_handler,
                                           bool readable,
                                           bool writable,
                                           uv_buf_t input_buffer)
    : process_handler_(process_handler),
      readable_(readable),
      writable_(writable),
      input_buffer_(input_buffer),

      first_output_buffer_(nullptr),
      last_output_buffer_(nullptr),

      uv_pipe_(),
      write_req_(),
      shutdown_req_(),

      lifecycle_(kUninitialized) {
  CHECK(readable || writable);
}


SyncProcessStdioPipe::~SyncProcessStdioPipe() {
  CHECK(lifecycle_ == kUninitialized || lifecycle_ == kClosed);

  SyncProcessOutputBuffer* buf;
  SyncProcessOutputBuffer* next;

  for (buf = first_output_buffer_; buf != nullptr; buf = next) {
    next = buf->next();
    delete buf;
  }
}


int SyncProcessStdioPipe::Initialize(uv_loop_t* loop) {
  CHECK_EQ(lifecycle_, kUninitialized);

  int r = uv_pipe_init(loop, uv_pipe(), 0);
  if (r < 0)
    return r;

  uv_pipe()->data = this;

  lifecycle_ = kInitialized;
  return 0;
}


int SyncProcessStdioPipe::Start() {
  CHECK_EQ(lifecycle_, kInitialized);

  // Set the busy flag already. If this function fails no recovery is
  // possible.
  lifecycle_ = kStarted;

  if (readable()) {
    if (input_buffer_.len > 0) {
      CHECK_NOT_NULL(input_buffer_.base);

      int r = uv_write(&write_req_,
                       uv_stream(),
                       &input_buffer_,
                       1,
                       WriteCallback);
      if (r < 0)
        return r;
    }

    int r = uv_shutdown(&shutdown_req_, uv_stream(), ShutdownCallback);
    if (r < 0)
      return r;
  }

  if (writable()) {
    int r = uv_read_start(uv_stream(), AllocCallback, ReadCallback);
    if (r < 0)
      return r;
  }

  return 0;
}


void SyncProcessStdioPipe::Close() {
  CHECK(lifecycle_ == kInitialized || lifecycle_ == kStarted);

  uv_close(uv_handle(), CloseCallback);

  lifecycle_ = kClosing;
}


Local<Object> SyncProcessStdioPipe::GetOutputAsBuffer(Environment* env) const {
  size_t length = OutputLength();
  Local<Object> js_buffer = Buffer::New(env, length).ToLocalChecked();
  CopyOutput(Buffer::Data(js_buffer));
  return js_buffer;
}


bool SyncProcessStdioPipe::readable() const {
  return readable_;
}


bool SyncProcessStdioPipe::writable() const {
  return writable_;
}


uv_stdio_flags SyncProcessStdioPipe::uv_flags() const {
  unsigned int flags;

  flags = UV_CREATE_PIPE;
  if (readable())
    flags |= UV_READABLE_PIPE;
  if (writable())
    flags |= UV_WRITABLE_PIPE;

  return static_cast<uv_stdio_flags>(flags);
}


uv_pipe_t* SyncProcessStdioPipe::uv_pipe() const {
  CHECK_LT(lifecycle_, kClosing);
  return &uv_pipe_;
}


uv_stream_t* SyncProcessStdioPipe::uv_stream() const {
  return reinterpret_cast<uv_stream_t*>(uv_pipe());
}


uv_handle_t* SyncProcessStdioPipe::uv_handle() const {
  return reinterpret_cast<uv_handle_t*>(uv_pipe());
}


size_t SyncProcessStdioPipe::OutputLength() const {
  SyncProcessOutputBuffer* buf;
  size_t size = 0;

  for (buf = first_output_buffer_; buf != nullptr; buf = buf->next())
    size += buf->used();

  return size;
}


void SyncProcessStdioPipe::CopyOutput(char* dest) const {
  SyncProcessOutputBuffer* buf;
  size_t offset = 0;

  for (buf = first_output_buffer_; buf != nullptr; buf = buf->next())
    offset += buf->Copy(dest + offset);
}


void SyncProcessStdioPipe::OnAlloc(size_t suggested_size, uv_buf_t* buf) {
  // This function assumes that libuv will never allocate two buffers for the
  // same stream at the same time. There's an assert in
  // SyncProcessOutputBuffer::OnRead that would fail if this assumption was
  // ever violated.

  if (last_output_buffer_ == nullptr) {
    // Allocate the first capture buffer.
    first_output_buffer_ = new SyncProcessOutputBuffer();
    last_output_buffer_ = first_output_buffer_;

  } else if (last_output_buffer_->available() == 0) {
    // The current capture buffer is full so get us a new one.
    SyncProcessOutputBuffer* buf = new SyncProcessOutputBuffer();
    last_output_buffer_->set_next(buf);
    last_output_buffer_ = buf;
  }

  last_output_buffer_->OnAlloc(suggested_size, buf);
}


void SyncProcessStdioPipe::OnRead(const uv_buf_t* buf, ssize_t nread) {
  if (nread == UV_EOF) {
    // Libuv implicitly stops reading on EOF.

  } else if (nread < 0) {
    SetError(static_cast<int>(nread));
    // At some point libuv should really implicitly stop reading on error.
    uv_read_stop(uv_stream());

  } else {
    last_output_buffer_->OnRead(buf, nread);
    process_handler_->IncrementBufferSizeAndCheckOverflow(nread);
  }
}


void SyncProcessStdioPipe::OnWriteDone(int result) {
  if (result < 0)
    SetError(result);
}


void SyncProcessStdioPipe::OnShutdownDone(int result) {
  if (result < 0)
    SetError(result);
}


void SyncProcessStdioPipe::OnClose() {
  lifecycle_ = kClosed;
}


void SyncProcessStdioPipe::SetError(int error) {
  CHECK_NE(error, 0);
  process_handler_->SetPipeError(error);
}


void SyncProcessStdioPipe::AllocCallback(uv_handle_t* handle,
                                         size_t suggested_size,
                                         uv_buf_t* buf) {
  SyncProcessStdioPipe* self =
      reinterpret_cast<SyncProcessStdioPipe*>(handle->data);
  self->OnAlloc(suggested_size, buf);
}


void SyncProcessStdioPipe::ReadCallback(uv_stream_t* stream,
                                        ssize_t nread,
                                        const uv_buf_t* buf) {
  SyncProcessStdioPipe* self =
        reinterpret_cast<SyncProcessStdioPipe*>(stream->data);
  self->OnRead(buf, nread);
}


void SyncProcessStdioPipe::WriteCallback(uv_write_t* req, int result) {
  SyncProcessStdioPipe* self =
      reinterpret_cast<SyncProcessStdioPipe*>(req->handle->data);
  self->OnWriteDone(result);
}


void SyncProcessStdioPipe::ShutdownCallback(uv_shutdown_t* req, int result) {
  SyncProcessStdioPipe* self =
      reinterpret_cast<SyncProcessStdioPipe*>(req->handle->data);

  // On AIX, OS X and the BSDs, calling shutdown() on one end of a pipe
  // when the other end has closed the connection fails with ENOTCONN.
  // Libuv is not the right place to handle that because it can't tell
  // if the error is genuine but we here can.
  if (result == UV_ENOTCONN)
    result = 0;

  self->OnShutdownDone(result);
}


void SyncProcessStdioPipe::CloseCallback(uv_handle_t* handle) {
  SyncProcessStdioPipe* self =
      reinterpret_cast<SyncProcessStdioPipe*>(handle->data);
  self->OnClose();
}


void SyncProcessRunner::Initialize(Local<Object> target,
                                   Local<Value> unused,
                                   Local<Context> context,
                                   void* priv) {
  SetMethod(context, target, "spawn", Spawn);
}

void SyncProcessRunner::RegisterExternalReferences(
    ExternalReferenceRegistry* registry) {
  registry->Register(Spawn);
}

void SyncProcessRunner::Spawn(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  THROW_IF_INSUFFICIENT_PERMISSIONS(
      env, permission::PermissionScope::kChildProcess, "");
  env->PrintSyncTrace();
  SyncProcessRunner p(env);
  Local<Value> result;
  if (!p.Run(args[0]).ToLocal(&result)) return;
  args.GetReturnValue().Set(result);
}


SyncProcessRunner::SyncProcessRunner(Environment* env)
    : max_buffer_(0),
      timeout_(0),
      kill_signal_(SIGTERM),

      uv_loop_(nullptr),

      stdio_count_(0),
      uv_stdio_containers_(nullptr),
      stdio_pipes_initialized_(false),

      uv_process_options_(),
      file_buffer_(nullptr),
      args_buffer_(nullptr),
      env_buffer_(nullptr),
      cwd_buffer_(nullptr),

      uv_process_(),
      killed_(false),

      buffered_output_size_(0),
      exit_status_(-1),
      term_signal_(-1),

      uv_timer_(),
      kill_timer_initialized_(false),

      error_(0),
      pipe_error_(0),

      lifecycle_(kUninitialized),

      env_(env) {
}


SyncProcessRunner::~SyncProcessRunner() {
  CHECK_EQ(lifecycle_, kHandlesClosed);

  stdio_pipes_.clear();
  delete[] file_buffer_;
  delete[] args_buffer_;
  delete[] cwd_buffer_;
  delete[] env_buffer_;
  delete[] uv_stdio_containers_;
}


Environment* SyncProcessRunner::env() const {
  return env_;
}

MaybeLocal<Object> SyncProcessRunner::Run(Local<Value> options) {
  EscapableHandleScope scope(env()->isolate());

  CHECK_EQ(lifecycle_, kUninitialized);

  Maybe<void> r = TryInitializeAndRunLoop(options);
  CloseHandlesAndDeleteLoop();
  if (r.IsNothing()) return MaybeLocal<Object>();

  Local<Object> result = BuildResultObject();

  return scope.Escape(result);
}

Maybe<void> SyncProcessRunner::TryInitializeAndRunLoop(Local<Value> options) {
  int r;

  // There is no recovery from failure inside TryInitializeAndRunLoop - the
  // only option we'd have is to close all handles and destroy the loop.
  CHECK_EQ(lifecycle_, kUninitialized);
  lifecycle_ = kInitialized;

  uv_loop_ = new uv_loop_t;
  if (uv_loop_ == nullptr) {
    SetError(UV_ENOMEM);
    return JustVoid();
  }

  r = uv_loop_init(uv_loop_);
  if (r < 0) {
    delete uv_loop_;
    uv_loop_ = nullptr;
    SetError(r);
    return JustVoid();
  }

  if (!ParseOptions(options).To(&r)) return Nothing<void>();

  if (r < 0) {
    SetError(r);
    return JustVoid();
  }

  if (timeout_ > 0) {
    r = uv_timer_init(uv_loop_, &uv_timer_);
    if (r < 0) {
      SetError(r);
      return JustVoid();
    }

    uv_unref(reinterpret_cast<uv_handle_t*>(&uv_timer_));

    uv_timer_.data = this;
    kill_timer_initialized_ = true;

    // Start the timer immediately. If uv_spawn fails then
    // CloseHandlesAndDeleteLoop() will immediately close the timer handle
    // which implicitly stops it, so there is no risk that the timeout callback
    // runs when the process didn't start.
    r = uv_timer_start(&uv_timer_, KillTimerCallback, timeout_, 0);
    if (r < 0) {
      SetError(r);
      return JustVoid();
    }
  }

  uv_process_options_.exit_cb = ExitCallback;
  r = uv_spawn(uv_loop_, &uv_process_, &uv_process_options_);
  if (r < 0) {
    SetError(r);
    return JustVoid();
  }
  uv_process_.data = this;

  for (const auto& pipe : stdio_pipes_) {
    if (pipe != nullptr) {
      r = pipe->Start();
      if (r < 0) {
        SetPipeError(r);
        return JustVoid();
      }
    }
  }

  r = uv_run(uv_loop_, UV_RUN_DEFAULT);
  if (r < 0)
    // We can't handle uv_run failure.
    ABORT();

  // If we get here the process should have exited.
  CHECK_GE(exit_status_, 0);
  return JustVoid();
}

void SyncProcessRunner::CloseHandlesAndDeleteLoop() {
  CHECK_LT(lifecycle_, kHandlesClosed);

  if (uv_loop_ != nullptr) {
    CloseStdioPipes();
    CloseKillTimer();
    // Close the process handle when ExitCallback was not called.
    uv_handle_t* uv_process_handle =
        reinterpret_cast<uv_handle_t*>(&uv_process_);

    // Close the process handle if it is still open. The handle type also
    // needs to be checked because TryInitializeAndRunLoop() won't spawn a
    // process if input validation fails.
    if (uv_process_handle->type == UV_PROCESS &&
        !uv_is_closing(uv_process_handle))
      uv_close(uv_process_handle, nullptr);

    // Give closing watchers a chance to finish closing and get their close
    // callbacks called.
    int r = uv_run(uv_loop_, UV_RUN_DEFAULT);
    if (r < 0)
      ABORT();

    CheckedUvLoopClose(uv_loop_);
    delete uv_loop_;
    uv_loop_ = nullptr;

  } else {
    // If the loop doesn't exist, neither should any pipes or timers.
    CHECK_EQ(false, stdio_pipes_initialized_);
    CHECK_EQ(false, kill_timer_initialized_);
  }

  lifecycle_ = kHandlesClosed;
}


void SyncProcessRunner::CloseStdioPipes() {
  CHECK_LT(lifecycle_, kHandlesClosed);

  if (stdio_pipes_initialized_) {
    CHECK(!stdio_pipes_.empty());
    CHECK_NOT_NULL(uv_loop_);

    for (const auto& pipe : stdio_pipes_) {
      if (pipe)
        pipe->Close();
    }

    stdio_pipes_initialized_ = false;
  }
}


void SyncProcessRunner::CloseKillTimer() {
  CHECK_LT(lifecycle_, kHandlesClosed);

  if (kill_timer_initialized_) {
    CHECK_GT(timeout_, 0);
    CHECK_NOT_NULL(uv_loop_);

    uv_handle_t* uv_timer_handle = reinterpret_cast<uv_handle_t*>(&uv_timer_);
    uv_ref(uv_timer_handle);
    uv_close(uv_timer_handle, KillTimerCloseCallback);

    kill_timer_initialized_ = false;
  }
}


void SyncProcessRunner::Kill() {
  // Only attempt to kill once.
  if (killed_)
    return;
  killed_ = true;

  // We might get here even if the process we spawned has already exited. This
  // could happen when our child process spawned another process which
  // inherited (one of) the stdio pipes. In this case we won't attempt to send
  // a signal to the process, however we will still close our end of the stdio
  // pipes so this situation won't make us hang.
  if (exit_status_ < 0) {
    int r = uv_process_kill(&uv_process_, kill_signal_);

    // If uv_kill failed with an error that isn't ESRCH, the user probably
    // specified an invalid or unsupported signal. Signal this to the user as
    // and error and kill the process with SIGKILL instead.
    if (r < 0 && r != UV_ESRCH) {
      SetError(r);

      // Deliberately ignore the return value, we might not have
      // sufficient privileges to signal the child process.
      USE(uv_process_kill(&uv_process_, SIGKILL));
    }
  }

  // Close all stdio pipes.
  CloseStdioPipes();

  // Stop the timeout timer immediately.
  CloseKillTimer();
}


void SyncProcessRunner::IncrementBufferSizeAndCheckOverflow(ssize_t length) {
  buffered_output_size_ += length;

  if (max_buffer_ > 0 && buffered_output_size_ > max_buffer_) {
    SetError(UV_ENOBUFS);
    Kill();
  }
}


void SyncProcessRunner::OnExit(int64_t exit_status, int term_signal) {
  if (exit_status < 0)
    return SetError(static_cast<int>(exit_status));

  exit_status_ = exit_status;
  term_signal_ = term_signal;
}


void SyncProcessRunner::OnKillTimerTimeout() {
  SetError(UV_ETIMEDOUT);
  Kill();
}


int SyncProcessRunner::GetError() {
  if (error_ != 0)
    return error_;
  else
    return pipe_error_;
}


void SyncProcessRunner::SetError(int error) {
  if (error_ == 0)
    error_ = error;
}


void SyncProcessRunner::SetPipeError(int pipe_error) {
  if (pipe_error_ == 0)
    pipe_error_ = pipe_error;
}


Local<Object> SyncProcessRunner::BuildResultObject() {
  EscapableHandleScope scope(env()->isolate());
  Local<Context> context = env()->context();

  Local<Object> js_result = Object::New(env()->isolate());

  if (GetError() != 0) {
    js_result->Set(context, env()->error_string(),
                   Integer::New(env()->isolate(), GetError())).Check();
  }

  if (exit_status_ >= 0) {
    if (term_signal_ > 0) {
      js_result->Set(context, env()->status_string(),
                     Null(env()->isolate())).Check();
    } else {
      js_result->Set(context, env()->status_string(),
                     Number::New(env()->isolate(),
                                 static_cast<double>(exit_status_))).Check();
    }
  } else {
    // If exit_status_ < 0 the process was never started because of some error.
    js_result->Set(context, env()->status_string(),
                   Null(env()->isolate())).Check();
  }

  if (term_signal_ > 0)
    js_result->Set(context, env()->signal_string(),
                   String::NewFromUtf8(env()->isolate(),
                                       signo_string(term_signal_))
                       .ToLocalChecked())
        .Check();
  else
    js_result->Set(context, env()->signal_string(),
                   Null(env()->isolate())).Check();

  if (exit_status_ >= 0)
    js_result->Set(context, env()->output_string(),
                   BuildOutputArray()).Check();
  else
    js_result->Set(context, env()->output_string(),
                   Null(env()->isolate())).Check();

  js_result->Set(context, env()->pid_string(),
                 Number::New(env()->isolate(), uv_process_.pid)).Check();

  return scope.Escape(js_result);
}


Local<Array> SyncProcessRunner::BuildOutputArray() {
  CHECK_GE(lifecycle_, kInitialized);
  CHECK(!stdio_pipes_.empty());

  EscapableHandleScope scope(env()->isolate());
  MaybeStackBuffer<Local<Value>, 8> js_output(stdio_pipes_.size());

  for (uint32_t i = 0; i < stdio_pipes_.size(); i++) {
    SyncProcessStdioPipe* h = stdio_pipes_[i].get();
    if (h != nullptr && h->writable())
      js_output[i] = h->GetOutputAsBuffer(env());
    else
      js_output[i] = Null(env()->isolate());
  }

  return scope.Escape(
      Array::New(env()->isolate(), js_output.out(), js_output.length()));
}

Maybe<int> SyncProcessRunner::ParseOptions(Local<Value> js_value) {
  Isolate* isolate = env()->isolate();
  HandleScope scope(isolate);
  int r;

  if (!js_value->IsObject()) return Just<int>(UV_EINVAL);

  Local<Context> context = env()->context();
  Local<Object> js_options = js_value.As<Object>();

  Local<Value> js_file =
      js_options->Get(context, env()->file_string()).ToLocalChecked();
  if (!CopyJsString(js_file, &file_buffer_).To(&r)) return Nothing<int>();
  if (r < 0) return Just(r);
  uv_process_options_.file = file_buffer_;

  // Undocumented feature of Win32 CreateProcess API allows spawning
  // batch files directly but is potentially insecure because arguments
  // are not escaped (and sometimes cannot be unambiguously escaped),
  // hence why they are rejected here.
#ifdef _WIN32
  if (IsWindowsBatchFile(uv_process_options_.file))
    return Just<int>(UV_EINVAL);
#endif

  Local<Value> js_args =
      js_options->Get(context, env()->args_string()).ToLocalChecked();
  if (!CopyJsStringArray(js_args, &args_buffer_).To(&r)) return Nothing<int>();
  if (r < 0) return Just(r);
  uv_process_options_.args = reinterpret_cast<char**>(args_buffer_);

  Local<Value> js_cwd =
      js_options->Get(context, env()->cwd_string()).ToLocalChecked();
  if (IsSet(js_cwd)) {
    if (!CopyJsString(js_cwd, &cwd_buffer_).To(&r)) return Nothing<int>();
    if (r < 0) return Just(r);
    uv_process_options_.cwd = cwd_buffer_;
  }

  Local<Value> js_env_pairs =
      js_options->Get(context, env()->env_pairs_string()).ToLocalChecked();
  if (IsSet(js_env_pairs)) {
    if (!CopyJsStringArray(js_env_pairs, &env_buffer_).To(&r))
      return Nothing<int>();
    if (r < 0) return Just(r);

    uv_process_options_.env = reinterpret_cast<char**>(env_buffer_);
  }
  Local<Value> js_uid =
      js_options->Get(context, env()->uid_string()).ToLocalChecked();
  if (IsSet(js_uid)) {
    CHECK(js_uid->IsInt32());
    const int32_t uid = js_uid.As<Int32>()->Value();
    uv_process_options_.uid = static_cast<uv_uid_t>(uid);
    uv_process_options_.flags |= UV_PROCESS_SETUID;
  }

  Local<Value> js_gid =
      js_options->Get(context, env()->gid_string()).ToLocalChecked();
  if (IsSet(js_gid)) {
    CHECK(js_gid->IsInt32());
    const int32_t gid = js_gid.As<Int32>()->Value();
    uv_process_options_.gid = static_cast<uv_gid_t>(gid);
    uv_process_options_.flags |= UV_PROCESS_SETGID;
  }

  Local<Value> js_detached =
      js_options->Get(context, env()->detached_string()).ToLocalChecked();
  if (js_detached->BooleanValue(isolate))
    uv_process_options_.flags |= UV_PROCESS_DETACHED;

  Local<Value> js_win_hide =
      js_options->Get(context, env()->windows_hide_string()).ToLocalChecked();
  if (js_win_hide->BooleanValue(isolate))
    uv_process_options_.flags |= UV_PROCESS_WINDOWS_HIDE;

  if (env()->hide_console_windows())
    uv_process_options_.flags |= UV_PROCESS_WINDOWS_HIDE_CONSOLE;

  Local<Value> js_wva =
      js_options->Get(context, env()->windows_verbatim_arguments_string())
          .ToLocalChecked();

  if (js_wva->BooleanValue(isolate))
    uv_process_options_.flags |= UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS;

  Local<Value> js_timeout =
      js_options->Get(context, env()->timeout_string()).ToLocalChecked();
  if (IsSet(js_timeout)) {
    CHECK(js_timeout->IsNumber());
    int64_t timeout = js_timeout->IntegerValue(context).FromJust();
    timeout_ = static_cast<uint64_t>(timeout);
  }

  Local<Value> js_max_buffer =
      js_options->Get(context, env()->max_buffer_string()).ToLocalChecked();
  if (IsSet(js_max_buffer)) {
    CHECK(js_max_buffer->IsNumber());
    max_buffer_ = js_max_buffer->NumberValue(context).FromJust();
  }

  Local<Value> js_kill_signal =
      js_options->Get(context, env()->kill_signal_string()).ToLocalChecked();
  if (IsSet(js_kill_signal)) {
    CHECK(js_kill_signal->IsInt32());
    kill_signal_ = js_kill_signal.As<Int32>()->Value();
  }

  Local<Value> js_stdio =
      js_options->Get(context, env()->stdio_string()).ToLocalChecked();
  r = ParseStdioOptions(js_stdio);
  if (r < 0) return Just(r);

  return Just(0);
}


int SyncProcessRunner::ParseStdioOptions(Local<Value> js_value) {
  HandleScope scope(env()->isolate());
  Local<Array> js_stdio_options;

  if (!js_value->IsArray())
    return UV_EINVAL;

  Local<Context> context = env()->context();
  js_stdio_options = js_value.As<Array>();

  stdio_count_ = js_stdio_options->Length();
  uv_stdio_containers_ = new uv_stdio_container_t[stdio_count_];

  stdio_pipes_.clear();
  stdio_pipes_.resize(stdio_count_);
  stdio_pipes_initialized_ = true;

  for (uint32_t i = 0; i < stdio_count_; i++) {
    Local<Value> js_stdio_option =
        js_stdio_options->Get(context, i).ToLocalChecked();

    if (!js_stdio_option->IsObject())
      return UV_EINVAL;

    int r = ParseStdioOption(i, js_stdio_option.As<Object>());
    if (r < 0)
      return r;
  }

  uv_process_options_.stdio = uv_stdio_containers_;
  uv_process_options_.stdio_count = stdio_count_;

  return 0;
}


int SyncProcessRunner::ParseStdioOption(int child_fd,
                                        Local<Object> js_stdio_option) {
  Local<Context> context = env()->context();
  Local<Value> js_type =
      js_stdio_option->Get(context, env()->type_string()).ToLocalChecked();

  if (js_type->StrictEquals(env()->ignore_string())) {
    return AddStdioIgnore(child_fd);

  } else if (js_type->StrictEquals(env()->pipe_string())) {
    Isolate* isolate = env()->isolate();
    Local<String> rs = env()->readable_string();
    Local<String> ws = env()->writable_string();

    bool readable = js_stdio_option->Get(context, rs)
        .ToLocalChecked()->BooleanValue(isolate);
    bool writable =
        js_stdio_option->Get(context, ws)
        .ToLocalChecked()->BooleanValue(isolate);

    uv_buf_t buf = uv_buf_init(nullptr, 0);

    if (readable) {
      Local<Value> input =
          js_stdio_option->Get(context, env()->input_string()).ToLocalChecked();
      if (Buffer::HasInstance(input)) {
        buf = uv_buf_init(Buffer::Data(input),
                          static_cast<unsigned int>(Buffer::Length(input)));
      } else if (!input->IsUndefined() && !input->IsNull()) {
        // Strings, numbers etc. are currently unsupported. It's not possible
        // to create a buffer for them here because there is no way to free
        // them afterwards.
        return UV_EINVAL;
      }
    }

    return AddStdioPipe(child_fd, readable, writable, buf);

  } else if (js_type->StrictEquals(env()->inherit_string()) ||
             js_type->StrictEquals(env()->fd_string())) {
    int inherit_fd = js_stdio_option->Get(context, env()->fd_string())
        .ToLocalChecked()->Int32Value(context).FromJust();
    return AddStdioInheritFD(child_fd, inherit_fd);

  } else {
    UNREACHABLE("invalid child stdio type");
  }
}


int SyncProcessRunner::AddStdioIgnore(uint32_t child_fd) {
  CHECK_LT(child_fd, stdio_count_);
  CHECK(!stdio_pipes_[child_fd]);

  uv_stdio_containers_[child_fd].flags = UV_IGNORE;

  return 0;
}


int SyncProcessRunner::AddStdioPipe(uint32_t child_fd,
                                    bool readable,
                                    bool writable,
                                    uv_buf_t input_buffer) {
  CHECK_LT(child_fd, stdio_count_);
  CHECK(!stdio_pipes_[child_fd]);

  std::unique_ptr<SyncProcessStdioPipe> h(
      new SyncProcessStdioPipe(this, readable, writable, input_buffer));

  int r = h->Initialize(uv_loop_);
  if (r < 0) {
    h.reset();
    return r;
  }

  uv_stdio_containers_[child_fd].flags = h->uv_flags();
  uv_stdio_containers_[child_fd].data.stream = h->uv_stream();

  stdio_pipes_[child_fd] = std::move(h);

  return 0;
}


int SyncProcessRunner::AddStdioInheritFD(uint32_t child_fd, int inherit_fd) {
  CHECK_LT(child_fd, stdio_count_);
  CHECK(!stdio_pipes_[child_fd]);

  uv_stdio_containers_[child_fd].flags = UV_INHERIT_FD;
  uv_stdio_containers_[child_fd].data.fd = inherit_fd;

  return 0;
}


bool SyncProcessRunner::IsSet(Local<Value> value) {
  return !value->IsUndefined() && !value->IsNull();
}

Maybe<int> SyncProcessRunner::CopyJsString(Local<Value> js_value,
                                           const char** target) {
  Isolate* isolate = env()->isolate();
  Local<String> js_string;
  size_t size, written;
  char* buffer;

  if (js_value->IsString())
    js_string = js_value.As<String>();
  else if (!js_value->ToString(env()->isolate()->GetCurrentContext())
                .ToLocal(&js_string))
    return Nothing<int>();

  // Include space for null terminator byte.
  if (!StringBytes::StorageSize(isolate, js_string, UTF8).To(&size))
    return Nothing<int>();
  size += 1;

  buffer = new char[size];

  written = StringBytes::Write(isolate, buffer, -1, js_string, UTF8);
  buffer[written] = '\0';

  *target = buffer;
  return Just(0);
}

Maybe<int> SyncProcessRunner::CopyJsStringArray(Local<Value> js_value,
                                                char** target) {
  Isolate* isolate = env()->isolate();
  Local<Array> js_array;
  uint32_t length;
  size_t list_size, data_size, data_offset;
  char** list;
  char* buffer;

  if (!js_value->IsArray()) return Just<int>(UV_EINVAL);

  Local<Context> context = env()->context();
  js_array = js_value.As<Array>()->Clone().As<Array>();
  length = js_array->Length();
  data_size = 0;

  // Index has a pointer to every string element, plus one more for a final
  // null pointer.
  list_size = (length + 1) * sizeof *list;

  // Convert all array elements to string. Modify the js object itself if
  // needed - it's okay since we cloned the original object. Also compute the
  // length of all strings, including room for a null terminator after every
  // string. Align strings to cache lines.
  for (uint32_t i = 0; i < length; i++) {
    auto value = js_array->Get(context, i).ToLocalChecked();

    if (!value->IsString()) {
      Local<String> string;
      if (!value->ToString(env()->isolate()->GetCurrentContext())
               .ToLocal(&string))
        return Nothing<int>();
      js_array
          ->Set(context,
                i,
                string)
          .Check();
    }

    Maybe<size_t> maybe_size = StringBytes::StorageSize(isolate, value, UTF8);
    if (maybe_size.IsNothing()) return Nothing<int>();
    data_size += maybe_size.FromJust() + 1;
    data_size = nbytes::RoundUp(data_size, sizeof(void*));
  }

  buffer = new char[list_size + data_size];

  list = reinterpret_cast<char**>(buffer);
  data_offset = list_size;

  for (uint32_t i = 0; i < length; i++) {
    list[i] = buffer + data_offset;
    auto value = js_array->Get(context, i).ToLocalChecked();
    data_offset += StringBytes::Write(isolate,
                                      buffer + data_offset,
                                      -1,
                                      value,
                                      UTF8);
    buffer[data_offset++] = '\0';
    data_offset = nbytes::RoundUp(data_offset, sizeof(void*));
  }

  list[length] = nullptr;

  *target = buffer;
  return Just(0);
}


void SyncProcessRunner::ExitCallback(uv_process_t* handle,
                                     int64_t exit_status,
                                     int term_signal) {
  SyncProcessRunner* self = reinterpret_cast<SyncProcessRunner*>(handle->data);
  uv_close(reinterpret_cast<uv_handle_t*>(handle), nullptr);
  self->OnExit(exit_status, term_signal);
}


void SyncProcessRunner::KillTimerCallback(uv_timer_t* handle) {
  SyncProcessRunner* self = reinterpret_cast<SyncProcessRunner*>(handle->data);
  self->OnKillTimerTimeout();
}


void SyncProcessRunner::KillTimerCloseCallback(uv_handle_t* handle) {
  // No-op.
}

}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(spawn_sync,
                                    node::SyncProcessRunner::Initialize)
NODE_BINDING_EXTERNAL_REFERENCE(
    spawn_sync, node::SyncProcessRunner::RegisterExternalReferences)
