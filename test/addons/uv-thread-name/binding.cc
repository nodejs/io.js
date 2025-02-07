#include <node.h>
#include <uv.h>
#include <v8.h>

using v8::FunctionCallbackInfo;
using v8::Isolate;
using v8::String;
using v8::Value;

void GetThreadName(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  uv_thread_t thread;
  char thread_name[16];
#ifdef _WIN32
  /* uv_thread_self isn't defined for the main thread on Windows. */
  thread = GetCurrentThread();
#else
  thread = uv_thread_self();
#endif
  uv_thread_getname(&thread, thread_name, sizeof(thread_name));
  args.GetReturnValue().Set(
      String::NewFromUtf8(isolate, thread_name).ToLocalChecked());
}

void init(v8::Local<v8::Object> exports) {
  NODE_SET_METHOD(exports, "getThreadName", GetThreadName);
}

NODE_MODULE_CONTEXT_AWARE(NODE_GYP_MODULE_NAME, init)
