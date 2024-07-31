// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/libplatform/v8-tracing.h"
#include "src/base/atomicops.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/time.h"

#ifdef V8_USE_PERFETTO
#include "perfetto/ext/trace_processor/export_json.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "perfetto/tracing/tracing.h"
#include "protos/perfetto/config/data_source_config.gen.h"
#include "protos/perfetto/config/trace_config.gen.h"
#include "protos/perfetto/config/track_event/track_event_config.gen.h"
#include "src/base/platform/platform.h"
#include "src/base/platform/semaphore.h"
#include "src/libplatform/tracing/trace-event-listener.h"
#endif  // V8_USE_PERFETTO

#ifdef V8_USE_PERFETTO
class JsonOutputWriter : public perfetto::trace_processor::json::OutputWriter {
 public:
  explicit JsonOutputWriter(std::ostream* stream) : stream_(stream) {}

  perfetto::trace_processor::util::Status AppendString(
      const std::string& string) override {
    *stream_ << string;
    return perfetto::trace_processor::util::OkStatus();
  }

 private:
  std::ostream* stream_;
};
#endif  // V8_USE_PERFETTO

namespace v8 {
namespace platform {
namespace tracing {

#if !defined(V8_USE_PERFETTO)
static const size_t kMaxCategoryGroups = 200;

// Parallel arrays g_category_groups and g_category_group_enabled are separate
// so that a pointer to a member of g_category_group_enabled can be easily
// converted to an index into g_category_groups. This allows macros to deal
// only with char enabled pointers from g_category_group_enabled, and we can
// convert internally to determine the category name from the char enabled
// pointer.
const char* g_category_groups[kMaxCategoryGroups] = {
    "toplevel",
    "tracing categories exhausted; must increase kMaxCategoryGroups",
    "__metadata"};

// The enabled flag is char instead of bool so that the API can be used from C.
unsigned char g_category_group_enabled[kMaxCategoryGroups] = {0};
// Indexes here have to match the g_category_groups array indexes above.
const int g_category_categories_exhausted = 1;
// Metadata category not used in V8.
// const int g_category_metadata = 2;
const int g_num_builtin_categories = 3;

// Skip default categories.
v8::base::AtomicWord g_category_index = g_num_builtin_categories;
#endif  // !defined(V8_USE_PERFETTO)

TracingController::TracingController() { mutex_.reset(new base::Mutex()); }

TracingController::~TracingController() {
  StopTracing();

#if !defined(V8_USE_PERFETTO)
  {
    // Free memory for category group names allocated via strdup.
    base::MutexGuard lock(mutex_.get());
    for (size_t i = g_category_index - 1; i >= g_num_builtin_categories; --i) {
      const char* group = g_category_groups[i];
      g_category_groups[i] = nullptr;
      free(const_cast<char*>(group));
    }
    g_category_index = g_num_builtin_categories;
  }
#endif  // !defined(V8_USE_PERFETTO)
}

#ifdef V8_USE_PERFETTO
void TracingController::InitializeForPerfetto(std::ostream* output_stream) {
  output_stream_ = output_stream;
  DCHECK_NOT_NULL(output_stream);
  DCHECK(output_stream->good());
}

void TracingController::SetTraceEventListenerForTesting(
    TraceEventListener* listener) {
  listener_for_testing_ = listener;
}
#else   // !V8_USE_PERFETTO
void TracingController::Initialize(TraceBuffer* trace_buffer) {
  trace_buffer_.reset(trace_buffer);
}

int64_t TracingController::CurrentTimestampMicroseconds() {
  return base::TimeTicks::Now().ToInternalValue();
}

int64_t TracingController::CurrentCpuTimestampMicroseconds() {
  return base::ThreadTicks::Now().ToInternalValue();
}

uint64_t TracingController::AddTraceEvent(
    char phase, const uint8_t* category_enabled_flag, const char* name,
    const char* scope, uint64_t id, uint64_t bind_id, int num_args,
    const char** arg_names, const uint8_t* arg_types,
    const uint64_t* arg_values,
    std::unique_ptr<v8::ConvertableToTraceFormat>* arg_convertables,
    unsigned int flags) {
  int64_t now_us = CurrentTimestampMicroseconds();

  return AddTraceEventWithTimestamp(
      phase, category_enabled_flag, name, scope, id, bind_id, num_args,
      arg_names, arg_types, arg_values, arg_convertables, flags, now_us);
}

uint64_t TracingController::AddTraceEventWithTimestamp(
    char phase, const uint8_t* category_enabled_flag, const char* name,
    const char* scope, uint64_t id, uint64_t bind_id, int num_args,
    const char** arg_names, const uint8_t* arg_types,
    const uint64_t* arg_values,
    std::unique_ptr<v8::ConvertableToTraceFormat>* arg_convertables,
    unsigned int flags, int64_t timestamp) {
  int64_t cpu_now_us = CurrentCpuTimestampMicroseconds();

  uint64_t handle = 0;
  if (recording_.load(std::memory_order_acquire)) {
    TraceObject* trace_object = trace_buffer_->AddTraceEvent(&handle);
    if (trace_object) {
      {
        base::MutexGuard lock(mutex_.get());
        trace_object->Initialize(phase, category_enabled_flag, name, scope, id,
                                 bind_id, num_args, arg_names, arg_types,
                                 arg_values, arg_convertables, flags, timestamp,
                                 cpu_now_us);
      }
    }
  }
  return handle;
}

void TracingController::UpdateTraceEventDuration(
    const uint8_t* category_enabled_flag, const char* name, uint64_t handle) {
  int64_t now_us = CurrentTimestampMicroseconds();
  int64_t cpu_now_us = CurrentCpuTimestampMicroseconds();

  TraceObject* trace_object = trace_buffer_->GetEventByHandle(handle);
  if (!trace_object) return;
  trace_object->UpdateDuration(now_us, cpu_now_us);
}

const char* TracingController::GetCategoryGroupName(
    const uint8_t* category_group_enabled) {
  // Calculate the index of the category group by finding
  // category_group_enabled in g_category_group_enabled array.
  uintptr_t category_begin =
      reinterpret_cast<uintptr_t>(g_category_group_enabled);
  uintptr_t category_ptr = reinterpret_cast<uintptr_t>(category_group_enabled);
  // Check for out of bounds category pointers.
  DCHECK(category_ptr >= category_begin &&
         category_ptr < reinterpret_cast<uintptr_t>(g_category_group_enabled +
                                                    kMaxCategoryGroups));
  uintptr_t category_index =
      (category_ptr - category_begin) / sizeof(g_category_group_enabled[0]);
  return g_category_groups[category_index];
}
#endif  // !defined(V8_USE_PERFETTO)

void TracingController::StartTracing(TraceConfig* trace_config) {
#ifdef V8_USE_PERFETTO
  DCHECK_NOT_NULL(output_stream_);
  DCHECK(output_stream_->good());
  perfetto::trace_processor::Config processor_config;
  trace_processor_ =
      perfetto::trace_processor::TraceProcessorStorage::CreateInstance(
          processor_config);

  ::perfetto::TraceConfig perfetto_trace_config;
  perfetto_trace_config.add_buffers()->set_size_kb(4096);
  auto ds_config = perfetto_trace_config.add_data_sources()->mutable_config();
  ds_config->set_name("track_event");
  perfetto::protos::gen::TrackEventConfig te_config;
  te_config.add_disabled_categories("*");
  for (const auto& category : trace_config->GetEnabledCategories())
    te_config.add_enabled_categories(category);
  ds_config->set_track_event_config_raw(te_config.SerializeAsString());

  tracing_session_ =
      perfetto::Tracing::NewTrace(perfetto::BackendType::kUnspecifiedBackend);
  tracing_session_->Setup(perfetto_trace_config);
  tracing_session_->StartBlocking();

#endif  // V8_USE_PERFETTO

  trace_config_.reset(trace_config);
  recording_.store(true, std::memory_order_release);

#ifndef V8_USE_PERFETTO
  std::unordered_set<v8::TracingController::TraceStateObserver*> observers_copy;
  {
    base::MutexGuard lock(mutex_.get());
    UpdateCategoryGroupEnabledFlags();
    observers_copy = observers_;
  }
  for (auto o : observers_copy) {
    o->OnTraceEnabled();
  }
#endif
}

void TracingController::StopTracing() {
  bool expected = true;
  if (!recording_.compare_exchange_strong(expected, false)) {
    return;
  }
#ifndef V8_USE_PERFETTO
  UpdateCategoryGroupEnabledFlags();
  std::unordered_set<v8::TracingController::TraceStateObserver*> observers_copy;
  {
    base::MutexGuard lock(mutex_.get());
    observers_copy = observers_;
  }
  for (auto o : observers_copy) {
    o->OnTraceDisabled();
  }
#endif

#ifdef V8_USE_PERFETTO
  tracing_session_->StopBlocking();

  std::vector<char> trace = tracing_session_->ReadTraceBlocking();
  std::unique_ptr<uint8_t[]> trace_bytes(new uint8_t[trace.size()]);
  std::copy(&trace[0], &trace[0] + trace.size(), &trace_bytes[0]);
  trace_processor_->Parse(std::move(trace_bytes), trace.size());
  trace_processor_->NotifyEndOfFile();
  JsonOutputWriter output_writer(output_stream_);
  auto status = perfetto::trace_processor::json::ExportJson(
      trace_processor_.get(), &output_writer, nullptr, nullptr, nullptr);
  DCHECK(status.ok());

  if (listener_for_testing_) listener_for_testing_->ParseFromArray(trace);

  trace_processor_.reset();
#else

  {
    base::MutexGuard lock(mutex_.get());
    DCHECK(trace_buffer_);
    trace_buffer_->Flush();
  }
#endif  // V8_USE_PERFETTO
}

#if !defined(V8_USE_PERFETTO)
void TracingController::UpdateCategoryGroupEnabledFlag(size_t category_index) {
  unsigned char enabled_flag = 0;
  const char* category_group = g_category_groups[category_index];
  if (recording_.load(std::memory_order_acquire) &&
      trace_config_->IsCategoryGroupEnabled(category_group)) {
    enabled_flag |= ENABLED_FOR_RECORDING;
  }

  // TODO(fmeawad): EventCallback and ETW modes are not yet supported in V8.
  // TODO(primiano): this is a temporary workaround for catapult:#2341,
  // to guarantee that metadata events are always added even if the category
  // filter is "-*". See crbug.com/618054 for more details and long-term fix.
  if (recording_.load(std::memory_order_acquire) &&
      !strcmp(category_group, "__metadata")) {
    enabled_flag |= ENABLED_FOR_RECORDING;
  }

  base::Relaxed_Store(reinterpret_cast<base::Atomic8*>(
                          g_category_group_enabled + category_index),
                      enabled_flag);
}

void TracingController::UpdateCategoryGroupEnabledFlags() {
  size_t category_index = base::Acquire_Load(&g_category_index);
  for (size_t i = 0; i < category_index; i++) UpdateCategoryGroupEnabledFlag(i);
}

const uint8_t* TracingController::GetCategoryGroupEnabled(
    const char* category_group) {
  // Check that category group does not contain double quote
  DCHECK(!strchr(category_group, '"'));

  // The g_category_groups is append only, avoid using a lock for the fast path.
  size_t category_index = base::Acquire_Load(&g_category_index);

  // Search for pre-existing category group.
  for (size_t i = 0; i < category_index; ++i) {
    if (strcmp(g_category_groups[i], category_group) == 0) {
      return &g_category_group_enabled[i];
    }
  }

  // Slow path. Grab the lock.
  base::MutexGuard lock(mutex_.get());

  // Check the list again with lock in hand.
  unsigned char* category_group_enabled = nullptr;
  category_index = base::Acquire_Load(&g_category_index);
  for (size_t i = 0; i < category_index; ++i) {
    if (strcmp(g_category_groups[i], category_group) == 0) {
      return &g_category_group_enabled[i];
    }
  }

  // Create a new category group.
  // Check that there is a slot for the new category_group.
  DCHECK(category_index < kMaxCategoryGroups);
  if (category_index < kMaxCategoryGroups) {
    // Don't hold on to the category_group pointer, so that we can create
    // category groups with strings not known at compile time (this is
    // required by SetWatchEvent).
    const char* new_group = strdup(category_group);
    g_category_groups[category_index] = new_group;
    DCHECK(!g_category_group_enabled[category_index]);
    // Note that if both included and excluded patterns in the
    // TraceConfig are empty, we exclude nothing,
    // thereby enabling this category group.
    UpdateCategoryGroupEnabledFlag(category_index);
    category_group_enabled = &g_category_group_enabled[category_index];
    // Update the max index now.
    base::Release_Store(&g_category_index, category_index + 1);
  } else {
    category_group_enabled =
        &g_category_group_enabled[g_category_categories_exhausted];
  }
  return category_group_enabled;
}

void TracingController::AddTraceStateObserver(
    v8::TracingController::TraceStateObserver* observer) {
  {
    base::MutexGuard lock(mutex_.get());
    observers_.insert(observer);
    if (!recording_.load(std::memory_order_acquire)) return;
  }
  // Fire the observer if recording is already in progress.
  observer->OnTraceEnabled();
}

void TracingController::RemoveTraceStateObserver(
    v8::TracingController::TraceStateObserver* observer) {
  base::MutexGuard lock(mutex_.get());
  DCHECK(observers_.find(observer) != observers_.end());
  observers_.erase(observer);
}
#endif  // !defined(V8_USE_PERFETTO)

}  // namespace tracing
}  // namespace platform
}  // namespace v8
