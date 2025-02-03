// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/tracing/tracing-category-observer.h"

#include "src/base/atomic-utils.h"
#include "src/init/v8.h"
#include "src/logging/counters.h"
#include "src/logging/tracing-flags.h"
#include "src/tracing/trace-event.h"

namespace v8 {
namespace tracing {

TracingCategoryObserver* TracingCategoryObserver::instance_ = nullptr;

void TracingCategoryObserver::SetUp() {
  TracingCategoryObserver::instance_ = new TracingCategoryObserver();
#if defined(V8_USE_PERFETTO)
  TrackEvent::AddSessionObserver(instance_);
  // Fire the observer if tracing is already in progress.
  if (TrackEvent::IsEnabled()) instance_->OnStart({});
#else
  i::V8::GetCurrentPlatform()->GetTracingController()->AddTraceStateObserver(
      TracingCategoryObserver::instance_);
#endif
}

void TracingCategoryObserver::TearDown() {
#if defined(V8_USE_PERFETTO)
  TrackEvent::RemoveSessionObserver(TracingCategoryObserver::instance_);
#else
  i::V8::GetCurrentPlatform()->GetTracingController()->RemoveTraceStateObserver(
      TracingCategoryObserver::instance_);
#endif
  delete TracingCategoryObserver::instance_;
}

#if defined(V8_USE_PERFETTO)
void TracingCategoryObserver::OnStart(
    const perfetto::DataSourceBase::StartArgs&) {
#else
void TracingCategoryObserver::OnTraceEnabled() {
#endif
  bool enabled = false;
  TRACE_EVENT_CATEGORY_GROUP_ENABLED(
      TRACE_DISABLED_BY_DEFAULT("v8.runtime_stats"), &enabled);
  if (enabled) {
    i::TracingFlags::runtime_stats.fetch_or(ENABLED_BY_TRACING,
                                            std::memory_order_relaxed);
  }
  TRACE_EVENT_CATEGORY_GROUP_ENABLED(
      TRACE_DISABLED_BY_DEFAULT("v8.runtime_stats_sampling"), &enabled);
  if (enabled) {
    i::TracingFlags::runtime_stats.fetch_or(ENABLED_BY_SAMPLING,
                                            std::memory_order_relaxed);
  }
  TRACE_EVENT_CATEGORY_GROUP_ENABLED(TRACE_DISABLED_BY_DEFAULT("v8.gc"),
                                     &enabled);
  if (enabled) {
    i::TracingFlags::gc.fetch_or(ENABLED_BY_TRACING, std::memory_order_relaxed);
  }
  TRACE_EVENT_CATEGORY_GROUP_ENABLED(TRACE_DISABLED_BY_DEFAULT("v8.gc_stats"),
                                     &enabled);
  if (enabled) {
    i::TracingFlags::gc_stats.fetch_or(ENABLED_BY_TRACING,
                                       std::memory_order_relaxed);
  }
  TRACE_EVENT_CATEGORY_GROUP_ENABLED(TRACE_DISABLED_BY_DEFAULT("v8.ic_stats"),
                                     &enabled);
  if (enabled) {
    i::TracingFlags::ic_stats.fetch_or(ENABLED_BY_TRACING,
                                       std::memory_order_relaxed);
  }

  TRACE_EVENT_CATEGORY_GROUP_ENABLED(TRACE_DISABLED_BY_DEFAULT("v8.zone_stats"),
                                     &enabled);
  if (enabled) {
    i::TracingFlags::zone_stats.fetch_or(ENABLED_BY_TRACING,
                                         std::memory_order_relaxed);
  }
}

#if defined(V8_USE_PERFETTO)
void TracingCategoryObserver::OnStop(
    const perfetto::DataSourceBase::StopArgs&) {
#else
void TracingCategoryObserver::OnTraceDisabled() {
#endif
  i::TracingFlags::runtime_stats.fetch_and(
      ~(ENABLED_BY_TRACING | ENABLED_BY_SAMPLING), std::memory_order_relaxed);

  i::TracingFlags::gc.fetch_and(~ENABLED_BY_TRACING, std::memory_order_relaxed);

  i::TracingFlags::gc_stats.fetch_and(~ENABLED_BY_TRACING,
                                      std::memory_order_relaxed);

  i::TracingFlags::ic_stats.fetch_and(~ENABLED_BY_TRACING,
                                      std::memory_order_relaxed);
}

}  // namespace tracing
}  // namespace v8
