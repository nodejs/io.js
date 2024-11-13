// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_TRACE_EVENT_H_
#define V8_HEAP_CPPGC_TRACE_EVENT_H_

#if !CPPGC_IS_STANDALONE
#include "src/tracing/trace-event.h"
using ConvertableToTraceFormat = v8::ConvertableToTraceFormat;
#else
// This is a subset of stc/tracing/trace-event.h required to support
// tracing in the cppgc standalone library using TracingController.

#include "include/cppgc/platform.h"
#include "src/base/atomicops.h"
#include "src/base/macros.h"
#include "src/tracing/trace-event-no-perfetto.h"

// This header file defines implementation details of how the trace macros in
// trace-event-no-erfetto.h collect and store trace events. Anything not
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
};

#define INTERNAL_TRACE_EVENT_CATEGORY_GROUP_ENABLED_FOR_RECORDING_MODE() \
  TRACE_EVENT_API_LOAD_CATEGORY_GROUP_ENABLED() &                        \
      (kEnabledForRecording_CategoryGroupEnabledFlags |                  \
       kEnabledForEventCallback_CategoryGroupEnabledFlags)

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
#define TRACE_EVENT_API_GET_CATEGORY_GROUP_ENABLED \
  platform->GetTracingController()->GetCategoryGroupEnabled

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
#define TRACE_EVENT_API_ADD_TRACE_EVENT cppgc::internal::AddTraceEventImpl

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
#define INTERNAL_TRACE_EVENT_UID3(a, b) cppgc_trace_event_unique_##a##b
#define INTERNAL_TRACE_EVENT_UID2(a, b) INTERNAL_TRACE_EVENT_UID3(a, b)
#define INTERNAL_TRACE_EVENT_UID(name_prefix) \
  INTERNAL_TRACE_EVENT_UID2(name_prefix, __LINE__)

// Implementation detail: internal macro to create static category.
// No barriers are needed, because this code is designed to operate safely
// even when the unsigned char* points to garbage data (which may be the case
// on processors without cache coherency).
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
  DCHECK_NOT_NULL(name);                                                     \
  do {                                                                       \
    cppgc::Platform* platform = stats_collector_->platform_;                 \
    INTERNAL_TRACE_EVENT_GET_CATEGORY_INFO(category_group);                  \
    if (INTERNAL_TRACE_EVENT_CATEGORY_GROUP_ENABLED_FOR_RECORDING_MODE()) {  \
      cppgc::internal::AddTraceEvent(                                        \
          phase, INTERNAL_TRACE_EVENT_UID(category_group_enabled), name,     \
          nullptr /* scope */, 0 /* id */, 0 /* bind_id */, flags, platform, \
          ##__VA_ARGS__);                                                    \
    }                                                                        \
  } while (false)

namespace cppgc {
namespace internal {

using ConvertableToTraceFormat = v8::ConvertableToTraceFormat;

class TraceEventHelper {
 public:
  V8_EXPORT_PRIVATE static TracingController* GetTracingController();
};

static V8_INLINE uint64_t AddTraceEventImpl(
    char phase, const uint8_t* category_group_enabled, const char* name,
    const char* scope, uint64_t id, uint64_t bind_id, int32_t num_args,
    const char** arg_names, const uint8_t* arg_types,
    const uint64_t* arg_values, unsigned int flags, Platform* platform) {
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
  TracingController* controller = platform->GetTracingController();
  return controller->AddTraceEvent(phase, category_group_enabled, name, scope,
                                   id, bind_id, num_args, arg_names, arg_types,
                                   arg_values, arg_convertables, flags);
}

// Define SetTraceValue for each allowed type. It stores the type and value
// in the return arguments. This allows this API to avoid declaring any
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
INTERNAL_DECLARE_SET_TRACE_VALUE(const char*, TRACE_VALUE_TYPE_STRING)
#undef INTERNAL_DECLARE_SET_TRACE_VALUE

// These AddTraceEvent template functions are defined here instead of in
// the macro, because the arg_values could be temporary objects, such as
// std::string. In order to store pointers to the internal c_str and pass
// through to the tracing API, the arg_values must live throughout these
// procedures.

static V8_INLINE uint64_t AddTraceEvent(char phase,
                                        const uint8_t* category_group_enabled,
                                        const char* name, const char* scope,
                                        uint64_t id, uint64_t bind_id,
                                        unsigned int flags,
                                        Platform* platform) {
  return TRACE_EVENT_API_ADD_TRACE_EVENT(
      phase, category_group_enabled, name, scope, id, bind_id, 0 /* num_args */,
      nullptr, nullptr, nullptr, flags, platform);
}

template <class ARG1_TYPE>
static V8_INLINE uint64_t AddTraceEvent(
    char phase, const uint8_t* category_group_enabled, const char* name,
    const char* scope, uint64_t id, uint64_t bind_id, unsigned int flags,
    Platform* platform, const char* arg1_name, ARG1_TYPE&& arg1_val) {
  const int num_args = 1;
  uint8_t arg_type;
  uint64_t arg_value;
  SetTraceValue(std::forward<ARG1_TYPE>(arg1_val), &arg_type, &arg_value);
  return TRACE_EVENT_API_ADD_TRACE_EVENT(
      phase, category_group_enabled, name, scope, id, bind_id, num_args,
      &arg1_name, &arg_type, &arg_value, flags, platform);
}

template <class ARG1_TYPE, class ARG2_TYPE>
static V8_INLINE uint64_t AddTraceEvent(
    char phase, const uint8_t* category_group_enabled, const char* name,
    const char* scope, uint64_t id, uint64_t bind_id, unsigned int flags,
    Platform* platform, const char* arg1_name, ARG1_TYPE&& arg1_val,
    const char* arg2_name, ARG2_TYPE&& arg2_val) {
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
      arg_names, arg_types, arg_values, flags, platform);
}

}  // namespace internal
}  // namespace cppgc

#endif  // !CPPGC_IS_STANDALONE

#endif  // V8_HEAP_CPPGC_TRACE_EVENT_H_
