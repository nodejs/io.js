// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_V8_DEBUGGER_H_
#define V8_INSPECTOR_V8_DEBUGGER_H_

#include <list>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "include/v8-inspector.h"
#include "src/base/macros.h"
#include "src/inspector/inspected-context.h"
#include "src/inspector/protocol/Debugger.h"
#include "src/inspector/protocol/Forward.h"
#include "src/inspector/protocol/Runtime.h"
#include "src/inspector/v8-debugger-id.h"
#include "src/inspector/v8-debugger-script.h"

namespace v8_inspector {

class AsyncStackTrace;
class StackFrame;
class V8Debugger;
class V8DebuggerAgentImpl;
class V8InspectorImpl;
class V8RuntimeAgentImpl;
class V8StackTraceImpl;
struct V8StackTraceId;

enum class WrapMode { kJson, kIdOnly, kPreview, kDeep };

struct WrapSerializationOptions {
  int maxDepth = v8::internal::kMaxInt;
  v8::Global<v8::Object> additionalParameters;
};

struct WrapOptions {
  WrapMode mode;
  WrapSerializationOptions serializationOptions = {};
};

using protocol::Response;
using TerminateExecutionCallback =
    protocol::Runtime::Backend::TerminateExecutionCallback;

class V8Debugger : public v8::debug::DebugDelegate,
                   public v8::debug::AsyncEventDelegate {
 public:
  V8Debugger(v8::Isolate*, V8InspectorImpl*);
  ~V8Debugger() override;
  V8Debugger(const V8Debugger&) = delete;
  V8Debugger& operator=(const V8Debugger&) = delete;

  bool enabled() const;
  v8::Isolate* isolate() const { return m_isolate; }

  void setBreakpointsActive(bool);
  void removeBreakpoint(v8::debug::BreakpointId id);

  v8::debug::ExceptionBreakState getPauseOnExceptionsState();
  void setPauseOnExceptionsState(v8::debug::ExceptionBreakState);
  bool canBreakProgram();
  bool isInInstrumentationPause() const;
  void breakProgram(int targetContextGroupId);
  void interruptAndBreak(int targetContextGroupId);
  void requestPauseAfterInstrumentation();
  void continueProgram(int targetContextGroupId,
                       bool terminateOnResume = false);
  void breakProgramOnAssert(int targetContextGroupId);

  void setPauseOnNextCall(bool, int targetContextGroupId);
  void stepIntoStatement(int targetContextGroupId, bool breakOnAsyncCall);
  void stepOverStatement(int targetContextGroupId);
  void stepOutOfFunction(int targetContextGroupId);

  void terminateExecution(v8::Local<v8::Context> context,
                          std::unique_ptr<TerminateExecutionCallback> callback);

  Response continueToLocation(int targetContextGroupId,
                              V8DebuggerScript* script,
                              std::unique_ptr<protocol::Debugger::Location>,
                              const String16& targetCallFramess);
  bool restartFrame(int targetContextGroupId, int callFrameOrdinal);

  // Each script inherits debug data from v8::Context where it has been
  // compiled.
  // Only scripts whose debug data matches |contextGroupId| will be reported.
  // Passing 0 will result in reporting all scripts.
  std::vector<std::unique_ptr<V8DebuggerScript>> getCompiledScripts(
      int contextGroupId, V8DebuggerAgentImpl* agent);
  void enable();
  void disable();

  bool isPaused() const { return m_pausedContextGroupId; }
  bool isPausedInContextGroup(int contextGroupId) const;

  int maxAsyncCallChainDepth() { return m_maxAsyncCallStackDepth; }
  void setAsyncCallStackDepth(V8DebuggerAgentImpl*, int);

  int maxCallStackSizeToCapture() const { return m_maxCallStackSizeToCapture; }
  void setMaxCallStackSizeToCapture(V8RuntimeAgentImpl*, int);

  std::shared_ptr<AsyncStackTrace> currentAsyncParent();
  V8StackTraceId currentExternalParent();

  std::shared_ptr<StackFrame> symbolize(v8::Local<v8::StackFrame> v8Frame);

  std::unique_ptr<V8StackTraceImpl> createStackTrace(v8::Local<v8::StackTrace>);
  std::unique_ptr<V8StackTraceImpl> captureStackTrace(bool fullStack);

  v8::MaybeLocal<v8::Array> internalProperties(v8::Local<v8::Context>,
                                               v8::Local<v8::Value>);

  v8::Local<v8::Array> queryObjects(v8::Local<v8::Context> context,
                                    v8::Local<v8::Object> prototype);

  void asyncTaskScheduled(const StringView& taskName, void* task,
                          bool recurring);
  void asyncTaskCanceled(void* task);
  void asyncTaskStarted(void* task);
  void asyncTaskFinished(void* task);
  void allAsyncTasksCanceled();

  V8StackTraceId storeCurrentStackTrace(const StringView& description);
  void externalAsyncTaskStarted(const V8StackTraceId& parent);
  void externalAsyncTaskFinished(const V8StackTraceId& parent);

  uintptr_t storeStackTrace(std::shared_ptr<AsyncStackTrace> stack);

  void muteScriptParsedEvents();
  void unmuteScriptParsedEvents();

  V8InspectorImpl* inspector() { return m_inspector; }

  void setMaxAsyncTaskStacksForTest(int limit);
  void dumpAsyncTaskStacksStateForTest();

  internal::V8DebuggerId debuggerIdFor(int contextGroupId);
  std::shared_ptr<AsyncStackTrace> stackTraceFor(int contextGroupId,
                                                 const V8StackTraceId& id);

  void reportTermination();

 private:
  bool addInternalObject(v8::Local<v8::Context> context,
                         v8::Local<v8::Object> object,
                         V8InternalValueType type);

  void clearContinueToLocation();
  bool shouldContinueToCurrentLocation();

  static size_t nearHeapLimitCallback(void* data, size_t current_heap_limit,
                                      size_t initial_heap_limit);
  static void terminateExecutionCompletedCallback(v8::Isolate* isolate);
  static void terminateExecutionCompletedCallbackIgnoringData(
      v8::Isolate* isolate, void*);
  void installTerminateExecutionCallbacks(v8::Local<v8::Context> context);

  void handleProgramBreak(
      v8::Local<v8::Context> pausedContext, v8::Local<v8::Value> exception,
      const std::vector<v8::debug::BreakpointId>& hitBreakpoints,
      v8::debug::BreakReasons break_reasons,
      v8::debug::ExceptionType exception_type = v8::debug::kException,
      bool isUncaught = false);

  enum ScopeTargetKind {
    FUNCTION,
    GENERATOR,
  };
  v8::MaybeLocal<v8::Value> getTargetScopes(v8::Local<v8::Context>,
                                            v8::Local<v8::Value>,
                                            ScopeTargetKind);

  v8::MaybeLocal<v8::Value> functionScopes(v8::Local<v8::Context>,
                                           v8::Local<v8::Function>);
  v8::MaybeLocal<v8::Value> generatorScopes(v8::Local<v8::Context>,
                                            v8::Local<v8::Value>);
  v8::MaybeLocal<v8::Array> collectionsEntries(v8::Local<v8::Context> context,
                                               v8::Local<v8::Value> value);
  v8::MaybeLocal<v8::Array> privateMethods(v8::Local<v8::Context> context,
                                           v8::Local<v8::Value> value);

  void asyncTaskScheduledForStack(const StringView& taskName, void* task,
                                  bool recurring, bool skipTopFrame = false);
  void asyncTaskCanceledForStack(void* task);
  void asyncTaskStartedForStack(void* task);
  void asyncTaskFinishedForStack(void* task);

  void asyncTaskCandidateForStepping(void* task);
  void asyncTaskStartedForStepping(void* task);
  void asyncTaskFinishedForStepping(void* task);
  void asyncTaskCanceledForStepping(void* task);

  // v8::debug::DebugEventListener implementation.
  void AsyncEventOccurred(v8::debug::DebugAsyncActionType type, int id,
                          bool isBlackboxed) override;
  void ScriptCompiled(v8::Local<v8::debug::Script> script, bool is_live_edited,
                      bool has_compile_error) override;
  void BreakProgramRequested(
      v8::Local<v8::Context> paused_context,
      const std::vector<v8::debug::BreakpointId>& break_points_hit,
      v8::debug::BreakReasons break_reasons) override;
  ActionAfterInstrumentation BreakOnInstrumentation(
      v8::Local<v8::Context> paused_context, v8::debug::BreakpointId) override;
  void ExceptionThrown(v8::Local<v8::Context> paused_context,
                       v8::Local<v8::Value> exception,
                       v8::Local<v8::Value> promise, bool is_uncaught,
                       v8::debug::ExceptionType exception_type) override;
  bool IsFunctionBlackboxed(v8::Local<v8::debug::Script> script,
                            const v8::debug::Location& start,
                            const v8::debug::Location& end) override;

  bool ShouldBeSkipped(v8::Local<v8::debug::Script> script, int line,
                       int column) override;
  void BreakpointConditionEvaluated(v8::Local<v8::Context> context,
                                    v8::debug::BreakpointId breakpoint_id,
                                    bool exception_thrown,
                                    v8::Local<v8::Value> exception) override;

  int currentContextGroupId();

  bool hasScheduledBreakOnNextFunctionCall() const;

  void quitMessageLoopIfAgentsFinishedInstrumentation();

  v8::Isolate* m_isolate;
  V8InspectorImpl* m_inspector;
  int m_enableCount;

  int m_breakpointsActiveCount = 0;
  int m_ignoreScriptParsedEventsCounter;
  size_t m_originalHeapLimit = 0;
  bool m_scheduledOOMBreak = false;
  int m_targetContextGroupId = 0;
  int m_pausedContextGroupId = 0;
  bool m_instrumentationPause = false;
  bool m_requestedPauseAfterInstrumentation = false;
  int m_continueToLocationBreakpointId;
  String16 m_continueToLocationTargetCallFrames;
  std::unique_ptr<V8StackTraceImpl> m_continueToLocationStack;

  // We cache symbolized stack frames by (scriptId,lineNumber,columnNumber)
  // to reduce memory pressure for huge web apps with lots of deep async
  // stacks.
  struct CachedStackFrameKey {
    int scriptId;
    int lineNumber;
    int columnNumber;

    struct Equal {
      bool operator()(CachedStackFrameKey const& a,
                      CachedStackFrameKey const& b) const {
        return a.scriptId == b.scriptId && a.lineNumber == b.lineNumber &&
               a.columnNumber == b.columnNumber;
      }
    };

    struct Hash {
      size_t operator()(CachedStackFrameKey const& key) const {
        size_t code = 0;
        code = code * 31 + key.scriptId;
        code = code * 31 + key.lineNumber;
        code = code * 31 + key.columnNumber;
        return code;
      }
    };
  };
  std::unordered_map<CachedStackFrameKey, std::weak_ptr<StackFrame>,
                     CachedStackFrameKey::Hash, CachedStackFrameKey::Equal>
      m_cachedStackFrames;

  using AsyncTaskToStackTrace =
      std::unordered_map<void*, std::weak_ptr<AsyncStackTrace>>;
  AsyncTaskToStackTrace m_asyncTaskStacks;
  std::unordered_set<void*> m_recurringTasks;

  size_t m_maxAsyncCallStacks;
  int m_maxAsyncCallStackDepth;
  int m_maxCallStackSizeToCapture;

  std::vector<void*> m_currentTasks;
  std::vector<std::shared_ptr<AsyncStackTrace>> m_currentAsyncParent;
  std::vector<V8StackTraceId> m_currentExternalParent;

  void collectOldAsyncStacksIfNeeded();
  // V8Debugger owns all the async stacks, while most of the other references
  // are weak, which allows to collect some stacks when there are too many.
  std::list<std::shared_ptr<AsyncStackTrace>> m_allAsyncStacks;

  std::unordered_map<V8DebuggerAgentImpl*, int> m_maxAsyncCallStackDepthMap;
  std::unordered_map<V8RuntimeAgentImpl*, int> m_maxCallStackSizeToCaptureMap;
  void* m_taskWithScheduledBreak = nullptr;

  // If any of the following three is true, we schedule pause on next JS
  // execution using SetBreakOnNextFunctionCall.
  bool m_externalAsyncTaskPauseRequested = false;       // External async task.
  bool m_taskWithScheduledBreakPauseRequested = false;  // Local async task.
  bool m_pauseOnNextCallRequested = false;  // setPauseOnNextCall API call.

  v8::debug::ExceptionBreakState m_pauseOnExceptionsState;
  // Whether we should pause on async call execution (if any) while stepping in.
  // See Debugger.stepInto for details.
  bool m_pauseOnAsyncCall = false;

  using StackTraceIdToStackTrace =
      std::unordered_map<uintptr_t, std::weak_ptr<AsyncStackTrace>>;
  StackTraceIdToStackTrace m_storedStackTraces;
  uintptr_t m_lastStackTraceId = 0;

  std::unordered_map<int, internal::V8DebuggerId> m_contextGroupIdToDebuggerId;

  std::unique_ptr<TerminateExecutionCallback> m_terminateExecutionCallback;
  v8::Global<v8::Context> m_terminateExecutionCallbackContext;
  bool m_terminateExecutionReported = true;
};

}  // namespace v8_inspector

#endif  // V8_INSPECTOR_V8_DEBUGGER_H_
