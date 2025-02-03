// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dispatch.h"

#include <cassert>
#include "cbor.h"
#include "error_support.h"
#include "find_by_first.h"
#include "frontend_channel.h"
#include "protocol_core.h"

namespace crdtp {
// =============================================================================
// DispatchResponse - Error status and chaining / fall through
// =============================================================================

// static
DispatchResponse DispatchResponse::Success() {
  DispatchResponse result;
  result.code_ = DispatchCode::SUCCESS;
  return result;
}

// static
DispatchResponse DispatchResponse::FallThrough() {
  DispatchResponse result;
  result.code_ = DispatchCode::FALL_THROUGH;
  return result;
}

// static
DispatchResponse DispatchResponse::ParseError(std::string message) {
  DispatchResponse result;
  result.code_ = DispatchCode::PARSE_ERROR;
  result.message_ = std::move(message);
  return result;
}

// static
DispatchResponse DispatchResponse::InvalidRequest(std::string message) {
  DispatchResponse result;
  result.code_ = DispatchCode::INVALID_REQUEST;
  result.message_ = std::move(message);
  return result;
}

// static
DispatchResponse DispatchResponse::MethodNotFound(std::string message) {
  DispatchResponse result;
  result.code_ = DispatchCode::METHOD_NOT_FOUND;
  result.message_ = std::move(message);
  return result;
}

// static
DispatchResponse DispatchResponse::InvalidParams(std::string message) {
  DispatchResponse result;
  result.code_ = DispatchCode::INVALID_PARAMS;
  result.message_ = std::move(message);
  return result;
}

// static
DispatchResponse DispatchResponse::InternalError() {
  DispatchResponse result;
  result.code_ = DispatchCode::INTERNAL_ERROR;
  result.message_ = "Internal error";
  return result;
}

// static
DispatchResponse DispatchResponse::ServerError(std::string message) {
  DispatchResponse result;
  result.code_ = DispatchCode::SERVER_ERROR;
  result.message_ = std::move(message);
  return result;
}

// static
DispatchResponse DispatchResponse::SessionNotFound(std::string message) {
  DispatchResponse result;
  result.code_ = DispatchCode::SESSION_NOT_FOUND;
  result.message_ = std::move(message);
  return result;
}

// =============================================================================
// Dispatchable - a shallow parser for CBOR encoded DevTools messages
// =============================================================================
Dispatchable::Dispatchable(span<uint8_t> serialized) : serialized_(serialized) {
  Status s = cbor::CheckCBORMessage(serialized);
  if (!s.ok()) {
    status_ = {Error::MESSAGE_MUST_BE_AN_OBJECT, s.pos};
    return;
  }
  cbor::CBORTokenizer tokenizer(serialized);
  if (tokenizer.TokenTag() == cbor::CBORTokenTag::ERROR_VALUE) {
    status_ = tokenizer.Status();
    return;
  }

  // We checked for the envelope start byte above, so the tokenizer
  // must agree here, since it's not an error.
  assert(tokenizer.TokenTag() == cbor::CBORTokenTag::ENVELOPE);

  // Before we enter the envelope, we save the position that we
  // expect to see after we're done parsing the envelope contents.
  // This way we can compare and produce an error if the contents
  // didn't fit exactly into the envelope length.
  const size_t pos_past_envelope =
      tokenizer.Status().pos + tokenizer.GetEnvelopeHeader().outer_size();
  tokenizer.EnterEnvelope();
  if (tokenizer.TokenTag() == cbor::CBORTokenTag::ERROR_VALUE) {
    status_ = tokenizer.Status();
    return;
  }
  if (tokenizer.TokenTag() != cbor::CBORTokenTag::MAP_START) {
    status_ = {Error::MESSAGE_MUST_BE_AN_OBJECT, tokenizer.Status().pos};
    return;
  }
  assert(tokenizer.TokenTag() == cbor::CBORTokenTag::MAP_START);
  tokenizer.Next();  // Now we should be pointed at the map key.
  while (tokenizer.TokenTag() != cbor::CBORTokenTag::STOP) {
    switch (tokenizer.TokenTag()) {
      case cbor::CBORTokenTag::DONE:
        status_ =
            Status{Error::CBOR_UNEXPECTED_EOF_IN_MAP, tokenizer.Status().pos};
        return;
      case cbor::CBORTokenTag::ERROR_VALUE:
        status_ = tokenizer.Status();
        return;
      case cbor::CBORTokenTag::STRING8:
        if (!MaybeParseProperty(&tokenizer))
          return;
        break;
      default:
        // We require the top-level keys to be UTF8 (US-ASCII in practice).
        status_ = Status{Error::CBOR_INVALID_MAP_KEY, tokenizer.Status().pos};
        return;
    }
  }
  tokenizer.Next();
  if (!has_call_id_) {
    status_ = Status{Error::MESSAGE_MUST_HAVE_INTEGER_ID_PROPERTY,
                     tokenizer.Status().pos};
    return;
  }
  if (method_.empty()) {
    status_ = Status{Error::MESSAGE_MUST_HAVE_STRING_METHOD_PROPERTY,
                     tokenizer.Status().pos};
    return;
  }
  // The contents of the envelope parsed OK, now check that we're at
  // the expected position.
  if (pos_past_envelope != tokenizer.Status().pos) {
    status_ = Status{Error::CBOR_ENVELOPE_CONTENTS_LENGTH_MISMATCH,
                     tokenizer.Status().pos};
    return;
  }
  if (tokenizer.TokenTag() != cbor::CBORTokenTag::DONE) {
    status_ = Status{Error::CBOR_TRAILING_JUNK, tokenizer.Status().pos};
    return;
  }
}

bool Dispatchable::ok() const {
  return status_.ok();
}

DispatchResponse Dispatchable::DispatchError() const {
  // TODO(johannes): Replace with DCHECK / similar?
  if (status_.ok())
    return DispatchResponse::Success();

  if (status_.IsMessageError())
    return DispatchResponse::InvalidRequest(status_.Message());
  return DispatchResponse::ParseError(status_.ToASCIIString());
}

bool Dispatchable::MaybeParseProperty(cbor::CBORTokenizer* tokenizer) {
  span<uint8_t> property_name = tokenizer->GetString8();
  if (SpanEquals(SpanFrom("id"), property_name))
    return MaybeParseCallId(tokenizer);
  if (SpanEquals(SpanFrom("method"), property_name))
    return MaybeParseMethod(tokenizer);
  if (SpanEquals(SpanFrom("params"), property_name))
    return MaybeParseParams(tokenizer);
  if (SpanEquals(SpanFrom("sessionId"), property_name))
    return MaybeParseSessionId(tokenizer);
  status_ =
      Status{Error::MESSAGE_HAS_UNKNOWN_PROPERTY, tokenizer->Status().pos};
  return false;
}

bool Dispatchable::MaybeParseCallId(cbor::CBORTokenizer* tokenizer) {
  if (has_call_id_) {
    status_ = Status{Error::CBOR_DUPLICATE_MAP_KEY, tokenizer->Status().pos};
    return false;
  }
  tokenizer->Next();
  if (tokenizer->TokenTag() != cbor::CBORTokenTag::INT32) {
    status_ = Status{Error::MESSAGE_MUST_HAVE_INTEGER_ID_PROPERTY,
                     tokenizer->Status().pos};
    return false;
  }
  call_id_ = tokenizer->GetInt32();
  has_call_id_ = true;
  tokenizer->Next();
  return true;
}

bool Dispatchable::MaybeParseMethod(cbor::CBORTokenizer* tokenizer) {
  if (!method_.empty()) {
    status_ = Status{Error::CBOR_DUPLICATE_MAP_KEY, tokenizer->Status().pos};
    return false;
  }
  tokenizer->Next();
  if (tokenizer->TokenTag() != cbor::CBORTokenTag::STRING8) {
    status_ = Status{Error::MESSAGE_MUST_HAVE_STRING_METHOD_PROPERTY,
                     tokenizer->Status().pos};
    return false;
  }
  method_ = tokenizer->GetString8();
  tokenizer->Next();
  return true;
}

bool Dispatchable::MaybeParseParams(cbor::CBORTokenizer* tokenizer) {
  if (params_seen_) {
    status_ = Status{Error::CBOR_DUPLICATE_MAP_KEY, tokenizer->Status().pos};
    return false;
  }
  params_seen_ = true;
  tokenizer->Next();
  if (tokenizer->TokenTag() == cbor::CBORTokenTag::NULL_VALUE) {
    tokenizer->Next();
    return true;
  }
  if (tokenizer->TokenTag() != cbor::CBORTokenTag::ENVELOPE) {
    status_ = Status{Error::MESSAGE_MAY_HAVE_OBJECT_PARAMS_PROPERTY,
                     tokenizer->Status().pos};
    return false;
  }
  params_ = tokenizer->GetEnvelope();
  tokenizer->Next();
  return true;
}

bool Dispatchable::MaybeParseSessionId(cbor::CBORTokenizer* tokenizer) {
  if (!session_id_.empty()) {
    status_ = Status{Error::CBOR_DUPLICATE_MAP_KEY, tokenizer->Status().pos};
    return false;
  }
  tokenizer->Next();
  if (tokenizer->TokenTag() != cbor::CBORTokenTag::STRING8) {
    status_ = Status{Error::MESSAGE_MAY_HAVE_STRING_SESSION_ID_PROPERTY,
                     tokenizer->Status().pos};
    return false;
  }
  session_id_ = tokenizer->GetString8();
  tokenizer->Next();
  return true;
}

namespace {
class ProtocolError : public Serializable {
 public:
  explicit ProtocolError(DispatchResponse dispatch_response)
      : dispatch_response_(std::move(dispatch_response)) {}

  void AppendSerialized(std::vector<uint8_t>* out) const override {
    Status status;
    std::unique_ptr<ParserHandler> encoder = cbor::NewCBOREncoder(out, &status);
    encoder->HandleMapBegin();
    if (has_call_id_) {
      encoder->HandleString8(SpanFrom("id"));
      encoder->HandleInt32(call_id_);
    }
    encoder->HandleString8(SpanFrom("error"));
    encoder->HandleMapBegin();
    encoder->HandleString8(SpanFrom("code"));
    encoder->HandleInt32(static_cast<int32_t>(dispatch_response_.Code()));
    encoder->HandleString8(SpanFrom("message"));
    encoder->HandleString8(SpanFrom(dispatch_response_.Message()));
    if (!data_.empty()) {
      encoder->HandleString8(SpanFrom("data"));
      encoder->HandleString8(SpanFrom(data_));
    }
    encoder->HandleMapEnd();
    encoder->HandleMapEnd();
    assert(status.ok());
  }

  void SetCallId(int call_id) {
    has_call_id_ = true;
    call_id_ = call_id;
  }
  void SetData(std::string data) { data_ = std::move(data); }

 private:
  const DispatchResponse dispatch_response_;
  std::string data_;
  int call_id_ = 0;
  bool has_call_id_ = false;
};
}  // namespace

// =============================================================================
// Helpers for creating protocol cresponses and notifications.
// =============================================================================

std::unique_ptr<Serializable> CreateErrorResponse(
    int call_id,
    DispatchResponse dispatch_response) {
  auto protocol_error =
      std::make_unique<ProtocolError>(std::move(dispatch_response));
  protocol_error->SetCallId(call_id);
  return protocol_error;
}

std::unique_ptr<Serializable> CreateErrorResponse(
    int call_id,
    DispatchResponse dispatch_response,
    const DeserializerState& state) {
  auto protocol_error =
      std::make_unique<ProtocolError>(std::move(dispatch_response));
  protocol_error->SetCallId(call_id);
  // TODO(caseq): should we plumb the call name here?
  protocol_error->SetData(state.ErrorMessage(MakeSpan("params")));
  return protocol_error;
}

std::unique_ptr<Serializable> CreateErrorNotification(
    DispatchResponse dispatch_response) {
  return std::make_unique<ProtocolError>(std::move(dispatch_response));
}

namespace {
class Response : public Serializable {
 public:
  Response(int call_id, std::unique_ptr<Serializable> params)
      : call_id_(call_id), params_(std::move(params)) {}

  void AppendSerialized(std::vector<uint8_t>* out) const override {
    Status status;
    std::unique_ptr<ParserHandler> encoder = cbor::NewCBOREncoder(out, &status);
    encoder->HandleMapBegin();
    encoder->HandleString8(SpanFrom("id"));
    encoder->HandleInt32(call_id_);
    encoder->HandleString8(SpanFrom("result"));
    if (params_) {
      params_->AppendSerialized(out);
    } else {
      encoder->HandleMapBegin();
      encoder->HandleMapEnd();
    }
    encoder->HandleMapEnd();
    assert(status.ok());
  }

 private:
  const int call_id_;
  std::unique_ptr<Serializable> params_;
};

class Notification : public Serializable {
 public:
  Notification(const char* method, std::unique_ptr<Serializable> params)
      : method_(method), params_(std::move(params)) {}

  void AppendSerialized(std::vector<uint8_t>* out) const override {
    Status status;
    std::unique_ptr<ParserHandler> encoder = cbor::NewCBOREncoder(out, &status);
    encoder->HandleMapBegin();
    encoder->HandleString8(SpanFrom("method"));
    encoder->HandleString8(SpanFrom(method_));
    encoder->HandleString8(SpanFrom("params"));
    if (params_) {
      params_->AppendSerialized(out);
    } else {
      encoder->HandleMapBegin();
      encoder->HandleMapEnd();
    }
    encoder->HandleMapEnd();
    assert(status.ok());
  }

 private:
  const char* method_;
  std::unique_ptr<Serializable> params_;
};
}  // namespace

std::unique_ptr<Serializable> CreateResponse(
    int call_id,
    std::unique_ptr<Serializable> params) {
  return std::make_unique<Response>(call_id, std::move(params));
}

std::unique_ptr<Serializable> CreateNotification(
    const char* method,
    std::unique_ptr<Serializable> params) {
  return std::make_unique<Notification>(method, std::move(params));
}

// =============================================================================
// DomainDispatcher - Dispatching betwen protocol methods within a domain.
// =============================================================================
DomainDispatcher::WeakPtr::WeakPtr(DomainDispatcher* dispatcher)
    : dispatcher_(dispatcher) {}

DomainDispatcher::WeakPtr::~WeakPtr() {
  if (dispatcher_)
    dispatcher_->weak_ptrs_.erase(this);
}

DomainDispatcher::Callback::~Callback() = default;

void DomainDispatcher::Callback::dispose() {
  backend_impl_ = nullptr;
}

DomainDispatcher::Callback::Callback(
    std::unique_ptr<DomainDispatcher::WeakPtr> backend_impl,
    int call_id,
    span<uint8_t> method,
    span<uint8_t> message)
    : backend_impl_(std::move(backend_impl)),
      call_id_(call_id),
      method_(method),
      message_(message.begin(), message.end()) {}

void DomainDispatcher::Callback::sendIfActive(
    std::unique_ptr<Serializable> partialMessage,
    const DispatchResponse& response) {
  if (!backend_impl_ || !backend_impl_->get())
    return;
  backend_impl_->get()->sendResponse(call_id_, response,
                                     std::move(partialMessage));
  backend_impl_ = nullptr;
}

void DomainDispatcher::Callback::fallThroughIfActive() {
  if (!backend_impl_ || !backend_impl_->get())
    return;
  backend_impl_->get()->channel()->FallThrough(call_id_, method_,
                                               SpanFrom(message_));
  backend_impl_ = nullptr;
}

DomainDispatcher::DomainDispatcher(FrontendChannel* frontendChannel)
    : frontend_channel_(frontendChannel) {}

DomainDispatcher::~DomainDispatcher() {
  clearFrontend();
}

void DomainDispatcher::sendResponse(int call_id,
                                    const DispatchResponse& response,
                                    std::unique_ptr<Serializable> result) {
  if (!frontend_channel_)
    return;
  std::unique_ptr<Serializable> serializable;
  if (response.IsError()) {
    serializable = CreateErrorResponse(call_id, response);
  } else {
    serializable = CreateResponse(call_id, std::move(result));
  }
  frontend_channel_->SendProtocolResponse(call_id, std::move(serializable));
}

void DomainDispatcher::ReportInvalidParams(const Dispatchable& dispatchable,
                                           const DeserializerState& state) {
  assert(!state.status().ok());
  if (frontend_channel_) {
    frontend_channel_->SendProtocolResponse(
        dispatchable.CallId(),
        CreateErrorResponse(
            dispatchable.CallId(),
            DispatchResponse::InvalidParams("Invalid parameters"), state));
  }
}

void DomainDispatcher::clearFrontend() {
  frontend_channel_ = nullptr;
  for (auto& weak : weak_ptrs_)
    weak->dispose();
  weak_ptrs_.clear();
}

std::unique_ptr<DomainDispatcher::WeakPtr> DomainDispatcher::weakPtr() {
  auto weak = std::make_unique<DomainDispatcher::WeakPtr>(this);
  weak_ptrs_.insert(weak.get());
  return weak;
}

// =============================================================================
// UberDispatcher - dispatches between domains (backends).
// =============================================================================
UberDispatcher::DispatchResult::DispatchResult(bool method_found,
                                               std::function<void()> runnable)
    : method_found_(method_found), runnable_(runnable) {}

void UberDispatcher::DispatchResult::Run() {
  if (!runnable_)
    return;
  runnable_();
  runnable_ = nullptr;
}

UberDispatcher::UberDispatcher(FrontendChannel* frontend_channel)
    : frontend_channel_(frontend_channel) {
  assert(frontend_channel);
}

UberDispatcher::~UberDispatcher() = default;

constexpr size_t kNotFound = std::numeric_limits<size_t>::max();

namespace {
size_t DotIdx(span<uint8_t> method) {
  const void* p = memchr(method.data(), '.', method.size());
  return p ? reinterpret_cast<const uint8_t*>(p) - method.data() : kNotFound;
}
}  // namespace

UberDispatcher::DispatchResult UberDispatcher::Dispatch(
    const Dispatchable& dispatchable) const {
  span<uint8_t> method = FindByFirst(redirects_, dispatchable.Method(),
                                     /*default_value=*/dispatchable.Method());
  size_t dot_idx = DotIdx(method);
  if (dot_idx != kNotFound) {
    span<uint8_t> domain = method.subspan(0, dot_idx);
    span<uint8_t> command = method.subspan(dot_idx + 1);
    DomainDispatcher* dispatcher = FindByFirst(dispatchers_, domain);
    if (dispatcher) {
      std::function<void(const Dispatchable&)> dispatched =
          dispatcher->Dispatch(command);
      if (dispatched) {
        return DispatchResult(
            true, [dispatchable, dispatched = std::move(dispatched)]() {
              dispatched(dispatchable);
            });
      }
    }
  }
  return DispatchResult(false, [this, dispatchable]() {
    frontend_channel_->SendProtocolResponse(
        dispatchable.CallId(),
        CreateErrorResponse(dispatchable.CallId(),
                            DispatchResponse::MethodNotFound(
                                "'" +
                                std::string(dispatchable.Method().begin(),
                                            dispatchable.Method().end()) +
                                "' wasn't found")));
  });
}

template <typename T>
struct FirstLessThan {
  bool operator()(const std::pair<span<uint8_t>, T>& left,
                  const std::pair<span<uint8_t>, T>& right) {
    return SpanLessThan(left.first, right.first);
  }
};

void UberDispatcher::WireBackend(
    span<uint8_t> domain,
    const std::vector<std::pair<span<uint8_t>, span<uint8_t>>>&
        sorted_redirects,
    std::unique_ptr<DomainDispatcher> dispatcher) {
  auto it = redirects_.insert(redirects_.end(), sorted_redirects.begin(),
                              sorted_redirects.end());
  std::inplace_merge(redirects_.begin(), it, redirects_.end(),
                     FirstLessThan<span<uint8_t>>());
  auto jt = dispatchers_.insert(dispatchers_.end(),
                                std::make_pair(domain, std::move(dispatcher)));
  std::inplace_merge(dispatchers_.begin(), jt, dispatchers_.end(),
                     FirstLessThan<std::unique_ptr<DomainDispatcher>>());
}

}  // namespace crdtp
