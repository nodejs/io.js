#include "node_wasm_web_api.h"

#include "memory_tracker-inl.h"
#include "node_errors.h"
#include "node_external_reference.h"

namespace node {
namespace wasm_web_api {

using v8::ArrayBuffer;
using v8::ArrayBufferView;
using v8::Context;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Isolate;
using v8::Local;
using v8::MaybeLocal;
using v8::Object;
using v8::Value;
using v8::WasmStreaming;

Local<Function> WasmStreamingObject::Initialize(Environment* env) {
  Local<Function> templ = env->wasm_streaming_object_constructor();
  if (!templ.IsEmpty()) {
    return templ;
  }

  Isolate* isolate = env->isolate();
  Local<FunctionTemplate> t = NewFunctionTemplate(isolate, New);
  t->InstanceTemplate()->SetInternalFieldCount(
      WasmStreamingObject::kInternalFieldCount);

  SetProtoMethod(isolate, t, "setURL", SetURL);
  SetProtoMethod(isolate, t, "push", Push);
  SetProtoMethod(isolate, t, "finish", Finish);
  SetProtoMethod(isolate, t, "abort", Abort);

  auto function = t->GetFunction(env->context()).ToLocalChecked();
  env->set_wasm_streaming_object_constructor(function);
  return function;
}

void WasmStreamingObject::RegisterExternalReferences(
    ExternalReferenceRegistry* registry) {
  registry->Register(New);
  registry->Register(Push);
  registry->Register(Finish);
  registry->Register(Abort);
}

void WasmStreamingObject::MemoryInfo(MemoryTracker* tracker) const {
  // v8::WasmStreaming is opaque. We assume that the size of the WebAssembly
  // module that is being compiled is roughly what V8 allocates (as in, off by
  // only a small factor).
  tracker->TrackFieldWithSize("streaming", wasm_size_);
}

MaybeLocal<Object> WasmStreamingObject::Create(
    Environment* env, std::shared_ptr<WasmStreaming> streaming) {
  Local<Function> ctor = Initialize(env);
  Local<Object> obj;
  if (!ctor->NewInstance(env->context(), 0, nullptr).ToLocal(&obj)) {
    return MaybeLocal<Object>();
  }

  CHECK(streaming);

  WasmStreamingObject* ptr = Unwrap<WasmStreamingObject>(obj);
  CHECK_NOT_NULL(ptr);
  ptr->streaming_ = streaming;
  ptr->wasm_size_ = 0;
  return obj;
}

void WasmStreamingObject::New(const FunctionCallbackInfo<Value>& args) {
  CHECK(args.IsConstructCall());
  Environment* env = Environment::GetCurrent(args);
  new WasmStreamingObject(env, args.This());
}

void WasmStreamingObject::SetURL(const FunctionCallbackInfo<Value>& args) {
  WasmStreamingObject* obj;
  ASSIGN_OR_RETURN_UNWRAP(&obj, args.This());
  CHECK(obj->streaming_);

  CHECK_EQ(args.Length(), 1);
  CHECK(args[0]->IsString());
  Utf8Value url(Environment::GetCurrent(args)->isolate(), args[0]);
  obj->streaming_->SetUrl(url.out(), url.length());
}

void WasmStreamingObject::Push(const FunctionCallbackInfo<Value>& args) {
  WasmStreamingObject* obj;
  ASSIGN_OR_RETURN_UNWRAP(&obj, args.This());
  CHECK(obj->streaming_);

  CHECK_EQ(args.Length(), 1);
  Local<Value> chunk = args[0];

  // The start of the memory section backing the ArrayBuffer(View), the offset
  // of the ArrayBuffer(View) within the memory section, and its size in bytes.
  const void* bytes;
  size_t offset;
  size_t size;

  if (chunk->IsArrayBufferView()) [[likely]] {
    Local<ArrayBufferView> view = chunk.As<ArrayBufferView>();
    bytes = view->Buffer()->Data();
    offset = view->ByteOffset();
    size = view->ByteLength();
  } else if (chunk->IsArrayBuffer()) [[likely]] {
    Local<ArrayBuffer> buffer = chunk.As<ArrayBuffer>();
    bytes = buffer->Data();
    offset = 0;
    size = buffer->ByteLength();
  } else {
    return node::THROW_ERR_INVALID_ARG_TYPE(
        Environment::GetCurrent(args),
        "chunk must be an ArrayBufferView or an ArrayBuffer");
  }

  // Forward the data to V8. Internally, V8 will make a copy.
  obj->streaming_->OnBytesReceived(static_cast<const uint8_t*>(bytes) + offset,
                                   size);
  obj->wasm_size_ += size;
}

void WasmStreamingObject::Finish(const FunctionCallbackInfo<Value>& args) {
  WasmStreamingObject* obj;
  ASSIGN_OR_RETURN_UNWRAP(&obj, args.This());
  CHECK(obj->streaming_);

  CHECK_EQ(args.Length(), 0);
  obj->streaming_->Finish();
}

void WasmStreamingObject::Abort(const FunctionCallbackInfo<Value>& args) {
  WasmStreamingObject* obj;
  ASSIGN_OR_RETURN_UNWRAP(&obj, args.This());
  CHECK(obj->streaming_);

  CHECK_EQ(args.Length(), 1);
  obj->streaming_->Abort(args[0]);
}

void StartStreamingCompilation(const FunctionCallbackInfo<Value>& info) {
  // V8 passes an instance of v8::WasmStreaming to this callback, which we can
  // use to pass the WebAssembly module bytes to V8 as we receive them.
  // Unfortunately, our fetch() implementation is a JavaScript dependency, so it
  // is difficult to implement the required logic here. Instead, we create a
  // a WasmStreamingObject that encapsulates v8::WasmStreaming and that we can
  // pass to the JavaScript implementation. The JavaScript implementation can
  // then push() bytes from the Response and eventually either finish() or
  // abort() the operation.

  // Create the wrapper object.
  std::shared_ptr<WasmStreaming> streaming =
      WasmStreaming::Unpack(info.GetIsolate(), info.Data());
  Environment* env = Environment::GetCurrent(info);
  Local<Object> obj;
  if (!WasmStreamingObject::Create(env, streaming).ToLocal(&obj)) {
    // A JavaScript exception is pending. Let V8 deal with it.
    return;
  }

  // V8 always passes one argument to this callback.
  CHECK_EQ(info.Length(), 1);

  // Prepare the JavaScript implementation for invocation. We will pass the
  // WasmStreamingObject as the first argument, followed by the argument that we
  // received from V8, i.e., the first argument passed to compileStreaming (or
  // instantiateStreaming).
  Local<Function> impl = env->wasm_streaming_compilation_impl();
  CHECK(!impl.IsEmpty());
  Local<Value> args[] = {obj, info[0]};

  // Hand control to the JavaScript implementation. It should never throw an
  // error, but if it does, we leave it to the calling V8 code to handle that
  // gracefully. Otherwise, we assert that the JavaScript function does not
  // return anything.
  MaybeLocal<Value> maybe_ret =
      impl->Call(env->context(), info.This(), 2, args);
  Local<Value> ret;
  CHECK_IMPLIES(maybe_ret.ToLocal(&ret), ret->IsUndefined());
}

// Called once by JavaScript during initialization.
void SetImplementation(const FunctionCallbackInfo<Value>& info) {
  Environment* env = Environment::GetCurrent(info);
  env->set_wasm_streaming_compilation_impl(info[0].As<Function>());
}

void Initialize(Local<Object> target,
                Local<Value>,
                Local<Context> context,
                void*) {
  SetMethod(context, target, "setImplementation", SetImplementation);
}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(SetImplementation);
  registry->Register(StartStreamingCompilation);
  WasmStreamingObject::RegisterExternalReferences(registry);
}

}  // namespace wasm_web_api
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(wasm_web_api,
                                    node::wasm_web_api::Initialize)
NODE_BINDING_EXTERNAL_REFERENCE(wasm_web_api,
                                node::wasm_web_api::RegisterExternalReferences)
