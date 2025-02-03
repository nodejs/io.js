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

#include "env-inl.h"
#include "node_external_reference.h"
#include "permission/permission.h"
#include "stream_base-inl.h"
#include "stream_wrap.h"
#include "util-inl.h"

#include <climits>
#include <cstdlib>
#include <cstring>

namespace node {

using v8::Array;
using v8::Context;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Int32;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::Number;
using v8::Object;
using v8::String;
using v8::Value;

namespace {

class ProcessWrap : public HandleWrap {
 public:
  static void Initialize(Local<Object> target,
                         Local<Value> unused,
                         Local<Context> context,
                         void* priv) {
    Environment* env = Environment::GetCurrent(context);
    Isolate* isolate = env->isolate();
    Local<FunctionTemplate> constructor = NewFunctionTemplate(isolate, New);
    constructor->InstanceTemplate()->SetInternalFieldCount(
        ProcessWrap::kInternalFieldCount);

    constructor->Inherit(HandleWrap::GetConstructorTemplate(env));

    SetProtoMethod(isolate, constructor, "spawn", Spawn);
    SetProtoMethod(isolate, constructor, "kill", Kill);

    SetConstructorFunction(context, target, "Process", constructor);
  }

  static void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
    registry->Register(New);
    registry->Register(Spawn);
    registry->Register(Kill);
  }

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(ProcessWrap)
  SET_SELF_SIZE(ProcessWrap)

 private:
  static void New(const FunctionCallbackInfo<Value>& args) {
    // This constructor should not be exposed to public javascript.
    // Therefore we assert that we are not trying to call this as a
    // normal function.
    CHECK(args.IsConstructCall());
    Environment* env = Environment::GetCurrent(args);
    new ProcessWrap(env, args.This());
  }

  ProcessWrap(Environment* env, Local<Object> object)
      : HandleWrap(env,
                   object,
                   reinterpret_cast<uv_handle_t*>(&process_),
                   AsyncWrap::PROVIDER_PROCESSWRAP) {
    MarkAsUninitialized();
  }

  static uv_stream_t* StreamForWrap(Environment* env, Local<Object> stdio) {
    Local<String> handle_key = env->handle_string();
    // This property has always been set by JS land if we are in this code path.
    Local<Object> handle =
        stdio->Get(env->context(), handle_key).ToLocalChecked().As<Object>();

    uv_stream_t* stream = LibuvStreamWrap::From(env, handle)->stream();
    CHECK_NOT_NULL(stream);
    return stream;
  }

  static void ParseStdioOptions(Environment* env,
                                Local<Object> js_options,
                                uv_process_options_t* options) {
    Local<Context> context = env->context();
    Local<String> stdio_key = env->stdio_string();
    Local<Array> stdios =
        js_options->Get(context, stdio_key).ToLocalChecked().As<Array>();

    uint32_t len = stdios->Length();
    options->stdio = new uv_stdio_container_t[len];
    options->stdio_count = len;

    for (uint32_t i = 0; i < len; i++) {
      Local<Object> stdio =
          stdios->Get(context, i).ToLocalChecked().As<Object>();
      Local<Value> type =
          stdio->Get(context, env->type_string()).ToLocalChecked();

      if (type->StrictEquals(env->ignore_string())) {
        options->stdio[i].flags = UV_IGNORE;
      } else if (type->StrictEquals(env->pipe_string())) {
        options->stdio[i].flags = static_cast<uv_stdio_flags>(
            UV_CREATE_PIPE | UV_READABLE_PIPE | UV_WRITABLE_PIPE);
        options->stdio[i].data.stream = StreamForWrap(env, stdio);
      } else if (type->StrictEquals(env->overlapped_string())) {
        options->stdio[i].flags = static_cast<uv_stdio_flags>(
            UV_CREATE_PIPE | UV_READABLE_PIPE | UV_WRITABLE_PIPE |
            UV_OVERLAPPED_PIPE);
        options->stdio[i].data.stream = StreamForWrap(env, stdio);
      } else if (type->StrictEquals(env->wrap_string())) {
        options->stdio[i].flags = UV_INHERIT_STREAM;
        options->stdio[i].data.stream = StreamForWrap(env, stdio);
      } else {
        Local<String> fd_key = env->fd_string();
        Local<Value> fd_value = stdio->Get(context, fd_key).ToLocalChecked();
        CHECK(fd_value->IsNumber());
        int fd = static_cast<int>(fd_value.As<Integer>()->Value());
        options->stdio[i].flags = UV_INHERIT_FD;
        options->stdio[i].data.fd = fd;
      }
    }
  }

  static void Spawn(const FunctionCallbackInfo<Value>& args) {
    Environment* env = Environment::GetCurrent(args);
    Local<Context> context = env->context();
    ProcessWrap* wrap;
    ASSIGN_OR_RETURN_UNWRAP(&wrap, args.This());
    THROW_IF_INSUFFICIENT_PERMISSIONS(
        env, permission::PermissionScope::kChildProcess, "");
    int err = 0;

    Local<Object> js_options =
        args[0]->ToObject(env->context()).ToLocalChecked();

    uv_process_options_t options;
    memset(&options, 0, sizeof(uv_process_options_t));

    options.exit_cb = OnExit;

    // options.uid
    Local<Value> uid_v =
        js_options->Get(context, env->uid_string()).ToLocalChecked();
    if (!uid_v->IsUndefined() && !uid_v->IsNull()) {
      CHECK(uid_v->IsInt32());
      const int32_t uid = uid_v.As<Int32>()->Value();
      options.flags |= UV_PROCESS_SETUID;
      options.uid = static_cast<uv_uid_t>(uid);
    }

    // options.gid
    Local<Value> gid_v =
        js_options->Get(context, env->gid_string()).ToLocalChecked();
    if (!gid_v->IsUndefined() && !gid_v->IsNull()) {
      CHECK(gid_v->IsInt32());
      const int32_t gid = gid_v.As<Int32>()->Value();
      options.flags |= UV_PROCESS_SETGID;
      options.gid = static_cast<uv_gid_t>(gid);
    }

    // TODO(bnoordhuis) is this possible to do without mallocing ?

    // options.file
    Local<Value> file_v =
        js_options->Get(context, env->file_string()).ToLocalChecked();
    CHECK(file_v->IsString());
    node::Utf8Value file(env->isolate(), file_v);
    options.file = *file;

    // Undocumented feature of Win32 CreateProcess API allows spawning
    // batch files directly but is potentially insecure because arguments
    // are not escaped (and sometimes cannot be unambiguously escaped),
    // hence why they are rejected here.
#ifdef _WIN32
    if (IsWindowsBatchFile(options.file))
      err = UV_EINVAL;
#endif

    // options.args
    Local<Value> argv_v =
        js_options->Get(context, env->args_string()).ToLocalChecked();
    if (!argv_v.IsEmpty() && argv_v->IsArray()) {
      Local<Array> js_argv = argv_v.As<Array>();
      int argc = js_argv->Length();
      CHECK_LT(argc, INT_MAX);  // Check for overflow.

      // Heap allocate to detect errors. +1 is for nullptr.
      options.args = new char*[argc + 1];
      for (int i = 0; i < argc; i++) {
        node::Utf8Value arg(env->isolate(),
                            js_argv->Get(context, i).ToLocalChecked());
        options.args[i] = strdup(*arg);
        CHECK_NOT_NULL(options.args[i]);
      }
      options.args[argc] = nullptr;
    }

    // options.cwd
    Local<Value> cwd_v =
        js_options->Get(context, env->cwd_string()).ToLocalChecked();
    node::Utf8Value cwd(env->isolate(),
                        cwd_v->IsString() ? cwd_v : Local<Value>());
    if (cwd.length() > 0) {
      options.cwd = *cwd;
    }

    // options.env
    Local<Value> env_v =
        js_options->Get(context, env->env_pairs_string()).ToLocalChecked();
    if (!env_v.IsEmpty() && env_v->IsArray()) {
      Local<Array> env_opt = env_v.As<Array>();
      int envc = env_opt->Length();
      CHECK_LT(envc, INT_MAX);            // Check for overflow.
      options.env = new char*[envc + 1];  // Heap allocated to detect errors.
      for (int i = 0; i < envc; i++) {
        node::Utf8Value pair(env->isolate(),
                             env_opt->Get(context, i).ToLocalChecked());
        options.env[i] = strdup(*pair);
        CHECK_NOT_NULL(options.env[i]);
      }
      options.env[envc] = nullptr;
    }

    // options.stdio
    ParseStdioOptions(env, js_options, &options);

    // options.windowsHide
    Local<Value> hide_v =
        js_options->Get(context, env->windows_hide_string()).ToLocalChecked();

    if (hide_v->IsTrue()) {
      options.flags |= UV_PROCESS_WINDOWS_HIDE;
    }

    if (env->hide_console_windows()) {
      options.flags |= UV_PROCESS_WINDOWS_HIDE_CONSOLE;
    }

    // options.windows_verbatim_arguments
    Local<Value> wva_v =
        js_options->Get(context, env->windows_verbatim_arguments_string())
            .ToLocalChecked();

    if (wva_v->IsTrue()) {
      options.flags |= UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS;
    }

    // options.detached
    Local<Value> detached_v =
        js_options->Get(context, env->detached_string()).ToLocalChecked();

    if (detached_v->IsTrue()) {
      options.flags |= UV_PROCESS_DETACHED;
    }

    if (err == 0) {
      err = uv_spawn(env->event_loop(), &wrap->process_, &options);
      wrap->MarkAsInitialized();
    }

    if (err == 0) {
      CHECK_EQ(wrap->process_.data, wrap);
      wrap->object()->Set(context, env->pid_string(),
                          Integer::New(env->isolate(),
                                       wrap->process_.pid)).Check();
    }

    if (options.args) {
      for (int i = 0; options.args[i]; i++) free(options.args[i]);
      delete [] options.args;
    }

    if (options.env) {
      for (int i = 0; options.env[i]; i++) free(options.env[i]);
      delete [] options.env;
    }

    delete[] options.stdio;

    args.GetReturnValue().Set(err);
  }

  static void Kill(const FunctionCallbackInfo<Value>& args) {
    Environment* env = Environment::GetCurrent(args);
    ProcessWrap* wrap;
    ASSIGN_OR_RETURN_UNWRAP(&wrap, args.This());
    int signal = args[0]->Int32Value(env->context()).FromJust();
#ifdef _WIN32
    if (signal != SIGKILL && signal != SIGTERM && signal != SIGINT &&
        signal != SIGQUIT) {
      signal = SIGKILL;
    }
#endif
    int err = uv_process_kill(&wrap->process_, signal);
    args.GetReturnValue().Set(err);
  }

  static void OnExit(uv_process_t* handle,
                     int64_t exit_status,
                     int term_signal) {
    ProcessWrap* wrap = ContainerOf(&ProcessWrap::process_, handle);
    CHECK_EQ(&wrap->process_, handle);

    Environment* env = wrap->env();
    HandleScope handle_scope(env->isolate());
    Context::Scope context_scope(env->context());

    Local<Value> argv[] = {
      Number::New(env->isolate(), static_cast<double>(exit_status)),
      OneByteString(env->isolate(), signo_string(term_signal))
    };

    wrap->MakeCallback(env->onexit_string(), arraysize(argv), argv);
  }

  uv_process_t process_;
};


}  // anonymous namespace
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(process_wrap, node::ProcessWrap::Initialize)
NODE_BINDING_EXTERNAL_REFERENCE(process_wrap,
                                node::ProcessWrap::RegisterExternalReferences)
