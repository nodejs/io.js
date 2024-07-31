#include "node.h"
#include "v8.h"

#include <assert.h>

using v8::Function;
using v8::FunctionCallbackInfo;
using v8::Isolate;
using v8::Local;
using v8::Object;
using v8::TryCatch;
using v8::Value;

namespace {

void MakeCallback(const FunctionCallbackInfo<Value>& args) {
  assert(args[0]->IsObject());
  assert(args[1]->IsFunction());
  Isolate* isolate = args.GetIsolate();
  Local<Object> recv = args[0].As<Object>();
  Local<Function> method = args[1].As<Function>();

  TryCatch try_catch(isolate);
  node::MakeCallback(isolate, recv, method, 0, nullptr,
                     node::async_context{0, 0});
  if (try_catch.HasCaught()) {
    try_catch.ReThrow();
  }
}

void Initialize(Local<Object> exports) {
  NODE_SET_METHOD(exports, "makeCallback", MakeCallback);
}

}  // anonymous namespace

NODE_MODULE(NODE_GYP_MODULE_NAME, Initialize)
