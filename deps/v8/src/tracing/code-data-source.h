// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TRACING_CODE_DATA_SOURCE_H_
#define V8_TRACING_CODE_DATA_SOURCE_H_

#include <cstdint>
#include <string>
#include <unordered_map>

#include "perfetto/protozero/scattered_heap_buffer.h"
#include "perfetto/tracing/data_source.h"
#include "protos/perfetto/config/chrome/v8_config.gen.h"
#include "protos/perfetto/trace/interned_data/interned_data.pbzero.h"
#include "src/base/functional.h"
#include "src/handles/handles.h"
#include "src/objects/function-kind.h"
#include "src/objects/tagged.h"
#include "src/tracing/perfetto-utils.h"

namespace v8 {
namespace internal {

class CodeDataSourceIncrementalState;
class Isolate;
class Script;
class SharedFunctionInfo;

struct CodeDataSourceTraits : public perfetto::DefaultDataSourceTraits {
  using IncrementalStateType = CodeDataSourceIncrementalState;
  using TlsStateType = void;
};

class CodeDataSource
    : public perfetto::DataSource<CodeDataSource, CodeDataSourceTraits> {
 public:
  static void Register();

  void OnSetup(const SetupArgs&) override;
  void OnStart(const StartArgs&) override;
  void OnStop(const StopArgs&) override;

  const perfetto::protos::gen::V8Config& config() const { return config_; }

 private:
  using Base = DataSource<CodeDataSource, CodeDataSourceTraits>;

  int num_active_instances = 0;
  perfetto::protos::gen::V8Config config_;
};

class CodeDataSourceIncrementalState {
 public:
  CodeDataSourceIncrementalState() = default;
  void Init(const CodeDataSource::TraceContext& context);

  bool has_buffered_interned_data() const {
    return !serialized_interned_data_.empty();
  }

  void FlushInternedData(
      CodeDataSource::TraceContext::TracePacketHandle& packet);

  uint64_t InternIsolate(Isolate& isolate);
  uint64_t InternJsScript(Isolate& isolate, Tagged<Script> script);
  uint64_t InternJsFunction(Isolate& isolate, Handle<SharedFunctionInfo> info,
                            uint64_t v8_js_script_iid, int line_num,
                            int column_num);
  uint64_t InternWasmScript(Isolate& isolate, int script_id,
                            const std::string& url);

  bool is_initialized() const { return initialized_; }
  bool log_script_sources() const { return log_script_sources_; }
  bool log_instructions() const { return log_instructions_; }

 private:
  using JsFunctionNameIid = uint64_t;
  struct Function {
    uint64_t v8_js_script_iid;
    bool is_toplevel;
    int32_t start_position;

    bool operator==(const Function& other) const {
      return v8_js_script_iid == other.v8_js_script_iid &&
             is_toplevel == other.is_toplevel &&
             start_position == other.start_position;
    }

    bool operator!=(const Function& other) const { return !(*this == other); }

    struct Hash {
      size_t operator()(const Function& f) const {
        return base::Hasher::Combine(f.v8_js_script_iid, f.is_toplevel,
                                     f.start_position);
      }
    };
  };

  struct ScriptUniqueId {
    int isolate_id;
    int script_id;
    bool operator==(const ScriptUniqueId& other) const {
      return isolate_id == other.isolate_id && script_id == other.script_id;
    }

    bool operator!=(const ScriptUniqueId& other) const {
      return !(*this == other);
    }

    struct Hash {
      size_t operator()(const ScriptUniqueId& id) const {
        return base::Hasher::Combine(id.isolate_id, id.script_id);
      }
    };
  };

  uint64_t InternJsFunctionName(Tagged<String> function_name);

  uint64_t next_isolate_iid() const { return isolates_.size() + 1; }

  uint64_t next_script_iid() const { return scripts_.size() + 1; }

  uint64_t next_function_iid() const { return functions_.size() + 1; }

  uint64_t next_js_function_name_iid() const {
    return js_function_names_.size() + 1;
  }

  // Stores newly seen interned data while in the middle of writing a new
  // TracePacket. Interned data is serialized into this buffer and then flushed
  // to the actual trace stream when the packet ends.
  // This data is cached as part of the incremental state to reuse the
  // underlying buffer allocation.
  protozero::HeapBuffered<perfetto::protos::pbzero::InternedData>
      serialized_interned_data_;

  std::unordered_map<int, uint64_t> isolates_;
  std::unordered_map<ScriptUniqueId, uint64_t, ScriptUniqueId::Hash> scripts_;
  std::unordered_map<Function, uint64_t, Function::Hash> functions_;
  std::unordered_map<PerfettoV8String, uint64_t, PerfettoV8String::Hasher>
      js_function_names_;
  std::unordered_map<std::string, uint64_t> two_byte_function_names_;

  bool log_script_sources_ = false;
  bool log_instructions_ = false;
  bool initialized_ = false;
};

}  // namespace internal
}  // namespace v8

PERFETTO_DECLARE_DATA_SOURCE_STATIC_MEMBERS(v8::internal::CodeDataSource,
                                            v8::internal::CodeDataSourceTraits);

#endif  // V8_TRACING_CODE_DATA_SOURCE_H_
