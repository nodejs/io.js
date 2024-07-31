// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ic/ic-stats.h"

#include "src/init/v8.h"
#include "src/logging/counters.h"
#include "src/objects/objects-inl.h"
#include "src/tracing/trace-event.h"
#include "src/tracing/traced-value.h"

namespace v8 {
namespace internal {

base::LazyInstance<ICStats>::type ICStats::instance_ =
    LAZY_INSTANCE_INITIALIZER;

ICStats::ICStats() : ic_infos_(MAX_IC_INFO), pos_(0) {
  base::Relaxed_Store(&enabled_, 0);
}

void ICStats::Begin() {
  if (V8_LIKELY(!TracingFlags::is_ic_stats_enabled())) return;
  base::Relaxed_Store(&enabled_, 1);
}

void ICStats::End() {
  if (base::Relaxed_Load(&enabled_) != 1) return;
  ++pos_;
  if (pos_ == MAX_IC_INFO) {
    Dump();
  }
  base::Relaxed_Store(&enabled_, 0);
}

void ICStats::Reset() {
  for (auto ic_info : ic_infos_) {
    ic_info.Reset();
  }
  pos_ = 0;
}

void ICStats::Dump() {
  auto value = v8::tracing::TracedValue::Create();
  value->BeginArray("data");
  for (int i = 0; i < pos_; ++i) {
    ic_infos_[i].AppendToTracedValue(value.get());
  }
  value->EndArray();

  TRACE_EVENT_INSTANT1(TRACE_DISABLED_BY_DEFAULT("v8.ic_stats"), "V8.ICStats",
                       TRACE_EVENT_SCOPE_THREAD, "ic-stats", std::move(value));
  Reset();
}

const char* ICStats::GetOrCacheScriptName(Tagged<Script> script) {
  Address script_ptr = script.ptr();
  if (script_name_map_.find(script_ptr) != script_name_map_.end()) {
    return script_name_map_[script_ptr].get();
  }
  Tagged<Object> script_name_raw = script->name();
  if (IsString(script_name_raw)) {
    Tagged<String> script_name = String::cast(script_name_raw);
    char* c_script_name =
        script_name->ToCString(DISALLOW_NULLS, ROBUST_STRING_TRAVERSAL)
            .release();
    script_name_map_.insert(
        std::make_pair(script_ptr, std::unique_ptr<char[]>(c_script_name)));
    return c_script_name;
  }
  script_name_map_.insert(
      std::make_pair(script_ptr, std::unique_ptr<char[]>(nullptr)));
  return nullptr;
}

const char* ICStats::GetOrCacheFunctionName(IsolateForSandbox isolate,
                                            Tagged<JSFunction> function) {
  Address function_ptr = function.ptr();
  // Lookup the function name or add a null unique_ptr if no entry exists.
  std::unique_ptr<char[]>& function_name = function_name_map_[function_ptr];
  if (!function_name) {
    ic_infos_[pos_].is_optimized = function->HasAttachedOptimizedCode(isolate);
    // Update the map entry with the actual debug name.
    function_name = function->shared()->DebugNameCStr();
  }
  return function_name.get();
}

ICInfo::ICInfo()
    : function_name(nullptr),
      script_offset(0),
      script_name(nullptr),
      line_num(-1),
      column_num(-1),
      is_constructor(false),
      is_optimized(false),
      map(nullptr),
      is_dictionary_map(false),
      number_of_own_descriptors(0) {}

void ICInfo::Reset() {
  type.clear();
  function_name = nullptr;
  script_offset = 0;
  script_name = nullptr;
  line_num = -1;
  column_num = -1;
  is_constructor = false;
  is_optimized = false;
  state.clear();
  map = nullptr;
  is_dictionary_map = false;
  number_of_own_descriptors = 0;
  instance_type.clear();
}

void ICInfo::AppendToTracedValue(v8::tracing::TracedValue* value) const {
  value->BeginDictionary();
  value->SetString("type", type);
  if (function_name) {
    value->SetString("functionName", function_name);
    if (is_optimized) {
      value->SetInteger("optimized", is_optimized);
    }
  }
  if (script_offset) value->SetInteger("offset", script_offset);
  if (script_name) value->SetString("scriptName", script_name);
  if (line_num != -1) value->SetInteger("lineNum", line_num);
  if (column_num != -1) value->SetInteger("columnNum", column_num);
  if (is_constructor) value->SetInteger("constructor", is_constructor);
  if (!state.empty()) value->SetString("state", state);
  if (map) {
    // V8 cannot represent integer above 2^53 - 1 in JavaScript from JSON,
    // thus `map` should be converted to a string rather than an integer.
    std::stringstream ss;
    ss << map;
    value->SetString("map", ss.str());
  }
  if (map) value->SetInteger("dict", is_dictionary_map);
  if (map) value->SetInteger("own", number_of_own_descriptors);
  if (!instance_type.empty()) value->SetString("instanceType", instance_type);
  value->EndDictionary();
}

}  // namespace internal
}  // namespace v8
