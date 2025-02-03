#include <node.h>
#include <uv.h>
#include <v8.h>

static void Method(const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  args.GetReturnValue().Set(
      v8::String::NewFromUtf8(isolate, "hello world").ToLocalChecked());
}

static void InitModule(v8::Local<v8::Object> exports,
                       v8::Local<v8::Value> module,
                       v8::Local<v8::Context> context) {
  NODE_SET_METHOD(exports, "default", Method);
}

NODE_MODULE_CONTEXT_AWARE(Binding, InitModule)
