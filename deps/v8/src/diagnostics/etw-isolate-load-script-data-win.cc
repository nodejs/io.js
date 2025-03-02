// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/diagnostics/etw-isolate-load-script-data-win.h"

#include <windows.h>

#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "include/v8-callbacks.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "include/v8-primitive.h"
#include "include/v8-script.h"
#include "src/api/api-inl.h"
#include "src/base/lazy-instance.h"
#include "src/base/logging.h"
#include "src/base/platform/platform.h"
#include "src/diagnostics/etw-debug-win.h"
#include "src/diagnostics/etw-isolate-capture-state-monitor-win.h"
#include "src/diagnostics/etw-isolate-operations-win.h"
#include "src/diagnostics/etw-jit-metadata-win.h"
#include "src/diagnostics/etw-jit-win.h"
#include "src/logging/log.h"
#include "src/objects/shared-function-info.h"
#include "src/tasks/cancelable-task.h"
#include "src/tasks/task-utils.h"

namespace v8 {
namespace internal {
namespace ETWJITInterface {

constexpr auto kCaptureStateTimeout = base::TimeDelta::FromSeconds(10);

IsolateLoadScriptData::IsolateLoadScriptData(Isolate* isolate)
    : isolate_(isolate) {}
IsolateLoadScriptData::IsolateLoadScriptData(IsolateLoadScriptData&& rhs)
    V8_NOEXCEPT {
  isolate_ = rhs.isolate_;
  loaded_scripts_ids_ = std::move(rhs.loaded_scripts_ids_);
  event_id_ = rhs.event_id_.load();
}

// static
void IsolateLoadScriptData::AddIsolate(Isolate* isolate) {
  base::MutexGuard guard(isolates_mutex.Pointer());
  isolate_map.Pointer()->emplace(isolate, IsolateLoadScriptData(isolate));
}

// static
void IsolateLoadScriptData::RemoveIsolate(Isolate* isolate) {
  base::MutexGuard guard(isolates_mutex.Pointer());
  isolate_map.Pointer()->erase(isolate);
}

// static
void IsolateLoadScriptData::UpdateAllIsolates(bool etw_enabled,
                                              uint32_t options) {
  ETWTRACEDBG << "UpdateAllIsolates with etw_enabled==" << etw_enabled
              << " and options==" << options << " acquiring mutex" << std::endl;
  base::MutexGuard guard(isolates_mutex.Pointer());
  ETWTRACEDBG << "UpdateAllIsolates Isolate count=="
              << isolate_map.Pointer()->size() << std::endl;
  auto monitor = std::make_shared<EtwIsolateCaptureStateMonitor>(
      isolates_mutex.Pointer(), isolate_map.Pointer()->size());
  bool capture_state =
      (options & kJitCodeEventEnumExisting) == kJitCodeEventEnumExisting;
  std::weak_ptr<EtwIsolateCaptureStateMonitor> weak_monitor = monitor;
  std::for_each(
      isolate_map.Pointer()->begin(), isolate_map.Pointer()->end(),
      [etw_enabled, weak_monitor, options](auto& pair) {
        auto& isolate_data = pair.second;
        if (etw_enabled) {
          ETWTRACEDBG << "UpdateAllIsolates enqueing enablelog" << std::endl;
          isolate_data.EnqueueEnableLog(weak_monitor, options);
        } else {
          ETWTRACEDBG << "UpdateAllIsolates enqueing disablelog" << std::endl;
          isolate_data.EnqueueDisableLog();
        }
      });

  if (!capture_state) {
    return;
  }

  ETWTRACEDBG << "UpdateAllIsolates starting WaitFor" << std::endl;
  bool timeout = !monitor->WaitFor(kCaptureStateTimeout);
  ETWTRACEDBG << "UpdateAllIsolates WaitFor "
              << (timeout ? "timeout" : "completed") << std::endl;
}

// static
bool IsolateLoadScriptData::MaybeAddLoadedScript(Isolate* isolate,
                                                 int script_id) {
  base::MutexGuard guard(isolates_mutex.Pointer());
  auto& data = GetData(isolate);
  if (data.IsScriptLoaded(script_id)) {
    return false;
  }
  data.AddLoadedScript(script_id);
  return true;
}

// static
void IsolateLoadScriptData::EnableLog(
    Isolate* isolate, size_t event_id,
    std::weak_ptr<EtwIsolateCaptureStateMonitor> weak_monitor,
    uint32_t options) {
  {
    ETWTRACEDBG << "EnableLog called with event_id==" << event_id
                << " and options==" << options << " taking mutex" << std::endl;
    base::MutexGuard guard(isolates_mutex.Pointer());
    auto& data = GetData(isolate);
    if (event_id > 0 && data.CurrentEventId() != event_id) {
      // This interrupt was canceled by a newer interrupt.
      return;
    }

    // Cause all SourceLoad events to be re-emitted.
    if (options & kJitCodeEventEnumExisting) {
      data.RemoveAllLoadedScripts();
    }
  }

  ETWTRACEDBG << "Mutex released with event_id==" << event_id << std::endl;

  // This cannot be done while isolate_mutex is locked, as it can call
  // EventHandler while in the call for all the existing code.
  EtwIsolateOperations::Instance()->SetEtwCodeEventHandler(isolate, options);
  isolate->SetETWTracingEnabled(true);

  // Notify waiting thread if a monitor was provided.
  if (auto monitor = weak_monitor.lock()) {
    ETWTRACEDBG << "monitor->Notify with event_id==" << event_id << std::endl;
    monitor->Notify();
  }
}

// static
void IsolateLoadScriptData::EnableLogWithFilterDataOnAllIsolates(
    const uint8_t* data, size_t size, uint32_t options) {
  base::MutexGuard guard(isolates_mutex.Pointer());

  std::string etw_filter_payload;
  etw_filter_payload.assign(data, data + size);
  auto monitor = std::make_shared<EtwIsolateCaptureStateMonitor>(
      isolates_mutex.Pointer(), isolate_map.Pointer()->size());
  bool capture_state =
      (options & kJitCodeEventEnumExisting) == kJitCodeEventEnumExisting;
  std::weak_ptr<EtwIsolateCaptureStateMonitor> weak_monitor = monitor;
  std::for_each(isolate_map.Pointer()->begin(), isolate_map.Pointer()->end(),
                [&etw_filter_payload, weak_monitor, options](auto& pair) {
                  auto& isolate_data = pair.second;
                  isolate_data.EnqueueEnableLogWithFilterData(
                      etw_filter_payload, weak_monitor, options);
                });

  if (!capture_state) {
    return;
  }

  bool timeout = !monitor->WaitFor(kCaptureStateTimeout);
  ETWTRACEDBG << "EnableLogWithFilterDataOnAllIsolates WaitFor "
              << (timeout ? "timeout" : "completed") << std::endl;
}

// static
void IsolateLoadScriptData::DisableLog(Isolate* isolate, size_t event_id) {
  {
    base::MutexGuard guard(isolates_mutex.Pointer());
    auto& data = GetData(isolate);
    if (event_id > 0 && data.CurrentEventId() != event_id) {
      // This interrupt was canceled by a newer interrupt.
      return;
    }
    data.RemoveAllLoadedScripts();
  }
  EtwIsolateOperations::Instance()->ResetEtwCodeEventHandler(isolate);
  isolate->SetETWTracingEnabled(false);
}

// static
void IsolateLoadScriptData::EnableLogWithFilterData(
    Isolate* isolate, size_t event_id, const std::string& etw_filter_payload,
    std::weak_ptr<EtwIsolateCaptureStateMonitor> weak_monitor,
    uint32_t options) {
  bool filter_did_match = false;
  DCHECK(!etw_filter_payload.empty());

  {
    ETWTRACEDBG << "EnableLogWithFilterData called with event_id==" << event_id
                << " and options==" << options << " taking mutex" << std::endl;
    base::MutexGuard guard(isolates_mutex.Pointer());

    auto& data = GetData(isolate);
    if (event_id > 0 && data.CurrentEventId() != event_id) {
      // This interrupt was canceled by a newer interrupt.
      return;
    }

    FilterETWSessionByURLResult filter_etw_session_by_url_result =
        EtwIsolateOperations::Instance()->RunFilterETWSessionByURLCallback(
            isolate, etw_filter_payload);
    filter_did_match = filter_etw_session_by_url_result.enable_etw_tracing;
    if (filter_did_match) {
      if (filter_etw_session_by_url_result.trace_interpreter_frames) {
        isolate->set_etw_trace_interpreted_frames();
      }

      // Cause all SourceLoad events to be re-emitted.
      if (options & kJitCodeEventEnumExisting) {
        data.RemoveAllLoadedScripts();
      }
    }
  }

  if (filter_did_match) {
    ETWTRACEDBG << "Filter was matched with event_id==" << event_id
                << std::endl;
    EtwIsolateOperations::Instance()->SetEtwCodeEventHandler(isolate, options);
    isolate->SetETWTracingEnabled(true);
  }

  // Notify waiting thread if a monitor was provided.
  if (auto monitor = weak_monitor.lock()) {
    ETWTRACEDBG << "monitor->Notify with event_id==" << event_id << std::endl;
    monitor->Notify();
  }
}

// static
IsolateLoadScriptData& IsolateLoadScriptData::GetData(Isolate* isolate) {
  return isolate_map.Pointer()->at(isolate);
}

void IsolateLoadScriptData::EnqueueEnableLog(
    std::weak_ptr<EtwIsolateCaptureStateMonitor> weak_monitor,
    uint32_t options) {
  size_t event_id = event_id_.fetch_add(1);
  EtwIsolateOperations::Instance()->RequestInterrupt(
      isolate_,
      // Executed in the isolate thread.
      [](v8::Isolate* v8_isolate, void* data) {
        std::unique_ptr<EnableInterruptData> interrupt_data(
            reinterpret_cast<EnableInterruptData*>(data));
        size_t event_id = interrupt_data->event_id;
        auto weak_monitor = interrupt_data->weak_monitor;
        uint32_t options = interrupt_data->options;
        EnableLog(reinterpret_cast<Isolate*>(v8_isolate), event_id,
                  weak_monitor, options);
      },
      new EnableInterruptData{event_id + 1, weak_monitor, options});
}

void IsolateLoadScriptData::EnqueueDisableLog() {
  size_t event_id = event_id_.fetch_add(1);
  EtwIsolateOperations::Instance()->RequestInterrupt(
      isolate_,
      // Executed in the isolate thread.
      [](v8::Isolate* v8_isolate, void* data) {
        DisableLog(reinterpret_cast<Isolate*>(v8_isolate),
                   reinterpret_cast<size_t>(data));
      },
      reinterpret_cast<void*>(event_id + 1));
}

void IsolateLoadScriptData::EnqueueEnableLogWithFilterData(
    const std::string& etw_filter_payload,
    std::weak_ptr<EtwIsolateCaptureStateMonitor> weak_monitor,
    uint32_t options) {
  size_t event_id = event_id_.fetch_add(1);
  EtwIsolateOperations::Instance()->RequestInterrupt(
      isolate_,
      // Executed in the isolate thread.
      [](v8::Isolate* v8_isolate, void* data) {
        std::unique_ptr<EnableWithFilterDataInterruptData> interrupt_data(
            reinterpret_cast<EnableWithFilterDataInterruptData*>(data));
        size_t event_id = interrupt_data->event_id;
        std::string etw_filter_payload = interrupt_data->payload;
        auto weak_monitor = interrupt_data->weak_monitor;
        uint32_t options = interrupt_data->options;
        EnableLogWithFilterData(reinterpret_cast<Isolate*>(v8_isolate),
                                event_id, etw_filter_payload, weak_monitor,
                                options);
      },
      new EnableWithFilterDataInterruptData{event_id + 1, etw_filter_payload,
                                            weak_monitor, options});
}

bool IsolateLoadScriptData::IsScriptLoaded(int script_id) const {
  return loaded_scripts_ids_.find(script_id) != loaded_scripts_ids_.end();
}
void IsolateLoadScriptData::AddLoadedScript(int script_id) {
  loaded_scripts_ids_.insert(script_id);
}
void IsolateLoadScriptData::RemoveAllLoadedScripts() {
  loaded_scripts_ids_.clear();
}

size_t IsolateLoadScriptData::CurrentEventId() const {
  return event_id_.load();
}

base::LazyMutex isolates_mutex = LAZY_MUTEX_INITIALIZER;
base::LazyInstance<IsolateMapType>::type isolate_map =
    LAZY_INSTANCE_INITIALIZER;
base::LazyInstance<FilterDataType>::type etw_filter_payload_glob =
    LAZY_INSTANCE_INITIALIZER;

}  // namespace ETWJITInterface
}  // namespace internal
}  // namespace v8
