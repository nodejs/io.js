/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef V8_INSPECTOR_INJECTED_SCRIPT_H_
#define V8_INSPECTOR_INJECTED_SCRIPT_H_

#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "include/v8-exception.h"
#include "include/v8-local-handle.h"
#include "include/v8-persistent-handle.h"
#include "src/base/macros.h"
#include "src/inspector/inspected-context.h"
#include "src/inspector/protocol/Forward.h"
#include "src/inspector/protocol/Runtime.h"
#include "src/inspector/v8-console.h"
#include "src/inspector/v8-debugger.h"

namespace v8_inspector {

class RemoteObjectId;
class V8InspectorImpl;
class V8InspectorSessionImpl;
class ValueMirror;

using protocol::Response;

class EvaluateCallback {
 public:
  static void sendSuccess(
      std::weak_ptr<EvaluateCallback> callback, InjectedScript* injectedScript,
      std::unique_ptr<protocol::Runtime::RemoteObject> result,
      std::unique_ptr<protocol::Runtime::ExceptionDetails> exceptionDetails);
  static void sendFailure(std::weak_ptr<EvaluateCallback> callback,
                          InjectedScript* injectedScript,
                          const protocol::DispatchResponse& response);

  virtual ~EvaluateCallback() = default;

 private:
  virtual void sendSuccess(
      std::unique_ptr<protocol::Runtime::RemoteObject> result,
      std::unique_ptr<protocol::Runtime::ExceptionDetails>
          exceptionDetails) = 0;
  virtual void sendFailure(const protocol::DispatchResponse& response) = 0;
};

class InjectedScript final {
 public:
  InjectedScript(InspectedContext*, int sessionId);
  ~InjectedScript();
  InjectedScript(const InjectedScript&) = delete;
  InjectedScript& operator=(const InjectedScript&) = delete;

  InspectedContext* context() const { return m_context; }

  Response getProperties(
      v8::Local<v8::Object>, const String16& groupName, bool ownProperties,
      bool accessorPropertiesOnly, bool nonIndexedPropertiesOnly,
      const WrapOptions& wrapOptions,
      std::unique_ptr<protocol::Array<protocol::Runtime::PropertyDescriptor>>*
          result,
      std::unique_ptr<protocol::Runtime::ExceptionDetails>*);

  Response getInternalAndPrivateProperties(
      v8::Local<v8::Value>, const String16& groupName,
      bool accessorPropertiesOnly,
      std::unique_ptr<
          protocol::Array<protocol::Runtime::InternalPropertyDescriptor>>*
          internalProperties,
      std::unique_ptr<
          protocol::Array<protocol::Runtime::PrivatePropertyDescriptor>>*
          privateProperties);

  void releaseObject(const String16& objectId);

  Response wrapObject(v8::Local<v8::Value>, const String16& groupName,
                      const WrapOptions& wrapOptions,
                      std::unique_ptr<protocol::Runtime::RemoteObject>* result);
  Response wrapObject(v8::Local<v8::Value>, const String16& groupName,
                      const WrapOptions& wrapOptions,
                      v8::MaybeLocal<v8::Value> customPreviewConfig,
                      int maxCustomPreviewDepth,
                      std::unique_ptr<protocol::Runtime::RemoteObject>* result);
  Response wrapObjectMirror(
      const ValueMirror& mirror, const String16& groupName,
      const WrapOptions& wrapOptions,
      v8::MaybeLocal<v8::Value> customPreviewConfig, int maxCustomPreviewDepth,
      std::unique_ptr<protocol::Runtime::RemoteObject>* result);
  std::unique_ptr<protocol::Runtime::RemoteObject> wrapTable(
      v8::Local<v8::Object> table, v8::MaybeLocal<v8::Array> columns);

  void addPromiseCallback(V8InspectorSessionImpl* session,
                          v8::MaybeLocal<v8::Value> value,
                          const String16& objectGroup,
                          std::unique_ptr<WrapOptions> wrapOptions,
                          bool replMode, bool throwOnSideEffect,
                          std::shared_ptr<EvaluateCallback> callback);

  Response findObject(const RemoteObjectId&, v8::Local<v8::Value>*) const;
  String16 objectGroupName(const RemoteObjectId&) const;
  void releaseObjectGroup(const String16&);
  void setCustomObjectFormatterEnabled(bool);
  Response resolveCallArgument(protocol::Runtime::CallArgument*,
                               v8::Local<v8::Value>* result);

  Response createExceptionDetails(
      const v8::TryCatch&, const String16& groupName,
      std::unique_ptr<protocol::Runtime::ExceptionDetails>* result);
  Response createExceptionDetails(
      v8::Local<v8::Message> message, v8::Local<v8::Value> exception,
      const String16& groupName,
      std::unique_ptr<protocol::Runtime::ExceptionDetails>* result);

  Response wrapEvaluateResult(
      v8::MaybeLocal<v8::Value> maybeResultValue, const v8::TryCatch&,
      const String16& objectGroup, const WrapOptions& wrapOptions,
      bool throwOnSideEffect,
      std::unique_ptr<protocol::Runtime::RemoteObject>* result,
      std::unique_ptr<protocol::Runtime::ExceptionDetails>*);
  v8::Local<v8::Value> lastEvaluationResult() const;
  void setLastEvaluationResult(v8::Local<v8::Value> result);

  class Scope {
   public:
    Response initialize();
    void installCommandLineAPI();
    void ignoreExceptionsAndMuteConsole();
    void pretendUserGesture();
    void allowCodeGenerationFromStrings();
    void setTryCatchVerbose();
    v8::Local<v8::Context> context() const { return m_context; }
    InjectedScript* injectedScript() const { return m_injectedScript; }
    const v8::TryCatch& tryCatch() const { return m_tryCatch; }
    V8InspectorImpl* inspector() const { return m_inspector; }

   protected:
    explicit Scope(V8InspectorSessionImpl*);
    virtual ~Scope();
    virtual Response findInjectedScript(V8InspectorSessionImpl*) = 0;

    V8InspectorImpl* m_inspector;
    InjectedScript* m_injectedScript;

   private:
    void cleanup();
    v8::debug::ExceptionBreakState setPauseOnExceptionsState(
        v8::debug::ExceptionBreakState);

    v8::HandleScope m_handleScope;
    v8::TryCatch m_tryCatch;
    v8::Local<v8::Context> m_context;
    std::unique_ptr<V8Console::CommandLineAPIScope> m_commandLineAPIScope;
    bool m_ignoreExceptionsAndMuteConsole;
    v8::debug::ExceptionBreakState m_previousPauseOnExceptionsState;
    bool m_userGesture;
    bool m_allowEval;
    int m_contextGroupId;
    int m_sessionId;
  };

  class ContextScope : public Scope {
   public:
    ContextScope(V8InspectorSessionImpl*, int executionContextId);
    ~ContextScope() override;
    ContextScope(const ContextScope&) = delete;
    ContextScope& operator=(const ContextScope&) = delete;

   private:
    Response findInjectedScript(V8InspectorSessionImpl*) override;
    int m_executionContextId;
  };

  class ObjectScope : public Scope {
   public:
    ObjectScope(V8InspectorSessionImpl*, const String16& remoteObjectId);
    ~ObjectScope() override;
    ObjectScope(const ObjectScope&) = delete;
    ObjectScope& operator=(const ObjectScope&) = delete;
    const String16& objectGroupName() const { return m_objectGroupName; }
    v8::Local<v8::Value> object() const { return m_object; }

   private:
    Response findInjectedScript(V8InspectorSessionImpl*) override;
    String16 m_remoteObjectId;
    String16 m_objectGroupName;
    v8::Local<v8::Value> m_object;
  };

  class CallFrameScope : public Scope {
   public:
    CallFrameScope(V8InspectorSessionImpl*, const String16& remoteCallFrameId);
    ~CallFrameScope() override;
    CallFrameScope(const CallFrameScope&) = delete;
    CallFrameScope& operator=(const CallFrameScope&) = delete;
    size_t frameOrdinal() const { return m_frameOrdinal; }

   private:
    Response findInjectedScript(V8InspectorSessionImpl*) override;
    String16 m_remoteCallFrameId;
    size_t m_frameOrdinal;
  };
  String16 bindObject(v8::Local<v8::Value>, const String16& groupName);

 private:
  friend class EvaluateCallback;
  friend class PromiseHandlerTracker;

  v8::Local<v8::Object> commandLineAPI();
  void unbindObject(int id);

  static Response bindRemoteObjectIfNeeded(
      int sessionId, v8::Local<v8::Context> context, v8::Local<v8::Value>,
      const String16& groupName, protocol::Runtime::RemoteObject* remoteObject);

  class ProtocolPromiseHandler;
  void discardEvaluateCallbacks();
  void deleteEvaluateCallback(std::shared_ptr<EvaluateCallback> callback);
  Response addExceptionToDetails(
      v8::Local<v8::Value> exception,
      protocol::Runtime::ExceptionDetails* exceptionDetails,
      const String16& objectGroup);

  InspectedContext* m_context;
  int m_sessionId;
  v8::Global<v8::Value> m_lastEvaluationResult;
  v8::Global<v8::Object> m_commandLineAPI;
  int m_lastBoundObjectId = 1;
  std::unordered_map<int, v8::Global<v8::Value>> m_idToWrappedObject;
  std::unordered_map<int, String16> m_idToObjectGroupName;
  std::unordered_map<String16, std::vector<int>> m_nameToObjectGroup;
  std::unordered_set<std::shared_ptr<EvaluateCallback>> m_evaluateCallbacks;
  bool m_customPreviewEnabled = false;
};

// Owns and tracks the life-time of {ProtocolPromiseHandler} instances.
// Each Runtime#evaluate, Runtime#awaitPromise or Runtime#callFunctionOn
// can create a {ProtocolPromiseHandler} to send the CDP response once it's
// ready.
//
// A {ProtocolPromiseHandler} can be destroyed by various events:
//
//   1) The evaluation promise fulfills (and we send the CDP response).
//   2) The evaluation promise gets GC'ed
//   3) The {PromiseHandlerTracker} owning the {ProtocolPromiseHandler} dies.
//
// We keep the logic of {PromiseHandlerTracker} separate so it's
// easier to move it. E.g. we could keep it on the inspector, session or
// inspected context level.
class PromiseHandlerTracker {
 public:
  PromiseHandlerTracker();
  PromiseHandlerTracker(const PromiseHandlerTracker&) = delete;
  void operator=(const PromiseHandlerTracker&) = delete;
  ~PromiseHandlerTracker();

  // Any reason other then kFulfilled will send a CDP error response as to
  // not keep the request pending forever. Depending on when the
  // {PromiseHandlerTracker} is destructed, the {EvaluateCallback} might
  // already be dead and we can't send the error response (but that's fine).
  enum class DiscardReason {
    kFulfilled,
    kPromiseCollected,
    kTearDown,
  };
  using Id = int64_t;

  template <typename... Args>
  Id create(Args&&... args);
  void discard(Id id, DiscardReason reason);
  InjectedScript::ProtocolPromiseHandler* get(Id id) const;

 private:
  void sendFailure(InjectedScript::ProtocolPromiseHandler* handler,
                   const protocol::DispatchResponse& response) const;
  void discardAll();

  std::map<Id, std::unique_ptr<InjectedScript::ProtocolPromiseHandler>>
      m_promiseHandlers;
  Id m_lastUsedId = 1;
};

}  // namespace v8_inspector

#endif  // V8_INSPECTOR_INJECTED_SCRIPT_H_
