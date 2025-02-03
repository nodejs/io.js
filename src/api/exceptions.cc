// This file contains implementation of error APIs exposed in node.h

#include "env-inl.h"
#include "node.h"
#include "node_errors.h"
#include "util-inl.h"
#include "uv.h"
#include "v8.h"

#include <cstring>

namespace node {

using v8::Context;
using v8::Exception;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::Object;
using v8::String;
using v8::Value;

Local<Value> ErrnoException(Isolate* isolate,
                            int errorno,
                            const char* syscall,
                            const char* msg,
                            const char* path) {
  Environment* env = Environment::GetCurrent(isolate);
  CHECK_NOT_NULL(env);

  Local<Value> e;
  Local<String> estring = OneByteString(isolate, errors::errno_string(errorno));
  if (msg == nullptr || msg[0] == '\0') {
    msg = strerror(errorno);
  }
  Local<String> message = OneByteString(isolate, msg);

  Local<String> cons =
      String::Concat(isolate, estring, FIXED_ONE_BYTE_STRING(isolate, ", "));
  cons = String::Concat(isolate, cons, message);

  Local<String> path_string;
  if (path != nullptr) {
    // FIXME(bnoordhuis) It's questionable to interpret the file path as UTF-8.
    path_string = String::NewFromUtf8(isolate, path).ToLocalChecked();
  }

  if (path_string.IsEmpty() == false) {
    cons = String::Concat(isolate, cons, FIXED_ONE_BYTE_STRING(isolate, " '"));
    cons = String::Concat(isolate, cons, path_string);
    cons = String::Concat(isolate, cons, FIXED_ONE_BYTE_STRING(isolate, "'"));
  }
  e = Exception::Error(cons);

  Local<Context> context = env->context();
  Local<Object> obj = e.As<Object>();
  obj->Set(context,
           env->errno_string(),
           Integer::New(isolate, errorno)).Check();
  obj->Set(context, env->code_string(), estring).Check();

  if (path_string.IsEmpty() == false) {
    obj->Set(context, env->path_string(), path_string).Check();
  }

  if (syscall != nullptr) {
    obj->Set(context,
             env->syscall_string(),
             OneByteString(isolate, syscall)).Check();
  }

  return e;
}

static Local<String> StringFromPath(Isolate* isolate, const char* path) {
#ifdef _WIN32
  if (strncmp(path, "\\\\?\\UNC\\", 8) == 0) {
    return String::Concat(
        isolate,
        FIXED_ONE_BYTE_STRING(isolate, "\\\\"),
        String::NewFromUtf8(isolate, path + 8).ToLocalChecked());
  } else if (strncmp(path, "\\\\?\\", 4) == 0) {
    return String::NewFromUtf8(isolate, path + 4).ToLocalChecked();
  }
#endif

  return String::NewFromUtf8(isolate, path).ToLocalChecked();
}


Local<Value> UVException(Isolate* isolate,
                         int errorno,
                         const char* syscall,
                         const char* msg,
                         const char* path,
                         const char* dest) {
  Environment* env = Environment::GetCurrent(isolate);
  CHECK_NOT_NULL(env);

  if (!msg || !msg[0])
    msg = uv_strerror(errorno);

  Local<String> js_code = OneByteString(isolate, uv_err_name(errorno));
  Local<String> js_syscall = OneByteString(isolate, syscall);
  Local<String> js_path;
  Local<String> js_dest;

  Local<String> js_msg = js_code;
  js_msg =
      String::Concat(isolate, js_msg, FIXED_ONE_BYTE_STRING(isolate, ": "));
  js_msg = String::Concat(isolate, js_msg, OneByteString(isolate, msg));
  js_msg =
      String::Concat(isolate, js_msg, FIXED_ONE_BYTE_STRING(isolate, ", "));
  js_msg = String::Concat(isolate, js_msg, js_syscall);

  if (path != nullptr) {
    js_path = StringFromPath(isolate, path);

    js_msg =
        String::Concat(isolate, js_msg, FIXED_ONE_BYTE_STRING(isolate, " '"));
    js_msg = String::Concat(isolate, js_msg, js_path);
    js_msg =
        String::Concat(isolate, js_msg, FIXED_ONE_BYTE_STRING(isolate, "'"));
  }

  if (dest != nullptr) {
    js_dest = StringFromPath(isolate, dest);

    js_msg = String::Concat(
        isolate, js_msg, FIXED_ONE_BYTE_STRING(isolate, " -> '"));
    js_msg = String::Concat(isolate, js_msg, js_dest);
    js_msg =
        String::Concat(isolate, js_msg, FIXED_ONE_BYTE_STRING(isolate, "'"));
  }

  Local<Object> e =
    Exception::Error(js_msg)->ToObject(isolate->GetCurrentContext())
      .ToLocalChecked();

  Local<Context> context = env->context();
  e->Set(context,
         env->errno_string(),
         Integer::New(isolate, errorno)).Check();
  e->Set(context, env->code_string(), js_code).Check();
  e->Set(context, env->syscall_string(), js_syscall).Check();
  if (!js_path.IsEmpty())
    e->Set(context, env->path_string(), js_path).Check();
  if (!js_dest.IsEmpty())
    e->Set(context, env->dest_string(), js_dest).Check();

  return e;
}

#ifdef _WIN32
// Does about the same as strerror(),
// but supports all windows error messages
static const char* winapi_strerror(const int errorno, bool* must_free) {
  char* errmsg = nullptr;

  FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                     FORMAT_MESSAGE_IGNORE_INSERTS,
                 nullptr,
                 errorno,
                 MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                 reinterpret_cast<LPSTR>(&errmsg),
                 0,
                 nullptr);

  if (errmsg) {
    *must_free = true;

    // Remove trailing newlines
    for (int i = strlen(errmsg) - 1;
         i >= 0 && (errmsg[i] == '\n' || errmsg[i] == '\r');
         i--) {
      errmsg[i] = '\0';
    }

    return errmsg;
  } else {
    // FormatMessage failed
    *must_free = false;
    return "Unknown error";
  }
}

Local<Value> WinapiErrnoException(Isolate* isolate,
                                  int errorno,
                                  const char* syscall,
                                  const char* msg,
                                  const char* path) {
  Environment* env = Environment::GetCurrent(isolate);
  CHECK_NOT_NULL(env);
  Local<Value> e;
  bool must_free = false;
  if (!msg || !msg[0]) {
    msg = winapi_strerror(errorno, &must_free);
  }
  Local<String> message = OneByteString(isolate, msg);

  if (path) {
    Local<String> cons1 =
        String::Concat(isolate, message, FIXED_ONE_BYTE_STRING(isolate, " '"));
    Local<String> cons2 = String::Concat(
        isolate,
        cons1,
        String::NewFromUtf8(isolate, path).ToLocalChecked());
    Local<String> cons3 =
        String::Concat(isolate, cons2, FIXED_ONE_BYTE_STRING(isolate, "'"));
    e = Exception::Error(cons3);
  } else {
    e = Exception::Error(message);
  }

  Local<Context> context = env->context();
  Local<Object> obj = e.As<Object>();
  obj->Set(context, env->errno_string(), Integer::New(isolate, errorno))
      .Check();

  if (path != nullptr) {
    obj->Set(context,
             env->path_string(),
             String::NewFromUtf8(isolate, path).ToLocalChecked())
        .Check();
  }

  if (syscall != nullptr) {
    obj->Set(context,
             env->syscall_string(),
             OneByteString(isolate, syscall))
        .Check();
  }

  if (must_free) {
    LocalFree(const_cast<char*>(msg));
  }

  return e;
}
#endif  // _WIN32

// Implement the legacy name exposed in node.h. This has not been in fact
// fatal any more, as the user can handle the exception in the
// TryCatch by listening to `uncaughtException`.
// TODO(joyeecheung): deprecate it in favor of a more accurate name.
void FatalException(Isolate* isolate, const v8::TryCatch& try_catch) {
  errors::TriggerUncaughtException(isolate, try_catch);
}

}  // namespace node
