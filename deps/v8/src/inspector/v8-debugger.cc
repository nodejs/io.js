// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/inspector/v8-debugger.h"

#include "include/v8-container.h"
#include "include/v8-context.h"
#include "include/v8-function.h"
#include "include/v8-microtask-queue.h"
#include "include/v8-profiler.h"
#include "include/v8-util.h"
#include "src/inspector/inspected-context.h"
#include "src/inspector/protocol/Protocol.h"
#include "src/inspector/string-util.h"
#include "src/inspector/v8-debugger-agent-impl.h"
#include "src/inspector/v8-heap-profiler-agent-impl.h"
#include "src/inspector/v8-inspector-impl.h"
#include "src/inspector/v8-inspector-session-impl.h"
#include "src/inspector/v8-runtime-agent-impl.h"
#include "src/inspector/v8-stack-trace-impl.h"
#include "src/inspector/v8-value-utils.h"

namespace v8_inspector {

namespace {

static const size_t kMaxAsyncTaskStacks = 8 * 1024;
static const int kNoBreakpointId = 0;

template <typename Map>
void cleanupExpiredWeakPointers(Map& map) {
  for (auto it = map.begin(); it != map.end();) {
    if (it->second.expired()) {
      it = map.erase(it);
    } else {
      ++it;
    }
  }
}

// Allow usages of v8::Object::GetPrototype() for now.
// TODO(https://crbug.com/333672197): remove.
START_ALLOW_USE_DEPRECATED()

class MatchPrototypePredicate : public v8::QueryObjectPredicate {
 public:
  MatchPrototypePredicate(V8InspectorImpl* inspector,
                          v8::Local<v8::Context> context,
                          v8::Local<v8::Object> prototype)
      : m_inspector(inspector), m_context(context), m_prototype(prototype) {}

  bool Filter(v8::Local<v8::Object> object) override {
    if (object->IsModuleNamespaceObject()) return false;
    v8::Local<v8::Context> objectContext;
    if (!v8::debug::GetCreationContext(object).ToLocal(&objectContext)) {
      return false;
    }
    if (objectContext != m_context) return false;
    if (!m_inspector->client()->isInspectableHeapObject(object)) return false;
    // Get prototype chain for current object until first visited prototype.
    for (v8::Local<v8::Value> prototype = object->GetPrototype();
         prototype->IsObject();
         prototype = prototype.As<v8::Object>()->GetPrototype()) {
      if (m_prototype == prototype) return true;
    }
    return false;
  }

 private:
  V8InspectorImpl* m_inspector;
  v8::Local<v8::Context> m_context;
  v8::Local<v8::Value> m_prototype;
};

// Allow usages of v8::Object::GetPrototype() for now.
// TODO(https://crbug.com/333672197): remove.
END_ALLOW_USE_DEPRECATED()

}  // namespace

V8Debugger::V8Debugger(v8::Isolate* isolate, V8InspectorImpl* inspector)
    : m_isolate(isolate),
      m_inspector(inspector),
      m_enableCount(0),
      m_ignoreScriptParsedEventsCounter(0),
      m_continueToLocationBreakpointId(kNoBreakpointId),
      m_maxAsyncCallStacks(kMaxAsyncTaskStacks),
      m_maxAsyncCallStackDepth(0),
      m_maxCallStackSizeToCapture(
          V8StackTraceImpl::kDefaultMaxCallStackSizeToCapture),
      m_pauseOnExceptionsState(v8::debug::NoBreakOnException) {}

V8Debugger::~V8Debugger() {
  m_isolate->RemoveCallCompletedCallback(
      &V8Debugger::terminateExecutionCompletedCallback);
  if (!m_terminateExecutionCallbackContext.IsEmpty()) {
    v8::HandleScope handles(m_isolate);
    v8::MicrotaskQueue* microtask_queue =
        m_terminateExecutionCallbackContext.Get(m_isolate)->GetMicrotaskQueue();
    microtask_queue->RemoveMicrotasksCompletedCallback(
        &V8Debugger::terminateExecutionCompletedCallbackIgnoringData,
        microtask_queue);
  }
}

void V8Debugger::enable() {
  if (m_enableCount++) return;
  v8::HandleScope scope(m_isolate);
  v8::debug::SetDebugDelegate(m_isolate, this);
  m_isolate->AddNearHeapLimitCallback(&V8Debugger::nearHeapLimitCallback, this);
  v8::debug::ChangeBreakOnException(m_isolate, v8::debug::NoBreakOnException);
  m_pauseOnExceptionsState = v8::debug::NoBreakOnException;
#if V8_ENABLE_WEBASSEMBLY
  v8::debug::EnterDebuggingForIsolate(m_isolate);
#endif  // V8_ENABLE_WEBASSEMBLY
}

void V8Debugger::disable() {
  if (isPaused()) {
    bool scheduledOOMBreak = m_scheduledOOMBreak;
    bool hasAgentAcceptsPause = false;

    if (m_instrumentationPause) {
      quitMessageLoopIfAgentsFinishedInstrumentation();
    } else {
      m_inspector->forEachSession(
          m_pausedContextGroupId, [&scheduledOOMBreak, &hasAgentAcceptsPause](
                                      V8InspectorSessionImpl* session) {
            if (session->debuggerAgent()->acceptsPause(scheduledOOMBreak)) {
              hasAgentAcceptsPause = true;
            }
          });
      if (!hasAgentAcceptsPause)
        m_inspector->client()->quitMessageLoopOnPause();
    }
  }
  if (--m_enableCount) return;
  clearContinueToLocation();
  m_taskWithScheduledBreak = nullptr;
  m_externalAsyncTaskPauseRequested = false;
  m_taskWithScheduledBreakPauseRequested = false;
  m_pauseOnNextCallRequested = false;
  m_pauseOnAsyncCall = false;
#if V8_ENABLE_WEBASSEMBLY
  v8::debug::LeaveDebuggingForIsolate(m_isolate);
#endif  // V8_ENABLE_WEBASSEMBLY
  v8::debug::SetDebugDelegate(m_isolate, nullptr);
  m_isolate->RemoveNearHeapLimitCallback(&V8Debugger::nearHeapLimitCallback,
                                         m_originalHeapLimit);
  m_originalHeapLimit = 0;
}

bool V8Debugger::isPausedInContextGroup(int contextGroupId) const {
  return isPaused() && m_pausedContextGroupId == contextGroupId;
}

bool V8Debugger::enabled() const { return m_enableCount > 0; }

std::vector<std::unique_ptr<V8DebuggerScript>> V8Debugger::getCompiledScripts(
    int contextGroupId, V8DebuggerAgentImpl* agent) {
  std::vector<std::unique_ptr<V8DebuggerScript>> result;
  v8::HandleScope scope(m_isolate);
  std::vector<v8::Global<v8::debug::Script>> scripts;
  v8::debug::GetLoadedScripts(m_isolate, scripts);
  for (size_t i = 0; i < scripts.size(); ++i) {
    v8::Local<v8::debug::Script> script = scripts[i].Get(m_isolate);
    if (!script->WasCompiled()) continue;
    if (!script->IsEmbedded()) {
      int contextId;
      if (!script->ContextId().To(&contextId)) continue;
      if (m_inspector->contextGroupId(contextId) != contextGroupId) continue;
    }
    result.push_back(V8DebuggerScript::Create(m_isolate, script, false, agent,
                                              m_inspector->client()));
  }
  return result;
}

void V8Debugger::setBreakpointsActive(bool active) {
  if (!enabled()) {
    UNREACHABLE();
  }
  m_breakpointsActiveCount += active ? 1 : -1;
  DCHECK_GE(m_breakpointsActiveCount, 0);
  v8::debug::SetBreakPointsActive(m_isolate, m_breakpointsActiveCount);
}

void V8Debugger::removeBreakpoint(v8::debug::BreakpointId id) {
  v8::debug::RemoveBreakpoint(m_isolate, id);
}

v8::debug::ExceptionBreakState V8Debugger::getPauseOnExceptionsState() {
  DCHECK(enabled());
  return m_pauseOnExceptionsState;
}

void V8Debugger::setPauseOnExceptionsState(
    v8::debug::ExceptionBreakState pauseOnExceptionsState) {
  DCHECK(enabled());
  if (m_pauseOnExceptionsState == pauseOnExceptionsState) return;
  v8::debug::ChangeBreakOnException(m_isolate, pauseOnExceptionsState);
  m_pauseOnExceptionsState = pauseOnExceptionsState;
}

void V8Debugger::setPauseOnNextCall(bool pause, int targetContextGroupId) {
  if (isPaused()) return;
  DCHECK(targetContextGroupId);
  if (!pause && m_targetContextGroupId &&
      m_targetContextGroupId != targetContextGroupId) {
    return;
  }
  if (pause) {
    bool didHaveBreak = hasScheduledBreakOnNextFunctionCall();
    m_pauseOnNextCallRequested = true;
    if (!didHaveBreak) {
      m_targetContextGroupId = targetContextGroupId;
      v8::debug::SetBreakOnNextFunctionCall(m_isolate);
    }
  } else {
    m_pauseOnNextCallRequested = false;
    if (!hasScheduledBreakOnNextFunctionCall()) {
      v8::debug::ClearBreakOnNextFunctionCall(m_isolate);
    }
  }
}

bool V8Debugger::canBreakProgram() {
  return v8::debug::CanBreakProgram(m_isolate);
}

bool V8Debugger::isInInstrumentationPause() const {
  return m_instrumentationPause;
}

void V8Debugger::breakProgram(int targetContextGroupId) {
  DCHECK(canBreakProgram());
  // Don't allow nested breaks.
  if (isPaused()) return;
  DCHECK(targetContextGroupId);
  m_targetContextGroupId = targetContextGroupId;
  v8::debug::BreakRightNow(m_isolate);
}

void V8Debugger::interruptAndBreak(int targetContextGroupId) {
  // Don't allow nested breaks.
  if (isPaused()) return;
  DCHECK(targetContextGroupId);
  m_targetContextGroupId = targetContextGroupId;
  m_isolate->RequestInterrupt(
      [](v8::Isolate* isolate, void*) {
        v8::debug::BreakRightNow(
            isolate,
            v8::debug::BreakReasons({v8::debug::BreakReason::kScheduled}));
      },
      nullptr);
}

void V8Debugger::requestPauseAfterInstrumentation() {
  m_requestedPauseAfterInstrumentation = true;
}

void V8Debugger::quitMessageLoopIfAgentsFinishedInstrumentation() {
  bool allAgentsFinishedInstrumentation = true;
  m_inspector->forEachSession(
      m_pausedContextGroupId,
      [&allAgentsFinishedInstrumentation](V8InspectorSessionImpl* session) {
        if (!session->debuggerAgent()->instrumentationFinished()) {
          allAgentsFinishedInstrumentation = false;
        }
      });
  if (allAgentsFinishedInstrumentation) {
    m_inspector->client()->quitMessageLoopOnPause();
  }
}

void V8Debugger::continueProgram(int targetContextGroupId,
                                 bool terminateOnResume) {
  if (m_pausedContextGroupId != targetContextGroupId) return;
  if (isPaused()) {
    if (m_instrumentationPause) {
      quitMessageLoopIfAgentsFinishedInstrumentation();
    } else if (terminateOnResume) {
      v8::debug::SetTerminateOnResume(m_isolate);

      v8::HandleScope handles(m_isolate);
      v8::Local<v8::Context> context =
          m_inspector->client()->ensureDefaultContextInGroup(
              targetContextGroupId);
      installTerminateExecutionCallbacks(context);

      m_inspector->client()->quitMessageLoopOnPause();
    } else {
      m_inspector->client()->quitMessageLoopOnPause();
    }
  }
}

void V8Debugger::breakProgramOnAssert(int targetContextGroupId) {
  if (!enabled()) return;
  if (m_pauseOnExceptionsState == v8::debug::NoBreakOnException) return;
  // Don't allow nested breaks.
  if (isPaused()) return;
  if (!canBreakProgram()) return;
  DCHECK(targetContextGroupId);
  m_targetContextGroupId = targetContextGroupId;
  v8::debug::BreakRightNow(
      m_isolate, v8::debug::BreakReasons({v8::debug::BreakReason::kAssert}));
}

void V8Debugger::stepIntoStatement(int targetContextGroupId,
                                   bool breakOnAsyncCall) {
  DCHECK(isPaused());
  DCHECK(targetContextGroupId);
  m_targetContextGroupId = targetContextGroupId;
  m_pauseOnAsyncCall = breakOnAsyncCall;
  v8::debug::PrepareStep(m_isolate, v8::debug::StepInto);
  continueProgram(targetContextGroupId);
}

void V8Debugger::stepOverStatement(int targetContextGroupId) {
  DCHECK(isPaused());
  DCHECK(targetContextGroupId);
  m_targetContextGroupId = targetContextGroupId;
  v8::debug::PrepareStep(m_isolate, v8::debug::StepOver);
  continueProgram(targetContextGroupId);
}

void V8Debugger::stepOutOfFunction(int targetContextGroupId) {
  DCHECK(isPaused());
  DCHECK(targetContextGroupId);
  m_targetContextGroupId = targetContextGroupId;
  v8::debug::PrepareStep(m_isolate, v8::debug::StepOut);
  continueProgram(targetContextGroupId);
}

void V8Debugger::terminateExecution(
    v8::Local<v8::Context> context,
    std::unique_ptr<TerminateExecutionCallback> callback) {
  if (!m_terminateExecutionReported) {
    if (callback) {
      callback->sendFailure(Response::ServerError(
          "There is current termination request in progress"));
    }
    return;
  }
  m_terminateExecutionCallback = std::move(callback);
  installTerminateExecutionCallbacks(context);
  m_isolate->TerminateExecution();
}

void V8Debugger::installTerminateExecutionCallbacks(
    v8::Local<v8::Context> context) {
  m_isolate->AddCallCompletedCallback(
      &V8Debugger::terminateExecutionCompletedCallback);

  if (!context.IsEmpty()) {
    m_terminateExecutionCallbackContext.Reset(m_isolate, context);
    m_terminateExecutionCallbackContext.SetWeak();
    v8::MicrotaskQueue* microtask_queue = context->GetMicrotaskQueue();
    microtask_queue->AddMicrotasksCompletedCallback(
        &V8Debugger::terminateExecutionCompletedCallbackIgnoringData,
        microtask_queue);
  }

  DCHECK(m_terminateExecutionReported);
  m_terminateExecutionReported = false;
}

void V8Debugger::reportTermination() {
  if (m_terminateExecutionReported) {
    DCHECK(m_terminateExecutionCallbackContext.IsEmpty());
    return;
  }
  v8::HandleScope handles(m_isolate);
  m_isolate->RemoveCallCompletedCallback(
      &V8Debugger::terminateExecutionCompletedCallback);
  if (!m_terminateExecutionCallbackContext.IsEmpty()) {
    v8::MicrotaskQueue* microtask_queue =
        m_terminateExecutionCallbackContext.Get(m_isolate)->GetMicrotaskQueue();
    if (microtask_queue) {
      microtask_queue->RemoveMicrotasksCompletedCallback(
          &V8Debugger::terminateExecutionCompletedCallbackIgnoringData,
          microtask_queue);
    }
  }
  m_isolate->CancelTerminateExecution();
  if (m_terminateExecutionCallback) {
    m_terminateExecutionCallback->sendSuccess();
    m_terminateExecutionCallback.reset();
  }
  m_terminateExecutionCallbackContext.Reset();
  m_terminateExecutionReported = true;
}

void V8Debugger::terminateExecutionCompletedCallback(v8::Isolate* isolate) {
  V8InspectorImpl* inspector =
      static_cast<V8InspectorImpl*>(v8::debug::GetInspector(isolate));
  V8Debugger* debugger = inspector->debugger();
  debugger->reportTermination();
}

void V8Debugger::terminateExecutionCompletedCallbackIgnoringData(
    v8::Isolate* isolate, void* data) {
  DCHECK(data);
  // Ensure that after every microtask completed callback we remove the
  // callback regardless of how `terminateExecutionCompletedCallback` behaves.
  static_cast<v8::MicrotaskQueue*>(data)->RemoveMicrotasksCompletedCallback(
      &V8Debugger::terminateExecutionCompletedCallbackIgnoringData, data);
  terminateExecutionCompletedCallback(isolate);
}

Response V8Debugger::continueToLocation(
    int targetContextGroupId, V8DebuggerScript* script,
    std::unique_ptr<protocol::Debugger::Location> location,
    const String16& targetCallFrames) {
  DCHECK(isPaused());
  DCHECK(targetContextGroupId);
  m_targetContextGroupId = targetContextGroupId;
  v8::debug::Location v8Location(location->getLineNumber(),
                                 location->getColumnNumber(0));
  if (script->setBreakpoint(String16(), &v8Location,
                            &m_continueToLocationBreakpointId)) {
    m_continueToLocationTargetCallFrames = targetCallFrames;
    if (m_continueToLocationTargetCallFrames !=
        protocol::Debugger::ContinueToLocation::TargetCallFramesEnum::Any) {
      m_continueToLocationStack = V8StackTraceImpl::capture(
          this, V8StackTraceImpl::kDefaultMaxCallStackSizeToCapture);
      DCHECK(m_continueToLocationStack);
    }
    continueProgram(targetContextGroupId);
    // TODO(kozyatinskiy): Return actual line and column number.
    return Response::Success();
  } else {
    return Response::ServerError("Cannot continue to specified location");
  }
}

bool V8Debugger::restartFrame(int targetContextGroupId, int callFrameOrdinal) {
  DCHECK(isPaused());
  DCHECK(targetContextGroupId);
  m_targetContextGroupId = targetContextGroupId;

  if (v8::debug::PrepareRestartFrame(m_isolate, callFrameOrdinal)) {
    continueProgram(targetContextGroupId);
    return true;
  }
  return false;
}

bool V8Debugger::shouldContinueToCurrentLocation() {
  if (m_continueToLocationTargetCallFrames ==
      protocol::Debugger::ContinueToLocation::TargetCallFramesEnum::Any) {
    return true;
  }
  std::unique_ptr<V8StackTraceImpl> currentStack = V8StackTraceImpl::capture(
      this, V8StackTraceImpl::kDefaultMaxCallStackSizeToCapture);
  if (m_continueToLocationTargetCallFrames ==
      protocol::Debugger::ContinueToLocation::TargetCallFramesEnum::Current) {
    return m_continueToLocationStack->isEqualIgnoringTopFrame(
        currentStack.get());
  }
  return true;
}

void V8Debugger::clearContinueToLocation() {
  if (m_continueToLocationBreakpointId == kNoBreakpointId) return;
  v8::debug::RemoveBreakpoint(m_isolate, m_continueToLocationBreakpointId);
  m_continueToLocationBreakpointId = kNoBreakpointId;
  m_continueToLocationTargetCallFrames = String16();
  m_continueToLocationStack.reset();
}

void V8Debugger::handleProgramBreak(
    v8::Local<v8::Context> pausedContext, v8::Local<v8::Value> exception,
    const std::vector<v8::debug::BreakpointId>& breakpointIds,
    v8::debug::BreakReasons breakReasons,
    v8::debug::ExceptionType exceptionType, bool isUncaught) {
  // Don't allow nested breaks.
  if (isPaused()) return;

  int contextGroupId = m_inspector->contextGroupId(pausedContext);
  if (m_targetContextGroupId && contextGroupId != m_targetContextGroupId) {
    v8::debug::PrepareStep(m_isolate, v8::debug::StepOut);
    return;
  }

  DCHECK(hasScheduledBreakOnNextFunctionCall() ==
         (m_taskWithScheduledBreakPauseRequested ||
          m_externalAsyncTaskPauseRequested || m_pauseOnNextCallRequested));
  if (m_taskWithScheduledBreakPauseRequested ||
      m_externalAsyncTaskPauseRequested)
    breakReasons.Add(v8::debug::BreakReason::kAsyncStep);
  if (m_pauseOnNextCallRequested)
    breakReasons.Add(v8::debug::BreakReason::kAgent);

  m_targetContextGroupId = 0;
  m_pauseOnNextCallRequested = false;
  m_pauseOnAsyncCall = false;
  m_taskWithScheduledBreak = nullptr;
  m_externalAsyncTaskPauseRequested = false;
  m_taskWithScheduledBreakPauseRequested = false;

  bool scheduledOOMBreak = m_scheduledOOMBreak;
  DCHECK(scheduledOOMBreak ==
         breakReasons.contains(v8::debug::BreakReason::kOOM));
  bool hasAgents = false;

  m_inspector->forEachSession(
      contextGroupId,
      [&scheduledOOMBreak, &hasAgents](V8InspectorSessionImpl* session) {
        if (session->debuggerAgent()->acceptsPause(scheduledOOMBreak))
          hasAgents = true;
      });
  if (!hasAgents) return;

  if (breakpointIds.size() == 1 &&
      breakpointIds[0] == m_continueToLocationBreakpointId) {
    v8::Context::Scope contextScope(pausedContext);
    if (!shouldContinueToCurrentLocation()) return;
  }
  clearContinueToLocation();

  DCHECK(contextGroupId);
  m_pausedContextGroupId = contextGroupId;

  m_inspector->forEachSession(
      contextGroupId,
      [&pausedContext, &exception, &breakpointIds, &exceptionType, &isUncaught,
       &scheduledOOMBreak, &breakReasons](V8InspectorSessionImpl* session) {
        if (session->debuggerAgent()->acceptsPause(scheduledOOMBreak)) {
          session->debuggerAgent()->didPause(
              InspectedContext::contextId(pausedContext), exception,
              breakpointIds, exceptionType, isUncaught, breakReasons);
        }
      });
  {
    v8::Context::Scope scope(pausedContext);

    m_inspector->forEachSession(
        contextGroupId, [](V8InspectorSessionImpl* session) {
          if (session->heapProfilerAgent()) {
            session->heapProfilerAgent()->takePendingHeapSnapshots();
          }
        });

    m_inspector->client()->runMessageLoopOnPause(contextGroupId);
    m_pausedContextGroupId = 0;
  }
  m_inspector->forEachSession(contextGroupId,
                              [](V8InspectorSessionImpl* session) {
                                if (session->debuggerAgent()->enabled()) {
                                  session->debuggerAgent()->clearBreakDetails();
                                  session->debuggerAgent()->didContinue();
                                }
                              });

  if (m_scheduledOOMBreak) m_isolate->RestoreOriginalHeapLimit();
  m_scheduledOOMBreak = false;
}

namespace {

size_t HeapLimitForDebugging(size_t initial_heap_limit) {
  const size_t kDebugHeapSizeFactor = 4;
  size_t max_limit = std::numeric_limits<size_t>::max() / 4;
  return std::min(max_limit, initial_heap_limit * kDebugHeapSizeFactor);
}

}  // anonymous namespace

size_t V8Debugger::nearHeapLimitCallback(void* data, size_t current_heap_limit,
                                         size_t initial_heap_limit) {
  V8Debugger* thisPtr = static_cast<V8Debugger*>(data);
  thisPtr->m_originalHeapLimit = current_heap_limit;
  thisPtr->m_scheduledOOMBreak = true;
  v8::Local<v8::Context> context =
      thisPtr->m_isolate->GetEnteredOrMicrotaskContext();
  thisPtr->m_targetContextGroupId =
      context.IsEmpty() ? 0 : thisPtr->m_inspector->contextGroupId(context);
  thisPtr->m_isolate->RequestInterrupt(
      [](v8::Isolate* isolate, void*) {
        // There's a redundancy  between setting `m_scheduledOOMBreak` and
        // passing the reason along in `BreakRightNow`. The
        // `m_scheduledOOMBreak` is used elsewhere, so we cannot remove it. And
        // for being explicit, we still pass the break reason along.
        v8::debug::BreakRightNow(
            isolate, v8::debug::BreakReasons({v8::debug::BreakReason::kOOM}));
      },
      nullptr);
  return HeapLimitForDebugging(initial_heap_limit);
}

void V8Debugger::ScriptCompiled(v8::Local<v8::debug::Script> script,
                                bool is_live_edited, bool has_compile_error) {
  if (m_ignoreScriptParsedEventsCounter != 0) return;

  int contextId;
  if (!script->ContextId().To(&contextId)) return;

  v8::Isolate* isolate = m_isolate;
  V8InspectorClient* client = m_inspector->client();

  m_inspector->forEachSession(
      m_inspector->contextGroupId(contextId),
      [isolate, &script, has_compile_error, is_live_edited,
       client](V8InspectorSessionImpl* session) {
        auto agent = session->debuggerAgent();
        if (!agent->enabled()) return;
        agent->didParseSource(
            V8DebuggerScript::Create(isolate, script, is_live_edited, agent,
                                     client),
            !has_compile_error);
      });
}

V8Debugger::ActionAfterInstrumentation V8Debugger::BreakOnInstrumentation(
    v8::Local<v8::Context> pausedContext,
    v8::debug::BreakpointId instrumentationId) {
  // Don't allow nested breaks.
  if (isPaused()) return ActionAfterInstrumentation::kPauseIfBreakpointsHit;

  int contextGroupId = m_inspector->contextGroupId(pausedContext);
  bool hasAgents = false;
  m_inspector->forEachSession(
      contextGroupId, [&hasAgents](V8InspectorSessionImpl* session) {
        if (session->debuggerAgent()->acceptsPause(false /* isOOMBreak */))
          hasAgents = true;
      });
  if (!hasAgents) return ActionAfterInstrumentation::kPauseIfBreakpointsHit;

  m_pausedContextGroupId = contextGroupId;
  m_instrumentationPause = true;
  m_inspector->forEachSession(
      contextGroupId, [instrumentationId](V8InspectorSessionImpl* session) {
        if (session->debuggerAgent()->acceptsPause(false /* isOOMBreak */)) {
          session->debuggerAgent()->didPauseOnInstrumentation(
              instrumentationId);
        }
      });
  {
    v8::Context::Scope scope(pausedContext);
    m_inspector->client()->runMessageLoopOnInstrumentationPause(contextGroupId);
  }
  bool requestedPauseAfterInstrumentation =
      m_requestedPauseAfterInstrumentation;

  m_requestedPauseAfterInstrumentation = false;
  m_pausedContextGroupId = 0;
  m_instrumentationPause = false;

  hasAgents = false;
  m_inspector->forEachSession(
      contextGroupId, [&hasAgents](V8InspectorSessionImpl* session) {
        if (session->debuggerAgent()->enabled())
          session->debuggerAgent()->didContinue();
        if (session->debuggerAgent()->acceptsPause(false /* isOOMBreak */))
          hasAgents = true;
      });
  if (!hasAgents) {
    return ActionAfterInstrumentation::kContinue;
  } else if (requestedPauseAfterInstrumentation) {
    return ActionAfterInstrumentation::kPause;
  } else {
    return ActionAfterInstrumentation::kPauseIfBreakpointsHit;
  }
}

void V8Debugger::BreakProgramRequested(
    v8::Local<v8::Context> pausedContext,
    const std::vector<v8::debug::BreakpointId>& break_points_hit,
    v8::debug::BreakReasons reasons) {
  handleProgramBreak(pausedContext, v8::Local<v8::Value>(), break_points_hit,
                     reasons);
}

void V8Debugger::ExceptionThrown(v8::Local<v8::Context> pausedContext,
                                 v8::Local<v8::Value> exception,
                                 v8::Local<v8::Value> promise, bool isUncaught,
                                 v8::debug::ExceptionType exceptionType) {
  std::vector<v8::debug::BreakpointId> break_points_hit;
  handleProgramBreak(
      pausedContext, exception, break_points_hit,
      v8::debug::BreakReasons({v8::debug::BreakReason::kException}),
      exceptionType, isUncaught);
}

bool V8Debugger::IsFunctionBlackboxed(v8::Local<v8::debug::Script> script,
                                      const v8::debug::Location& start,
                                      const v8::debug::Location& end) {
  int contextId;
  if (!script->ContextId().To(&contextId)) return false;
  bool hasAgents = false;
  bool allBlackboxed = true;
  String16 scriptId = String16::fromInteger(script->Id());
  m_inspector->forEachSession(
      m_inspector->contextGroupId(contextId),
      [&hasAgents, &allBlackboxed, &scriptId, &start,
       &end](V8InspectorSessionImpl* session) {
        V8DebuggerAgentImpl* agent = session->debuggerAgent();
        if (!agent->enabled()) return;
        hasAgents = true;
        allBlackboxed &= agent->isFunctionBlackboxed(scriptId, start, end);
      });
  return hasAgents && allBlackboxed;
}

bool V8Debugger::ShouldBeSkipped(v8::Local<v8::debug::Script> script, int line,
                                 int column) {
  int contextId;
  if (!script->ContextId().To(&contextId)) return false;

  bool hasAgents = false;
  bool allShouldBeSkipped = true;
  String16 scriptId = String16::fromInteger(script->Id());
  m_inspector->forEachSession(
      m_inspector->contextGroupId(contextId),
      [&hasAgents, &allShouldBeSkipped, &scriptId, line,
       column](V8InspectorSessionImpl* session) {
        V8DebuggerAgentImpl* agent = session->debuggerAgent();
        if (!agent->enabled()) return;
        hasAgents = true;
        const bool skip = agent->shouldBeSkipped(scriptId, line, column);
        allShouldBeSkipped &= skip;
      });
  return hasAgents && allShouldBeSkipped;
}

void V8Debugger::BreakpointConditionEvaluated(
    v8::Local<v8::Context> context, v8::debug::BreakpointId breakpoint_id,
    bool exception_thrown, v8::Local<v8::Value> exception) {
  if (!exception_thrown || exception.IsEmpty()) return;

  v8::Local<v8::Message> message =
      v8::debug::CreateMessageFromException(isolate(), exception);
  v8::ScriptOrigin origin = message->GetScriptOrigin();
  String16 url;
  if (origin.ResourceName()->IsString()) {
    url = toProtocolString(isolate(), origin.ResourceName().As<v8::String>());
  }
  // The message text is prepended to the exception text itself so we don't
  // need to get it from the v8::Message.
  StringView messageText;
  StringView detailedMessage;
  m_inspector->exceptionThrown(
      context, messageText, exception, detailedMessage, toStringView(url),
      message->GetLineNumber(context).FromMaybe(0),
      message->GetStartColumn() + 1, createStackTrace(message->GetStackTrace()),
      origin.ScriptId());
}

void V8Debugger::AsyncEventOccurred(v8::debug::DebugAsyncActionType type,
                                    int id, bool isBlackboxed) {
  // Async task events from Promises are given misaligned pointers to prevent
  // from overlapping with other Blink task identifiers.
  void* task = reinterpret_cast<void*>(id * 2 + 1);
  switch (type) {
    case v8::debug::kDebugPromiseThen:
      asyncTaskScheduledForStack(toStringView("Promise.then"), task, false);
      if (!isBlackboxed) asyncTaskCandidateForStepping(task);
      break;
    case v8::debug::kDebugPromiseCatch:
      asyncTaskScheduledForStack(toStringView("Promise.catch"), task, false);
      if (!isBlackboxed) asyncTaskCandidateForStepping(task);
      break;
    case v8::debug::kDebugPromiseFinally:
      asyncTaskScheduledForStack(toStringView("Promise.finally"), task, false);
      if (!isBlackboxed) asyncTaskCandidateForStepping(task);
      break;
    case v8::debug::kDebugWillHandle:
      asyncTaskStartedForStack(task);
      asyncTaskStartedForStepping(task);
      break;
    case v8::debug::kDebugDidHandle:
      asyncTaskFinishedForStack(task);
      asyncTaskFinishedForStepping(task);
      break;
    case v8::debug::kDebugAwait: {
      asyncTaskScheduledForStack(toStringView("await"), task, false, true);
      break;
    }
  }
}

std::shared_ptr<AsyncStackTrace> V8Debugger::currentAsyncParent() {
  return m_currentAsyncParent.empty() ? nullptr : m_currentAsyncParent.back();
}

V8StackTraceId V8Debugger::currentExternalParent() {
  return m_currentExternalParent.empty() ? V8StackTraceId()
                                         : m_currentExternalParent.back();
}

v8::MaybeLocal<v8::Value> V8Debugger::getTargetScopes(
    v8::Local<v8::Context> context, v8::Local<v8::Value> value,
    ScopeTargetKind kind) {
  std::unique_ptr<v8::debug::ScopeIterator> iterator;
  switch (kind) {
    case FUNCTION:
      iterator = v8::debug::ScopeIterator::CreateForFunction(
          m_isolate, value.As<v8::Function>());
      break;
    case GENERATOR:
      v8::Local<v8::debug::GeneratorObject> generatorObject =
          v8::debug::GeneratorObject::Cast(value);
      if (!generatorObject->IsSuspended()) return v8::MaybeLocal<v8::Value>();

      iterator = v8::debug::ScopeIterator::CreateForGeneratorObject(
          m_isolate, value.As<v8::Object>());
      break;
  }
  if (!iterator) return v8::MaybeLocal<v8::Value>();
  v8::Local<v8::Array> result = v8::Array::New(m_isolate);
  if (!result->SetPrototypeV2(context, v8::Null(m_isolate)).FromMaybe(false)) {
    return v8::MaybeLocal<v8::Value>();
  }

  for (; !iterator->Done(); iterator->Advance()) {
    v8::Local<v8::Object> scope = v8::Object::New(m_isolate);
    if (!addInternalObject(context, scope, V8InternalValueType::kScope))
      return v8::MaybeLocal<v8::Value>();
    String16 nameSuffix = toProtocolStringWithTypeCheck(
        m_isolate, iterator->GetFunctionDebugName());
    String16 description;
    if (nameSuffix.length()) nameSuffix = " (" + nameSuffix + ")";
    switch (iterator->GetType()) {
      case v8::debug::ScopeIterator::ScopeTypeGlobal:
        description = "Global" + nameSuffix;
        break;
      case v8::debug::ScopeIterator::ScopeTypeLocal:
        description = "Local" + nameSuffix;
        break;
      case v8::debug::ScopeIterator::ScopeTypeWith:
        description = "With Block" + nameSuffix;
        break;
      case v8::debug::ScopeIterator::ScopeTypeClosure:
        description = "Closure" + nameSuffix;
        break;
      case v8::debug::ScopeIterator::ScopeTypeCatch:
        description = "Catch" + nameSuffix;
        break;
      case v8::debug::ScopeIterator::ScopeTypeBlock:
        description = "Block" + nameSuffix;
        break;
      case v8::debug::ScopeIterator::ScopeTypeScript:
        description = "Script" + nameSuffix;
        break;
      case v8::debug::ScopeIterator::ScopeTypeEval:
        description = "Eval" + nameSuffix;
        break;
      case v8::debug::ScopeIterator::ScopeTypeModule:
        description = "Module" + nameSuffix;
        break;
      case v8::debug::ScopeIterator::ScopeTypeWasmExpressionStack:
        description = "Wasm Expression Stack" + nameSuffix;
        break;
    }
    v8::Local<v8::Object> object = iterator->GetObject();
    createDataProperty(context, scope,
                       toV8StringInternalized(m_isolate, "description"),
                       toV8String(m_isolate, description));
    createDataProperty(context, scope,
                       toV8StringInternalized(m_isolate, "object"), object);
    createDataProperty(context, result, result->Length(), scope);
  }
  if (!addInternalObject(context, result, V8InternalValueType::kScopeList))
    return v8::MaybeLocal<v8::Value>();
  return result;
}

v8::MaybeLocal<v8::Value> V8Debugger::functionScopes(
    v8::Local<v8::Context> context, v8::Local<v8::Function> function) {
  return getTargetScopes(context, function, FUNCTION);
}

v8::MaybeLocal<v8::Value> V8Debugger::generatorScopes(
    v8::Local<v8::Context> context, v8::Local<v8::Value> generator) {
  return getTargetScopes(context, generator, GENERATOR);
}

v8::MaybeLocal<v8::Array> V8Debugger::collectionsEntries(
    v8::Local<v8::Context> context, v8::Local<v8::Value> collection) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::Local<v8::Array> entries;
  bool isKeyValue = false;
  if (!collection->IsObject() || !collection.As<v8::Object>()
                                      ->PreviewEntries(&isKeyValue)
                                      .ToLocal(&entries)) {
    return v8::MaybeLocal<v8::Array>();
  }

  v8::Local<v8::Array> wrappedEntries = v8::Array::New(isolate);
  CHECK(!isKeyValue || wrappedEntries->Length() % 2 == 0);
  if (!wrappedEntries->SetPrototypeV2(context, v8::Null(isolate))
           .FromMaybe(false))
    return v8::MaybeLocal<v8::Array>();
  for (uint32_t i = 0; i < entries->Length(); i += isKeyValue ? 2 : 1) {
    v8::Local<v8::Value> item;
    if (!entries->Get(context, i).ToLocal(&item)) continue;
    v8::Local<v8::Value> value;
    if (isKeyValue && !entries->Get(context, i + 1).ToLocal(&value)) continue;
    v8::Local<v8::Object> wrapper = v8::Object::New(isolate);
    if (!wrapper->SetPrototypeV2(context, v8::Null(isolate)).FromMaybe(false))
      continue;
    createDataProperty(
        context, wrapper,
        toV8StringInternalized(isolate, isKeyValue ? "key" : "value"), item);
    if (isKeyValue) {
      createDataProperty(context, wrapper,
                         toV8StringInternalized(isolate, "value"), value);
    }
    if (!addInternalObject(context, wrapper, V8InternalValueType::kEntry))
      continue;
    createDataProperty(context, wrappedEntries, wrappedEntries->Length(),
                       wrapper);
  }
  return wrappedEntries;
}

v8::MaybeLocal<v8::Array> V8Debugger::privateMethods(
    v8::Local<v8::Context> context, v8::Local<v8::Value> receiver) {
  if (!receiver->IsObject()) {
    return v8::MaybeLocal<v8::Array>();
  }
  v8::Isolate* isolate = context->GetIsolate();
  v8::LocalVector<v8::Value> names(isolate);
  v8::LocalVector<v8::Value> values(isolate);
  int filter =
      static_cast<int>(v8::debug::PrivateMemberFilter::kPrivateMethods);
  if (!v8::debug::GetPrivateMembers(context, receiver.As<v8::Object>(), filter,
                                    &names, &values) ||
      names.empty()) {
    return v8::MaybeLocal<v8::Array>();
  }

  v8::Local<v8::Array> result = v8::Array::New(isolate);
  if (!result->SetPrototypeV2(context, v8::Null(isolate)).FromMaybe(false))
    return v8::MaybeLocal<v8::Array>();
  for (uint32_t i = 0; i < names.size(); i++) {
    v8::Local<v8::Value> name = names[i];
    v8::Local<v8::Value> value = values[i];
    DCHECK(value->IsFunction());
    v8::Local<v8::Object> wrapper = v8::Object::New(isolate);
    if (!wrapper->SetPrototypeV2(context, v8::Null(isolate)).FromMaybe(false))
      continue;
    createDataProperty(context, wrapper,
                       toV8StringInternalized(isolate, "name"), name);
    createDataProperty(context, wrapper,
                       toV8StringInternalized(isolate, "value"), value);
    if (!addInternalObject(context, wrapper,
                           V8InternalValueType::kPrivateMethod))
      continue;
    createDataProperty(context, result, result->Length(), wrapper);
  }

  if (!addInternalObject(context, result,
                         V8InternalValueType::kPrivateMethodList))
    return v8::MaybeLocal<v8::Array>();
  return result;
}

v8::MaybeLocal<v8::Array> V8Debugger::internalProperties(
    v8::Local<v8::Context> context, v8::Local<v8::Value> value) {
  v8::Local<v8::Array> properties;
  if (!v8::debug::GetInternalProperties(m_isolate, value).ToLocal(&properties))
    return v8::MaybeLocal<v8::Array>();
  v8::Local<v8::Array> entries;
  if (collectionsEntries(context, value).ToLocal(&entries)) {
    createDataProperty(context, properties, properties->Length(),
                       toV8StringInternalized(m_isolate, "[[Entries]]"));
    createDataProperty(context, properties, properties->Length(), entries);
  }

  if (value->IsGeneratorObject()) {
    v8::Local<v8::Value> scopes;
    if (generatorScopes(context, value).ToLocal(&scopes)) {
      createDataProperty(context, properties, properties->Length(),
                         toV8StringInternalized(m_isolate, "[[Scopes]]"));
      createDataProperty(context, properties, properties->Length(), scopes);
    }
  }
  if (value->IsFunction()) {
    v8::Local<v8::Function> function = value.As<v8::Function>();
    v8::Local<v8::Value> scopes;
    if (functionScopes(context, function).ToLocal(&scopes)) {
      createDataProperty(context, properties, properties->Length(),
                         toV8StringInternalized(m_isolate, "[[Scopes]]"));
      createDataProperty(context, properties, properties->Length(), scopes);
    }
  }
  v8::Local<v8::Array> private_methods;
  if (privateMethods(context, value).ToLocal(&private_methods)) {
    createDataProperty(context, properties, properties->Length(),
                       toV8StringInternalized(m_isolate, "[[PrivateMethods]]"));
    createDataProperty(context, properties, properties->Length(),
                       private_methods);
  }
  return properties;
}

v8::Local<v8::Array> V8Debugger::queryObjects(v8::Local<v8::Context> context,
                                              v8::Local<v8::Object> prototype) {
  v8::Isolate* isolate = context->GetIsolate();
  std::vector<v8::Global<v8::Object>> v8_objects;
  MatchPrototypePredicate predicate(m_inspector, context, prototype);
  isolate->GetHeapProfiler()->QueryObjects(context, &predicate, &v8_objects);

  v8::MicrotasksScope microtasksScope(context,
                                      v8::MicrotasksScope::kDoNotRunMicrotasks);
  v8::Local<v8::Array> resultArray = v8::Array::New(
      m_inspector->isolate(), static_cast<int>(v8_objects.size()));
  for (size_t i = 0; i < v8_objects.size(); ++i) {
    createDataProperty(context, resultArray, static_cast<int>(i),
                       v8_objects[i].Get(isolate));
  }
  return resultArray;
}

std::unique_ptr<V8StackTraceImpl> V8Debugger::createStackTrace(
    v8::Local<v8::StackTrace> v8StackTrace) {
  return V8StackTraceImpl::create(
      this, v8StackTrace, V8StackTraceImpl::kDefaultMaxCallStackSizeToCapture);
}

void V8Debugger::setAsyncCallStackDepth(V8DebuggerAgentImpl* agent, int depth) {
  if (depth <= 0)
    m_maxAsyncCallStackDepthMap.erase(agent);
  else
    m_maxAsyncCallStackDepthMap[agent] = depth;

  int maxAsyncCallStackDepth = 0;
  for (const auto& pair : m_maxAsyncCallStackDepthMap) {
    if (pair.second > maxAsyncCallStackDepth)
      maxAsyncCallStackDepth = pair.second;
  }

  if (m_maxAsyncCallStackDepth == maxAsyncCallStackDepth) return;
  // TODO(dgozman): ideally, this should be per context group.
  m_maxAsyncCallStackDepth = maxAsyncCallStackDepth;
  m_inspector->client()->maxAsyncCallStackDepthChanged(
      m_maxAsyncCallStackDepth);
  if (!maxAsyncCallStackDepth) allAsyncTasksCanceled();
  v8::debug::SetAsyncEventDelegate(m_isolate,
                                   maxAsyncCallStackDepth ? this : nullptr);
}

void V8Debugger::setMaxCallStackSizeToCapture(V8RuntimeAgentImpl* agent,
                                              int size) {
  if (size < 0) {
    m_maxCallStackSizeToCaptureMap.erase(agent);
  } else {
    m_maxCallStackSizeToCaptureMap[agent] = size;
  }

  // The following logic is a bit complicated to decipher because we
  // want to retain backwards compatible semantics:
  //
  // (a) When no `Runtime` domain is enabled, we stick to the default
  //     maximum call stack size, but don't let V8 collect stack traces
  //     for uncaught exceptions.
  // (b) When `Runtime` is enabled for at least one front-end, we compute
  //     the maximum of the requested maximum call stack sizes of all the
  //     front-ends whose `Runtime` domains are enabled (which might be 0),
  //     and ask V8 to collect stack traces for uncaught exceptions.
  //
  // The latter allows performance test automation infrastructure to drive
  // browser via `Runtime` domain while still minimizing the performance
  // overhead of having the inspector attached - see the relevant design
  // document https://bit.ly/v8-cheaper-inspector-stack-traces for more
  if (m_maxCallStackSizeToCaptureMap.empty()) {
    m_maxCallStackSizeToCapture =
        V8StackTraceImpl::kDefaultMaxCallStackSizeToCapture;
    m_isolate->SetCaptureStackTraceForUncaughtExceptions(false);
  } else {
    m_maxCallStackSizeToCapture = 0;
    for (auto const& pair : m_maxCallStackSizeToCaptureMap) {
      if (m_maxCallStackSizeToCapture < pair.second)
        m_maxCallStackSizeToCapture = pair.second;
    }
    m_isolate->SetCaptureStackTraceForUncaughtExceptions(
        m_maxCallStackSizeToCapture > 0, m_maxCallStackSizeToCapture);
  }
}

std::shared_ptr<AsyncStackTrace> V8Debugger::stackTraceFor(
    int contextGroupId, const V8StackTraceId& id) {
  if (debuggerIdFor(contextGroupId).pair() != id.debugger_id) return nullptr;
  auto it = m_storedStackTraces.find(id.id);
  if (it == m_storedStackTraces.end()) return nullptr;
  return it->second.lock();
}

V8StackTraceId V8Debugger::storeCurrentStackTrace(
    const StringView& description) {
  if (!m_maxAsyncCallStackDepth) return V8StackTraceId();

  v8::HandleScope scope(m_isolate);
  int contextGroupId = currentContextGroupId();
  if (!contextGroupId) return V8StackTraceId();

  std::shared_ptr<AsyncStackTrace> asyncStack =
      AsyncStackTrace::capture(this, toString16(description));
  if (!asyncStack) return V8StackTraceId();

  uintptr_t id = AsyncStackTrace::store(this, asyncStack);

  m_allAsyncStacks.push_back(std::move(asyncStack));
  collectOldAsyncStacksIfNeeded();

  bool shouldPause =
      m_pauseOnAsyncCall && contextGroupId == m_targetContextGroupId;
  if (shouldPause) {
    m_pauseOnAsyncCall = false;
    v8::debug::ClearStepping(m_isolate);  // Cancel step into.
  }
  return V8StackTraceId(id, debuggerIdFor(contextGroupId).pair(), shouldPause);
}

uintptr_t V8Debugger::storeStackTrace(
    std::shared_ptr<AsyncStackTrace> asyncStack) {
  uintptr_t id = ++m_lastStackTraceId;
  m_storedStackTraces[id] = asyncStack;
  return id;
}

void V8Debugger::externalAsyncTaskStarted(const V8StackTraceId& parent) {
  if (!m_maxAsyncCallStackDepth || parent.IsInvalid()) return;
  m_currentExternalParent.push_back(parent);
  m_currentAsyncParent.emplace_back();
  m_currentTasks.push_back(reinterpret_cast<void*>(parent.id));

  if (!parent.should_pause) return;
  bool didHaveBreak = hasScheduledBreakOnNextFunctionCall();
  m_externalAsyncTaskPauseRequested = true;
  if (didHaveBreak) return;
  m_targetContextGroupId = currentContextGroupId();
  v8::debug::SetBreakOnNextFunctionCall(m_isolate);
}

void V8Debugger::externalAsyncTaskFinished(const V8StackTraceId& parent) {
  if (!m_maxAsyncCallStackDepth || m_currentExternalParent.empty()) return;
  m_currentExternalParent.pop_back();
  m_currentAsyncParent.pop_back();
  DCHECK(m_currentTasks.back() == reinterpret_cast<void*>(parent.id));
  m_currentTasks.pop_back();

  if (!parent.should_pause) return;
  m_externalAsyncTaskPauseRequested = false;
  if (hasScheduledBreakOnNextFunctionCall()) return;
  v8::debug::ClearBreakOnNextFunctionCall(m_isolate);
}

void V8Debugger::asyncTaskScheduled(const StringView& taskName, void* task,
                                    bool recurring) {
  asyncTaskScheduledForStack(taskName, task, recurring);
  asyncTaskCandidateForStepping(task);
}

void V8Debugger::asyncTaskCanceled(void* task) {
  asyncTaskCanceledForStack(task);
  asyncTaskCanceledForStepping(task);
}

void V8Debugger::asyncTaskStarted(void* task) {
  asyncTaskStartedForStack(task);
  asyncTaskStartedForStepping(task);
}

void V8Debugger::asyncTaskFinished(void* task) {
  asyncTaskFinishedForStepping(task);
  asyncTaskFinishedForStack(task);
}

void V8Debugger::asyncTaskScheduledForStack(const StringView& taskName,
                                            void* task, bool recurring,
                                            bool skipTopFrame) {
  if (!m_maxAsyncCallStackDepth) return;
  v8::HandleScope scope(m_isolate);
  std::shared_ptr<AsyncStackTrace> asyncStack =
      AsyncStackTrace::capture(this, toString16(taskName), skipTopFrame);
  if (asyncStack) {
    m_asyncTaskStacks[task] = asyncStack;
    if (recurring) m_recurringTasks.insert(task);
    m_allAsyncStacks.push_back(std::move(asyncStack));
    collectOldAsyncStacksIfNeeded();
  }
}

void V8Debugger::asyncTaskCanceledForStack(void* task) {
  if (!m_maxAsyncCallStackDepth) return;
  m_asyncTaskStacks.erase(task);
  m_recurringTasks.erase(task);
}

void V8Debugger::asyncTaskStartedForStack(void* task) {
  if (!m_maxAsyncCallStackDepth) return;
  // Needs to support following order of events:
  // - asyncTaskScheduled
  //   <-- attached here -->
  // - asyncTaskStarted
  // - asyncTaskCanceled <-- canceled before finished
  //   <-- async stack requested here -->
  // - asyncTaskFinished
  m_currentTasks.push_back(task);
  AsyncTaskToStackTrace::iterator stackIt = m_asyncTaskStacks.find(task);
  if (stackIt != m_asyncTaskStacks.end() && !stackIt->second.expired()) {
    std::shared_ptr<AsyncStackTrace> stack(stackIt->second);
    m_currentAsyncParent.push_back(stack);
  } else {
    m_currentAsyncParent.emplace_back();
  }
  m_currentExternalParent.emplace_back();
}

void V8Debugger::asyncTaskFinishedForStack(void* task) {
  if (!m_maxAsyncCallStackDepth) return;
  // We could start instrumenting half way and the stack is empty.
  if (m_currentTasks.empty()) return;
  DCHECK(m_currentTasks.back() == task);
  m_currentTasks.pop_back();

  m_currentAsyncParent.pop_back();
  m_currentExternalParent.pop_back();

  if (m_recurringTasks.find(task) == m_recurringTasks.end()) {
    asyncTaskCanceledForStack(task);
  }
}

void V8Debugger::asyncTaskCandidateForStepping(void* task) {
  if (!m_pauseOnAsyncCall) return;
  int contextGroupId = currentContextGroupId();
  if (contextGroupId != m_targetContextGroupId) return;
  m_taskWithScheduledBreak = task;
  m_pauseOnAsyncCall = false;
  v8::debug::ClearStepping(m_isolate);  // Cancel step into.
}

void V8Debugger::asyncTaskStartedForStepping(void* task) {
  // TODO(kozyatinskiy): we should search task in async chain to support
  // blackboxing.
  if (task != m_taskWithScheduledBreak) return;
  bool didHaveBreak = hasScheduledBreakOnNextFunctionCall();
  m_taskWithScheduledBreakPauseRequested = true;
  if (didHaveBreak) return;
  m_targetContextGroupId = currentContextGroupId();
  v8::debug::SetBreakOnNextFunctionCall(m_isolate);
}

void V8Debugger::asyncTaskFinishedForStepping(void* task) {
  if (task != m_taskWithScheduledBreak) return;
  m_taskWithScheduledBreak = nullptr;
  m_taskWithScheduledBreakPauseRequested = false;
  if (hasScheduledBreakOnNextFunctionCall()) return;
  v8::debug::ClearBreakOnNextFunctionCall(m_isolate);
}

void V8Debugger::asyncTaskCanceledForStepping(void* task) {
  asyncTaskFinishedForStepping(task);
}

void V8Debugger::allAsyncTasksCanceled() {
  m_asyncTaskStacks.clear();
  m_recurringTasks.clear();
  m_currentAsyncParent.clear();
  m_currentExternalParent.clear();
  m_currentTasks.clear();

  m_allAsyncStacks.clear();
}

void V8Debugger::muteScriptParsedEvents() {
  ++m_ignoreScriptParsedEventsCounter;
}

void V8Debugger::unmuteScriptParsedEvents() {
  --m_ignoreScriptParsedEventsCounter;
  DCHECK_GE(m_ignoreScriptParsedEventsCounter, 0);
}

std::unique_ptr<V8StackTraceImpl> V8Debugger::captureStackTrace(
    bool fullStack) {
  int contextGroupId = currentContextGroupId();
  if (!contextGroupId) return nullptr;

  int stackSize = 1;
  if (fullStack) {
    stackSize = V8StackTraceImpl::kDefaultMaxCallStackSizeToCapture;
  } else {
    m_inspector->forEachSession(
        contextGroupId, [this, &stackSize](V8InspectorSessionImpl* session) {
          if (session->runtimeAgent()->enabled())
            stackSize = maxCallStackSizeToCapture();
        });
  }
  return V8StackTraceImpl::capture(this, stackSize);
}

int V8Debugger::currentContextGroupId() {
  if (!m_isolate->InContext()) return 0;
  v8::HandleScope handleScope(m_isolate);
  return m_inspector->contextGroupId(m_isolate->GetCurrentContext());
}

void V8Debugger::collectOldAsyncStacksIfNeeded() {
  if (m_allAsyncStacks.size() <= m_maxAsyncCallStacks) return;
  size_t halfOfLimitRoundedUp =
      m_maxAsyncCallStacks / 2 + m_maxAsyncCallStacks % 2;
  while (m_allAsyncStacks.size() > halfOfLimitRoundedUp) {
    m_allAsyncStacks.pop_front();
  }
  cleanupExpiredWeakPointers(m_asyncTaskStacks);
  cleanupExpiredWeakPointers(m_cachedStackFrames);
  cleanupExpiredWeakPointers(m_storedStackTraces);
  for (auto it = m_recurringTasks.begin(); it != m_recurringTasks.end();) {
    if (m_asyncTaskStacks.find(*it) == m_asyncTaskStacks.end()) {
      it = m_recurringTasks.erase(it);
    } else {
      ++it;
    }
  }
}

std::shared_ptr<StackFrame> V8Debugger::symbolize(
    v8::Local<v8::StackFrame> v8Frame) {
  int scriptId = v8Frame->GetScriptId();
  auto location = v8Frame->GetLocation();
  int lineNumber = location.GetLineNumber();
  int columnNumber = location.GetColumnNumber();
  CachedStackFrameKey key{scriptId, lineNumber, columnNumber};
  auto functionName = toProtocolString(isolate(), v8Frame->GetFunctionName());
  auto it = m_cachedStackFrames.find(key);
  if (it != m_cachedStackFrames.end() && !it->second.expired()) {
    auto stackFrame = it->second.lock();
    if (stackFrame->functionName() == functionName) {
      DCHECK_EQ(
          stackFrame->sourceURL(),
          toProtocolString(isolate(), v8Frame->GetScriptNameOrSourceURL()));
      return stackFrame;
    }
  }
  auto sourceURL =
      toProtocolString(isolate(), v8Frame->GetScriptNameOrSourceURL());
  auto hasSourceURLComment =
      v8Frame->GetScriptName() != v8Frame->GetScriptNameOrSourceURL();
  auto stackFrame = std::make_shared<StackFrame>(
      std::move(functionName), scriptId, std::move(sourceURL), lineNumber,
      columnNumber, hasSourceURLComment);
  m_cachedStackFrames.emplace(key, stackFrame);
  return stackFrame;
}

void V8Debugger::setMaxAsyncTaskStacksForTest(int limit) {
  m_maxAsyncCallStacks = 0;
  collectOldAsyncStacksIfNeeded();
  m_maxAsyncCallStacks = limit;
}

internal::V8DebuggerId V8Debugger::debuggerIdFor(int contextGroupId) {
  auto it = m_contextGroupIdToDebuggerId.find(contextGroupId);
  if (it != m_contextGroupIdToDebuggerId.end()) return it->second;
  internal::V8DebuggerId debuggerId =
      internal::V8DebuggerId::generate(m_inspector);
  m_contextGroupIdToDebuggerId.insert(
      it, std::make_pair(contextGroupId, debuggerId));
  return debuggerId;
}

bool V8Debugger::addInternalObject(v8::Local<v8::Context> context,
                                   v8::Local<v8::Object> object,
                                   V8InternalValueType type) {
  int contextId = InspectedContext::contextId(context);
  InspectedContext* inspectedContext = m_inspector->getContext(contextId);
  return inspectedContext ? inspectedContext->addInternalObject(object, type)
                          : false;
}

void V8Debugger::dumpAsyncTaskStacksStateForTest() {
  fprintf(stdout, "Async stacks count: %zu\n", m_allAsyncStacks.size());
  fprintf(stdout, "Scheduled async tasks: %zu\n", m_asyncTaskStacks.size());
  fprintf(stdout, "Recurring async tasks: %zu\n", m_recurringTasks.size());
  fprintf(stdout, "\n");
}

bool V8Debugger::hasScheduledBreakOnNextFunctionCall() const {
  return m_pauseOnNextCallRequested || m_taskWithScheduledBreakPauseRequested ||
         m_externalAsyncTaskPauseRequested;
}

}  // namespace v8_inspector
