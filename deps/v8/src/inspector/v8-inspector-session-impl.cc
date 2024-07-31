// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/inspector/v8-inspector-session-impl.h"

#include "../../third_party/inspector_protocol/crdtp/cbor.h"
#include "../../third_party/inspector_protocol/crdtp/dispatch.h"
#include "../../third_party/inspector_protocol/crdtp/json.h"
#include "include/v8-context.h"
#include "include/v8-microtask-queue.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/inspector/injected-script.h"
#include "src/inspector/inspected-context.h"
#include "src/inspector/protocol/Protocol.h"
#include "src/inspector/remote-object-id.h"
#include "src/inspector/search-util.h"
#include "src/inspector/string-util.h"
#include "src/inspector/v8-console-agent-impl.h"
#include "src/inspector/v8-debugger-agent-impl.h"
#include "src/inspector/v8-debugger-barrier.h"
#include "src/inspector/v8-debugger.h"
#include "src/inspector/v8-heap-profiler-agent-impl.h"
#include "src/inspector/v8-inspector-impl.h"
#include "src/inspector/v8-profiler-agent-impl.h"
#include "src/inspector/v8-runtime-agent-impl.h"
#include "src/inspector/v8-schema-agent-impl.h"

namespace v8_inspector {
namespace {
using v8_crdtp::span;
using v8_crdtp::SpanFrom;
using v8_crdtp::Status;
using v8_crdtp::cbor::CheckCBORMessage;
using v8_crdtp::json::ConvertCBORToJSON;
using v8_crdtp::json::ConvertJSONToCBOR;

bool IsCBORMessage(StringView msg) {
  if (!msg.is8Bit() || msg.length() < 3) return false;
  const uint8_t* bytes = msg.characters8();
  return bytes[0] == 0xd8 &&
         (bytes[1] == 0x5a || (bytes[1] == 0x18 && bytes[2] == 0x5a));
}

Status ConvertToCBOR(StringView state, std::vector<uint8_t>* cbor) {
  return state.is8Bit()
             ? ConvertJSONToCBOR(
                   span<uint8_t>(state.characters8(), state.length()), cbor)
             : ConvertJSONToCBOR(
                   span<uint16_t>(state.characters16(), state.length()), cbor);
}

std::unique_ptr<protocol::DictionaryValue> ParseState(StringView state) {
  std::vector<uint8_t> converted;
  span<uint8_t> cbor;
  if (IsCBORMessage(state))
    cbor = span<uint8_t>(state.characters8(), state.length());
  else if (ConvertToCBOR(state, &converted).ok())
    cbor = SpanFrom(converted);
  if (!cbor.empty()) {
    std::unique_ptr<protocol::Value> value =
        protocol::Value::parseBinary(cbor.data(), cbor.size());
    std::unique_ptr<protocol::DictionaryValue> dictionaryValue =
        protocol::DictionaryValue::cast(std::move(value));
    if (dictionaryValue) return dictionaryValue;
  }
  return protocol::DictionaryValue::create();
}
}  // namespace

// static
bool V8InspectorSession::canDispatchMethod(StringView method) {
  return stringViewStartsWith(method,
                              protocol::Runtime::Metainfo::commandPrefix) ||
         stringViewStartsWith(method,
                              protocol::Debugger::Metainfo::commandPrefix) ||
         stringViewStartsWith(method,
                              protocol::Profiler::Metainfo::commandPrefix) ||
         stringViewStartsWith(
             method, protocol::HeapProfiler::Metainfo::commandPrefix) ||
         stringViewStartsWith(method,
                              protocol::Console::Metainfo::commandPrefix) ||
         stringViewStartsWith(method,
                              protocol::Schema::Metainfo::commandPrefix);
}

// static
int V8ContextInfo::executionContextId(v8::Local<v8::Context> context) {
  return InspectedContext::contextId(context);
}

std::unique_ptr<V8InspectorSessionImpl> V8InspectorSessionImpl::create(
    V8InspectorImpl* inspector, int contextGroupId, int sessionId,
    V8Inspector::Channel* channel, StringView state,
    V8Inspector::ClientTrustLevel clientTrustLevel,
    std::shared_ptr<V8DebuggerBarrier> debuggerBarrier) {
  return std::unique_ptr<V8InspectorSessionImpl>(new V8InspectorSessionImpl(
      inspector, contextGroupId, sessionId, channel, state, clientTrustLevel,
      std::move(debuggerBarrier)));
}

V8InspectorSessionImpl::V8InspectorSessionImpl(
    V8InspectorImpl* inspector, int contextGroupId, int sessionId,
    V8Inspector::Channel* channel, StringView savedState,
    V8Inspector::ClientTrustLevel clientTrustLevel,
    std::shared_ptr<V8DebuggerBarrier> debuggerBarrier)
    : m_contextGroupId(contextGroupId),
      m_sessionId(sessionId),
      m_inspector(inspector),
      m_channel(channel),
      m_customObjectFormatterEnabled(false),
      m_dispatcher(this),
      m_state(ParseState(savedState)),
      m_runtimeAgent(nullptr),
      m_debuggerAgent(nullptr),
      m_heapProfilerAgent(nullptr),
      m_profilerAgent(nullptr),
      m_consoleAgent(nullptr),
      m_schemaAgent(nullptr),
      m_clientTrustLevel(clientTrustLevel) {
  m_state->getBoolean("use_binary_protocol", &use_binary_protocol_);

  m_runtimeAgent.reset(new V8RuntimeAgentImpl(
      this, this, agentState(protocol::Runtime::Metainfo::domainName),
      std::move(debuggerBarrier)));
  protocol::Runtime::Dispatcher::wire(&m_dispatcher, m_runtimeAgent.get());

  m_debuggerAgent.reset(new V8DebuggerAgentImpl(
      this, this, agentState(protocol::Debugger::Metainfo::domainName)));
  protocol::Debugger::Dispatcher::wire(&m_dispatcher, m_debuggerAgent.get());

  m_consoleAgent.reset(new V8ConsoleAgentImpl(
      this, this, agentState(protocol::Console::Metainfo::domainName)));
  protocol::Console::Dispatcher::wire(&m_dispatcher, m_consoleAgent.get());

  m_profilerAgent.reset(new V8ProfilerAgentImpl(
      this, this, agentState(protocol::Profiler::Metainfo::domainName)));
  protocol::Profiler::Dispatcher::wire(&m_dispatcher, m_profilerAgent.get());

  if (m_clientTrustLevel == V8Inspector::kFullyTrusted) {
    m_heapProfilerAgent.reset(new V8HeapProfilerAgentImpl(
        this, this, agentState(protocol::HeapProfiler::Metainfo::domainName)));
    protocol::HeapProfiler::Dispatcher::wire(&m_dispatcher,
                                             m_heapProfilerAgent.get());

    m_schemaAgent.reset(new V8SchemaAgentImpl(
        this, this, agentState(protocol::Schema::Metainfo::domainName)));
    protocol::Schema::Dispatcher::wire(&m_dispatcher, m_schemaAgent.get());
  }
  if (savedState.length()) {
    m_runtimeAgent->restore();
    m_debuggerAgent->restore();
    if (m_heapProfilerAgent) m_heapProfilerAgent->restore();
    m_profilerAgent->restore();
    m_consoleAgent->restore();
  }
}

V8InspectorSessionImpl::~V8InspectorSessionImpl() {
  v8::Isolate::Scope scope(m_inspector->isolate());
  discardInjectedScripts();
  m_consoleAgent->disable();
  m_profilerAgent->disable();
  if (m_heapProfilerAgent) m_heapProfilerAgent->disable();
  m_debuggerAgent->disable();
  m_runtimeAgent->disable();
  m_inspector->disconnect(this);
}

protocol::DictionaryValue* V8InspectorSessionImpl::agentState(
    const String16& name) {
  protocol::DictionaryValue* state = m_state->getObject(name);
  if (!state) {
    std::unique_ptr<protocol::DictionaryValue> newState =
        protocol::DictionaryValue::create();
    state = newState.get();
    m_state->setObject(name, std::move(newState));
  }
  return state;
}

std::unique_ptr<StringBuffer> V8InspectorSessionImpl::serializeForFrontend(
    std::unique_ptr<protocol::Serializable> message) {
  std::vector<uint8_t> cbor = message->Serialize();
  DCHECK(CheckCBORMessage(SpanFrom(cbor)).ok());
  if (use_binary_protocol_) return StringBufferFrom(std::move(cbor));
  std::vector<uint8_t> json;
  Status status = ConvertCBORToJSON(SpanFrom(cbor), &json);
  DCHECK(status.ok());
  USE(status);
  // TODO(johannes): It should be OK to make a StringBuffer from |json|
  // directly, since it's 7 Bit US-ASCII with anything else escaped.
  // However it appears that the Node.js tests (or perhaps even production)
  // assume that the StringBuffer is 16 Bit. It probably accesses
  // characters16() somehwere without checking is8Bit. Until it's fixed
  // we take a detour via String16 which makes the StringBuffer 16 bit.
  String16 string16(reinterpret_cast<const char*>(json.data()), json.size());
  return StringBufferFrom(std::move(string16));
}

void V8InspectorSessionImpl::SendProtocolResponse(
    int callId, std::unique_ptr<protocol::Serializable> message) {
  m_channel->sendResponse(callId, serializeForFrontend(std::move(message)));
}

void V8InspectorSessionImpl::SendProtocolNotification(
    std::unique_ptr<protocol::Serializable> message) {
  m_channel->sendNotification(serializeForFrontend(std::move(message)));
}

void V8InspectorSessionImpl::FallThrough(int callId,
                                         const v8_crdtp::span<uint8_t> method,
                                         v8_crdtp::span<uint8_t> message) {
  // There's no other layer to handle the command.
  UNREACHABLE();
}

void V8InspectorSessionImpl::FlushProtocolNotifications() {
  m_channel->flushProtocolNotifications();
}

void V8InspectorSessionImpl::reset() {
  m_debuggerAgent->reset();
  m_runtimeAgent->reset();
  discardInjectedScripts();
}

void V8InspectorSessionImpl::discardInjectedScripts() {
  m_inspectedObjects.clear();
  int sessionId = m_sessionId;
  m_inspector->forEachContext(m_contextGroupId,
                              [&sessionId](InspectedContext* context) {
                                context->discardInjectedScript(sessionId);
                              });
}

Response V8InspectorSessionImpl::findInjectedScript(
    int contextId, InjectedScript*& injectedScript) {
  injectedScript = nullptr;
  InspectedContext* context =
      m_inspector->getContext(m_contextGroupId, contextId);
  if (!context)
    return Response::ServerError("Cannot find context with specified id");
  injectedScript = context->getInjectedScript(m_sessionId);
  if (!injectedScript) {
    injectedScript = context->createInjectedScript(m_sessionId);
    if (m_customObjectFormatterEnabled)
      injectedScript->setCustomObjectFormatterEnabled(true);
  }
  return Response::Success();
}

Response V8InspectorSessionImpl::findInjectedScript(
    RemoteObjectIdBase* objectId, InjectedScript*& injectedScript) {
  if (objectId->isolateId() != m_inspector->isolateId())
    return Response::ServerError("Cannot find context with specified id");
  return findInjectedScript(objectId->contextId(), injectedScript);
}

void V8InspectorSessionImpl::releaseObjectGroup(StringView objectGroup) {
  releaseObjectGroup(toString16(objectGroup));
}

void V8InspectorSessionImpl::releaseObjectGroup(const String16& objectGroup) {
  int sessionId = m_sessionId;
  m_inspector->forEachContext(
      m_contextGroupId, [&objectGroup, &sessionId](InspectedContext* context) {
        InjectedScript* injectedScript = context->getInjectedScript(sessionId);
        if (injectedScript) injectedScript->releaseObjectGroup(objectGroup);
      });
}

bool V8InspectorSessionImpl::unwrapObject(
    std::unique_ptr<StringBuffer>* error, StringView objectId,
    v8::Local<v8::Value>* object, v8::Local<v8::Context>* context,
    std::unique_ptr<StringBuffer>* objectGroup) {
  String16 objectGroupString;
  Response response = unwrapObject(toString16(objectId), object, context,
                                   objectGroup ? &objectGroupString : nullptr);
  if (response.IsError()) {
    if (error) {
      const std::string& msg = response.Message();
      *error = StringBufferFrom(String16::fromUTF8(msg.data(), msg.size()));
    }
    return false;
  }
  if (objectGroup)
    *objectGroup = StringBufferFrom(std::move(objectGroupString));
  return true;
}

Response V8InspectorSessionImpl::unwrapObject(const String16& objectId,
                                              v8::Local<v8::Value>* object,
                                              v8::Local<v8::Context>* context,
                                              String16* objectGroup) {
  std::unique_ptr<RemoteObjectId> remoteId;
  Response response = RemoteObjectId::parse(objectId, &remoteId);
  if (!response.IsSuccess()) return response;
  InjectedScript* injectedScript = nullptr;
  response = findInjectedScript(remoteId.get(), injectedScript);
  if (!response.IsSuccess()) return response;
  response = injectedScript->findObject(*remoteId, object);
  if (!response.IsSuccess()) return response;
  *context = injectedScript->context()->context();
  if (objectGroup) *objectGroup = injectedScript->objectGroupName(*remoteId);
  return Response::Success();
}

std::unique_ptr<protocol::Runtime::API::RemoteObject>
V8InspectorSessionImpl::wrapObject(v8::Local<v8::Context> context,
                                   v8::Local<v8::Value> value,
                                   StringView groupName, bool generatePreview) {
  return wrapObject(context, value, toString16(groupName), generatePreview);
}

std::unique_ptr<protocol::Runtime::RemoteObject>
V8InspectorSessionImpl::wrapObject(v8::Local<v8::Context> context,
                                   v8::Local<v8::Value> value,
                                   const String16& groupName,
                                   bool generatePreview) {
  InjectedScript* injectedScript = nullptr;
  findInjectedScript(InspectedContext::contextId(context), injectedScript);
  if (!injectedScript) return nullptr;
  std::unique_ptr<protocol::Runtime::RemoteObject> result;
  injectedScript->wrapObject(value, groupName,
                             generatePreview ? WrapOptions({WrapMode::kPreview})
                                             : WrapOptions({WrapMode::kIdOnly}),
                             &result);
  return result;
}

std::unique_ptr<protocol::Runtime::RemoteObject>
V8InspectorSessionImpl::wrapTable(v8::Local<v8::Context> context,
                                  v8::Local<v8::Object> table,
                                  v8::MaybeLocal<v8::Array> columns) {
  InjectedScript* injectedScript = nullptr;
  findInjectedScript(InspectedContext::contextId(context), injectedScript);
  if (!injectedScript) return nullptr;
  return injectedScript->wrapTable(table, columns);
}

void V8InspectorSessionImpl::setCustomObjectFormatterEnabled(bool enabled) {
  m_customObjectFormatterEnabled = enabled;
  int sessionId = m_sessionId;
  m_inspector->forEachContext(
      m_contextGroupId, [&enabled, &sessionId](InspectedContext* context) {
        InjectedScript* injectedScript = context->getInjectedScript(sessionId);
        if (injectedScript)
          injectedScript->setCustomObjectFormatterEnabled(enabled);
      });
}

void V8InspectorSessionImpl::reportAllContexts(V8RuntimeAgentImpl* agent) {
  m_inspector->forEachContext(m_contextGroupId,
                              [&agent](InspectedContext* context) {
                                agent->reportExecutionContextCreated(context);
                              });
}

void V8InspectorSessionImpl::dispatchProtocolMessage(StringView message) {
  using v8_crdtp::span;
  using v8_crdtp::SpanFrom;
  span<uint8_t> cbor;
  std::vector<uint8_t> converted_cbor;
  if (IsCBORMessage(message)) {
    use_binary_protocol_ = true;
    m_state->setBoolean("use_binary_protocol", true);
    cbor = span<uint8_t>(message.characters8(), message.length());
  } else {
    // We're ignoring the return value of the conversion function
    // intentionally. It means the |parsed_message| below will be nullptr.
    auto status = ConvertToCBOR(message, &converted_cbor);
    if (!status.ok()) {
      m_channel->sendNotification(
          serializeForFrontend(v8_crdtp::CreateErrorNotification(
              v8_crdtp::DispatchResponse::ParseError(status.ToASCIIString()))));
      return;
    }
    cbor = SpanFrom(converted_cbor);
  }
  v8_crdtp::Dispatchable dispatchable(cbor);
  if (!dispatchable.ok()) {
    if (!dispatchable.HasCallId()) {
      m_channel->sendNotification(serializeForFrontend(
          v8_crdtp::CreateErrorNotification(dispatchable.DispatchError())));
    } else {
      m_channel->sendResponse(
          dispatchable.CallId(),
          serializeForFrontend(v8_crdtp::CreateErrorResponse(
              dispatchable.CallId(), dispatchable.DispatchError())));
    }
    return;
  }
  m_dispatcher.Dispatch(dispatchable).Run();
}

std::vector<uint8_t> V8InspectorSessionImpl::state() {
  return m_state->Serialize();
}

std::vector<std::unique_ptr<protocol::Schema::API::Domain>>
V8InspectorSessionImpl::supportedDomains() {
  std::vector<std::unique_ptr<protocol::Schema::Domain>> domains =
      supportedDomainsImpl();
  std::vector<std::unique_ptr<protocol::Schema::API::Domain>> result;
  for (size_t i = 0; i < domains.size(); ++i)
    result.push_back(std::move(domains[i]));
  return result;
}

std::vector<std::unique_ptr<protocol::Schema::Domain>>
V8InspectorSessionImpl::supportedDomainsImpl() {
  std::vector<std::unique_ptr<protocol::Schema::Domain>> result;
  result.push_back(protocol::Schema::Domain::create()
                       .setName(protocol::Runtime::Metainfo::domainName)
                       .setVersion(protocol::Runtime::Metainfo::version)
                       .build());
  result.push_back(protocol::Schema::Domain::create()
                       .setName(protocol::Debugger::Metainfo::domainName)
                       .setVersion(protocol::Debugger::Metainfo::version)
                       .build());
  result.push_back(protocol::Schema::Domain::create()
                       .setName(protocol::Profiler::Metainfo::domainName)
                       .setVersion(protocol::Profiler::Metainfo::version)
                       .build());
  result.push_back(protocol::Schema::Domain::create()
                       .setName(protocol::HeapProfiler::Metainfo::domainName)
                       .setVersion(protocol::HeapProfiler::Metainfo::version)
                       .build());
  result.push_back(protocol::Schema::Domain::create()
                       .setName(protocol::Schema::Metainfo::domainName)
                       .setVersion(protocol::Schema::Metainfo::version)
                       .build());
  return result;
}

void V8InspectorSessionImpl::addInspectedObject(
    std::unique_ptr<V8InspectorSession::Inspectable> inspectable) {
  m_inspectedObjects.insert(m_inspectedObjects.begin(), std::move(inspectable));
  if (m_inspectedObjects.size() > kInspectedObjectBufferSize)
    m_inspectedObjects.resize(kInspectedObjectBufferSize);
}

V8InspectorSession::Inspectable* V8InspectorSessionImpl::inspectedObject(
    unsigned num) {
  if (num >= m_inspectedObjects.size()) return nullptr;
  return m_inspectedObjects[num].get();
}

void V8InspectorSessionImpl::schedulePauseOnNextStatement(
    StringView breakReason, StringView breakDetails) {
  std::vector<uint8_t> cbor;
  ConvertToCBOR(breakDetails, &cbor);
  m_debuggerAgent->schedulePauseOnNextStatement(
      toString16(breakReason),
      protocol::DictionaryValue::cast(
          protocol::Value::parseBinary(cbor.data(), cbor.size())));
}

void V8InspectorSessionImpl::cancelPauseOnNextStatement() {
  m_debuggerAgent->cancelPauseOnNextStatement();
}

void V8InspectorSessionImpl::breakProgram(StringView breakReason,
                                          StringView breakDetails) {
  std::vector<uint8_t> cbor;
  ConvertToCBOR(breakDetails, &cbor);
  m_debuggerAgent->breakProgram(
      toString16(breakReason),
      protocol::DictionaryValue::cast(
          protocol::Value::parseBinary(cbor.data(), cbor.size())));
}

void V8InspectorSessionImpl::setSkipAllPauses(bool skip) {
  m_debuggerAgent->setSkipAllPauses(skip);
}

void V8InspectorSessionImpl::resume(bool terminateOnResume) {
  m_debuggerAgent->resume(terminateOnResume);
}

void V8InspectorSessionImpl::stepOver() { m_debuggerAgent->stepOver({}); }

std::vector<std::unique_ptr<protocol::Debugger::API::SearchMatch>>
V8InspectorSessionImpl::searchInTextByLines(StringView text, StringView query,
                                            bool caseSensitive, bool isRegex) {
  // TODO(dgozman): search may operate on StringView and avoid copying |text|.
  std::vector<std::unique_ptr<protocol::Debugger::SearchMatch>> matches =
      searchInTextByLinesImpl(this, toString16(text), toString16(query),
                              caseSensitive, isRegex);
  std::vector<std::unique_ptr<protocol::Debugger::API::SearchMatch>> result;
  for (size_t i = 0; i < matches.size(); ++i)
    result.push_back(std::move(matches[i]));
  return result;
}

void V8InspectorSessionImpl::triggerPreciseCoverageDeltaUpdate(
    StringView occasion) {
  m_profilerAgent->triggerPreciseCoverageDeltaUpdate(toString16(occasion));
}

V8InspectorSession::EvaluateResult V8InspectorSessionImpl::evaluate(
    v8::Local<v8::Context> context, StringView expression,
    bool includeCommandLineAPI) {
  v8::EscapableHandleScope handleScope(m_inspector->isolate());
  InjectedScript::ContextScope scope(this,
                                     InspectedContext::contextId(context));
  if (!scope.initialize().IsSuccess()) {
    return {EvaluateResult::ResultType::kNotRun, v8::Local<v8::Value>()};
  }

  // Temporarily allow eval.
  scope.allowCodeGenerationFromStrings();
  scope.setTryCatchVerbose();
  if (includeCommandLineAPI) {
    scope.installCommandLineAPI();
  }
  v8::MaybeLocal<v8::Value> maybeResultValue;
  {
    v8::MicrotasksScope microtasksScope(scope.context(),
                                        v8::MicrotasksScope::kRunMicrotasks);
    const v8::Local<v8::String> source =
        toV8String(m_inspector->isolate(), expression);
    maybeResultValue = v8::debug::EvaluateGlobal(
        m_inspector->isolate(), source, v8::debug::EvaluateGlobalMode::kDefault,
        /*repl_mode=*/false);
  }

  if (scope.tryCatch().HasCaught()) {
    return {EvaluateResult::ResultType::kException,
            handleScope.Escape(scope.tryCatch().Exception())};
  }
  v8::Local<v8::Value> result;
  CHECK(maybeResultValue.ToLocal(&result));
  return {EvaluateResult::ResultType::kSuccess, handleScope.Escape(result)};
}

void V8InspectorSessionImpl::stop() { m_debuggerAgent->stop(); }

}  // namespace v8_inspector
