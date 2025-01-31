#pragma once

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC

#include <base_object.h>
#include <env.h>
#include <memory_tracker.h>
#include <nghttp3/nghttp3.h>
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <node.h>
#include <node_mem.h>
#include <v8.h>
#include <unordered_map>
#include <vector>
#include "defs.h"

namespace node::quic {

class Endpoint;
class Packet;

// ============================================================================

// The FunctionTemplates the BindingData will store for us.
#define QUIC_CONSTRUCTORS(V)                                                   \
  V(endpoint)                                                                  \
  V(logstream)                                                                 \
  V(packet)                                                                    \
  V(session)                                                                   \
  V(stream)                                                                    \
  V(udp)                                                                       \
  V(http3application)

// The callbacks are persistent v8::Function references that are set in the
// quic::BindingState used to communicate data and events back out to the JS
// environment. They are set once from the JavaScript side when the
// internalBinding('quic') is first loaded.
#define QUIC_JS_CALLBACKS(V)                                                   \
  V(endpoint_close, EndpointClose)                                             \
  V(session_new, SessionNew)                                                   \
  V(session_close, SessionClose)                                               \
  V(session_datagram, SessionDatagram)                                         \
  V(session_datagram_status, SessionDatagramStatus)                            \
  V(session_handshake, SessionHandshake)                                       \
  V(session_ticket, SessionTicket)                                             \
  V(session_version_negotiation, SessionVersionNegotiation)                    \
  V(session_path_validation, SessionPathValidation)                            \
  V(stream_close, StreamClose)                                                 \
  V(stream_created, StreamCreated)                                             \
  V(stream_reset, StreamReset)                                                 \
  V(stream_headers, StreamHeaders)                                             \
  V(stream_blocked, StreamBlocked)                                             \
  V(stream_trailers, StreamTrailers)

// The various JS strings the implementation uses.
#define QUIC_STRINGS(V)                                                        \
  V(aborted, "aborted")                                                        \
  V(acknowledged, "acknowledged")                                              \
  V(ack_delay_exponent, "ackDelayExponent")                                    \
  V(active_connection_id_limit, "activeConnectionIDLimit")                     \
  V(address_lru_size, "addressLRUSize")                                        \
  V(application_provider, "provider")                                          \
  V(bbr, "bbr")                                                                \
  V(ca, "ca")                                                                  \
  V(certs, "certs")                                                            \
  V(cc_algorithm, "cc")                                                        \
  V(crl, "crl")                                                                \
  V(ciphers, "ciphers")                                                        \
  V(cubic, "cubic")                                                            \
  V(disable_stateless_reset, "disableStatelessReset")                          \
  V(enable_connect_protocol, "enableConnectProtocol")                          \
  V(enable_datagrams, "enableDatagrams")                                       \
  V(enable_tls_trace, "tlsTrace")                                              \
  V(endpoint, "Endpoint")                                                      \
  V(endpoint_udp, "Endpoint::UDP")                                             \
  V(failure, "failure")                                                        \
  V(groups, "groups")                                                          \
  V(handshake_timeout, "handshakeTimeout")                                     \
  V(http3_alpn, &NGHTTP3_ALPN_H3[1])                                           \
  V(http3application, "Http3Application")                                      \
  V(initial_max_data, "initialMaxData")                                        \
  V(initial_max_stream_data_bidi_local, "initialMaxStreamDataBidiLocal")       \
  V(initial_max_stream_data_bidi_remote, "initialMaxStreamDataBidiRemote")     \
  V(initial_max_stream_data_uni, "initialMaxStreamDataUni")                    \
  V(initial_max_streams_bidi, "initialMaxStreamsBidi")                         \
  V(initial_max_streams_uni, "initialMaxStreamsUni")                           \
  V(ipv6_only, "ipv6Only")                                                     \
  V(keylog, "keylog")                                                          \
  V(keys, "keys")                                                              \
  V(logstream, "LogStream")                                                    \
  V(lost, "lost")                                                              \
  V(max_ack_delay, "maxAckDelay")                                              \
  V(max_connections_per_host, "maxConnectionsPerHost")                         \
  V(max_connections_total, "maxConnectionsTotal")                              \
  V(max_datagram_frame_size, "maxDatagramFrameSize")                           \
  V(max_field_section_size, "maxFieldSectionSize")                             \
  V(max_header_length, "maxHeaderLength")                                      \
  V(max_header_pairs, "maxHeaderPairs")                                        \
  V(max_idle_timeout, "maxIdleTimeout")                                        \
  V(max_payload_size, "maxPayloadSize")                                        \
  V(max_retries, "maxRetries")                                                 \
  V(max_stateless_resets, "maxStatelessResetsPerHost")                         \
  V(max_stream_window, "maxStreamWindow")                                      \
  V(max_window, "maxWindow")                                                   \
  V(min_version, "minVersion")                                                 \
  V(packetwrap, "PacketWrap")                                                  \
  V(preferred_address_strategy, "preferredAddressPolicy")                      \
  V(protocol, "protocol")                                                      \
  V(qlog, "qlog")                                                              \
  V(qpack_blocked_streams, "qpackBlockedStreams")                              \
  V(qpack_encoder_max_dtable_capacity, "qpackEncoderMaxDTableCapacity")        \
  V(qpack_max_dtable_capacity, "qpackMaxDTableCapacity")                       \
  V(reject_unauthorized, "rejectUnauthorized")                                 \
  V(reno, "reno")                                                              \
  V(retry_token_expiration, "retryTokenExpiration")                            \
  V(reset_token_secret, "resetTokenSecret")                                    \
  V(rx_loss, "rxDiagnosticLoss")                                               \
  V(servername, "servername")                                                  \
  V(session, "Session")                                                        \
  V(stream, "Stream")                                                          \
  V(success, "success")                                                        \
  V(tls_options, "tls")                                                        \
  V(token_expiration, "tokenExpiration")                                       \
  V(token_secret, "tokenSecret")                                               \
  V(transport_params, "transportParams")                                       \
  V(tx_loss, "txDiagnosticLoss")                                               \
  V(udp_receive_buffer_size, "udpReceiveBufferSize")                           \
  V(udp_send_buffer_size, "udpSendBufferSize")                                 \
  V(udp_ttl, "udpTTL")                                                         \
  V(unacknowledged_packet_threshold, "unacknowledgedPacketThreshold")          \
  V(validate_address, "validateAddress")                                       \
  V(verify_client, "verifyClient")                                             \
  V(verify_private_key, "verifyPrivateKey")                                    \
  V(version, "version")

// =============================================================================
// The BindingState object holds state for the internalBinding('quic') binding
// instance. It is mostly used to hold the persistent constructors, strings, and
// callback references used for the rest of the implementation.
//
// TODO(@jasnell): Make this snapshotable?
class BindingData final
    : public BaseObject,
      public mem::NgLibMemoryManager<BindingData, ngtcp2_mem> {
 public:
  SET_BINDING_ID(quic_binding_data)
  static void InitPerContext(Realm* realm, v8::Local<v8::Object> target);
  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);

  static BindingData& Get(Environment* env);

  BindingData(Realm* realm, v8::Local<v8::Object> object);
  DISALLOW_COPY_AND_MOVE(BindingData)

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(BindingData)
  SET_SELF_SIZE(BindingData)

  // NgLibMemoryManager
  operator ngtcp2_mem();
  operator nghttp3_mem();
  void CheckAllocatedSize(size_t previous_size) const;
  void IncreaseAllocatedSize(size_t size);
  void DecreaseAllocatedSize(size_t size);

  // Installs the set of JavaScript callback functions that are used to
  // bridge out to the JS API.
  static void SetCallbacks(const v8::FunctionCallbackInfo<v8::Value>& args);

  std::vector<BaseObjectPtr<BaseObject>> packet_freelist;

  std::unordered_map<Endpoint*, BaseObjectPtr<BaseObject>> listening_endpoints;

  // Purge the packet free list to free up memory.
  static void FlushPacketFreelist(
      const v8::FunctionCallbackInfo<v8::Value>& args);

  bool in_ngtcp2_callback_scope = false;
  bool in_nghttp3_callback_scope = false;

  // The following set up various storage and accessors for common strings,
  // construction templates, and callbacks stored on the BindingData. These
  // are all defined in defs.h

#define V(name)                                                                \
  void set_##name##_constructor_template(                                      \
      v8::Local<v8::FunctionTemplate> tmpl);                                   \
  v8::Local<v8::FunctionTemplate> name##_constructor_template() const;
  QUIC_CONSTRUCTORS(V)
#undef V

#define V(name, _)                                                             \
  void set_##name##_callback(v8::Local<v8::Function> fn);                      \
  v8::Local<v8::Function> name##_callback() const;
  QUIC_JS_CALLBACKS(V)
#undef V

#define V(name, _) v8::Local<v8::String> name##_string() const;
  QUIC_STRINGS(V)
#undef V

#define V(name, _) v8::Local<v8::String> on_##name##_string() const;
  QUIC_JS_CALLBACKS(V)
#undef V

  size_t current_ngtcp2_memory_ = 0;

#define V(name) v8::Global<v8::FunctionTemplate> name##_constructor_template_;
  QUIC_CONSTRUCTORS(V)
#undef V

#define V(name, _) v8::Global<v8::Function> name##_callback_;
  QUIC_JS_CALLBACKS(V)
#undef V

#define V(name, _) mutable v8::Eternal<v8::String> name##_string_;
  QUIC_STRINGS(V)
#undef V

#define V(name, _) mutable v8::Eternal<v8::String> on_##name##_string_;
  QUIC_JS_CALLBACKS(V)
#undef V
};

void IllegalConstructor(const v8::FunctionCallbackInfo<v8::Value>& args);

// The ngtcp2 and nghttp3 callbacks have certain restrictions
// that forbid re-entry. We provide the following scopes for
// use in those to help protect against it.
struct NgTcp2CallbackScope {
  Environment* env;
  explicit NgTcp2CallbackScope(Environment* env);
  DISALLOW_COPY_AND_MOVE(NgTcp2CallbackScope)
  ~NgTcp2CallbackScope();
  static bool in_ngtcp2_callback(Environment* env);
};

struct NgHttp3CallbackScope {
  Environment* env;
  explicit NgHttp3CallbackScope(Environment* env);
  DISALLOW_COPY_AND_MOVE(NgHttp3CallbackScope)
  ~NgHttp3CallbackScope();
  static bool in_nghttp3_callback(Environment* env);
};

struct CallbackScopeBase {
  Environment* env;
  v8::Context::Scope context_scope;
  v8::TryCatch try_catch;

  explicit CallbackScopeBase(Environment* env);
  DISALLOW_COPY_AND_MOVE(CallbackScopeBase)
  ~CallbackScopeBase();
};

// Maintains a strong reference to BaseObject type ptr to keep it alive during
// a MakeCallback during which it might be destroyed.
template <typename T>
struct CallbackScope final : public CallbackScopeBase {
  BaseObjectPtr<T> ref;
  explicit CallbackScope(const T* ptr)
      : CallbackScopeBase(ptr->env()), ref(ptr) {}
  DISALLOW_COPY_AND_MOVE(CallbackScope)
  explicit CallbackScope(T* ptr) : CallbackScopeBase(ptr->env()), ref(ptr) {}
};

}  // namespace node::quic

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
