// Include uv.h and v8.h ahead of node.h to verify that node.h doesn't need to
// be included first. Disable clang-format as it will sort the include lists.
// clang-format off
#include <uv.h>
#include <v8.h>
#include <node.h>
// clang-format on

static void Method(const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  args.GetReturnValue().Set(
      v8::String::NewFromUtf8(isolate, "world").ToLocalChecked());
}

static void InitModule(v8::Local<v8::Object> exports,
                       v8::Local<v8::Value> module,
                       v8::Local<v8::Context> context) {
  NODE_SET_METHOD(exports, "hello", Method);
}

NODE_MODULE(NODE_GYP_MODULE_NAME, InitModule)
