#include <node.h>
#include <uv.h>
#include <v8.h>

static void InitModule(v8::Local<v8::Object> exports,
                       v8::Local<v8::Value> module_val,
                       v8::Local<v8::Context> context) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::Local<v8::Object> module = module_val.As<v8::Object>();
  module
      ->Set(context,
            v8::String::NewFromUtf8(isolate, "exports").ToLocalChecked(),
            v8::String::NewFromUtf8(isolate, "hello world").ToLocalChecked())
      .FromJust();
}

NODE_MODULE_CONTEXT_AWARE(Binding, InitModule)
