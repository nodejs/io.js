// Copyright 2010 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/diagnostics/etw-jit-win.h"

#include "include/v8-callbacks.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "include/v8-primitive.h"
#include "include/v8-script.h"
#include "src/api/api-inl.h"
#include "src/base/lazy-instance.h"
#include "src/base/logging.h"
#include "src/base/platform/platform.h"
#include "src/diagnostics/etw-jit-metadata-win.h"
#include "src/logging/log.h"
#include "src/objects/shared-function-info.h"
#include "src/tasks/cancelable-task.h"
#include "src/tasks/task-utils.h"

#if !defined(V8_ENABLE_ETW_STACK_WALKING)
#error "This file is only compiled if v8_enable_etw_stack_walking"
#endif

namespace v8 {
namespace internal {
namespace ETWJITInterface {

V8_DECLARE_TRACELOGGING_PROVIDER(g_v8Provider);
V8_DEFINE_TRACELOGGING_PROVIDER(g_v8Provider);

std::atomic<bool> is_etw_enabled = false;

namespace {

class IsolateLoadScriptData {
 public:
  explicit IsolateLoadScriptData(Isolate* isolate) : isolate_(isolate) {}
  explicit IsolateLoadScriptData(IsolateLoadScriptData&& rhs) V8_NOEXCEPT {
    isolate_ = rhs.isolate_;
    loaded_scripts_ids_ = std::move(rhs.loaded_scripts_ids_);
    event_id_ = rhs.event_id_.load();
  }

  static void AddIsolate(Isolate* isolate);
  static void RemoveIsolate(Isolate* isolate);
  static void UpdateAllIsolates(bool etw_enabled);
  static bool MaybeAddLoadedScript(Isolate* isolate, int script_id);
  static void EnableLog(Isolate* isolate, size_t event_id);
  static void DisableLog(Isolate* isolate, size_t event_id);

  static void EnableLogWithFilterDataOnAllIsolates(const uint8_t* data,
                                                   size_t size);
  static void EnableLogWithFilterData(
      Isolate* isolate, size_t event_id,
      const std::string& EnableLogWithFilterData);

 private:
  static IsolateLoadScriptData& GetData(Isolate* isolate);
  void EnqueueEnableLog() {
    size_t event_id = event_id_.fetch_add(1);
    isolate_->RequestInterrupt(
        // Executed in the isolate thread.
        [](v8::Isolate* v8_isolate, void* data) {
          EnableLog(reinterpret_cast<Isolate*>(v8_isolate),
                    reinterpret_cast<size_t>(data));
        },
        reinterpret_cast<void*>(event_id + 1));
  }
  void EnqueueDisableLog() {
    size_t event_id = event_id_.fetch_add(1);
    isolate_->RequestInterrupt(
        // Executed in the isolate thread.
        [](v8::Isolate* v8_isolate, void* data) {
          DisableLog(reinterpret_cast<Isolate*>(v8_isolate),
                     reinterpret_cast<size_t>(data));
        },
        reinterpret_cast<void*>(event_id + 1));
  }

  struct EnableWithFilterDataInterruptData {
    size_t event_id;
    std::string payload;
  };

  void EnqueueEnableLogWithFilterData(const std::string& etw_filter_payload) {
    size_t event_id = event_id_.fetch_add(1);
    isolate_->RequestInterrupt(
        // Executed in the isolate thread.
        [](v8::Isolate* v8_isolate, void* data) {
          std::unique_ptr<EnableWithFilterDataInterruptData> interrupt_data(
              reinterpret_cast<EnableWithFilterDataInterruptData*>(data));
          size_t event_id = interrupt_data->event_id;
          std::string etw_filter_payload = interrupt_data->payload;
          EnableLogWithFilterData(reinterpret_cast<Isolate*>(v8_isolate),
                                  event_id, etw_filter_payload);
        },
        new EnableWithFilterDataInterruptData{event_id + 1,
                                              etw_filter_payload});
  }

  bool IsScriptLoaded(int script_id) const {
    return loaded_scripts_ids_.find(script_id) != loaded_scripts_ids_.end();
  }
  void AddLoadedScript(int script_id) { loaded_scripts_ids_.insert(script_id); }
  void RemoveAllLoadedScripts() { loaded_scripts_ids_.clear(); }

  size_t CurrentEventId() const { return event_id_.load(); }

  Isolate* isolate_ = nullptr;
  std::unordered_set<int> loaded_scripts_ids_;
  std::atomic<size_t> event_id_ = 0;
};

static base::LazyMutex isolates_mutex = LAZY_MUTEX_INITIALIZER;
using IsolateMapType =
    std::unordered_map<v8::internal::Isolate*, IsolateLoadScriptData>;
static base::LazyInstance<IsolateMapType>::type isolate_map =
    LAZY_INSTANCE_INITIALIZER;

using FilterDataType = std::string;
// Used when Isolates are created during a ETW tracing session.
static base::LazyInstance<FilterDataType>::type etw_filter_payload =
    LAZY_INSTANCE_INITIALIZER;

// static
IsolateLoadScriptData& IsolateLoadScriptData::GetData(Isolate* isolate) {
  return isolate_map.Pointer()->at(isolate);
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
void IsolateLoadScriptData::EnableLog(Isolate* isolate, size_t event_id) {
  {
    base::MutexGuard guard(isolates_mutex.Pointer());
    auto& data = GetData(isolate);
    if (event_id > 0 && data.CurrentEventId() != event_id) {
      // This interrupt was canceled by a newer interrupt.
      return;
    }
  }

  // This cannot be done while isolate_mutex is locked, as it can call
  // EventHandler while in the call for all the existing code.
  isolate->v8_file_logger()->SetEtwCodeEventHandler(kJitCodeEventDefault);
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
  isolate->v8_file_logger()->ResetEtwCodeEventHandler();
}

// static
void IsolateLoadScriptData::EnableLogWithFilterData(
    Isolate* isolate, size_t event_id, const std::string& etw_filter_payload) {
  {
    base::MutexGuard guard(isolates_mutex.Pointer());
    auto& data = GetData(isolate);
    if (event_id > 0 && data.CurrentEventId() != event_id) {
      // This interrupt was canceled by a newer interrupt.
      return;
    }
  }

  DCHECK(!etw_filter_payload.empty());

  // We should not call back into V8 from the RunFilterETWSessionByURLCallback
  // callback.
  DisallowJavascriptExecution no_js(isolate);

  if (isolate->RunFilterETWSessionByURLCallback(etw_filter_payload)) {
    isolate->v8_file_logger()->SetEtwCodeEventHandler(kJitCodeEventDefault);
  }
}

// static
void IsolateLoadScriptData::EnableLogWithFilterDataOnAllIsolates(
    const uint8_t* data, size_t size) {
  base::MutexGuard guard(isolates_mutex.Pointer());
  std::string etw_filter_payload;
  etw_filter_payload.assign(data, data + size);
  std::for_each(
      isolate_map.Pointer()->begin(), isolate_map.Pointer()->end(),
      [&etw_filter_payload](auto& pair) {
        auto& isolate_data = pair.second;
        isolate_data.EnqueueEnableLogWithFilterData(etw_filter_payload);
      });
}

// static
void IsolateLoadScriptData::UpdateAllIsolates(bool etw_enabled) {
  base::MutexGuard guard(isolates_mutex.Pointer());
  std::for_each(isolate_map.Pointer()->begin(), isolate_map.Pointer()->end(),
                [etw_enabled](auto& pair) {
                  auto& isolate_data = pair.second;
                  if (etw_enabled) {
                    isolate_data.EnqueueEnableLog();
                  } else {
                    isolate_data.EnqueueDisableLog();
                  }
                });
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

}  // namespace

void MaybeSetHandlerNow(Isolate* isolate) {
  if (is_etw_enabled) {
    if (etw_filter_payload.Pointer()->empty()) {
      IsolateLoadScriptData::EnableLog(isolate, 0);
    } else {
      IsolateLoadScriptData::EnableLogWithFilterData(
          isolate, 0, *etw_filter_payload.Pointer());
    }
  }
}

// TODO(v8/11911): UnboundScript::GetLineNumber should be replaced
Tagged<SharedFunctionInfo> GetSharedFunctionInfo(const JitCodeEvent* event) {
  return event->script.IsEmpty() ? Tagged<SharedFunctionInfo>()
                                 : *Utils::OpenDirectHandle(*event->script);
}

std::wstring GetScriptMethodNameFromEvent(const JitCodeEvent* event) {
  int name_len = static_cast<int>(event->name.len);
  // Note: event->name.str is not null terminated.
  std::wstring method_name(name_len + 1, '\0');
  MultiByteToWideChar(
      CP_UTF8, 0, event->name.str, name_len,
      // Const cast needed as building with C++14 (not const in >= C++17)
      const_cast<LPWSTR>(method_name.data()),
      static_cast<int>(method_name.size()));
  return method_name;
}

std::wstring GetScriptMethodNameFromSharedFunctionInfo(
    Tagged<SharedFunctionInfo> sfi) {
  auto sfi_name = sfi->DebugNameCStr();
  int method_name_length = static_cast<int>(strlen(sfi_name.get()));
  std::wstring method_name(method_name_length, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, sfi_name.get(), method_name_length,
                      const_cast<LPWSTR>(method_name.data()),
                      static_cast<int>(method_name.length()));
  return method_name;
}

std::wstring GetScriptMethodName(const JitCodeEvent* event) {
  auto sfi = GetSharedFunctionInfo(event);
  return sfi.is_null() ? GetScriptMethodNameFromEvent(event)
                       : GetScriptMethodNameFromSharedFunctionInfo(sfi);
}

void UpdateETWEnabled(bool enabled) {
  DCHECK(v8_flags.enable_etw_stack_walking);
  if (enabled == is_etw_enabled) {
    return;
  }
  is_etw_enabled = enabled;

  IsolateLoadScriptData::UpdateAllIsolates(enabled);
}

// This callback is invoked by Windows every time the ETW tracing status is
// changed for this application. As such, V8 needs to track its value for
// knowing if the event requires us to emit JIT runtime events.
void WINAPI ETWEnableCallback(LPCGUID /* source_id */, ULONG is_enabled,
                              UCHAR level, ULONGLONG match_any_keyword,
                              ULONGLONG match_all_keyword,
                              PEVENT_FILTER_DESCRIPTOR filter_data,
                              PVOID /* callback_context */) {
  DCHECK(v8_flags.enable_etw_stack_walking);
  bool is_etw_enabled_now =
      is_enabled && level >= kTraceLevel &&
      (match_any_keyword & kJScriptRuntimeKeyword) &&
      ((match_all_keyword & kJScriptRuntimeKeyword) == match_all_keyword);

  FilterDataType* etw_filter = etw_filter_payload.Pointer();

  if (!is_etw_enabled_now || !filter_data ||
      filter_data->Type != EVENT_FILTER_TYPE_SCHEMATIZED) {
    etw_filter->clear();
    UpdateETWEnabled(is_etw_enabled_now);
    return;
  }

  if (is_etw_enabled) return;

  if (filter_data->Size <= sizeof(EVENT_FILTER_DESCRIPTOR)) {
    return;  // Invalid data
  }

  EVENT_FILTER_HEADER* filter_event_header =
      reinterpret_cast<EVENT_FILTER_HEADER*>(filter_data->Ptr);
  if (filter_event_header->Size < sizeof(EVENT_FILTER_HEADER)) {
    return;  // Invalid data
  }

  const uint8_t* payload_start =
      reinterpret_cast<uint8_t*>(filter_event_header) +
      sizeof(EVENT_FILTER_HEADER);
  const size_t payload_size =
      filter_event_header->Size - sizeof(EVENT_FILTER_HEADER);
  etw_filter->assign(payload_start, payload_start + payload_size);
  is_etw_enabled = is_etw_enabled_now;
  IsolateLoadScriptData::EnableLogWithFilterDataOnAllIsolates(
      reinterpret_cast<const uint8_t*>(etw_filter->data()), etw_filter->size());
}

void Register() {
  DCHECK(!TraceLoggingProviderEnabled(g_v8Provider, 0, 0));
  TraceLoggingRegisterEx(g_v8Provider, ETWEnableCallback, nullptr);
}

void Unregister() {
  if (g_v8Provider) {
    TraceLoggingUnregister(g_v8Provider);
  }
  UpdateETWEnabled(false);
}

void AddIsolate(Isolate* isolate) {
  IsolateLoadScriptData::AddIsolate(isolate);
}

void RemoveIsolate(Isolate* isolate) {
  IsolateLoadScriptData::RemoveIsolate(isolate);
}

void EventHandler(const JitCodeEvent* event) {
  if (!is_etw_enabled) return;
  if (event->code_type != v8::JitCodeEvent::CodeType::JIT_CODE) return;
  if (event->type != v8::JitCodeEvent::EventType::CODE_ADDED) return;

  std::wstring method_name = GetScriptMethodName(event);

  // No heap allocations after this point.
  DisallowGarbageCollection no_gc;

  v8::Isolate* script_context = event->isolate;
  Isolate* isolate = reinterpret_cast<Isolate*>(script_context);

  int script_id = 0;
  uint32_t script_line = -1;
  uint32_t script_column = -1;

  Tagged<SharedFunctionInfo> sfi = GetSharedFunctionInfo(event);
  if (!sfi.is_null() && IsScript(sfi->script())) {
    Tagged<Script> script = Script::cast(sfi->script());

    // if the first time seeing this source file, log the SourceLoad event
    script_id = script->id();
    if (IsolateLoadScriptData::MaybeAddLoadedScript(isolate, script_id)) {
      std::wstring wstr_name(0, L'\0');
      Tagged<Object> script_name = script->GetNameOrSourceURL();
      if (IsString(script_name)) {
        Tagged<String> v8str_name = String::cast(script_name);
        wstr_name.resize(v8str_name->length());
        // On Windows wchar_t == uint16_t. const_Cast needed for C++14.
        uint16_t* wstr_data = const_cast<uint16_t*>(
            reinterpret_cast<const uint16_t*>(wstr_name.data()));
        String::WriteToFlat(v8str_name, wstr_data, 0, v8str_name->length());
      }

      constexpr static auto source_load_event_meta =
          EventMetadata(kSourceLoadEventID, kJScriptRuntimeKeyword);
      constexpr static auto source_load_event_fields = EventFields(
          "SourceLoad", Field("SourceID", TlgInUINT64),
          Field("ScriptContextID", TlgInPOINTER),
          Field("SourceFlags", TlgInUINT32), Field("Url", TlgInUNICODESTRING));
      LogEventData(g_v8Provider, &source_load_event_meta,
                   &source_load_event_fields, (uint64_t)script_id,
                   script_context,
                   (uint32_t)0,  // SourceFlags
                   wstr_name);
    }

    Script::PositionInfo info;
    script->GetPositionInfo(sfi->StartPosition(), &info);
    script_line = info.line + 1;
    script_column = info.column + 1;
  }

  constexpr static auto method_load_event_meta =
      EventMetadata(kMethodLoadEventID, kJScriptRuntimeKeyword);
  constexpr static auto method_load_event_fields = EventFields(
      "MethodLoad", Field("ScriptContextID", TlgInPOINTER),
      Field("MethodStartAddress", TlgInPOINTER),
      Field("MethodSize", TlgInUINT64), Field("MethodID", TlgInUINT32),
      Field("MethodFlags", TlgInUINT16),
      Field("MethodAddressRangeID", TlgInUINT16),
      Field("SourceID", TlgInUINT64), Field("Line", TlgInUINT32),
      Field("Column", TlgInUINT32), Field("MethodName", TlgInUNICODESTRING));

  LogEventData(g_v8Provider, &method_load_event_meta, &method_load_event_fields,
               script_context, event->code_start, (uint64_t)event->code_len,
               (uint32_t)0,  // MethodId
               (uint16_t)0,  // MethodFlags
               (uint16_t)0,  // MethodAddressRangeId
               (uint64_t)script_id, script_line, script_column, method_name);
}

}  // namespace ETWJITInterface
}  // namespace internal
}  // namespace v8
