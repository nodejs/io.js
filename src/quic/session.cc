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
using v8::BigInt;
using v8::Boolean;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Integer;
using v8::Just;
using v8::Local;
using v8::Maybe;
using v8::Nothing;
using v8::Object;
using v8::PropertyAttribute;
using v8::String;
using v8::Uint32;
using v8::Undefined;
using v8::Value;

namespace quic {

#define SESSION_STATE(V)                                                       \
  /* Set if the JavaScript wrapper has a path-validation event listener */     \
  V(PATH_VALIDATION, path_validation, uint8_t)                                 \
  /* Set if the JavaScript wrapper has a version-negotiation event listener */ \
  V(VERSION_NEGOTIATION, version_negotiation, uint8_t)                         \
  /* Set if the JavaScript wrapper has a datagram event listener */            \
  V(DATAGRAM, datagram, uint8_t)                                               \
  /* Set if the JavaScript wrapper has a session-ticket event listener */      \
  V(SESSION_TICKET, session_ticket, uint8_t)                                   \
  V(CLOSING, closing, uint8_t)                                                 \
  V(GRACEFUL_CLOSE, graceful_close, uint8_t)                                   \
  V(SILENT_CLOSE, silent_close, uint8_t)                                       \
  V(STATELESS_RESET, stateless_reset, uint8_t)                                 \
  V(DESTROYED, destroyed, uint8_t)                                             \
  V(HANDSHAKE_COMPLETED, handshake_completed, uint8_t)                         \
  V(HANDSHAKE_CONFIRMED, handshake_confirmed, uint8_t)                         \
  V(STREAM_OPEN_ALLOWED, stream_open_allowed, uint8_t)                         \
  V(PRIORITY_SUPPORTED, priority_supported, uint8_t)                           \
  /* A Session is wrapped if it has been passed out to JS */                   \
  V(WRAPPED, wrapped, uint8_t)                                                 \
  V(LAST_DATAGRAM_ID, last_datagram_id, uint64_t)

#define SESSION_STATS(V)                                                       \
  V(CREATED_AT, created_at)                                                    \
  V(CLOSING_AT, closing_at)                                                    \
  V(DESTROYED_AT, destroyed_at)                                                \
  V(HANDSHAKE_COMPLETED_AT, handshake_completed_at)                            \
  V(HANDSHAKE_CONFIRMED_AT, handshake_confirmed_at)                            \
  V(GRACEFUL_CLOSING_AT, graceful_closing_at)                                  \
  V(BYTES_RECEIVED, bytes_received)                                            \
  V(BYTES_SENT, bytes_sent)                                                    \
  V(BIDI_IN_STREAM_COUNT, bidi_in_stream_count)                                \
  V(BIDI_OUT_STREAM_COUNT, bidi_out_stream_count)                              \
  V(UNI_IN_STREAM_COUNT, uni_in_stream_count)                                  \
  V(UNI_OUT_STREAM_COUNT, uni_out_stream_count)                                \
  V(LOSS_RETRANSMIT_COUNT, loss_retransmit_count)                              \
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
  V(DoDestroy, destroy, false)                                                 \
  V(GetRemoteAddress, getRemoteAddress, true)                                  \
  V(GetCertificate, getCertificate, true)                                      \
  V(GetEphemeralKeyInfo, getEphemeralKey, true)                                \
  V(GetPeerCertificate, getPeerCertificate, true)                              \
  V(GracefulClose, gracefulClose, false)                                       \
  V(SilentClose, silentClose, false)                                           \
  V(UpdateKey, updateKey, false)                                               \
  V(DoOpenStream, openStream, false)                                           \
  V(DoSendDatagram, sendDatagram, false)

struct Session::State {
#define V(_, name, type) type name;
  SESSION_STATE(V)
#undef V
};

STAT_STRUCT(Session, SESSION)

// ============================================================================
// Used to conditionally trigger sending an explicit connection
// close. If there are multiple MaybeCloseConnectionScope in the
// stack, the determination of whether to send the close will be
// done once the final scope is closed.
struct Session::MaybeCloseConnectionScope final {
  Session* session;
  bool silent = false;
  MaybeCloseConnectionScope(Session* session_, bool silent_)
      : session(session_),
        silent(silent_ || session->connection_close_depth_ > 0) {
    Debug(session_,
          "Entering maybe close connection scope. Silent? %s",
          silent ? "yes" : "no");
    session->connection_close_depth_++;
  }
  DISALLOW_COPY_AND_MOVE(MaybeCloseConnectionScope)
  ~MaybeCloseConnectionScope() {
    // We only want to trigger the sending the connection close if ...
    // a) Silent is not explicitly true at this scope.
    // b) We're not within the scope of an ngtcp2 callback, and
    // c) We are not already in a closing or draining period.
    if (--session->connection_close_depth_ == 0 && !silent &&
        session->can_send_packets()) {
      session->SendConnectionClose();
    }
  }
};

// ============================================================================
// Used to conditionally trigger sending of any pending data the session may
// be holding onto. If there are multiple SendPendingDataScope in the stack,
// the determination of whether to send the data will be done once the final
// scope is closed.

Session::SendPendingDataScope::SendPendingDataScope(Session* session)
    : session(session) {
  Debug(session, "Entering send pending data scope");
  session->send_scope_depth_++;
}

Session::SendPendingDataScope::SendPendingDataScope(
    const BaseObjectPtr<Session>& session)
    : SendPendingDataScope(session.get()) {}

Session::SendPendingDataScope::~SendPendingDataScope() {
  if (--session->send_scope_depth_ == 0 && session->can_send_packets()) {
    session->application().SendPendingData();
  }
}

// ============================================================================

namespace {

inline std::string to_string(ngtcp2_encryption_level level) {
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
               const v8::Local<Object>& object,
               const v8::Local<String>& name) {
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
               const v8::Local<Object>& object,
               const v8::Local<String>& name) {
  Local<Value> value;
  TLSContext::Options opts;
  if (!object->Get(env->context(), name).ToLocal(&value) ||
      !TLSContext::Options::From(env, value).To(&opts)) {
    return false;
  }
  options->*member = opts;
  return true;
}

template <typename Opt, Session::Application_Options Opt::*member>
bool SetOption(Environment* env,
               Opt* options,
               const v8::Local<Object>& object,
               const v8::Local<String>& name) {
  Local<Value> value;
  Session::Application_Options opts;
  if (!object->Get(env->context(), name).ToLocal(&value) ||
      !Session::Application_Options::From(env, value).To(&opts)) {
    return false;
  }
  options->*member = opts;
  return true;
}

template <typename Opt, TransportParams::Options Opt::*member>
bool SetOption(Environment* env,
               Opt* options,
               const v8::Local<Object>& object,
               const v8::Local<String>& name) {
  Local<Value> value;
  TransportParams::Options opts;
  if (!object->Get(env->context(), name).ToLocal(&value) ||
      !TransportParams::Options::From(env, value).To(&opts)) {
    return false;
  }
  options->*member = opts;
  return true;
}

}  // namespace

// ============================================================================
Session::Config::Config(Side side,
                        const Endpoint& endpoint,
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

  settings.tokenlen = 0;
  settings.token = nullptr;

  if (options.qlog) {
    settings.qlog_write = on_qlog_write;
  }

  if (endpoint.env()->enabled_debug_list()->enabled(
          DebugCategory::NGTCP2_DEBUG)) {
    settings.log_printf = ngtcp2_debug_log;
  }

  // We pull parts of the settings for the session from the endpoint options.
  auto& config = endpoint.options();
  settings.no_tx_udp_payload_size_shaping = config.no_udp_payload_size_shaping;
  settings.handshake_timeout = config.handshake_timeout;
  settings.max_stream_window = config.max_stream_window;
  settings.max_window = config.max_window;
  settings.cc_algo = config.cc_algorithm;
  settings.max_tx_udp_payload_size = config.max_payload_size;
  if (config.unacknowledged_packet_threshold > 0) {
    settings.ack_thresh = config.unacknowledged_packet_threshold;
  }
}

Session::Config::Config(const Endpoint& endpoint,
                        const Options& options,
                        const SocketAddress& local_address,
                        const SocketAddress& remote_address,
                        const CID& ocid)
    : Config(Side::CLIENT,
             endpoint,
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

  auto sidestr = ([&] {
    switch (side) {
      case Side::CLIENT:
        return "client";
      case Side::SERVER:
        return "server";
    }
    return "<unknown>";
  })();
  res += prefix + "side: " + std::string(sidestr);
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
      !SET(transport_params) || !SET(tls_options) ||
      !SET(application_options) || !SET(qlog)) {
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
  tracker->TrackField("application_options", application_options);
  tracker->TrackField("cid_factory_ref", cid_factory_ref);
}

std::string Session::Options::ToString() const {
  DebugIndentScope indent;
  auto prefix = indent.Prefix();
  std::string res("{");
  res += prefix + "version: " + std::to_string(version);
  res += prefix + "min version: " + std::to_string(min_version);

  auto policy = ([&] {
    switch (preferred_address_strategy) {
      case PreferredAddress::Policy::USE_PREFERRED:
        return "use";
      case PreferredAddress::Policy::IGNORE_PREFERRED:
        return "ignore";
    }
    return "<unknown>";
  })();
  res += prefix + "preferred address policy: " + std::string(policy);
  res += prefix + "transport params: " + transport_params.ToString();
  res += prefix + "crypto options: " + tls_options.ToString();
  res += prefix + "application options: " + application_options.ToString();
  res += prefix + "qlog: " + (qlog ? std::string("yes") : std::string("no"));
  res += indent.Close();
  return res;
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
    return BaseObjectPtr<Session>();
  }

  return MakeDetachedBaseObject<Session>(
      endpoint, obj, config, tls_context, ticket);
}

Session::Session(Endpoint* endpoint,
                 v8::Local<v8::Object> object,
                 const Config& config,
                 TLSContext* tls_context,
                 const std::optional<SessionTicket>& session_ticket)
    : AsyncWrap(endpoint->env(), object, AsyncWrap::PROVIDER_QUIC_SESSION),
      stats_(env()->isolate()),
      state_(env()->isolate()),
      allocator_(BindingData::Get(env())),
      endpoint_(BaseObjectWeakPtr<Endpoint>(endpoint)),
      config_(config),
      local_address_(config.local_address),
      remote_address_(config.remote_address),
      connection_(InitConnection()),
      tls_session_(tls_context->NewSession(this, session_ticket)),
      application_(select_application()),
      timer_(env(),
             [this, self = BaseObjectPtr<Session>(this)] { OnTimeout(); }) {
  MakeWeak();

  Debug(this, "Session created.");

  timer_.Unref();

  application().ExtendMaxStreams(EndpointLabel::LOCAL,
                                 Direction::BIDIRECTIONAL,
                                 TransportParams::DEFAULT_MAX_STREAMS_BIDI);
  application().ExtendMaxStreams(EndpointLabel::LOCAL,
                                 Direction::UNIDIRECTIONAL,
                                 TransportParams::DEFAULT_MAX_STREAMS_UNI);

  const auto defineProperty = [&](auto name, auto value) {
    object
        ->DefineOwnProperty(
            env()->context(), name, value, PropertyAttribute::ReadOnly)
        .Check();
  };

  defineProperty(env()->state_string(), state_.GetArrayBuffer());
  defineProperty(env()->stats_string(), stats_.GetArrayBuffer());

  auto& state = BindingData::Get(env());

  if (UNLIKELY(config_.options.qlog)) {
    qlog_stream_ = LogStream::Create(env());
    if (qlog_stream_)
      defineProperty(state.qlog_string(), qlog_stream_->object());
  }

  if (UNLIKELY(config_.options.tls_options.keylog)) {
    keylog_stream_ = LogStream::Create(env());
    if (keylog_stream_)
      defineProperty(state.keylog_string(), keylog_stream_->object());
  }

  // We index the Session by our local CID (the scid) and dcid (the peer's cid)
  endpoint_->AddSession(config_.scid, BaseObjectPtr<Session>(this));
  endpoint_->AssociateCID(config_.dcid, config_.scid);

  UpdateDataStats();
}

Session::~Session() {
  Debug(this, "Session destroyed.");
  if (conn_closebuf_) {
    conn_closebuf_->Done(0);
  }
  if (qlog_stream_) {
    Debug(this, "Closing the qlog stream for this session");
    env()->SetImmediate(
        [ptr = std::move(qlog_stream_)](Environment*) { ptr->End(); });
  }
  if (keylog_stream_) {
    Debug(this, "Closing the keylog stream for this session");
    env()->SetImmediate(
        [ptr = std::move(keylog_stream_)](Environment*) { ptr->End(); });
  }
  DCHECK(streams_.empty());
}

size_t Session::max_packet_size() const {
  return ngtcp2_conn_get_max_tx_udp_payload_size(*this);
}

Session::operator ngtcp2_conn*() const {
  return connection_.get();
}

uint32_t Session::version() const {
  return config_.version;
}

Endpoint& Session::endpoint() const {
  return *endpoint_;
}

TLSSession& Session::tls_session() {
  return *tls_session_;
}

Session::Application& Session::application() {
  return *application_;
}

const SocketAddress& Session::remote_address() const {
  return remote_address_;
}

const SocketAddress& Session::local_address() const {
  return local_address_;
}

bool Session::is_closing() const {
  return state_->closing;
}

bool Session::is_graceful_closing() const {
  return state_->graceful_close;
}

bool Session::is_silent_closing() const {
  return state_->silent_close;
}

bool Session::is_destroyed() const {
  return state_->destroyed;
}

bool Session::is_server() const {
  return config_.side == Side::SERVER;
}

std::string Session::diagnostic_name() const {
  const auto get_type = [&] { return is_server() ? "server" : "client"; };

  return std::string("Session (") + get_type() + "," +
         std::to_string(env()->thread_id()) + ":" +
         std::to_string(static_cast<int64_t>(get_async_id())) + ")";
}

const Session::Config& Session::config() const {
  return config_;
}

const Session::Options& Session::options() const {
  return config_.options;
}

void Session::HandleQlog(uint32_t flags, const void* data, size_t len) {
  if (qlog_stream_) {
    // Fun fact... ngtcp2 does not emit the final qlog statement until the
    // ngtcp2_conn object is destroyed. Ideally, destroying is explicit, but
    // sometimes the Session object can be garbage collected without being
    // explicitly destroyed. During those times, we cannot call out to
    // JavaScript. Because we don't know for sure if we're in in a GC when this
    // is called, it is safer to just defer writes to immediate, and to keep it
    // consistent, let's just always defer (this is not performance sensitive so
    // the deferring is fine).
    std::vector<uint8_t> buffer(len);
    memcpy(buffer.data(), data, len);
    Debug(this, "Emitting qlog data to the qlog stream");
    env()->SetImmediate(
        [ptr = qlog_stream_, buffer = std::move(buffer), flags](Environment*) {
          ptr->Emit(buffer.data(),
                    buffer.size(),
                    flags & NGTCP2_QLOG_WRITE_FLAG_FIN
                        ? LogStream::EmitOption::FIN
                        : LogStream::EmitOption::NONE);
        });
  }
}

TransportParams Session::GetLocalTransportParams() const {
  DCHECK(!is_destroyed());
  return TransportParams(ngtcp2_conn_get_local_transport_params(*this));
}

TransportParams Session::GetRemoteTransportParams() const {
  DCHECK(!is_destroyed());
  return TransportParams(ngtcp2_conn_get_remote_transport_params(*this));
}

void Session::SetLastError(QuicError&& error) {
  Debug(this, "Setting last error to %s", error);
  last_error_ = std::move(error);
}

void Session::Close(Session::CloseMethod method) {
  if (is_destroyed()) return;
  switch (method) {
    case CloseMethod::DEFAULT: {
      Debug(this, "Closing session");
      DoClose(false);
      break;
    }
    case CloseMethod::SILENT: {
      Debug(this, "Closing session silently");
      DoClose(true);
      break;
    }
    case CloseMethod::GRACEFUL: {
      if (is_graceful_closing()) return;
      Debug(this, "Closing session gracefully");
      // If there are no open streams, then we can close just immediately and
      // not worry about waiting around for the right moment.
      if (streams_.empty()) {
        DoClose(false);
      } else {
        state_->graceful_close = 1;
        STAT_RECORD_TIMESTAMP(Stats, graceful_closing_at);
      }
      break;
    }
  }
}

void Session::Destroy() {
  if (is_destroyed()) return;
  Debug(this, "Session destroyed");

  // The DoClose() method should have already been called.
  DCHECK(state_->closing);

  // We create a copy of the streams because they will remove themselves
  // from streams_ as they are cleaning up, causing the iterator to be
  // invalidated.
  auto streams = streams_;
  for (auto& stream : streams) stream.second->Destroy(last_error_);
  DCHECK(streams_.empty());

  STAT_RECORD_TIMESTAMP(Stats, destroyed_at);
  state_->closing = 0;
  state_->graceful_close = 0;

  timer_.Stop();

  // The Session instances are kept alive using a in the Endpoint. Removing the
  // Session from the Endpoint will free that pointer, allowing the Session to
  // be deconstructed once the stack unwinds and any remaining
  // BaseObjectPtr<Session> instances fall out of scope.

  MaybeStackBuffer<ngtcp2_cid, 10> cids(ngtcp2_conn_get_scid(*this, nullptr));
  ngtcp2_conn_get_scid(*this, cids.out());

  MaybeStackBuffer<ngtcp2_cid_token, 10> tokens(
      ngtcp2_conn_get_active_dcid(*this, nullptr));
  ngtcp2_conn_get_active_dcid(*this, tokens.out());

  endpoint_->DisassociateCID(config_.dcid);
  endpoint_->DisassociateCID(config_.preferred_address_cid);

  for (size_t n = 0; n < cids.length(); n++) {
    endpoint_->DisassociateCID(CID(cids[n]));
  }

  for (size_t n = 0; n < tokens.length(); n++) {
    if (tokens[n].token_present) {
      endpoint_->DisassociateStatelessResetToken(
          StatelessResetToken(tokens[n].token));
    }
  }

  state_->destroyed = 1;

  // Removing the session from the endpoint may cause the endpoint to be
  // destroyed if it is waiting on the last session to be destroyed. Let's grab
  // a reference just to be safe for the rest of the function.
  BaseObjectPtr<Endpoint> endpoint = std::move(endpoint_);
  endpoint->RemoveSession(config_.scid);
}

bool Session::Receive(Store&& store,
                      const SocketAddress& local_address,
                      const SocketAddress& remote_address) {
  if (is_destroyed()) return false;

  const auto receivePacket = [&](ngtcp2_path* path, ngtcp2_vec vec) {
    DCHECK(!is_destroyed());

    uint64_t now = uv_hrtime();
    ngtcp2_pkt_info pi{};  // Not used but required.
    int err = ngtcp2_conn_read_pkt(*this, path, &pi, vec.base, vec.len, now);
    switch (err) {
      case 0: {
        // Return true so we send after receiving.
        Debug(this, "Session successfully received packet");
        return true;
      }
      case NGTCP2_ERR_DRAINING: {
        // Connection has entered the draining state, no further data should be
        // sent. This happens when the remote peer has sent a CONNECTION_CLOSE.
        Debug(this, "Session is draining");
        return false;
      }
      case NGTCP2_ERR_CLOSING: {
        // Connection has entered the closing state, no further data should be
        // sent. This happens when the local peer has called
        // ngtcp2_conn_write_connection_close.
        Debug(this, "Session is closing");
        return false;
      }
      case NGTCP2_ERR_CRYPTO: {
        // Crypto error happened! Set the last error to the tls alert
        last_error_ = QuicError::ForTlsAlert(ngtcp2_conn_get_tls_alert(*this));
        Debug(this, "Crypto error while receiving packet: %s", last_error_);
        Close();
        return false;
      }
      case NGTCP2_ERR_RETRY: {
        // This should only ever happen on the server. We have to send a path
        // validation challenge in the form of a RETRY packet to the peer and
        // drop the connection.
        DCHECK(is_server());
        Debug(this, "Server must send a retry packet");
        endpoint_->SendRetry(PathDescriptor{
            version(),
            config_.dcid,
            config_.scid,
            local_address_,
            remote_address_,
        });
        Close(CloseMethod::SILENT);
        return false;
      }
      case NGTCP2_ERR_DROP_CONN: {
        // There's nothing else to do but drop the connection state.
        Debug(this, "Session must drop the connection");
        Close(CloseMethod::SILENT);
        return false;
      }
    }
    // Shouldn't happen but just in case.
    last_error_ = QuicError::ForNgtcp2Error(err);
    Debug(this, "Error while receiving packet: %s (%d)", last_error_, err);
    Close();
    return false;
  };

  auto update_stats = OnScopeLeave([&] { UpdateDataStats(); });
  remote_address_ = remote_address;
  Path path(local_address, remote_address_);
  Debug(this, "Session is receiving packet received along path %s", path);
  STAT_INCREMENT_N(Stats, bytes_received, store.length());
  if (receivePacket(&path, store)) application().SendPendingData();

  if (!is_destroyed()) UpdateTimer();

  return true;
}

void Session::Send(Packet* packet) {
  // Sending a Packet is generally best effort. If we're not in a state
  // where we can send a packet, it's ok to drop it on the floor. The
  // packet loss mechanisms will cause the packet data to be resent later
  // if appropriate (and possible).
  DCHECK(!is_destroyed());
  DCHECK(!is_in_draining_period());

  if (can_send_packets() && packet->length() > 0) {
    Debug(this, "Session is sending %s", packet->ToString());
    STAT_INCREMENT_N(Stats, bytes_sent, packet->length());
    endpoint_->Send(packet);
    return;
  }

  Debug(this, "Session could not send %s", packet->ToString());
  packet->Done(packet->length() > 0 ? UV_ECANCELED : 0);
}

void Session::Send(Packet* packet, const PathStorage& path) {
  UpdatePath(path);
  Send(packet);
}

void Session::UpdatePacketTxTime() {
  ngtcp2_conn_update_pkt_tx_time(*this, uv_hrtime());
}

uint64_t Session::SendDatagram(Store&& data) {
  auto tp = ngtcp2_conn_get_remote_transport_params(*this);
  uint64_t max_datagram_size = tp->max_datagram_frame_size;
  if (max_datagram_size == 0 || data.length() > max_datagram_size) {
    // Datagram is too large.
    Debug(this, "Data is too large to send as a datagram");
    return 0;
  }

  Debug(this, "Session is sending datagram");
  Packet* packet = nullptr;
  uint8_t* pos = nullptr;
  int accepted = 0;
  ngtcp2_vec vec = data;
  PathStorage path;
  int flags = NGTCP2_WRITE_DATAGRAM_FLAG_MORE;
  uint64_t did = state_->last_datagram_id + 1;

  // Let's give it a max number of attempts to send the datagram
  static const int kMaxAttempts = 16;
  int attempts = 0;

  for (;;) {
    // We may have to make several attempts at encoding and sending the
    // datagram packet. On each iteration here we'll try to encode the
    // datagram. It's entirely up to ngtcp2 whether to include the datagram
    // in the packet on each call to ngtcp2_conn_writev_datagram.
    if (packet == nullptr) {
      packet = Packet::Create(env(),
                              endpoint_.get(),
                              remote_address_,
                              ngtcp2_conn_get_max_tx_udp_payload_size(*this),
                              "datagram");
      // Typically sending datagrams is best effort, but if we cannot create
      // the packet, then we handle it as a fatal error.
      if (packet == nullptr) {
        last_error_ = QuicError::ForNgtcp2Error(NGTCP2_ERR_INTERNAL);
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
    ngtcp2_conn_update_pkt_tx_time(*this, uv_hrtime());

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
          // as a fatal condition.
          break;
        }
        case NGTCP2_ERR_CALLBACK_FAILURE: {
          // There was an internal failure. Sadly we have to treat it as a fatal
          // condition.
          break;
        }
      }
      packet->Done(UV_ECANCELED);
      last_error_ = QuicError::ForNgtcp2Error(nwrite);
      Close(CloseMethod::SILENT);
      return 0;
    }

    // In this case, a complete packet was written and we need to send it along.
    // Note that this doesn't mean that the packet actually contains the
    // datagram! We'll check that next by checking the accepted value.
    packet->Truncate(nwrite);
    Send(std::move(packet));

    if (accepted != 0) {
      // Yay! The datagram was accepted into the packet we just sent and we can
      // return the datagram ID.
      Debug(this, "Session successfully encoded datagram");
      STAT_INCREMENT(Stats, datagrams_sent);
      STAT_INCREMENT_N(Stats, bytes_sent, vec.len);
      state_->last_datagram_id = did;
      return did;
    }

    // We sent a packet, but it wasn't the datagram packet. That can happen.
    // Let's loop around and try again.
    if (++attempts == kMaxAttempts) {
      Debug(this, "Too many attempts to send the datagram");
      // Too many attempts to send the datagram.
      break;
    }
  }

  return 0;
}

void Session::UpdatePath(const PathStorage& storage) {
  remote_address_.Update(storage.path.remote.addr, storage.path.remote.addrlen);
  local_address_.Update(storage.path.local.addr, storage.path.local.addrlen);
  Debug(this,
        "path updated. local %s, remote %s",
        local_address_,
        remote_address_);
}

BaseObjectPtr<Stream> Session::FindStream(int64_t id) const {
  auto it = streams_.find(id);
  return it == std::end(streams_) ? BaseObjectPtr<Stream>() : it->second;
}

BaseObjectPtr<Stream> Session::CreateStream(int64_t id) {
  if (!can_create_streams()) return BaseObjectPtr<Stream>();
  auto stream = Stream::Create(this, id);
  if (stream) AddStream(stream);
  return stream;
}

BaseObjectPtr<Stream> Session::OpenStream(Direction direction) {
  if (!can_create_streams()) return BaseObjectPtr<Stream>();
  int64_t id;
  switch (direction) {
    case Direction::BIDIRECTIONAL: {
      Debug(this, "Opening bidirectional stream");
      if (ngtcp2_conn_open_bidi_stream(*this, &id, nullptr) == 0)
        return CreateStream(id);
      break;
    }
    case Direction::UNIDIRECTIONAL: {
      Debug(this, "Opening uni-directional stream");
      if (ngtcp2_conn_open_uni_stream(*this, &id, nullptr) == 0)
        return CreateStream(id);
      break;
    }
  }
  return BaseObjectPtr<Stream>();
}

void Session::AddStream(const BaseObjectPtr<Stream>& stream) {
  Debug(this, "Adding stream %" PRIi64 " to session", stream->id());
  ngtcp2_conn_set_stream_user_data(*this, stream->id(), stream.get());
  streams_[stream->id()] = stream;

  // Update tracking statistics for the number of streams associated with this
  // session.
  switch (stream->origin()) {
    case Side::CLIENT: {
      if (is_server()) {
        switch (stream->direction()) {
          case Direction::BIDIRECTIONAL:
            STAT_INCREMENT(Stats, bidi_in_stream_count);
            break;
          case Direction::UNIDIRECTIONAL:
            STAT_INCREMENT(Stats, uni_in_stream_count);
            break;
        }
      } else {
        switch (stream->direction()) {
          case Direction::BIDIRECTIONAL:
            STAT_INCREMENT(Stats, bidi_out_stream_count);
            break;
          case Direction::UNIDIRECTIONAL:
            STAT_INCREMENT(Stats, uni_out_stream_count);
            break;
        }
      }
      break;
    }
    case Side::SERVER: {
      if (is_server()) {
        switch (stream->direction()) {
          case Direction::BIDIRECTIONAL:
            STAT_INCREMENT(Stats, bidi_out_stream_count);
            break;
          case Direction::UNIDIRECTIONAL:
            STAT_INCREMENT(Stats, uni_out_stream_count);
            break;
        }
      } else {
        switch (stream->direction()) {
          case Direction::BIDIRECTIONAL:
            STAT_INCREMENT(Stats, bidi_in_stream_count);
            break;
          case Direction::UNIDIRECTIONAL:
            STAT_INCREMENT(Stats, uni_in_stream_count);
            break;
        }
      }
      break;
    }
  }
}

void Session::RemoveStream(int64_t id) {
  // ngtcp2 does not extend the max streams count automatically except in very
  // specific conditions, none of which apply once we've gotten this far. We
  // need to manually extend when a remote peer initiated stream is removed.
  Debug(this, "Removing stream %" PRIi64 " from session", id);
  if (!is_in_draining_period() && !is_in_closing_period() &&
      !state_->silent_close &&
      !ngtcp2_conn_is_local_stream(connection_.get(), id)) {
    if (ngtcp2_is_bidi_stream(id))
      ngtcp2_conn_extend_max_streams_bidi(connection_.get(), 1);
    else
      ngtcp2_conn_extend_max_streams_uni(connection_.get(), 1);
  }

  // Frees the persistent reference to the Stream object, allowing it to be gc'd
  // any time after the JS side releases it's own reference.
  streams_.erase(id);
  ngtcp2_conn_set_stream_user_data(*this, id, nullptr);
}

void Session::ResumeStream(int64_t id) {
  Debug(this, "Resuming stream %" PRIi64, id);
  SendPendingDataScope send_scope(this);
  application_->ResumeStream(id);
}

void Session::ShutdownStream(int64_t id, QuicError error) {
  Debug(this, "Shutting down stream %" PRIi64 " with error %s", id, error);
  SendPendingDataScope send_scope(this);
  ngtcp2_conn_shutdown_stream(*this,
                              0,
                              id,
                              error.type() == QuicError::Type::APPLICATION
                                  ? error.code()
                                  : NGTCP2_APP_NOERROR);
}

void Session::StreamDataBlocked(int64_t id) {
  Debug(this, "Stream %" PRIi64 " is blocked", id);
  STAT_INCREMENT(Stats, block_count);
  application_->BlockStream(id);
}

void Session::ShutdownStreamWrite(int64_t id, QuicError code) {
  Debug(this, "Shutting down stream %" PRIi64 " write with error %s", id, code);
  SendPendingDataScope send_scope(this);
  ngtcp2_conn_shutdown_stream_write(*this,
                                    0,
                                    id,
                                    code.type() == QuicError::Type::APPLICATION
                                        ? code.code()
                                        : NGTCP2_APP_NOERROR);
}

void Session::CollectSessionTicketAppData(
    SessionTicket::AppData* app_data) const {
  application_->CollectSessionTicketAppData(app_data);
}

SessionTicket::AppData::Status Session::ExtractSessionTicketAppData(
    const SessionTicket::AppData& app_data,
    SessionTicket::AppData::Source::Flag flag) {
  return application_->ExtractSessionTicketAppData(app_data, flag);
}

void Session::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("config", config_);
  tracker->TrackField("endpoint", endpoint_);
  tracker->TrackField("streams", streams_);
  tracker->TrackField("local_address", local_address_);
  tracker->TrackField("remote_address", remote_address_);
  tracker->TrackField("application", application_);
  tracker->TrackField("tls_session", tls_session_);
  tracker->TrackField("timer", timer_);
  tracker->TrackField("conn_closebuf", conn_closebuf_);
  tracker->TrackField("qlog_stream", qlog_stream_);
  tracker->TrackField("keylog_stream", keylog_stream_);
}

bool Session::is_in_closing_period() const {
  return ngtcp2_conn_in_closing_period(*this) != 0;
}

bool Session::is_in_draining_period() const {
  return ngtcp2_conn_in_draining_period(*this) != 0;
}

bool Session::wants_session_ticket() const {
  return state_->session_ticket == 1;
}

void Session::SetStreamOpenAllowed() {
  state_->stream_open_allowed = 1;
}

bool Session::can_send_packets() const {
  // We can send packets if we're not in the middle of a ngtcp2 callback,
  // we're not destroyed, we're not in a draining or closing period, and
  // endpoint is set.
  return !NgTcp2CallbackScope::in_ngtcp2_callback(env()) && !is_destroyed() &&
         !is_in_draining_period() && !is_in_closing_period() && endpoint_;
}

bool Session::can_create_streams() const {
  return !state_->destroyed && !state_->graceful_close && !state_->closing &&
         !is_in_closing_period() && !is_in_draining_period();
}

uint64_t Session::max_data_left() const {
  return ngtcp2_conn_get_max_data_left(*this);
}

uint64_t Session::max_local_streams_uni() const {
  return ngtcp2_conn_get_streams_uni_left(*this);
}

uint64_t Session::max_local_streams_bidi() const {
  return ngtcp2_conn_get_local_transport_params(*this)
      ->initial_max_streams_bidi;
}

void Session::set_wrapped() {
  state_->wrapped = 1;
}

void Session::set_priority_supported(bool on) {
  state_->priority_supported = on ? 1 : 0;
}

void Session::DoClose(bool silent) {
  DCHECK(!is_destroyed());
  Debug(this, "Session is closing. Silently %s", silent ? "yes" : "no");
  // Once Close has been called, we cannot re-enter
  if (state_->closing == 1) return;
  state_->closing = 1;
  state_->silent_close = silent ? 1 : 0;
  STAT_RECORD_TIMESTAMP(Stats, closing_at);

  // Iterate through all of the known streams and close them. The streams
  // will remove themselves from the Session as soon as they are closed.
  // Note: we create a copy because the streams will remove themselves
  // while they are cleaning up which will invalidate the iterator.
  auto streams = streams_;
  for (auto& stream : streams) stream.second->Destroy(last_error_);
  DCHECK(streams.empty());

  // If the state has not been passed out to JavaScript yet, we can skip closing
  // entirely and drop directly out to Destroy.
  if (!state_->wrapped) return Destroy();

  // If we're not running within a ngtcp2 callback scope, schedule a
  // CONNECTION_CLOSE to be sent. If we are within a ngtcp2 callback scope,
  // sending the CONNECTION_CLOSE will be deferred.
  { MaybeCloseConnectionScope close_scope(this, silent); }

  // We emit a close callback so that the JavaScript side can clean up anything
  // it needs to clean up before destroying. It's the JavaScript side's
  // responsibility to call destroy() when ready.
  EmitClose();
}

void Session::ExtendStreamOffset(int64_t id, size_t amount) {
  Debug(this, "Extending stream %" PRIi64 " offset by %zu", id, amount);
  ngtcp2_conn_extend_max_stream_offset(*this, id, amount);
}

void Session::ExtendOffset(size_t amount) {
  Debug(this, "Extending offset by %zu", amount);
  ngtcp2_conn_extend_max_offset(*this, amount);
}

void Session::UpdateDataStats() {
  if (state_->destroyed) return;
  Debug(this, "Updating data stats");
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
  DCHECK(!NgTcp2CallbackScope::in_ngtcp2_callback(env()));
  if (is_destroyed() || is_in_draining_period() || state_->silent_close) return;

  Debug(this, "Sending connection close");
  auto on_exit = OnScopeLeave([this] { UpdateTimer(); });

  switch (config_.side) {
    case Side::SERVER: {
      if (!is_in_closing_period() && !StartClosingPeriod()) {
        Close(CloseMethod::SILENT);
      } else {
        DCHECK(conn_closebuf_);
        Send(conn_closebuf_->Clone());
      }
      return;
    }
    case Side::CLIENT: {
      Path path(local_address_, remote_address_);
      auto packet = Packet::Create(env(),
                                   endpoint_.get(),
                                   remote_address_,
                                   kDefaultMaxPacketLength,
                                   "immediate connection close (client)");
      ngtcp2_vec vec = *packet;
      ssize_t nwrite = ngtcp2_conn_write_connection_close(
          *this, &path, nullptr, vec.base, vec.len, last_error_, uv_hrtime());

      if (UNLIKELY(nwrite < 0)) {
        packet->Done(UV_ECANCELED);
        last_error_ = QuicError::ForNgtcp2Error(NGTCP2_INTERNAL_ERROR);
        Close(CloseMethod::SILENT);
      } else {
        packet->Truncate(nwrite);
        Send(std::move(packet));
      }
      return;
    }
  }
  UNREACHABLE();
}

void Session::OnTimeout() {
  HandleScope scope(env()->isolate());
  if (is_destroyed()) return;

  int ret = ngtcp2_conn_handle_expiry(*this, uv_hrtime());
  if (NGTCP2_OK(ret) && !is_in_closing_period() && !is_in_draining_period()) {
    Debug(this, "Sending pending data after timr expiry");
    SendPendingDataScope send_scope(this);
    return;
  }

  Debug(this, "Session timed out");
  last_error_ = QuicError::ForNgtcp2Error(ret);
  Close(CloseMethod::SILENT);
}

void Session::UpdateTimer() {
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
  timer_.Update(timeout == 0 ? 1 : timeout);
}

bool Session::StartClosingPeriod() {
  if (is_in_closing_period()) return true;
  if (is_destroyed()) return false;

  Debug(this, "Session is entering closing period");

  conn_closebuf_ = Packet::CreateConnectionClosePacket(
      env(), endpoint_.get(), remote_address_, *this, last_error_);

  // If we were unable to create a connection close packet, we're in trouble.
  // Set the internal error and return false so that the session will be
  // silently closed.
  if (!conn_closebuf_) {
    last_error_ = QuicError::ForNgtcp2Error(NGTCP2_INTERNAL_ERROR);
    return false;
  }

  return true;
}

void Session::DatagramStatus(uint64_t datagramId, quic::DatagramStatus status) {
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
  // If there is nothing watching for the datagram on the JavaScript side,
  // we just drop it on the floor.
  if (state_->datagram == 0 || datalen == 0) return;

  auto backing = ArrayBuffer::NewBackingStore(env()->isolate(), datalen);
  Debug(this, "Session is receiving datagram of size %zu", datalen);
  memcpy(backing->Data(), data, datalen);
  STAT_INCREMENT(Stats, datagrams_received);
  STAT_INCREMENT_N(Stats, bytes_received, datalen);
  EmitDatagram(Store(std::move(backing), datalen), flag);
}

bool Session::GenerateNewConnectionId(ngtcp2_cid* cid,
                                      size_t len,
                                      uint8_t* token) {
  CID cid_ = config_.options.cid_factory->GenerateInto(cid, len);
  Debug(this, "Generated new connection id %s", cid_);
  StatelessResetToken new_token(
      token, endpoint_->options().reset_token_secret, cid_);
  endpoint_->AssociateCID(cid_, config_.scid);
  endpoint_->AssociateStatelessResetToken(new_token, this);
  return true;
}

bool Session::HandshakeCompleted() {
  Debug(this, "Session handshake completed");

  if (state_->handshake_completed) return false;
  state_->handshake_completed = 1;

  STAT_RECORD_TIMESTAMP(Stats, handshake_completed_at);

  if (!tls_session().early_data_was_accepted())
    ngtcp2_conn_tls_early_data_rejected(*this);

  // When in a server session, handshake completed == handshake confirmed.
  if (is_server()) {
    HandshakeConfirmed();

    if (!endpoint().is_closed() && !endpoint().is_closing()) {
      auto token = endpoint().GenerateNewToken(version(), remote_address_);
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
  if (state_->handshake_confirmed) return;

  Debug(this, "Session handshake confirmed");

  state_->handshake_confirmed = true;
  STAT_RECORD_TIMESTAMP(Stats, handshake_confirmed_at);
}

void Session::SelectPreferredAddress(PreferredAddress* preferredAddress) {
  if (config_.options.preferred_address_strategy ==
      PreferredAddress::Policy::IGNORE_PREFERRED) {
    Debug(this, "Ignoring preferred address");
    return;
  }

  auto local_address = endpoint_->local_address();
  int family = local_address.family();

  switch (family) {
    case AF_INET: {
      Debug(this, "Selecting preferred address for AF_INET");
      auto ipv4 = preferredAddress->ipv4();
      if (ipv4.has_value()) {
        if (ipv4->address.empty() || ipv4->port == 0) return;
        CHECK(SocketAddress::New(AF_INET,
                                 std::string(ipv4->address).c_str(),
                                 ipv4->port,
                                 &remote_address_));
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
                                 &remote_address_));
        preferredAddress->Use(ipv6.value());
      }
      break;
    }
  }
}

CID Session::new_cid(size_t len) const {
  return config_.options.cid_factory->Generate(len);
}

// JavaScript callouts

void Session::EmitClose(const QuicError& error) {
  DCHECK(!is_destroyed());
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
  Debug(this, "Notifying JavaScript of session close");
  MakeCallback(
      BindingData::Get(env()).session_close_callback(), arraysize(argv), argv);
}

void Session::EmitDatagram(Store&& datagram, DatagramReceivedFlags flag) {
  DCHECK(!is_destroyed());
  if (!env()->can_call_into_js()) return;

  CallbackScope cbv_scope(this);

  Local<Value> argv[] = {datagram.ToUint8Array(env()),
                         v8::Boolean::New(env()->isolate(), flag.early)};

  Debug(this, "Notifying JavaScript of datagram");
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
  Debug(this, "Notifying JavaScript of datagram status");
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
      Undefined(isolate),  // The selected alpn
      Undefined(isolate),  // Cipher name
      Undefined(isolate),  // Cipher version
      Undefined(isolate),  // Validation error reason
      Undefined(isolate),  // Validation error code
      v8::Boolean::New(isolate, tls_session().early_data_was_accepted())};

  auto& tls = tls_session();
  auto peerVerifyError = tls.VerifyPeerIdentity(env());
  if (peerVerifyError.has_value() &&
      (!peerVerifyError->reason.ToLocal(&argv[kValidationErrorReason]) ||
       !peerVerifyError->code.ToLocal(&argv[kValidationErrorCode]))) {
    return;
  }

  if (!ToV8Value(env()->context(), tls.servername())
           .ToLocal(&argv[kServerName]) ||
      !ToV8Value(env()->context(), tls.alpn()).ToLocal(&argv[kSelectedAlpn]) ||
      !tls.cipher_name(env()).ToLocal(&argv[kCipherName]) ||
      !tls.cipher_version(env()).ToLocal(&argv[kCipherVersion])) {
    return;
  }

  Debug(this, "Notifying JavaScript of handshake complete");
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
  if (LIKELY(state_->path_validation == 0)) return;

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
  if (LIKELY(!wants_session_ticket())) {
    Debug(this, "Session ticket was discarded");
    return;
  }

  CallbackScope<Session> cb_scope(this);

  auto remote_transport_params = GetRemoteTransportParams();
  Store transport_params;
  if (remote_transport_params)
    transport_params = remote_transport_params.Encode(env());

  SessionTicket session_ticket(std::move(ticket), std::move(transport_params));
  Local<Value> argv;
  if (session_ticket.encode(env()).ToLocal(&argv)) {
    Debug(this, "Notifying JavaScript of session ticket");
    MakeCallback(BindingData::Get(env()).session_ticket_callback(), 1, &argv);
  }
}

void Session::EmitStream(BaseObjectPtr<Stream> stream) {
  if (is_destroyed()) return;
  if (!env()->can_call_into_js()) return;
  CallbackScope<Session> cb_scope(this);
  Local<Value> arg = stream->object();

  Debug(this, "Notifying JavaScript of stream created");
  MakeCallback(BindingData::Get(env()).stream_created_callback(), 1, &arg);
}

void Session::EmitVersionNegotiation(const ngtcp2_pkt_hd& hd,
                                     const uint32_t* sv,
                                     size_t nsv) {
  DCHECK(!is_destroyed());
  DCHECK(!is_server());
  if (!env()->can_call_into_js()) return;

  auto isolate = env()->isolate();
  const auto to_integer = [&](uint32_t version) {
    return Integer::NewFromUnsigned(isolate, version);
  };

  CallbackScope<Session> cb_scope(this);

  // version() is the version that was actually configured for this session.

  // versions are the versions requested by the peer.
  MaybeStackBuffer<Local<Value>, 5> versions;
  versions.AllocateSufficientStorage(nsv);
  for (size_t n = 0; n < nsv; n++) versions[n] = to_integer(sv[n]);

  // supported are the versons we acutually support expressed as a range.
  // The first value is the minimum version, the second is the maximum.
  Local<Value> supported[] = {to_integer(config_.options.min_version),
                              to_integer(config_.options.version)};

  Local<Value> argv[] = {// The version configured for this session.
                         to_integer(version()),
                         // The versions requested.
                         Array::New(isolate, versions.out(), nsv),
                         // The versions we actually support.
                         Array::New(isolate, supported, arraysize(supported))};

  Debug(this, "Notifying JavaScript of version negotiation");
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
// ngtcp2 static callback functions

#define NGTCP2_CALLBACK_SCOPE(name)                                            \
  auto name = Impl::From(conn, user_data);                                     \
  if (UNLIKELY(name->is_destroyed())) return NGTCP2_ERR_CALLBACK_FAILURE;      \
  NgTcp2CallbackScope scope(session->env());

struct Session::Impl {
  static Session* From(ngtcp2_conn* conn, void* user_data) {
    DCHECK_NOT_NULL(user_data);
    auto session = static_cast<Session*>(user_data);
    DCHECK_EQ(conn, session->connection_.get());
    return session;
  }

  static void DoDestroy(const FunctionCallbackInfo<Value>& args) {
    Session* session;
    ASSIGN_OR_RETURN_UNWRAP(&session, args.This());
    session->Destroy();
  }

  static void GetRemoteAddress(const FunctionCallbackInfo<Value>& args) {
    auto env = Environment::GetCurrent(args);
    Session* session;
    ASSIGN_OR_RETURN_UNWRAP(&session, args.This());
    auto address = session->remote_address();
    args.GetReturnValue().Set(
        SocketAddressBase::Create(env, std::make_shared<SocketAddress>(address))
            ->object());
  }

  static void GetCertificate(const FunctionCallbackInfo<Value>& args) {
    auto env = Environment::GetCurrent(args);
    Session* session;
    ASSIGN_OR_RETURN_UNWRAP(&session, args.This());
    Local<Value> ret;
    if (session->tls_session().cert(env).ToLocal(&ret))
      args.GetReturnValue().Set(ret);
  }

  static void GetEphemeralKeyInfo(const FunctionCallbackInfo<Value>& args) {
    auto env = Environment::GetCurrent(args);
    Session* session;
    ASSIGN_OR_RETURN_UNWRAP(&session, args.This());
    Local<Object> ret;
    if (!session->is_server() &&
        session->tls_session().ephemeral_key(env).ToLocal(&ret))
      args.GetReturnValue().Set(ret);
  }

  static void GetPeerCertificate(const FunctionCallbackInfo<Value>& args) {
    auto env = Environment::GetCurrent(args);
    Session* session;
    ASSIGN_OR_RETURN_UNWRAP(&session, args.This());
    Local<Value> ret;
    if (session->tls_session().peer_cert(env).ToLocal(&ret))
      args.GetReturnValue().Set(ret);
  }

  static void GracefulClose(const FunctionCallbackInfo<Value>& args) {
    Session* session;
    ASSIGN_OR_RETURN_UNWRAP(&session, args.This());
    session->Close(Session::CloseMethod::GRACEFUL);
  }

  static void SilentClose(const FunctionCallbackInfo<Value>& args) {
    // This is exposed for testing purposes only!
    Session* session;
    ASSIGN_OR_RETURN_UNWRAP(&session, args.This());
    session->Close(Session::CloseMethod::SILENT);
  }

  static void UpdateKey(const FunctionCallbackInfo<Value>& args) {
    Session* session;
    ASSIGN_OR_RETURN_UNWRAP(&session, args.This());
    // Initiating a key update may fail if it is done too early (either
    // before the TLS handshake has been confirmed or while a previous
    // key update is being processed). When it fails, InitiateKeyUpdate()
    // will return false.
    Debug(session, "Initiating key update");
    args.GetReturnValue().Set(session->tls_session().InitiateKeyUpdate());
  }

  static void DoOpenStream(const FunctionCallbackInfo<Value>& args) {
    Session* session;
    ASSIGN_OR_RETURN_UNWRAP(&session, args.This());
    DCHECK(args[0]->IsUint32());
    auto direction = static_cast<Direction>(args[0].As<Uint32>()->Value());
    BaseObjectPtr<Stream> stream = session->OpenStream(direction);

    if (stream) args.GetReturnValue().Set(stream->object());
  }

  static void DoSendDatagram(const FunctionCallbackInfo<Value>& args) {
    auto env = Environment::GetCurrent(args);
    Session* session;
    ASSIGN_OR_RETURN_UNWRAP(&session, args.This());
    DCHECK(args[0]->IsArrayBufferView());
    args.GetReturnValue().Set(BigInt::New(
        env->isolate(),
        session->SendDatagram(Store(args[0].As<ArrayBufferView>()))));
  }

  static int on_acknowledge_stream_data_offset(ngtcp2_conn* conn,
                                               int64_t stream_id,
                                               uint64_t offset,
                                               uint64_t datalen,
                                               void* user_data,
                                               void* stream_user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    session->application().AcknowledgeStreamData(Stream::From(stream_user_data),
                                                 datalen);
    return NGTCP2_SUCCESS;
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
        endpoint.AssociateCID(session->config_.scid, CID(cid));
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
    session->application().ExtendMaxStreams(
        EndpointLabel::REMOTE, Direction::BIDIRECTIONAL, max_streams);
    return NGTCP2_SUCCESS;
  }

  static int on_extend_max_remote_streams_uni(ngtcp2_conn* conn,
                                              uint64_t max_streams,
                                              void* user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    session->application().ExtendMaxStreams(
        EndpointLabel::REMOTE, Direction::UNIDIRECTIONAL, max_streams);
    return NGTCP2_SUCCESS;
  }

  static int on_extend_max_streams_bidi(ngtcp2_conn* conn,
                                        uint64_t max_streams,
                                        void* user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    session->application().ExtendMaxStreams(
        EndpointLabel::LOCAL, Direction::BIDIRECTIONAL, max_streams);
    return NGTCP2_SUCCESS;
  }

  static int on_extend_max_streams_uni(ngtcp2_conn* conn,
                                       uint64_t max_streams,
                                       void* user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    session->application().ExtendMaxStreams(
        EndpointLabel::LOCAL, Direction::UNIDIRECTIONAL, max_streams);
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
    return session->GenerateNewConnectionId(cid, cidlen, token)
               ? NGTCP2_SUCCESS
               : NGTCP2_ERR_CALLBACK_FAILURE;
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
    DatagramReceivedFlags f;
    f.early = flags & NGTCP2_DATAGRAM_FLAG_0RTT;
    session->DatagramReceived(data, datalen, f);
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
    auto session = Impl::From(conn, user_data);
    if (UNLIKELY(session->is_destroyed())) return NGTCP2_ERR_CALLBACK_FAILURE;
    CHECK(!session->is_server());

    if (level != NGTCP2_ENCRYPTION_LEVEL_1RTT) return NGTCP2_SUCCESS;

    Debug(session,
          "Receiving RX key for level %d for dcid %s",
          to_string(level),
          session->config().dcid);

    return session->application().Start() ? NGTCP2_SUCCESS
                                          : NGTCP2_ERR_CALLBACK_FAILURE;
  }

  static int on_receive_stateless_reset(ngtcp2_conn* conn,
                                        const ngtcp2_pkt_stateless_reset* sr,
                                        void* user_data) {
    NGTCP2_CALLBACK_SCOPE(session)
    session->state_->stateless_reset = 1;
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
    Stream::ReceiveDataFlags f;
    f.early = flags & NGTCP2_STREAM_DATA_FLAG_0RTT;
    f.fin = flags & NGTCP2_STREAM_DATA_FLAG_FIN;

    if (stream_user_data == nullptr) {
      // We have an implicitly created stream.
      auto stream = session->CreateStream(stream_id);
      if (stream) {
        session->EmitStream(stream);
        session->application().ReceiveStreamData(
            stream.get(), data, datalen, f);
      } else {
        return ngtcp2_conn_shutdown_stream(
                   *session, 0, stream_id, NGTCP2_APP_NOERROR) == 0
                   ? NGTCP2_SUCCESS
                   : NGTCP2_ERR_CALLBACK_FAILURE;
      }
    } else {
      session->application().ReceiveStreamData(
          Stream::From(stream_user_data), data, datalen, f);
    }
    return NGTCP2_SUCCESS;
  }

  static int on_receive_tx_key(ngtcp2_conn* conn,
                               ngtcp2_encryption_level level,
                               void* user_data) {
    auto session = Impl::From(conn, user_data);
    if (UNLIKELY(session->is_destroyed())) return NGTCP2_ERR_CALLBACK_FAILURE;
    CHECK(session->is_server());

    if (level != NGTCP2_ENCRYPTION_LEVEL_1RTT) return NGTCP2_SUCCESS;

    Debug(session,
          "Receiving TX key for level %d for dcid %s",
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

Session::QuicConnectionPointer Session::InitConnection() {
  ngtcp2_conn* conn;
  Path path(local_address_, remote_address_);
  Debug(this, "Initializing session for path %s", path);
  TransportParams::Config tp_config(
      config_.side, config_.ocid, config_.retry_scid);
  TransportParams transport_params(tp_config, config_.options.transport_params);
  transport_params.GenerateSessionTokens(this);

  switch (config_.side) {
    case Side::SERVER: {
      CHECK_EQ(ngtcp2_conn_server_new(&conn,
                                      config_.dcid,
                                      config_.scid,
                                      path,
                                      config_.version,
                                      &Impl::SERVER,
                                      &config_.settings,
                                      transport_params,
                                      &allocator_,
                                      this),
               0);
      break;
    }
    case Side::CLIENT: {
      CHECK_EQ(ngtcp2_conn_client_new(&conn,
                                      config_.dcid,
                                      config_.scid,
                                      path,
                                      config_.version,
                                      &Impl::CLIENT,
                                      &config_.settings,
                                      transport_params,
                                      &allocator_,
                                      this),
               0);
      break;
    }
  }
  return QuicConnectionPointer(conn);
}

void Session::InitPerIsolate(IsolateData* data,
                             v8::Local<v8::ObjectTemplate> target) {
  // TODO(@jasnell): Implement the per-isolate state
}

void Session::InitPerContext(Realm* realm, Local<Object> target) {
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
