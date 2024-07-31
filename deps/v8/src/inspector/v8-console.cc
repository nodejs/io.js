// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/inspector/v8-console.h"

#include "include/v8-container.h"
#include "include/v8-context.h"
#include "include/v8-function.h"
#include "include/v8-inspector.h"
#include "include/v8-microtask-queue.h"
#include "src/base/macros.h"
#include "src/debug/debug-interface.h"
#include "src/inspector/injected-script.h"
#include "src/inspector/inspected-context.h"
#include "src/inspector/string-util.h"
#include "src/inspector/v8-console-message.h"
#include "src/inspector/v8-debugger-agent-impl.h"
#include "src/inspector/v8-inspector-impl.h"
#include "src/inspector/v8-inspector-session-impl.h"
#include "src/inspector/v8-profiler-agent-impl.h"
#include "src/inspector/v8-runtime-agent-impl.h"
#include "src/inspector/v8-stack-trace-impl.h"
#include "src/inspector/v8-value-utils.h"
#include "src/tracing/trace-event.h"

namespace v8_inspector {

namespace {

String16 consoleContextToString(
    v8::Isolate* isolate, const v8::debug::ConsoleContext& consoleContext) {
  if (consoleContext.id() == 0) return String16();
  return toProtocolString(isolate, consoleContext.name()) + "#" +
         String16::fromInteger(consoleContext.id());
}

class ConsoleHelper {
 public:
  ConsoleHelper(const v8::debug::ConsoleCallArguments& info,
                const v8::debug::ConsoleContext& consoleContext,
                V8InspectorImpl* inspector)
      : m_info(info),
        m_consoleContext(consoleContext),
        m_isolate(inspector->isolate()),
        m_context(m_isolate->GetCurrentContext()),
        m_inspector(inspector),
        m_contextId(InspectedContext::contextId(m_context)),
        m_groupId(m_inspector->contextGroupId(m_contextId)) {}

  ConsoleHelper(const ConsoleHelper&) = delete;
  ConsoleHelper& operator=(const ConsoleHelper&) = delete;

  int contextId() const { return m_contextId; }
  int groupId() const { return m_groupId; }

  InjectedScript* injectedScript(int sessionId) {
    InspectedContext* context = m_inspector->getContext(m_groupId, m_contextId);
    if (!context) return nullptr;
    return context->getInjectedScript(sessionId);
  }

  V8InspectorSessionImpl* session(int sessionId) {
    return m_inspector->sessionById(m_groupId, sessionId);
  }

  V8ConsoleMessageStorage* consoleMessageStorage() {
    return m_inspector->ensureConsoleMessageStorage(m_groupId);
  }

  void reportCall(ConsoleAPIType type) {
    if (!m_info.Length()) return;
    v8::LocalVector<v8::Value> arguments(m_isolate);
    arguments.reserve(m_info.Length());
    for (int i = 0; i < m_info.Length(); ++i) arguments.push_back(m_info[i]);
    reportCall(type, {arguments.begin(), arguments.end()});
  }

  void reportCallWithDefaultArgument(ConsoleAPIType type,
                                     const String16& message) {
    v8::LocalVector<v8::Value> arguments(m_isolate);
    arguments.reserve(m_info.Length());
    for (int i = 0; i < m_info.Length(); ++i) arguments.push_back(m_info[i]);
    if (!m_info.Length()) arguments.push_back(toV8String(m_isolate, message));
    reportCall(type, {arguments.begin(), arguments.end()});
  }

  void reportCallAndReplaceFirstArgument(ConsoleAPIType type,
                                         const String16& message) {
    v8::LocalVector<v8::Value> arguments(m_isolate);
    arguments.push_back(toV8String(m_isolate, message));
    for (int i = 1; i < m_info.Length(); ++i) arguments.push_back(m_info[i]);
    reportCall(type, {arguments.begin(), arguments.end()});
  }

  void reportCallWithArgument(ConsoleAPIType type, const String16& message) {
    auto arguments =
        v8::to_array<v8::Local<v8::Value>>({toV8String(m_isolate, message)});
    reportCall(type, arguments);
  }

  void reportCall(ConsoleAPIType type,
                  v8::MemorySpan<const v8::Local<v8::Value>> arguments) {
    if (!m_groupId) return;
    std::unique_ptr<V8ConsoleMessage> message =
        V8ConsoleMessage::createForConsoleAPI(
            m_context, m_contextId, m_groupId, m_inspector,
            m_inspector->client()->currentTimeMS(), type, arguments,
            consoleContextToString(m_isolate, m_consoleContext),
            m_inspector->debugger()->captureStackTrace(false));
    consoleMessageStorage()->addMessage(std::move(message));
  }

  void reportDeprecatedCall(const char* id, const String16& message) {
    if (!consoleMessageStorage()->shouldReportDeprecationMessage(m_contextId,
                                                                 id)) {
      return;
    }
    auto arguments =
        v8::to_array<v8::Local<v8::Value>>({toV8String(m_isolate, message)});
    reportCall(ConsoleAPIType::kWarning, arguments);
  }

  bool firstArgToBoolean(bool defaultValue) {
    if (m_info.Length() < 1) return defaultValue;
    if (m_info[0]->IsBoolean()) return m_info[0].As<v8::Boolean>()->Value();
    return m_info[0]->BooleanValue(m_context->GetIsolate());
  }

  String16 firstArgToString(const String16& defaultValue,
                            bool allowUndefined = true) {
    if (m_info.Length() < 1 || (!allowUndefined && m_info[0]->IsUndefined())) {
      return defaultValue;
    }
    v8::Local<v8::String> titleValue;
    if (!m_info[0]->ToString(m_context).ToLocal(&titleValue))
      return defaultValue;
    return toProtocolString(m_context->GetIsolate(), titleValue);
  }

  v8::MaybeLocal<v8::Object> firstArgAsObject() {
    if (m_info.Length() < 1 || !m_info[0]->IsObject())
      return v8::MaybeLocal<v8::Object>();
    return m_info[0].As<v8::Object>();
  }

  v8::MaybeLocal<v8::Function> firstArgAsFunction() {
    if (m_info.Length() < 1 || !m_info[0]->IsFunction())
      return v8::MaybeLocal<v8::Function>();
    v8::Local<v8::Function> func = m_info[0].As<v8::Function>();
    while (func->GetBoundFunction()->IsFunction())
      func = func->GetBoundFunction().As<v8::Function>();
    return func;
  }

  void forEachSession(std::function<void(V8InspectorSessionImpl*)> callback) {
    m_inspector->forEachSession(m_groupId, std::move(callback));
  }

 private:
  const v8::debug::ConsoleCallArguments& m_info;
  const v8::debug::ConsoleContext& m_consoleContext;
  v8::Isolate* m_isolate;
  v8::Local<v8::Context> m_context;
  V8InspectorImpl* m_inspector = nullptr;
  int m_contextId;
  int m_groupId;
};

void createBoundFunctionProperty(
    v8::Local<v8::Context> context, v8::Local<v8::Object> console,
    v8::Local<v8::Value> data, const char* name, v8::FunctionCallback callback,
    v8::SideEffectType side_effect_type = v8::SideEffectType::kHasSideEffect) {
  v8::Local<v8::String> funcName =
      toV8StringInternalized(context->GetIsolate(), name);
  v8::Local<v8::Function> func;
  if (!v8::Function::New(context, callback, data, 0,
                         v8::ConstructorBehavior::kThrow, side_effect_type)
           .ToLocal(&func))
    return;
  func->SetName(funcName);
  createDataProperty(context, console, funcName, func);
}

enum InspectRequest { kRegular, kCopyToClipboard, kQueryObjects };

}  // namespace

V8Console::V8Console(V8InspectorImpl* inspector) : m_inspector(inspector) {}

void V8Console::Debug(const v8::debug::ConsoleCallArguments& info,
                      const v8::debug::ConsoleContext& consoleContext) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.inspector"), "V8Console::Debug");
  ConsoleHelper(info, consoleContext, m_inspector)
      .reportCall(ConsoleAPIType::kDebug);
}

void V8Console::Error(const v8::debug::ConsoleCallArguments& info,
                      const v8::debug::ConsoleContext& consoleContext) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.inspector"), "V8Console::Error");
  ConsoleHelper(info, consoleContext, m_inspector)
      .reportCall(ConsoleAPIType::kError);
}

void V8Console::Info(const v8::debug::ConsoleCallArguments& info,
                     const v8::debug::ConsoleContext& consoleContext) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.inspector"), "V8Console::Info");
  ConsoleHelper(info, consoleContext, m_inspector)
      .reportCall(ConsoleAPIType::kInfo);
}

void V8Console::Log(const v8::debug::ConsoleCallArguments& info,
                    const v8::debug::ConsoleContext& consoleContext) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.inspector"), "V8Console::Log");
  ConsoleHelper(info, consoleContext, m_inspector)
      .reportCall(ConsoleAPIType::kLog);
}

void V8Console::Warn(const v8::debug::ConsoleCallArguments& info,
                     const v8::debug::ConsoleContext& consoleContext) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.inspector"), "V8Console::Warn");
  ConsoleHelper(info, consoleContext, m_inspector)
      .reportCall(ConsoleAPIType::kWarning);
}

void V8Console::Dir(const v8::debug::ConsoleCallArguments& info,
                    const v8::debug::ConsoleContext& consoleContext) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.inspector"), "V8Console::Dir");
  ConsoleHelper(info, consoleContext, m_inspector)
      .reportCall(ConsoleAPIType::kDir);
}

void V8Console::DirXml(const v8::debug::ConsoleCallArguments& info,
                       const v8::debug::ConsoleContext& consoleContext) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.inspector"), "V8Console::DirXml");
  ConsoleHelper(info, consoleContext, m_inspector)
      .reportCall(ConsoleAPIType::kDirXML);
}

void V8Console::Table(const v8::debug::ConsoleCallArguments& info,
                      const v8::debug::ConsoleContext& consoleContext) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.inspector"), "V8Console::Table");
  ConsoleHelper(info, consoleContext, m_inspector)
      .reportCall(ConsoleAPIType::kTable);
}

void V8Console::Trace(const v8::debug::ConsoleCallArguments& info,
                      const v8::debug::ConsoleContext& consoleContext) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.inspector"), "V8Console::Trace");
  ConsoleHelper(info, consoleContext, m_inspector)
      .reportCallWithDefaultArgument(ConsoleAPIType::kTrace,
                                     String16("console.trace"));
}

void V8Console::Group(const v8::debug::ConsoleCallArguments& info,
                      const v8::debug::ConsoleContext& consoleContext) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.inspector"), "V8Console::Group");
  ConsoleHelper(info, consoleContext, m_inspector)
      .reportCallWithDefaultArgument(ConsoleAPIType::kStartGroup,
                                     String16("console.group"));
}

void V8Console::GroupCollapsed(
    const v8::debug::ConsoleCallArguments& info,
    const v8::debug::ConsoleContext& consoleContext) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.inspector"),
               "V8Console::GroupCollapsed");
  ConsoleHelper(info, consoleContext, m_inspector)
      .reportCallWithDefaultArgument(ConsoleAPIType::kStartGroupCollapsed,
                                     String16("console.groupCollapsed"));
}

void V8Console::GroupEnd(const v8::debug::ConsoleCallArguments& info,
                         const v8::debug::ConsoleContext& consoleContext) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.inspector"),
               "V8Console::GroupEnd");
  ConsoleHelper(info, consoleContext, m_inspector)
      .reportCallWithDefaultArgument(ConsoleAPIType::kEndGroup,
                                     String16("console.groupEnd"));
}

void V8Console::Clear(const v8::debug::ConsoleCallArguments& info,
                      const v8::debug::ConsoleContext& consoleContext) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.inspector"), "V8Console::Clear");
  ConsoleHelper helper(info, consoleContext, m_inspector);
  if (!helper.groupId()) return;
  m_inspector->client()->consoleClear(helper.groupId());
  helper.reportCallWithDefaultArgument(ConsoleAPIType::kClear,
                                       String16("console.clear"));
}

static String16 identifierFromTitleOrStackTrace(
    const String16& title, const ConsoleHelper& helper,
    const v8::debug::ConsoleContext& consoleContext,
    V8InspectorImpl* inspector) {
  String16 identifier;
  if (title.isEmpty()) {
    std::unique_ptr<V8StackTraceImpl> stackTrace =
        V8StackTraceImpl::capture(inspector->debugger(), 1);
    if (stackTrace && !stackTrace->isEmpty()) {
      identifier = toString16(stackTrace->topSourceURL()) + ":" +
                   String16::fromInteger(stackTrace->topLineNumber());
    }
  } else {
    identifier = title + "@";
  }
  identifier = consoleContextToString(inspector->isolate(), consoleContext) +
               "@" + identifier;

  return identifier;
}

void V8Console::Count(const v8::debug::ConsoleCallArguments& info,
                      const v8::debug::ConsoleContext& consoleContext) {
  TRACE_EVENT_BEGIN0(TRACE_DISABLED_BY_DEFAULT("v8.inspector"),
                     "V8Console::Count");
  ConsoleHelper helper(info, consoleContext, m_inspector);
  String16 title = helper.firstArgToString(String16("default"), false);
  String16 identifier = identifierFromTitleOrStackTrace(
      title, helper, consoleContext, m_inspector);

  int count =
      helper.consoleMessageStorage()->count(helper.contextId(), identifier);
  String16 countString = String16::fromInteger(count);
  helper.reportCallWithArgument(
      ConsoleAPIType::kCount,
      title.isEmpty() ? countString : (title + ": " + countString));
  TRACE_EVENT_END2(TRACE_DISABLED_BY_DEFAULT("v8.inspector"),
                   "V8Console::Count", "title",
                   TRACE_STR_COPY(title.utf8().c_str()), "count", count);
}

void V8Console::CountReset(const v8::debug::ConsoleCallArguments& info,
                           const v8::debug::ConsoleContext& consoleContext) {
  TRACE_EVENT_BEGIN0(TRACE_DISABLED_BY_DEFAULT("v8.inspector"),
                     "V8Console::CountReset");
  ConsoleHelper helper(info, consoleContext, m_inspector);
  String16 title = helper.firstArgToString(String16("default"), false);
  String16 identifier = identifierFromTitleOrStackTrace(
      title, helper, consoleContext, m_inspector);

  if (!helper.consoleMessageStorage()->countReset(helper.contextId(),
                                                  identifier)) {
    helper.reportCallWithArgument(ConsoleAPIType::kWarning,
                                  "Count for '" + title + "' does not exist");
  }
  TRACE_EVENT_END1(TRACE_DISABLED_BY_DEFAULT("v8.inspector"),
                   "V8Console::CountReset", "title",
                   TRACE_STR_COPY(title.utf8().c_str()));
}

void V8Console::Assert(const v8::debug::ConsoleCallArguments& info,
                       const v8::debug::ConsoleContext& consoleContext) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.inspector"), "V8Console::Assert");
  ConsoleHelper helper(info, consoleContext, m_inspector);
  DCHECK(!helper.firstArgToBoolean(false));

  v8::Isolate* isolate = m_inspector->isolate();
  v8::LocalVector<v8::Value> arguments(isolate);
  for (int i = 1; i < info.Length(); ++i) arguments.push_back(info[i]);
  if (info.Length() < 2)
    arguments.push_back(toV8String(isolate, String16("console.assert")));
  helper.reportCall(ConsoleAPIType::kAssert,
                    {arguments.begin(), arguments.end()});
  m_inspector->debugger()->breakProgramOnAssert(helper.groupId());
}

void V8Console::Profile(const v8::debug::ConsoleCallArguments& info,
                        const v8::debug::ConsoleContext& consoleContext) {
  TRACE_EVENT_BEGIN0(TRACE_DISABLED_BY_DEFAULT("v8.inspector"),
                     "V8Console::Profile");
  ConsoleHelper helper(info, consoleContext, m_inspector);
  String16 title = helper.firstArgToString(String16());
  helper.forEachSession([&title](V8InspectorSessionImpl* session) {
    session->profilerAgent()->consoleProfile(title);
  });
  TRACE_EVENT_END1(TRACE_DISABLED_BY_DEFAULT("v8.inspector"),
                   "V8Console::Profile", "title",
                   TRACE_STR_COPY(title.utf8().c_str()));
}

void V8Console::ProfileEnd(const v8::debug::ConsoleCallArguments& info,
                           const v8::debug::ConsoleContext& consoleContext) {
  TRACE_EVENT_BEGIN0(TRACE_DISABLED_BY_DEFAULT("v8.inspector"),
                     "V8Console::ProfileEnd");
  ConsoleHelper helper(info, consoleContext, m_inspector);
  String16 title = helper.firstArgToString(String16());
  helper.forEachSession([&title](V8InspectorSessionImpl* session) {
    session->profilerAgent()->consoleProfileEnd(title);
  });
  TRACE_EVENT_END1(TRACE_DISABLED_BY_DEFAULT("v8.inspector"),
                   "V8Console::ProfileEnd", "title",
                   TRACE_STR_COPY(title.utf8().c_str()));
}

static void timeFunction(const v8::debug::ConsoleCallArguments& info,
                         const v8::debug::ConsoleContext& consoleContext,
                         bool timelinePrefix, V8InspectorImpl* inspector) {
  ConsoleHelper helper(info, consoleContext, inspector);
  String16 protocolTitle = helper.firstArgToString("default", false);
  if (timelinePrefix) protocolTitle = "Timeline '" + protocolTitle + "'";
  const String16& timerId =
      protocolTitle + "@" +
      consoleContextToString(inspector->isolate(), consoleContext);
  if (helper.consoleMessageStorage()->hasTimer(helper.contextId(), timerId)) {
    helper.reportCallWithArgument(
        ConsoleAPIType::kWarning,
        "Timer '" + protocolTitle + "' already exists");
    return;
  }
  inspector->client()->consoleTime(toStringView(protocolTitle));
  helper.consoleMessageStorage()->time(helper.contextId(), timerId);
}

static void timeEndFunction(const v8::debug::ConsoleCallArguments& info,
                            const v8::debug::ConsoleContext& consoleContext,
                            bool timeLog, V8InspectorImpl* inspector) {
  ConsoleHelper helper(info, consoleContext, inspector);
  String16 protocolTitle = helper.firstArgToString("default", false);
  const String16& timerId =
      protocolTitle + "@" +
      consoleContextToString(inspector->isolate(), consoleContext);
  if (!helper.consoleMessageStorage()->hasTimer(helper.contextId(), timerId)) {
    helper.reportCallWithArgument(
        ConsoleAPIType::kWarning,
        "Timer '" + protocolTitle + "' does not exist");
    return;
  }
  inspector->client()->consoleTimeEnd(toStringView(protocolTitle));
  String16 title = protocolTitle + "@" +
                   consoleContextToString(inspector->isolate(), consoleContext);
  double elapsed;
  if (timeLog) {
    elapsed =
        helper.consoleMessageStorage()->timeLog(helper.contextId(), title);
  } else {
    elapsed =
        helper.consoleMessageStorage()->timeEnd(helper.contextId(), title);
  }
  String16 message =
      protocolTitle + ": " + String16::fromDouble(elapsed) + " ms";
  if (timeLog)
    helper.reportCallAndReplaceFirstArgument(ConsoleAPIType::kLog, message);
  else
    helper.reportCallWithArgument(ConsoleAPIType::kTimeEnd, message);
}

void V8Console::Time(const v8::debug::ConsoleCallArguments& info,
                     const v8::debug::ConsoleContext& consoleContext) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.inspector"), "V8Console::Time");
  timeFunction(info, consoleContext, false, m_inspector);
}

void V8Console::TimeLog(const v8::debug::ConsoleCallArguments& info,
                        const v8::debug::ConsoleContext& consoleContext) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.inspector"), "V8Console::TimeLog");
  timeEndFunction(info, consoleContext, true, m_inspector);
}

void V8Console::TimeEnd(const v8::debug::ConsoleCallArguments& info,
                        const v8::debug::ConsoleContext& consoleContext) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.inspector"), "V8Console::TimeEnd");
  timeEndFunction(info, consoleContext, false, m_inspector);
}

void V8Console::TimeStamp(const v8::debug::ConsoleCallArguments& info,
                          const v8::debug::ConsoleContext& consoleContext) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.inspector"),
               "V8Console::TimeStamp");
  ConsoleHelper helper(info, consoleContext, m_inspector);
  String16 title = helper.firstArgToString(String16());
  m_inspector->client()->consoleTimeStamp(toStringView(title));
}

void V8Console::memoryGetterCallback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  v8::Local<v8::Value> memoryValue;
  if (!m_inspector->client()
           ->memoryInfo(info.GetIsolate(),
                        info.GetIsolate()->GetCurrentContext())
           .ToLocal(&memoryValue))
    return;
  info.GetReturnValue().Set(memoryValue);
}

void V8Console::memorySetterCallback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  // We can't make the attribute readonly as it breaks existing code that relies
  // on being able to assign to console.memory in strict mode. Instead, the
  // setter just ignores the passed value.  http://crbug.com/468611
}

void V8Console::createTask(const v8::FunctionCallbackInfo<v8::Value>& info) {
  v8::Isolate* isolate = info.GetIsolate();

  v8::debug::RecordAsyncStackTaggingCreateTaskCall(isolate);

  if (info.Length() < 1 || !info[0]->IsString() ||
      !info[0].As<v8::String>()->Length()) {
    isolate->ThrowError("First argument must be a non-empty string.");
    return;
  }

  v8::Local<v8::Object> task = taskTemplate()
                                   ->NewInstance(isolate->GetCurrentContext())
                                   .ToLocalChecked();

  auto taskInfo = std::make_unique<TaskInfo>(isolate, this, task);
  void* taskId = taskInfo->Id();
  auto [iter, inserted] = m_tasks.emplace(taskId, std::move(taskInfo));
  CHECK(inserted);

  String16 nameArgument = toProtocolString(isolate, info[0].As<v8::String>());
  StringView taskName =
      StringView(nameArgument.characters16(), nameArgument.length());
  m_inspector->asyncTaskScheduled(taskName, taskId, /* recurring */ true);

  info.GetReturnValue().Set(task);
}

void V8Console::runTask(const v8::FunctionCallbackInfo<v8::Value>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  if (info.Length() < 1 || !info[0]->IsFunction()) {
    isolate->ThrowError("First argument must be a function.");
    return;
  }
  v8::Local<v8::Function> function = info[0].As<v8::Function>();

  v8::Local<v8::Object> task = info.This();
  v8::Local<v8::Value> maybeTaskExternal;
  if (!task->GetPrivate(isolate->GetCurrentContext(), taskInfoKey())
           .ToLocal(&maybeTaskExternal)) {
    // An exception is already thrown.
    return;
  }

  if (!maybeTaskExternal->IsExternal()) {
    isolate->ThrowError("'run' called with illegal receiver.");
    return;
  }

  v8::Local<v8::External> taskExternal = maybeTaskExternal.As<v8::External>();
  TaskInfo* taskInfo = reinterpret_cast<TaskInfo*>(taskExternal->Value());

  m_inspector->asyncTaskStarted(taskInfo->Id());
  v8::Local<v8::Value> result;
  if (function
          ->Call(isolate->GetCurrentContext(), v8::Undefined(isolate), 0, {})
          .ToLocal(&result)) {
    info.GetReturnValue().Set(result);
  }
  m_inspector->asyncTaskFinished(taskInfo->Id());
}

v8::Local<v8::Private> V8Console::taskInfoKey() {
  v8::Isolate* isolate = m_inspector->isolate();
  if (m_taskInfoKey.IsEmpty()) {
    m_taskInfoKey.Reset(isolate, v8::Private::New(isolate));
  }
  return m_taskInfoKey.Get(isolate);
}

v8::Local<v8::ObjectTemplate> V8Console::taskTemplate() {
  v8::Isolate* isolate = m_inspector->isolate();
  if (!m_taskTemplate.IsEmpty()) {
    return m_taskTemplate.Get(isolate);
  }

  v8::Local<v8::External> data = v8::External::New(isolate, this);
  v8::Local<v8::ObjectTemplate> taskTemplate = v8::ObjectTemplate::New(isolate);
  v8::Local<v8::FunctionTemplate> funcTemplate = v8::FunctionTemplate::New(
      isolate, &V8Console::call<&V8Console::runTask>, data);
  taskTemplate->Set(isolate, "run", funcTemplate);

  m_taskTemplate.Reset(isolate, taskTemplate);
  return taskTemplate;
}

void V8Console::cancelConsoleTask(TaskInfo* taskInfo) {
  m_inspector->asyncTaskCanceled(taskInfo->Id());
  m_tasks.erase(taskInfo->Id());
}

namespace {

void cleanupTaskInfo(const v8::WeakCallbackInfo<TaskInfo>& info) {
  TaskInfo* task = info.GetParameter();
  CHECK(task);
  task->Cancel();
}

}  // namespace

TaskInfo::TaskInfo(v8::Isolate* isolate, V8Console* console,
                   v8::Local<v8::Object> task)
    : m_task(isolate, task), m_console(console) {
  task->SetPrivate(isolate->GetCurrentContext(), console->taskInfoKey(),
                   v8::External::New(isolate, this))
      .Check();
  m_task.SetWeak(this, cleanupTaskInfo, v8::WeakCallbackType::kParameter);
}

void V8Console::keysCallback(const v8::FunctionCallbackInfo<v8::Value>& info,
                             int sessionId) {
  v8::Isolate* isolate = info.GetIsolate();
  info.GetReturnValue().Set(v8::Array::New(isolate));

  v8::debug::ConsoleCallArguments args(info);
  ConsoleHelper helper(args, v8::debug::ConsoleContext(), m_inspector);
  v8::Local<v8::Object> obj;
  if (!helper.firstArgAsObject().ToLocal(&obj)) return;
  v8::Local<v8::Array> names;
  if (!obj->GetOwnPropertyNames(isolate->GetCurrentContext()).ToLocal(&names))
    return;
  info.GetReturnValue().Set(names);
}

void V8Console::valuesCallback(const v8::FunctionCallbackInfo<v8::Value>& info,
                               int sessionId) {
  v8::Isolate* isolate = info.GetIsolate();
  info.GetReturnValue().Set(v8::Array::New(isolate));

  v8::debug::ConsoleCallArguments args(info);
  ConsoleHelper helper(args, v8::debug::ConsoleContext(), m_inspector);
  v8::Local<v8::Object> obj;
  if (!helper.firstArgAsObject().ToLocal(&obj)) return;
  v8::Local<v8::Array> names;
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  if (!obj->GetOwnPropertyNames(context).ToLocal(&names)) return;
  v8::Local<v8::Array> values = v8::Array::New(isolate, names->Length());
  for (uint32_t i = 0; i < names->Length(); ++i) {
    v8::Local<v8::Value> key;
    if (!names->Get(context, i).ToLocal(&key)) continue;
    v8::Local<v8::Value> value;
    if (!obj->Get(context, key).ToLocal(&value)) continue;
    createDataProperty(context, values, i, value);
  }
  info.GetReturnValue().Set(values);
}

static void setFunctionBreakpoint(ConsoleHelper& helper, int sessionId,
                                  v8::Local<v8::Function> function,
                                  V8DebuggerAgentImpl::BreakpointSource source,
                                  v8::Local<v8::String> condition,
                                  bool enable) {
  V8InspectorSessionImpl* session = helper.session(sessionId);
  if (session == nullptr) return;
  if (!session->debuggerAgent()->enabled()) return;
  if (enable) {
    session->debuggerAgent()->setBreakpointFor(function, condition, source);
  } else {
    session->debuggerAgent()->removeBreakpointFor(function, source);
  }
}

void V8Console::debugFunctionCallback(
    const v8::FunctionCallbackInfo<v8::Value>& info, int sessionId) {
  v8::debug::ConsoleCallArguments args(info);
  ConsoleHelper helper(args, v8::debug::ConsoleContext(), m_inspector);
  v8::Local<v8::Function> function;
  v8::Local<v8::String> condition;
  if (!helper.firstArgAsFunction().ToLocal(&function)) return;
  if (args.Length() > 1 && args[1]->IsString()) {
    condition = args[1].As<v8::String>();
  }
  setFunctionBreakpoint(helper, sessionId, function,
                        V8DebuggerAgentImpl::DebugCommandBreakpointSource,
                        condition, true);
}

void V8Console::undebugFunctionCallback(
    const v8::FunctionCallbackInfo<v8::Value>& info, int sessionId) {
  v8::debug::ConsoleCallArguments args(info);
  ConsoleHelper helper(args, v8::debug::ConsoleContext(), m_inspector);
  v8::Local<v8::Function> function;
  if (!helper.firstArgAsFunction().ToLocal(&function)) return;
  setFunctionBreakpoint(helper, sessionId, function,
                        V8DebuggerAgentImpl::DebugCommandBreakpointSource,
                        v8::Local<v8::String>(), false);
}

void V8Console::monitorFunctionCallback(
    const v8::FunctionCallbackInfo<v8::Value>& info, int sessionId) {
  v8::debug::ConsoleCallArguments args(info);
  ConsoleHelper helper(args, v8::debug::ConsoleContext(), m_inspector);
  v8::Local<v8::Function> function;
  if (!helper.firstArgAsFunction().ToLocal(&function)) return;
  v8::Local<v8::Value> name = function->GetName();
  if (!name->IsString() || !name.As<v8::String>()->Length())
    name = function->GetInferredName();
  String16 functionName =
      toProtocolStringWithTypeCheck(info.GetIsolate(), name);
  String16Builder builder;
  builder.append("console.log(\"function ");
  if (functionName.isEmpty())
    builder.append("(anonymous function)");
  else
    builder.append(functionName);
  builder.append(
      " called\" + (typeof arguments !== \"undefined\" && arguments.length > 0 "
      "? \" with arguments: \" + Array.prototype.join.call(arguments, \", \") "
      ": \"\")) && false");
  setFunctionBreakpoint(helper, sessionId, function,
                        V8DebuggerAgentImpl::MonitorCommandBreakpointSource,
                        toV8String(info.GetIsolate(), builder.toString()),
                        true);
}

void V8Console::unmonitorFunctionCallback(
    const v8::FunctionCallbackInfo<v8::Value>& info, int sessionId) {
  v8::debug::ConsoleCallArguments args(info);
  ConsoleHelper helper(args, v8::debug::ConsoleContext(), m_inspector);
  v8::Local<v8::Function> function;
  if (!helper.firstArgAsFunction().ToLocal(&function)) return;
  setFunctionBreakpoint(helper, sessionId, function,
                        V8DebuggerAgentImpl::MonitorCommandBreakpointSource,
                        v8::Local<v8::String>(), false);
}

void V8Console::lastEvaluationResultCallback(
    const v8::FunctionCallbackInfo<v8::Value>& info, int sessionId) {
  v8::debug::ConsoleCallArguments args(info);
  ConsoleHelper helper(args, v8::debug::ConsoleContext(), m_inspector);
  InjectedScript* injectedScript = helper.injectedScript(sessionId);
  if (!injectedScript) return;
  info.GetReturnValue().Set(injectedScript->lastEvaluationResult());
}

static void inspectImpl(const v8::FunctionCallbackInfo<v8::Value>& info,
                        v8::Local<v8::Value> value, int sessionId,
                        InspectRequest request, V8InspectorImpl* inspector) {
  if (request == kRegular) info.GetReturnValue().Set(value);

  v8::debug::ConsoleCallArguments args(info);
  ConsoleHelper helper(args, v8::debug::ConsoleContext(), inspector);
  InjectedScript* injectedScript = helper.injectedScript(sessionId);
  if (!injectedScript) return;
  std::unique_ptr<protocol::Runtime::RemoteObject> wrappedObject;
  protocol::Response response = injectedScript->wrapObject(
      value, "", WrapOptions({WrapMode::kIdOnly}), &wrappedObject);
  if (!response.IsSuccess()) return;

  std::unique_ptr<protocol::DictionaryValue> hints =
      protocol::DictionaryValue::create();
  if (request == kCopyToClipboard) {
    hints->setBoolean("copyToClipboard", true);
  } else if (request == kQueryObjects) {
    hints->setBoolean("queryObjects", true);
  }
  if (V8InspectorSessionImpl* session = helper.session(sessionId)) {
    session->runtimeAgent()->inspect(std::move(wrappedObject), std::move(hints),
                                     helper.contextId());
  }
}

void V8Console::inspectCallback(const v8::FunctionCallbackInfo<v8::Value>& info,
                                int sessionId) {
  if (info.Length() < 1) return;
  inspectImpl(info, info[0], sessionId, kRegular, m_inspector);
}

void V8Console::copyCallback(const v8::FunctionCallbackInfo<v8::Value>& info,
                             int sessionId) {
  if (info.Length() < 1) return;
  inspectImpl(info, info[0], sessionId, kCopyToClipboard, m_inspector);
}

void V8Console::queryObjectsCallback(
    const v8::FunctionCallbackInfo<v8::Value>& info, int sessionId) {
  if (info.Length() < 1) return;
  v8::Local<v8::Value> arg = info[0];
  if (arg->IsFunction()) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::TryCatch tryCatch(isolate);
    v8::Local<v8::Value> prototype;
    if (arg.As<v8::Function>()
            ->Get(isolate->GetCurrentContext(),
                  toV8StringInternalized(isolate, "prototype"))
            .ToLocal(&prototype) &&
        prototype->IsObject()) {
      arg = prototype;
    }
    if (tryCatch.HasCaught()) {
      tryCatch.ReThrow();
      return;
    }
  }
  inspectImpl(info, arg, sessionId, kQueryObjects, m_inspector);
}

void V8Console::inspectedObject(const v8::FunctionCallbackInfo<v8::Value>& info,
                                int sessionId, unsigned num) {
  DCHECK_GT(V8InspectorSessionImpl::kInspectedObjectBufferSize, num);
  v8::debug::ConsoleCallArguments args(info);
  ConsoleHelper helper(args, v8::debug::ConsoleContext(), m_inspector);
  if (V8InspectorSessionImpl* session = helper.session(sessionId)) {
    V8InspectorSession::Inspectable* object = session->inspectedObject(num);
    v8::Isolate* isolate = info.GetIsolate();
    if (object)
      info.GetReturnValue().Set(object->get(isolate->GetCurrentContext()));
    else
      info.GetReturnValue().Set(v8::Undefined(isolate));
  }
}

void V8Console::installMemoryGetter(v8::Local<v8::Context> context,
                                    v8::Local<v8::Object> console) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::Local<v8::External> data = v8::External::New(isolate, this);
  console->SetAccessorProperty(
      toV8StringInternalized(isolate, "memory"),
      v8::Function::New(
          context, &V8Console::call<&V8Console::memoryGetterCallback>, data, 0,
          v8::ConstructorBehavior::kThrow, v8::SideEffectType::kHasNoSideEffect)
          .ToLocalChecked(),
      v8::Function::New(context,
                        &V8Console::call<&V8Console::memorySetterCallback>,
                        data, 0, v8::ConstructorBehavior::kThrow)
          .ToLocalChecked(),
      static_cast<v8::PropertyAttribute>(v8::None));
}

void V8Console::installAsyncStackTaggingAPI(v8::Local<v8::Context> context,
                                            v8::Local<v8::Object> console) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::Local<v8::External> data = v8::External::New(isolate, this);

  v8::MicrotasksScope microtasksScope(context,
                                      v8::MicrotasksScope::kDoNotRunMicrotasks);

  createBoundFunctionProperty(context, console, data, "createTask",
                              &V8Console::call<&V8Console::createTask>);
}

v8::Local<v8::Object> V8Console::createCommandLineAPI(
    v8::Local<v8::Context> context, int sessionId) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::MicrotasksScope microtasksScope(context,
                                      v8::MicrotasksScope::kDoNotRunMicrotasks);

  v8::Local<v8::Object> commandLineAPI = v8::Object::New(isolate);
  bool success =
      commandLineAPI->SetPrototype(context, v8::Null(isolate)).FromMaybe(false);
  DCHECK(success);
  USE(success);

  v8::Local<v8::ArrayBuffer> data =
      v8::ArrayBuffer::New(isolate, sizeof(CommandLineAPIData));
  *static_cast<CommandLineAPIData*>(data->GetBackingStore()->Data()) =
      CommandLineAPIData(this, sessionId);
  createBoundFunctionProperty(context, commandLineAPI, data, "dir",
                              &V8Console::call<&V8Console::Dir>);
  createBoundFunctionProperty(context, commandLineAPI, data, "dirxml",
                              &V8Console::call<&V8Console::DirXml>);
  createBoundFunctionProperty(context, commandLineAPI, data, "profile",
                              &V8Console::call<&V8Console::Profile>);
  createBoundFunctionProperty(context, commandLineAPI, data, "profileEnd",
                              &V8Console::call<&V8Console::ProfileEnd>);
  createBoundFunctionProperty(context, commandLineAPI, data, "clear",
                              &V8Console::call<&V8Console::Clear>);
  createBoundFunctionProperty(context, commandLineAPI, data, "table",
                              &V8Console::call<&V8Console::Table>);

  createBoundFunctionProperty(context, commandLineAPI, data, "keys",
                              &V8Console::call<&V8Console::keysCallback>,
                              v8::SideEffectType::kHasNoSideEffect);
  createBoundFunctionProperty(context, commandLineAPI, data, "values",
                              &V8Console::call<&V8Console::valuesCallback>,
                              v8::SideEffectType::kHasNoSideEffect);
  createBoundFunctionProperty(
      context, commandLineAPI, data, "debug",
      &V8Console::call<&V8Console::debugFunctionCallback>);
  createBoundFunctionProperty(
      context, commandLineAPI, data, "undebug",
      &V8Console::call<&V8Console::undebugFunctionCallback>);
  createBoundFunctionProperty(
      context, commandLineAPI, data, "monitor",
      &V8Console::call<&V8Console::monitorFunctionCallback>);
  createBoundFunctionProperty(
      context, commandLineAPI, data, "unmonitor",
      &V8Console::call<&V8Console::unmonitorFunctionCallback>);
  createBoundFunctionProperty(context, commandLineAPI, data, "inspect",
                              &V8Console::call<&V8Console::inspectCallback>);
  createBoundFunctionProperty(context, commandLineAPI, data, "copy",
                              &V8Console::call<&V8Console::copyCallback>);
  createBoundFunctionProperty(
      context, commandLineAPI, data, "queryObjects",
      &V8Console::call<&V8Console::queryObjectsCallback>);
  createBoundFunctionProperty(
      context, commandLineAPI, data, "$_",
      &V8Console::call<&V8Console::lastEvaluationResultCallback>,
      v8::SideEffectType::kHasNoSideEffect);
  createBoundFunctionProperty(context, commandLineAPI, data, "$0",
                              &V8Console::call<&V8Console::inspectedObject0>,
                              v8::SideEffectType::kHasNoSideEffect);
  createBoundFunctionProperty(context, commandLineAPI, data, "$1",
                              &V8Console::call<&V8Console::inspectedObject1>,
                              v8::SideEffectType::kHasNoSideEffect);
  createBoundFunctionProperty(context, commandLineAPI, data, "$2",
                              &V8Console::call<&V8Console::inspectedObject2>,
                              v8::SideEffectType::kHasNoSideEffect);
  createBoundFunctionProperty(context, commandLineAPI, data, "$3",
                              &V8Console::call<&V8Console::inspectedObject3>,
                              v8::SideEffectType::kHasNoSideEffect);
  createBoundFunctionProperty(context, commandLineAPI, data, "$4",
                              &V8Console::call<&V8Console::inspectedObject4>,
                              v8::SideEffectType::kHasNoSideEffect);

  m_inspector->client()->installAdditionalCommandLineAPI(context,
                                                         commandLineAPI);
  return commandLineAPI;
}

static bool isCommandLineAPIGetter(const String16& name) {
  if (name.length() != 2) return false;
  // $0 ... $4, $_
  return name[0] == '$' &&
         ((name[1] >= '0' && name[1] <= '4') || name[1] == '_');
}

void V8Console::CommandLineAPIScope::accessorGetterCallback(
    v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  CommandLineAPIScope* scope = *static_cast<CommandLineAPIScope**>(
      info.Data().As<v8::ArrayBuffer>()->GetBackingStore()->Data());
  v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();
  if (scope == nullptr) {
    USE(info.Holder()->Delete(context, name).FromMaybe(false));
    return;
  }

  v8::Local<v8::Value> value;
  if (!scope->commandLineAPI()->Get(context, name).ToLocal(&value)) return;
  if (isCommandLineAPIGetter(
          toProtocolStringWithTypeCheck(info.GetIsolate(), name))) {
    DCHECK(value->IsFunction());
    v8::MicrotasksScope microtasks(context,
                                   v8::MicrotasksScope::kDoNotRunMicrotasks);
    if (value.As<v8::Function>()
            ->Call(context, scope->commandLineAPI(), 0, nullptr)
            .ToLocal(&value))
      info.GetReturnValue().Set(value);
  } else {
    info.GetReturnValue().Set(value);
  }
}

void V8Console::CommandLineAPIScope::accessorSetterCallback(
    v8::Local<v8::Name> name, v8::Local<v8::Value> value,
    const v8::PropertyCallbackInfo<void>& info) {
  CommandLineAPIScope* scope = *static_cast<CommandLineAPIScope**>(
      info.Data().As<v8::ArrayBuffer>()->GetBackingStore()->Data());
  if (scope == nullptr) return;
  v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();
  if (!info.Holder()->Delete(context, name).FromMaybe(false)) return;
  if (!info.Holder()->CreateDataProperty(context, name, value).FromMaybe(false))
    return;

  v8::Local<v8::PrimitiveArray> methods = scope->installedMethods();
  for (int i = 0; i < methods->Length(); ++i) {
    v8::Local<v8::Value> methodName = methods->Get(scope->m_isolate, i);
    if (methodName.IsEmpty() || !methodName->IsName()) continue;
    if (!name->StrictEquals(methodName)) continue;
    methods->Set(scope->m_isolate, i, v8::Undefined(scope->m_isolate));
    break;
  }
}

V8Console::CommandLineAPIScope::CommandLineAPIScope(
    v8::Local<v8::Context> context, v8::Local<v8::Object> commandLineAPI,
    v8::Local<v8::Object> global)
    : m_isolate(context->GetIsolate()),
      m_context(m_isolate, context),
      m_commandLineAPI(m_isolate, commandLineAPI),
      m_global(m_isolate, global) {
  v8::MicrotasksScope microtasksScope(context,
                                      v8::MicrotasksScope::kDoNotRunMicrotasks);
  v8::Local<v8::Array> names;
  if (!commandLineAPI->GetOwnPropertyNames(context).ToLocal(&names)) return;
  m_installedMethods.Reset(m_isolate,
                           v8::PrimitiveArray::New(m_isolate, names->Length()));

  m_thisReference = v8::Global<v8::ArrayBuffer>(
      m_isolate, v8::ArrayBuffer::New(context->GetIsolate(),
                                      sizeof(CommandLineAPIScope*)));
  *static_cast<CommandLineAPIScope**>(
      thisReference()->GetBackingStore()->Data()) = this;
  v8::Local<v8::PrimitiveArray> methods = installedMethods();
  for (uint32_t i = 0; i < names->Length(); ++i) {
    v8::Local<v8::Value> name;
    if (!names->Get(context, i).ToLocal(&name) || !name->IsName()) continue;
    if (global->Has(context, name).FromMaybe(true)) continue;

    const v8::SideEffectType get_accessor_side_effect_type =
        isCommandLineAPIGetter(
            toProtocolStringWithTypeCheck(context->GetIsolate(), name))
            ? v8::SideEffectType::kHasNoSideEffect
            : v8::SideEffectType::kHasSideEffect;
    if (!global
             ->SetNativeDataProperty(
                 context, name.As<v8::Name>(),
                 CommandLineAPIScope::accessorGetterCallback,
                 CommandLineAPIScope::accessorSetterCallback, thisReference(),
                 v8::DontEnum, get_accessor_side_effect_type)
             .FromMaybe(false)) {
      continue;
    }
    methods->Set(m_isolate, i, name.As<v8::Name>());
  }
}

V8Console::CommandLineAPIScope::~CommandLineAPIScope() {
  if (m_isolate->IsExecutionTerminating()) return;
  v8::MicrotasksScope microtasksScope(context(),
                                      v8::MicrotasksScope::kDoNotRunMicrotasks);
  *static_cast<CommandLineAPIScope**>(
      thisReference()->GetBackingStore()->Data()) = nullptr;
  v8::Local<v8::PrimitiveArray> names = installedMethods();
  for (int i = 0; i < names->Length(); ++i) {
    v8::Local<v8::Value> name = names->Get(m_isolate, i);
    if (name.IsEmpty() || !name->IsName()) continue;
    if (name->IsString()) {
      v8::Local<v8::Value> descriptor;
      bool success =
          global()
              ->GetOwnPropertyDescriptor(context(), name.As<v8::String>())
              .ToLocal(&descriptor);
      USE(success);
    }
  }
}

}  // namespace v8_inspector
