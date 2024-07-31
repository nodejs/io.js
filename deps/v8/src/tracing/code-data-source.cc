// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/tracing/code-data-source.h"

#include "protos/perfetto/common/data_source_descriptor.gen.h"
#include "protos/perfetto/config/chrome/v8_config.gen.h"
#include "protos/perfetto/trace/chrome/v8.pbzero.h"
#include "src/execution/isolate.h"
#include "src/handles/handles.h"
#include "src/heap/code-range.h"
#include "src/objects/function-kind.h"
#include "src/objects/script.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/string-inl.h"
#include "src/tracing/perfetto-logger.h"
#include "src/tracing/perfetto-utils.h"

PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(v8::internal::CodeDataSource,
                                           v8::internal::CodeDataSourceTraits);

namespace v8 {
namespace internal {
namespace {

using ::perfetto::protos::gen::V8Config;
using ::perfetto::protos::pbzero::InternedV8JsFunction;
using ::perfetto::protos::pbzero::InternedV8JsScript;
using ::perfetto::protos::pbzero::InternedV8String;
using ::perfetto::protos::pbzero::TracePacket;

InternedV8JsScript::Type GetJsScriptType(Tagged<Script> script) {
  if (script->compilation_type() == Script::CompilationType::kEval) {
    return InternedV8JsScript::TYPE_EVAL;
  }

  // TODO(carlscab): Camillo to extend the Script::Type enum. compilation_type
  // will no longer be needed.

  switch (script->type()) {
    case Script::Type::kNative:
      return InternedV8JsScript::TYPE_NATIVE;
    case Script::Type::kExtension:
      return InternedV8JsScript::TYPE_EXTENSION;
    case Script::Type::kNormal:
      return InternedV8JsScript::TYPE_NORMAL;
#if V8_ENABLE_WEBASSEMBLY
    case Script::Type::kWasm:
      UNREACHABLE();
#endif  // V8_ENABLE_WEBASSEMBLY
    case Script::Type::kInspector:
      return InternedV8JsScript::TYPE_INSPECTOR;
  }
}

InternedV8JsFunction::Kind GetJsFunctionKind(FunctionKind kind) {
  switch (kind) {
    case FunctionKind::kNormalFunction:
      return InternedV8JsFunction::KIND_NORMAL_FUNCTION;
    case FunctionKind::kModule:
      return InternedV8JsFunction::KIND_MODULE;
    case FunctionKind::kAsyncModule:
      return InternedV8JsFunction::KIND_ASYNC_MODULE;
    case FunctionKind::kBaseConstructor:
      return InternedV8JsFunction::KIND_BASE_CONSTRUCTOR;
    case FunctionKind::kDefaultBaseConstructor:
      return InternedV8JsFunction::KIND_DEFAULT_BASE_CONSTRUCTOR;
    case FunctionKind::kDefaultDerivedConstructor:
      return InternedV8JsFunction::KIND_DEFAULT_DERIVED_CONSTRUCTOR;
    case FunctionKind::kDerivedConstructor:
      return InternedV8JsFunction::KIND_DERIVED_CONSTRUCTOR;
    case FunctionKind::kGetterFunction:
      return InternedV8JsFunction::KIND_GETTER_FUNCTION;
    case FunctionKind::kStaticGetterFunction:
      return InternedV8JsFunction::KIND_STATIC_GETTER_FUNCTION;
    case FunctionKind::kSetterFunction:
      return InternedV8JsFunction::KIND_SETTER_FUNCTION;
    case FunctionKind::kStaticSetterFunction:
      return InternedV8JsFunction::KIND_STATIC_SETTER_FUNCTION;
    case FunctionKind::kArrowFunction:
      return InternedV8JsFunction::KIND_ARROW_FUNCTION;
    case FunctionKind::kAsyncArrowFunction:
      return InternedV8JsFunction::KIND_ASYNC_ARROW_FUNCTION;
    case FunctionKind::kAsyncFunction:
      return InternedV8JsFunction::KIND_ASYNC_FUNCTION;
    case FunctionKind::kAsyncConciseMethod:
      return InternedV8JsFunction::KIND_ASYNC_CONCISE_METHOD;
    case FunctionKind::kStaticAsyncConciseMethod:
      return InternedV8JsFunction::KIND_STATIC_ASYNC_CONCISE_METHOD;
    case FunctionKind::kAsyncConciseGeneratorMethod:
      return InternedV8JsFunction::KIND_ASYNC_CONCISE_GENERATOR_METHOD;
    case FunctionKind::kStaticAsyncConciseGeneratorMethod:
      return InternedV8JsFunction::KIND_STATIC_ASYNC_CONCISE_GENERATOR_METHOD;
    case FunctionKind::kAsyncGeneratorFunction:
      return InternedV8JsFunction::KIND_ASYNC_GENERATOR_FUNCTION;
    case FunctionKind::kGeneratorFunction:
      return InternedV8JsFunction::KIND_GENERATOR_FUNCTION;
    case FunctionKind::kConciseGeneratorMethod:
      return InternedV8JsFunction::KIND_CONCISE_GENERATOR_METHOD;
    case FunctionKind::kStaticConciseGeneratorMethod:
      return InternedV8JsFunction::KIND_STATIC_CONCISE_GENERATOR_METHOD;
    case FunctionKind::kConciseMethod:
      return InternedV8JsFunction::KIND_CONCISE_METHOD;
    case FunctionKind::kStaticConciseMethod:
      return InternedV8JsFunction::KIND_STATIC_CONCISE_METHOD;
    case FunctionKind::kClassMembersInitializerFunction:
      return InternedV8JsFunction::KIND_CLASS_MEMBERS_INITIALIZER_FUNCTION;
    case FunctionKind::kClassStaticInitializerFunction:
      return InternedV8JsFunction::KIND_CLASS_STATIC_INITIALIZER_FUNCTION;
    case FunctionKind::kInvalid:
      return InternedV8JsFunction::KIND_INVALID;
  }

  return InternedV8JsFunction::KIND_UNKNOWN;
}

}  // namespace

void CodeDataSourceIncrementalState::Init(
    const CodeDataSource::TraceContext& context) {
  if (auto ds = context.GetDataSourceLocked(); ds) {
    const V8Config& config = ds->config();
    log_script_sources_ = config.log_script_sources();
    log_instructions_ = config.log_instructions();
  }
  initialized_ = true;
}

void CodeDataSourceIncrementalState::FlushInternedData(
    CodeDataSource::TraceContext::TracePacketHandle& packet) {
  auto ranges = serialized_interned_data_.GetRanges();
  packet->AppendScatteredBytes(TracePacket::kInternedDataFieldNumber,
                               &ranges[0], ranges.size());
  serialized_interned_data_.Reset();
}

uint64_t CodeDataSourceIncrementalState::InternIsolate(Isolate& isolate) {
  auto [it, was_inserted] = isolates_.emplace(isolate.id(), next_isolate_iid());
  uint64_t iid = it->second;
  if (!was_inserted) {
    return iid;
  }

  auto* isolate_proto = serialized_interned_data_->add_v8_isolate();
  isolate_proto->set_iid(iid);
  isolate_proto->set_isolate_id(isolate.id());
  isolate_proto->set_pid(base::OS::GetCurrentProcessId());
  isolate_proto->set_embedded_blob_code_start_address(
      reinterpret_cast<uint64_t>(isolate.embedded_blob_code()));
  isolate_proto->set_embedded_blob_code_size(isolate.embedded_blob_code_size());
  if (auto* code_range = isolate.heap()->code_range(); code_range != nullptr) {
    auto* v8_code_range = isolate_proto->set_code_range();
    v8_code_range->set_base_address(code_range->base());
    v8_code_range->set_size(code_range->size());
    if (code_range == CodeRange::GetProcessWideCodeRange()) {
      v8_code_range->set_is_process_wide(true);
    }
    if (auto* embedded_builtins_start = code_range->embedded_blob_code_copy();
        embedded_builtins_start != nullptr) {
      v8_code_range->set_embedded_blob_code_copy_start_address(
          reinterpret_cast<uint64_t>(embedded_builtins_start));
    }
  }

  return iid;
}

uint64_t CodeDataSourceIncrementalState::InternJsScript(Isolate& isolate,
                                                        Tagged<Script> script) {
  auto [it, was_inserted] = scripts_.emplace(
      CodeDataSourceIncrementalState::ScriptUniqueId{isolate.id(),
                                                     script->id()},
      next_script_iid());
  uint64_t iid = it->second;
  if (!was_inserted) {
    return iid;
  }

  auto* proto = serialized_interned_data_->add_v8_js_script();
  proto->set_iid(iid);
  proto->set_script_id(script->id());
  proto->set_type(GetJsScriptType(script));
  if (IsString(script->name())) {
    PerfettoV8String(String::cast(script->name()))
        .WriteToProto(*proto->set_name());
  }
  if (log_script_sources() && IsString(script->source())) {
    PerfettoV8String(String::cast(script->source()))
        .WriteToProto(*proto->set_source());
  }

  return iid;
}

uint64_t CodeDataSourceIncrementalState::InternJsFunction(
    Isolate& isolate, Handle<SharedFunctionInfo> info,
    uint64_t v8_js_script_iid, int line_num, int column_num) {
  Handle<String> function_name = SharedFunctionInfo::DebugName(&isolate, info);
  uint64_t v8_js_function_name_iid = InternJsFunctionName(*function_name);

  auto [it, was_inserted] = functions_.emplace(
      CodeDataSourceIncrementalState::Function{
          v8_js_script_iid, info->is_toplevel(), info->StartPosition()},
      next_function_iid());
  const uint64_t iid = it->second;
  if (!was_inserted) {
    return iid;
  }

  auto* function_proto = serialized_interned_data_->add_v8_js_function();
  function_proto->set_iid(iid);
  function_proto->set_v8_js_function_name_iid(v8_js_function_name_iid);
  function_proto->set_v8_js_script_iid(v8_js_script_iid);
  function_proto->set_kind(GetJsFunctionKind(info->kind()));
  int32_t start_position = info->StartPosition();
  if (start_position >= 0) {
    function_proto->set_byte_offset(static_cast<uint32_t>(start_position));
  }

  return iid;
}

uint64_t CodeDataSourceIncrementalState::InternWasmScript(
    Isolate& isolate, int script_id, const std::string& url) {
  auto [it, was_inserted] = scripts_.emplace(
      CodeDataSourceIncrementalState::ScriptUniqueId{isolate.id(), script_id},
      next_script_iid());
  uint64_t iid = it->second;
  if (!was_inserted) {
    return iid;
  }

  auto* script = serialized_interned_data_->add_v8_wasm_script();
  script->set_iid(iid);
  script->set_script_id(script_id);
  script->set_url(url);

  // TODO(carlscab): Log scrip source if needed.

  return iid;
}

uint64_t CodeDataSourceIncrementalState::InternJsFunctionName(
    Tagged<String> function_name) {
  auto [it, was_inserted] = js_function_names_.emplace(
      PerfettoV8String(function_name), next_js_function_name_iid());
  uint64_t iid = it->second;
  if (!was_inserted) {
    return iid;
  }

  auto* v8_function_name = serialized_interned_data_->add_v8_js_function_name();
  v8_function_name->set_iid(iid);
  it->first.WriteToProto(*v8_function_name);
  return iid;
}

// static
void CodeDataSource::Register() {
  perfetto::DataSourceDescriptor desc;
  desc.set_name("dev.v8.code");
  Base::Register(desc);
}

void CodeDataSource::OnSetup(const SetupArgs& args) {
  config_.ParseFromString(args.config->v8_config_raw());
}

void CodeDataSource::OnStart(const StartArgs&) {
  PerfettoLogger::OnCodeDataSourceStart();
}

void CodeDataSource::OnStop(const StopArgs&) {
  PerfettoLogger::OnCodeDataSourceStop();
}

}  // namespace internal
}  // namespace v8
