#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC

#include "session.h"
#include <aliased_struct-inl.h>
#include <async_wrap-inl.h>
#include <crypto/crypto_util.h>
#include <debug_utils-inl.h>
#include <env-inl.h>
#include <memory_tracker-inl.h>
#include <ngtcp2/ngtcp2.h>
#include <node_bob-inl.h>
#include <node_errors.h>
#include <node_http_common-inl.h>
#include <node_sockaddr-inl.h>
#include <req_wrap-inl.h>
#include <timer_wrap-inl.h>
#include <util-inl.h>
#include <uv.h>
#include <v8.h>
#include "application.h"
#include "bindingdata.h"
#include "cid.h"
#include "data.h"
#include "defs.h"
#include "endpoint.h"
#include "http3.h"
#include "logstream.h"
#include "ncrypto.h"
#include "packet.h"
#include "preferredaddress.h"
#include "sessionticket.h"
#include "streams.h"
#include "tlscontext.h"
#include "transportparams.h"

namespace node {

using v8::Array;
using v8::ArrayBuffer;
using v8::ArrayBufferView;
using v8::BackingStoreInitializationMode;
using v8::BigInt;
using v8::Boolean;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Int32;
using v8::Integer;
using v8::Just;
using v8::Local;
using v8::LocalVector;
using v8::Maybe;
using v8::MaybeLocal;
using v8::Nothing;
using v8::Object;
using v8::ObjectTemplate;
using v8::PropertyAttribute;
using v8::String;
using v8::Uint32;
using v8::Undefined;
using v8::Value;

namespace quic {

#define SESSION_STATE(V)                                                       \
  V(PATH_VALIDATION, path_validation, uint8_t)                                 \
  V(VERSION_NEGOTIATION, version_negotiation, uint8_t)                         \
  V(DATAGRAM, datagram, uint8_t)                                               \
  V(SESSION_TICKET, session_ticket, uint8_t)                                   \
  V(CLOSING, closing, uint8_t)                                                 \
  V(GRACEFUL_CLOSE, graceful_close, uint8_t)                                   \
  V(SILENT_CLOSE, silent_close, uint8_t)                                       \
  V(STATELESS_RESET, stateless_reset, uint8_t)                                 \
  V(HANDSHAKE_COMPLETED, handshake_completed, uint8_t)                         \
  V(HANDSHAKE_CONFIRMED, handshake_confirmed, uint8_t)                         \
  V(STREAM_OPEN_ALLOWED, stream_open_allowed, uint8_t)                         \
  V(PRIORITY_SUPPORTED, priority_supported, uint8_t)                           \
  V(WRAPPED, wrapped, uint8_t)                                                 \
  V(LAST_DATAGRAM_ID, last_datagram_id, uint64_t)

#define SESSION_STATS(V)                                                       \
  V(CREATED_AT, created_at)                                                    \
  V(CLOSING_AT, closing_at)                                                    \
  V(HANDSHAKE_COMPLETED_AT, handshake_completed_at)                            \
  V(HANDSHAKE_CONFIRMED_AT, handshake_confirmed_at)                            \
  V(BYTES_RECEIVED, bytes_received)                                            \
  V(BYTES_SENT, bytes_sent)                                                    \
  V(BIDI_IN_STREAM_COUNT, bidi_in_stream_count)                                \
  V(BIDI_OUT_STREAM_COUNT, bidi_out_stream_count)                              \
  V(UNI_IN_STREAM_COUNT, uni_in_stream_count)                                  \
  V(UNI_OUT_STREAM_COUNT, uni_out_stream_count)                                \
  V(MAX_BYTES_IN_FLIGHT, max_bytes_in_flight)                                  \
  V(BYTES_IN_FLIGHT, bytes_in_flight)                                          \
  V(BLOCK_COUNT, block_count)                                                  \
  V(CWND, cwnd)                                                                \
  V(LATEST_RTT, latest_rtt)                                                    \
  V(MIN_RTT, min_rtt)                                                          \
  V(RTTVAR, rttvar)                                                            \
  V(SMOOTHED_RTT, smoothed_rtt)                                                \
  V(SSTHRESH, ssthresh)                                                        \
  V(DATAGRAMS_RECEIVED, datagrams_received)                                    \
  V(DATAGRAMS_SENT, datagrams_sent)                                            \
  V(DATAGRAMS_ACKNOWLEDGED, datagrams_acknowledged)                            \
  V(DATAGRAMS_LOST, datagrams_lost)

#define SESSION_JS_METHODS(V)                                                  \
  V(Destroy, destroy, false)                                                   \
  V(GetRemoteAddress, getRemoteAddress, true)                                  \
  V(GetCertificate, getCertificate, true)                                      \
  V(GetEphemeralKeyInfo, getEphemeralKey, true)                                \
  V(GetPeerCertificate, getPeerCertificate, true)                              \
  V(GracefulClose, gracefulClose, false)                                       \
  V(SilentClose, silentClose, false)                                           \
  V(UpdateKey, updateKey, false)                                               \
  V(OpenStream, openStream, false)                                             \
  V(SendDatagram, sendDatagram, false)

struct Session::State final {
#define V(_, name, type) type name;
  SESSION_STATE(V)
#undef V
};

STAT_STRUCT(Session, SESSION)

// ============================================================================

class Http3Application;

namespace {
std::string to_string(PreferredAddress::Policy policy) {
  switch (policy) {
    case PreferredAddress::Policy::USE_PREFERRED:
      return "use";
    case PreferredAddress::Policy::IGNORE_PREFERRED:
      return "ignore";
  }
  return "<unknown>";
}

std::string to_string(Side side) {
  switch (side) {
    case Side::CLIENT:
      return "client";
    case Side::SERVER:
      return "server";
  }
  return "<unknown>";
}

std::string to_string(ngtcp2_encryption_level level) {
  switch (level) {
    case NGTCP2_ENCRYPTION_LEVEL_1RTT:
      return "1rtt";
    case NGTCP2_ENCRYPTION_LEVEL_0RTT:
      return "0rtt";
    case NGTCP2_ENCRYPTION_LEVEL_HANDSHAKE:
      return "handshake";
    case NGTCP2_ENCRYPTION_LEVEL_INITIAL:
      return "initial";
  }
  return "<unknown>";
}

std::string to_string(ngtcp2_cc_algo cc_algorithm) {
#define V(name, label)                                                         \
  case NGTCP2_CC_ALGO_##name:                                                  \
    return #label;
  switch (cc_algorithm) { CC_ALGOS(V) }
  return "<unknown>";
#undef V
}

Maybe<ngtcp2_cc_algo> getAlgoFromString(Environment* env, Local<String> input) {
  auto& state = BindingData::Get(env);
#define V(name, str)                                                           \
  if (input->StringEquals(state.str##_string())) {                             \
    return Just(NGTCP2_CC_ALGO_##name);                                        \
  }

  CC_ALGOS(V)

#undef V
  return Nothing<ngtcp2_cc_algo>();
}

// Qlog is a JSON-based logging format that is being standardized for low-level
// debug logging of QUIC connections and dataflows. The qlog output is generated
// optionally by ngtcp2 for us. The on_qlog_write callback is registered with
// ngtcp2 to emit the qlog information. Every Session will have it's own qlog
// stream.
void on_qlog_write(void* user_data,
                   uint32_t flags,
                   const void* data,
                   size_t len) {
  static_cast<Session*>(user_data)->HandleQlog(flags, data, len);
}

// Forwards detailed(verbose) debugging information from ngtcp2. Enabled using
// the NODE_DEBUG_NATIVE=NGTCP2_DEBUG category.
void ngtcp2_debug_log(void* user_data, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::string format(fmt, strlen(fmt) + 1);
  format[strlen(fmt)] = '\n';
  // Debug() does not work with the va_list here. So we use vfprintf
  // directly instead. Ngtcp2DebugLog is only enabled when the debug
  // category is enabled.
  vfprintf(stderr, format.c_str(), ap);
  va_end(ap);
}

template <typename Opt, PreferredAddress::Policy Opt::*member>
bool SetOption(Environment* env,
               Opt* options,
               const Local<Object>& object,
               const Local<String>& name) {
  Local<Value> value;
  PreferredAddress::Policy policy = PreferredAddress::Policy::USE_PREFERRED;
  if (!object->Get(env->context(), name).ToLocal(&value) ||
      !PreferredAddress::tryGetPolicy(env, value).To(&policy)) {
    return false;
  }
  options->*member = policy;
  return true;
}

template <typename Opt, TLSContext::Options Opt::*member>
bool SetOption(Environment* env,
               Opt* options,
               const Local<Object>& object,
               const Local<String>& name) {
  Local<Value> value;
  TLSContext::Options opts;
  if (!object->Get(env->context(), name).ToLocal(&value) ||
      !TLSContext::Options::From(env, value).To(&opts)) {
    return false;
  }
  options->*member = opts;
  return true;
}

template <typename Opt, TransportParams::Options Opt::*member>
bool SetOption(Environment* env,
               Opt* options,
               const Local<Object>& object,
               const Local<String>& name) {
  Local<Value> value;
  TransportParams::Options opts;
  if (!object->Get(env->context(), name).ToLocal(&value) ||
      !TransportParams::Options::From(env, value).To(&opts)) {
    return false;
  }
  options->*member = opts;
  return true;
}

template <typename Opt,
          BaseObjectPtr<Session::ApplicationProvider> Opt::*member>
bool SetOption(Environment* env,
               Opt* options,
               const Local<Object>& object,
               const Local<String>& name) {
  Local<Value> value;
  if (!object->Get(env->context(), name).ToLocal(&value)) {
    return false;
  }
  if (!value->IsUndefined()) {
    // We currently only support Http3Application for this option.
    if (!Http3Application::HasInstance(env, value)) {
      THROW_ERR_INVALID_ARG_TYPE(env,
                                 "Application must be an Http3Application");
      return false;
    }
    Http3Application* app;
    ASSIGN_OR_RETURN_UNWRAP(&app, value.As<Object>(), false);
    CHECK_NOT_NULL(app);
    auto& assigned = options->*member =
                         BaseObjectPtr<Session::ApplicationProvider>(app);
    assigned->Detach();
  }
  return true;
}

template <typename Opt, ngtcp2_cc_algo Opt::*member>
bool SetOption(Environment* env,
               Opt* options,
               const Local<Object>& object,
               const Local<String>& name) {
  Local<Value> value;
  if (!object->Get(env->context(), name).ToLocal(&value)) return false;
  if (!value->IsUndefined()) {
    ngtcp2_cc_algo algo;
    if (value->IsString()) {
      if (!getAlgoFromString(env, value.As<String>()).To(&algo)) {
        THROW_ERR_INVALID_ARG_VALUE(env, "The cc_algorithm option is invalid");
        return false;
      }
    } else {
      if (!value->IsInt32()) {
        THROW_ERR_INVALID_ARG_VALUE(
            env, "The cc_algorithm option must be a string or an integer");
        return false;
      }
      Local<Int32> num;
      if (!value->ToInt32(env->context()).ToLocal(&num)) {
        THROW_ERR_INVALID_ARG_VALUE(env, "The cc_algorithm option is invalid");
        return false;
      }
      switch (num->Value()) {
#define V(name, _)                                                             \
  case NGTCP2_CC_ALGO_##name:                                                  \
    break;
        CC_ALGOS(V)
#undef V
        default:
          THROW_ERR_INVALID_ARG_VALUE(env,
                                      "The cc_algorithm option is invalid");
          return false;
      }
      algo = static_cast<ngtcp2_cc_algo>(num->Value());
    }
    options->*member = algo;
  }
  return true;
}

}  // namespace

// ============================================================================
Session::Config::Config(Environment* env,
                        Side side,
                        const Options& options,
                        uint32_t version,
                        const SocketAddress& local_address,
                        const SocketAddress& remote_address,
                        const CID& dcid,
                        const CID& scid,
                        const CID& ocid)
    : side(side),
      options(options),
      version(version),
      local_address(local_address),
      remote_address(remote_address),
      dcid(dcid),
      scid(scid),
      ocid(ocid) {
  ngtcp2_settings_default(&settings);
  settings.initial_ts = uv_hrtime();

  // We currently do not support Path MTU Discovery. Once we do, unset this.
  settings.no_pmtud = 1;
  // Per the ngtcp2 documentation, when no_tx_udp_payload_size_shaping is set
  // to a non-zero value, ngtcp2 not to limit the UDP payload size to
  // NGTCP2_MAX_UDP_PAYLOAD_SIZE` and will instead "use the minimum size among
  // the given buffer size, :member:`max_tx_udp_payload_size`, and the
  // received max_udp_payload_size QUIC transport parameter." For now, this
  // works for us, especially since we do not implement Path MTU discovery.
  settings.no_tx_udp_payload_size_shaping = 1;
  settings.max_tx_udp_payload_size = options.max_payload_size;

  settings.tokenlen = 0;
  settings.token = nullptr;

  if (options.qlog) {
    settings.qlog_write = on_qlog_write;
  }

  if (env->enabled_debug_list()->enabled(DebugCategory::NGTCP2_DEBUG)) {
    settings.log_printf = ngtcp2_debug_log;
  }

  settings.handshake_timeout = options.handshake_timeout;
  settings.max_stream_window = options.max_stream_window;
  settings.max_window = options.max_window;
  settings.ack_thresh = options.unacknowledged_packet_threshold;
  settings.cc_algo = options.cc_algorithm;
}

Session::Config::Config(Environment* env,
                        const Options& options,
                        const SocketAddress& local_address,
                        const SocketAddress& remote_address,
                        const CID& ocid)
    : Config(env,
             Side::CLIENT,
             options,
             options.version,
             local_address,
             remote_address,
             CID::Factory::random().Generate(NGTCP2_MIN_INITIAL_DCIDLEN),
             options.cid_factory->Generate(),
             ocid) {}

void Session::Config::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("options", options);
  tracker->TrackField("local_address", local_address);
  tracker->TrackField("remote_address", remote_address);
  tracker->TrackField("dcid", dcid);
  tracker->TrackField("scid", scid);
  tracker->TrackField("ocid", ocid);
  tracker->TrackField("retry_scid", retry_scid);
}

void Session::Config::set_token(const uint8_t* token,
                                size_t len,
                                ngtcp2_token_type type) {
  settings.token = token;
  settings.tokenlen = len;
  settings.token_type = type;
}

void Session::Config::set_token(const RetryToken& token) {
  ngtcp2_vec vec = token;
  set_token(vec.base, vec.len, NGTCP2_TOKEN_TYPE_RETRY);
}

void Session::Config::set_token(const RegularToken& token) {
  ngtcp2_vec vec = token;
  set_token(vec.base, vec.len, NGTCP2_TOKEN_TYPE_NEW_TOKEN);
}

std::string Session::Config::ToString() const {
  DebugIndentScope indent;
  auto prefix = indent.Prefix();
  std::string res("{");
  res += prefix + "side: " + to_string(side);
  res += prefix + "options: " + options.ToString();
  res += prefix + "version: " + std::to_string(version);
  res += prefix + "local address: " + local_address.ToString();
  res += prefix + "remote address: " + remote_address.ToString();
  res += prefix + "dcid: " + dcid.ToString();
  res += prefix + "scid: " + scid.ToString();
  res += prefix + "ocid: " + ocid.ToString();
  res += prefix + "retry scid: " + retry_scid.ToString();
  res += prefix + "preferred address cid: " + preferred_address_cid.ToString();
  res += indent.Close();
  return res;
}

// ============================================================================

Maybe<Session::Options> Session::Options::From(Environment* env,
                                               Local<Value> value) {
  if (value.IsEmpty() || !value->IsObject()) {
    THROW_ERR_INVALID_ARG_TYPE(env, "options must be an object");
    return Nothing<Options>();
  }

  auto& state = BindingData::Get(env);
  auto params = value.As<Object>();
  Options options;

#define SET(name)                                                              \
  SetOption<Session::Options, &Session::Options::name>(                        \
      env, &options, params, state.name##_string())

  if (!SET(version) || !SET(min_version) || !SET(preferred_address_strategy) ||
      !SET(transport_params) || !SET(tls_options) || !SET(qlog) ||
      !SET(application_provider) || !SET(handshake_timeout) ||
      !SET(max_stream_window) || !SET(max_window) || !SET(max_payload_size) ||
      !SET(unacknowledged_packet_threshold) || !SET(cc_algorithm)) {
    return Nothing<Options>();
  }

#undef SET

  // TODO(@jasnell): Later we will also support setting the CID::Factory.
  // For now, we're just using the default random factory.

  return Just<Options>(options);
}

void Session::Options::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("transport_params", transport_params);
  tracker->TrackField("crypto_options", tls_options);
  tracker->TrackField("cid_factory_ref", cid_factory_ref);
}

std::string Session::Options::ToString() const {
  DebugIndentScope indent;
  auto prefix = indent.Prefix();
  std::string res("{");
  res += prefix + "version: " + std::to_string(version);
  res += prefix + "min version: " + std::to_string(min_version);
  res += prefix +
         "preferred address policy: " + to_string(preferred_address_strategy);
  res += prefix + "transport params: " + transport_params.ToString();
  res += prefix + "crypto options: " + tls_options.ToString();
  if (qlog) {
    res += prefix + "qlog: yes";
  }
  if (handshake_timeout == UINT64_MAX) {
    res += prefix + "handshake timeout: <none>";
  } else {
    res += prefix + "handshake timeout: " + std::to_string(handshake_timeout) +
           " nanoseconds";
  }
  res += prefix + "max stream window: " + std::to_string(max_stream_window);
  res += prefix + "max window: " + std::to_string(max_window);
  res += prefix + "max payload size: " + std::to_string(max_payload_size);
  if (unacknowledged_packet_threshold != 0) {
    res += prefix + "unacknowledged packet threshold: " +
           std::to_string(unacknowledged_packet_threshold);
  } else {
    res += prefix + "unacknowledged packet threshold: <default>";
  }
  res += prefix + "cc algorithm: " + to_string(cc_algorithm);
  res += indent.Close();
  return res;
}

// ============================================================================
// ngtcp2 static callback functions

// Utility used only within Session::Impl to reduce boilerplate
#define NGTCP2_CALLBACK_SCOPE(name)                                            \
  auto name = Impl::From(conn, user_data);                                     \
  if (name == nullptr) return NGTCP2_ERR_CALLBACK_FAILURE;                     \
  NgTcp2CallbackScope scope(name->env());

// Session::Impl maintains most of the internal state of an active Session.
struct Session::Impl final : public MemoryRetainer {
  Session* session_;
  AliasedStruct<Stats> stats_;
  AliasedStruct<State> state_;
  BaseObjectWeakPtr<Endpoint> endpoint_;
  Config config_;
  SocketAddress local_address_;
  SocketAddress remote_address_;
  std::unique_ptr<Application> application_;
  StreamsMap streams_;
  TimerWrapHandle timer_;
  size_t send_scope_depth_ = 0;
  QuicError last_error_;
  PendingStream::PendingStreamQueue pending_bidi_stream_queue_;
  PendingStream::PendingStreamQueue pending_uni_stream_queue_;

  Impl(Session* session, Endpoint* endpoint, const Config& config)
      : session_(session),
        stats_(env()->isolate()),
        state_(env()->isolate()),
        endpoint_(endpoint),
        config_(config),
        local_address_(config.local_address),
        remote_address_(config.remote_address),
        application_(SelectApplication(session, config_)),
        timer_(session_->env(), [this] { session_->OnTimeout(); }) {
    timer_.Unref();
  }

  inline bool is_closing() const { return state_->closing; }

  /**
   * @returns {boolean} Returns true if the Session can be destroyed
   * immediately.
   */
  bool Close() {
    if (state_->closing) return true;
    state_->closing = 1;
    STAT_RECORD_TIMESTAMP(Stats, closing_at);

    // Iterate through all of the known streams and close them. The streams
    // will remove themselves from the Session as soon as they are closed.
    // Note: we create a copy because the streams will remove themselves
    // while they are cleaning up which will invalidate the iterator.
    StreamsMap streams = streams_;
    for (auto& stream : streams) stream.second->Destroy(last_error_);
    DCHECK(streams.empty());

    // Clear the pending streams.
    while (!pending_bidi_stream_queue_.IsEmpty()) {
      pending_bidi_stream_queue_.PopFront()->reject(last_error_);
    }
    while (!pending_uni_stream_queue_.IsEmpty()) {
      pending_uni_stream_queue_.PopFront()->reject(last_error_);
    }

    // If we are able to send packets, we should try sending a connection
    // close packet to the remote peer.
    if (!state_->silent_close) {
      session_->SendConnectionClose();
    }

    timer_.Close();

    return !state_->wrapped;
  }

  ~Impl() {
    // Ensure that Close() was called before dropping
    DCHECK(is_closing());
    DCHECK(endpoint_);

    // Removing the session from the endpoint may cause the endpoint to be
    // destroyed if it is waiting on the last session to be destroyed. Let's
    // grab a reference just to be safe for the rest of the function.
    BaseObjectPtr<Endpoint> endpoint = endpoint_;
    endpoint_.reset();

    MaybeStackBuffer<ngtcp2_cid, 10> cids(
        ngtcp2_conn_get_scid(*session_, nullptr));
    ngtcp2_conn_get_scid(*session_, cids.out());

    MaybeStackBuffer<ngtcp2_cid_token, 10> tokens(
        ngtcp2_conn_get_active_dcid(*session_, nullptr));
    ngtcp2_conn_get_active_dcid(*session_, tokens.out());

    endpoint->DisassociateCID(config_.dcid);
    endpoint->DisassociateCID(config_.preferred_address_cid);

    for (size_t n = 0; n < cids.length(); n++) {
      endpoint->DisassociateCID(CID(cids[n]));
    }

    for (size_t n = 0; n < tokens.length(); n++) {
      if (tokens[n].token_present) {
        endpoint->DisassociateStatelessResetToken(
            StatelessResetToken(tokens[n].token));
      }
    }

    endpoint->RemoveSession(config_.scid, remote_address_);
  }

  void MemoryInfo(MemoryTracker* tracker) const override {
    tracker->TrackField("config", config_);
    tracker->TrackField("endpoint", endpoint_);
    tracker->TrackField("streams", streams_);
    tracker->TrackField("local_address", local_address_);
    tracker->TrackField("remote_address", remote_address_);
    tracker->TrackField("application", application_);
    tracker->TrackField("timer", timer_);
  }
  SET_SELF_SIZE(Impl)
  SET_MEMORY_INFO_NAME(Session::Impl)

  Environment* env() const { return session_->env(); }

  // Gets the Session pointer from the user_data void pointer
  // provided by ngtcp2.
  static Session* From(ngtcp2_conn* conn, void* user_data) {
    if (user_data == nullptr) [[unlikely]] {
      return nullptr;
    }
    auto session = static_cast<Session*>(user_data);
    if (session->is_destroyed()) [[unlikely]] {
      return nullptr;
    }
    return session;
  }

  // JavaScript APIs

  static void Destroy(const FunctionCallbackInfo<Value>& args) {
    auto env = Environment::GetCurrent(args);
    Session* session;
    ASSIGN_OR_RETURN_UNWRAP(&session, args.This());
    if (session->is_destroyed()) {
      THROW_ERR_INVALID_STATE(env, "Session is destroyed");
    }
    session->Destroy();
  }

  static void GetRemoteAddress(const FunctionCallbackInfo<Value>& args) {
    auto env = Environment::GetCurrent(args);
    Session* session;
    ASSIGN_OR_RETURN_UNWRAP(&session, args.This());

    if (session->is_destroyed()) {
      THROW_ERR_INVALID_STATE(env, "Session is destroyed");
    }

    auto address = session->remote_address();
    args.GetReturnValue().Set(
        SocketAddressBase::Create(env, std::make_shared<SocketAddress>(address))
            ->object());
  }

  static void GetCertificate(const FunctionCallbackInfo<Value>& args) {
    auto env = Environment::GetCurrent(args);
    Session* session;
    ASSIGN_OR_RETURN_UNWRAP(&session, args.This());

    if (session->is_destroyed()) {
      THROW_ERR_INVALID_STATE(env, "Session is destroyed");
    }

    Local<Value> ret;
    if (session->tls_session().cert(env).ToLocal(&ret))
      args.GetReturnValue().Set(ret);
  }

  static void GetEphemeralKeyInfo(const FunctionCallbackInfo<Value>& args) {
    auto env = Environment::GetCurrent(args);
    Session* session;
    ASSIGN_OR_RETURN_UNWRAP(&session, args.This());

    if (session->is_destroyed()) {
      THROW_ERR_INVALID_STATE(env, "Session is destroyed");
    }

    Local<Object> ret;
    if (!session->is_server() &&
        session->tls_session().ephemeral_key(env).ToLocal(&ret))
      args.GetReturnValue().Set(ret);
  }

  static void GetPeerCertificate(const FunctionCallbackInfo<Value>& args) {
    auto env = Environment::GetCurrent(args);
    Session* session;
    ASSIGN_OR_RETURN_UNWRAP(&session, args.This());

    if (session->is_destroyed()) {
      THROW_ERR_INVALID_STATE(env, "Session is destroyed");
    }

    Local<Value> ret;
    if (session->tls_session().peer_cert(env).ToLocal(&ret))
      args.GetReturnValue().Set(ret);
  }

  static void GracefulClose(const FunctionCallbackInfo<Value>& args) {
    auto env = Environment::GetCurrent(args);
    Session* session;
    ASSIGN_OR_RETURN_UNWRAP(&session, args.This());

    if (session->is_destroyed()) {
      THROW_ERR_INVALID_STATE(env, "Session is destroyed");
    }

    session->Close(Session::CloseMethod::GRACEFUL);
  }

  static void SilentClose(const FunctionCallbackInfo<Value>& args) {
    // This is exposed for testing purposes only!
    auto env = Environment::GetCurrent(args);
    Session* session;
    ASSIGN_OR_RETURN_UNWRAP(&session, args.This());

    if (session->is_destroyed()) {
      THROW_ERR_INVALID_STATE(env, "Session is destroyed");
    }

    session->Close(Session::CloseMethod::SILENT);
  }

  static void UpdateKey(const FunctionCallbackInfo<Value>& args) {
    auto env = Environment::GetCurrent(args);
    Session* session;
    ASSIGN_OR_RETURN_UNWRAP(&session, args.This());

    if (session->is_destroyed()) {
      THROW_ERR_INVALID_STATE(env, "Session is destroyed");
    }

    // Initiating a key update may fail if it is done too early (either
    // before the TLS handshake has been confirmed or while a previous
    // key update is being processed). When it fails, InitiateKeyUpdate()
    // will return false.
    SendPendingDataScope send_scope(session);
    args.GetReturnValue().Set(session->tls_session().InitiateKeyUpdate());
  }

  static void OpenStream(const FunctionCallbackInfo<Value>& args) {
    auto env = Environment::GetCurrent(args);
    Session* session;
    ASSIGN_OR_RETURN_UNWRAP(&session, args.This());

    if (session->is_destroyed()) {
      THROW_ERR_INVALID_STATE(env, "Session is destroyed");
    }

    DCHECK(args[0]->IsUint32());

    // GetDataQueueFromSource handles type validation.
    std::shared_ptr<DataQueue> data_source =
        Stream::GetDataQueueFromSource(env, args[1]).ToChecked();
    if (data_source == nullptr) {
      THROW_ERR_INVALID_ARG_VALUE(env, "Invalid data source");
    }

    SendPendingDataScope send_scope(session);
    auto direction = static_cast<Direction>(args[0].As<Uint32>()->Value());
    Local<Object> stream;
    if (session->OpenStream(direction, std::move(data_source)).ToLocal(&stream))
        [[likely]] {
      args.GetReturnValue().Set(stream);
    }
  }

  static void SendDatagram(const FunctionCallbackInfo<Value>& args) {
    auto env = Environment::GetCurrent(args);
    Session* session;
    ASSIGN_OR_RETURN_UNWRAP(&session, args.This());

    if (session->is_destroyed()) {
      THROW_ERR_INVALID_STATE(env, "Session is destroyed");
    }

    DCHECK(args[0]->IsArrayBufferView());
    SendPendingDataScope send_scope(session);
    args.GetReturnValue().Set(BigInt::New(
        env->isolate(),
        session->SendDatagram(Store(args[0].As<ArrayBufferView>()))));
  }

  // Internal ngtcp2 callbacks

  static int on_acknowledge_stream_data_offset(ngtcp2_conn* conn,
                                               int64_t stream_id,
                                               uint64_t offset,
                                               uint64_t datalen,
                                               void* user_data,
                                               void* stream_user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    // The callback will be invoked with datalen 0 if a zero-length
    // stream frame with fin flag set is received. In that case, let's
    // just ignore it.
    // Per ngtcp2, the range of bytes that are being acknowledged here
    // are `[offset, offset + datalen]` but we only really care about
    // the datalen as our accounting does not track the offset and
    // acknowledges should never come out of order here.
    if (datalen == 0) return NGTCP2_SUCCESS;
    return session->application().AcknowledgeStreamData(stream_id, datalen)
               ? NGTCP2_SUCCESS
               : NGTCP2_ERR_CALLBACK_FAILURE;
  }

  static int on_acknowledge_datagram(ngtcp2_conn* conn,
                                     uint64_t dgram_id,
                                     void* user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    session->DatagramStatus(dgram_id, quic::DatagramStatus::ACKNOWLEDGED);
    return NGTCP2_SUCCESS;
  }

  static int on_cid_status(ngtcp2_conn* conn,
                           ngtcp2_connection_id_status_type type,
                           uint64_t seq,
                           const ngtcp2_cid* cid,
                           const uint8_t* token,
                           void* user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    std::optional<StatelessResetToken> maybe_reset_token;
    if (token != nullptr) maybe_reset_token.emplace(token);
    auto& endpoint = session->endpoint();
    switch (type) {
      case NGTCP2_CONNECTION_ID_STATUS_TYPE_ACTIVATE: {
        endpoint.AssociateCID(session->config().scid, CID(cid));
        if (token != nullptr) {
          endpoint.AssociateStatelessResetToken(StatelessResetToken(token),
                                                session);
        }
        break;
      }
      case NGTCP2_CONNECTION_ID_STATUS_TYPE_DEACTIVATE: {
        endpoint.DisassociateCID(CID(cid));
        if (token != nullptr) {
          endpoint.DisassociateStatelessResetToken(StatelessResetToken(token));
        }
        break;
      }
    }
    return NGTCP2_SUCCESS;
  }

  static int on_extend_max_remote_streams_bidi(ngtcp2_conn* conn,
                                               uint64_t max_streams,
                                               void* user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    // TODO(@jasnell): Do anything here?
    return NGTCP2_SUCCESS;
  }

  static int on_extend_max_remote_streams_uni(ngtcp2_conn* conn,
                                              uint64_t max_streams,
                                              void* user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    // TODO(@jasnell): Do anything here?
    return NGTCP2_SUCCESS;
  }

  static int on_extend_max_streams_bidi(ngtcp2_conn* conn,
                                        uint64_t max_streams,
                                        void* user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    session->ProcessPendingBidiStreams();
    return NGTCP2_SUCCESS;
  }

  static int on_extend_max_streams_uni(ngtcp2_conn* conn,
                                       uint64_t max_streams,
                                       void* user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    session->ProcessPendingUniStreams();
    return NGTCP2_SUCCESS;
  }

  static int on_extend_max_stream_data(ngtcp2_conn* conn,
                                       int64_t stream_id,
                                       uint64_t max_data,
                                       void* user_data,
                                       void* stream_user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    session->application().ExtendMaxStreamData(Stream::From(stream_user_data),
                                               max_data);
    return NGTCP2_SUCCESS;
  }

  static int on_get_new_cid(ngtcp2_conn* conn,
                            ngtcp2_cid* cid,
                            uint8_t* token,
                            size_t cidlen,
                            void* user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    session->GenerateNewConnectionId(cid, cidlen, token);
    return NGTCP2_SUCCESS;
  }

  static int on_handshake_completed(ngtcp2_conn* conn, void* user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    return session->HandshakeCompleted() ? NGTCP2_SUCCESS
                                         : NGTCP2_ERR_CALLBACK_FAILURE;
  }

  static int on_handshake_confirmed(ngtcp2_conn* conn, void* user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    session->HandshakeConfirmed();
    return NGTCP2_SUCCESS;
  }

  static int on_lost_datagram(ngtcp2_conn* conn,
                              uint64_t dgram_id,
                              void* user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    session->DatagramStatus(dgram_id, quic::DatagramStatus::LOST);
    return NGTCP2_SUCCESS;
  }

  static int on_path_validation(ngtcp2_conn* conn,
                                uint32_t flags,
                                const ngtcp2_path* path,
                                const ngtcp2_path* old_path,
                                ngtcp2_path_validation_result res,
                                void* user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    bool flag_preferred_address =
        flags & NGTCP2_PATH_VALIDATION_FLAG_PREFERRED_ADDR;
    ValidatedPath newValidatedPath{
        std::make_shared<SocketAddress>(path->local.addr),
        std::make_shared<SocketAddress>(path->remote.addr)};
    std::optional<ValidatedPath> oldValidatedPath = std::nullopt;
    if (old_path != nullptr) {
      oldValidatedPath =
          ValidatedPath{std::make_shared<SocketAddress>(old_path->local.addr),
                        std::make_shared<SocketAddress>(old_path->remote.addr)};
    }
    session->EmitPathValidation(static_cast<PathValidationResult>(res),
                                PathValidationFlags{flag_preferred_address},
                                newValidatedPath,
                                oldValidatedPath);
    return NGTCP2_SUCCESS;
  }

  static int on_receive_datagram(ngtcp2_conn* conn,
                                 uint32_t flags,
                                 const uint8_t* data,
                                 size_t datalen,
                                 void* user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    session->DatagramReceived(
        data,
        datalen,
        DatagramReceivedFlags{
            .early = (flags & NGTCP2_DATAGRAM_FLAG_0RTT) ==
                     NGTCP2_DATAGRAM_FLAG_0RTT,
        });
    return NGTCP2_SUCCESS;
  }

  static int on_receive_new_token(ngtcp2_conn* conn,
                                  const uint8_t* token,
                                  size_t tokenlen,
                                  void* user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    // We currently do nothing with this callback.
    return NGTCP2_SUCCESS;
  }

  static int on_receive_rx_key(ngtcp2_conn* conn,
                               ngtcp2_encryption_level level,
                               void* user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    CHECK(!session->is_server());

    if (level != NGTCP2_ENCRYPTION_LEVEL_1RTT) return NGTCP2_SUCCESS;

    Debug(session,
          "Receiving RX key for level %s for dcid %s",
          to_string(level),
          session->config().dcid);

    return session->application().Start() ? NGTCP2_SUCCESS
                                          : NGTCP2_ERR_CALLBACK_FAILURE;
  }

  static int on_receive_stateless_reset(ngtcp2_conn* conn,
                                        const ngtcp2_pkt_stateless_reset* sr,
                                        void* user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    session->impl_->state_->stateless_reset = 1;
    return NGTCP2_SUCCESS;
  }

  static int on_receive_stream_data(ngtcp2_conn* conn,
                                    uint32_t flags,
                                    int64_t stream_id,
                                    uint64_t offset,
                                    const uint8_t* data,
                                    size_t datalen,
                                    void* user_data,
                                    void* stream_user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    Stream::ReceiveDataFlags data_flags{
        // The fin flag indicates that this is the last chunk of data we will
        // receive on this stream.
        .fin = (flags & NGTCP2_STREAM_DATA_FLAG_FIN) ==
               NGTCP2_STREAM_DATA_FLAG_FIN,
        // Stream data is early if it is received before the TLS handshake is
        // complete.
        .early = (flags & NGTCP2_STREAM_DATA_FLAG_0RTT) ==
                 NGTCP2_STREAM_DATA_FLAG_0RTT,
    };

    // We received data for a stream! What we don't know yet at this point
    // is whether the application wants us to treat this as a control stream
    // data (something the application will handle on its own) or a user stream
    // data (something that we should create a Stream handle for that is passed
    // out to JavaScript). HTTP3, for instance, will generally create three
    // control stream in either direction and we want to make sure those are
    // never exposed to users and that we don't waste time creating Stream
    // handles for them. So, what we do here is pass the stream data on to the
    // application for processing. If it ends up being a user stream, the
    // application will handle creating the Stream handle and passing that off
    // to the JavaScript side.
    if (!session->application().ReceiveStreamData(
            stream_id, data, datalen, data_flags, stream_user_data)) {
      return NGTCP2_ERR_CALLBACK_FAILURE;
    }

    return NGTCP2_SUCCESS;
  }

  static int on_receive_tx_key(ngtcp2_conn* conn,
                               ngtcp2_encryption_level level,
                               void* user_data) {
    NGTCP2_CALLBACK_SCOPE(session);
    CHECK(session->is_server());

    if (level != NGTCP2_ENCRYPTION_LEVEL_1RTT) return NGTCP2_SUCCESS;

    Debug(session,
          "Receiving TX key for level %s for dcid %s",
          to_string(level),
          session->config().dcid);
    return session->application().Start() ? NGTCP2_SUCCESS
                                          : NGTCP2_ERR_CALLBACK_FAILURE;
  }

  static int on_receive_version_negotiation(ngtcp2_conn* conn,
                                            const ngtcp2_pkt_hd* hd,
                                            const uint32_t* sv,
                                            size_t nsv,
                                            void* user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    session->EmitVersionNegotiation(*hd, sv, nsv);
    return NGTCP2_SUCCESS;
  }

  static int on_remove_connection_id(ngtcp2_conn* conn,
                                     const ngtcp2_cid* cid,
                                     void* user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    session->endpoint().DisassociateCID(CID(cid));
    return NGTCP2_SUCCESS;
  }

  static int on_select_preferred_address(ngtcp2_conn* conn,
                                         ngtcp2_path* dest,
                                         const ngtcp2_preferred_addr* paddr,
                                         void* user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    PreferredAddress preferred_address(dest, paddr);
    session->SelectPreferredAddress(&preferred_address);
    return NGTCP2_SUCCESS;
  }

  static int on_stream_close(ngtcp2_conn* conn,
                             uint32_t flags,
                             int64_t stream_id,
                             uint64_t app_error_code,
                             void* user_data,
                             void* stream_user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    if (flags & NGTCP2_STREAM_CLOSE_FLAG_APP_ERROR_CODE_SET) {
      session->application().StreamClose(
          Stream::From(stream_user_data),
          QuicError::ForApplication(app_error_code));
    } else {
      session->application().StreamClose(Stream::From(stream_user_data));
    }
    return NGTCP2_SUCCESS;
  }

  static int on_stream_reset(ngtcp2_conn* conn,
                             int64_t stream_id,
                             uint64_t final_size,
                             uint64_t app_error_code,
                             void* user_data,
                             void* stream_user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    session->application().StreamReset(
        Stream::From(stream_user_data),
        final_size,
        QuicError::ForApplication(app_error_code));
    return NGTCP2_SUCCESS;
  }

  static int on_stream_stop_sending(ngtcp2_conn* conn,
                                    int64_t stream_id,
                                    uint64_t app_error_code,
                                    void* user_data,
                                    void* stream_user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    session->application().StreamStopSending(
        Stream::From(stream_user_data),
        QuicError::ForApplication(app_error_code));
    return NGTCP2_SUCCESS;
  }

  static void on_rand(uint8_t* dest,
                      size_t destlen,
                      const ngtcp2_rand_ctx* rand_ctx) {
    CHECK(ncrypto::CSPRNG(dest, destlen));
  }

  static int on_early_data_rejected(ngtcp2_conn* conn, void* user_data) {
    // TODO(@jasnell): Called when early data was rejected by server during the
    // TLS handshake or client decided not to attempt early data.
    return NGTCP2_SUCCESS;
  }

  static constexpr ngtcp2_callbacks CLIENT = {
      ngtcp2_crypto_client_initial_cb,
      nullptr,
      ngtcp2_crypto_recv_crypto_data_cb,
      on_handshake_completed,
      on_receive_version_negotiation,
      ngtcp2_crypto_encrypt_cb,
      ngtcp2_crypto_decrypt_cb,
      ngtcp2_crypto_hp_mask_cb,
      on_receive_stream_data,
      on_acknowledge_stream_data_offset,
      nullptr,
      on_stream_close,
      on_receive_stateless_reset,
      ngtcp2_crypto_recv_retry_cb,
      on_extend_max_streams_bidi,
      on_extend_max_streams_uni,
      on_rand,
      on_get_new_cid,
      on_remove_connection_id,
      ngtcp2_crypto_update_key_cb,
      on_path_validation,
      on_select_preferred_address,
      on_stream_reset,
      on_extend_max_remote_streams_bidi,
      on_extend_max_remote_streams_uni,
      on_extend_max_stream_data,
      on_cid_status,
      on_handshake_confirmed,
      on_receive_new_token,
      ngtcp2_crypto_delete_crypto_aead_ctx_cb,
      ngtcp2_crypto_delete_crypto_cipher_ctx_cb,
      on_receive_datagram,
      on_acknowledge_datagram,
      on_lost_datagram,
      ngtcp2_crypto_get_path_challenge_data_cb,
      on_stream_stop_sending,
      ngtcp2_crypto_version_negotiation_cb,
      on_receive_rx_key,
      nullptr,
      on_early_data_rejected};

  static constexpr ngtcp2_callbacks SERVER = {
      nullptr,
      ngtcp2_crypto_recv_client_initial_cb,
      ngtcp2_crypto_recv_crypto_data_cb,
      on_handshake_completed,
      nullptr,
      ngtcp2_crypto_encrypt_cb,
      ngtcp2_crypto_decrypt_cb,
      ngtcp2_crypto_hp_mask_cb,
      on_receive_stream_data,
      on_acknowledge_stream_data_offset,
      nullptr,
      on_stream_close,
      on_receive_stateless_reset,
      nullptr,
      on_extend_max_streams_bidi,
      on_extend_max_streams_uni,
      on_rand,
      on_get_new_cid,
      on_remove_connection_id,
      ngtcp2_crypto_update_key_cb,
      on_path_validation,
      nullptr,
      on_stream_reset,
      on_extend_max_remote_streams_bidi,
      on_extend_max_remote_streams_uni,
      on_extend_max_stream_data,
      on_cid_status,
      nullptr,
      nullptr,
      ngtcp2_crypto_delete_crypto_aead_ctx_cb,
      ngtcp2_crypto_delete_crypto_cipher_ctx_cb,
      on_receive_datagram,
      on_acknowledge_datagram,
      on_lost_datagram,
      ngtcp2_crypto_get_path_challenge_data_cb,
      on_stream_stop_sending,
      ngtcp2_crypto_version_negotiation_cb,
      nullptr,
      on_receive_tx_key,
      on_early_data_rejected};
};

#undef NGTCP2_CALLBACK_SCOPE

// ============================================================================
Session::SendPendingDataScope::SendPendingDataScope(Session* session)
    : session(session) {
  CHECK_NOT_NULL(session);
  CHECK(!session->is_destroyed());
  ++session->impl_->send_scope_depth_;
}

Session::SendPendingDataScope::SendPendingDataScope(
    const BaseObjectPtr<Session>& session)
    : SendPendingDataScope(session.get()) {}

Session::SendPendingDataScope::~SendPendingDataScope() {
  if (session->is_destroyed()) return;
  DCHECK_GE(session->impl_->send_scope_depth_, 1);
  if (--session->impl_->send_scope_depth_ == 0) {
    session->application().SendPendingData();
  }
}

// ============================================================================
bool Session::HasInstance(Environment* env, Local<Value> value) {
  return GetConstructorTemplate(env)->HasInstance(value);
}

BaseObjectPtr<Session> Session::Create(
    Endpoint* endpoint,
    const Config& config,
    TLSContext* tls_context,
    const std::optional<SessionTicket>& ticket) {
  Local<Object> obj;
  if (!GetConstructorTemplate(endpoint->env())
           ->InstanceTemplate()
           ->NewInstance(endpoint->env()->context())
           .ToLocal(&obj)) {
    return {};
  }

  return MakeDetachedBaseObject<Session>(
      endpoint, obj, config, tls_context, ticket);
}

Session::Session(Endpoint* endpoint,
                 Local<Object> object,
                 const Config& config,
                 TLSContext* tls_context,
                 const std::optional<SessionTicket>& session_ticket)
    : AsyncWrap(endpoint->env(), object, AsyncWrap::PROVIDER_QUIC_SESSION),
      side_(config.side),
      allocator_(BindingData::Get(env())),
      impl_(std::make_unique<Impl>(this, endpoint, config)),
      connection_(InitConnection()),
      tls_session_(tls_context->NewSession(this, session_ticket)) {
  DCHECK(impl_);
  MakeWeak();
  Debug(this, "Session created.");

  const auto defineProperty = [&](auto name, auto value) {
    object
        ->DefineOwnProperty(
            env()->context(), name, value, PropertyAttribute::ReadOnly)
        .Check();
  };

  defineProperty(env()->state_string(), impl_->state_.GetArrayBuffer());
  defineProperty(env()->stats_string(), impl_->stats_.GetArrayBuffer());

  auto& binding = BindingData::Get(env());

  if (config.options.qlog) [[unlikely]] {
    qlog_stream_ = LogStream::Create(env());
    defineProperty(binding.qlog_string(), qlog_stream_->object());
  }

  if (config.options.tls_options.keylog) [[unlikely]] {
    keylog_stream_ = LogStream::Create(env());
    defineProperty(binding.keylog_string(), keylog_stream_->object());
  }

  UpdateDataStats();
}

Session::~Session() {
  // Double check that Destroy() was called first.
  CHECK(is_destroyed());
}

Session::QuicConnectionPointer Session::InitConnection() {
  ngtcp2_conn* conn;
  Path path(config().local_address, config().remote_address);
  TransportParams::Config tp_config(side_, config().ocid, config().retry_scid);
  TransportParams transport_params(tp_config,
                                   config().options.transport_params);
  transport_params.GenerateSessionTokens(this);

  switch (side_) {
    case Side::SERVER: {
      // On the server side there are certain transport parameters that are
      // required to be sent. Let's make sure they are set.
      const ngtcp2_transport_params& params = transport_params;
      CHECK_EQ(params.original_dcid_present, 1);
      CHECK_EQ(ngtcp2_conn_server_new(&conn,
                                      config().dcid,
                                      config().scid,
                                      path,
                                      config().version,
                                      &Impl::SERVER,
                                      &config().settings,
                                      transport_params,
                                      &allocator_,
                                      this),
               0);
      break;
    }
    case Side::CLIENT: {
      CHECK_EQ(ngtcp2_conn_client_new(&conn,
                                      config().dcid,
                                      config().scid,
                                      path,
                                      config().version,
                                      &Impl::CLIENT,
                                      &config().settings,
                                      transport_params,
                                      &allocator_,
                                      this),
               0);
      break;
    }
  }
  return QuicConnectionPointer(conn);
}

Session::operator ngtcp2_conn*() const {
  return connection_.get();
}

bool Session::is_server() const {
  return side_ == Side::SERVER;
}

bool Session::is_destroyed() const {
  return !impl_;
}

bool Session::is_destroyed_or_closing() const {
  return !impl_ || impl_->state_->closing;
}

void Session::Close(Session::CloseMethod method) {
  if (is_destroyed()) return;
  auto& stats_ = impl_->stats_;

  if (impl_->last_error_) {
    Debug(this, "Closing with error: %s", impl_->last_error_);
  }

  STAT_RECORD_TIMESTAMP(Stats, closing_at);
  impl_->state_->closing = 1;

  // With both the DEFAULT and SILENT options, we will proceed to closing
  // the session immediately. All open streams will be immediately destroyed
  // with whatever is set as the last error. The session will then be destroyed
  // with a possible roundtrip to JavaScript to emit a close event and clean up
  // any JavaScript side state. Importantly, these operations are all done
  // synchronously, so the session will be destroyed once FinishClose returns.
  //
  // With the graceful option, we will wait for all streams to close on their
  // own before proceeding with the FinishClose operation. New streams will
  // be rejected, however.

  switch (method) {
    case CloseMethod::DEFAULT: {
      Debug(this, "Immediately closing session");
      impl_->state_->silent_close = 0;
      return FinishClose();
    }
    case CloseMethod::SILENT: {
      Debug(this, "Immediately closing session silently");
      impl_->state_->silent_close = 1;
      return FinishClose();
    }
    case CloseMethod::GRACEFUL: {
      // If there are no open streams, then we can close just immediately and
      // not worry about waiting around.
      if (impl_->streams_.empty()) {
        impl_->state_->silent_close = 0;
        impl_->state_->graceful_close = 0;
        return FinishClose();
      }

      // If we are already closing gracefully, do nothing.
      if (impl_->state_->graceful_close) [[unlikely]] {
        return;
      }
      impl_->state_->graceful_close = 1;
      Debug(this,
            "Gracefully closing session (waiting on %zu streams)",
            impl_->streams_.size());
      return;
    }
  }
  UNREACHABLE();
}

void Session::FinishClose() {
  // FinishClose() should be called only after, and as a result of, Close()
  // being called first.
  DCHECK(!is_destroyed());
  DCHECK(impl_->state_->closing);

  // If impl_->Close() returns true, then the session can be destroyed
  // immediately without round-tripping through JavaScript.
  if (impl_->Close()) {
    return Destroy();
  }

  // Otherwise, we emit a close callback so that the JavaScript side can
  // clean up anything it needs to clean up before destroying.
  EmitClose();
}

void Session::Destroy() {
  // Destroy() should be called only after, and as a result of, Close()
  // being called first.
  DCHECK(impl_);
  DCHECK(impl_->state_->closing);
  Debug(this, "Session destroyed");
  impl_.reset();
  if (qlog_stream_ || keylog_stream_) {
    env()->SetImmediate(
        [qlog = qlog_stream_, keylog = keylog_stream_](Environment*) {
          if (qlog) qlog->End();
          if (keylog) keylog->End();
        });
  }
  qlog_stream_.reset();
  keylog_stream_.reset();
}

PendingStream::PendingStreamQueue& Session::pending_bidi_stream_queue() const {
  CHECK(!is_destroyed());
  return impl_->pending_bidi_stream_queue_;
}

PendingStream::PendingStreamQueue& Session::pending_uni_stream_queue() const {
  CHECK(!is_destroyed());
  return impl_->pending_uni_stream_queue_;
}

size_t Session::max_packet_size() const {
  CHECK(!is_destroyed());
  return ngtcp2_conn_get_max_tx_udp_payload_size(*this);
}

uint32_t Session::version() const {
  CHECK(!is_destroyed());
  return impl_->config_.version;
}

Endpoint& Session::endpoint() const {
  CHECK(!is_destroyed());
  return *impl_->endpoint_;
}

TLSSession& Session::tls_session() const {
  CHECK(!is_destroyed());
  return *tls_session_;
}

Session::Application& Session::application() const {
  CHECK(!is_destroyed());
  return *impl_->application_;
}

const SocketAddress& Session::remote_address() const {
  CHECK(!is_destroyed());
  return impl_->remote_address_;
}

const SocketAddress& Session::local_address() const {
  CHECK(!is_destroyed());
  return impl_->local_address_;
}

std::string Session::diagnostic_name() const {
  const auto get_type = [&] { return is_server() ? "server" : "client"; };

  return std::string("Session (") + get_type() + "," +
         std::to_string(env()->thread_id()) + ":" +
         std::to_string(static_cast<int64_t>(get_async_id())) + ")";
}

const Session::Config& Session::config() const {
  CHECK(!is_destroyed());
  return impl_->config_;
}

Session::Config& Session::config() {
  CHECK(!is_destroyed());
  return impl_->config_;
}

const Session::Options& Session::options() const {
  CHECK(!is_destroyed());
  return impl_->config_.options;
}

void Session::HandleQlog(uint32_t flags, const void* data, size_t len) {
  DCHECK(qlog_stream_);
  // Fun fact... ngtcp2 does not emit the final qlog statement until the
  // ngtcp2_conn object is destroyed.
  std::vector<uint8_t> buffer(len);
  memcpy(buffer.data(), data, len);
  Debug(this, "Emitting qlog data to the qlog stream");
  env()->SetImmediate([ptr = qlog_stream_, buffer = std::move(buffer), flags](
                          Environment*) {
    ptr->Emit(buffer.data(),
              buffer.size(),
              flags & NGTCP2_QLOG_WRITE_FLAG_FIN ? LogStream::EmitOption::FIN
                                                 : LogStream::EmitOption::NONE);
  });
}

const TransportParams Session::local_transport_params() const {
  CHECK(!is_destroyed());
  return ngtcp2_conn_get_local_transport_params(*this);
}

const TransportParams Session::remote_transport_params() const {
  CHECK(!is_destroyed());
  return ngtcp2_conn_get_remote_transport_params(*this);
}

void Session::SetLastError(QuicError&& error) {
  CHECK(!is_destroyed());
  Debug(this, "Setting last error to %s", error);
  impl_->last_error_ = std::move(error);
}

bool Session::Receive(Store&& store,
                      const SocketAddress& local_address,
                      const SocketAddress& remote_address) {
  CHECK(!is_destroyed());
  impl_->remote_address_ = remote_address;

  // When we are done processing thins packet, we arrange to send any
  // pending data for this session.
  SendPendingDataScope send_scope(this);

  ngtcp2_vec vec = store;
  Path path(local_address, remote_address);

  Debug(this,
        "Session is receiving %zu-byte packet received along path %s",
        vec.len,
        path);

  // It is important to understand that reading the packet will cause
  // callback functions to be invoked, any one of which could lead to
  // the Session being closed/destroyed synchronously. After calling
  // ngtcp2_conn_read_pkt here, we will need to double check that the
  // session is not destroyed before we try doing anything with it
  // (like updating stats, sending pending data, etc).
  int err = ngtcp2_conn_read_pkt(
      *this, path, nullptr, vec.base, vec.len, uv_hrtime());

  switch (err) {
    case 0: {
      Debug(this, "Session successfully received %zu-byte packet", vec.len);
      if (!is_destroyed()) [[unlikely]] {
        auto& stats_ = impl_->stats_;
        STAT_INCREMENT_N(Stats, bytes_received, vec.len);
      }
      return true;
    }
    case NGTCP2_ERR_REQUIRED_TRANSPORT_PARAM: {
      Debug(this,
            "Receiving packet failed: "
            "Remote peer failed to send a required transport parameter");
      if (!is_destroyed()) [[likely]] {
        SetLastError(QuicError::ForTransport(err));
        Close();
      }
      return false;
    }
    case NGTCP2_ERR_DRAINING: {
      // Connection has entered the draining state, no further data should be
      // sent. This happens when the remote peer has already sent a
      // CONNECTION_CLOSE.
      Debug(this, "Receiving packet failed: Session is draining");
      return false;
    }
    case NGTCP2_ERR_CLOSING: {
      // Connection has entered the closing state, no further data should be
      // sent. This happens when the local peer has called
      // ngtcp2_conn_write_connection_close.
      Debug(this, "Receiving packet failed: Session is closing");
      return false;
    }
    case NGTCP2_ERR_CRYPTO: {
      Debug(this, "Receiving packet failed: Crypto error");
      // Crypto error happened! Set the last error to the tls alert
      if (!is_destroyed()) [[likely]] {
        SetLastError(QuicError::ForTlsAlert(ngtcp2_conn_get_tls_alert(*this)));
        Close();
      }
      return false;
    }
    case NGTCP2_ERR_RETRY: {
      // This should only ever happen on the server. We have to send a path
      // validation challenge in the form of a RETRY packet to the peer and
      // drop the connection.
      DCHECK(is_server());
      Debug(this, "Receiving packet failed: Server must send a retry packet");
      if (!is_destroyed()) {
        endpoint().SendRetry(PathDescriptor{
            version(),
            config().dcid,
            config().scid,
            impl_->local_address_,
            impl_->remote_address_,
        });
        Close(CloseMethod::SILENT);
      }
      return false;
    }
    case NGTCP2_ERR_DROP_CONN: {
      // There's nothing else to do but drop the connection state.
      Debug(this, "Receiving packet failed: Session must drop the connection");
      if (!is_destroyed()) {
        Close(CloseMethod::SILENT);
      }
      return false;
    }
  }

  // Shouldn't happen but just in case... handle other unknown errors
  Debug(this,
        "Receiving packet failed: "
        "Unexpected error %d while receiving packet",
        err);
  if (!is_destroyed()) {
    SetLastError(QuicError::ForNgtcp2Error(err));
    Close();
  }
  return false;
}

void Session::Send(const BaseObjectPtr<Packet>& packet) {
  // Sending a Packet is generally best effort. If we're not in a state
  // where we can send a packet, it's ok to drop it on the floor. The
  // packet loss mechanisms will cause the packet data to be resent later
  // if appropriate (and possible).

  // That said, we should never be trying to send a packet when we're in
  // a draining period.
  CHECK(!is_destroyed());
  DCHECK(!is_in_draining_period());

  if (!can_send_packets()) [[unlikely]] {
    return packet->Done(UV_ECANCELED);
  }

  Debug(this, "Session is sending %s", packet->ToString());
  auto& stats_ = impl_->stats_;
  STAT_INCREMENT_N(Stats, bytes_sent, packet->length());
  endpoint().Send(packet);
}

void Session::Send(const BaseObjectPtr<Packet>& packet,
                   const PathStorage& path) {
  UpdatePath(path);
  Send(packet);
}

uint64_t Session::SendDatagram(Store&& data) {
  CHECK(!is_destroyed());
  if (!can_send_packets()) {
    Debug(this, "Unable to send datagram");
    return 0;
  }

  const ngtcp2_transport_params* tp = remote_transport_params();
  uint64_t max_datagram_size = tp->max_datagram_frame_size;

  if (max_datagram_size == 0) {
    Debug(this, "Datagrams are disabled");
    return 0;
  }

  if (data.length() > max_datagram_size) {
    Debug(this, "Ignoring oversized datagram");
    return 0;
  }

  if (data.length() == 0) {
    Debug(this, "Ignoring empty datagram");
    return 0;
  }

  BaseObjectPtr<Packet> packet;
  uint8_t* pos = nullptr;
  int accepted = 0;
  ngtcp2_vec vec = data;
  PathStorage path;
  int flags = NGTCP2_WRITE_DATAGRAM_FLAG_MORE;
  uint64_t did = impl_->state_->last_datagram_id + 1;

  Debug(this, "Sending %zu-byte datagram %" PRIu64, data.length(), did);

  // Let's give it a max number of attempts to send the datagram.
  static const int kMaxAttempts = 16;
  int attempts = 0;

  auto on_exit = OnScopeLeave([&] {
    UpdatePacketTxTime();
    UpdateTimer();
    UpdateDataStats();
  });

  for (;;) {
    // We may have to make several attempts at encoding and sending the
    // datagram packet. On each iteration here we'll try to encode the
    // datagram. It's entirely up to ngtcp2 whether to include the datagram
    // in the packet on each call to ngtcp2_conn_writev_datagram.
    if (!packet) {
      packet = Packet::Create(env(),
                              endpoint(),
                              impl_->remote_address_,
                              ngtcp2_conn_get_max_tx_udp_payload_size(*this),
                              "datagram");
      // Typically sending datagrams is best effort, but if we cannot create
      // the packet, then we handle it as a fatal error.
      if (!packet) {
        SetLastError(QuicError::ForNgtcp2Error(NGTCP2_ERR_INTERNAL));
        Close(CloseMethod::SILENT);
        return 0;
      }
      pos = ngtcp2_vec(*packet).base;
    }

    ssize_t nwrite = ngtcp2_conn_writev_datagram(*this,
                                                 &path.path,
                                                 nullptr,
                                                 pos,
                                                 packet->length(),
                                                 &accepted,
                                                 flags,
                                                 did,
                                                 &vec,
                                                 1,
                                                 uv_hrtime());

    if (nwrite <= 0) {
      // Nothing was written to the packet.
      switch (nwrite) {
        case 0: {
          // We cannot send data because of congestion control or the data will
          // not fit. Since datagrams are best effort, we are going to abandon
          // the attempt and just return.
          CHECK_EQ(accepted, 0);
          packet->Done(UV_ECANCELED);
          return 0;
        }
        case NGTCP2_ERR_WRITE_MORE: {
          // We keep on looping! Keep on sending!
          continue;
        }
        case NGTCP2_ERR_INVALID_STATE: {
          // The remote endpoint does not want to accept datagrams. That's ok,
          // just return 0.
          packet->Done(UV_ECANCELED);
          return 0;
        }
        case NGTCP2_ERR_INVALID_ARGUMENT: {
          // The datagram is too large. That should have been caught above but
          // that's ok. We'll just abandon the attempt and return.
          packet->Done(UV_ECANCELED);
          return 0;
        }
        case NGTCP2_ERR_PKT_NUM_EXHAUSTED: {
          // We've exhausted the packet number space. Sadly we have to treat it
          // as a fatal condition (which we will do after the switch)
          break;
        }
        case NGTCP2_ERR_CALLBACK_FAILURE: {
          // There was an internal failure. Sadly we have to treat it as a fatal
          // condition. (which we will do after the switch)
          break;
        }
      }
      packet->Done(UV_ECANCELED);
      SetLastError(QuicError::ForTransport(nwrite));
      Close(CloseMethod::SILENT);
      return 0;
    }

    // In this case, a complete packet was written and we need to send it along.
    // Note that this doesn't mean that the packet actually contains the
    // datagram! We'll check that next by checking the accepted value.
    packet->Truncate(nwrite);
    Send(packet);
    packet.reset();

    if (accepted) {
      // Yay! The datagram was accepted into the packet we just sent and we can
      // return the datagram ID.
      Debug(this, "Datagram %" PRIu64 " sent", did);
      auto& stats_ = impl_->stats_;
      STAT_INCREMENT(Stats, datagrams_sent);
      STAT_INCREMENT_N(Stats, bytes_sent, vec.len);
      impl_->state_->last_datagram_id = did;
      return did;
    }

    // We sent a packet, but it wasn't the datagram packet. That can happen.
    // Let's loop around and try again.
    if (++attempts == kMaxAttempts) [[unlikely]] {
      Debug(this, "Too many attempts to send datagram. Canceling.");
      // Too many attempts to send the datagram.
      break;
    }

    // If we get here that means the datagram has not yet been sent.
    // We're going to loop around to try again.
  }

  return 0;
}

void Session::UpdatePacketTxTime() {
  CHECK(!is_destroyed());
  ngtcp2_conn_update_pkt_tx_time(*this, uv_hrtime());
}

void Session::UpdatePath(const PathStorage& storage) {
  CHECK(!is_destroyed());
  impl_->remote_address_.Update(storage.path.remote.addr,
                                storage.path.remote.addrlen);
  impl_->local_address_.Update(storage.path.local.addr,
                               storage.path.local.addrlen);
  Debug(this,
        "path updated. local %s, remote %s",
        impl_->local_address_,
        impl_->remote_address_);
}

BaseObjectPtr<Stream> Session::FindStream(int64_t id) const {
  if (is_destroyed()) return {};
  auto it = impl_->streams_.find(id);
  if (it == std::end(impl_->streams_)) return {};
  return it->second;
}

BaseObjectPtr<Stream> Session::CreateStream(
    int64_t id,
    CreateStreamOption option,
    std::shared_ptr<DataQueue> data_source) {
  if (!can_create_streams()) [[unlikely]]
    return {};
  if (auto stream = Stream::Create(this, id, std::move(data_source)))
      [[likely]] {
    AddStream(stream, option);
    return stream;
  }
  return {};
}

MaybeLocal<Object> Session::OpenStream(Direction direction,
                                       std::shared_ptr<DataQueue> data_source) {
  // If can_create_streams() returns false, we are not able to open a stream
  // at all now, even in a pending state. The implication is that that session
  // is destroyed or closing.
  if (!can_create_streams()) [[unlikely]]
    return {};

  // If can_open_streams() returns false, we are able to create streams but
  // they will remain in a pending state. The implication is that the session
  // TLS handshake is still progressing. Note that when a pending stream is
  // created, it will not be listed in the streams list.
  if (!can_open_streams()) {
    if (auto stream = Stream::Create(this, direction, std::move(data_source)))
        [[likely]] {
      return stream->object();
    }
    return {};
  }

  int64_t id = -1;
  auto open = [&] {
    switch (direction) {
      case Direction::BIDIRECTIONAL: {
        Debug(this, "Opening bidirectional stream");
        return ngtcp2_conn_open_bidi_stream(*this, &id, nullptr);
      }
      case Direction::UNIDIRECTIONAL: {
        Debug(this, "Opening uni-directional stream");
        return ngtcp2_conn_open_uni_stream(*this, &id, nullptr);
      }
    }
    UNREACHABLE();
  };

  switch (open()) {
    case 0: {
      // Woo! Our stream was created.
      CHECK_GE(id, 0);
      if (auto stream = CreateStream(
              id, CreateStreamOption::DO_NOT_NOTIFY, std::move(data_source)))
          [[likely]] {
        return stream->object();
      }
      break;
    }
    case NGTCP2_ERR_STREAM_ID_BLOCKED: {
      // The stream cannot yet be opened.
      // This is typically caused by the application exceeding the allowed max
      // number of concurrent streams. We will allow the stream to be created
      // in a pending state.
      if (auto stream = Stream::Create(this, direction, std::move(data_source)))
          [[likely]] {
        return stream->object();
      }
      break;
    }
  }
  return {};
}

void Session::AddStream(BaseObjectPtr<Stream> stream,
                        CreateStreamOption option) {
  CHECK(!is_destroyed());
  CHECK(stream);

  auto id = stream->id();
  auto direction = stream->direction();

  // Let's double check that a stream with the given id does not already
  // exist. If it does, that means we've got a bug somewhere.
  DCHECK_EQ(impl_->streams_.find(id), impl_->streams_.end());

  Debug(this, "Adding stream %" PRIi64 " to session", id);

  // The streams_ map becomes the sole owner of the Stream instance.
  // We mark the stream detached so that when it is removed from
  // the session, or is the session is destroyed, the stream will
  // also be destroyed.
  impl_->streams_[id] = stream;
  stream->Detach();

  ngtcp2_conn_set_stream_user_data(*this, id, stream.get());

  if (option == CreateStreamOption::NOTIFY) {
    EmitStream(stream);
  }

  // Update tracking statistics for the number of streams associated with this
  // session.
  auto& stats_ = impl_->stats_;
  if (ngtcp2_conn_is_local_stream(*this, id)) {
    switch (direction) {
      case Direction::BIDIRECTIONAL: {
        STAT_INCREMENT(Stats, bidi_out_stream_count);
        break;
      }
      case Direction::UNIDIRECTIONAL: {
        STAT_INCREMENT(Stats, uni_out_stream_count);
        break;
      }
    }
  } else {
    switch (direction) {
      case Direction::BIDIRECTIONAL: {
        STAT_INCREMENT(Stats, bidi_in_stream_count);
        break;
      }
      case Direction::UNIDIRECTIONAL: {
        STAT_INCREMENT(Stats, uni_in_stream_count);
        break;
      }
    }
  }
}

void Session::RemoveStream(int64_t id) {
  CHECK(!is_destroyed());
  Debug(this, "Removing stream %" PRIi64 " from session", id);
  if (!is_in_draining_period() && !is_in_closing_period() &&
      !ngtcp2_conn_is_local_stream(*this, id)) {
    if (ngtcp2_is_bidi_stream(id)) {
      ngtcp2_conn_extend_max_streams_bidi(*this, 1);
    } else {
      ngtcp2_conn_extend_max_streams_uni(*this, 1);
    }
  }

  ngtcp2_conn_set_stream_user_data(*this, id, nullptr);

  // Note that removing the stream from the streams map likely releases
  // the last BaseObjectPtr holding onto the Stream instance, at which
  // point it will be freed. If there are other BaseObjectPtr instances
  // or other references to the Stream, however, freeing will be deferred.
  // In either case, we cannot assume that the stream still exists after
  // this call.
  impl_->streams_.erase(id);

  // If we are gracefully closing and there are no more streams,
  // then we can proceed to finishing the close now. Note that the
  // expectation is that the session will be destroyed once FinishClose
  // returns.
  if (impl_->state_->closing && impl_->state_->graceful_close) {
    FinishClose();
    CHECK(is_destroyed());
  }
}

void Session::ResumeStream(int64_t id) {
  CHECK(!is_destroyed());
  SendPendingDataScope send_scope(this);
  application().ResumeStream(id);
}

void Session::ShutdownStream(int64_t id, QuicError error) {
  CHECK(!is_destroyed());
  Debug(this, "Shutting down stream %" PRIi64 " with error %s", id, error);
  SendPendingDataScope send_scope(this);
  ngtcp2_conn_shutdown_stream(*this,
                              0,
                              id,
                              error.type() == QuicError::Type::APPLICATION
                                  ? error.code()
                                  : NGTCP2_APP_NOERROR);
}

void Session::ShutdownStreamWrite(int64_t id, QuicError code) {
  CHECK(!is_destroyed());
  Debug(this, "Shutting down stream %" PRIi64 " write with error %s", id, code);
  SendPendingDataScope send_scope(this);
  ngtcp2_conn_shutdown_stream_write(*this,
                                    0,
                                    id,
                                    code.type() == QuicError::Type::APPLICATION
                                        ? code.code()
                                        : NGTCP2_APP_NOERROR);
}

void Session::StreamDataBlocked(int64_t id) {
  CHECK(!is_destroyed());
  auto& stats_ = impl_->stats_;
  STAT_INCREMENT(Stats, block_count);
  application().BlockStream(id);
}

void Session::CollectSessionTicketAppData(
    SessionTicket::AppData* app_data) const {
  CHECK(!is_destroyed());
  application().CollectSessionTicketAppData(app_data);
}

SessionTicket::AppData::Status Session::ExtractSessionTicketAppData(
    const SessionTicket::AppData& app_data,
    SessionTicket::AppData::Source::Flag flag) {
  CHECK(!is_destroyed());
  return application().ExtractSessionTicketAppData(app_data, flag);
}

void Session::MemoryInfo(MemoryTracker* tracker) const {
  if (impl_) {
    tracker->TrackField("impl", impl_);
  }
  tracker->TrackField("tls_session", tls_session_);
  if (qlog_stream_) {
    tracker->TrackField("qlog_stream", qlog_stream_);
  }
  if (keylog_stream_) {
    tracker->TrackField("keylog_stream", keylog_stream_);
  }
}

bool Session::is_in_closing_period() const {
  CHECK(!is_destroyed());
  return ngtcp2_conn_in_closing_period(*this) != 0;
}

bool Session::is_in_draining_period() const {
  CHECK(!is_destroyed());
  return ngtcp2_conn_in_draining_period(*this) != 0;
}

bool Session::wants_session_ticket() const {
  return !is_destroyed() && impl_->state_->session_ticket == 1;
}

void Session::SetStreamOpenAllowed() {
  CHECK(!is_destroyed());
  impl_->state_->stream_open_allowed = 1;
}

bool Session::can_send_packets() const {
  // We can send packets if we're not in the middle of a ngtcp2 callback,
  // we're not destroyed, we're not in a draining or closing period, and
  // endpoint is set.
  return !is_destroyed() && !NgTcp2CallbackScope::in_ngtcp2_callback(env()) &&
         !is_in_draining_period() && !is_in_closing_period();
}

bool Session::can_create_streams() const {
  return !is_destroyed_or_closing() && !is_in_closing_period() &&
         !is_in_draining_period();
}

bool Session::can_open_streams() const {
  return !is_destroyed() && impl_->state_->stream_open_allowed;
}

uint64_t Session::max_data_left() const {
  CHECK(!is_destroyed());
  return ngtcp2_conn_get_max_data_left(*this);
}

uint64_t Session::max_local_streams_uni() const {
  CHECK(!is_destroyed());
  return ngtcp2_conn_get_streams_uni_left(*this);
}

uint64_t Session::max_local_streams_bidi() const {
  CHECK(!is_destroyed());
  return ngtcp2_conn_get_local_transport_params(*this)
      ->initial_max_streams_bidi;
}

void Session::set_wrapped() {
  CHECK(!is_destroyed());
  impl_->state_->wrapped = 1;
}

void Session::set_priority_supported(bool on) {
  CHECK(!is_destroyed());
  impl_->state_->priority_supported = on ? 1 : 0;
}

void Session::ExtendStreamOffset(int64_t id, size_t amount) {
  CHECK(!is_destroyed());
  Debug(this, "Extending stream %" PRIi64 " offset by %zu bytes", id, amount);
  ngtcp2_conn_extend_max_stream_offset(*this, id, amount);
}

void Session::ExtendOffset(size_t amount) {
  CHECK(!is_destroyed());
  Debug(this, "Extending offset by %zu bytes", amount);
  ngtcp2_conn_extend_max_offset(*this, amount);
}

void Session::UpdateDataStats() {
  Debug(this, "Updating data stats");
  auto& stats_ = impl_->stats_;
  ngtcp2_conn_info info;
  ngtcp2_conn_get_conn_info(*this, &info);
  STAT_SET(Stats, bytes_in_flight, info.bytes_in_flight);
  STAT_SET(Stats, cwnd, info.cwnd);
  STAT_SET(Stats, latest_rtt, info.latest_rtt);
  STAT_SET(Stats, min_rtt, info.min_rtt);
  STAT_SET(Stats, rttvar, info.rttvar);
  STAT_SET(Stats, smoothed_rtt, info.smoothed_rtt);
  STAT_SET(Stats, ssthresh, info.ssthresh);
  STAT_SET(
      Stats,
      max_bytes_in_flight,
      std::max(STAT_GET(Stats, max_bytes_in_flight), info.bytes_in_flight));
}

void Session::SendConnectionClose() {
  // Method is a non-op if the session is in a state where packets cannot
  // be transmitted to the remote peer.
  if (!can_send_packets()) return;

  Debug(this, "Sending connection close packet to peer");

  auto ErrorAndSilentClose = [&] {
    Debug(this, "Failed to create connection close packet");
    SetLastError(QuicError::ForNgtcp2Error(NGTCP2_INTERNAL_ERROR));
    Close(CloseMethod::SILENT);
  };

  if (is_server()) {
    if (auto packet = Packet::CreateConnectionClosePacket(
            env(),
            endpoint(),
            impl_->remote_address_,
            *this,
            impl_->last_error_)) [[likely]] {
      return Send(packet);
    }

    // If we are unable to create a connection close packet then
    // we are in a bad state. An internal error will be set and
    // the session will be silently closed. This is not ideal
    // because the remote peer will not know immediately that
    // the connection has terminated but there's not much else
    // we can do.
    return ErrorAndSilentClose();
  }

  auto packet = Packet::Create(env(),
                               endpoint(),
                               impl_->remote_address_,
                               kDefaultMaxPacketLength,
                               "immediate connection close (client)");
  if (!packet) [[unlikely]] {
    return ErrorAndSilentClose();
  }

  ngtcp2_vec vec = *packet;
  Path path(impl_->local_address_, impl_->remote_address_);
  ssize_t nwrite = ngtcp2_conn_write_connection_close(*this,
                                                      &path,
                                                      nullptr,
                                                      vec.base,
                                                      vec.len,
                                                      impl_->last_error_,
                                                      uv_hrtime());

  if (nwrite < 0) [[unlikely]] {
    packet->Done(UV_ECANCELED);
    return ErrorAndSilentClose();
  }

  packet->Truncate(nwrite);
  return Send(packet);
}

void Session::OnTimeout() {
  CHECK(!is_destroyed());
  HandleScope scope(env()->isolate());
  int ret = ngtcp2_conn_handle_expiry(*this, uv_hrtime());
  if (NGTCP2_OK(ret) && !is_in_closing_period() && !is_in_draining_period()) {
    return application().SendPendingData();
  }

  Debug(this, "Session timed out");
  SetLastError(QuicError::ForNgtcp2Error(ret));
  Close(CloseMethod::SILENT);
}

void Session::UpdateTimer() {
  CHECK(!is_destroyed());
  // Both uv_hrtime and ngtcp2_conn_get_expiry return nanosecond units.
  uint64_t expiry = ngtcp2_conn_get_expiry(*this);
  uint64_t now = uv_hrtime();
  Debug(
      this, "Updating timer. Expiry: %" PRIu64 ", now: %" PRIu64, expiry, now);

  if (expiry <= now) {
    // The timer has already expired.
    return OnTimeout();
  }

  auto timeout = (expiry - now) / NGTCP2_MILLISECONDS;
  Debug(this, "Updating timeout to %zu milliseconds", timeout);

  // If timeout is zero here, it means our timer is less than a millisecond
  // off from expiry. Let's bump the timer to 1.
  impl_->timer_.Update(timeout == 0 ? 1 : timeout);
}

void Session::DatagramStatus(uint64_t datagramId, quic::DatagramStatus status) {
  DCHECK(!is_destroyed());
  auto& stats_ = impl_->stats_;
  switch (status) {
    case quic::DatagramStatus::ACKNOWLEDGED: {
      Debug(this, "Datagram %" PRIu64 " was acknowledged", datagramId);
      STAT_INCREMENT(Stats, datagrams_acknowledged);
      break;
    }
    case quic::DatagramStatus::LOST: {
      Debug(this, "Datagram %" PRIu64 " was lost", datagramId);
      STAT_INCREMENT(Stats, datagrams_lost);
      break;
    }
  }
  EmitDatagramStatus(datagramId, status);
}

void Session::DatagramReceived(const uint8_t* data,
                               size_t datalen,
                               DatagramReceivedFlags flag) {
  DCHECK(!is_destroyed());
  // If there is nothing watching for the datagram on the JavaScript side,
  // or if the datagram is zero-length, we just drop it on the floor.
  if (impl_->state_->datagram == 0 || datalen == 0) return;

  Debug(this, "Session is receiving datagram of size %zu", datalen);
  auto& stats_ = impl_->stats_;
  STAT_INCREMENT(Stats, datagrams_received);
  auto backing = ArrayBuffer::NewBackingStore(
      env()->isolate(),
      datalen,
      BackingStoreInitializationMode::kUninitialized);
  memcpy(backing->Data(), data, datalen);
  EmitDatagram(Store(std::move(backing), datalen), flag);
}

void Session::GenerateNewConnectionId(ngtcp2_cid* cid,
                                      size_t len,
                                      uint8_t* token) {
  DCHECK(!is_destroyed());
  CID cid_ = impl_->config_.options.cid_factory->GenerateInto(cid, len);
  Debug(this, "Generated new connection id %s", cid_);
  StatelessResetToken new_token(
      token, endpoint().options().reset_token_secret, cid_);
  endpoint().AssociateCID(cid_, impl_->config_.scid);
  endpoint().AssociateStatelessResetToken(new_token, this);
}

bool Session::HandshakeCompleted() {
  DCHECK(!is_destroyed());
  DCHECK(!impl_->state_->handshake_completed);

  Debug(this, "Session handshake completed");
  impl_->state_->handshake_completed = 1;
  auto& stats_ = impl_->stats_;
  STAT_RECORD_TIMESTAMP(Stats, handshake_completed_at);
  SetStreamOpenAllowed();

  // TODO(@jasnel): Not yet supporting early data...
  // if (!tls_session().early_data_was_accepted())
  //   ngtcp2_conn_tls_early_data_rejected(*this);

  // When in a server session, handshake completed == handshake confirmed.
  if (is_server()) {
    HandshakeConfirmed();

    auto& ep = endpoint();

    if (!ep.is_closed() && !ep.is_closing()) {
      auto token = ep.GenerateNewToken(version(), impl_->remote_address_);
      ngtcp2_vec vec = token;
      if (NGTCP2_ERR(ngtcp2_conn_submit_new_token(*this, vec.base, vec.len))) {
        // Submitting the new token failed... In this case we're going to
        // fail because submitting the new token should only fail if we
        // ran out of memory or some other unrecoverable state.
        return false;
      }
    }
  }

  EmitHandshakeComplete();

  return true;
}

void Session::HandshakeConfirmed() {
  DCHECK(!is_destroyed());
  DCHECK(!impl_->state_->handshake_confirmed);
  Debug(this, "Session handshake confirmed");
  impl_->state_->handshake_confirmed = 1;
  auto& stats_ = impl_->stats_;
  STAT_RECORD_TIMESTAMP(Stats, handshake_confirmed_at);
}

void Session::SelectPreferredAddress(PreferredAddress* preferredAddress) {
  if (options().preferred_address_strategy ==
      PreferredAddress::Policy::IGNORE_PREFERRED) {
    Debug(this, "Ignoring preferred address");
    return;
  }

  switch (endpoint().local_address().family()) {
    case AF_INET: {
      Debug(this, "Selecting preferred address for AF_INET");
      auto ipv4 = preferredAddress->ipv4();
      if (ipv4.has_value()) {
        if (ipv4->address.empty() || ipv4->port == 0) return;
        CHECK(SocketAddress::New(AF_INET,
                                 std::string(ipv4->address).c_str(),
                                 ipv4->port,
                                 &impl_->remote_address_));
        preferredAddress->Use(ipv4.value());
      }
      break;
    }
    case AF_INET6: {
      Debug(this, "Selecting preferred address for AF_INET6");
      auto ipv6 = preferredAddress->ipv6();
      if (ipv6.has_value()) {
        if (ipv6->address.empty() || ipv6->port == 0) return;
        CHECK(SocketAddress::New(AF_INET,
                                 std::string(ipv6->address).c_str(),
                                 ipv6->port,
                                 &impl_->remote_address_));
        preferredAddress->Use(ipv6.value());
      }
      break;
    }
  }
}

CID Session::new_cid(size_t len) const {
  return options().cid_factory->Generate(len);
}

void Session::ProcessPendingBidiStreams() {
  // It shouldn't be possible to get here if can_create_streams() is false.
  DCHECK(can_create_streams());

  int64_t id;

  while (!impl_->pending_bidi_stream_queue_.IsEmpty()) {
    if (ngtcp2_conn_get_streams_bidi_left(*this) == 0) {
      return;
    }

    switch (ngtcp2_conn_open_bidi_stream(*this, &id, nullptr)) {
      case 0: {
        impl_->pending_bidi_stream_queue_.PopFront()->fulfill(id);
        continue;
      }
      case NGTCP2_ERR_STREAM_ID_BLOCKED: {
        // This case really should not happen since we've checked the number
        // of bidi streams left above. However, if it does happen we'll treat
        // it the same as if the get_streams_bidi_left call returned zero.
        return;
      }
      default: {
        // We failed to open the stream for some reason other than being
        // blocked. Report the failure.
        impl_->pending_bidi_stream_queue_.PopFront()->reject(
            QuicError::ForTransport(NGTCP2_STREAM_LIMIT_ERROR));
        continue;
      }
    }
  }
}

void Session::ProcessPendingUniStreams() {
  // It shouldn't be possible to get here if can_create_streams() is false.
  DCHECK(can_create_streams());

  int64_t id;

  while (!impl_->pending_uni_stream_queue_.IsEmpty()) {
    if (ngtcp2_conn_get_streams_uni_left(*this) == 0) {
      return;
    }

    switch (ngtcp2_conn_open_uni_stream(*this, &id, nullptr)) {
      case 0: {
        impl_->pending_uni_stream_queue_.PopFront()->fulfill(id);
        continue;
      }
      case NGTCP2_ERR_STREAM_ID_BLOCKED: {
        // This case really should not happen since we've checked the number
        // of bidi streams left above. However, if it does happen we'll treat
        // it the same as if the get_streams_bidi_left call returned zero.
        return;
      }
      default: {
        // We failed to open the stream for some reason other than being
        // blocked. Report the failure.
        impl_->pending_uni_stream_queue_.PopFront()->reject(
            QuicError::ForTransport(NGTCP2_STREAM_LIMIT_ERROR));
        continue;
      }
    }
  }
}

// JavaScript callouts

void Session::EmitClose(const QuicError& error) {
  DCHECK(!is_destroyed());
  // When EmitClose is called, the expectation is that the JavaScript
  // side will close the loop and call destroy on the underlying session.
  // If we cannot call out into JavaScript at this point, go ahead and
  // skip to calling destroy directly.
  if (!env()->can_call_into_js()) return Destroy();

  CallbackScope<Session> cb_scope(this);

  Local<Value> argv[] = {
      Integer::New(env()->isolate(), static_cast<int>(error.type())),
      BigInt::NewFromUnsigned(env()->isolate(), error.code()),
      Undefined(env()->isolate()),
  };
  if (error.reason().length() > 0 &&
      !ToV8Value(env()->context(), error.reason()).ToLocal(&argv[2])) {
    return;
  }

  MakeCallback(
      BindingData::Get(env()).session_close_callback(), arraysize(argv), argv);

  // Importantly, the session instance itself should have been destroyed!
  CHECK(is_destroyed());
}

void Session::EmitDatagram(Store&& datagram, DatagramReceivedFlags flag) {
  DCHECK(!is_destroyed());
  if (!env()->can_call_into_js()) return;

  CallbackScope<Session> cbv_scope(this);

  Local<Value> argv[] = {datagram.ToUint8Array(env()),
                         Boolean::New(env()->isolate(), flag.early)};

  MakeCallback(BindingData::Get(env()).session_datagram_callback(),
               arraysize(argv),
               argv);
}

void Session::EmitDatagramStatus(uint64_t id, quic::DatagramStatus status) {
  DCHECK(!is_destroyed());

  if (!env()->can_call_into_js()) return;

  CallbackScope<Session> cb_scope(this);

  auto& state = BindingData::Get(env());

  const auto status_to_string = ([&] {
    switch (status) {
      case quic::DatagramStatus::ACKNOWLEDGED:
        return state.acknowledged_string();
      case quic::DatagramStatus::LOST:
        return state.lost_string();
    }
    UNREACHABLE();
  })();

  Local<Value> argv[] = {BigInt::NewFromUnsigned(env()->isolate(), id),
                         status_to_string};

  MakeCallback(state.session_datagram_status_callback(), arraysize(argv), argv);
}

void Session::EmitHandshakeComplete() {
  DCHECK(!is_destroyed());

  if (!env()->can_call_into_js()) return;

  CallbackScope<Session> cb_scope(this);

  auto isolate = env()->isolate();

  static constexpr auto kServerName = 0;
  static constexpr auto kSelectedAlpn = 1;
  static constexpr auto kCipherName = 2;
  static constexpr auto kCipherVersion = 3;
  static constexpr auto kValidationErrorReason = 4;
  static constexpr auto kValidationErrorCode = 5;

  Local<Value> argv[] = {
      Undefined(isolate),  // The negotiated server name
      Undefined(isolate),  // The selected protocol
      Undefined(isolate),  // Cipher name
      Undefined(isolate),  // Cipher version
      Undefined(isolate),  // Validation error reason
      Undefined(isolate),  // Validation error code
      Boolean::New(isolate, tls_session().early_data_was_accepted())};

  auto& tls = tls_session();
  auto peerVerifyError = tls.VerifyPeerIdentity(env());
  if (peerVerifyError.has_value() &&
      (!peerVerifyError->reason.ToLocal(&argv[kValidationErrorReason]) ||
       !peerVerifyError->code.ToLocal(&argv[kValidationErrorCode]))) {
    return;
  }

  if (!ToV8Value(env()->context(), tls.servername())
           .ToLocal(&argv[kServerName]) ||
      !ToV8Value(env()->context(), tls.protocol())
           .ToLocal(&argv[kSelectedAlpn]) ||
      !tls.cipher_name(env()).ToLocal(&argv[kCipherName]) ||
      !tls.cipher_version(env()).ToLocal(&argv[kCipherVersion])) {
    return;
  }

  MakeCallback(BindingData::Get(env()).session_handshake_callback(),
               arraysize(argv),
               argv);
}

void Session::EmitPathValidation(PathValidationResult result,
                                 PathValidationFlags flags,
                                 const ValidatedPath& newPath,
                                 const std::optional<ValidatedPath>& oldPath) {
  DCHECK(!is_destroyed());

  if (!env()->can_call_into_js()) return;

  if (impl_->state_->path_validation == 0) [[likely]] {
    return;
  }

  auto isolate = env()->isolate();
  CallbackScope<Session> cb_scope(this);
  auto& state = BindingData::Get(env());

  const auto resultToString = ([&] {
    switch (result) {
      case PathValidationResult::ABORTED:
        return state.aborted_string();
      case PathValidationResult::FAILURE:
        return state.failure_string();
      case PathValidationResult::SUCCESS:
        return state.success_string();
    }
    UNREACHABLE();
  })();

  Local<Value> argv[] = {
      resultToString,
      SocketAddressBase::Create(env(), newPath.local)->object(),
      SocketAddressBase::Create(env(), newPath.remote)->object(),
      Undefined(isolate),
      Undefined(isolate),
      Boolean::New(isolate, flags.preferredAddress)};

  if (oldPath.has_value()) {
    argv[3] = SocketAddressBase::Create(env(), oldPath->local)->object();
    argv[4] = SocketAddressBase::Create(env(), oldPath->remote)->object();
  }

  Debug(this, "Notifying JavaScript of path validation");
  MakeCallback(state.session_path_validation_callback(), arraysize(argv), argv);
}

void Session::EmitSessionTicket(Store&& ticket) {
  DCHECK(!is_destroyed());

  if (!env()->can_call_into_js()) return;

  // If there is nothing listening for the session ticket, don't bother
  // emitting.
  if (impl_->state_->session_ticket == 0) [[likely]] {
    Debug(this, "Session ticket was discarded");
    return;
  }

  CallbackScope<Session> cb_scope(this);

  auto& remote_params = remote_transport_params();
  Store transport_params;
  if (remote_params) {
    if (auto transport_params = remote_params.Encode(env())) {
      SessionTicket session_ticket(std::move(ticket),
                                   std::move(transport_params));
      Local<Value> argv;
      if (session_ticket.encode(env()).ToLocal(&argv)) [[likely]] {
        MakeCallback(
            BindingData::Get(env()).session_ticket_callback(), 1, &argv);
      }
    }
  }
}

void Session::EmitStream(const BaseObjectWeakPtr<Stream>& stream) {
  DCHECK(!is_destroyed());

  if (!env()->can_call_into_js()) return;
  CallbackScope<Session> cb_scope(this);

  auto isolate = env()->isolate();
  Local<Value> argv[] = {
      stream->object(),
      Integer::NewFromUnsigned(isolate,
                               static_cast<uint32_t>(stream->direction())),
  };

  MakeCallback(
      BindingData::Get(env()).stream_created_callback(), arraysize(argv), argv);
}

void Session::EmitVersionNegotiation(const ngtcp2_pkt_hd& hd,
                                     const uint32_t* sv,
                                     size_t nsv) {
  DCHECK(!is_destroyed());
  DCHECK(!is_server());

  if (!env()->can_call_into_js()) return;

  CallbackScope<Session> cb_scope(this);
  auto& opts = options();

  // version() is the version that was actually configured for this session.
  // versions are the versions requested by the peer.
  // supported are the versions supported by Node.js.

  LocalVector<Value> versions(env()->isolate(), nsv);
  for (size_t n = 0; n < nsv; n++) {
    versions[n] = Integer::NewFromUnsigned(env()->isolate(), sv[n]);
  }

  // supported are the versions we acutually support expressed as a range.
  // The first value is the minimum version, the second is the maximum.
  Local<Value> supported[] = {
      Integer::NewFromUnsigned(env()->isolate(), opts.min_version),
      Integer::NewFromUnsigned(env()->isolate(), opts.version)};

  Local<Value> argv[] = {
      // The version configured for this session.
      Integer::NewFromUnsigned(env()->isolate(), version()),
      // The versions requested.
      Array::New(env()->isolate(), versions.data(), nsv),
      // The versions we actually support.
      Array::New(env()->isolate(), supported, arraysize(supported))};

  MakeCallback(BindingData::Get(env()).session_version_negotiation_callback(),
               arraysize(argv),
               argv);
}

void Session::EmitKeylog(const char* line) {
  if (!env()->can_call_into_js()) return;
  if (keylog_stream_) {
    Debug(this, "Emitting keylog line");
    env()->SetImmediate([ptr = keylog_stream_, data = std::string(line) + "\n"](
                            Environment* env) { ptr->Emit(data); });
  }
}

// ============================================================================

Local<FunctionTemplate> Session::GetConstructorTemplate(Environment* env) {
  auto& state = BindingData::Get(env);
  auto tmpl = state.session_constructor_template();
  if (tmpl.IsEmpty()) {
    auto isolate = env->isolate();
    tmpl = NewFunctionTemplate(isolate, IllegalConstructor);
    tmpl->SetClassName(state.session_string());
    tmpl->Inherit(AsyncWrap::GetConstructorTemplate(env));
    tmpl->InstanceTemplate()->SetInternalFieldCount(
        Session::kInternalFieldCount);
#define V(name, key, no_side_effect)                                           \
  if (no_side_effect) {                                                        \
    SetProtoMethodNoSideEffect(isolate, tmpl, #key, Impl::name);               \
  } else {                                                                     \
    SetProtoMethod(isolate, tmpl, #key, Impl::name);                           \
  }
    SESSION_JS_METHODS(V)

#undef V
    state.set_session_constructor_template(tmpl);
  }
  return tmpl;
}

void Session::RegisterExternalReferences(ExternalReferenceRegistry* registry) {
#define V(name, _, __) registry->Register(Impl::name);
  SESSION_JS_METHODS(V)
#undef V
}

void Session::InitPerIsolate(IsolateData* data, Local<ObjectTemplate> target) {
  // TODO(@jasnell): Implement the per-isolate state
}

void Session::InitPerContext(Realm* realm, Local<Object> target) {
#define V(name, str)                                                           \
  NODE_DEFINE_CONSTANT(target, CC_ALGO_##name);                                \
  NODE_DEFINE_STRING_CONSTANT(target, "CC_ALGO_" #name "_STR", #str);
  CC_ALGOS(V)
#undef V
  // Make sure the Session constructor template is initialized.
  USE(GetConstructorTemplate(realm->env()));

  TransportParams::Initialize(realm->env(), target);
  PreferredAddress::Initialize(realm->env(), target);

  static constexpr auto STREAM_DIRECTION_BIDIRECTIONAL =
      static_cast<uint32_t>(Direction::BIDIRECTIONAL);
  static constexpr auto STREAM_DIRECTION_UNIDIRECTIONAL =
      static_cast<uint32_t>(Direction::UNIDIRECTIONAL);
  static constexpr auto QUIC_PROTO_MAX = NGTCP2_PROTO_VER_MAX;
  static constexpr auto QUIC_PROTO_MIN = NGTCP2_PROTO_VER_MIN;

  NODE_DEFINE_CONSTANT(target, STREAM_DIRECTION_BIDIRECTIONAL);
  NODE_DEFINE_CONSTANT(target, STREAM_DIRECTION_UNIDIRECTIONAL);
  NODE_DEFINE_CONSTANT(target, DEFAULT_MAX_HEADER_LIST_PAIRS);
  NODE_DEFINE_CONSTANT(target, DEFAULT_MAX_HEADER_LENGTH);
  NODE_DEFINE_CONSTANT(target, QUIC_PROTO_MAX);
  NODE_DEFINE_CONSTANT(target, QUIC_PROTO_MIN);

  NODE_DEFINE_STRING_CONSTANT(
      target, "DEFAULT_CIPHERS", TLSContext::DEFAULT_CIPHERS);
  NODE_DEFINE_STRING_CONSTANT(
      target, "DEFAULT_GROUPS", TLSContext::DEFAULT_GROUPS);

#define V(name, _) IDX_STATS_SESSION_##name,
  enum SessionStatsIdx { SESSION_STATS(V) IDX_STATS_SESSION_COUNT };
#undef V

#define V(name, key, __)                                                       \
  auto IDX_STATE_SESSION_##name = offsetof(Session::State, key);
  SESSION_STATE(V)
#undef V

#define V(name, _) NODE_DEFINE_CONSTANT(target, IDX_STATS_SESSION_##name);
  SESSION_STATS(V)
  NODE_DEFINE_CONSTANT(target, IDX_STATS_SESSION_COUNT);
#undef V
#define V(name, _, __) NODE_DEFINE_CONSTANT(target, IDX_STATE_SESSION_##name);
  SESSION_STATE(V)
#undef V
}

}  // namespace quic
}  // namespace node

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
