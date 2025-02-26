// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/inspector/v8-console-message.h"

#include "include/v8-container.h"
#include "include/v8-context.h"
#include "include/v8-inspector.h"
#include "include/v8-microtask-queue.h"
#include "include/v8-primitive-object.h"
#include "src/debug/debug-interface.h"
#include "src/inspector/inspected-context.h"
#include "src/inspector/protocol/Protocol.h"
#include "src/inspector/string-util.h"
#include "src/inspector/v8-console-agent-impl.h"
#include "src/inspector/v8-inspector-impl.h"
#include "src/inspector/v8-inspector-session-impl.h"
#include "src/inspector/v8-runtime-agent-impl.h"
#include "src/inspector/v8-stack-trace-impl.h"
#include "src/inspector/value-mirror.h"
#include "src/tracing/trace-event.h"

namespace v8_inspector {

namespace {

String16 consoleAPITypeValue(ConsoleAPIType type) {
  switch (type) {
    case ConsoleAPIType::kLog:
      return protocol::Runtime::ConsoleAPICalled::TypeEnum::Log;
    case ConsoleAPIType::kDebug:
      return protocol::Runtime::ConsoleAPICalled::TypeEnum::Debug;
    case ConsoleAPIType::kInfo:
      return protocol::Runtime::ConsoleAPICalled::TypeEnum::Info;
    case ConsoleAPIType::kError:
      return protocol::Runtime::ConsoleAPICalled::TypeEnum::Error;
    case ConsoleAPIType::kWarning:
      return protocol::Runtime::ConsoleAPICalled::TypeEnum::Warning;
    case ConsoleAPIType::kClear:
      return protocol::Runtime::ConsoleAPICalled::TypeEnum::Clear;
    case ConsoleAPIType::kDir:
      return protocol::Runtime::ConsoleAPICalled::TypeEnum::Dir;
    case ConsoleAPIType::kDirXML:
      return protocol::Runtime::ConsoleAPICalled::TypeEnum::Dirxml;
    case ConsoleAPIType::kTable:
      return protocol::Runtime::ConsoleAPICalled::TypeEnum::Table;
    case ConsoleAPIType::kTrace:
      return protocol::Runtime::ConsoleAPICalled::TypeEnum::Trace;
    case ConsoleAPIType::kStartGroup:
      return protocol::Runtime::ConsoleAPICalled::TypeEnum::StartGroup;
    case ConsoleAPIType::kStartGroupCollapsed:
      return protocol::Runtime::ConsoleAPICalled::TypeEnum::StartGroupCollapsed;
    case ConsoleAPIType::kEndGroup:
      return protocol::Runtime::ConsoleAPICalled::TypeEnum::EndGroup;
    case ConsoleAPIType::kAssert:
      return protocol::Runtime::ConsoleAPICalled::TypeEnum::Assert;
    case ConsoleAPIType::kTimeEnd:
      return protocol::Runtime::ConsoleAPICalled::TypeEnum::TimeEnd;
    case ConsoleAPIType::kCount:
      return protocol::Runtime::ConsoleAPICalled::TypeEnum::Count;
  }
  return protocol::Runtime::ConsoleAPICalled::TypeEnum::Log;
}

const char kGlobalConsoleMessageHandleLabel[] = "DevTools console";
const unsigned maxConsoleMessageCount = 1000;
const int maxConsoleMessageV8Size = 10 * 1024 * 1024;
const unsigned maxArrayItemsLimit = 10000;
const unsigned maxStackDepthLimit = 32;

class V8ValueStringBuilder {
 public:
  static String16 toString(v8::Local<v8::Value> value,
                           v8::Local<v8::Context> context) {
    V8ValueStringBuilder builder(context);
    if (!builder.append(value)) return String16();
    return builder.toString();
  }

 private:
  enum {
    IgnoreNull = 1 << 0,
    IgnoreUndefined = 1 << 1,
  };

  explicit V8ValueStringBuilder(v8::Local<v8::Context> context)
      : m_arrayLimit(maxArrayItemsLimit),
        m_isolate(context->GetIsolate()),
        m_visitedArrays(context->GetIsolate()),
        m_tryCatch(context->GetIsolate()),
        m_context(context) {}

  bool append(v8::Local<v8::Value> value, unsigned ignoreOptions = 0) {
    if (value.IsEmpty()) return true;
    if ((ignoreOptions & IgnoreNull) && value->IsNull()) return true;
    if ((ignoreOptions & IgnoreUndefined) && value->IsUndefined()) return true;
    if (value->IsBigIntObject()) {
      value = value.As<v8::BigIntObject>()->ValueOf();
    } else if (value->IsBooleanObject()) {
      value =
          v8::Boolean::New(m_isolate, value.As<v8::BooleanObject>()->ValueOf());
    } else if (value->IsNumberObject()) {
      value =
          v8::Number::New(m_isolate, value.As<v8::NumberObject>()->ValueOf());
    } else if (value->IsStringObject()) {
      value = value.As<v8::StringObject>()->ValueOf();
    } else if (value->IsSymbolObject()) {
      value = value.As<v8::SymbolObject>()->ValueOf();
    }
    if (value->IsString()) return append(value.As<v8::String>());
    if (value->IsBigInt()) return append(value.As<v8::BigInt>());
    if (value->IsSymbol()) return append(value.As<v8::Symbol>());
    if (value->IsArray()) return append(value.As<v8::Array>());
    if (value->IsProxy()) {
      m_builder.append("[object Proxy]");
      return true;
    }
    if (value->IsObject() && !value->IsDate() && !value->IsFunction() &&
        !value->IsNativeError() && !value->IsRegExp()) {
      v8::Local<v8::Object> object = value.As<v8::Object>();
      v8::Local<v8::String> stringValue;
      if (object->ObjectProtoToString(m_context).ToLocal(&stringValue))
        return append(stringValue);
    }
    v8::Local<v8::String> stringValue;
    if (!value->ToString(m_context).ToLocal(&stringValue)) return false;
    return append(stringValue);
  }

  bool append(v8::Local<v8::Array> array) {
    for (const auto& it : m_visitedArrays) {
      if (it == array) return true;
    }
    uint32_t length = array->Length();
    if (length > m_arrayLimit) return false;
    if (m_visitedArrays.size() > maxStackDepthLimit) return false;

    bool result = true;
    m_arrayLimit -= length;
    m_visitedArrays.push_back(array);
    for (uint32_t i = 0; i < length; ++i) {
      if (i) m_builder.append(',');
      v8::Local<v8::Value> value;
      if (!array->Get(m_context, i).ToLocal(&value)) continue;
      if (!append(value, IgnoreNull | IgnoreUndefined)) {
        result = false;
        break;
      }
    }
    m_visitedArrays.pop_back();
    return result;
  }

  bool append(v8::Local<v8::Symbol> symbol) {
    m_builder.append("Symbol(");
    bool result = append(symbol->Description(m_isolate), IgnoreUndefined);
    m_builder.append(')');
    return result;
  }

  bool append(v8::Local<v8::BigInt> bigint) {
    v8::Local<v8::String> bigint_string;
    if (!bigint->ToString(m_context).ToLocal(&bigint_string)) return false;
    bool result = append(bigint_string);
    if (m_tryCatch.HasCaught()) return false;
    m_builder.append('n');
    return result;
  }

  bool append(v8::Local<v8::String> string) {
    if (m_tryCatch.HasCaught()) return false;
    if (!string.IsEmpty()) {
      m_builder.append(toProtocolString(m_isolate, string));
    }
    return true;
  }

  String16 toString() {
    if (m_tryCatch.HasCaught()) return String16();
    return m_builder.toString();
  }

  uint32_t m_arrayLimit;
  v8::Isolate* m_isolate;
  String16Builder m_builder;
  v8::LocalVector<v8::Array> m_visitedArrays;
  v8::TryCatch m_tryCatch;
  v8::Local<v8::Context> m_context;
};

}  // namespace

V8ConsoleMessage::V8ConsoleMessage(V8MessageOrigin origin, double timestamp,
                                   const String16& message)
    : m_origin(origin),
      m_timestamp(timestamp),
      m_message(message),
      m_lineNumber(0),
      m_columnNumber(0),
      m_scriptId(0),
      m_contextId(0),
      m_type(ConsoleAPIType::kLog),
      m_exceptionId(0),
      m_revokedExceptionId(0) {}

V8ConsoleMessage::~V8ConsoleMessage() = default;

void V8ConsoleMessage::setLocation(const String16& url, unsigned lineNumber,
                                   unsigned columnNumber,
                                   std::unique_ptr<V8StackTraceImpl> stackTrace,
                                   int scriptId) {
  const char* dataURIPrefix = "data:";
  if (url.substring(0, strlen(dataURIPrefix)) == dataURIPrefix) {
    m_url = String16();
  } else {
    m_url = url;
  }
  m_lineNumber = lineNumber;
  m_columnNumber = columnNumber;
  m_stackTrace = std::move(stackTrace);
  m_scriptId = scriptId;
}

void V8ConsoleMessage::reportToFrontend(
    protocol::Console::Frontend* frontend) const {
  DCHECK_EQ(V8MessageOrigin::kConsole, m_origin);
  String16 level = protocol::Console::ConsoleMessage::LevelEnum::Log;
  if (m_type == ConsoleAPIType::kDebug || m_type == ConsoleAPIType::kCount ||
      m_type == ConsoleAPIType::kTimeEnd)
    level = protocol::Console::ConsoleMessage::LevelEnum::Debug;
  else if (m_type == ConsoleAPIType::kError ||
           m_type == ConsoleAPIType::kAssert)
    level = protocol::Console::ConsoleMessage::LevelEnum::Error;
  else if (m_type == ConsoleAPIType::kWarning)
    level = protocol::Console::ConsoleMessage::LevelEnum::Warning;
  else if (m_type == ConsoleAPIType::kInfo)
    level = protocol::Console::ConsoleMessage::LevelEnum::Info;
  std::unique_ptr<protocol::Console::ConsoleMessage> result =
      protocol::Console::ConsoleMessage::create()
          .setSource(protocol::Console::ConsoleMessage::SourceEnum::ConsoleApi)
          .setLevel(level)
          .setText(m_message)
          .build();
  if (m_lineNumber) result->setLine(m_lineNumber);
  if (m_columnNumber) result->setColumn(m_columnNumber);
  if (!m_url.isEmpty()) result->setUrl(m_url);
  frontend->messageAdded(std::move(result));
}

std::unique_ptr<protocol::Array<protocol::Runtime::RemoteObject>>
V8ConsoleMessage::wrapArguments(V8InspectorSessionImpl* session,
                                bool generatePreview) const {
  V8InspectorImpl* inspector = session->inspector();
  int contextGroupId = session->contextGroupId();
  int contextId = m_contextId;
  if (m_arguments.empty() || !contextId) return nullptr;
  InspectedContext* inspectedContext =
      inspector->getContext(contextGroupId, contextId);
  if (!inspectedContext) return nullptr;

  v8::Isolate* isolate = inspectedContext->isolate();
  v8::HandleScope handles(isolate);
  v8::Local<v8::Context> context = inspectedContext->context();

  auto args =
      std::make_unique<protocol::Array<protocol::Runtime::RemoteObject>>();

  v8::Local<v8::Value> value = m_arguments[0]->Get(isolate);
  if (value->IsObject() && m_type == ConsoleAPIType::kTable &&
      generatePreview) {
    v8::MaybeLocal<v8::Array> columns;
    if (m_arguments.size() > 1) {
      v8::Local<v8::Value> secondArgument = m_arguments[1]->Get(isolate);
      if (secondArgument->IsArray()) {
        columns = secondArgument.As<v8::Array>();
      } else if (secondArgument->IsString()) {
        v8::TryCatch tryCatch(isolate);
        v8::Local<v8::Array> array = v8::Array::New(isolate);
        if (array->Set(context, 0, secondArgument).IsJust()) {
          columns = array;
        }
      }
    }
    std::unique_ptr<protocol::Runtime::RemoteObject> wrapped =
        session->wrapTable(context, value.As<v8::Object>(), columns);
    inspectedContext = inspector->getContext(contextGroupId, contextId);
    if (!inspectedContext) return nullptr;
    if (wrapped) {
      args->emplace_back(std::move(wrapped));
    } else {
      args = nullptr;
    }
  } else {
    for (size_t i = 0; i < m_arguments.size(); ++i) {
      std::unique_ptr<protocol::Runtime::RemoteObject> wrapped =
          session->wrapObject(context, m_arguments[i]->Get(isolate), "console",
                              generatePreview);
      inspectedContext = inspector->getContext(contextGroupId, contextId);
      if (!inspectedContext) return nullptr;
      if (!wrapped) {
        args = nullptr;
        break;
      }
      args->emplace_back(std::move(wrapped));
    }
  }
  return args;
}

void V8ConsoleMessage::reportToFrontend(protocol::Runtime::Frontend* frontend,
                                        V8InspectorSessionImpl* session,
                                        bool generatePreview) const {
  int contextGroupId = session->contextGroupId();
  V8InspectorImpl* inspector = session->inspector();
  // Protect against reentrant debugger calls via interrupts.
  v8::debug::PostponeInterruptsScope no_interrupts(inspector->isolate());

  if (m_origin == V8MessageOrigin::kException) {
    std::unique_ptr<protocol::Runtime::RemoteObject> exception =
        wrapException(session, generatePreview);
    if (!inspector->hasConsoleMessageStorage(contextGroupId)) return;
    std::unique_ptr<protocol::Runtime::ExceptionDetails> exceptionDetails =
        protocol::Runtime::ExceptionDetails::create()
            .setExceptionId(m_exceptionId)
            .setText(exception ? m_message : m_detailedMessage)
            .setLineNumber(m_lineNumber ? m_lineNumber - 1 : 0)
            .setColumnNumber(m_columnNumber ? m_columnNumber - 1 : 0)
            .build();
    if (m_scriptId)
      exceptionDetails->setScriptId(String16::fromInteger(m_scriptId));
    if (!m_url.isEmpty()) exceptionDetails->setUrl(m_url);
    if (m_stackTrace) {
      exceptionDetails->setStackTrace(
          m_stackTrace->buildInspectorObjectImpl(inspector->debugger()));
    }
    if (m_contextId) exceptionDetails->setExecutionContextId(m_contextId);
    if (exception) exceptionDetails->setException(std::move(exception));
    std::unique_ptr<protocol::DictionaryValue> data =
        getAssociatedExceptionData(inspector, session);
    if (data) exceptionDetails->setExceptionMetaData(std::move(data));
    frontend->exceptionThrown(m_timestamp, std::move(exceptionDetails));
    return;
  }
  if (m_origin == V8MessageOrigin::kRevokedException) {
    frontend->exceptionRevoked(m_message, m_revokedExceptionId);
    return;
  }
  if (m_origin == V8MessageOrigin::kConsole) {
    std::unique_ptr<protocol::Array<protocol::Runtime::RemoteObject>>
        arguments = wrapArguments(session, generatePreview);
    if (!inspector->hasConsoleMessageStorage(contextGroupId)) return;
    if (!arguments) {
      arguments =
          std::make_unique<protocol::Array<protocol::Runtime::RemoteObject>>();
      if (!m_message.isEmpty()) {
        std::unique_ptr<protocol::Runtime::RemoteObject> messageArg =
            protocol::Runtime::RemoteObject::create()
                .setType(protocol::Runtime::RemoteObject::TypeEnum::String)
                .build();
        messageArg->setValue(protocol::StringValue::create(m_message));
        arguments->emplace_back(std::move(messageArg));
      }
    }
    std::optional<String16> consoleContext;
    if (!m_consoleContext.isEmpty()) consoleContext = m_consoleContext;
    std::unique_ptr<protocol::Runtime::StackTrace> stackTrace;
    if (m_stackTrace) {
      switch (m_type) {
        case ConsoleAPIType::kAssert:
        case ConsoleAPIType::kError:
        case ConsoleAPIType::kTrace:
        case ConsoleAPIType::kWarning:
          stackTrace =
              m_stackTrace->buildInspectorObjectImpl(inspector->debugger());
          break;
        default:
          stackTrace =
              m_stackTrace->buildInspectorObjectImpl(inspector->debugger(), 0);
          break;
      }
    }
    frontend->consoleAPICalled(
        consoleAPITypeValue(m_type), std::move(arguments), m_contextId,
        m_timestamp, std::move(stackTrace), std::move(consoleContext));
    return;
  }
  UNREACHABLE();
}

std::unique_ptr<protocol::DictionaryValue>
V8ConsoleMessage::getAssociatedExceptionData(
    V8InspectorImpl* inspector, V8InspectorSessionImpl* session) const {
  if (m_arguments.empty() || !m_contextId) return nullptr;
  DCHECK_EQ(1u, m_arguments.size());

  v8::Isolate* isolate = inspector->isolate();
  v8::HandleScope handles(isolate);
  v8::MaybeLocal<v8::Value> maybe_exception = m_arguments[0]->Get(isolate);
  v8::Local<v8::Value> exception;
  if (!maybe_exception.ToLocal(&exception)) return nullptr;

  return inspector->getAssociatedExceptionDataForProtocol(exception);
}

std::unique_ptr<protocol::Runtime::RemoteObject>
V8ConsoleMessage::wrapException(V8InspectorSessionImpl* session,
                                bool generatePreview) const {
  if (m_arguments.empty() || !m_contextId) return nullptr;
  DCHECK_EQ(1u, m_arguments.size());
  InspectedContext* inspectedContext =
      session->inspector()->getContext(session->contextGroupId(), m_contextId);
  if (!inspectedContext) return nullptr;

  v8::Isolate* isolate = inspectedContext->isolate();
  v8::HandleScope handles(isolate);
  // TODO(dgozman): should we use different object group?
  return session->wrapObject(inspectedContext->context(),
                             m_arguments[0]->Get(isolate), "console",
                             generatePreview);
}

V8MessageOrigin V8ConsoleMessage::origin() const { return m_origin; }

ConsoleAPIType V8ConsoleMessage::type() const { return m_type; }

// static
std::unique_ptr<V8ConsoleMessage> V8ConsoleMessage::createForConsoleAPI(
    v8::Local<v8::Context> v8Context, int contextId, int groupId,
    V8InspectorImpl* inspector, double timestamp, ConsoleAPIType type,
    v8::MemorySpan<const v8::Local<v8::Value>> arguments,
    const String16& consoleContext,
    std::unique_ptr<V8StackTraceImpl> stackTrace) {
  v8::Isolate* isolate = v8Context->GetIsolate();

  std::unique_ptr<V8ConsoleMessage> message(
      new V8ConsoleMessage(V8MessageOrigin::kConsole, timestamp, String16()));
  if (stackTrace && !stackTrace->isEmpty()) {
    message->m_url = toString16(stackTrace->topSourceURL());
    message->m_lineNumber = stackTrace->topLineNumber();
    message->m_columnNumber = stackTrace->topColumnNumber();
  }
  message->m_stackTrace = std::move(stackTrace);
  message->m_consoleContext = consoleContext;
  message->m_type = type;
  message->m_contextId = contextId;
  for (v8::Local<v8::Value> arg : arguments) {
    std::unique_ptr<v8::Global<v8::Value>> argument(
        new v8::Global<v8::Value>(isolate, arg));
    argument->AnnotateStrongRetainer(kGlobalConsoleMessageHandleLabel);
    message->m_arguments.push_back(std::move(argument));
    message->m_v8Size += v8::debug::EstimatedValueSize(isolate, arg);
  }
  bool sep = false;
  for (v8::Local<v8::Value> arg : arguments) {
    if (sep) {
      message->m_message += String16(" ");
    } else {
      sep = true;
    }
    message->m_message += V8ValueStringBuilder::toString(arg, v8Context);
  }

  v8::Isolate::MessageErrorLevel clientLevel = v8::Isolate::kMessageInfo;
  if (type == ConsoleAPIType::kDebug || type == ConsoleAPIType::kCount ||
      type == ConsoleAPIType::kTimeEnd) {
    clientLevel = v8::Isolate::kMessageDebug;
  } else if (type == ConsoleAPIType::kError ||
             type == ConsoleAPIType::kAssert) {
    clientLevel = v8::Isolate::kMessageError;
  } else if (type == ConsoleAPIType::kWarning) {
    clientLevel = v8::Isolate::kMessageWarning;
  } else if (type == ConsoleAPIType::kInfo) {
    clientLevel = v8::Isolate::kMessageInfo;
  } else if (type == ConsoleAPIType::kLog) {
    clientLevel = v8::Isolate::kMessageLog;
  }

  if (type != ConsoleAPIType::kClear) {
    inspector->client()->consoleAPIMessage(
        groupId, clientLevel, toStringView(message->m_message),
        toStringView(message->m_url), message->m_lineNumber,
        message->m_columnNumber, message->m_stackTrace.get());
  }

  return message;
}

// static
std::unique_ptr<V8ConsoleMessage> V8ConsoleMessage::createForException(
    double timestamp, const String16& detailedMessage, const String16& url,
    unsigned lineNumber, unsigned columnNumber,
    std::unique_ptr<V8StackTraceImpl> stackTrace, int scriptId,
    v8::Isolate* isolate, const String16& message, int contextId,
    v8::Local<v8::Value> exception, unsigned exceptionId) {
  std::unique_ptr<V8ConsoleMessage> consoleMessage(
      new V8ConsoleMessage(V8MessageOrigin::kException, timestamp, message));
  consoleMessage->setLocation(url, lineNumber, columnNumber,
                              std::move(stackTrace), scriptId);
  consoleMessage->m_exceptionId = exceptionId;
  consoleMessage->m_detailedMessage = detailedMessage;
  if (contextId && !exception.IsEmpty()) {
    consoleMessage->m_contextId = contextId;
    consoleMessage->m_arguments.push_back(
        std::unique_ptr<v8::Global<v8::Value>>(
            new v8::Global<v8::Value>(isolate, exception)));
    consoleMessage->m_v8Size +=
        v8::debug::EstimatedValueSize(isolate, exception);
  }
  return consoleMessage;
}

// static
std::unique_ptr<V8ConsoleMessage> V8ConsoleMessage::createForRevokedException(
    double timestamp, const String16& messageText,
    unsigned revokedExceptionId) {
  std::unique_ptr<V8ConsoleMessage> message(new V8ConsoleMessage(
      V8MessageOrigin::kRevokedException, timestamp, messageText));
  message->m_revokedExceptionId = revokedExceptionId;
  return message;
}

void V8ConsoleMessage::contextDestroyed(int contextId) {
  if (contextId != m_contextId) return;
  m_contextId = 0;
  if (m_message.isEmpty()) m_message = "<message collected>";
  Arguments empty;
  m_arguments.swap(empty);
  m_v8Size = 0;
}

// ------------------------ V8ConsoleMessageStorage
// ----------------------------

V8ConsoleMessageStorage::V8ConsoleMessageStorage(V8InspectorImpl* inspector,
                                                 int contextGroupId)
    : m_inspector(inspector), m_contextGroupId(contextGroupId) {}

V8ConsoleMessageStorage::~V8ConsoleMessageStorage() { clear(); }

namespace {

void TraceV8ConsoleMessageEvent(V8MessageOrigin origin, ConsoleAPIType type) {
  // Change in this function requires adjustment of Catapult/Telemetry metric
  // tracing/tracing/metrics/console_error_metric.html.
  // See https://crbug.com/880432
  if (origin == V8MessageOrigin::kException) {
    TRACE_EVENT_INSTANT0("v8.console", "V8ConsoleMessage::Exception",
                         TRACE_EVENT_SCOPE_THREAD);
  } else if (type == ConsoleAPIType::kError) {
    TRACE_EVENT_INSTANT0("v8.console", "V8ConsoleMessage::Error",
                         TRACE_EVENT_SCOPE_THREAD);
  } else if (type == ConsoleAPIType::kAssert) {
    TRACE_EVENT_INSTANT0("v8.console", "V8ConsoleMessage::Assert",
                         TRACE_EVENT_SCOPE_THREAD);
  }
}

}  // anonymous namespace

void V8ConsoleMessageStorage::addMessage(
    std::unique_ptr<V8ConsoleMessage> message) {
  int contextGroupId = m_contextGroupId;
  V8InspectorImpl* inspector = m_inspector;
  if (message->type() == ConsoleAPIType::kClear) clear();

  TraceV8ConsoleMessageEvent(message->origin(), message->type());

  inspector->forEachSession(
      contextGroupId, [&message](V8InspectorSessionImpl* session) {
        if (message->origin() == V8MessageOrigin::kConsole)
          session->consoleAgent()->messageAdded(message.get());
        session->runtimeAgent()->messageAdded(message.get());
      });
  if (!inspector->hasConsoleMessageStorage(contextGroupId)) return;

  DCHECK(m_messages.size() <= maxConsoleMessageCount);
  if (m_messages.size() == maxConsoleMessageCount) {
    m_estimatedSize -= m_messages.front()->estimatedSize();
    m_messages.pop_front();
  }
  while (m_estimatedSize + message->estimatedSize() > maxConsoleMessageV8Size &&
         !m_messages.empty()) {
    m_estimatedSize -= m_messages.front()->estimatedSize();
    m_messages.pop_front();
  }

  m_messages.push_back(std::move(message));
  m_estimatedSize += m_messages.back()->estimatedSize();
}

void V8ConsoleMessageStorage::clear() {
  m_messages.clear();
  m_estimatedSize = 0;
  m_inspector->forEachSession(m_contextGroupId,
                              [](V8InspectorSessionImpl* session) {
                                session->releaseObjectGroup("console");
                              });
  for (auto& data : m_data) {
    data.second.m_counters.clear();
    data.second.m_reportedDeprecationMessages.clear();
  }
}

bool V8ConsoleMessageStorage::shouldReportDeprecationMessage(
    int contextId, const String16& method) {
  std::set<String16>& reportedDeprecationMessages =
      m_data[contextId].m_reportedDeprecationMessages;
  auto it = reportedDeprecationMessages.find(method);
  if (it != reportedDeprecationMessages.end()) return false;
  reportedDeprecationMessages.insert(it, method);
  return true;
}

int V8ConsoleMessageStorage::count(int contextId, int consoleContextId,
                                   const String16& label) {
  return ++m_data[contextId].m_counters[LabelKey{consoleContextId, label}];
}

bool V8ConsoleMessageStorage::countReset(int contextId, int consoleContextId,
                                         const String16& label) {
  std::map<LabelKey, int>& counters = m_data[contextId].m_counters;
  auto it = counters.find(LabelKey{consoleContextId, label});
  if (it == counters.end()) return false;
  counters.erase(it);
  return true;
}

bool V8ConsoleMessageStorage::time(int contextId, int consoleContextId,
                                   const String16& label) {
  return m_data[contextId]
      .m_timers
      .try_emplace(LabelKey{consoleContextId, label},
                   m_inspector->client()->currentTimeMS())
      .second;
}

std::optional<double> V8ConsoleMessageStorage::timeLog(int contextId,
                                                       int consoleContextId,
                                                       const String16& label) {
  auto& timers = m_data[contextId].m_timers;
  auto it = timers.find(std::make_pair(consoleContextId, label));
  if (it == timers.end()) return std::nullopt;
  return m_inspector->client()->currentTimeMS() - it->second;
}

std::optional<double> V8ConsoleMessageStorage::timeEnd(int contextId,
                                                       int consoleContextId,
                                                       const String16& label) {
  auto& timers = m_data[contextId].m_timers;
  auto it = timers.find(std::make_pair(consoleContextId, label));
  if (it == timers.end()) return std::nullopt;
  double result = m_inspector->client()->currentTimeMS() - it->second;
  timers.erase(it);
  return result;
}

void V8ConsoleMessageStorage::contextDestroyed(int contextId) {
  m_estimatedSize = 0;
  for (size_t i = 0; i < m_messages.size(); ++i) {
    m_messages[i]->contextDestroyed(contextId);
    m_estimatedSize += m_messages[i]->estimatedSize();
  }
  {
    auto it = m_data.find(contextId);
    if (it != m_data.end()) m_data.erase(contextId);
  }
}

}  // namespace v8_inspector
