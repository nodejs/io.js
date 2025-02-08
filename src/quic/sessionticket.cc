#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC

#include "sessionticket.h"
#include <env-inl.h>
#include <memory_tracker-inl.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <node_buffer.h>
#include <node_errors.h>

namespace node {

using v8::ArrayBufferView;
using v8::Just;
using v8::Local;
using v8::Maybe;
using v8::MaybeLocal;
using v8::Nothing;
using v8::Object;
using v8::Value;
using v8::ValueDeserializer;
using v8::ValueSerializer;

namespace quic {

namespace {
SessionTicket::AppData::Source* GetAppDataSource(SSL* ssl) {
  ngtcp2_crypto_conn_ref* ref =
      static_cast<ngtcp2_crypto_conn_ref*>(SSL_get_app_data(ssl));
  if (ref != nullptr && ref->user_data != nullptr) {
    return static_cast<SessionTicket::AppData::Source*>(ref->user_data);
  }
  return nullptr;
}
}  // namespace

SessionTicket::SessionTicket(Store&& ticket, Store&& transport_params)
    : ticket_(std::move(ticket)),
      transport_params_(std::move(transport_params)) {}

Maybe<SessionTicket> SessionTicket::FromV8Value(Environment* env,
                                                Local<Value> value) {
  if (!value->IsArrayBufferView()) {
    THROW_ERR_INVALID_ARG_TYPE(env, "The ticket must be an ArrayBufferView.");
    return Nothing<SessionTicket>();
  }

  Store content(value.As<ArrayBufferView>());
  ngtcp2_vec vec = content;

  ValueDeserializer des(env->isolate(), vec.base, vec.len);

  if (des.ReadHeader(env->context()).IsNothing()) {
    THROW_ERR_INVALID_ARG_VALUE(env, "The ticket format is invalid.");
    return Nothing<SessionTicket>();
  }

  Local<Value> ticket;
  Local<Value> transport_params;

  errors::TryCatchScope tryCatch(env);

  if (!des.ReadValue(env->context()).ToLocal(&ticket) ||
      !des.ReadValue(env->context()).ToLocal(&transport_params) ||
      !ticket->IsArrayBufferView() || !transport_params->IsArrayBufferView()) {
    if (tryCatch.HasCaught()) {
      // Any errors thrown we want to catch and suppress. The only
      // error we want to expose to the user is that the ticket format
      // is invalid.
      if (!tryCatch.HasTerminated()) {
        THROW_ERR_INVALID_ARG_VALUE(env, "The ticket format is invalid.");
        tryCatch.ReThrow();
      }
      return Nothing<SessionTicket>();
    }
    THROW_ERR_INVALID_ARG_VALUE(env, "The ticket format is invalid.");
    return Nothing<SessionTicket>();
  }

  return Just(SessionTicket(Store(ticket.As<ArrayBufferView>()),
                            Store(transport_params.As<ArrayBufferView>())));
}

MaybeLocal<Object> SessionTicket::encode(Environment* env) const {
  auto context = env->context();
  ValueSerializer ser(env->isolate());
  ser.WriteHeader();

  if (ser.WriteValue(context, ticket_.ToUint8Array(env)).IsNothing() ||
      ser.WriteValue(context, transport_params_.ToUint8Array(env))
          .IsNothing()) {
    return MaybeLocal<Object>();
  }

  auto result = ser.Release();

  return Buffer::New(env, reinterpret_cast<char*>(result.first), result.second);
}

const uv_buf_t SessionTicket::ticket() const {
  return ticket_;
}

const ngtcp2_vec SessionTicket::transport_params() const {
  return transport_params_;
}

void SessionTicket::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("ticket", ticket_);
  tracker->TrackField("transport_params", transport_params_);
}

int SessionTicket::GenerateCallback(SSL* ssl, void* arg) {
  AppData::Collect(ssl);
  return 1;
}

SSL_TICKET_RETURN SessionTicket::DecryptedCallback(SSL* ssl,
                                                   SSL_SESSION* session,
                                                   const unsigned char* keyname,
                                                   size_t keyname_len,
                                                   SSL_TICKET_STATUS status,
                                                   void* arg) {
  switch (status) {
    default:
      return SSL_TICKET_RETURN_IGNORE;
    case SSL_TICKET_EMPTY:
      [[fallthrough]];
    case SSL_TICKET_NO_DECRYPT:
      return SSL_TICKET_RETURN_IGNORE_RENEW;
    case SSL_TICKET_SUCCESS_RENEW:
      [[fallthrough]];
    case SSL_TICKET_SUCCESS:
      return static_cast<SSL_TICKET_RETURN>(AppData::Extract(ssl));
  }
}

SessionTicket::AppData::AppData(SSL* ssl) : ssl_(ssl) {}

bool SessionTicket::AppData::Set(const uv_buf_t& data) {
  if (set_ || data.base == nullptr || data.len == 0) return false;
  set_ = true;
  SSL_SESSION_set1_ticket_appdata(SSL_get0_session(ssl_), data.base, data.len);
  return set_;
}

std::optional<const uv_buf_t> SessionTicket::AppData::Get() const {
  uv_buf_t buf;
  int ret =
      SSL_SESSION_get0_ticket_appdata(SSL_get0_session(ssl_),
                                      reinterpret_cast<void**>(&buf.base),
                                      reinterpret_cast<size_t*>(&buf.len));
  if (ret != 1) return std::nullopt;
  return buf;
}

void SessionTicket::AppData::Collect(SSL* ssl) {
  AppData app_data(ssl);
  if (auto source = GetAppDataSource(ssl)) {
    source->CollectSessionTicketAppData(&app_data);
  }
}

SessionTicket::AppData::Status SessionTicket::AppData::Extract(SSL* ssl) {
  auto source = GetAppDataSource(ssl);
  if (source != nullptr) {
    AppData app_data(ssl);
    return source->ExtractSessionTicketAppData(app_data);
  }
  return Status::TICKET_IGNORE;
}

}  // namespace quic
}  // namespace node

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
