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

#ifndef SRC_STREAM_WRAP_H_
#define SRC_STREAM_WRAP_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "stream_base.h"
#include "handle_wrap.h"
#include "v8.h"

namespace node {

class Environment;
class ExternalReferenceRegistry;

class LibuvStreamWrap : public HandleWrap, public StreamBase {
 public:
  static void Initialize(v8::Local<v8::Object> target,
                         v8::Local<v8::Value> unused,
                         v8::Local<v8::Context> context,
                         void* priv);
  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);
  int GetFD() override;
  bool IsAlive() override;
  bool IsClosing() override;
  bool IsIPCPipe() override;

  // JavaScript functions
  int ReadStart() override;
  int ReadStop() override;

  // Resource implementation
  int DoShutdown(ShutdownWrap* req_wrap) override;
  int DoTryWrite(uv_buf_t** bufs, size_t* count) override;
  inline bool HasDoTryWrite() const override { return true; }
  int DoWrite(WriteWrap* w,
              uv_buf_t* bufs,
              size_t count,
              uv_stream_t* send_handle) override;

  inline uv_stream_t* stream() const {
    return stream_;
  }

  inline bool is_named_pipe() const {
    return stream()->type == UV_NAMED_PIPE;
  }

  inline bool is_named_pipe_ipc() const {
    return is_named_pipe() &&
           reinterpret_cast<const uv_pipe_t*>(stream())->ipc != 0;
  }

  inline bool is_tcp() const {
    return stream()->type == UV_TCP;
  }

  ShutdownWrap* CreateShutdownWrap(v8::Local<v8::Object> object) override;
  WriteWrap* CreateWriteWrap(v8::Local<v8::Object> object) override;

  static LibuvStreamWrap* From(Environment* env, v8::Local<v8::Object> object);

 protected:
  LibuvStreamWrap(Environment* env,
                  v8::Local<v8::Object> object,
                  uv_stream_t* stream,
                  AsyncWrap::ProviderType provider);

  AsyncWrap* GetAsyncWrap() override;

  static v8::Local<v8::FunctionTemplate> GetConstructorTemplate(
      Environment* env);

 protected:
  inline void set_fd(int fd) {
#ifdef _WIN32
    fd_ = fd;
#endif
  }


 private:
  static void GetWriteQueueSize(
      const v8::FunctionCallbackInfo<v8::Value>& info);
  static void SetBlocking(const v8::FunctionCallbackInfo<v8::Value>& args);

  // Callbacks for libuv
  void OnUvAlloc(size_t suggested_size, uv_buf_t* buf);
  v8::Maybe<void> OnUvRead(ssize_t nread, const uv_buf_t* buf);

  static void AfterUvWrite(uv_write_t* req, int status);
  static void AfterUvShutdown(uv_shutdown_t* req, int status);

  uv_stream_t* const stream_;

#ifdef _WIN32
  // We don't always have an FD that we could look up on the stream_
  // object itself on Windows. However, for some cases, we open handles
  // using FDs; In that case, we can store and provide the value.
  // This became necessary because it allows to detect situations
  // where multiple handles refer to the same stdio FDs (in particular,
  // a possible IPC channel and a regular process.std??? stream).
  int fd_ = -1;
#endif
};


}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_STREAM_WRAP_H_
