// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TRACING_TRACE_EVENT_H_
#define V8_TRACING_TRACE_EVENT_H_

#include <stddef.h>
#include <memory>

// Include first to ensure that V8_USE_PERFETTO can be defined before use.
#include "v8config.h"  // NOLINT(build/include_directory)

#if defined(V8_USE_PERFETTO)
#include "protos/perfetto/trace/track_event/debug_annotation.pbzero.h"
#include "src/tracing/trace-categories.h"
#else
#include "src/tracing/trace-event-no-perfetto.h"
#endif  // !defined(V8_USE_PERFETTO)

#include "include/v8-platform.h"
#include "src/base/atomicops.h"
#include "src/base/macros.h"

// This header file defines implementation details of how the trace macros in
// trace-event-no-perfetto.h collect and store trace events. Anything not
// implementation-specific should go in trace_macros_common.h instead of here.


// The pointer returned from GetCategoryGroupEnabled() points to a
// value with zero or more of the following bits. Used in this class only.
// The TRACE_EVENT macros should only use the value as a bool.
// These values must be in sync with macro values in trace_log.h in
// chromium.
enum CategoryGroupEnabledFlags {
  // Category group enabled for the recording mode.
  kEnabledForRecording_CategoryGroupEnabledFlags = 1 << 0,
  // Category group enabled by SetEventCallbackEnabled().
  kEnabledForEventCallback_CategoryGroupEnabledFlags = 1 << 2,
  // Category group enabled to export events to ETW.
  kEnabledForETWExport_CategoryGroupEnabledFlags = 1 << 3,
};

#if !defined(V8_USE_PERFETTO)

// TODO(petermarshall): Remove with the old tracing implementation - Perfetto
// copies const char* arguments by default.
// By default, const char* argument values are assumed to have long-lived scope
// and will not be copied. Use this macro to force a const char* to be copied.
#define TRACE_STR_COPY(str) v8::internal::tracing::TraceStringWithCopy(str)

// By default, trace IDs are eventually converted to a single 64-bit number. Use
// this macro to add a scope string.
#define TRACE_ID_WITH_SCOPE(scope, id) \
  v8::internal::tracing::TraceID::WithScope(scope, id)

#define INTERNAL_TRACE_EVENT_CATEGORY_GROUP_ENABLED_FOR_RECORDING_MODE() \
  TRACE_EVENT_API_LOAD_CATEGORY_GROUP_ENABLED() &                        \
      (kEnabledForRecording_CategoryGroupEnabledFlags |                  \
       kEnabledForEventCallback_CategoryGroupEnabledFlags)

// The following macro has no implementation, but it needs to exist since
// it gets called from scoped trace events. It cannot call UNIMPLEMENTED()
// since an empty implementation is a valid one.
#define INTERNAL_TRACE_MEMORY(category, name)

////////////////////////////////////////////////////////////////////////////////
// Implementation specific tracing API definitions.

// Get a pointer to the enabled state of the given trace category. Only
// long-lived literal strings should be given as the category group. The
// returned pointer can be held permanently in a local static for example. If
// the unsigned char is non-zero, tracing is enabled. If tracing is enabled,
// TRACE_EVENT_API_ADD_TRACE_EVENT can be called. It's OK if tracing is disabled
// between the load of the tracing state and the call to
// TRACE_EVENT_API_ADD_TRACE_EVENT, because this flag only provides an early out
// for best performance when tracing is disabled.
// const uint8_t*
//     TRACE_EVENT_API_GET_CATEGORY_GROUP_ENABLED(const char* category_group)
#define TRACE_EVENT_API_GET_CATEGORY_GROUP_ENABLED                \
  v8::internal::tracing::TraceEventHelper::GetTracingController() \
      ->GetCategoryGroupEnabled

// Get the number of times traces have been recorded. This is used to implement
// the TRACE_EVENT_IS_NEW_TRACE facility.
// unsigned int TRACE_EVENT_API_GET_NUM_TRACES_RECORDED()
#define TRACE_EVENT_API_GET_NUM_TRACES_RECORDED UNIMPLEMENTED()

// Add a trace event to the platform tracing system.
// uint64_t TRACE_EVENT_API_ADD_TRACE_EVENT(
//                    char phase,
//                    const uint8_t* category_group_enabled,
//                    const char* name,
//                    const char* scope,
//                    uint64_t id,
//                    uint64_t bind_id,
//                    int num_args,
//                    const char** arg_names,
//                    const uint8_t* arg_types,
//                    const uint64_t* arg_values,
//                    unsigned int flags)
#define TRACE_EVENT_API_ADD_TRACE_EVENT v8::internal::tracing::AddTraceEventImpl

// Add a trace event to the platform tracing system.
// uint64_t TRACE_EVENT_API_ADD_TRACE_EVENT_WITH_TIMESTAMP(
//                    char phase,
//                    const uint8_t* category_group_enabled,
//                    const char* name,
//                    const char* scope,
//                    uint64_t id,
//                    uint64_t bind_id,
//                    int num_args,
//                    const char** arg_names,
//                    const uint8_t* arg_types,
//                    const uint64_t* arg_values,
//                    unsigned int flags,
//                    int64_t timestamp)
#define TRACE_EVENT_API_ADD_TRACE_EVENT_WITH_TIMESTAMP \
  v8::internal::tracing::AddTraceEventWithTimestampImpl

// Set the duration field of a COMPLETE trace event.
// void TRACE_EVENT_API_UPDATE_TRACE_EVENT_DURATION(
//     const uint8_t* category_group_enabled,
//     const char* name,
//     uint64_t id)
#define TRACE_EVENT_API_UPDATE_TRACE_EVENT_DURATION               \
  v8::internal::tracing::TraceEventHelper::GetTracingController() \
      ->UpdateTraceEventDuration

// Defines atomic operations used internally by the tracing system.
// Acquire/release barriers are important here: crbug.com/1330114#c8.
#define TRACE_EVENT_API_ATOMIC_WORD v8::base::AtomicWord
#define TRACE_EVENT_API_ATOMIC_LOAD(var) v8::base::Acquire_Load(&(var))
#define TRACE_EVENT_API_ATOMIC_STORE(var, value) \
  v8::base::Release_Store(&(var), (value))
// This load can be Relaxed because it's reading the state of
// `category_group_enabled` and not inferring other variable's state from the
// result.
#define TRACE_EVENT_API_LOAD_CATEGORY_GROUP_ENABLED()                \
  v8::base::Relaxed_Load(reinterpret_cast<const v8::base::Atomic8*>( \
      INTERNAL_TRACE_EVENT_UID(category_group_enabled)))

////////////////////////////////////////////////////////////////////////////////

// Implementation detail: trace event macros create temporary variables
// to keep instrumentation overhead low. These macros give each temporary
// variable a unique name based on the line number to prevent name collisions.
#define INTERNAL_TRACE_EVENT_UID3(a, b) trace_event_unique_##a##b
#define INTERNAL_TRACE_EVENT_UID2(a, b) INTERNAL_TRACE_EVENT_UID3(a, b)
#define INTERNAL_TRACE_EVENT_UID(name_prefix) \
  INTERNAL_TRACE_EVENT_UID2(name_prefix, __LINE__)

// Implementation detail: internal macro to create static category.
// No barriers are needed, because this code is designed to operate safely
// even when the unsigned char* points to garbage data (which may be the case
// on processors without cache coherency).
// TODO(fmeawad): This implementation contradicts that we can have a different
// configuration for each isolate,
// https://code.google.com/p/v8/issues/detail?id=4563
#define INTERNAL_TRACE_EVENT_GET_CATEGORY_INFO_CUSTOM_VARIABLES(             \
    category_group, atomic, category_group_enabled)                          \
  category_group_enabled =                                                   \
      reinterpret_cast<const uint8_t*>(TRACE_EVENT_API_ATOMIC_LOAD(atomic)); \
  if (!category_group_enabled) {                                             \
    category_group_enabled =                                                 \
        TRACE_EVENT_API_GET_CATEGORY_GROUP_ENABLED(category_group);          \
    TRACE_EVENT_API_ATOMIC_STORE(                                            \
        atomic, reinterpret_cast<TRACE_EVENT_API_ATOMIC_WORD>(               \
                    category_group_enabled));                                \
  }

#define INTERNAL_TRACE_EVENT_GET_CATEGORY_INFO(category_group)             \
  static TRACE_EVENT_API_ATOMIC_WORD INTERNAL_TRACE_EVENT_UID(atomic) = 0; \
  const uint8_t* INTERNAL_TRACE_EVENT_UID(category_group_enabled);         \
  INTERNAL_TRACE_EVENT_GET_CATEGORY_INFO_CUSTOM_VARIABLES(                 \
      category_group, INTERNAL_TRACE_EVENT_UID(atomic),                    \
      INTERNAL_TRACE_EVENT_UID(category_group_enabled));

// Implementation detail: internal macro to create static category and add
// event if the category is enabled.
#define INTERNAL_TRACE_EVENT_ADD(phase, category_group, name, flags, ...)    \
  do {                                                                       \
    INTERNAL_TRACE_EVENT_GET_CATEGORY_INFO(category_group);                  \
    if (INTERNAL_TRACE_EVENT_CATEGORY_GROUP_ENABLED_FOR_RECORDING_MODE()) {  \
      v8::internal::tracing::AddTraceEvent(                                  \
          phase, INTERNAL_TRACE_EVENT_UID(category_group_enabled), name,     \
          v8::internal::tracing::kGlobalScope, v8::internal::tracing::kNoId, \
          v8::internal::tracing::kNoId, flags, ##__VA_ARGS__);               \
    }                                                                        \
  } while (false)

// Implementation detail: internal macro to create static category and add begin
// event if the category is enabled. Also adds the end event when the scope
// ends.
#define INTERNAL_TRACE_EVENT_ADD_SCOPED(category_group, name, ...)           \
  INTERNAL_TRACE_EVENT_GET_CATEGORY_INFO(category_group);                    \
  v8::internal::tracing::ScopedTracer INTERNAL_TRACE_EVENT_UID(tracer);      \
  if (INTERNAL_TRACE_EVENT_CATEGORY_GROUP_ENABLED_FOR_RECORDING_MODE()) {    \
    uint64_t h = v8::internal::tracing::AddTraceEvent(                       \
        TRACE_EVENT_PHASE_COMPLETE,                                          \
        INTERNAL_TRACE_EVENT_UID(category_group_enabled), name,              \
        v8::internal::tracing::kGlobalScope, v8::internal::tracing::kNoId,   \
        v8::internal::tracing::kNoId, TRACE_EVENT_FLAG_NONE, ##__VA_ARGS__); \
    INTERNAL_TRACE_EVENT_UID(tracer)                                         \
        .Initialize(INTERNAL_TRACE_EVENT_UID(category_group_enabled), name,  \
                    h);                                                      \
  }

#define INTERNAL_TRACE_EVENT_ADD_SCOPED_WITH_FLOW(category_group, name,     \
                                                  bind_id, flow_flags, ...) \
  INTERNAL_TRACE_EVENT_GET_CATEGORY_INFO(category_group);                   \
  v8::internal::tracing::ScopedTracer INTERNAL_TRACE_EVENT_UID(tracer);     \
  if (INTERNAL_TRACE_EVENT_CATEGORY_GROUP_ENABLED_FOR_RECORDING_MODE()) {   \
    unsigned int trace_event_flags = flow_flags;                            \
    v8::internal::tracing::TraceID trace_event_bind_id(bind_id,             \
                                                       &trace_event_flags); \
    uint64_t h = v8::internal::tracing::AddTraceEvent(                      \
        TRACE_EVENT_PHASE_COMPLETE,                                         \
        INTERNAL_TRACE_EVENT_UID(category_group_enabled), name,             \
        v8::internal::tracing::kGlobalScope, v8::internal::tracing::kNoId,  \
        trace_event_bind_id.raw_id(), trace_event_flags, ##__VA_ARGS__);    \
    INTERNAL_TRACE_EVENT_UID(tracer)                                        \
        .Initialize(INTERNAL_TRACE_EVENT_UID(category_group_enabled), name, \
                    h);                                                     \
  }

// Implementation detail: internal macro to create static category and add
// event if the category is enabled.
#define INTERNAL_TRACE_EVENT_ADD_WITH_ID(phase, category_group, name, id,      \
                                         flags, ...)                           \
  do {                                                                         \
    INTERNAL_TRACE_EVENT_GET_CATEGORY_INFO(category_group);                    \
    if (INTERNAL_TRACE_EVENT_CATEGORY_GROUP_ENABLED_FOR_RECORDING_MODE()) {    \
      unsigned int trace_event_flags = flags | TRACE_EVENT_FLAG_HAS_ID;        \
      v8::internal::tracing::TraceID trace_event_trace_id(id,                  \
                                                          &trace_event_flags); \
      v8::internal::tracing::AddTraceEvent(                                    \
          phase, INTERNAL_TRACE_EVENT_UID(category_group_enabled), name,       \
          trace_event_trace_id.scope(), trace_event_trace_id.raw_id(),         \
          v8::internal::tracing::kNoId, trace_event_flags, ##__VA_ARGS__);     \
    }                                                                          \
  } while (false)

// Adds a trace event with a given timestamp.
#define INTERNAL_TRACE_EVENT_ADD_WITH_TIMESTAMP(phase, category_group, name, \
                                                timestamp, flags, ...)       \
  do {                                                                       \
    INTERNAL_TRACE_EVENT_GET_CATEGORY_INFO(category_group);                  \
    if (INTERNAL_TRACE_EVENT_CATEGORY_GROUP_ENABLED_FOR_RECORDING_MODE()) {  \
      v8::internal::tracing::AddTraceEventWithTimestamp(                     \
          phase, INTERNAL_TRACE_EVENT_UID(category_group_enabled), name,     \
          v8::internal::tracing::kGlobalScope, v8::internal::tracing::kNoId, \
          v8::internal::tracing::kNoId, flags, timestamp, ##__VA_ARGS__);    \
    }                                                                        \
  } while (false)

// Adds a trace event with a given id and timestamp.
#define INTERNAL_TRACE_EVENT_ADD_WITH_ID_AND_TIMESTAMP(                        \
    phase, category_group, name, id, timestamp, flags, ...)                    \
  do {                                                                         \
    INTERNAL_TRACE_EVENT_GET_CATEGORY_INFO(category_group);                    \
    if (INTERNAL_TRACE_EVENT_CATEGORY_GROUP_ENABLED_FOR_RECORDING_MODE()) {    \
      unsigned int trace_event_flags = flags | TRACE_EVENT_FLAG_HAS_ID;        \
      v8::internal::tracing::TraceID trace_event_trace_id(id,                  \
                                                          &trace_event_flags); \
      v8::internal::tracing::AddTraceEventWithTimestamp(                       \
          phase, INTERNAL_TRACE_EVENT_UID(category_group_enabled), name,       \
          trace_event_trace_id.scope(), trace_event_trace_id.raw_id(),         \
          v8::internal::tracing::kNoId, trace_event_flags, timestamp,          \
          ##__VA_ARGS__);                                                      \
    }                                                                          \
  } while (false)

// Adds a trace event with a given id, thread_id, and timestamp. This redirects
// to INTERNAL_TRACE_EVENT_ADD_WITH_ID_AND_TIMESTAMP as we presently do not care
// about the thread id.
#define INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(            \
    phase, category_group, name, id, thread_id, timestamp, flags, ...) \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_AND_TIMESTAMP(                      \
      phase, category_group, name, id, timestamp, flags, ##__VA_ARGS__)

#define TRACE_EVENT_CALL_STATS_SCOPED(isolate, category_group, name) \
  INTERNAL_TRACE_EVENT_CALL_STATS_SCOPED(isolate, category_group, name)

#ifdef V8_RUNTIME_CALL_STATS
#define INTERNAL_TRACE_EVENT_CALL_STATS_SCOPED(isolate, category_group, name)  \
  INTERNAL_TRACE_EVENT_GET_CATEGORY_INFO(category_group);                      \
  v8::internal::tracing::CallStatsScopedTracer INTERNAL_TRACE_EVENT_UID(       \
      tracer);                                                                 \
  if (INTERNAL_TRACE_EVENT_CATEGORY_GROUP_ENABLED_FOR_RECORDING_MODE()) {      \
    INTERNAL_TRACE_EVENT_UID(tracer)                                           \
        .Initialize(isolate, INTERNAL_TRACE_EVENT_UID(category_group_enabled), \
                    name);                                                     \
  }
#else  // V8_RUNTIME_CALL_STATS
#define INTERNAL_TRACE_EVENT_CALL_STATS_SCOPED(isolate, category_group, name)
#endif  // V8_RUNTIME_CALL_STATS

namespace v8 {
namespace internal {

class Isolate;

namespace tracing {

// Specify these values when the corresponding argument of AddTraceEvent
// is not used.
const int kZeroNumArgs = 0;
const decltype(nullptr) kGlobalScope = nullptr;
const uint64_t kNoId = 0;

class TraceEventHelper {
 public:
  V8_EXPORT_PRIVATE static v8::TracingController* GetTracingController();
};

// TraceID encapsulates an ID that can either be an integer or pointer.
class TraceID {
 public:
  class WithScope {
   public:
    WithScope(const char* scope, uint64_t raw_id)
        : scope_(scope), raw_id_(raw_id) {}
    uint64_t raw_id() const { return raw_id_; }
    const char* scope() const { return scope_; }

   private:
    const char* scope_ = nullptr;
    uint64_t raw_id_;
  };

  TraceID(const void* raw_id, unsigned int* flags)
      : raw_id_(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(raw_id))) {}
  TraceID(uint64_t raw_id, unsigned int* flags) : raw_id_(raw_id) {
    (void)flags;
  }
  TraceID(unsigned int raw_id, unsigned int* flags) : raw_id_(raw_id) {
    (void)flags;
  }
  TraceID(uint16_t raw_id, unsigned int* flags) : raw_id_(raw_id) {
    (void)flags;
  }
  TraceID(unsigned char raw_id, unsigned int* flags) : raw_id_(raw_id) {
    (void)flags;
  }
  TraceID(int64_t raw_id, unsigned int* flags)
      : raw_id_(static_cast<uint64_t>(raw_id)) {
    (void)flags;
  }
  TraceID(int raw_id, unsigned int* flags)
      : raw_id_(static_cast<uint64_t>(raw_id)) {
    (void)flags;
  }
  TraceID(int16_t raw_id, unsigned int* flags)
      : raw_id_(static_cast<uint64_t>(raw_id)) {
    (void)flags;
  }
  TraceID(signed char raw_id, unsigned int* flags)
      : raw_id_(static_cast<uint64_t>(raw_id)) {
    (void)flags;
  }
  TraceID(WithScope scoped_id, unsigned int* flags)
      : scope_(scoped_id.scope()), raw_id_(scoped_id.raw_id()) {}

  uint64_t raw_id() const { return raw_id_; }
  const char* scope() const { return scope_; }

 private:
  const char* scope_ = nullptr;
  uint64_t raw_id_;
};

// Simple container for const char* that should be copied instead of retained.
class TraceStringWithCopy {
 public:
  explicit TraceStringWithCopy(const char* str) : str_(str) {}
  operator const char*() const { return str_; }

 private:
  const char* str_;
};

static V8_INLINE uint64_t AddTraceEventImpl(
    char phase, const uint8_t* category_group_enabled, const char* name,
    const char* scope, uint64_t id, uint64_t bind_id, int32_t num_args,
    const char** arg_names, const uint8_t* arg_types,
    const uint64_t* arg_values, unsigned int flags) {
  std::unique_ptr<ConvertableToTraceFormat> arg_convertables[2];
  if (num_args > 0 && arg_types[0] == TRACE_VALUE_TYPE_CONVERTABLE) {
    arg_convertables[0].reset(reinterpret_cast<ConvertableToTraceFormat*>(
        static_cast<intptr_t>(arg_values[0])));
  }
  if (num_args > 1 && arg_types[1] == TRACE_VALUE_TYPE_CONVERTABLE) {
    arg_convertables[1].reset(reinterpret_cast<ConvertableToTraceFormat*>(
        static_cast<intptr_t>(arg_values[1])));
  }
  DCHECK_LE(num_args, 2);
  v8::TracingController* controller =
      v8::internal::tracing::TraceEventHelper::GetTracingController();
  return controller->AddTraceEvent(phase, category_group_enabled, name, scope,
                                   id, bind_id, num_args, arg_names, arg_types,
                                   arg_values, arg_convertables, flags);
}

static V8_INLINE uint64_t AddTraceEventWithTimestampImpl(
    char phase, const uint8_t* category_group_enabled, const char* name,
    const char* scope, uint64_t id, uint64_t bind_id, int32_t num_args,
    const char** arg_names, const uint8_t* arg_types,
    const uint64_t* arg_values, unsigned int flags, int64_t timestamp) {
  std::unique_ptr<ConvertableToTraceFormat> arg_convertables[2];
  if (num_args > 0 && arg_types[0] == TRACE_VALUE_TYPE_CONVERTABLE) {
    arg_convertables[0].reset(reinterpret_cast<ConvertableToTraceFormat*>(
        static_cast<intptr_t>(arg_values[0])));
  }
  if (num_args > 1 && arg_types[1] == TRACE_VALUE_TYPE_CONVERTABLE) {
    arg_convertables[1].reset(reinterpret_cast<ConvertableToTraceFormat*>(
        static_cast<intptr_t>(arg_values[1])));
  }
  DCHECK_LE(num_args, 2);
  v8::TracingController* controller =
      v8::internal::tracing::TraceEventHelper::GetTracingController();
  return controller->AddTraceEventWithTimestamp(
      phase, category_group_enabled, name, scope, id, bind_id, num_args,
      arg_names, arg_types, arg_values, arg_convertables, flags, timestamp);
}

// Define SetTraceValue for each allowed type. It stores the type and
// value in the return arguments. This allows this API to avoid declaring any
// structures so that it is portable to third_party libraries.
// This is the base implementation for integer types (including bool) and enums.
template <typename T>
static V8_INLINE typename std::enable_if<
    std::is_integral<T>::value || std::is_enum<T>::value, void>::type
SetTraceValue(T arg, unsigned char* type, uint64_t* value) {
  *type = std::is_same<T, bool>::value
              ? TRACE_VALUE_TYPE_BOOL
              : std::is_signed<T>::value ? TRACE_VALUE_TYPE_INT
                                         : TRACE_VALUE_TYPE_UINT;
  *value = static_cast<uint64_t>(arg);
}

#define INTERNAL_DECLARE_SET_TRACE_VALUE(actual_type, value_type_id)        \
  static V8_INLINE void SetTraceValue(actual_type arg, unsigned char* type, \
                                      uint64_t* value) {                    \
    *type = value_type_id;                                                  \
    *value = 0;                                                             \
    static_assert(sizeof(arg) <= sizeof(*value));                           \
    memcpy(value, &arg, sizeof(arg));                                       \
  }
INTERNAL_DECLARE_SET_TRACE_VALUE(double, TRACE_VALUE_TYPE_DOUBLE)
INTERNAL_DECLARE_SET_TRACE_VALUE(const void*, TRACE_VALUE_TYPE_POINTER)
INTERNAL_DECLARE_SET_TRACE_VALUE(const char*, TRACE_VALUE_TYPE_STRING)
INTERNAL_DECLARE_SET_TRACE_VALUE(const TraceStringWithCopy&,
                                 TRACE_VALUE_TYPE_COPY_STRING)
#undef INTERNAL_DECLARE_SET_TRACE_VALUE

static V8_INLINE void SetTraceValue(ConvertableToTraceFormat* convertable_value,
                                    unsigned char* type, uint64_t* value) {
  *type = TRACE_VALUE_TYPE_CONVERTABLE;
  *value = static_cast<uint64_t>(reinterpret_cast<intptr_t>(convertable_value));
}

template <typename T>
static V8_INLINE typename std::enable_if<
    std::is_convertible<T*, ConvertableToTraceFormat*>::value>::type
SetTraceValue(std::unique_ptr<T> ptr, unsigned char* type, uint64_t* value) {
  SetTraceValue(ptr.release(), type, value);
}

// These AddTraceEvent template
// function is defined here instead of in the macro, because the arg_values
// could be temporary objects, such as std::string. In order to store
// pointers to the internal c_str and pass through to the tracing API,
// the arg_values must live throughout these procedures.

static V8_INLINE uint64_t AddTraceEvent(char phase,
                                        const uint8_t* category_group_enabled,
                                        const char* name, const char* scope,
                                        uint64_t id, uint64_t bind_id,
                                        unsigned int flags) {
  return TRACE_EVENT_API_ADD_TRACE_EVENT(phase, category_group_enabled, name,
                                         scope, id, bind_id, kZeroNumArgs,
                                         nullptr, nullptr, nullptr, flags);
}

template <class ARG1_TYPE>
static V8_INLINE uint64_t AddTraceEvent(
    char phase, const uint8_t* category_group_enabled, const char* name,
    const char* scope, uint64_t id, uint64_t bind_id, unsigned int flags,
    const char* arg1_name, ARG1_TYPE&& arg1_val) {
  const int num_args = 1;
  uint8_t arg_type;
  uint64_t arg_value;
  SetTraceValue(std::forward<ARG1_TYPE>(arg1_val), &arg_type, &arg_value);
  return TRACE_EVENT_API_ADD_TRACE_EVENT(
      phase, category_group_enabled, name, scope, id, bind_id, num_args,
      &arg1_name, &arg_type, &arg_value, flags);
}

template <class ARG1_TYPE, class ARG2_TYPE>
static V8_INLINE uint64_t AddTraceEvent(
    char phase, const uint8_t* category_group_enabled, const char* name,
    const char* scope, uint64_t id, uint64_t bind_id, unsigned int flags,
    const char* arg1_name, ARG1_TYPE&& arg1_val, const char* arg2_name,
    ARG2_TYPE&& arg2_val) {
  const int num_args = 2;
  const char* arg_names[2] = {arg1_name, arg2_name};
  unsigned char arg_types[2];
  uint64_t arg_values[2];
  SetTraceValue(std::forward<ARG1_TYPE>(arg1_val), &arg_types[0],
                &arg_values[0]);
  SetTraceValue(std::forward<ARG2_TYPE>(arg2_val), &arg_types[1],
                &arg_values[1]);
  return TRACE_EVENT_API_ADD_TRACE_EVENT(
      phase, category_group_enabled, name, scope, id, bind_id, num_args,
      arg_names, arg_types, arg_values, flags);
}

static V8_INLINE uint64_t AddTraceEventWithTimestamp(
    char phase, const uint8_t* category_group_enabled, const char* name,
    const char* scope, uint64_t id, uint64_t bind_id, unsigned int flags,
    int64_t timestamp) {
  return TRACE_EVENT_API_ADD_TRACE_EVENT_WITH_TIMESTAMP(
      phase, category_group_enabled, name, scope, id, bind_id, kZeroNumArgs,
      nullptr, nullptr, nullptr, flags, timestamp);
}

template <class ARG1_TYPE>
static V8_INLINE uint64_t AddTraceEventWithTimestamp(
    char phase, const uint8_t* category_group_enabled, const char* name,
    const char* scope, uint64_t id, uint64_t bind_id, unsigned int flags,
    int64_t timestamp, const char* arg1_name, ARG1_TYPE&& arg1_val) {
  const int num_args = 1;
  uint8_t arg_type;
  uint64_t arg_value;
  SetTraceValue(std::forward<ARG1_TYPE>(arg1_val), &arg_type, &arg_value);
  return TRACE_EVENT_API_ADD_TRACE_EVENT_WITH_TIMESTAMP(
      phase, category_group_enabled, name, scope, id, bind_id, num_args,
      &arg1_name, &arg_type, &arg_value, flags, timestamp);
}

template <class ARG1_TYPE, class ARG2_TYPE>
static V8_INLINE uint64_t AddTraceEventWithTimestamp(
    char phase, const uint8_t* category_group_enabled, const char* name,
    const char* scope, uint64_t id, uint64_t bind_id, unsigned int flags,
    int64_t timestamp, const char* arg1_name, ARG1_TYPE&& arg1_val,
    const char* arg2_name, ARG2_TYPE&& arg2_val) {
  const int num_args = 2;
  const char* arg_names[2] = {arg1_name, arg2_name};
  unsigned char arg_types[2];
  uint64_t arg_values[2];
  SetTraceValue(std::forward<ARG1_TYPE>(arg1_val), &arg_types[0],
                &arg_values[0]);
  SetTraceValue(std::forward<ARG2_TYPE>(arg2_val), &arg_types[1],
                &arg_values[1]);
  return TRACE_EVENT_API_ADD_TRACE_EVENT_WITH_TIMESTAMP(
      phase, category_group_enabled, name, scope, id, bind_id, num_args,
      arg_names, arg_types, arg_values, flags, timestamp);
}

// Used by TRACE_EVENTx macros. Do not use directly.
class ScopedTracer {
 public:
  // Note: members of data_ intentionally left uninitialized. See Initialize.
  ScopedTracer() : p_data_(nullptr) {}

  ~ScopedTracer() {
    if (p_data_ && base::Relaxed_Load(reinterpret_cast<const base::Atomic8*>(
                       data_.category_group_enabled))) {
      TRACE_EVENT_API_UPDATE_TRACE_EVENT_DURATION(
          data_.category_group_enabled, data_.name, data_.event_handle);
    }
  }

  void Initialize(const uint8_t* category_group_enabled, const char* name,
                  uint64_t event_handle) {
    data_.category_group_enabled = category_group_enabled;
    data_.name = name;
    data_.event_handle = event_handle;
    p_data_ = &data_;
  }

 private:
  // This Data struct workaround is to avoid initializing all the members
  // in Data during construction of this object, since this object is always
  // constructed, even when tracing is disabled. If the members of Data were
  // members of this class instead, compiler warnings occur about potential
  // uninitialized accesses.
  struct Data {
    const uint8_t* category_group_enabled;
    const char* name;
    uint64_t event_handle;
  };
  Data* p_data_;
  Data data_;
};

#ifdef V8_RUNTIME_CALL_STATS
// Do not use directly.
class CallStatsScopedTracer {
 public:
  CallStatsScopedTracer() : p_data_(nullptr) {}
  ~CallStatsScopedTracer() {
    if (V8_UNLIKELY(p_data_ && *data_.category_group_enabled)) {
      AddEndTraceEvent();
    }
  }

  void Initialize(v8::internal::Isolate* isolate,
                  const uint8_t* category_group_enabled, const char* name);

 private:
  void AddEndTraceEvent();
  struct Data {
    const uint8_t* category_group_enabled;
    const char* name;
    v8::internal::Isolate* isolate;
  };
  bool has_parent_scope_;
  Data* p_data_;
  Data data_;
};
#endif  // V8_RUNTIME_CALL_STATS

}  // namespace tracing
}  // namespace internal
}  // namespace v8

#else  // defined(V8_USE_PERFETTO)

#ifdef V8_RUNTIME_CALL_STATS

#define TRACE_EVENT_CALL_STATS_SCOPED(isolate, category, name)             \
  struct PERFETTO_UID(ScopedEvent) {                                       \
    struct ScopedStats {                                                   \
      ScopedStats(v8::internal::Isolate* isolate_arg, int) {               \
        TRACE_EVENT_BEGIN(category, name, [&](perfetto::EventContext) {    \
          isolate_ = isolate_arg;                                          \
          internal::RuntimeCallStats* table =                              \
              isolate_->counters()->runtime_call_stats();                  \
          has_parent_scope_ = table->InUse();                              \
          if (!has_parent_scope_) table->Reset();                          \
        });                                                                \
      }                                                                    \
      ~ScopedStats() {                                                     \
        TRACE_EVENT_END(category, [&](perfetto::EventContext ctx) {        \
          if (!has_parent_scope_ && isolate_) {                            \
            /* TODO(skyostil): Write as typed event instead of JSON */     \
            auto value = v8::tracing::TracedValue::Create();               \
            isolate_->counters()->runtime_call_stats()->Dump(value.get()); \
            auto annotation = ctx.event()->add_debug_annotations();        \
            annotation->set_name("runtime-call-stats");                    \
            value->Add(annotation);                                        \
          }                                                                \
        });                                                                \
      }                                                                    \
      v8::internal::Isolate* isolate_ = nullptr;                           \
      bool has_parent_scope_ = false;                                      \
    } stats;                                                               \
  } PERFETTO_UID(scoped_event) {                                           \
    { isolate, 0 }                                                         \
  }
#else  // V8_RUNTIME_CALL_STATS
#define TRACE_EVENT_CALL_STATS_SCOPED(isolate, category, name)
#endif  // V8_RUNTIME_CALL_STATS
#endif  // defined(V8_USE_PERFETTO)

#endif  // V8_TRACING_TRACE_EVENT_H_
