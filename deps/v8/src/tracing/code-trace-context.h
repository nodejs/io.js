// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TRACING_CODE_TRACE_CONTEXT_H_
#define V8_TRACING_CODE_TRACE_CONTEXT_H_

#include <string>

#include "protos/perfetto/trace/chrome/v8.pbzero.h"
#include "src/base/compiler-specific.h"
#include "src/objects/tagged.h"
#include "src/tracing/code-data-source.h"

namespace v8 {
namespace internal {

class Isolate;
class Script;
class SharedFunctionInfo;

// Helper class to write V8 related trace packets.
// Used to intern various types and to set common trace proto fields.
class CodeTraceContext {
 public:
  CodeTraceContext(CodeDataSource::TraceContext::TracePacketHandle trace_packet,
                   CodeDataSourceIncrementalState* incremental_state)
      : trace_packet_(std::move(trace_packet)),
        incremental_state_(*incremental_state) {}

  CodeTraceContext(CodeTraceContext&&) V8_NOEXCEPT = default;

  ~CodeTraceContext() {
    if (V8_UNLIKELY(incremental_state_.has_buffered_interned_data())) {
      incremental_state_.FlushInternedData(trace_packet_);
    }
  }

  uint64_t InternIsolate(Isolate& isolate) {
    return incremental_state_.InternIsolate(isolate);
  }

  uint64_t InternJsScript(Isolate& isolate, Tagged<Script> script) {
    return incremental_state_.InternJsScript(isolate, script);
  }

  uint64_t InternJsFunction(Isolate& isolate, Handle<SharedFunctionInfo> info,
                            uint64_t v8_js_script_iid, int line_num,
                            int column_num) {
    return incremental_state_.InternJsFunction(isolate, info, v8_js_script_iid,
                                               line_num, column_num);
  }

  uint64_t InternWasmScript(Isolate& isolate, int script_id,
                            const std::string& url) {
    return incremental_state_.InternWasmScript(isolate, script_id, url);
  }

  perfetto::protos::pbzero::V8JsCode* set_v8_js_code() {
    return trace_packet_->set_v8_js_code();
  }

  perfetto::protos::pbzero::V8InternalCode* set_v8_internal_code() {
    return trace_packet_->set_v8_internal_code();
  }

  perfetto::protos::pbzero::V8WasmCode* set_v8_wasm_code() {
    return trace_packet_->set_v8_wasm_code();
  }

  perfetto::protos::pbzero::V8RegExpCode* set_v8_reg_exp_code() {
    return trace_packet_->set_v8_reg_exp_code();
  }

  perfetto::protos::pbzero::V8CodeMove* set_code_move() {
    return trace_packet_->set_v8_code_move();
  }

  bool log_script_sources() const {
    return incremental_state_.log_script_sources();
  }

  bool log_instructions() const {
    return incremental_state_.log_instructions();
  }

 private:
  CodeDataSource::TraceContext::TracePacketHandle trace_packet_;
  CodeDataSourceIncrementalState& incremental_state_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_TRACING_CODE_TRACE_CONTEXT_H_
