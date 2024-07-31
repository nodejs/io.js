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

#include "node_stat_watcher.h"
#include "async_wrap-inl.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "node_external_reference.h"
#include "node_file-inl.h"
#include "permission/permission.h"
#include "util-inl.h"

#include <cstring>
#include <cstdlib>

namespace node {

using v8::Context;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::Object;
using v8::ObjectTemplate;
using v8::Uint32;
using v8::Value;

void StatWatcher::CreatePerIsolateProperties(IsolateData* isolate_data,
                                             Local<ObjectTemplate> target) {
  Isolate* isolate = isolate_data->isolate();

  Local<FunctionTemplate> t = NewFunctionTemplate(isolate, StatWatcher::New);
  t->InstanceTemplate()->SetInternalFieldCount(
      StatWatcher::kInternalFieldCount);
  t->Inherit(HandleWrap::GetConstructorTemplate(isolate_data));
  SetProtoMethod(isolate, t, "start", StatWatcher::Start);

  SetConstructorFunction(isolate, target, "StatWatcher", t);
}

void StatWatcher::RegisterExternalReferences(
    ExternalReferenceRegistry* registry) {
  registry->Register(StatWatcher::New);
  registry->Register(StatWatcher::Start);
}

StatWatcher::StatWatcher(fs::BindingData* binding_data,
                         Local<Object> wrap,
                         bool use_bigint)
    : HandleWrap(binding_data->env(),
                 wrap,
                 reinterpret_cast<uv_handle_t*>(&watcher_),
                 AsyncWrap::PROVIDER_STATWATCHER),
      use_bigint_(use_bigint),
      binding_data_(binding_data) {
  CHECK_EQ(0, uv_fs_poll_init(env()->event_loop(), &watcher_));
}


void StatWatcher::Callback(uv_fs_poll_t* handle,
                           int status,
                           const uv_stat_t* prev,
                           const uv_stat_t* curr) {
  StatWatcher* wrap = ContainerOf(&StatWatcher::watcher_, handle);
  Environment* env = wrap->env();
  HandleScope handle_scope(env->isolate());
  Context::Scope context_scope(env->context());

  Local<Value> arr = fs::FillGlobalStatsArray(
      wrap->binding_data_.get(), wrap->use_bigint_, curr);
  USE(fs::FillGlobalStatsArray(
      wrap->binding_data_.get(), wrap->use_bigint_, prev, true));

  Local<Value> argv[2] = { Integer::New(env->isolate(), status), arr };
  wrap->MakeCallback(env->onchange_string(), arraysize(argv), argv);
}


void StatWatcher::New(const FunctionCallbackInfo<Value>& args) {
  CHECK(args.IsConstructCall());
  fs::BindingData* binding_data = Realm::GetBindingData<fs::BindingData>(args);
  new StatWatcher(binding_data, args.This(), args[0]->IsTrue());
}

// wrap.start(filename, interval)
void StatWatcher::Start(const FunctionCallbackInfo<Value>& args) {
  CHECK_EQ(args.Length(), 2);

  StatWatcher* wrap;
  ASSIGN_OR_RETURN_UNWRAP(&wrap, args.This());
  CHECK(!uv_is_active(wrap->GetHandle()));

  node::Utf8Value path(args.GetIsolate(), args[0]);
  CHECK_NOT_NULL(*path);
  THROW_IF_INSUFFICIENT_PERMISSIONS(
      wrap->env(),
      permission::PermissionScope::kFileSystemRead,
      path.ToStringView());

  CHECK(args[1]->IsUint32());
  const uint32_t interval = args[1].As<Uint32>()->Value();

  // Note that uv_fs_poll_start does not return ENOENT, we are handling
  // mostly memory errors here.
  const int err = uv_fs_poll_start(&wrap->watcher_, Callback, *path, interval);
  if (err != 0) {
    args.GetReturnValue().Set(err);
  }
}

}  // namespace node
