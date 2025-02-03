#include "node_binding.h"
#include "node_external_reference.h"
#include "util-inl.h"
#include "v8-profiler.h"
#include "v8.h"

using v8::Array;
using v8::Context;
using v8::FunctionCallbackInfo;
using v8::Global;
using v8::Isolate;
using v8::Local;
using v8::LocalVector;
using v8::Object;
using v8::Value;

namespace node {
namespace internal_only_v8 {

class PrototypeChainHas : public v8::QueryObjectPredicate {
 public:
  PrototypeChainHas(Local<Context> context, Local<Object> search)
      : context_(context), search_(search) {}

  // What we can do in the filter can be quite limited, but looking up
  // the prototype chain is something that the inspector console API
  // queryObject() does so it is supported.
  bool Filter(Local<Object> object) override {
    Local<Context> creation_context;
    if (!object->GetCreationContext().ToLocal(&creation_context)) {
      return false;
    }
    if (creation_context != context_) {
      return false;
    }
    for (Local<Value> proto = object->GetPrototypeV2(); proto->IsObject();
         proto = proto.As<Object>()->GetPrototypeV2()) {
      if (search_ == proto) return true;
    }
    return false;
  }

 private:
  Local<Context> context_;
  Local<Object> search_;
};

void QueryObjects(const FunctionCallbackInfo<Value>& args) {
  CHECK_EQ(args.Length(), 1);
  Isolate* isolate = args.GetIsolate();
  if (!args[0]->IsObject()) {
    args.GetReturnValue().Set(Array::New(isolate));
    return;
  }
  Local<Object> proto = args[0].As<Object>();
  Local<Context> context = isolate->GetCurrentContext();
  PrototypeChainHas prototype_chain_has(context, proto.As<Object>());
  std::vector<Global<Object>> out;
  isolate->GetHeapProfiler()->QueryObjects(context, &prototype_chain_has, &out);
  LocalVector<Value> result(isolate);
  result.reserve(out.size());
  for (size_t i = 0; i < out.size(); ++i) {
    result.push_back(out[i].Get(isolate));
  }

  args.GetReturnValue().Set(Array::New(isolate, result.data(), result.size()));
}

void Initialize(Local<Object> target,
                Local<Value> unused,
                Local<Context> context,
                void* priv) {
  SetMethod(context, target, "queryObjects", QueryObjects);
}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(QueryObjects);
}

}  // namespace internal_only_v8
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(internal_only_v8,
                                    node::internal_only_v8::Initialize)
NODE_BINDING_EXTERNAL_REFERENCE(
    internal_only_v8, node::internal_only_v8::RegisterExternalReferences)
