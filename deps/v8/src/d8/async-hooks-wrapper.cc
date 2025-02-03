// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/d8/async-hooks-wrapper.h"

#include "include/v8-function.h"
#include "include/v8-local-handle.h"
#include "include/v8-primitive.h"
#include "include/v8-template.h"
#include "src/api/api-inl.h"
#include "src/api/api.h"
#include "src/d8/d8.h"
#include "src/execution/isolate-inl.h"
#include "src/objects/managed-inl.h"

namespace v8 {

namespace {
std::shared_ptr<AsyncHooksWrap> UnwrapHook(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* v8_isolate = info.GetIsolate();
  HandleScope scope(v8_isolate);
  Local<Object> hook = info.This();

  AsyncHooks* hooks = PerIsolateData::Get(v8_isolate)->GetAsyncHooks();

  if (!hooks->async_hook_ctor.Get(v8_isolate)->HasInstance(hook)) {
    v8_isolate->ThrowError(
        "Invalid 'this' passed instead of AsyncHooks instance");
    return nullptr;
  }

  i::Handle<i::Object> handle = Utils::OpenHandle(*hook->GetInternalField(0));
  return Cast<i::Managed<AsyncHooksWrap>>(handle)->get();
}

void EnableHook(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  auto wrap = UnwrapHook(info);
  if (wrap) wrap->Enable();
}

void DisableHook(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  auto wrap = UnwrapHook(info);
  if (wrap) wrap->Disable();
}

}  // namespace

AsyncHooks::AsyncHooks(v8::Isolate* v8_isolate) : v8_isolate_(v8_isolate) {
  AsyncContext ctx;
  ctx.execution_async_id = 1;
  ctx.trigger_async_id = 0;
  asyncContexts.push(ctx);
  current_async_id = 1;

  HandleScope handle_scope(v8_isolate_);

  async_hook_ctor.Reset(v8_isolate_, FunctionTemplate::New(v8_isolate_));
  async_hook_ctor.Get(v8_isolate_)
      ->SetClassName(String::NewFromUtf8Literal(v8_isolate_, "AsyncHook"));

  async_hooks_templ.Reset(v8_isolate_,
                          async_hook_ctor.Get(v8_isolate_)->InstanceTemplate());
  async_hooks_templ.Get(v8_isolate_)->SetInternalFieldCount(1);
  async_hooks_templ.Get(v8_isolate_)
      ->Set(v8_isolate_, "enable",
            FunctionTemplate::New(v8_isolate_, EnableHook));
  async_hooks_templ.Get(v8_isolate_)
      ->Set(v8_isolate_, "disable",
            FunctionTemplate::New(v8_isolate_, DisableHook));

  async_id_symbol.Reset(v8_isolate_, Private::New(v8_isolate_));
  trigger_id_symbol.Reset(v8_isolate_, Private::New(v8_isolate_));

  v8_isolate_->SetPromiseHook(ShellPromiseHook);
}

AsyncHooks::~AsyncHooks() {
  v8_isolate_->SetPromiseHook(nullptr);
  async_wraps_.clear();
}

void AsyncHooksWrap::Enable() { enabled_ = true; }

void AsyncHooksWrap::Disable() { enabled_ = false; }

v8::Local<v8::Function> AsyncHooksWrap::init_function() const {
  return init_function_.Get(isolate_);
}
void AsyncHooksWrap::set_init_function(v8::Local<v8::Function> value) {
  init_function_.Reset(isolate_, value);
}
v8::Local<v8::Function> AsyncHooksWrap::before_function() const {
  return before_function_.Get(isolate_);
}
void AsyncHooksWrap::set_before_function(v8::Local<v8::Function> value) {
  before_function_.Reset(isolate_, value);
}
v8::Local<v8::Function> AsyncHooksWrap::after_function() const {
  return after_function_.Get(isolate_);
}
void AsyncHooksWrap::set_after_function(v8::Local<v8::Function> value) {
  after_function_.Reset(isolate_, value);
}
v8::Local<v8::Function> AsyncHooksWrap::promiseResolve_function() const {
  return promiseResolve_function_.Get(isolate_);
}
void AsyncHooksWrap::set_promiseResolve_function(
    v8::Local<v8::Function> value) {
  promiseResolve_function_.Reset(isolate_, value);
}

async_id_t AsyncHooks::GetExecutionAsyncId() const {
  return asyncContexts.top().execution_async_id;
}

async_id_t AsyncHooks::GetTriggerAsyncId() const {
  return asyncContexts.top().trigger_async_id;
}

Local<Object> AsyncHooks::CreateHook(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* v8_isolate = info.GetIsolate();
  EscapableHandleScope handle_scope(v8_isolate);

  if (v8_isolate->IsExecutionTerminating()) {
    return Local<Object>();
  }

  Local<Context> currentContext = v8_isolate->GetCurrentContext();

  if (info.Length() != 1 || !info[0]->IsObject()) {
    v8_isolate->ThrowError("Invalid arguments passed to createHook");
    return Local<Object>();
  }

  std::shared_ptr<AsyncHooksWrap> wrap =
      std::make_shared<AsyncHooksWrap>(v8_isolate);

  Local<Object> fn_obj = info[0].As<Object>();

  v8::TryCatch try_catch(v8_isolate);
#define SET_HOOK_FN(name)                                                     \
  MaybeLocal<Value> name##_maybe_func = fn_obj->Get(                          \
      currentContext, String::NewFromUtf8Literal(v8_isolate, #name));         \
  Local<Value> name##_func;                                                   \
  if (name##_maybe_func.ToLocal(&name##_func) && name##_func->IsFunction()) { \
    wrap->set_##name##_function(name##_func.As<Function>());                  \
  } else {                                                                    \
    try_catch.ReThrow();                                                      \
  }

  SET_HOOK_FN(init);
  SET_HOOK_FN(before);
  SET_HOOK_FN(after);
  SET_HOOK_FN(promiseResolve);
#undef SET_HOOK_FN

  Local<Object> obj = async_hooks_templ.Get(v8_isolate)
                          ->NewInstance(currentContext)
                          .ToLocalChecked();
  i::Handle<i::Object> managed = i::Managed<AsyncHooksWrap>::From(
      reinterpret_cast<i::Isolate*>(v8_isolate), sizeof(AsyncHooksWrap), wrap);
  obj->SetInternalField(0, Utils::ToLocal(managed));

  async_wraps_.push_back(std::move(wrap));

  return handle_scope.Escape(obj);
}

void AsyncHooks::ShellPromiseHook(PromiseHookType type, Local<Promise> promise,
                                  Local<Value> parent) {
  v8::Isolate* v8_isolate = promise->GetIsolate();
  AsyncHooks* hooks = PerIsolateData::Get(v8_isolate)->GetAsyncHooks();
  if (v8_isolate->IsExecutionTerminating() || hooks->skip_after_termination_) {
    hooks->skip_after_termination_ = true;
    return;
  }
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(v8_isolate);

  HandleScope handle_scope(v8_isolate);
  i::Handle<i::Object> exception;
  // Keep track of any previously thrown exception.
  if (i_isolate->has_exception()) {
    exception = handle(i_isolate->exception(), i_isolate);
  }
  {
    TryCatch try_catch(v8_isolate);
    try_catch.SetVerbose(true);

    Local<Context> currentContext = v8_isolate->GetCurrentContext();
    DCHECK(!currentContext.IsEmpty());

    if (type == PromiseHookType::kInit) {
      ++hooks->current_async_id;
      Local<Integer> async_id =
          Integer::New(v8_isolate, hooks->current_async_id);
      CHECK(!promise
                 ->HasPrivate(currentContext,
                              hooks->async_id_symbol.Get(v8_isolate))
                 .ToChecked());
      promise->SetPrivate(currentContext,
                          hooks->async_id_symbol.Get(v8_isolate), async_id);

      if (parent->IsPromise()) {
        Local<Promise> parent_promise = parent.As<Promise>();
        Local<Value> parent_async_id =
            parent_promise
                ->GetPrivate(currentContext,
                             hooks->async_id_symbol.Get(v8_isolate))
                .ToLocalChecked();
        promise->SetPrivate(currentContext,
                            hooks->trigger_id_symbol.Get(v8_isolate),
                            parent_async_id);
      } else {
        CHECK(parent->IsUndefined());
        promise->SetPrivate(currentContext,
                            hooks->trigger_id_symbol.Get(v8_isolate),
                            Integer::New(v8_isolate, 0));
      }
    } else if (type == PromiseHookType::kBefore) {
      AsyncContext ctx;
      ctx.execution_async_id =
          promise
              ->GetPrivate(currentContext,
                           hooks->async_id_symbol.Get(v8_isolate))
              .ToLocalChecked()
              .As<Integer>()
              ->Value();
      ctx.trigger_async_id =
          promise
              ->GetPrivate(currentContext,
                           hooks->trigger_id_symbol.Get(v8_isolate))
              .ToLocalChecked()
              .As<Integer>()
              ->Value();
      hooks->asyncContexts.push(ctx);
    } else if (type == PromiseHookType::kAfter) {
      hooks->asyncContexts.pop();
    }
    if (!i::StackLimitCheck{i_isolate}.HasOverflowed()) {
      for (size_t i = 0; i < hooks->async_wraps_.size(); ++i) {
        std::shared_ptr<AsyncHooksWrap> wrap = hooks->async_wraps_[i];
        PromiseHookDispatch(type, promise, parent, *wrap, hooks);
        if (try_catch.HasCaught()) break;
      }
      if (try_catch.HasCaught()) Shell::ReportException(v8_isolate, try_catch);
    }
  }
  if (!exception.is_null()) {
    i_isolate->set_exception(*exception);
  }
}

void AsyncHooks::PromiseHookDispatch(PromiseHookType type,
                                     Local<Promise> promise,
                                     Local<Value> parent,
                                     const AsyncHooksWrap& wrap,
                                     AsyncHooks* hooks) {
  if (!wrap.IsEnabled()) return;
  v8::Isolate* v8_isolate = hooks->v8_isolate_;
  if (v8_isolate->IsExecutionTerminating()) return;
  HandleScope handle_scope(v8_isolate);

  Local<Value> rcv = Undefined(v8_isolate);
  Local<Context> context = v8_isolate->GetCurrentContext();
  Local<Value> async_id =
      promise->GetPrivate(context, hooks->async_id_symbol.Get(v8_isolate))
          .ToLocalChecked();
  Local<Value> args[1] = {async_id};

  switch (type) {
    case PromiseHookType::kInit:
      if (!wrap.init_function().IsEmpty()) {
        Local<Value> initArgs[4] = {
            async_id, String::NewFromUtf8Literal(v8_isolate, "PROMISE"),
            promise
                ->GetPrivate(context, hooks->trigger_id_symbol.Get(v8_isolate))
                .ToLocalChecked(),
            promise};
        USE(wrap.init_function()->Call(context, rcv, 4, initArgs));
      }
      break;
    case PromiseHookType::kBefore:
      if (!wrap.before_function().IsEmpty()) {
        USE(wrap.before_function()->Call(context, rcv, 1, args));
      }
      break;
    case PromiseHookType::kAfter:
      if (!wrap.after_function().IsEmpty()) {
        USE(wrap.after_function()->Call(context, rcv, 1, args));
      }
      break;
    case PromiseHookType::kResolve:
      if (!wrap.promiseResolve_function().IsEmpty()) {
        USE(wrap.promiseResolve_function()->Call(context, rcv, 1, args));
      }
  }
}

}  // namespace v8
