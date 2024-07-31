// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TRACING_PERFETTO_LOGGER_H_
#define V8_TRACING_PERFETTO_LOGGER_H_

#include "src/logging/code-events.h"

namespace v8 {
namespace internal {

class Isolate;

// Implementation that writes events to a Perfetto data source.
class PerfettoLogger : public LogEventListener {
 public:
  static void RegisterIsolate(Isolate* isolate);
  static void UnregisterIsolate(Isolate* isolate);
  static void OnCodeDataSourceStart();
  static void OnCodeDataSourceStop();

  explicit PerfettoLogger(Isolate* isolate);
  ~PerfettoLogger() override;

  void LogExistingCode();

  void CodeCreateEvent(CodeTag tag, Handle<AbstractCode> code,
                       const char* name) override;
  void CodeCreateEvent(CodeTag tag, Handle<AbstractCode> code,
                       Handle<Name> name) override;
  void CodeCreateEvent(CodeTag tag, Handle<AbstractCode> code,
                       Handle<SharedFunctionInfo> shared,
                       Handle<Name> script_name) override;
  void CodeCreateEvent(CodeTag tag, Handle<AbstractCode> code,
                       Handle<SharedFunctionInfo> shared,
                       Handle<Name> script_name, int line, int column) override;
#if V8_ENABLE_WEBASSEMBLY
  void CodeCreateEvent(CodeTag tag, const wasm::WasmCode* code,
                       wasm::WasmName name, const char* source_url,
                       int code_offset, int script_id) override;
#endif  // V8_ENABLE_WEBASSEMBLY

  void CallbackEvent(Handle<Name> name, Address entry_point) override;
  void GetterCallbackEvent(Handle<Name> name, Address entry_point) override;
  void SetterCallbackEvent(Handle<Name> name, Address entry_point) override;
  void RegExpCodeCreateEvent(Handle<AbstractCode> code,
                             Handle<String> source) override;
  void CodeMoveEvent(Tagged<InstructionStream> from,
                     Tagged<InstructionStream> to) override;
  void BytecodeMoveEvent(Tagged<BytecodeArray> from,
                         Tagged<BytecodeArray> to) override;
  void SharedFunctionInfoMoveEvent(Address from, Address to) override;
  void NativeContextMoveEvent(Address from, Address to) override;
  void CodeMovingGCEvent() override;
  void CodeDisableOptEvent(Handle<AbstractCode> code,
                           Handle<SharedFunctionInfo> shared) override;
  void CodeDeoptEvent(Handle<Code> code, DeoptimizeKind kind, Address pc,
                      int fp_to_sp_delta) override;
  void CodeDependencyChangeEvent(Handle<Code> code,
                                 Handle<SharedFunctionInfo> shared,
                                 const char* reason) override;
  void WeakCodeClearEvent() override;
  bool is_listening_to_code_events() override;

 private:
  Isolate& isolate_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_TRACING_PERFETTO_LOGGER_H_
