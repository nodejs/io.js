// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INSPECTOR_V8_INSPECTOR_SESSION_IMPL_H_
#define V8_INSPECTOR_V8_INSPECTOR_SESSION_IMPL_H_

#include <memory>
#include <vector>

#include "src/base/macros.h"
#include "src/inspector/protocol/Forward.h"
#include "src/inspector/protocol/Runtime.h"
#include "src/inspector/protocol/Schema.h"

#include "include/v8-inspector.h"

namespace v8_inspector {

class InjectedScript;
class RemoteObjectIdBase;
class V8ConsoleAgentImpl;
class V8DebuggerAgentImpl;
class V8DebuggerBarrier;
class V8InspectorImpl;
class V8HeapProfilerAgentImpl;
class V8ProfilerAgentImpl;
class V8RuntimeAgentImpl;
class V8SchemaAgentImpl;

using protocol::Response;

class V8InspectorSessionImpl : public V8InspectorSession,
                               public protocol::FrontendChannel {
 public:
  static std::unique_ptr<V8InspectorSessionImpl> create(
      V8InspectorImpl*, int contextGroupId, int sessionId,
      V8Inspector::Channel*, StringView state,
      v8_inspector::V8Inspector::ClientTrustLevel,
      std::shared_ptr<V8DebuggerBarrier>);
  ~V8InspectorSessionImpl() override;
  V8InspectorSessionImpl(const V8InspectorSessionImpl&) = delete;
  V8InspectorSessionImpl& operator=(const V8InspectorSessionImpl&) = delete;

  V8InspectorImpl* inspector() const { return m_inspector; }
  V8ConsoleAgentImpl* consoleAgent() { return m_consoleAgent.get(); }
  V8DebuggerAgentImpl* debuggerAgent() { return m_debuggerAgent.get(); }
  V8SchemaAgentImpl* schemaAgent() { return m_schemaAgent.get(); }
  V8ProfilerAgentImpl* profilerAgent() { return m_profilerAgent.get(); }
  V8RuntimeAgentImpl* runtimeAgent() { return m_runtimeAgent.get(); }
  V8HeapProfilerAgentImpl* heapProfilerAgent() {
    return m_heapProfilerAgent.get();
  }
  int contextGroupId() const { return m_contextGroupId; }
  int sessionId() const { return m_sessionId; }

  Response findInjectedScript(int contextId, InjectedScript*&);
  Response findInjectedScript(RemoteObjectIdBase*, InjectedScript*&);
  void reset();
  void discardInjectedScripts();
  void reportAllContexts(V8RuntimeAgentImpl*);
  void setCustomObjectFormatterEnabled(bool);
  std::unique_ptr<protocol::Runtime::RemoteObject> wrapObject(
      v8::Local<v8::Context>, v8::Local<v8::Value>, const String16& groupName,
      bool generatePreview);
  std::unique_ptr<protocol::Runtime::RemoteObject> wrapTable(
      v8::Local<v8::Context>, v8::Local<v8::Object> table,
      v8::MaybeLocal<v8::Array> columns);
  std::vector<std::unique_ptr<protocol::Schema::Domain>> supportedDomainsImpl();
  Response unwrapObject(const String16& objectId, v8::Local<v8::Value>*,
                        v8::Local<v8::Context>*, String16* objectGroup);
  void releaseObjectGroup(const String16& objectGroup);

  // V8InspectorSession implementation.
  void dispatchProtocolMessage(StringView message) override;
  std::vector<uint8_t> state() override;
  std::vector<std::unique_ptr<protocol::Schema::API::Domain>> supportedDomains()
      override;
  void addInspectedObject(
      std::unique_ptr<V8InspectorSession::Inspectable>) override;
  void schedulePauseOnNextStatement(StringView breakReason,
                                    StringView breakDetails) override;
  void cancelPauseOnNextStatement() override;
  void breakProgram(StringView breakReason, StringView breakDetails) override;
  void setSkipAllPauses(bool) override;
  void resume(bool terminateOnResume = false) override;
  void stepOver() override;
  std::vector<std::unique_ptr<protocol::Debugger::API::SearchMatch>>
  searchInTextByLines(StringView text, StringView query, bool caseSensitive,
                      bool isRegex) override;
  void releaseObjectGroup(StringView objectGroup) override;
  bool unwrapObject(std::unique_ptr<StringBuffer>*, StringView objectId,
                    v8::Local<v8::Value>*, v8::Local<v8::Context>*,
                    std::unique_ptr<StringBuffer>* objectGroup) override;
  std::unique_ptr<protocol::Runtime::API::RemoteObject> wrapObject(
      v8::Local<v8::Context>, v8::Local<v8::Value>, StringView groupName,
      bool generatePreview) override;

  V8InspectorSession::Inspectable* inspectedObject(unsigned num);
  static const unsigned kInspectedObjectBufferSize = 5;

  void triggerPreciseCoverageDeltaUpdate(StringView occasion) override;
  EvaluateResult evaluate(v8::Local<v8::Context> context, StringView expression,
                          bool includeCommandLineAPI = false) override;
  void stop() override;

  V8Inspector::ClientTrustLevel clientTrustLevel() {
    return m_clientTrustLevel;
  }

 private:
  V8InspectorSessionImpl(V8InspectorImpl*, int contextGroupId, int sessionId,
                         V8Inspector::Channel*, StringView state,
                         V8Inspector::ClientTrustLevel,
                         std::shared_ptr<V8DebuggerBarrier>);
  protocol::DictionaryValue* agentState(const String16& name);

  // protocol::FrontendChannel implementation.
  void SendProtocolResponse(
      int callId, std::unique_ptr<protocol::Serializable> message) override;
  void SendProtocolNotification(
      std::unique_ptr<protocol::Serializable> message) override;
  void FallThrough(int callId, v8_crdtp::span<uint8_t> method,
                   v8_crdtp::span<uint8_t> message) override;
  void FlushProtocolNotifications() override;

  std::unique_ptr<StringBuffer> serializeForFrontend(
      std::unique_ptr<protocol::Serializable> message);
  int m_contextGroupId;
  int m_sessionId;
  V8InspectorImpl* m_inspector;
  V8Inspector::Channel* m_channel;
  bool m_customObjectFormatterEnabled;

  protocol::UberDispatcher m_dispatcher;
  std::unique_ptr<protocol::DictionaryValue> m_state;

  std::unique_ptr<V8RuntimeAgentImpl> m_runtimeAgent;
  std::unique_ptr<V8DebuggerAgentImpl> m_debuggerAgent;
  std::unique_ptr<V8HeapProfilerAgentImpl> m_heapProfilerAgent;
  std::unique_ptr<V8ProfilerAgentImpl> m_profilerAgent;
  std::unique_ptr<V8ConsoleAgentImpl> m_consoleAgent;
  std::unique_ptr<V8SchemaAgentImpl> m_schemaAgent;
  std::vector<std::unique_ptr<V8InspectorSession::Inspectable>>
      m_inspectedObjects;
  bool use_binary_protocol_ = false;
  V8Inspector::ClientTrustLevel m_clientTrustLevel = V8Inspector::kUntrusted;
};

}  // namespace v8_inspector

#endif  // V8_INSPECTOR_V8_INSPECTOR_SESSION_IMPL_H_
