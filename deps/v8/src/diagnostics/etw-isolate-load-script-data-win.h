// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DIAGNOSTICS_ETW_ISOLATE_LOAD_SCRIPT_DATA_WIN_H_
#define V8_DIAGNOSTICS_ETW_ISOLATE_LOAD_SCRIPT_DATA_WIN_H_

#include <windows.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "include/v8-isolate.h"
#include "src/base/lazy-instance.h"
#include "src/diagnostics/etw-isolate-capture-state-monitor-win.h"

namespace v8 {
namespace internal {
namespace ETWJITInterface {

class V8_EXPORT_PRIVATE IsolateLoadScriptData {
 public:
  explicit IsolateLoadScriptData(Isolate* isolate);
  explicit IsolateLoadScriptData(IsolateLoadScriptData&& rhs) V8_NOEXCEPT;

  static void AddIsolate(Isolate* isolate);
  static void RemoveIsolate(Isolate* isolate);
  static void UpdateAllIsolates(bool etw_enabled, uint32_t options);
  static bool MaybeAddLoadedScript(Isolate* isolate, int script_id);
  static void EnableLog(
      Isolate* isolate, size_t event_id,
      std::weak_ptr<EtwIsolateCaptureStateMonitor> weak_monitor,
      uint32_t options);
  static void DisableLog(Isolate* isolate, size_t event_id);

  static void EnableLogWithFilterDataOnAllIsolates(const uint8_t* data,
                                                   size_t size,
                                                   uint32_t options);
  static void EnableLogWithFilterData(
      Isolate* isolate, size_t event_id,
      const std::string& EnableLogWithFilterData,
      std::weak_ptr<EtwIsolateCaptureStateMonitor> weak_monitor,
      uint32_t options);

 private:
  static IsolateLoadScriptData& GetData(Isolate* isolate);

  struct EnableInterruptData {
    size_t event_id;
    std::weak_ptr<EtwIsolateCaptureStateMonitor> weak_monitor;
    uint32_t options;
  };

  void EnqueueEnableLog(
      std::weak_ptr<EtwIsolateCaptureStateMonitor> weak_monitor,
      uint32_t options);

  struct EnableWithFilterDataInterruptData {
    size_t event_id;
    std::string payload;
    std::weak_ptr<EtwIsolateCaptureStateMonitor> weak_monitor;
    uint32_t options;
  };

  void EnqueueEnableLogWithFilterData(
      const std::string& etw_filter_payload,
      std::weak_ptr<EtwIsolateCaptureStateMonitor> weak_monitor,
      uint32_t options);

  void EnqueueDisableLog();

  bool IsScriptLoaded(int script_id) const;
  void AddLoadedScript(int script_id);
  void RemoveAllLoadedScripts();

  size_t CurrentEventId() const;

  Isolate* isolate_ = nullptr;
  std::unordered_set<int> loaded_scripts_ids_;
  std::atomic<size_t> event_id_ = 0;
};

extern base::LazyMutex isolates_mutex;

using IsolateMapType =
    std::unordered_map<v8::internal::Isolate*, IsolateLoadScriptData>;
extern base::LazyInstance<IsolateMapType>::type isolate_map;

using FilterDataType = std::string;
// Used when Isolates are created during an ETW tracing session.
extern base::LazyInstance<FilterDataType>::type etw_filter_payload_glob;

}  // namespace ETWJITInterface
}  // namespace internal
}  // namespace v8

#endif  // V8_DIAGNOSTICS_ETW_ISOLATE_LOAD_SCRIPT_DATA_WIN_H_
