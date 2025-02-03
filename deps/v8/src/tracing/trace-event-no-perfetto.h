// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TRACING_TRACE_EVENT_NO_PERFETTO_H_
#define V8_TRACING_TRACE_EVENT_NO_PERFETTO_H_

// This is the legacy implementation of tracing macros. There have been two
// concurrent implementations within chromium after perfetto was introduced.
// As of 2024-05, V8 is the only remaining customer of the legacy implementation
// and moved the legacy part from its previous location at
// chromium/src/base/trace_event/common/trace_event_common.h into V8 directly.

// New projects wishing to enable tracing should use the Perfetto SDK. See
// https://perfetto.dev/docs/instrumentation/tracing-sdk for details.

// Check that nobody includes this file directly. Clients are supposed to
// include the surrounding "trace_event.h" of their project instead.
#if defined(TRACE_EVENT0)
#error "Another copy of this file has already been included."
#endif

// This will mark the trace event as disabled by default. The user will need
// to explicitly enable the event.
#define TRACE_DISABLED_BY_DEFAULT(name) "disabled-by-default-" name

// Records a pair of begin and end events called "name" for the current
// scope, with 0, 1 or 2 associated arguments. If the category is not
// enabled, then this does nothing.
// - category and name strings must have application lifetime (statics or
//   literals). They may not include " chars.
#define TRACE_EVENT0(category_group, name)    \
  INTERNAL_TRACE_EVENT_ADD_SCOPED(category_group, name)
#define TRACE_EVENT_WITH_FLOW0(category_group, name, bind_id, flow_flags)  \
  INTERNAL_TRACE_EVENT_ADD_SCOPED_WITH_FLOW(category_group, name, bind_id, \
                                            flow_flags)
#define TRACE_EVENT1(category_group, name, arg1_name, arg1_val) \
  INTERNAL_TRACE_EVENT_ADD_SCOPED(category_group, name, arg1_name, arg1_val)
#define TRACE_EVENT_WITH_FLOW1(category_group, name, bind_id, flow_flags,  \
                               arg1_name, arg1_val)                        \
  INTERNAL_TRACE_EVENT_ADD_SCOPED_WITH_FLOW(category_group, name, bind_id, \
                                            flow_flags, arg1_name, arg1_val)
#define TRACE_EVENT2(category_group, name, arg1_name, arg1_val, arg2_name,   \
                     arg2_val)                                               \
  INTERNAL_TRACE_EVENT_ADD_SCOPED(category_group, name, arg1_name, arg1_val, \
                                  arg2_name, arg2_val)
#define TRACE_EVENT_WITH_FLOW2(category_group, name, bind_id, flow_flags,    \
                               arg1_name, arg1_val, arg2_name, arg2_val)     \
  INTERNAL_TRACE_EVENT_ADD_SCOPED_WITH_FLOW(category_group, name, bind_id,   \
                                            flow_flags, arg1_name, arg1_val, \
                                            arg2_name, arg2_val)

// Records a single event called "name" immediately, with 0, 1 or 2
// associated arguments. If the category is not enabled, then this
// does nothing.
// - category and name strings must have application lifetime (statics or
//   literals). They may not include " chars.
#define TRACE_EVENT_INSTANT0(category_group, name, scope)                   \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_INSTANT, category_group, name, \
                           TRACE_EVENT_FLAG_NONE | scope)
#define TRACE_EVENT_INSTANT1(category_group, name, scope, arg1_name, arg1_val) \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_INSTANT, category_group, name,    \
                           TRACE_EVENT_FLAG_NONE | scope, arg1_name, arg1_val)
#define TRACE_EVENT_INSTANT2(category_group, name, scope, arg1_name, arg1_val, \
                             arg2_name, arg2_val)                              \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_INSTANT, category_group, name,    \
                           TRACE_EVENT_FLAG_NONE | scope, arg1_name, arg1_val, \
                           arg2_name, arg2_val)
#define TRACE_EVENT_COPY_INSTANT0(category_group, name, scope)              \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_INSTANT, category_group, name, \
                           TRACE_EVENT_FLAG_COPY | scope)
#define TRACE_EVENT_COPY_INSTANT1(category_group, name, scope, arg1_name,   \
                                  arg1_val)                                 \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_INSTANT, category_group, name, \
                           TRACE_EVENT_FLAG_COPY | scope, arg1_name, arg1_val)
#define TRACE_EVENT_COPY_INSTANT2(category_group, name, scope, arg1_name,      \
                                  arg1_val, arg2_name, arg2_val)               \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_INSTANT, category_group, name,    \
                           TRACE_EVENT_FLAG_COPY | scope, arg1_name, arg1_val, \
                           arg2_name, arg2_val)
#define TRACE_EVENT_INSTANT_WITH_FLAGS0(category_group, name, scope_and_flags) \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_INSTANT, category_group, name,    \
                           scope_and_flags)
#define TRACE_EVENT_INSTANT_WITH_FLAGS1(category_group, name, scope_and_flags, \
                                        arg1_name, arg1_val)                   \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_INSTANT, category_group, name,    \
                           scope_and_flags, arg1_name, arg1_val)

#define TRACE_EVENT_INSTANT_WITH_TIMESTAMP0(category_group, name, scope, \
                                            timestamp)                   \
  INTERNAL_TRACE_EVENT_ADD_WITH_TIMESTAMP(                               \
      TRACE_EVENT_PHASE_INSTANT, category_group, name, timestamp,        \
      TRACE_EVENT_FLAG_NONE | scope)

#define TRACE_EVENT_INSTANT_WITH_TIMESTAMP1(category_group, name, scope,  \
                                            timestamp, arg_name, arg_val) \
  INTERNAL_TRACE_EVENT_ADD_WITH_TIMESTAMP(                                \
      TRACE_EVENT_PHASE_INSTANT, category_group, name, timestamp,         \
      TRACE_EVENT_FLAG_NONE | scope, arg_name, arg_val)

// Records a single BEGIN event called "name" immediately, with 0, 1 or 2
// associated arguments. If the category is not enabled, then this
// does nothing.
// - category and name strings must have application lifetime (statics or
//   literals). They may not include " chars.
#define TRACE_EVENT_BEGIN0(category_group, name)                          \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_BEGIN, category_group, name, \
                           TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_BEGIN1(category_group, name, arg1_name, arg1_val)     \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_BEGIN, category_group, name, \
                           TRACE_EVENT_FLAG_NONE, arg1_name, arg1_val)
#define TRACE_EVENT_BEGIN2(category_group, name, arg1_name, arg1_val,     \
                           arg2_name, arg2_val)                           \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_BEGIN, category_group, name, \
                           TRACE_EVENT_FLAG_NONE, arg1_name, arg1_val,    \
                           arg2_name, arg2_val)
#define TRACE_EVENT_BEGIN_WITH_FLAGS0(category_group, name, flags) \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_BEGIN, category_group, name, flags)
#define TRACE_EVENT_BEGIN_WITH_FLAGS1(category_group, name, flags, arg1_name, \
                                      arg1_val)                               \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_BEGIN, category_group, name,     \
                           flags, arg1_name, arg1_val)
#define TRACE_EVENT_COPY_BEGIN2(category_group, name, arg1_name, arg1_val, \
                                arg2_name, arg2_val)                       \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_BEGIN, category_group, name,  \
                           TRACE_EVENT_FLAG_COPY, arg1_name, arg1_val,     \
                           arg2_name, arg2_val)

// Similar to TRACE_EVENT_BEGINx but with a custom |timestamp| provided.
// - |id| is used to match the _BEGIN event with the _END event.
//   Events are considered to match if their category_group, name and id values
//   all match. |id| must either be a pointer or an integer value up to 64 bits.
//   If it's a pointer, the bits will be xored with a hash of the process ID so
//   that the same pointer on two different processes will not collide.
// - |timestamp| must be non-null or it crashes. Use DCHECK(timestamp) before
//   calling this to detect an invalid timestamp even when tracing is not
//   enabled, as the commit queue doesn't run all tests with tracing enabled.
// Note: This legacy macro is deprecated. It should not be used in new code.
//       If thread_id is different from current thread id, it will result into
//       DCHECK failure. This note is also applicable to `_COPY` and `_END`
//       variant of this macro.
#define TRACE_EVENT_BEGIN_WITH_ID_TID_AND_TIMESTAMP0(category_group, name, id, \
                                                     thread_id, timestamp)     \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                          \
      TRACE_EVENT_PHASE_ASYNC_BEGIN, category_group, name, id, thread_id,      \
      timestamp, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_COPY_BEGIN_WITH_ID_TID_AND_TIMESTAMP0(                \
    category_group, name, id, thread_id, timestamp)                       \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                     \
      TRACE_EVENT_PHASE_ASYNC_BEGIN, category_group, name, id, thread_id, \
      timestamp, TRACE_EVENT_FLAG_COPY)
#define TRACE_EVENT_COPY_BEGIN_WITH_ID_TID_AND_TIMESTAMP1(                \
    category_group, name, id, thread_id, timestamp, arg1_name, arg1_val)  \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                     \
      TRACE_EVENT_PHASE_ASYNC_BEGIN, category_group, name, id, thread_id, \
      timestamp, TRACE_EVENT_FLAG_COPY, arg1_name, arg1_val)
#define TRACE_EVENT_COPY_BEGIN_WITH_ID_TID_AND_TIMESTAMP2(                \
    category_group, name, id, thread_id, timestamp, arg1_name, arg1_val,  \
    arg2_name, arg2_val)                                                  \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                     \
      TRACE_EVENT_PHASE_ASYNC_BEGIN, category_group, name, id, thread_id, \
      timestamp, TRACE_EVENT_FLAG_COPY, arg1_name, arg1_val, arg2_name,   \
      arg2_val)

// Records a single END event for "name" immediately. If the category
// is not enabled, then this does nothing.
// - category and name strings must have application lifetime (statics or
//   literals). They may not include " chars.
#define TRACE_EVENT_END0(category_group, name)                          \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_END, category_group, name, \
                           TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_END1(category_group, name, arg1_name, arg1_val)     \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_END, category_group, name, \
                           TRACE_EVENT_FLAG_NONE, arg1_name, arg1_val)
#define TRACE_EVENT_END2(category_group, name, arg1_name, arg1_val, arg2_name, \
                         arg2_val)                                             \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_END, category_group, name,        \
                           TRACE_EVENT_FLAG_NONE, arg1_name, arg1_val,         \
                           arg2_name, arg2_val)
#define TRACE_EVENT_END_WITH_FLAGS0(category_group, name, flags) \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_END, category_group, name, flags)
#define TRACE_EVENT_END_WITH_FLAGS1(category_group, name, flags, arg1_name,    \
                                    arg1_val)                                  \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_END, category_group, name, flags, \
                           arg1_name, arg1_val)
#define TRACE_EVENT_COPY_END2(category_group, name, arg1_name, arg1_val, \
                              arg2_name, arg2_val)                       \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_END, category_group, name,  \
                           TRACE_EVENT_FLAG_COPY, arg1_name, arg1_val,   \
                           arg2_name, arg2_val)

// Adds a trace event with the given |name| and |timestamp|. |timestamp| must be
// non-null or it crashes. Use DCHECK(timestamp) before calling this to detect
// an invalid timestamp even when tracing is not enabled, as the commit queue
// doesn't run all tests with tracing enabled.
#define TRACE_EVENT_MARK_WITH_TIMESTAMP0(category_group, name, timestamp) \
  INTERNAL_TRACE_EVENT_ADD_WITH_TIMESTAMP(                                \
      TRACE_EVENT_PHASE_MARK, category_group, name, timestamp,            \
      TRACE_EVENT_FLAG_NONE)

#define TRACE_EVENT_MARK_WITH_TIMESTAMP1(category_group, name, timestamp, \
                                         arg1_name, arg1_val)             \
  INTERNAL_TRACE_EVENT_ADD_WITH_TIMESTAMP(                                \
      TRACE_EVENT_PHASE_MARK, category_group, name, timestamp,            \
      TRACE_EVENT_FLAG_NONE, arg1_name, arg1_val)

#define TRACE_EVENT_MARK_WITH_TIMESTAMP2(                                      \
    category_group, name, timestamp, arg1_name, arg1_val, arg2_name, arg2_val) \
  INTERNAL_TRACE_EVENT_ADD_WITH_TIMESTAMP(                                     \
      TRACE_EVENT_PHASE_MARK, category_group, name, timestamp,                 \
      TRACE_EVENT_FLAG_NONE, arg1_name, arg1_val, arg2_name, arg2_val)

#define TRACE_EVENT_COPY_MARK(category_group, name)                      \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_MARK, category_group, name, \
                           TRACE_EVENT_FLAG_COPY)

#define TRACE_EVENT_COPY_MARK1(category_group, name, arg1_name, arg1_val) \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_MARK, category_group, name,  \
                           TRACE_EVENT_FLAG_COPY, arg1_name, arg1_val)

#define TRACE_EVENT_COPY_MARK_WITH_TIMESTAMP(category_group, name, timestamp) \
  INTERNAL_TRACE_EVENT_ADD_WITH_TIMESTAMP(                                    \
      TRACE_EVENT_PHASE_MARK, category_group, name, timestamp,                \
      TRACE_EVENT_FLAG_COPY)

// Similar to TRACE_EVENT_ENDx but with a custom |timestamp| provided.
// - |id| is used to match the _BEGIN event with the _END event.
//   Events are considered to match if their category_group, name and id values
//   all match. |id| must either be a pointer or an integer value up to 64 bits.
//   If it's a pointer, the bits will be xored with a hash of the process ID so
//   that the same pointer on two different processes will not collide.
// - |timestamp| must be non-null or it crashes. Use DCHECK(timestamp) before
//   calling this to detect an invalid timestamp even when tracing is not
//   enabled, as the commit queue doesn't run all tests with tracing enabled.
#define TRACE_EVENT_END_WITH_ID_TID_AND_TIMESTAMP0(category_group, name, id, \
                                                   thread_id, timestamp)     \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                        \
      TRACE_EVENT_PHASE_ASYNC_END, category_group, name, id, thread_id,      \
      timestamp, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_COPY_END_WITH_ID_TID_AND_TIMESTAMP0(                \
    category_group, name, id, thread_id, timestamp)                     \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                   \
      TRACE_EVENT_PHASE_ASYNC_END, category_group, name, id, thread_id, \
      timestamp, TRACE_EVENT_FLAG_COPY)
#define TRACE_EVENT_COPY_END_WITH_ID_TID_AND_TIMESTAMP1(                 \
    category_group, name, id, thread_id, timestamp, arg1_name, arg1_val) \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                    \
      TRACE_EVENT_PHASE_ASYNC_END, category_group, name, id, thread_id,  \
      timestamp, TRACE_EVENT_FLAG_COPY, arg1_name, arg1_val)
#define TRACE_EVENT_COPY_END_WITH_ID_TID_AND_TIMESTAMP2(                 \
    category_group, name, id, thread_id, timestamp, arg1_name, arg1_val, \
    arg2_name, arg2_val)                                                 \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                    \
      TRACE_EVENT_PHASE_ASYNC_END, category_group, name, id, thread_id,  \
      timestamp, TRACE_EVENT_FLAG_COPY, arg1_name, arg1_val, arg2_name,  \
      arg2_val)

// Records the value of a counter called "name" immediately. Value
// must be representable as a 32 bit integer.
// - category and name strings must have application lifetime (statics or
//   literals). They may not include " chars.
#define TRACE_COUNTER1(category_group, name, value)                         \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_COUNTER, category_group, name, \
                           TRACE_EVENT_FLAG_NONE, "value",                  \
                           static_cast<int>(value))
#define TRACE_COUNTER_WITH_FLAG1(category_group, name, flag, value)         \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_COUNTER, category_group, name, \
                           flag, "value", static_cast<int>(value))
#define TRACE_COPY_COUNTER1(category_group, name, value)                    \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_COUNTER, category_group, name, \
                           TRACE_EVENT_FLAG_COPY, "value",                  \
                           static_cast<int>(value))

// Records the values of a multi-parted counter called "name" immediately.
// The UI will treat value1 and value2 as parts of a whole, displaying their
// values as a stacked-bar chart.
// - category and name strings must have application lifetime (statics or
//   literals). They may not include " chars.
#define TRACE_COUNTER2(category_group, name, value1_name, value1_val,       \
                       value2_name, value2_val)                             \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_COUNTER, category_group, name, \
                           TRACE_EVENT_FLAG_NONE, value1_name,              \
                           static_cast<int>(value1_val), value2_name,       \
                           static_cast<int>(value2_val))
#define TRACE_COPY_COUNTER2(category_group, name, value1_name, value1_val,  \
                            value2_name, value2_val)                        \
  INTERNAL_TRACE_EVENT_ADD(TRACE_EVENT_PHASE_COUNTER, category_group, name, \
                           TRACE_EVENT_FLAG_COPY, value1_name,              \
                           static_cast<int>(value1_val), value2_name,       \
                           static_cast<int>(value2_val))

// Similar to TRACE_COUNTERx, but with a custom |timestamp| provided.
// - |timestamp| must be non-null or it crashes. Use DCHECK(timestamp) before
//   calling this to detect an invalid timestamp even when tracing is not
//   enabled, as the commit queue doesn't run all tests with tracing enabled.
#define TRACE_COUNTER_WITH_TIMESTAMP1(category_group, name, timestamp, value) \
  INTERNAL_TRACE_EVENT_ADD_WITH_TIMESTAMP(                                    \
      TRACE_EVENT_PHASE_COUNTER, category_group, name, timestamp,             \
      TRACE_EVENT_FLAG_NONE, "value", static_cast<int>(value))

#define TRACE_COUNTER_WITH_TIMESTAMP2(category_group, name, timestamp,      \
                                      value1_name, value1_val, value2_name, \
                                      value2_val)                           \
  INTERNAL_TRACE_EVENT_ADD_WITH_TIMESTAMP(                                  \
      TRACE_EVENT_PHASE_COUNTER, category_group, name, timestamp,           \
      TRACE_EVENT_FLAG_NONE, value1_name, static_cast<int>(value1_val),     \
      value2_name, static_cast<int>(value2_val))

// Records the value of a counter called "name" immediately. Value
// must be representable as a 32 bit integer.
// - category and name strings must have application lifetime (statics or
//   literals). They may not include " chars.
// - |id| is used to disambiguate counters with the same name. It must either
//   be a pointer or an integer value up to 64 bits. If it's a pointer, the bits
//   will be xored with a hash of the process ID so that the same pointer on
//   two different processes will not collide.
#define TRACE_COUNTER_ID1(category_group, name, id, value)                    \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_COUNTER, category_group, \
                                   name, id, TRACE_EVENT_FLAG_NONE, "value",  \
                                   static_cast<int>(value))
#define TRACE_COPY_COUNTER_ID1(category_group, name, id, value)               \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_COUNTER, category_group, \
                                   name, id, TRACE_EVENT_FLAG_COPY, "value",  \
                                   static_cast<int>(value))

// Records the values of a multi-parted counter called "name" immediately.
// The UI will treat value1 and value2 as parts of a whole, displaying their
// values as a stacked-bar chart.
// - category and name strings must have application lifetime (statics or
//   literals). They may not include " chars.
// - |id| is used to disambiguate counters with the same name. It must either
//   be a pointer or an integer value up to 64 bits. If it's a pointer, the bits
//   will be xored with a hash of the process ID so that the same pointer on
//   two different processes will not collide.
#define TRACE_COUNTER_ID2(category_group, name, id, value1_name, value1_val,  \
                          value2_name, value2_val)                            \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_COUNTER, category_group, \
                                   name, id, TRACE_EVENT_FLAG_NONE,           \
                                   value1_name, static_cast<int>(value1_val), \
                                   value2_name, static_cast<int>(value2_val))
#define TRACE_COPY_COUNTER_ID2(category_group, name, id, value1_name,         \
                               value1_val, value2_name, value2_val)           \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_COUNTER, category_group, \
                                   name, id, TRACE_EVENT_FLAG_COPY,           \
                                   value1_name, static_cast<int>(value1_val), \
                                   value2_name, static_cast<int>(value2_val))

#define TRACE_EVENT_SAMPLE_WITH_ID1(category_group, name, id, arg1_name,       \
                                    arg1_val)                                  \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_SAMPLE, category_group,   \
                                   name, id, TRACE_EVENT_FLAG_NONE, arg1_name, \
                                   arg1_val)

// -- TRACE_EVENT_ASYNC is DEPRECATED! --
//
// TRACE_EVENT_ASYNC_* APIs should be only used by legacy code. New code should
// use TRACE_EVENT_NESTABLE_ASYNC_* APIs instead.
//
// Records a single ASYNC_BEGIN event called "name" immediately, with 0, 1 or 2
// associated arguments. If the category is not enabled, then this
// does nothing.
// - category and name strings must have application lifetime (statics or
//   literals). They may not include " chars.
// - |id| is used to match the ASYNC_BEGIN event with the ASYNC_END event. ASYNC
//   events are considered to match if their category_group, name and id values
//   all match. |id| must either be a pointer or an integer value up to 64 bits.
//   If it's a pointer, the bits will be xored with a hash of the process ID so
//   that the same pointer on two different processes will not collide.
//
// An asynchronous operation can consist of multiple phases. The first phase is
// defined by the ASYNC_BEGIN calls. Additional phases can be defined using the
// ASYNC_STEP_INTO or ASYNC_STEP_PAST macros. The ASYNC_STEP_INTO macro will
// annotate the block following the call. The ASYNC_STEP_PAST macro will
// annotate the block prior to the call. Note that any particular event must use
// only STEP_INTO or STEP_PAST macros; they can not mix and match. When the
// operation completes, call ASYNC_END.
//
// An ASYNC trace typically occurs on a single thread (if not, they will only be
// drawn on the thread defined in the ASYNC_BEGIN event), but all events in that
// operation must use the same |name| and |id|. Each step can have its own
// args.
#define TRACE_EVENT_ASYNC_BEGIN0(category_group, name, id)        \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_ASYNC_BEGIN, \
                                   category_group, name, id,      \
                                   TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_ASYNC_BEGIN1(category_group, name, id, arg1_name, \
                                 arg1_val)                            \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_ASYNC_BEGIN,     \
                                   category_group, name, id,          \
                                   TRACE_EVENT_FLAG_NONE, arg1_name, arg1_val)
#define TRACE_EVENT_ASYNC_BEGIN2(category_group, name, id, arg1_name, \
                                 arg1_val, arg2_name, arg2_val)       \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(                                   \
      TRACE_EVENT_PHASE_ASYNC_BEGIN, category_group, name, id,        \
      TRACE_EVENT_FLAG_NONE, arg1_name, arg1_val, arg2_name, arg2_val)
#define TRACE_EVENT_COPY_ASYNC_BEGIN0(category_group, name, id)   \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_ASYNC_BEGIN, \
                                   category_group, name, id,      \
                                   TRACE_EVENT_FLAG_COPY)
#define TRACE_EVENT_COPY_ASYNC_BEGIN1(category_group, name, id, arg1_name, \
                                      arg1_val)                            \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_ASYNC_BEGIN,          \
                                   category_group, name, id,               \
                                   TRACE_EVENT_FLAG_COPY, arg1_name, arg1_val)
#define TRACE_EVENT_COPY_ASYNC_BEGIN2(category_group, name, id, arg1_name, \
                                      arg1_val, arg2_name, arg2_val)       \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(                                        \
      TRACE_EVENT_PHASE_ASYNC_BEGIN, category_group, name, id,             \
      TRACE_EVENT_FLAG_COPY, arg1_name, arg1_val, arg2_name, arg2_val)

// Similar to TRACE_EVENT_ASYNC_BEGINx but with a custom |at| timestamp
// provided.
#define TRACE_EVENT_ASYNC_BEGIN_WITH_TIMESTAMP0(category_group, name, id, \
                                                timestamp)                \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                     \
      TRACE_EVENT_PHASE_ASYNC_BEGIN, category_group, name, id,            \
      TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_ASYNC_BEGIN_WITH_TIMESTAMP1(                           \
    category_group, name, id, timestamp, arg1_name, arg1_val)              \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                      \
      TRACE_EVENT_PHASE_ASYNC_BEGIN, category_group, name, id,             \
      TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_NONE, \
      arg1_name, arg1_val)
#define TRACE_EVENT_ASYNC_BEGIN_WITH_TIMESTAMP2(category_group, name, id,      \
                                                timestamp, arg1_name,          \
                                                arg1_val, arg2_name, arg2_val) \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                          \
      TRACE_EVENT_PHASE_ASYNC_BEGIN, category_group, name, id,                 \
      TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_NONE,     \
      arg1_name, arg1_val, arg2_name, arg2_val)
#define TRACE_EVENT_COPY_ASYNC_BEGIN_WITH_TIMESTAMP0(category_group, name, id, \
                                                     timestamp)                \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                          \
      TRACE_EVENT_PHASE_ASYNC_BEGIN, category_group, name, id,                 \
      TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_COPY)

// Records a single ASYNC_STEP_INTO event for |step| immediately. If the
// category is not enabled, then this does nothing. The |name| and |id| must
// match the ASYNC_BEGIN event above. The |step| param identifies this step
// within the async event. This should be called at the beginning of the next
// phase of an asynchronous operation. The ASYNC_BEGIN event must not have any
// ASYNC_STEP_PAST events.
#define TRACE_EVENT_ASYNC_STEP_INTO0(category_group, name, id, step)  \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_ASYNC_STEP_INTO, \
                                   category_group, name, id,          \
                                   TRACE_EVENT_FLAG_NONE, "step", step)
#define TRACE_EVENT_ASYNC_STEP_INTO1(category_group, name, id, step, \
                                     arg1_name, arg1_val)            \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(                                  \
      TRACE_EVENT_PHASE_ASYNC_STEP_INTO, category_group, name, id,   \
      TRACE_EVENT_FLAG_NONE, "step", step, arg1_name, arg1_val)

// Similar to TRACE_EVENT_ASYNC_STEP_INTOx but with a custom |at| timestamp
// provided.
#define TRACE_EVENT_ASYNC_STEP_INTO_WITH_TIMESTAMP0(category_group, name, id, \
                                                    step, timestamp)          \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                         \
      TRACE_EVENT_PHASE_ASYNC_STEP_INTO, category_group, name, id,            \
      TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_NONE,    \
      "step", step)

// Records a single ASYNC_STEP_PAST event for |step| immediately. If the
// category is not enabled, then this does nothing. The |name| and |id| must
// match the ASYNC_BEGIN event above. The |step| param identifies this step
// within the async event. This should be called at the beginning of the next
// phase of an asynchronous operation. The ASYNC_BEGIN event must not have any
// ASYNC_STEP_INTO events.
#define TRACE_EVENT_ASYNC_STEP_PAST0(category_group, name, id, step)  \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_ASYNC_STEP_PAST, \
                                   category_group, name, id,          \
                                   TRACE_EVENT_FLAG_NONE, "step", step)
#define TRACE_EVENT_ASYNC_STEP_PAST1(category_group, name, id, step, \
                                     arg1_name, arg1_val)            \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(                                  \
      TRACE_EVENT_PHASE_ASYNC_STEP_PAST, category_group, name, id,   \
      TRACE_EVENT_FLAG_NONE, "step", step, arg1_name, arg1_val)

// Records a single ASYNC_END event for "name" immediately. If the category
// is not enabled, then this does nothing.
#define TRACE_EVENT_ASYNC_END0(category_group, name, id)        \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_ASYNC_END, \
                                   category_group, name, id,    \
                                   TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_ASYNC_END1(category_group, name, id, arg1_name, arg1_val) \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_ASYNC_END,               \
                                   category_group, name, id,                  \
                                   TRACE_EVENT_FLAG_NONE, arg1_name, arg1_val)
#define TRACE_EVENT_ASYNC_END2(category_group, name, id, arg1_name, arg1_val, \
                               arg2_name, arg2_val)                           \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(                                           \
      TRACE_EVENT_PHASE_ASYNC_END, category_group, name, id,                  \
      TRACE_EVENT_FLAG_NONE, arg1_name, arg1_val, arg2_name, arg2_val)
#define TRACE_EVENT_COPY_ASYNC_END0(category_group, name, id)   \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_ASYNC_END, \
                                   category_group, name, id,    \
                                   TRACE_EVENT_FLAG_COPY)
#define TRACE_EVENT_COPY_ASYNC_END1(category_group, name, id, arg1_name, \
                                    arg1_val)                            \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_ASYNC_END,          \
                                   category_group, name, id,             \
                                   TRACE_EVENT_FLAG_COPY, arg1_name, arg1_val)
#define TRACE_EVENT_COPY_ASYNC_END2(category_group, name, id, arg1_name, \
                                    arg1_val, arg2_name, arg2_val)       \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(                                      \
      TRACE_EVENT_PHASE_ASYNC_END, category_group, name, id,             \
      TRACE_EVENT_FLAG_COPY, arg1_name, arg1_val, arg2_name, arg2_val)

// Similar to TRACE_EVENT_ASYNC_ENDx but with a custom |at| timestamp provided.
#define TRACE_EVENT_ASYNC_END_WITH_TIMESTAMP0(category_group, name, id, \
                                              timestamp)                \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                   \
      TRACE_EVENT_PHASE_ASYNC_END, category_group, name, id,            \
      TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_ASYNC_END_WITH_TIMESTAMP1(category_group, name, id,       \
                                              timestamp, arg1_name, arg1_val) \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                         \
      TRACE_EVENT_PHASE_ASYNC_END, category_group, name, id,                  \
      TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_NONE,    \
      arg1_name, arg1_val)
#define TRACE_EVENT_ASYNC_END_WITH_TIMESTAMP2(category_group, name, id,       \
                                              timestamp, arg1_name, arg1_val, \
                                              arg2_name, arg2_val)            \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                         \
      TRACE_EVENT_PHASE_ASYNC_END, category_group, name, id,                  \
      TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_NONE,    \
      arg1_name, arg1_val, arg2_name, arg2_val)
#define TRACE_EVENT_COPY_ASYNC_END_WITH_TIMESTAMP0(category_group, name, id, \
                                                   timestamp)                \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                        \
      TRACE_EVENT_PHASE_ASYNC_END, category_group, name, id,                 \
      TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_COPY)

// NESTABLE_ASYNC_* APIs are used to describe an async operation, which can
// be nested within a NESTABLE_ASYNC event and/or have inner NESTABLE_ASYNC
// events.
// - category and name strings must have application lifetime (statics or
//   literals). They may not include " chars.
// - A pair of NESTABLE_ASYNC_BEGIN event and NESTABLE_ASYNC_END event is
//   considered as a match if their category_group, name and id all match.
// - |id| must either be a pointer or an integer value up to 64 bits.
//   If it's a pointer, the bits will be xored with a hash of the process ID so
//   that the same pointer on two different processes will not collide.
// - |id| is used to match a child NESTABLE_ASYNC event with its parent
//   NESTABLE_ASYNC event. Therefore, events in the same nested event tree must
//   be logged using the same id and category_group.
//
// Unmatched NESTABLE_ASYNC_END event will be parsed as an event that starts
// at the first NESTABLE_ASYNC event of that id, and unmatched
// NESTABLE_ASYNC_BEGIN event will be parsed as an event that ends at the last
// NESTABLE_ASYNC event of that id. Corresponding warning messages for
// unmatched events will be shown in the analysis view.

// Records a single NESTABLE_ASYNC_BEGIN event called "name" immediately, with
// 0, 1 or 2 associated arguments. If the category is not enabled, then this
// does nothing.
#define TRACE_EVENT_NESTABLE_ASYNC_BEGIN0(category_group, name, id)        \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN, \
                                   category_group, name, id,               \
                                   TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_NESTABLE_ASYNC_BEGIN1(category_group, name, id, arg1_name, \
                                          arg1_val)                            \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN,     \
                                   category_group, name, id,                   \
                                   TRACE_EVENT_FLAG_NONE, arg1_name, arg1_val)
#define TRACE_EVENT_NESTABLE_ASYNC_BEGIN2(category_group, name, id, arg1_name, \
                                          arg1_val, arg2_name, arg2_val)       \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(                                            \
      TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN, category_group, name, id,        \
      TRACE_EVENT_FLAG_NONE, arg1_name, arg1_val, arg2_name, arg2_val)
#define TRACE_EVENT_NESTABLE_ASYNC_BEGIN_WITH_FLAGS0(category_group, name, id, \
                                                     flags)                    \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN,     \
                                   category_group, name, id, flags)
// Records a single NESTABLE_ASYNC_END event called "name" immediately, with 0
// or 2 associated arguments. If the category is not enabled, then this does
// nothing.
#define TRACE_EVENT_NESTABLE_ASYNC_END0(category_group, name, id)        \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_END, \
                                   category_group, name, id,             \
                                   TRACE_EVENT_FLAG_NONE)
// Records a single NESTABLE_ASYNC_END event called "name" immediately, with 1
// associated argument. If the category is not enabled, then this does nothing.
#define TRACE_EVENT_NESTABLE_ASYNC_END1(category_group, name, id, arg1_name, \
                                        arg1_val)                            \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_END,     \
                                   category_group, name, id,                 \
                                   TRACE_EVENT_FLAG_NONE, arg1_name, arg1_val)
#define TRACE_EVENT_NESTABLE_ASYNC_END2(category_group, name, id, arg1_name, \
                                        arg1_val, arg2_name, arg2_val)       \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(                                          \
      TRACE_EVENT_PHASE_NESTABLE_ASYNC_END, category_group, name, id,        \
      TRACE_EVENT_FLAG_NONE, arg1_name, arg1_val, arg2_name, arg2_val)
#define TRACE_EVENT_NESTABLE_ASYNC_END_WITH_FLAGS0(category_group, name, id, \
                                                   flags)                    \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_END,     \
                                   category_group, name, id, flags)

// Records a single NESTABLE_ASYNC_INSTANT event called "name" immediately,
// with none, one or two associated argument. If the category is not enabled,
// then this does nothing.
#define TRACE_EVENT_NESTABLE_ASYNC_INSTANT0(category_group, name, id)        \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_INSTANT, \
                                   category_group, name, id,                 \
                                   TRACE_EVENT_FLAG_NONE)

#define TRACE_EVENT_NESTABLE_ASYNC_INSTANT1(category_group, name, id,        \
                                            arg1_name, arg1_val)             \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_INSTANT, \
                                   category_group, name, id,                 \
                                   TRACE_EVENT_FLAG_NONE, arg1_name, arg1_val)

#define TRACE_EVENT_NESTABLE_ASYNC_INSTANT2(                              \
    category_group, name, id, arg1_name, arg1_val, arg2_name, arg2_val)   \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(                                       \
      TRACE_EVENT_PHASE_NESTABLE_ASYNC_INSTANT, category_group, name, id, \
      TRACE_EVENT_FLAG_NONE, arg1_name, arg1_val, arg2_name, arg2_val)

#define TRACE_EVENT_COPY_NESTABLE_ASYNC_BEGIN_WITH_TTS2(                       \
    category_group, name, id, arg1_name, arg1_val, arg2_name, arg2_val)        \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(                                            \
      TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN, category_group, name, id,        \
      TRACE_EVENT_FLAG_ASYNC_TTS | TRACE_EVENT_FLAG_COPY, arg1_name, arg1_val, \
      arg2_name, arg2_val)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_END_WITH_TTS2(                         \
    category_group, name, id, arg1_name, arg1_val, arg2_name, arg2_val)        \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(                                            \
      TRACE_EVENT_PHASE_NESTABLE_ASYNC_END, category_group, name, id,          \
      TRACE_EVENT_FLAG_ASYNC_TTS | TRACE_EVENT_FLAG_COPY, arg1_name, arg1_val, \
      arg2_name, arg2_val)

// Similar to TRACE_EVENT_NESTABLE_ASYNC_{BEGIN,END}x but with a custom
// |timestamp| provided.
#define TRACE_EVENT_NESTABLE_ASYNC_BEGIN_WITH_TIMESTAMP0(category_group, name, \
                                                         id, timestamp)        \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                          \
      TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN, category_group, name, id,        \
      TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_NESTABLE_ASYNC_BEGIN_WITH_TIMESTAMP1(                  \
    category_group, name, id, timestamp, arg1_name, arg1_val)              \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                      \
      TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN, category_group, name, id,    \
      TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_NONE, \
      arg1_name, arg1_val)
#define TRACE_EVENT_NESTABLE_ASYNC_BEGIN_WITH_TIMESTAMP_AND_FLAGS0(     \
    category_group, name, id, timestamp, flags)                         \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                   \
      TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN, category_group, name, id, \
      TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, flags)
#define TRACE_EVENT_NESTABLE_ASYNC_END_WITH_TIMESTAMP0(category_group, name, \
                                                       id, timestamp)        \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                        \
      TRACE_EVENT_PHASE_NESTABLE_ASYNC_END, category_group, name, id,        \
      TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_NESTABLE_ASYNC_END_WITH_TIMESTAMP1(                    \
    category_group, name, id, timestamp, arg1_name, arg1_val)              \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                      \
      TRACE_EVENT_PHASE_NESTABLE_ASYNC_END, category_group, name, id,      \
      TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_NONE, \
      arg1_name, arg1_val)
#define TRACE_EVENT_NESTABLE_ASYNC_END_WITH_TIMESTAMP2(                    \
    category_group, name, id, timestamp, arg1_name, arg1_val, arg2_name,   \
    arg2_val)                                                              \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                      \
      TRACE_EVENT_PHASE_NESTABLE_ASYNC_END, category_group, name, id,      \
      TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_NONE, \
      arg1_name, arg1_val, arg2_name, arg2_val)
#define TRACE_EVENT_NESTABLE_ASYNC_END_WITH_TIMESTAMP_AND_FLAGS0(     \
    category_group, name, id, timestamp, flags)                       \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                 \
      TRACE_EVENT_PHASE_NESTABLE_ASYNC_END, category_group, name, id, \
      TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, flags)
#define TRACE_EVENT_NESTABLE_ASYNC_INSTANT_WITH_TIMESTAMP0(               \
    category_group, name, id, timestamp)                                  \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                     \
      TRACE_EVENT_PHASE_NESTABLE_ASYNC_INSTANT, category_group, name, id, \
      TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_NONE)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_BEGIN0(category_group, name, id)   \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN, \
                                   category_group, name, id,               \
                                   TRACE_EVENT_FLAG_COPY)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_BEGIN1(category_group, name, id,   \
                                               arg1_name, arg1_val)        \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN, \
                                   category_group, name, id,               \
                                   TRACE_EVENT_FLAG_COPY, arg1_name, arg1_val)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_BEGIN2(                         \
    category_group, name, id, arg1_name, arg1_val, arg2_name, arg2_val) \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(                                     \
      TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN, category_group, name, id, \
      TRACE_EVENT_FLAG_COPY, arg1_name, arg1_val, arg2_name, arg2_val)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_END0(category_group, name, id)   \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_END, \
                                   category_group, name, id,             \
                                   TRACE_EVENT_FLAG_COPY)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_BEGIN_WITH_TIMESTAMP0(          \
    category_group, name, id, timestamp)                                \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                   \
      TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN, category_group, name, id, \
      TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_COPY)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_BEGIN_WITH_TIMESTAMP1(             \
    category_group, name, id, timestamp, arg1_name, arg1_val)              \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                      \
      TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN, category_group, name, id,    \
      TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_COPY, \
      arg1_name, arg1_val)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_END_WITH_TIMESTAMP0(          \
    category_group, name, id, timestamp)                              \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                 \
      TRACE_EVENT_PHASE_NESTABLE_ASYNC_END, category_group, name, id, \
      TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_COPY)
#define TRACE_EVENT_COPY_NESTABLE_ASYNC_END1(category_group, name, id,   \
                                             arg1_name, arg1_val)        \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(TRACE_EVENT_PHASE_NESTABLE_ASYNC_END, \
                                   category_group, name, id,             \
                                   TRACE_EVENT_FLAG_COPY, arg1_name, arg1_val)

// TRACE_EVENT_METADATA* events are information related to other
// injected events, not events in their own right.
#define TRACE_EVENT_METADATA1(category_group, name, arg1_name, arg1_val) \
  INTERNAL_TRACE_EVENT_METADATA_ADD(category_group, name, arg1_name, arg1_val)

// Records a clock sync event.
#define TRACE_EVENT_CLOCK_SYNC_RECEIVER(sync_id)                               \
  INTERNAL_TRACE_EVENT_ADD(                                                    \
      TRACE_EVENT_PHASE_CLOCK_SYNC, "__metadata", "clock_sync",                \
      TRACE_EVENT_FLAG_NONE, "sync_id", sync_id)
#define TRACE_EVENT_CLOCK_SYNC_ISSUER(sync_id, issue_ts, issue_end_ts)         \
  INTERNAL_TRACE_EVENT_ADD_WITH_TIMESTAMP(                                     \
      TRACE_EVENT_PHASE_CLOCK_SYNC, "__metadata", "clock_sync",                \
      issue_end_ts, TRACE_EVENT_FLAG_NONE,                                     \
      "sync_id", sync_id, "issue_ts", issue_ts)

// Macros to track the life time and value of arbitrary client objects.
// See also TraceTrackableObject.
#define TRACE_EVENT_OBJECT_CREATED_WITH_ID(category_group, name, id) \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(                                  \
      TRACE_EVENT_PHASE_CREATE_OBJECT, category_group, name, id,     \
      TRACE_EVENT_FLAG_NONE)

#define TRACE_EVENT_OBJECT_SNAPSHOT_WITH_ID(category_group, name, id, \
                                            snapshot)                 \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(                                   \
      TRACE_EVENT_PHASE_SNAPSHOT_OBJECT, category_group, name,        \
      id, TRACE_EVENT_FLAG_NONE, "snapshot", snapshot)

#define TRACE_EVENT_OBJECT_SNAPSHOT_WITH_ID_AND_TIMESTAMP(                     \
    category_group, name, id, timestamp, snapshot)                             \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID_TID_AND_TIMESTAMP(                          \
      TRACE_EVENT_PHASE_SNAPSHOT_OBJECT, category_group, name,                 \
      id, TRACE_EVENT_API_CURRENT_THREAD_ID, timestamp, TRACE_EVENT_FLAG_NONE, \
      "snapshot", snapshot)

#define TRACE_EVENT_OBJECT_DELETED_WITH_ID(category_group, name, id) \
  INTERNAL_TRACE_EVENT_ADD_WITH_ID(                                  \
      TRACE_EVENT_PHASE_DELETE_OBJECT, category_group, name, id,     \
      TRACE_EVENT_FLAG_NONE)

// Macro to efficiently determine if a given category group is enabled.
#define TRACE_EVENT_CATEGORY_GROUP_ENABLED(category_group, ret)             \
  do {                                                                      \
    INTERNAL_TRACE_EVENT_GET_CATEGORY_INFO(category_group);                 \
    if (INTERNAL_TRACE_EVENT_CATEGORY_GROUP_ENABLED_FOR_RECORDING_MODE()) { \
      *ret = true;                                                          \
    } else {                                                                \
      *ret = false;                                                         \
    }                                                                       \
  } while (0)

// Macro to efficiently determine, through polling, if a new trace has begun.
#define TRACE_EVENT_IS_NEW_TRACE(ret)                                      \
  do {                                                                     \
    static int INTERNAL_TRACE_EVENT_UID(lastRecordingNumber) = 0;          \
    int num_traces_recorded = TRACE_EVENT_API_GET_NUM_TRACES_RECORDED();   \
    if (num_traces_recorded != -1 &&                                       \
        num_traces_recorded !=                                             \
            INTERNAL_TRACE_EVENT_UID(lastRecordingNumber)) {               \
      INTERNAL_TRACE_EVENT_UID(lastRecordingNumber) = num_traces_recorded; \
      *ret = true;                                                         \
    } else {                                                               \
      *ret = false;                                                        \
    }                                                                      \
  } while (0)

// Macro for getting the real base::TimeTicks::Now() which can be overridden in
// headless when VirtualTime is enabled.
#define TRACE_TIME_TICKS_NOW() INTERNAL_TRACE_TIME_TICKS_NOW()

// Macro for getting the real base::Time::Now() which can be overridden in
// headless when VirtualTime is enabled.
#define TRACE_TIME_NOW() INTERNAL_TRACE_TIME_NOW()

// Notes regarding the following definitions:
// New values can be added and propagated to third party libraries, but existing
// definitions must never be changed, because third party libraries may use old
// definitions.

// Phase indicates the nature of an event entry. E.g. part of a begin/end pair.
#define TRACE_EVENT_PHASE_BEGIN ('B')
#define TRACE_EVENT_PHASE_END ('E')
#define TRACE_EVENT_PHASE_COMPLETE ('X')
#define TRACE_EVENT_PHASE_INSTANT ('I')
#define TRACE_EVENT_PHASE_ASYNC_BEGIN ('S')
#define TRACE_EVENT_PHASE_ASYNC_STEP_INTO ('T')
#define TRACE_EVENT_PHASE_ASYNC_STEP_PAST ('p')
#define TRACE_EVENT_PHASE_ASYNC_END ('F')
#define TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN ('b')
#define TRACE_EVENT_PHASE_NESTABLE_ASYNC_END ('e')
#define TRACE_EVENT_PHASE_NESTABLE_ASYNC_INSTANT ('n')
#define TRACE_EVENT_PHASE_FLOW_BEGIN ('s')
#define TRACE_EVENT_PHASE_FLOW_STEP ('t')
#define TRACE_EVENT_PHASE_FLOW_END ('f')
#define TRACE_EVENT_PHASE_METADATA ('M')
#define TRACE_EVENT_PHASE_COUNTER ('C')
#define TRACE_EVENT_PHASE_SAMPLE ('P')
#define TRACE_EVENT_PHASE_CREATE_OBJECT ('N')
#define TRACE_EVENT_PHASE_SNAPSHOT_OBJECT ('O')
#define TRACE_EVENT_PHASE_DELETE_OBJECT ('D')
#define TRACE_EVENT_PHASE_MEMORY_DUMP ('v')
#define TRACE_EVENT_PHASE_MARK ('R')
#define TRACE_EVENT_PHASE_CLOCK_SYNC ('c')

// Flags for changing the behavior of TRACE_EVENT_API_ADD_TRACE_EVENT.
#define TRACE_EVENT_FLAG_NONE (static_cast<unsigned int>(0))

// Should not be used outside this file or
// except `trace_event_impl.cc` (implementation details).
// If used, it will result in CHECK failure in SDK build.
#define TRACE_EVENT_FLAG_COPY (static_cast<unsigned int>(1 << 0))

#define TRACE_EVENT_FLAG_HAS_ID (static_cast<unsigned int>(1 << 1))
#define TRACE_EVENT_FLAG_SCOPE_OFFSET (static_cast<unsigned int>(1 << 2))
#define TRACE_EVENT_FLAG_SCOPE_EXTRA (static_cast<unsigned int>(1 << 3))
#define TRACE_EVENT_FLAG_EXPLICIT_TIMESTAMP (static_cast<unsigned int>(1 << 4))
#define TRACE_EVENT_FLAG_ASYNC_TTS (static_cast<unsigned int>(1 << 5))
#define TRACE_EVENT_FLAG_BIND_TO_ENCLOSING (static_cast<unsigned int>(1 << 6))
#define TRACE_EVENT_FLAG_FLOW_IN (static_cast<unsigned int>(1 << 7))
#define TRACE_EVENT_FLAG_FLOW_OUT (static_cast<unsigned int>(1 << 8))
#define TRACE_EVENT_FLAG_HAS_CONTEXT_ID (static_cast<unsigned int>(1 << 9))
#define TRACE_EVENT_FLAG_HAS_PROCESS_ID (static_cast<unsigned int>(1 << 10))
#define TRACE_EVENT_FLAG_HAS_LOCAL_ID (static_cast<unsigned int>(1 << 11))
#define TRACE_EVENT_FLAG_HAS_GLOBAL_ID (static_cast<unsigned int>(1 << 12))
#define TRACE_EVENT_FLAG_JAVA_STRING_LITERALS \
  (static_cast<unsigned int>(1 << 16))

#define TRACE_EVENT_FLAG_SCOPE_MASK                          \
  (static_cast<unsigned int>(TRACE_EVENT_FLAG_SCOPE_OFFSET | \
                             TRACE_EVENT_FLAG_SCOPE_EXTRA))

// Type values for identifying types in the TraceValue union.
#define TRACE_VALUE_TYPE_BOOL (static_cast<unsigned char>(1))
#define TRACE_VALUE_TYPE_UINT (static_cast<unsigned char>(2))
#define TRACE_VALUE_TYPE_INT (static_cast<unsigned char>(3))
#define TRACE_VALUE_TYPE_DOUBLE (static_cast<unsigned char>(4))
#define TRACE_VALUE_TYPE_POINTER (static_cast<unsigned char>(5))
#define TRACE_VALUE_TYPE_STRING (static_cast<unsigned char>(6))
#define TRACE_VALUE_TYPE_COPY_STRING (static_cast<unsigned char>(7))
#define TRACE_VALUE_TYPE_CONVERTABLE (static_cast<unsigned char>(8))
#define TRACE_VALUE_TYPE_PROTO (static_cast<unsigned char>(9))

// Enum reflecting the scope of an INSTANT event. Must fit within
// TRACE_EVENT_FLAG_SCOPE_MASK.
#define TRACE_EVENT_SCOPE_GLOBAL (static_cast<unsigned char>(0 << 2))
#define TRACE_EVENT_SCOPE_PROCESS (static_cast<unsigned char>(1 << 2))
#define TRACE_EVENT_SCOPE_THREAD (static_cast<unsigned char>(2 << 2))

#define TRACE_EVENT_SCOPE_NAME_GLOBAL ('g')
#define TRACE_EVENT_SCOPE_NAME_PROCESS ('p')
#define TRACE_EVENT_SCOPE_NAME_THREAD ('t')

#endif  // V8_TRACING_TRACE_EVENT_NO_PERFETTO_H_
