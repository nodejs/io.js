// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_LOGGING_CODE_EVENTS_H_
#define V8_LOGGING_CODE_EVENTS_H_

#include <vector>

#include "src/base/platform/mutex.h"
#include "src/base/vector.h"
#include "src/common/globals.h"
#include "src/objects/bytecode-array.h"
#include "src/objects/code.h"
#include "src/objects/instruction-stream.h"
#include "src/objects/name.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/string.h"

namespace v8 {
namespace internal {

class AbstractCode;
class Name;
class SharedFunctionInfo;
class String;

namespace wasm {
class WasmCode;
using WasmName = base::Vector<const char>;
}  // namespace wasm

#define LOG_EVENT_LIST(V)                         \
  V(kCodeCreation, "code-creation")               \
  V(kCodeDisableOpt, "code-disable-optimization") \
  V(kCodeMove, "code-move")                       \
  V(kCodeDeopt, "code-deopt")                     \
  V(kCodeDelete, "code-delete")                   \
  V(kCodeMovingGC, "code-moving-gc")              \
  V(kSharedFuncMove, "sfi-move")                  \
  V(kSnapshotCodeName, "snapshot-code-name")      \
  V(kTick, "tick")

#define CODE_TYPE_LIST(V)              \
  V(kBuiltin, Builtin)                 \
  V(kCallback, Callback)               \
  V(kEval, Eval)                       \
  V(kFunction, JS)                     \
  V(kHandler, Handler)                 \
  V(kBytecodeHandler, BytecodeHandler) \
  V(kRegExp, RegExp)                   \
  V(kScript, Script)                   \
  V(kStub, Stub)                       \
  V(kNativeFunction, JS)               \
  V(kNativeScript, Script)
// Note that 'Native' cases for functions and scripts are mapped onto
// original tags when writing to the log.

#define PROFILE(the_isolate, Call) (the_isolate)->logger()->Call;

class LogEventListener {
 public:
#define DECLARE_ENUM(enum_item, _) enum_item,
  enum class Event : uint8_t { LOG_EVENT_LIST(DECLARE_ENUM) kLength };
  enum class CodeTag : uint8_t { CODE_TYPE_LIST(DECLARE_ENUM) kLength };
#undef DECLARE_ENUM

  virtual ~LogEventListener() = default;

  virtual void CodeCreateEvent(CodeTag tag, Handle<AbstractCode> code,
                               const char* name) = 0;
  virtual void CodeCreateEvent(CodeTag tag, Handle<AbstractCode> code,
                               Handle<Name> name) = 0;
  virtual void CodeCreateEvent(CodeTag tag, Handle<AbstractCode> code,
                               Handle<SharedFunctionInfo> shared,
                               Handle<Name> script_name) = 0;
  virtual void CodeCreateEvent(CodeTag tag, Handle<AbstractCode> code,
                               Handle<SharedFunctionInfo> shared,
                               Handle<Name> script_name, int line,
                               int column) = 0;
#if V8_ENABLE_WEBASSEMBLY
  virtual void CodeCreateEvent(CodeTag tag, const wasm::WasmCode* code,
                               wasm::WasmName name, const char* source_url,
                               int code_offset, int script_id) = 0;
#endif  // V8_ENABLE_WEBASSEMBLY

  virtual void CallbackEvent(Handle<Name> name, Address entry_point) = 0;
  virtual void GetterCallbackEvent(Handle<Name> name, Address entry_point) = 0;
  virtual void SetterCallbackEvent(Handle<Name> name, Address entry_point) = 0;
  virtual void RegExpCodeCreateEvent(Handle<AbstractCode> code,
                                     Handle<String> source) = 0;
  // Not handlified as this happens during GC. No allocation allowed.
  virtual void CodeMoveEvent(Tagged<InstructionStream> from,
                             Tagged<InstructionStream> to) = 0;
  virtual void BytecodeMoveEvent(Tagged<BytecodeArray> from,
                                 Tagged<BytecodeArray> to) = 0;
  virtual void SharedFunctionInfoMoveEvent(Address from, Address to) = 0;
  virtual void NativeContextMoveEvent(Address from, Address to) = 0;
  virtual void CodeMovingGCEvent() = 0;
  virtual void CodeDisableOptEvent(Handle<AbstractCode> code,
                                   Handle<SharedFunctionInfo> shared) = 0;
  virtual void CodeDeoptEvent(Handle<Code> code, DeoptimizeKind kind,
                              Address pc, int fp_to_sp_delta) = 0;
  // These events can happen when 1. an assumption made by optimized code fails
  // or 2. a weakly embedded object dies.
  virtual void CodeDependencyChangeEvent(Handle<Code> code,
                                         Handle<SharedFunctionInfo> shared,
                                         const char* reason) = 0;
  // Called during GC shortly after any weak references to code objects are
  // cleared.
  virtual void WeakCodeClearEvent() = 0;

  virtual bool is_listening_to_code_events() { return false; }
  virtual bool allows_code_compaction() { return true; }
};

// Dispatches events to a set of registered listeners.
class Logger {
 public:
  using Event = LogEventListener::Event;
  using CodeTag = LogEventListener::CodeTag;

  Logger() = default;
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  bool AddListener(LogEventListener* listener) {
    base::RecursiveMutexGuard guard(&mutex_);
    auto position = std::find(listeners_.begin(), listeners_.end(), listener);
    if (position != listeners_.end()) return false;
    // Add the listener to the end and update the element
    listeners_.push_back(listener);
    return true;
  }

  bool RemoveListener(LogEventListener* listener) {
    base::RecursiveMutexGuard guard(&mutex_);
    auto position = std::find(listeners_.begin(), listeners_.end(), listener);
    if (position == listeners_.end()) return false;
    listeners_.erase(position);
    return true;
  }

  bool is_listening_to_code_events() {
    base::RecursiveMutexGuard guard(&mutex_);
    for (auto listener : listeners_) {
      if (listener->is_listening_to_code_events()) return true;
    }
    return false;
  }

  bool allows_code_compaction() {
    base::RecursiveMutexGuard guard(&mutex_);
    for (auto listener : listeners_) {
      if (!listener->allows_code_compaction()) return false;
    }
    return true;
  }

  void CodeCreateEvent(CodeTag tag, Handle<AbstractCode> code,
                       const char* comment) {
    base::RecursiveMutexGuard guard(&mutex_);
    for (auto listener : listeners_) {
      listener->CodeCreateEvent(tag, code, comment);
    }
  }

  void CodeCreateEvent(CodeTag tag, Handle<AbstractCode> code,
                       Handle<Name> name) {
    base::RecursiveMutexGuard guard(&mutex_);
    for (auto listener : listeners_) {
      listener->CodeCreateEvent(tag, code, name);
    }
  }

  void CodeCreateEvent(CodeTag tag, Handle<AbstractCode> code,
                       Handle<SharedFunctionInfo> shared, Handle<Name> name) {
    base::RecursiveMutexGuard guard(&mutex_);
    for (auto listener : listeners_) {
      listener->CodeCreateEvent(tag, code, shared, name);
    }
  }

  void CodeCreateEvent(CodeTag tag, Handle<AbstractCode> code,
                       Handle<SharedFunctionInfo> shared, Handle<Name> source,
                       int line, int column) {
    base::RecursiveMutexGuard guard(&mutex_);
    for (auto listener : listeners_) {
      listener->CodeCreateEvent(tag, code, shared, source, line, column);
    }
  }

#if V8_ENABLE_WEBASSEMBLY
  void CodeCreateEvent(CodeTag tag, const wasm::WasmCode* code,
                       wasm::WasmName name, const char* source_url,
                       int code_offset, int script_id) {
    base::RecursiveMutexGuard guard(&mutex_);
    for (auto listener : listeners_) {
      listener->CodeCreateEvent(tag, code, name, source_url, code_offset,
                                script_id);
    }
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  void CallbackEvent(Handle<Name> name, Address entry_point) {
    base::RecursiveMutexGuard guard(&mutex_);
    for (auto listener : listeners_) {
      listener->CallbackEvent(name, entry_point);
    }
  }

  void GetterCallbackEvent(Handle<Name> name, Address entry_point) {
    base::RecursiveMutexGuard guard(&mutex_);
    for (auto listener : listeners_) {
      listener->GetterCallbackEvent(name, entry_point);
    }
  }

  void SetterCallbackEvent(Handle<Name> name, Address entry_point) {
    base::RecursiveMutexGuard guard(&mutex_);
    for (auto listener : listeners_) {
      listener->SetterCallbackEvent(name, entry_point);
    }
  }

  void RegExpCodeCreateEvent(Handle<AbstractCode> code, Handle<String> source) {
    base::RecursiveMutexGuard guard(&mutex_);
    for (auto listener : listeners_) {
      listener->RegExpCodeCreateEvent(code, source);
    }
  }

  void CodeMoveEvent(Tagged<InstructionStream> from,
                     Tagged<InstructionStream> to) {
    base::RecursiveMutexGuard guard(&mutex_);
    for (auto listener : listeners_) {
      listener->CodeMoveEvent(from, to);
    }
  }

  void BytecodeMoveEvent(Tagged<BytecodeArray> from, Tagged<BytecodeArray> to) {
    base::RecursiveMutexGuard guard(&mutex_);
    for (auto listener : listeners_) {
      listener->BytecodeMoveEvent(from, to);
    }
  }

  void SharedFunctionInfoMoveEvent(Address from, Address to) {
    base::RecursiveMutexGuard guard(&mutex_);
    for (auto listener : listeners_) {
      listener->SharedFunctionInfoMoveEvent(from, to);
    }
  }

  void NativeContextMoveEvent(Address from, Address to) {
    base::RecursiveMutexGuard guard(&mutex_);
    for (auto listener : listeners_) {
      listener->NativeContextMoveEvent(from, to);
    }
  }

  void CodeMovingGCEvent() {
    base::RecursiveMutexGuard guard(&mutex_);
    for (auto listener : listeners_) {
      listener->CodeMovingGCEvent();
    }
  }

  void CodeDisableOptEvent(Handle<AbstractCode> code,
                           Handle<SharedFunctionInfo> shared) {
    base::RecursiveMutexGuard guard(&mutex_);
    for (auto listener : listeners_) {
      listener->CodeDisableOptEvent(code, shared);
    }
  }

  void CodeDeoptEvent(Handle<Code> code, DeoptimizeKind kind, Address pc,
                      int fp_to_sp_delta) {
    base::RecursiveMutexGuard guard(&mutex_);
    for (auto listener : listeners_) {
      listener->CodeDeoptEvent(code, kind, pc, fp_to_sp_delta);
    }
  }

  void CodeDependencyChangeEvent(Handle<Code> code,
                                 Handle<SharedFunctionInfo> sfi,
                                 const char* reason) {
    base::RecursiveMutexGuard guard(&mutex_);
    for (auto listener : listeners_) {
      listener->CodeDependencyChangeEvent(code, sfi, reason);
    }
  }

  void WeakCodeClearEvent() {
    base::RecursiveMutexGuard guard(&mutex_);
    for (auto listener : listeners_) {
      listener->WeakCodeClearEvent();
    }
  }

 private:
  std::vector<LogEventListener*> listeners_;
  base::RecursiveMutex mutex_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_LOGGING_CODE_EVENTS_H_
