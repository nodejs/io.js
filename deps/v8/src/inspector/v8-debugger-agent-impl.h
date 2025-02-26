// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_V8_DEBUGGER_AGENT_IMPL_H_
#define V8_INSPECTOR_V8_DEBUGGER_AGENT_IMPL_H_

#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>

#include "src/base/enum-set.h"
#include "src/base/macros.h"
#include "src/debug/debug-interface.h"
#include "src/inspector/protocol/Debugger.h"
#include "src/inspector/protocol/Forward.h"

namespace v8_inspector {

struct ScriptBreakpoint;
class DisassemblyCollectorImpl;
class V8Debugger;
class V8DebuggerScript;
class V8InspectorImpl;
class V8InspectorSessionImpl;
class V8Regex;

using protocol::Response;

class V8DebuggerAgentImpl : public protocol::Debugger::Backend {
 public:
  enum BreakpointSource {
    UserBreakpointSource,
    DebugCommandBreakpointSource,
    MonitorCommandBreakpointSource
  };

  V8DebuggerAgentImpl(V8InspectorSessionImpl*, protocol::FrontendChannel*,
                      protocol::DictionaryValue* state);
  ~V8DebuggerAgentImpl() override;
  V8DebuggerAgentImpl(const V8DebuggerAgentImpl&) = delete;
  V8DebuggerAgentImpl& operator=(const V8DebuggerAgentImpl&) = delete;
  void restore();
  void stop();

  // Part of the protocol.
  Response enable(std::optional<double> maxScriptsCacheSize,
                  String16* outDebuggerId) override;
  Response disable() override;
  Response setBreakpointsActive(bool active) override;
  Response setSkipAllPauses(bool skip) override;
  Response setBreakpointByUrl(
      int lineNumber, std::optional<String16> optionalURL,
      std::optional<String16> optionalURLRegex,
      std::optional<String16> optionalScriptHash,
      std::optional<int> optionalColumnNumber,
      std::optional<String16> optionalCondition, String16*,
      std::unique_ptr<protocol::Array<protocol::Debugger::Location>>* locations)
      override;
  Response setBreakpoint(
      std::unique_ptr<protocol::Debugger::Location>,
      std::optional<String16> optionalCondition, String16*,
      std::unique_ptr<protocol::Debugger::Location>* actualLocation) override;
  Response setBreakpointOnFunctionCall(
      const String16& functionObjectId,
      std::optional<String16> optionalCondition,
      String16* outBreakpointId) override;
  Response setInstrumentationBreakpoint(const String16& instrumentation,
                                        String16* outBreakpointId) override;
  Response removeBreakpoint(const String16& breakpointId) override;
  Response continueToLocation(
      std::unique_ptr<protocol::Debugger::Location>,
      std::optional<String16> targetCallFrames) override;
  Response getStackTrace(
      std::unique_ptr<protocol::Runtime::StackTraceId> inStackTraceId,
      std::unique_ptr<protocol::Runtime::StackTrace>* outStackTrace) override;
  Response searchInContent(
      const String16& scriptId, const String16& query,
      std::optional<bool> optionalCaseSensitive,
      std::optional<bool> optionalIsRegex,
      std::unique_ptr<protocol::Array<protocol::Debugger::SearchMatch>>*)
      override;
  Response getPossibleBreakpoints(
      std::unique_ptr<protocol::Debugger::Location> start,
      std::unique_ptr<protocol::Debugger::Location> end,
      std::optional<bool> restrictToFunction,
      std::unique_ptr<protocol::Array<protocol::Debugger::BreakLocation>>*
          locations) override;
  Response setScriptSource(
      const String16& inScriptId, const String16& inScriptSource,
      std::optional<bool> dryRun, std::optional<bool> allowTopFrameEditing,
      std::unique_ptr<protocol::Array<protocol::Debugger::CallFrame>>*
          optOutCallFrames,
      std::optional<bool>* optOutStackChanged,
      std::unique_ptr<protocol::Runtime::StackTrace>* optOutAsyncStackTrace,
      std::unique_ptr<protocol::Runtime::StackTraceId>* optOutAsyncStackTraceId,
      String16* outStatus,
      std::unique_ptr<protocol::Runtime::ExceptionDetails>* optOutCompileError)
      override;
  Response restartFrame(
      const String16& callFrameId, std::optional<String16> mode,
      std::unique_ptr<protocol::Array<protocol::Debugger::CallFrame>>*
          newCallFrames,
      std::unique_ptr<protocol::Runtime::StackTrace>* asyncStackTrace,
      std::unique_ptr<protocol::Runtime::StackTraceId>* asyncStackTraceId)
      override;
  Response getScriptSource(const String16& scriptId, String16* scriptSource,
                           std::optional<protocol::Binary>* bytecode) override;
  Response disassembleWasmModule(
      const String16& in_scriptId, std::optional<String16>* out_streamId,
      int* out_totalNumberOfLines,
      std::unique_ptr<protocol::Array<int>>* out_functionBodyOffsets,
      std::unique_ptr<protocol::Debugger::WasmDisassemblyChunk>* out_chunk)
      override;
  Response nextWasmDisassemblyChunk(
      const String16& in_streamId,
      std::unique_ptr<protocol::Debugger::WasmDisassemblyChunk>* out_chunk)
      override;
  Response getWasmBytecode(const String16& scriptId,
                           protocol::Binary* bytecode) override;
  Response pause() override;
  Response resume(std::optional<bool> terminateOnResume) override;
  Response stepOver(
      std::unique_ptr<protocol::Array<protocol::Debugger::LocationRange>>
          inSkipList) override;
  Response stepInto(
      std::optional<bool> inBreakOnAsyncCall,
      std::unique_ptr<protocol::Array<protocol::Debugger::LocationRange>>
          inSkipList) override;
  Response stepOut() override;
  Response pauseOnAsyncCall(std::unique_ptr<protocol::Runtime::StackTraceId>
                                inParentStackTraceId) override;
  Response setPauseOnExceptions(const String16& pauseState) override;
  Response evaluateOnCallFrame(
      const String16& callFrameId, const String16& expression,
      std::optional<String16> objectGroup,
      std::optional<bool> includeCommandLineAPI, std::optional<bool> silent,
      std::optional<bool> returnByValue, std::optional<bool> generatePreview,
      std::optional<bool> throwOnSideEffect, std::optional<double> timeout,
      std::unique_ptr<protocol::Runtime::RemoteObject>* result,
      std::unique_ptr<protocol::Runtime::ExceptionDetails>*) override;
  Response setVariableValue(
      int scopeNumber, const String16& variableName,
      std::unique_ptr<protocol::Runtime::CallArgument> newValue,
      const String16& callFrame) override;
  Response setReturnValue(
      std::unique_ptr<protocol::Runtime::CallArgument> newValue) override;
  Response setAsyncCallStackDepth(int depth) override;
  Response setBlackboxPatterns(
      std::unique_ptr<protocol::Array<String16>> patterns,
      std::optional<bool> skipAnonymous) override;
  Response setBlackboxExecutionContexts(
      std::unique_ptr<protocol::Array<String16>> uniqueIds) override;
  Response setBlackboxedRanges(
      const String16& scriptId,
      std::unique_ptr<protocol::Array<protocol::Debugger::ScriptPosition>>
          positions) override;

  bool enabled() const { return m_enableState == kEnabled; }

  void setBreakpointFor(v8::Local<v8::Function> function,
                        v8::Local<v8::String> condition,
                        BreakpointSource source);
  void removeBreakpointFor(v8::Local<v8::Function> function,
                           BreakpointSource source);
  void schedulePauseOnNextStatement(
      const String16& breakReason,
      std::unique_ptr<protocol::DictionaryValue> data);
  void cancelPauseOnNextStatement();
  void breakProgram(const String16& breakReason,
                    std::unique_ptr<protocol::DictionaryValue> data);

  void reset();

  bool instrumentationFinished() { return m_instrumentationFinished; }
  // Interface for V8InspectorImpl
  void didPauseOnInstrumentation(v8::debug::BreakpointId instrumentationId);

  void didPause(int contextId, v8::Local<v8::Value> exception,
                const std::vector<v8::debug::BreakpointId>& hitBreakpoints,
                v8::debug::ExceptionType exceptionType, bool isUncaught,
                v8::debug::BreakReasons breakReasons);
  void didContinue();
  void didParseSource(std::unique_ptr<V8DebuggerScript>, bool success);

  bool isFunctionBlackboxed(const String16& scriptId,
                            const v8::debug::Location& start,
                            const v8::debug::Location& end);
  bool shouldBeSkipped(const String16& scriptId, int line, int column);

  bool acceptsPause(bool isOOMBreak) const;

  void ScriptCollected(const V8DebuggerScript* script);

  v8::Isolate* isolate() { return m_isolate; }

  void clearBreakDetails();

 private:
  void enableImpl();

  Response currentCallFrames(
      std::unique_ptr<protocol::Array<protocol::Debugger::CallFrame>>*);
  std::unique_ptr<protocol::Runtime::StackTrace> currentAsyncStackTrace();
  std::unique_ptr<protocol::Runtime::StackTraceId> currentExternalStackTrace();

  void setPauseOnExceptionsImpl(int);

  std::unique_ptr<protocol::Debugger::Location> setBreakpointImpl(
      const String16& breakpointId, const String16& scriptId,
      const String16& condition, int lineNumber, int columnNumber);
  void setBreakpointImpl(const String16& breakpointId,
                         v8::Local<v8::Function> function,
                         v8::Local<v8::String> condition);
  void removeBreakpointImpl(const String16& breakpointId,
                            const std::vector<V8DebuggerScript*>& scripts);

  void internalSetAsyncCallStackDepth(int);
  void increaseCachedSkipStackGeneration();

  Response setBlackboxPattern(const String16& pattern);
  void resetBlackboxedStateCache();

  bool isPaused() const;

  void setScriptInstrumentationBreakpointIfNeeded(V8DebuggerScript* script);

  Response processSkipList(
      protocol::Array<protocol::Debugger::LocationRange>& skipList);

  using ScriptsMap =
      std::unordered_map<String16, std::unique_ptr<V8DebuggerScript>>;
  using BreakpointIdToDebuggerBreakpointIdsMap =
      std::unordered_map<String16, std::vector<v8::debug::BreakpointId>>;
  using DebuggerBreakpointIdToBreakpointIdMap =
      std::unordered_map<v8::debug::BreakpointId, String16>;

  enum EnableState {
    kDisabled,
    kEnabled,
    kStopping,  // This is the same as 'disabled', but it cannot become enabled
                // again.
  };

  V8InspectorImpl* m_inspector;
  V8Debugger* m_debugger;
  V8InspectorSessionImpl* m_session;
  EnableState m_enableState;
  protocol::DictionaryValue* m_state;
  protocol::Debugger::Frontend m_frontend;
  v8::Isolate* m_isolate;
  ScriptsMap m_scripts;
  BreakpointIdToDebuggerBreakpointIdsMap m_breakpointIdToDebuggerBreakpointIds;
  DebuggerBreakpointIdToBreakpointIdMap m_debuggerBreakpointIdToBreakpointId;
  std::map<String16, std::unique_ptr<DisassemblyCollectorImpl>>
      m_wasmDisassemblies;
  size_t m_nextWasmDisassemblyStreamId = 0;

  size_t m_maxScriptCacheSize = 0;
  size_t m_cachedScriptSize = 0;
  struct CachedScript {
    String16 scriptId;
    String16 source;
    std::vector<uint8_t> bytecode;

    size_t size() const {
      return source.length() * sizeof(UChar) + bytecode.size();
    }
  };
  std::deque<CachedScript> m_cachedScripts;

  using BreakReason =
      std::pair<String16, std::unique_ptr<protocol::DictionaryValue>>;
  std::vector<BreakReason> m_breakReason;

  void pushBreakDetails(
      const String16& breakReason,
      std::unique_ptr<protocol::DictionaryValue> breakAuxData);
  void popBreakDetails();

  bool m_skipAllPauses = false;
  bool m_breakpointsActive = false;
  bool m_instrumentationFinished = true;
  bool m_skipAnonymousScripts = false;

  std::unique_ptr<V8Regex> m_blackboxPattern;
  std::unordered_map<String16, std::vector<std::pair<int, int>>>
      m_blackboxedPositions;
  std::unordered_map<String16, std::vector<std::pair<int, int>>> m_skipList;
  std::unordered_set<String16> m_blackboxedExecutionContexts;
};

}  // namespace v8_inspector

#endif  // V8_INSPECTOR_V8_DEBUGGER_AGENT_IMPL_H_
