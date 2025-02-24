#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC

#include "tlscontext.h"
#include <async_wrap-inl.h>
#include <base_object-inl.h>
#include <crypto/crypto_util.h>
#include <debug_utils-inl.h>
#include <env-inl.h>
#include <memory_tracker-inl.h>
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_quictls.h>
#include <node_sockaddr-inl.h>
#include <openssl/ssl.h>
#include <v8.h>
#include "bindingdata.h"
#include "defs.h"
#include "session.h"
#include "transportparams.h"

namespace node {

using ncrypto::BIOPointer;
using ncrypto::ClearErrorOnReturn;
using ncrypto::MarkPopErrorOnReturn;
using ncrypto::SSLCtxPointer;
using ncrypto::SSLPointer;
using ncrypto::SSLSessionPointer;
using ncrypto::X509Pointer;
using v8::ArrayBuffer;
using v8::Just;
using v8::Local;
using v8::Maybe;
using v8::MaybeLocal;
using v8::Nothing;
using v8::Object;
using v8::Undefined;
using v8::Value;

namespace quic {

// ============================================================================

namespace {
// TODO(@jasnell): One time initialization. ngtcp2 says this is optional but
// highly recommended to deal with some perf regression. Unfortunately doing
// this breaks some existing tests and we need to understand the potential
// impact of calling this.
// auto _ = []() {
//   CHECK_EQ(ngtcp2_crypto_quictls_init(), 0);
//   return 0;
// }();

void EnableTrace(Environment* env, BIOPointer* bio, SSL* ssl) {
#if HAVE_SSL_TRACE
  static bool warn_trace_tls = true;
  if (warn_trace_tls) {
    warn_trace_tls = false;
    ProcessEmitWarning(env,
                       "Enabling --trace-tls can expose sensitive data in "
                       "the resulting log");
  }
  if (!*bio) {
    bio->reset(BIO_new_fp(stderr, BIO_NOCLOSE | BIO_FP_TEXT));
    SSL_set_msg_callback(
        ssl,
        [](int write_p,
           int version,
           int content_type,
           const void* buf,
           size_t len,
           SSL* ssl,
           void* arg) -> void {
          MarkPopErrorOnReturn mark_pop_error_on_return;
          SSL_trace(write_p, version, content_type, buf, len, ssl, arg);
        });
    SSL_set_msg_callback_arg(ssl, bio->get());
  }
#endif
}

template <typename T, typename Opt, std::vector<T> Opt::*member>
bool SetOption(Environment* env,
               Opt* options,
               const Local<Object>& object,
               const Local<v8::String>& name) {
  Local<Value> value;
  if (!object->Get(env->context(), name).ToLocal(&value)) return false;

  if (value->IsUndefined()) return true;

  // The value can be either a single item or an array of items.

  if (value->IsArray()) {
    auto context = env->context();
    auto values = value.As<v8::Array>();
    uint32_t count = values->Length();
    for (uint32_t n = 0; n < count; n++) {
      Local<Value> item;
      if (!values->Get(context, n).ToLocal(&item)) {
        return false;
      }
      if constexpr (std::is_same<T, crypto::KeyObjectData>::value) {
        if (crypto::KeyObjectHandle::HasInstance(env, item)) {
          crypto::KeyObjectHandle* handle;
          ASSIGN_OR_RETURN_UNWRAP(&handle, item, false);
          (options->*member).push_back(handle->Data().addRef());
        } else {
          Utf8Value namestr(env->isolate(), name);
          THROW_ERR_INVALID_ARG_TYPE(
              env, "%s value must be a key object", *namestr);
          return false;
        }
      } else if constexpr (std::is_same<T, Store>::value) {
        if (item->IsArrayBufferView()) {
          (options->*member).emplace_back(item.As<v8::ArrayBufferView>());
        } else if (item->IsArrayBuffer()) {
          (options->*member).emplace_back(item.As<ArrayBuffer>());
        } else {
          Utf8Value namestr(env->isolate(), name);
          THROW_ERR_INVALID_ARG_TYPE(
              env, "%s value must be an array buffer", *namestr);
          return false;
        }
      }
    }
  } else {
    if constexpr (std::is_same<T, crypto::KeyObjectData>::value) {
      if (crypto::KeyObjectHandle::HasInstance(env, value)) {
        crypto::KeyObjectHandle* handle;
        ASSIGN_OR_RETURN_UNWRAP(&handle, value, false);
        (options->*member).push_back(handle->Data().addRef());
      } else {
        Utf8Value namestr(env->isolate(), name);
        THROW_ERR_INVALID_ARG_TYPE(
            env, "%s value must be a key object", *namestr);
        return false;
      }
    } else if constexpr (std::is_same<T, Store>::value) {
      if (value->IsArrayBufferView()) {
        (options->*member).emplace_back(value.As<v8::ArrayBufferView>());
      } else if (value->IsArrayBuffer()) {
        (options->*member).emplace_back(value.As<ArrayBuffer>());
      } else {
        Utf8Value namestr(env->isolate(), name);
        THROW_ERR_INVALID_ARG_TYPE(
            env, "%s value must be an array buffer", *namestr);
        return false;
      }
    }
  }
  return true;
}
}  // namespace

std::shared_ptr<TLSContext> TLSContext::CreateClient(const Options& options) {
  return std::make_shared<TLSContext>(Side::CLIENT, options);
}

std::shared_ptr<TLSContext> TLSContext::CreateServer(const Options& options) {
  return std::make_shared<TLSContext>(Side::SERVER, options);
}

TLSContext::TLSContext(Side side, const Options& options)
    : side_(side), options_(options), ctx_(Initialize()) {}

TLSContext::operator SSL_CTX*() const {
  DCHECK(ctx_);
  return ctx_.get();
}

int TLSContext::OnSelectAlpn(SSL* ssl,
                             const unsigned char** out,
                             unsigned char* outlen,
                             const unsigned char* in,
                             unsigned int inlen,
                             void* arg) {
  static constexpr size_t kMaxAlpnLen = 255;
  auto& session = TLSSession::From(ssl);

  const auto& requested = session.context().options().protocol;
  if (requested.length() > kMaxAlpnLen) return SSL_TLSEXT_ERR_NOACK;

  // The Session supports exactly one ALPN identifier. If that does not match
  // any of the ALPN identifiers provided in the client request, then we fail
  // here. Note that this will not fail the TLS handshake, so we have to check
  // later if the ALPN matches the expected identifier or not.
  //
  // We might eventually want to support the ability to negotiate multiple
  // possible ALPN's on a single endpoint/session but for now, we only support
  // one.
  if (SSL_select_next_proto(
          const_cast<unsigned char**>(out),
          outlen,
          reinterpret_cast<const unsigned char*>(requested.data()),
          requested.length(),
          in,
          inlen) == OPENSSL_NPN_NO_OVERLAP) {
    Debug(&session.session(), "ALPN negotiation failed");
    return SSL_TLSEXT_ERR_NOACK;
  }

  Debug(&session.session(), "ALPN negotiation succeeded");
  return SSL_TLSEXT_ERR_OK;
}

int TLSContext::OnNewSession(SSL* ssl, SSL_SESSION* sess) {
  auto& session = TLSSession::From(ssl).session();

  // If there is nothing listening for the session ticket, do not bother.
  if (session.wants_session_ticket()) {
    Debug(&session, "Preparing TLS session resumption ticket");

    // Pre-fight to see how much space we need to allocate for the session
    // ticket.
    size_t size = i2d_SSL_SESSION(sess, nullptr);

    // If size is 0 or the size is greater than our max, let's ignore it
    // and continue without emitting the sessionticket event.
    if (size > 0 && size <= crypto::SecureContext::kMaxSessionSize) {
      auto ticket =
          ArrayBuffer::NewBackingStore(session.env()->isolate(), size);
      auto data = reinterpret_cast<unsigned char*>(ticket->Data());
      if (i2d_SSL_SESSION(sess, &data) > 0) {
        session.EmitSessionTicket(Store(std::move(ticket), size));
      }
    }
  }

  return 0;
}

void TLSContext::OnKeylog(const SSL* ssl, const char* line) {
  TLSSession::From(ssl).session().EmitKeylog(line);
}

int TLSContext::OnVerifyClientCertificate(int preverify_ok,
                                          X509_STORE_CTX* ctx) {
  // TODO(@jasnell): Implement the logic to verify the client certificate
  return 1;
}

std::unique_ptr<TLSSession> TLSContext::NewSession(
    Session* session, const std::optional<SessionTicket>& maybeSessionTicket) {
  // Passing a session ticket only makes sense with a client session.
  CHECK_IMPLIES(session->is_server(), !maybeSessionTicket.has_value());
  return std::make_unique<TLSSession>(
      session, shared_from_this(), maybeSessionTicket);
}

SSLCtxPointer TLSContext::Initialize() {
  SSLCtxPointer ctx;
  switch (side_) {
    case Side::SERVER: {
      static constexpr unsigned char kSidCtx[] = "Node.js QUIC Server";
      ctx = SSLCtxPointer::NewServer();
      CHECK_EQ(ngtcp2_crypto_quictls_configure_server_context(ctx.get()), 0);
      CHECK_EQ(SSL_CTX_set_max_early_data(ctx.get(), UINT32_MAX), 1);
      SSL_CTX_set_options(ctx.get(),
                          (SSL_OP_ALL & ~SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS) |
                              SSL_OP_SINGLE_ECDH_USE |
                              SSL_OP_CIPHER_SERVER_PREFERENCE |
                              SSL_OP_NO_ANTI_REPLAY);
      SSL_CTX_set_mode(ctx.get(), SSL_MODE_RELEASE_BUFFERS);
      SSL_CTX_set_alpn_select_cb(ctx.get(), OnSelectAlpn, this);
      CHECK_EQ(SSL_CTX_set_session_id_context(
                   ctx.get(), kSidCtx, sizeof(kSidCtx) - 1),
               1);

      if (options_.verify_client) {
        SSL_CTX_set_verify(ctx.get(),
                           SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE |
                               SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                           OnVerifyClientCertificate);
      }

      // TODO(@jasnell): There's a bug int the GenerateCallback flow somewhere.
      // Need to update in order to support session tickets.
      // CHECK_EQ(SSL_CTX_set_session_ticket_cb(ctx.get(),
      //                                        SessionTicket::GenerateCallback,
      //                                        SessionTicket::DecryptedCallback,
      //                                        nullptr),
      //          1);
      break;
    }
    case Side::CLIENT: {
      ctx = SSLCtxPointer::NewClient();
      CHECK_EQ(ngtcp2_crypto_quictls_configure_client_context(ctx.get()), 0);

      SSL_CTX_set_session_cache_mode(
          ctx.get(), SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL);
      SSL_CTX_sess_set_new_cb(ctx.get(), OnNewSession);
      break;
    }
  }

  SSL_CTX_set_default_verify_paths(ctx.get());
  SSL_CTX_set_keylog_callback(ctx.get(), OnKeylog);

  if (SSL_CTX_set_ciphersuites(ctx.get(), options_.ciphers.c_str()) != 1) {
    validation_error_ = "Invalid cipher suite";
    return SSLCtxPointer();
  }

  if (SSL_CTX_set1_groups_list(ctx.get(), options_.groups.c_str()) != 1) {
    validation_error_ = "Invalid cipher groups";
    return SSLCtxPointer();
  }

  {
    ClearErrorOnReturn clear_error_on_return;
    if (options_.ca.empty()) {
      auto store = crypto::GetOrCreateRootCertStore();
      X509_STORE_up_ref(store);
      SSL_CTX_set_cert_store(ctx.get(), store);
    } else {
      for (const auto& ca : options_.ca) {
        uv_buf_t buf = ca;
        if (buf.len == 0) {
          auto store = crypto::GetOrCreateRootCertStore();
          X509_STORE_up_ref(store);
          SSL_CTX_set_cert_store(ctx.get(), store);
        } else {
          BIOPointer bio = crypto::NodeBIO::NewFixed(buf.base, buf.len);
          CHECK(bio);
          X509_STORE* cert_store = SSL_CTX_get_cert_store(ctx.get());
          while (
              auto x509 = X509Pointer(PEM_read_bio_X509_AUX(
                  bio.get(), nullptr, crypto::NoPasswordCallback, nullptr))) {
            if (cert_store == crypto::GetOrCreateRootCertStore()) {
              cert_store = crypto::NewRootCertStore();
              SSL_CTX_set_cert_store(ctx.get(), cert_store);
            }
            CHECK_EQ(1, X509_STORE_add_cert(cert_store, x509.get()));
            CHECK_EQ(1, SSL_CTX_add_client_CA(ctx.get(), x509.get()));
          }
        }
      }
    }
  }

  {
    ClearErrorOnReturn clear_error_on_return;
    for (const auto& cert : options_.certs) {
      uv_buf_t buf = cert;
      if (buf.len > 0) {
        BIOPointer bio = crypto::NodeBIO::NewFixed(buf.base, buf.len);
        CHECK(bio);
        cert_.reset();
        issuer_.reset();
        if (crypto::SSL_CTX_use_certificate_chain(
                ctx.get(), std::move(bio), &cert_, &issuer_) == 0) {
          validation_error_ = "Invalid certificate";
          return SSLCtxPointer();
        }
      }
    }
  }

  {
    ClearErrorOnReturn clear_error_on_return;
    for (const auto& key : options_.keys) {
      if (key.GetKeyType() != crypto::KeyType::kKeyTypePrivate) {
        validation_error_ = "Invalid key";
        return SSLCtxPointer();
      }
      if (!SSL_CTX_use_PrivateKey(ctx.get(), key.GetAsymmetricKey().get())) {
        validation_error_ = "Invalid key";
        return SSLCtxPointer();
      }
    }
  }

  {
    ClearErrorOnReturn clear_error_on_return;
    for (const auto& crl : options_.crl) {
      uv_buf_t buf = crl;
      BIOPointer bio = crypto::NodeBIO::NewFixed(buf.base, buf.len);
      DeleteFnPtr<X509_CRL, X509_CRL_free> crlptr(PEM_read_bio_X509_CRL(
          bio.get(), nullptr, crypto::NoPasswordCallback, nullptr));

      if (!crlptr) {
        validation_error_ = "Invalid CRL";
        return SSLCtxPointer();
      }

      X509_STORE* cert_store = SSL_CTX_get_cert_store(ctx.get());
      if (cert_store == crypto::GetOrCreateRootCertStore()) {
        cert_store = crypto::NewRootCertStore();
        SSL_CTX_set_cert_store(ctx.get(), cert_store);
      }

      CHECK_EQ(1, X509_STORE_add_crl(cert_store, crlptr.get()));
      CHECK_EQ(
          1,
          X509_STORE_set_flags(
              cert_store, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL));
    }
  }

  {
    ClearErrorOnReturn clear_error_on_return;
    if (options_.verify_private_key &&
        SSL_CTX_check_private_key(ctx.get()) != 1) {
      validation_error_ = "Invalid private key";
      return SSLCtxPointer();
    }
  }

  return ctx;
}

void TLSContext::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("options", options_);
}

Maybe<TLSContext::Options> TLSContext::Options::From(Environment* env,
                                                     Local<Value> value) {
  if (value.IsEmpty()) {
    return Nothing<Options>();
  }

  Options options;
  auto& state = BindingData::Get(env);

  if (value->IsUndefined()) {
    return Just(kDefault);
  }

  if (!value->IsObject()) {
    THROW_ERR_INVALID_ARG_TYPE(env, "tls options must be an object");
    return Nothing<Options>();
  }

  auto params = value.As<Object>();

#define SET_VECTOR(Type, name)                                                 \
  SetOption<Type, TLSContext::Options, &TLSContext::Options::name>(            \
      env, &options, params, state.name##_string())

#define SET(name)                                                              \
  SetOption<TLSContext::Options, &TLSContext::Options::name>(                  \
      env, &options, params, state.name##_string())

  if (!SET(verify_client) || !SET(enable_tls_trace) || !SET(protocol) ||
      !SET(servername) || !SET(ciphers) || !SET(groups) ||
      !SET(verify_private_key) || !SET(keylog) ||
      !SET_VECTOR(crypto::KeyObjectData, keys) || !SET_VECTOR(Store, certs) ||
      !SET_VECTOR(Store, ca) || !SET_VECTOR(Store, crl)) {
    return Nothing<Options>();
  }

  return Just<Options>(options);
}

std::string TLSContext::Options::ToString() const {
  DebugIndentScope indent;
  auto prefix = indent.Prefix();
  std::string res("{");
  res += prefix + "protocol: " + protocol;
  res += prefix + "servername: " + servername;
  res +=
      prefix + "keylog: " + (keylog ? std::string("yes") : std::string("no"));
  res += prefix + "verify client: " +
         (verify_client ? std::string("yes") : std::string("no"));
  res += prefix + "enable_tls_trace: " +
         (enable_tls_trace ? std::string("yes") : std::string("no"));
  res += prefix + "verify private key: " +
         (verify_private_key ? std::string("yes") : std::string("no"));
  res += prefix + "ciphers: " + ciphers;
  res += prefix + "groups: " + groups;
  res += prefix + "keys: " + std::to_string(keys.size());
  res += prefix + "certs: " + std::to_string(certs.size());
  res += prefix + "ca: " + std::to_string(ca.size());
  res += prefix + "crl: " + std::to_string(crl.size());
  res += indent.Close();
  return res;
}

void TLSContext::Options::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("keys", keys);
  tracker->TrackField("certs", certs);
  tracker->TrackField("ca", ca);
  tracker->TrackField("crl", crl);
}

const TLSContext::Options TLSContext::Options::kDefault = {};

// ============================================================================

const TLSSession& TLSSession::From(const SSL* ssl) {
  auto ref = static_cast<ngtcp2_crypto_conn_ref*>(SSL_get_app_data(ssl));
  CHECK_NOT_NULL(ref);
  return *static_cast<TLSSession*>(ref->user_data);
}

TLSSession::TLSSession(Session* session,
                       std::shared_ptr<TLSContext> context,
                       const std::optional<SessionTicket>& maybeSessionTicket)
    : ref_({connection, this}),
      context_(std::move(context)),
      session_(session),
      ssl_(Initialize(maybeSessionTicket)) {
  Debug(session_, "Created new TLS session for %s", session->config().dcid);
}

TLSSession::~TLSSession() {
  if (ssl_) {
    SSL_set_app_data(ssl_.get(), nullptr);
  }
}

TLSSession::operator SSL*() const {
  CHECK(ssl_);
  return ssl_.get();
}

bool TLSSession::early_data_was_accepted() const {
  CHECK_NE(ngtcp2_conn_get_handshake_completed(*session_), 0);
  return SSL_get_early_data_status(*this) == SSL_EARLY_DATA_ACCEPTED;
}

SSLPointer TLSSession::Initialize(
    const std::optional<SessionTicket>& maybeSessionTicket) {
  auto& ctx = context();
  auto& options = ctx.options();
  SSLPointer ssl(SSL_new(ctx));
  SSL_set_app_data(ssl.get(), &ref_);
  ngtcp2_conn_set_tls_native_handle(*session_, ssl.get());

  // Enable tracing if the `--trace-tls` command line flag is used.
  if (session_->env()->options()->trace_tls || options.enable_tls_trace)
      [[unlikely]] {
    EnableTrace(session_->env(), &bio_trace_, *this);
  }

  switch (ctx.side()) {
    case Side::SERVER: {
      SSL_set_accept_state(ssl.get());
      SSL_set_quic_early_data_enabled(ssl.get(), 1);
      break;
    }
    case Side::CLIENT: {
      SSL_set_connect_state(ssl.get());
      if (SSL_set_alpn_protos(
              ssl.get(),
              reinterpret_cast<const unsigned char*>(options.protocol.data()),
              options.protocol.size()) != 0) {
        validation_error_ = "Invalid ALPN";
        return SSLPointer();
      }

      if (!options.servername.empty()) {
        SSL_set_tlsext_host_name(ssl.get(), options.servername.data());
      } else {
        SSL_set_tlsext_host_name(ssl.get(), "localhost");
      }

      if (maybeSessionTicket.has_value()) {
        auto sessionTicket = maybeSessionTicket.value();
        uv_buf_t buf = sessionTicket.ticket();
        SSLSessionPointer ticket = crypto::GetTLSSession(
            reinterpret_cast<unsigned char*>(buf.base), buf.len);

        // The early data will just be ignored if it's invalid.
        if (ssl.setSession(ticket) &&
            SSL_SESSION_get_max_early_data(ticket.get()) != 0) {
          ngtcp2_vec rtp = sessionTicket.transport_params();
          if (ngtcp2_conn_decode_and_set_0rtt_transport_params(
                  *session_, rtp.base, rtp.len) == 0) {
            SSL_set_quic_early_data_enabled(ssl.get(), 1);
            session_->SetStreamOpenAllowed();
          }
        }
      }

      break;
    }
  }

  TransportParams tp(ngtcp2_conn_get_local_transport_params(*session_));
  Store store = tp.Encode(session_->env());
  if (store && store.length() > 0) {
    ngtcp2_vec vec = store;
    SSL_set_quic_transport_params(ssl.get(), vec.base, vec.len);
  }

  return ssl;
}

std::optional<TLSSession::PeerIdentityValidationError>
TLSSession::VerifyPeerIdentity(Environment* env) {
  int err = ssl_.verifyPeerCertificate().value_or(X509_V_ERR_UNSPECIFIED);
  if (err == X509_V_OK) return std::nullopt;
  Local<Value> reason;
  Local<Value> code;
  if (!crypto::GetValidationErrorReason(env, err).ToLocal(&reason) ||
      !crypto::GetValidationErrorCode(env, err).ToLocal(&code)) {
    // Getting the validation error details failed. We'll return a value but
    // the fields will be empty.
    return PeerIdentityValidationError{};
  }
  return PeerIdentityValidationError{reason, code};
}

MaybeLocal<Object> TLSSession::cert(Environment* env) const {
  return crypto::X509Certificate::GetCert(env, ssl_);
}

MaybeLocal<Object> TLSSession::peer_cert(Environment* env) const {
  crypto::X509Certificate::GetPeerCertificateFlag flag =
      context_->side() == Side::SERVER
          ? crypto::X509Certificate::GetPeerCertificateFlag::SERVER
          : crypto::X509Certificate::GetPeerCertificateFlag::NONE;
  return crypto::X509Certificate::GetPeerCert(env, ssl_, flag);
}

MaybeLocal<Object> TLSSession::ephemeral_key(Environment* env) const {
  return crypto::GetEphemeralKey(env, ssl_);
}

MaybeLocal<Value> TLSSession::cipher_name(Environment* env) const {
  auto name = ssl_.getCipherName();
  if (!name.has_value()) return Undefined(env->isolate());
  return OneByteString(env->isolate(), name.value());
}

MaybeLocal<Value> TLSSession::cipher_version(Environment* env) const {
  auto version = ssl_.getCipherVersion();
  if (!version.has_value()) return Undefined(env->isolate());
  return OneByteString(env->isolate(), version.value());
}

const std::string_view TLSSession::servername() const {
  return ssl_.getServerName().value_or(std::string_view());
}

const std::string_view TLSSession::protocol() const {
  const unsigned char* alpn_buf = nullptr;
  unsigned int alpnlen;
  SSL_get0_alpn_selected(ssl_.get(), &alpn_buf, &alpnlen);
  return alpnlen ? std::string_view(reinterpret_cast<const char*>(alpn_buf),
                                    alpnlen)
                 : std::string_view();
}

bool TLSSession::InitiateKeyUpdate() {
  if (in_key_update_) return false;
  auto leave = OnScopeLeave([this] { in_key_update_ = false; });
  in_key_update_ = true;

  Debug(session_, "Initiating key update");
  return ngtcp2_conn_initiate_key_update(*session_, uv_hrtime()) == 0;
}

ngtcp2_conn* TLSSession::connection(ngtcp2_crypto_conn_ref* ref) {
  CHECK_NOT_NULL(ref->user_data);
  return static_cast<TLSSession*>(ref->user_data)->session();
}

}  // namespace quic
}  // namespace node

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
