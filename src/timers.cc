#include "timers.h"
#include "env-inl.h"
#include "node_external_reference.h"
#include "util-inl.h"
#include "v8.h"

#include <cstdint>

namespace node {
namespace timers {

using v8::Context;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::Isolate;
using v8::Local;
using v8::Number;
using v8::Object;
using v8::ObjectTemplate;
using v8::Value;

void BindingData::SetupTimers(const FunctionCallbackInfo<Value>& args) {
  CHECK(args[0]->IsFunction());
  CHECK(args[1]->IsFunction());
  auto env = Environment::GetCurrent(args);

  env->set_immediate_callback_function(args[0].As<Function>());
  env->set_timers_callback_function(args[1].As<Function>());
}

void BindingData::SlowGetLibuvNow(const FunctionCallbackInfo<Value>& args) {
  double now = GetLibuvNowImpl(Realm::GetBindingData<BindingData>(args));
  args.GetReturnValue().Set(Number::New(args.GetIsolate(), now));
}

double BindingData::FastGetLibuvNow(Local<Object> unused,
                                    Local<Object> receiver) {
  return GetLibuvNowImpl(FromJSObject<BindingData>(receiver));
}

double BindingData::GetLibuvNowImpl(BindingData* data) {
  return static_cast<double>(data->env()->GetNowUint64());
}

void BindingData::SlowScheduleTimer(const FunctionCallbackInfo<Value>& args) {
  int64_t duration =
      args[0]->IntegerValue(args.GetIsolate()->GetCurrentContext()).FromJust();
  ScheduleTimerImpl(Realm::GetBindingData<BindingData>(args), duration);
}

void BindingData::FastScheduleTimer(Local<Object> unused,
                                    Local<Object> receiver,
                                    int64_t duration) {
  ScheduleTimerImpl(FromJSObject<BindingData>(receiver), duration);
}

void BindingData::ScheduleTimerImpl(BindingData* data, int64_t duration) {
  data->env()->ScheduleTimer(duration);
}

void BindingData::SlowToggleTimerRef(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  ToggleTimerRefImpl(Realm::GetBindingData<BindingData>(args),
                     args[0]->IsTrue());
}

void BindingData::FastToggleTimerRef(Local<Object> unused,
                                     Local<Object> receiver,
                                     bool ref) {
  ToggleTimerRefImpl(FromJSObject<BindingData>(receiver), ref);
}

void BindingData::ToggleTimerRefImpl(BindingData* data, bool ref) {
  data->env()->ToggleTimerRef(ref);
}

void BindingData::SlowToggleImmediateRef(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  ToggleImmediateRefImpl(Realm::GetBindingData<BindingData>(args),
                         args[0]->IsTrue());
}

void BindingData::FastToggleImmediateRef(Local<Object> unused,
                                         Local<Object> receiver,
                                         bool ref) {
  ToggleImmediateRefImpl(FromJSObject<BindingData>(receiver), ref);
}

void BindingData::ToggleImmediateRefImpl(BindingData* data, bool ref) {
  data->env()->ToggleImmediateRef(ref);
}

BindingData::BindingData(Realm* realm, Local<Object> object)
    : SnapshotableObject(realm, object, type_int) {}

bool BindingData::PrepareForSerialization(Local<Context> context,
                                          v8::SnapshotCreator* creator) {
  // Return true because we need to maintain the reference to the binding from
  // JS land.
  return true;
}

InternalFieldInfoBase* BindingData::Serialize(int index) {
  DCHECK_IS_SNAPSHOT_SLOT(index);
  InternalFieldInfo* info =
      InternalFieldInfoBase::New<InternalFieldInfo>(type());
  return info;
}

void BindingData::Deserialize(Local<Context> context,
                              Local<Object> holder,
                              int index,
                              InternalFieldInfoBase* info) {
  DCHECK_IS_SNAPSHOT_SLOT(index);
  v8::HandleScope scope(context->GetIsolate());
  Realm* realm = Realm::GetCurrent(context);
  // Recreate the buffer in the constructor.
  BindingData* binding = realm->AddBindingData<BindingData>(holder);
  CHECK_NOT_NULL(binding);
}

v8::CFunction BindingData::fast_get_libuv_now_(
    v8::CFunction::Make(FastGetLibuvNow));
v8::CFunction BindingData::fast_schedule_timers_(
    v8::CFunction::Make(FastScheduleTimer));
v8::CFunction BindingData::fast_toggle_timer_ref_(
    v8::CFunction::Make(FastToggleTimerRef));
v8::CFunction BindingData::fast_toggle_immediate_ref_(
    v8::CFunction::Make(FastToggleImmediateRef));

void BindingData::CreatePerIsolateProperties(IsolateData* isolate_data,
                                             Local<ObjectTemplate> target) {
  Isolate* isolate = isolate_data->isolate();

  SetMethod(isolate, target, "setupTimers", SetupTimers);
  SetFastMethod(
      isolate, target, "getLibuvNow", SlowGetLibuvNow, &fast_get_libuv_now_);
  SetFastMethod(isolate,
                target,
                "scheduleTimer",
                SlowScheduleTimer,
                &fast_schedule_timers_);
  SetFastMethod(isolate,
                target,
                "toggleTimerRef",
                SlowToggleTimerRef,
                &fast_toggle_timer_ref_);
  SetFastMethod(isolate,
                target,
                "toggleImmediateRef",
                SlowToggleImmediateRef,
                &fast_toggle_immediate_ref_);
}

void BindingData::CreatePerContextProperties(Local<Object> target,
                                             Local<Value> unused,
                                             Local<Context> context,
                                             void* priv) {
  Realm* realm = Realm::GetCurrent(context);
  Environment* env = realm->env();
  BindingData* const binding_data = realm->AddBindingData<BindingData>(target);
  if (binding_data == nullptr) return;

  // TODO(joyeecheung): move these into BindingData.
  target
      ->Set(context,
            FIXED_ONE_BYTE_STRING(realm->isolate(), "immediateInfo"),
            env->immediate_info()->fields().GetJSArray())
      .Check();

  target
      ->Set(context,
            FIXED_ONE_BYTE_STRING(realm->isolate(), "timeoutInfo"),
            env->timeout_info().GetJSArray())
      .Check();
}

void BindingData::RegisterTimerExternalReferences(
    ExternalReferenceRegistry* registry) {
  registry->Register(SetupTimers);

  registry->Register(SlowGetLibuvNow);
  registry->Register(FastGetLibuvNow);
  registry->Register(fast_get_libuv_now_.GetTypeInfo());

  registry->Register(SlowScheduleTimer);
  registry->Register(FastScheduleTimer);
  registry->Register(fast_schedule_timers_.GetTypeInfo());

  registry->Register(SlowToggleTimerRef);
  registry->Register(FastToggleTimerRef);
  registry->Register(fast_toggle_timer_ref_.GetTypeInfo());

  registry->Register(SlowToggleImmediateRef);
  registry->Register(FastToggleImmediateRef);
  registry->Register(fast_toggle_immediate_ref_.GetTypeInfo());
}

}  // namespace timers

}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(
    timers, node::timers::BindingData::CreatePerContextProperties)
NODE_BINDING_PER_ISOLATE_INIT(
    timers, node::timers::BindingData::CreatePerIsolateProperties)
NODE_BINDING_EXTERNAL_REFERENCE(
    timers, node::timers::BindingData::RegisterTimerExternalReferences)
