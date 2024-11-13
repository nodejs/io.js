// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PROFILER_PROFILER_LISTENER_H_
#define V8_PROFILER_PROFILER_LISTENER_H_

#include <memory>

#include "include/v8-profiler.h"
#include "src/logging/code-events.h"
#include "src/profiler/profile-generator.h"
#include "src/profiler/weak-code-registry.h"

namespace v8 {
namespace internal {

class CodeEventsContainer;
class CodeDeoptEventRecord;

class CodeEventObserver {
 public:
  virtual void CodeEventHandler(const CodeEventsContainer& evt_rec) = 0;
  virtual ~CodeEventObserver() = default;
};

class V8_EXPORT_PRIVATE ProfilerListener : public LogEventListener,
                                           public WeakCodeRegistry::Listener {
 public:
  ProfilerListener(Isolate*, CodeEventObserver*,
                   CodeEntryStorage& code_entry_storage,
                   WeakCodeRegistry& weak_code_registry,
                   CpuProfilingNamingMode mode = kDebugNaming);
  ~ProfilerListener() override;
  ProfilerListener(const ProfilerListener&) = delete;
  ProfilerListener& operator=(const ProfilerListener&) = delete;

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
  void SharedFunctionInfoMoveEvent(Address from, Address to) override {}
  void NativeContextMoveEvent(Address from, Address to) override;
  void CodeMovingGCEvent() override {}
  void CodeDisableOptEvent(Handle<AbstractCode> code,
                           Handle<SharedFunctionInfo> shared) override;
  void CodeDeoptEvent(Handle<Code> code, DeoptimizeKind kind, Address pc,
                      int fp_to_sp_delta) override;
  void CodeDependencyChangeEvent(Handle<Code> code,
                                 Handle<SharedFunctionInfo> sfi,
                                 const char* reason) override {}
  void WeakCodeClearEvent() override;

  void OnHeapObjectDeletion(CodeEntry*) override;

  // Invoked after a mark-sweep cycle.
  void CodeSweepEvent();

  const char* GetName(Tagged<Name> name) {
    return code_entries_.strings().GetName(name);
  }
  const char* GetName(int args_count) {
    return code_entries_.strings().GetName(args_count);
  }
  const char* GetName(const char* name) {
    return code_entries_.strings().GetCopy(name);
  }
  const char* GetName(base::Vector<const char> name);
  const char* GetConsName(const char* prefix, Tagged<Name> name) {
    return code_entries_.strings().GetConsName(prefix, name);
  }

  void set_observer(CodeEventObserver* observer) { observer_ = observer; }

 private:
  const char* GetFunctionName(Tagged<SharedFunctionInfo>);

  void AttachDeoptInlinedFrames(DirectHandle<Code> code,
                                CodeDeoptEventRecord* rec);
  Tagged<Name> InferScriptName(Tagged<Name> name,
                               Tagged<SharedFunctionInfo> info);
  V8_INLINE void DispatchCodeEvent(const CodeEventsContainer& evt_rec) {
    observer_->CodeEventHandler(evt_rec);
  }

  Isolate* isolate_;
  CodeEventObserver* observer_;
  CodeEntryStorage& code_entries_;
  WeakCodeRegistry& weak_code_registry_;
  const CpuProfilingNamingMode naming_mode_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PROFILER_PROFILER_LISTENER_H_
