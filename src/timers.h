#ifndef SRC_TIMERS_H_
#define SRC_TIMERS_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include <cinttypes>
#include "node_snapshotable.h"

namespace node {
class ExternalReferenceRegistry;

namespace timers {
class BindingData : public SnapshotableObject {
 public:
  BindingData(Realm* env, v8::Local<v8::Object> obj);

  using InternalFieldInfo = InternalFieldInfoBase;

  SET_BINDING_ID(timers_binding_data)
  SERIALIZABLE_OBJECT_METHODS()

  SET_NO_MEMORY_INFO()
  SET_SELF_SIZE(BindingData)
  SET_MEMORY_INFO_NAME(BindingData)

  static void SetupTimers(const v8::FunctionCallbackInfo<v8::Value>& args);

  static void SlowGetLibuvNow(const v8::FunctionCallbackInfo<v8::Value>& args);
  static double FastGetLibuvNow(v8::Local<v8::Object> unused,
                                v8::Local<v8::Object> receiver);
  static double GetLibuvNowImpl(BindingData* data);

  static void SlowScheduleTimer(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  static void FastScheduleTimer(v8::Local<v8::Object> unused,
                                v8::Local<v8::Object> receiver,
                                int64_t duration);
  static void ScheduleTimerImpl(BindingData* data, int64_t duration);

  static void SlowToggleTimerRef(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  static void FastToggleTimerRef(v8::Local<v8::Object> unused,
                                 v8::Local<v8::Object> receiver,
                                 bool ref);
  static void ToggleTimerRefImpl(BindingData* data, bool ref);

  static void SlowToggleImmediateRef(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  static void FastToggleImmediateRef(v8::Local<v8::Object> unused,
                                     v8::Local<v8::Object> receiver,
                                     bool ref);
  static void ToggleImmediateRefImpl(BindingData* data, bool ref);

  static void CreatePerIsolateProperties(IsolateData* isolate_data,
                                         v8::Local<v8::ObjectTemplate> target);
  static void CreatePerContextProperties(v8::Local<v8::Object> target,
                                         v8::Local<v8::Value> unused,
                                         v8::Local<v8::Context> context,
                                         void* priv);
  static void RegisterTimerExternalReferences(
      ExternalReferenceRegistry* registry);

 private:
  static v8::CFunction fast_get_libuv_now_;
  static v8::CFunction fast_schedule_timers_;
  static v8::CFunction fast_toggle_timer_ref_;
  static v8::CFunction fast_toggle_immediate_ref_;
};

}  // namespace timers

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_TIMERS_H_
