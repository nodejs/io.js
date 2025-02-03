// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRDTP_DISPATCH_H_
#define CRDTP_DISPATCH_H_

#include <cassert>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include "export.h"
#include "serializable.h"
#include "span.h"
#include "status.h"

namespace crdtp {
class DeserializerState;
class ErrorSupport;
class FrontendChannel;
namespace cbor {
class CBORTokenizer;
}  // namespace cbor

// =============================================================================
// DispatchResponse - Error status and chaining / fall through
// =============================================================================
enum class DispatchCode {
  SUCCESS = 1,
  FALL_THROUGH = 2,
  // For historical reasons, these error codes correspond to commonly used
  // XMLRPC codes (e.g. see METHOD_NOT_FOUND in
  // https://github.com/python/cpython/blob/main/Lib/xmlrpc/client.py).
  PARSE_ERROR = -32700,
  INVALID_REQUEST = -32600,
  METHOD_NOT_FOUND = -32601,
  INVALID_PARAMS = -32602,
  INTERNAL_ERROR = -32603,
  SERVER_ERROR = -32000,
  SESSION_NOT_FOUND = SERVER_ERROR - 1,
};

// Information returned by command handlers. Usually returned after command
// execution attempts.
class CRDTP_EXPORT DispatchResponse {
 public:
  const std::string& Message() const { return message_; }

  DispatchCode Code() const { return code_; }

  bool IsSuccess() const { return code_ == DispatchCode::SUCCESS; }
  bool IsFallThrough() const { return code_ == DispatchCode::FALL_THROUGH; }
  bool IsError() const { return code_ < DispatchCode::SUCCESS; }

  static DispatchResponse Success();
  static DispatchResponse FallThrough();

  // Indicates that a message could not be parsed. E.g., malformed JSON.
  static DispatchResponse ParseError(std::string message);

  // Indicates that a request is lacking required top-level properties
  // ('id', 'method'), has top-level properties of the wrong type, or has
  // unknown top-level properties.
  static DispatchResponse InvalidRequest(std::string message);

  // Indicates that a protocol method such as "Page.bringToFront" could not be
  // dispatched because it's not known to the (domain) dispatcher.
  static DispatchResponse MethodNotFound(std::string message);

  // Indicates that the params sent to a domain handler are invalid.
  static DispatchResponse InvalidParams(std::string message);

  // Used for application level errors, e.g. within protocol agents.
  static DispatchResponse InternalError();

  // Used for application level errors, e.g. within protocol agents.
  static DispatchResponse ServerError(std::string message);

  // Indicate that session with the id specified in the protocol message
  // was not found (e.g. because it has already been detached).
  static DispatchResponse SessionNotFound(std::string message);

 private:
  DispatchResponse() = default;
  DispatchCode code_;
  std::string message_;
};

// =============================================================================
// Dispatchable - a shallow parser for CBOR encoded DevTools messages
// =============================================================================

// This parser extracts only the known top-level fields from a CBOR encoded map;
// method, id, sessionId, and params.
class CRDTP_EXPORT Dispatchable {
 public:
  // This constructor parses the |serialized| message. If successful,
  // |ok()| will yield |true|, and |Method()|, |SessionId()|, |CallId()|,
  // |Params()| can be used to access, the extracted contents. Otherwise,
  // |ok()| will yield |false|, and |DispatchError()| can be
  // used to send a response or notification to the client.
  explicit Dispatchable(span<uint8_t> serialized);

  // The serialized message that we just parsed.
  span<uint8_t> Serialized() const { return serialized_; }

  // Yields true if parsing was successful. This is cheaper than calling
  // ::DispatchError().
  bool ok() const;

  // If !ok(), returns a DispatchResponse with appropriate code and error
  // which can be sent to the client as a response or notification.
  DispatchResponse DispatchError() const;

  // Top level field: the command to be executed, fully qualified by
  // domain. E.g. "Page.createIsolatedWorld".
  span<uint8_t> Method() const { return method_; }
  // Used to identify protocol connections attached to a specific
  // target. See Target.attachToTarget, Target.setAutoAttach.
  span<uint8_t> SessionId() const { return session_id_; }
  // The call id, a sequence number that's used in responses to indicate
  // the request to which the response belongs.
  int32_t CallId() const { return call_id_; }
  bool HasCallId() const { return has_call_id_; }
  // The payload of the request in CBOR format. The |Dispatchable| parser does
  // not parse into this; it only provides access to its raw contents here.
  span<uint8_t> Params() const { return params_; }

 private:
  bool MaybeParseProperty(cbor::CBORTokenizer* tokenizer);
  bool MaybeParseCallId(cbor::CBORTokenizer* tokenizer);
  bool MaybeParseMethod(cbor::CBORTokenizer* tokenizer);
  bool MaybeParseParams(cbor::CBORTokenizer* tokenizer);
  bool MaybeParseSessionId(cbor::CBORTokenizer* tokenizer);

  span<uint8_t> serialized_;

  Status status_;

  bool has_call_id_ = false;
  int32_t call_id_;
  span<uint8_t> method_;
  bool params_seen_ = false;
  span<uint8_t> params_;
  span<uint8_t> session_id_;
};

// =============================================================================
// Helpers for creating protocol cresponses and notifications.
// =============================================================================

// The resulting notifications can be sent to a protocol client,
// usually via a FrontendChannel (see frontend_channel.h).

CRDTP_EXPORT std::unique_ptr<Serializable> CreateErrorResponse(
    int callId,
    DispatchResponse dispatch_response);

CRDTP_EXPORT std::unique_ptr<Serializable> CreateErrorNotification(
    DispatchResponse dispatch_response);

CRDTP_EXPORT std::unique_ptr<Serializable> CreateResponse(
    int callId,
    std::unique_ptr<Serializable> params);

CRDTP_EXPORT std::unique_ptr<Serializable> CreateNotification(
    const char* method,
    std::unique_ptr<Serializable> params = nullptr);

// =============================================================================
// DomainDispatcher - Dispatching betwen protocol methods within a domain.
// =============================================================================

// This class is subclassed by |DomainDispatcherImpl|, which we generate per
// DevTools domain. It contains routines called from the generated code,
// e.g. ::MaybeReportInvalidParams, which are optimized for small code size.
// The most important method is ::Dispatch, which implements method dispatch
// by command name lookup.
class CRDTP_EXPORT DomainDispatcher {
 public:
  class CRDTP_EXPORT WeakPtr {
   public:
    explicit WeakPtr(DomainDispatcher*);
    ~WeakPtr();
    DomainDispatcher* get() { return dispatcher_; }
    void dispose() { dispatcher_ = nullptr; }

   private:
    DomainDispatcher* dispatcher_;
  };

  class CRDTP_EXPORT Callback {
   public:
    virtual ~Callback();
    void dispose();

   protected:
    // |method| must point at static storage (a C++ string literal in practice).
    Callback(std::unique_ptr<WeakPtr> backend_impl,
             int call_id,
             span<uint8_t> method,
             span<uint8_t> message);

    void sendIfActive(std::unique_ptr<Serializable> partialMessage,
                      const DispatchResponse& response);
    void fallThroughIfActive();

   private:
    std::unique_ptr<WeakPtr> backend_impl_;
    int call_id_;
    // Subclasses of this class are instantiated from generated code which
    // passes a string literal for the method name to the constructor. So the
    // storage for |method| is the binary of the running process.
    span<uint8_t> method_;
    std::vector<uint8_t> message_;
  };

  explicit DomainDispatcher(FrontendChannel*);
  virtual ~DomainDispatcher();

  // Given a |command_name| without domain qualification, looks up the
  // corresponding method. If the method is not found, returns nullptr.
  // Otherwise, Returns a closure that will parse the provided
  // Dispatchable.params() to a protocol object and execute the
  // apprpropriate method. If the parsing fails it will issue an
  // error response on the frontend channel, otherwise it will execute the
  // command.
  virtual std::function<void(const Dispatchable&)> Dispatch(
      span<uint8_t> command_name) = 0;

  // Sends a response to the client via the channel.
  void sendResponse(int call_id,
                    const DispatchResponse&,
                    std::unique_ptr<Serializable> result = nullptr);

  void ReportInvalidParams(const Dispatchable& dispatchable,
                           const DeserializerState& state);

  FrontendChannel* channel() { return frontend_channel_; }

  void clearFrontend();

  std::unique_ptr<WeakPtr> weakPtr();

 private:
  FrontendChannel* frontend_channel_;
  std::unordered_set<WeakPtr*> weak_ptrs_;
};

// =============================================================================
// UberDispatcher - dispatches between domains (backends).
// =============================================================================
class CRDTP_EXPORT UberDispatcher {
 public:
  // Return type for ::Dispatch.
  class CRDTP_EXPORT DispatchResult {
   public:
    DispatchResult(bool method_found, std::function<void()> runnable);

    // Indicates whether the method was found, that is, it could be dispatched
    // to a backend registered with this dispatcher.
    bool MethodFound() const { return method_found_; }

    // Runs the dispatched result. This will send the appropriate error
    // responses if the method wasn't found or if something went wrong during
    // parameter parsing.
    void Run();

   private:
    bool method_found_;
    std::function<void()> runnable_;
  };

  // |frontend_hannel| can't be nullptr.
  explicit UberDispatcher(FrontendChannel* frontend_channel);
  virtual ~UberDispatcher();

  // Dispatches the provided |dispatchable| considering all redirects and domain
  // handlers registered with this uber dispatcher. Also see |DispatchResult|.
  // |dispatchable.ok()| must hold - callers must check this separately and
  // deal with errors.
  DispatchResult Dispatch(const Dispatchable& dispatchable) const;

  // Invoked from generated code for wiring domain backends; that is,
  // connecting domain handlers to an uber dispatcher.
  // See <domain-namespace>::Dispatcher::Wire(UberDispatcher*,Backend*).
  FrontendChannel* channel() const {
    assert(frontend_channel_);
    return frontend_channel_;
  }

  // Invoked from generated code for wiring domain backends; that is,
  // connecting domain handlers to an uber dispatcher.
  // See <domain-namespace>::Dispatcher::Wire(UberDispatcher*,Backend*).
  void WireBackend(span<uint8_t> domain,
                   const std::vector<std::pair<span<uint8_t>, span<uint8_t>>>&,
                   std::unique_ptr<DomainDispatcher> dispatcher);

 private:
  DomainDispatcher* findDispatcher(span<uint8_t> method);
  FrontendChannel* const frontend_channel_;
  // Pairs of ascii strings of the form ("Domain1.method1","Domain2.method2")
  // indicating that the first element of each pair redirects to the second.
  // Sorted by first element.
  std::vector<std::pair<span<uint8_t>, span<uint8_t>>> redirects_;
  // Domain dispatcher instances, sorted by their domain name.
  std::vector<std::pair<span<uint8_t>, std::unique_ptr<DomainDispatcher>>>
      dispatchers_;
};
}  // namespace crdtp

#endif  // CRDTP_DISPATCH_H_
