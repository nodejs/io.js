// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/tracing/perfetto-logger.h"

#include <memory>

#include "absl/container/flat_hash_map.h"
#include "protos/perfetto/common/builtin_clock.pbzero.h"
#include "protos/perfetto/trace/chrome/v8.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "protos/perfetto/trace/trace_packet_defaults.pbzero.h"
#include "src/base/logging.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/platform.h"
#include "src/base/platform/time.h"
#include "src/builtins/builtins.h"
#include "src/execution/isolate.h"
#include "src/handles/handles.h"
#include "src/heap/spaces.h"
#include "src/logging/log.h"
#include "src/objects/abstract-code.h"
#include "src/objects/code-kind.h"
#include "src/objects/heap-object.h"
#include "src/objects/objects-inl.h"
#include "src/objects/oddball.h"
#include "src/objects/script.h"
#include "src/objects/string.h"
#include "src/objects/tagged.h"
#include "src/tracing/code-data-source.h"
#include "src/tracing/code-trace-context.h"
#include "src/tracing/perfetto-utils.h"

namespace v8 {
namespace internal {
namespace {

using ::perfetto::protos::pbzero::BuiltinClock;
using ::perfetto::protos::pbzero::TracePacket;
using ::perfetto::protos::pbzero::V8InternalCode;
using ::perfetto::protos::pbzero::V8JsCode;

CodeDataSource::TraceContext::TracePacketHandle NewTracePacket(
    CodeDataSource::TraceContext& context) {
  CodeDataSourceIncrementalState* inc_state = context.GetIncrementalState();
  auto packet = context.NewTracePacket();
  packet->set_timestamp(base::TimeTicks::Now().since_origin().InNanoseconds());

  if (inc_state->is_initialized()) {
    packet->set_sequence_flags(TracePacket::SEQ_NEEDS_INCREMENTAL_STATE);
    return packet;
  }

  inc_state->Init(context);

  packet->set_sequence_flags(TracePacket::SEQ_INCREMENTAL_STATE_CLEARED);

  auto* defaults = packet->set_trace_packet_defaults();
  defaults->set_timestamp_clock_id(BuiltinClock::BUILTIN_CLOCK_MONOTONIC);

  auto* v8_defaults = defaults->set_v8_code_defaults();
  v8_defaults->set_tid(base::OS::GetCurrentThreadId());

  return packet;
}

CodeTraceContext NewCodeTraceContext(CodeDataSource::TraceContext& ctx) {
  return CodeTraceContext(NewTracePacket(ctx), ctx.GetIncrementalState());
}

class IsolateRegistry {
 public:
  static IsolateRegistry& GetInstance() {
    static IsolateRegistry* g_instance = new IsolateRegistry();
    return *g_instance;
  }

  void Register(Isolate* isolate) {
    auto logger = std::make_unique<PerfettoLogger>(isolate);
    base::MutexGuard lock(&mutex_);
    if (num_active_data_sources_ != 0) {
      isolate->logger()->AddListener(logger.get());
    }
    CHECK(isolates_.emplace(isolate, std::move(logger)).second);
  }

  void Unregister(Isolate* isolate) {
    base::MutexGuard lock(&mutex_);
    auto it = isolates_.find(isolate);
    CHECK(it != isolates_.end());
    if (num_active_data_sources_ != 0) {
      isolate->logger()->RemoveListener(it->second.get());
    }
    isolates_.erase(it);
  }

  void OnCodeDataSourceStart() {
    base::MutexGuard lock(&mutex_);
    ++num_active_data_sources_;
    if (num_active_data_sources_ == 1) {
      StartLogging(lock);
    }
    LogExistingCodeForAllIsolates(lock);
  }

  void OnCodeDataSourceStop() {
    base::MutexGuard lock(&mutex_);
    DCHECK_LT(0, num_active_data_sources_);
    --num_active_data_sources_;
    if (num_active_data_sources_ == 0) {
      StopLogging(lock);
    }
  }

 private:
  void StartLogging(const base::MutexGuard&) {
    for (const auto& [isolate, logger] : isolates_) {
      isolate->logger()->AddListener(logger.get());
    }
  }

  void StopLogging(const base::MutexGuard&) {
    for (const auto& [isolate, logger] : isolates_) {
      isolate->logger()->RemoveListener(logger.get());
    }
  }

  void LogExistingCodeForAllIsolates(const base::MutexGuard&) {
    for (const auto& [isolate, listener] : isolates_) {
      isolate->RequestInterrupt(
          [](v8::Isolate*, void* data) {
            PerfettoLogger* listener = reinterpret_cast<PerfettoLogger*>(data);
            listener->LogExistingCode();
          },
          listener.get());
    }
  }

  base::Mutex mutex_;
  int num_active_data_sources_ = 0;
  absl::flat_hash_map<Isolate*, std::unique_ptr<PerfettoLogger>> isolates_;
};

void WriteJsCode(const CodeTraceContext& ctx,
                 Tagged<AbstractCode> abstract_code, V8JsCode& code_proto) {
  if (IsBytecodeArray(abstract_code)) {
    Tagged<BytecodeArray> bytecode = abstract_code->GetBytecodeArray();
    code_proto.set_tier(V8JsCode::TIER_IGNITION);
    code_proto.set_instruction_start(bytecode->GetFirstBytecodeAddress());
    code_proto.set_instruction_size_bytes(bytecode->length());
    if (ctx.log_instructions()) {
      code_proto.set_bytecode(
          reinterpret_cast<const uint8_t*>(bytecode->GetFirstBytecodeAddress()),
          bytecode->length());
    }
    return;
  }

  DCHECK(IsCode(abstract_code));
  Tagged<Code> code = abstract_code->GetCode();

  V8JsCode::Tier tier = V8JsCode::TIER_UNKNOWN;
  switch (code->kind()) {
    case CodeKind::BUILTIN:
      if (code->builtin_id() == Builtin::kInterpreterEntryTrampoline) {
        DCHECK(v8_flags.interpreted_frames_native_stack);
        DCHECK(code->has_instruction_stream());
        tier = V8JsCode::TIER_IGNITION;
        break;
      }

      // kEmptyFunction is used as a placeholder sometimes.
      DCHECK_EQ(code->builtin_id(), Builtin::kEmptyFunction);
      DCHECK(!code->has_instruction_stream());
      return;

    case CodeKind::INTERPRETED_FUNCTION:
      // Handled above.
      UNREACHABLE();

    case CodeKind::BASELINE:
      tier = V8JsCode::TIER_SPARKPLUG;
      break;
    case CodeKind::MAGLEV:
      tier = V8JsCode::TIER_MAGLEV;
      break;
    case CodeKind::TURBOFAN:
      tier = V8JsCode::TIER_TURBOFAN;
      break;

    case CodeKind::BYTECODE_HANDLER:
    case CodeKind::FOR_TESTING:
    case CodeKind::REGEXP:
    case CodeKind::WASM_FUNCTION:
    case CodeKind::WASM_TO_CAPI_FUNCTION:
    case CodeKind::WASM_TO_JS_FUNCTION:
    case CodeKind::JS_TO_WASM_FUNCTION:
    case CodeKind::C_WASM_ENTRY:
      UNREACHABLE();
  }

  code_proto.set_tier(tier);
  code_proto.set_instruction_start(code->instruction_start());
  code_proto.set_instruction_size_bytes(code->instruction_size());
  if (ctx.log_instructions()) {
    code_proto.set_machine_code(
        reinterpret_cast<const uint8_t*>(code->instruction_start()),
        code->instruction_size());
  }
}

}  // namespace

// static
void PerfettoLogger::RegisterIsolate(Isolate* isolate) {
  IsolateRegistry::GetInstance().Register(isolate);
  // TODO(carlscab): Actually if both perfetto and file logging are active the
  // builtins will be logged twice to the file (EmitCodeCreateEvents is called
  // somewhere in the isolate setup code). Probably not very likely to happen
  // but we should find a better way.
  CodeDataSource::CallIfEnabled(
      [isolate](uint32_t) { Builtins::EmitCodeCreateEvents(isolate); });
}

// static
void PerfettoLogger::UnregisterIsolate(Isolate* isolate) {
  IsolateRegistry::GetInstance().Unregister(isolate);
}

// static
void PerfettoLogger::OnCodeDataSourceStart() {
  IsolateRegistry::GetInstance().OnCodeDataSourceStart();
}

// static
void PerfettoLogger::OnCodeDataSourceStop() {
  IsolateRegistry::GetInstance().OnCodeDataSourceStop();
}

void PerfettoLogger::LogExistingCode() {
  HandleScope scope(&isolate_);
  ExistingCodeLogger logger(&isolate_, this);
  logger.LogBuiltins();
  logger.LogCodeObjects();
  logger.LogCompiledFunctions();
}

PerfettoLogger::PerfettoLogger(Isolate* isolate) : isolate_(*isolate) {}
PerfettoLogger::~PerfettoLogger() {}

void PerfettoLogger::CodeCreateEvent(CodeTag tag,
                                     Handle<AbstractCode> abstract_code,
                                     const char* name) {
  DisallowGarbageCollection no_gc;
  if (!IsCode(*abstract_code)) return;
  Tagged<Code> code = abstract_code->GetCode();

  V8InternalCode::Type type = V8InternalCode::TYPE_UNKNOWN;
  switch (code->kind()) {
    case CodeKind::REGEXP:
      RegExpCodeCreateEvent(abstract_code, Handle<String>());
      break;
    case CodeKind::BYTECODE_HANDLER:
      type = V8InternalCode::TYPE_BYTECODE_HANDLER;
      break;
    case CodeKind::FOR_TESTING:
      type = V8InternalCode::TYPE_FOR_TESTING;
      break;
    case CodeKind::BUILTIN:
      type = V8InternalCode::TYPE_BUILTIN;
      break;
    case CodeKind::WASM_FUNCTION:
      type = V8InternalCode::TYPE_WASM_FUNCTION;
      break;
    case CodeKind::WASM_TO_CAPI_FUNCTION:
      type = V8InternalCode::TYPE_WASM_TO_CAPI_FUNCTION;
      break;
    case CodeKind::WASM_TO_JS_FUNCTION:
      type = V8InternalCode::TYPE_WASM_TO_JS_FUNCTION;
      break;
    case CodeKind::JS_TO_WASM_FUNCTION:
      type = V8InternalCode::TYPE_JS_TO_WASM_FUNCTION;
      break;
    case CodeKind::C_WASM_ENTRY:
      type = V8InternalCode::TYPE_C_WASM_ENTRY;
      break;

    case CodeKind::INTERPRETED_FUNCTION:
    case CodeKind::BASELINE:
    case CodeKind::MAGLEV:
    case CodeKind::TURBOFAN:
      UNREACHABLE();
  }

  CodeDataSource::Trace(
      [&](v8::internal::CodeDataSource::TraceContext trace_context) {
        CodeTraceContext ctx = NewCodeTraceContext(trace_context);

        auto* code_proto = ctx.set_v8_internal_code();
        code_proto->set_v8_isolate_iid(ctx.InternIsolate(isolate_));
        code_proto->set_name(name);
        code_proto->set_type(type);
        if (code->is_builtin()) {
          code_proto->set_builtin_id(static_cast<int32_t>(code->builtin_id()));
        }
        code_proto->set_instruction_start(code->instruction_start());
        code_proto->set_instruction_size_bytes(code->instruction_size());
        if (ctx.log_instructions()) {
          code_proto->set_machine_code(
              reinterpret_cast<const uint8_t*>(code->instruction_start()),
              code->instruction_size());
        }
      });
}

void PerfettoLogger::CodeCreateEvent(CodeTag tag,
                                     Handle<AbstractCode> abstract_code,
                                     Handle<Name> name) {
  DisallowGarbageCollection no_gc;
  if (!IsString(*name)) return;
  CodeCreateEvent(tag, abstract_code, Cast<String>(*name)->ToCString().get());
}

void PerfettoLogger::CodeCreateEvent(CodeTag tag,
                                     Handle<AbstractCode> abstract_code,
                                     Handle<SharedFunctionInfo> info,
                                     Handle<Name> script_name) {
  CodeCreateEvent(tag, abstract_code, info, script_name, 0, 0);
}

void PerfettoLogger::CodeCreateEvent(CodeTag tag,
                                     Handle<AbstractCode> abstract_code,
                                     Handle<SharedFunctionInfo> info,
                                     Handle<Name> script_name, int line,
                                     int column) {
  DisallowGarbageCollection no_gc;
  DCHECK(IsScript(info->script()));

  CodeDataSource::Trace(
      [&](v8::internal::CodeDataSource::TraceContext trace_context) {
        CodeTraceContext ctx = NewCodeTraceContext(trace_context);

        auto* code_proto = ctx.set_v8_js_code();
        code_proto->set_v8_isolate_iid(ctx.InternIsolate(isolate_));
        code_proto->set_v8_js_function_iid(ctx.InternJsFunction(
            isolate_, info,
            ctx.InternJsScript(isolate_, Cast<Script>(info->script())), line,
            column));
        WriteJsCode(ctx, *abstract_code, *code_proto);
      });
}
#if V8_ENABLE_WEBASSEMBLY
void PerfettoLogger::CodeCreateEvent(CodeTag tag, const wasm::WasmCode* code,
                                     wasm::WasmName name,
                                     const char* source_url, int code_offset,
                                     int script_id) {
  DisallowGarbageCollection no_gc;

  CodeDataSource::Trace(
      [&](v8::internal::CodeDataSource::TraceContext trace_context) {
        CodeTraceContext ctx = NewCodeTraceContext(trace_context);

        auto* code_proto = ctx.set_v8_wasm_code();
        code_proto->set_v8_isolate_iid(ctx.InternIsolate(isolate_));
        code_proto->set_v8_wasm_script_iid(
            ctx.InternWasmScript(isolate_, script_id, source_url));
        code_proto->set_function_name(name.begin(), name.size());
        // TODO(carlscab): Set tier
        code_proto->set_instruction_start(code->instruction_start());
        code_proto->set_instruction_size_bytes(code->instructions_size());
        if (ctx.log_instructions()) {
          code_proto->set_machine_code(
              reinterpret_cast<const uint8_t*>(code->instruction_start()),
              code->instructions_size());
        }
      });
}
#endif  // V8_ENABLE_WEBASSEMBLY

void PerfettoLogger::CallbackEvent(Handle<Name> name, Address entry_point) {}
void PerfettoLogger::GetterCallbackEvent(Handle<Name> name,
                                         Address entry_point) {}
void PerfettoLogger::SetterCallbackEvent(Handle<Name> name,
                                         Address entry_point) {}
void PerfettoLogger::RegExpCodeCreateEvent(Handle<AbstractCode> abstract_code,
                                           Handle<String> pattern) {
  DisallowGarbageCollection no_gc;
  DCHECK(IsCode(*abstract_code));
  Tagged<Code> code = abstract_code->GetCode();
  DCHECK(code->kind() == CodeKind::REGEXP);

  CodeDataSource::Trace(
      [&](v8::internal::CodeDataSource::TraceContext trace_context) {
        CodeTraceContext ctx = NewCodeTraceContext(trace_context);

        auto* code_proto = ctx.set_v8_reg_exp_code();
        code_proto->set_v8_isolate_iid(ctx.InternIsolate(isolate_));

        if (!pattern.is_null()) {
          PerfettoV8String(*pattern).WriteToProto(*code_proto->set_pattern());
        }
        code_proto->set_instruction_start(code->instruction_start());
        code_proto->set_instruction_size_bytes(code->instruction_size());
        if (ctx.log_instructions()) {
          code_proto->set_machine_code(
              reinterpret_cast<const uint8_t*>(code->instruction_start()),
              code->instruction_size());
        }
      });
}

void PerfettoLogger::CodeMoveEvent(Tagged<InstructionStream> from,
                                   Tagged<InstructionStream> to) {
  CodeDataSource::Trace(
      [&](v8::internal::CodeDataSource::TraceContext trace_context) {
        CodeTraceContext ctx = NewCodeTraceContext(trace_context);
        auto* code_move = ctx.set_code_move();
        code_move->set_isolate_iid(ctx.InternIsolate(isolate_));
        code_move->set_from_instruction_start_address(
            from->instruction_start());
        code_move->set_to_instruction_start_address(to->instruction_start());
        Tagged<Code> code = to->code(AcquireLoadTag());
        code_move->set_instruction_size_bytes(code->instruction_size());
        if (ctx.log_instructions()) {
          code_move->set_to_machine_code(
              reinterpret_cast<const uint8_t*>(code->instruction_start()),
              code->instruction_size());
        }
      });
}
void PerfettoLogger::BytecodeMoveEvent(Tagged<BytecodeArray> from,
                                       Tagged<BytecodeArray> to) {
  CodeDataSource::Trace(
      [&](v8::internal::CodeDataSource::TraceContext trace_context) {
        CodeTraceContext ctx = NewCodeTraceContext(trace_context);
        auto* code_move = ctx.set_code_move();
        code_move->set_isolate_iid(ctx.InternIsolate(isolate_));
        code_move->set_from_instruction_start_address(
            from->GetFirstBytecodeAddress());
        code_move->set_to_instruction_start_address(
            to->GetFirstBytecodeAddress());
        code_move->set_instruction_size_bytes(to->length());
        if (ctx.log_instructions()) {
          code_move->set_to_bytecode(
              reinterpret_cast<const uint8_t*>(to->GetFirstBytecodeAddress()),
              to->length());
        }
      });
}

void PerfettoLogger::SharedFunctionInfoMoveEvent(Address from, Address to) {}
void PerfettoLogger::NativeContextMoveEvent(Address from, Address to) {}
void PerfettoLogger::CodeMovingGCEvent() {}
void PerfettoLogger::CodeDisableOptEvent(Handle<AbstractCode> code,
                                         Handle<SharedFunctionInfo> shared) {}
void PerfettoLogger::CodeDeoptEvent(Handle<Code> code, DeoptimizeKind kind,
                                    Address pc, int fp_to_sp_delta) {}
void PerfettoLogger::CodeDependencyChangeEvent(
    Handle<Code> code, Handle<SharedFunctionInfo> shared, const char* reason) {}
void PerfettoLogger::WeakCodeClearEvent() {}

bool PerfettoLogger::is_listening_to_code_events() { return true; }

}  // namespace internal
}  // namespace v8
