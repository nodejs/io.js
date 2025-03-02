// Copyright 2010 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/diagnostics/etw-jit-win.h"

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
#include "src/diagnostics/etw-isolate-load-script-data-win.h"
#include "src/diagnostics/etw-isolate-operations-win.h"
#include "src/diagnostics/etw-jit-metadata-win.h"
#include "src/logging/log.h"
#include "src/objects/shared-function-info.h"
#include "src/tasks/cancelable-task.h"
#include "src/tasks/task-utils.h"

#if !defined(V8_ENABLE_ETW_STACK_WALKING)
#error "This file is only compiled if v8_enable_etw_stack_walking"
#endif

#include <windows.h>

#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace v8 {
namespace internal {
namespace ETWJITInterface {

V8_DECLARE_TRACELOGGING_PROVIDER(g_v8Provider);
V8_DEFINE_TRACELOGGING_PROVIDER(g_v8Provider);

std::atomic<bool> has_active_etw_tracing_session_or_custom_filter = false;

void MaybeSetHandlerNow(Isolate* isolate) {
  DCHECK(v8_flags.enable_etw_stack_walking ||
         v8_flags.enable_etw_by_custom_filter_only);
  ETWTRACEDBG << "MaybeSetHandlerNow called" << std::endl;
  // Iterating read-only heap before sealed might not be safe.
  if (has_active_etw_tracing_session_or_custom_filter &&
      !EtwIsolateOperations::Instance()->HeapReadOnlySpaceWritable(isolate)) {
    if (etw_filter_payload_glob.Pointer()->empty()) {
      DCHECK(v8_flags.enable_etw_stack_walking);
      IsolateLoadScriptData::EnableLog(
          isolate, 0, std::weak_ptr<EtwIsolateCaptureStateMonitor>(),
          kJitCodeEventDefault);
    } else {
      IsolateLoadScriptData::EnableLogWithFilterData(
          isolate, 0, *etw_filter_payload_glob.Pointer(),
          std::weak_ptr<EtwIsolateCaptureStateMonitor>(), kJitCodeEventDefault);
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

void UpdateETWEnabled(bool enabled, uint32_t options) {
  DCHECK(v8_flags.enable_etw_stack_walking ||
         v8_flags.enable_etw_by_custom_filter_only);
  has_active_etw_tracing_session_or_custom_filter = enabled;

  IsolateLoadScriptData::UpdateAllIsolates(enabled, options);
}

// This callback is invoked by Windows every time the ETW tracing status is
// changed for this application. As such, V8 needs to track its value for
// knowing if the event requires us to emit JIT runtime events.
void WINAPI V8_EXPORT_PRIVATE ETWEnableCallback(
    LPCGUID /* source_id */, ULONG is_enabled, UCHAR level,
    ULONGLONG match_any_keyword, ULONGLONG match_all_keyword,
    PEVENT_FILTER_DESCRIPTOR filter_data, PVOID /* callback_context */) {
  ETWTRACEDBG << "ETWEnableCallback called with is_enabled==" << is_enabled
              << std::endl;

  bool is_etw_enabled_now =
      is_enabled && level >= kTraceLevel &&
      (match_any_keyword & kJScriptRuntimeKeyword) &&
      ((match_all_keyword & kJScriptRuntimeKeyword) == match_all_keyword);

  uint32_t options = kJitCodeEventDefault;
  if (is_enabled == kEtwControlCaptureState) {
    options |= (kEtwRundown | kJitCodeEventEnumExisting);
  }

  FilterDataType* etw_filter = etw_filter_payload_glob.Pointer();

  if (!is_etw_enabled_now) {
    // Disable the current tracing session.
    ETWTRACEDBG << "Disabling" << std::endl;
    etw_filter->clear();
    UpdateETWEnabled(false, options);
    return;
  }

  DCHECK(is_etw_enabled_now);

  // Ignore the callback if ETW tracing is not fully enabled and we are not
  // passing a custom filter.
  if (!v8_flags.enable_etw_stack_walking &&
      (!filter_data || filter_data->Type != EVENT_FILTER_TYPE_SCHEMATIZED)) {
    return;
  }

  if (v8_flags.enable_etw_stack_walking &&
      (!filter_data || filter_data->Type != EVENT_FILTER_TYPE_SCHEMATIZED)) {
    etw_filter->clear();
    ETWTRACEDBG << "Enabling without filter" << std::endl;
    UpdateETWEnabled(is_etw_enabled_now, options);
    return;
  }

  // Ignore the callback if the --enable-etw-by-custom-filter-only flag is not
  // enabled.
  if (!v8_flags.enable_etw_by_custom_filter_only) return;

  // Validate custom filter data configured in a WPR profile and passed to the
  // callback.

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
  has_active_etw_tracing_session_or_custom_filter = true;

  ETWTRACEDBG << "Enabling with filter data" << std::endl;
  IsolateLoadScriptData::EnableLogWithFilterDataOnAllIsolates(
      reinterpret_cast<const uint8_t*>(etw_filter->data()), etw_filter->size(),
      options);
}

void Register() {
  DCHECK(!TraceLoggingProviderEnabled(g_v8Provider, 0, 0));
  TraceLoggingRegisterEx(g_v8Provider, ETWEnableCallback, nullptr);
}

void Unregister() {
  if (g_v8Provider) {
    TraceLoggingUnregister(g_v8Provider);
  }
  UpdateETWEnabled(false, kJitCodeEventDefault);
}

void AddIsolate(Isolate* isolate) {
  IsolateLoadScriptData::AddIsolate(isolate);
}

void RemoveIsolate(Isolate* isolate) {
  IsolateLoadScriptData::RemoveIsolate(isolate);
}

void EventHandler(const JitCodeEvent* event) {
  v8::Isolate* script_context = event->isolate;
  Isolate* isolate = reinterpret_cast<Isolate*>(script_context);
  if (!isolate->IsETWTracingEnabled()) return;

  if (event->code_type != v8::JitCodeEvent::CodeType::JIT_CODE) return;
  if (event->type != v8::JitCodeEvent::EventType::CODE_ADDED) return;

  std::wstring method_name = GetScriptMethodName(event);

  // No heap allocations after this point.
  DisallowGarbageCollection no_gc;

  int script_id = 0;
  uint32_t script_line = -1;
  uint32_t script_column = -1;

  Tagged<SharedFunctionInfo> sfi = GetSharedFunctionInfo(event);
  if (!sfi.is_null() && IsScript(sfi->script())) {
    Tagged<Script> script = Cast<Script>(sfi->script());

    // if the first time seeing this source file, log the SourceLoad event
    script_id = script->id();
    if (IsolateLoadScriptData::MaybeAddLoadedScript(isolate, script_id)) {
      std::wstring wstr_name(0, L'\0');
      Tagged<Object> script_name = script->GetNameOrSourceURL();
      if (IsString(script_name)) {
        Tagged<String> v8str_name = Cast<String>(script_name);
        wstr_name.resize(v8str_name->length());
        // On Windows wchar_t == uint16_t. const_Cast needed for C++14.
        uint16_t* wstr_data = const_cast<uint16_t*>(
            reinterpret_cast<const uint16_t*>(wstr_name.data()));
        String::WriteToFlat(v8str_name, wstr_data, 0, v8str_name->length());
      }

      if (isolate->ETWIsInRundown()) {
        constexpr static auto source_dcstart_event_meta =
            EventMetadata(kSourceDCStartEventID, kJScriptRuntimeKeyword);
        constexpr static auto source_dcstart_event_fields =
            EventFields("SourceDCStart", Field("SourceID", TlgInUINT64),
                        Field("ScriptContextID", TlgInPOINTER),
                        Field("SourceFlags", TlgInUINT32),
                        Field("Url", TlgInUNICODESTRING));
        LogEventData(g_v8Provider, &source_dcstart_event_meta,
                     &source_dcstart_event_fields, (uint64_t)script_id,
                     script_context,
                     (uint32_t)0,  // SourceFlags
                     wstr_name);
      } else {
        constexpr static auto source_load_event_meta =
            EventMetadata(kSourceLoadEventID, kJScriptRuntimeKeyword);
        constexpr static auto source_load_event_fields =
            EventFields("SourceLoad", Field("SourceID", TlgInUINT64),
                        Field("ScriptContextID", TlgInPOINTER),
                        Field("SourceFlags", TlgInUINT32),
                        Field("Url", TlgInUNICODESTRING));
        LogEventData(g_v8Provider, &source_load_event_meta,
                     &source_load_event_fields, (uint64_t)script_id,
                     script_context,
                     (uint32_t)0,  // SourceFlags
                     wstr_name);
      }
    }

    Script::PositionInfo info;
    script->GetPositionInfo(sfi->StartPosition(), &info);
    script_line = info.line + 1;
    script_column = info.column + 1;
  }

  auto code =
      EtwIsolateOperations::Instance()->HeapGcSafeTryFindCodeForInnerPointer(
          isolate, Address(event->code_start));
  if (code && code.value()->is_builtin()) {
    bool skip_emitting_builtin = true;
    // Skip logging functions with code kind BUILTIN as they are already present
    // in the PDB.

    // We should still emit builtin addresses if they are an interpreter
    // trampoline.
    if (code.value()->has_instruction_stream()) {
      skip_emitting_builtin = false;

      // The only builtin that might have instruction stream is the
      // InterpreterEntryTrampoline builtin and only when the
      // v8_flags.interpreted_frames_native_stack flag is enabled.
      DCHECK_IMPLIES(
          code.value()->is_builtin(),
          code.value()->builtin_id() == Builtin::kInterpreterEntryTrampoline &&
              isolate->interpreted_frames_native_stack());
    } else {
      DCHECK(code.value()->is_builtin());
    }

    // If the builtin has been relocated, we still need to emit the address
    if (skip_emitting_builtin && V8_SHORT_BUILTIN_CALLS_BOOL &&
        v8_flags.short_builtin_calls) {
      CodeRange* code_range = isolate->isolate_group()->GetCodeRange();
      if (code_range && code_range->embedded_blob_code_copy() != nullptr) {
        skip_emitting_builtin = false;
      }
    }

    if (skip_emitting_builtin) {
      return;
    }
  }

  if (isolate->ETWIsInRundown()) {
    constexpr static auto method_dcstart_event_meta =
        EventMetadata(kMethodDCStartEventID, kJScriptRuntimeKeyword);
    constexpr static auto method_dcstart_event_fields = EventFields(
        "MethodDCStart", Field("ScriptContextID", TlgInPOINTER),
        Field("MethodStartAddress", TlgInPOINTER),
        Field("MethodSize", TlgInUINT64), Field("MethodID", TlgInUINT32),
        Field("MethodFlags", TlgInUINT16),
        Field("MethodAddressRangeID", TlgInUINT16),
        Field("SourceID", TlgInUINT64), Field("Line", TlgInUINT32),
        Field("Column", TlgInUINT32), Field("MethodName", TlgInUNICODESTRING));
    LogEventData(g_v8Provider, &method_dcstart_event_meta,
                 &method_dcstart_event_fields, script_context,
                 event->code_start, (uint64_t)event->code_len,
                 (uint32_t)0,  // MethodId
                 (uint16_t)0,  // MethodFlags
                 (uint16_t)0,  // MethodAddressRangeId
                 (uint64_t)script_id, script_line, script_column, method_name);
  } else {
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
    LogEventData(g_v8Provider, &method_load_event_meta,
                 &method_load_event_fields, script_context, event->code_start,
                 (uint64_t)event->code_len,
                 (uint32_t)0,  // MethodId
                 (uint16_t)0,  // MethodFlags
                 (uint16_t)0,  // MethodAddressRangeId
                 (uint64_t)script_id, script_line, script_column, method_name);
  }
}

}  // namespace ETWJITInterface
}  // namespace internal
}  // namespace v8
