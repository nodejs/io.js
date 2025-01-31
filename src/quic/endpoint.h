#pragma once

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC

#include <aliased_struct.h>
#include <async_wrap.h>
#include <env.h>
#include <node_sockaddr.h>
#include <uv.h>
#include <v8.h>
#include <algorithm>
#include <optional>
#include "bindingdata.h"
#include "packet.h"
#include "session.h"
#include "sessionticket.h"
#include "tokens.h"

namespace node::quic {

// An Endpoint encapsulates the UDP local port binding and is responsible for
// sending and receiving QUIC packets. A single endpoint can act as both a QUIC
// client and server simultaneously.
class Endpoint final : public AsyncWrap, public Packet::Listener {
 public:
  static constexpr uint64_t DEFAULT_MAX_CONNECTIONS =
      std::min<uint64_t>(kMaxSizeT, static_cast<uint64_t>(kMaxSafeJsInteger));
  static constexpr uint64_t DEFAULT_MAX_CONNECTIONS_PER_HOST = 100;
  static constexpr uint64_t DEFAULT_MAX_SOCKETADDRESS_LRU_SIZE =
      (DEFAULT_MAX_CONNECTIONS_PER_HOST * 10);
  static constexpr uint64_t DEFAULT_MAX_STATELESS_RESETS = 10;
  static constexpr uint64_t DEFAULT_MAX_RETRY_LIMIT = 10;

  // Endpoint configuration options
  struct Options final : public MemoryRetainer {
    // The local socket address to which the UDP port will be bound. The port
    // may be 0 to have Node.js select an available port. IPv6 or IPv4 addresses
    // may be used. When using IPv6, dual mode will be supported by default.
    std::shared_ptr<SocketAddress> local_address;

    // Retry tokens issued by the Endpoint are time-limited. By default, retry
    // tokens expire after DEFAULT_RETRYTOKEN_EXPIRATION *seconds*. This is an
    // arbitrary choice that is not mandated by the QUIC specification; so we
    // can choose any value that makes sense here. Retry tokens are sent to the
    // client, which echoes them back to the server in a subsequent set of
    // packets, which means the expiration must be set high enough to allow a
    // reasonable round-trip time for the session TLS handshake to complete.
    uint64_t retry_token_expiration =
        RetryToken::QUIC_DEFAULT_RETRYTOKEN_EXPIRATION / NGTCP2_SECONDS;

    // Tokens issued using NEW_TOKEN are time-limited. By default, tokens expire
    // after DEFAULT_TOKEN_EXPIRATION *seconds*.
    uint64_t token_expiration =
        RegularToken::QUIC_DEFAULT_REGULARTOKEN_EXPIRATION / NGTCP2_SECONDS;

    // Each Endpoint places limits on the number of concurrent connections from
    // a single host, and the total number of concurrent connections allowed as
    // a whole. These are set to fairly modest, and arbitrary defaults. We can
    // set these to whatever we'd like.
    uint64_t max_connections_per_host = DEFAULT_MAX_CONNECTIONS_PER_HOST;
    uint64_t max_connections_total = DEFAULT_MAX_CONNECTIONS;

    // A stateless reset in QUIC is a discrete mechanism that one endpoint can
    // use to communicate to a peer that it has lost whatever state it
    // previously held about a session. Because generating a stateless reset
    // consumes resources (even very modestly), they can be a DOS vector in
    // which a malicious peer intentionally sends a large number of stateless
    // reset eliciting packets. To protect against that risk, we limit the
    // number of stateless resets that may be generated for a given remote host
    // within a window of time. This is not mandated by QUIC, and the limit is
    // arbitrary. We can set it to whatever we'd like.
    uint64_t max_stateless_resets = DEFAULT_MAX_STATELESS_RESETS;

    // For tracking the number of connections per host, the number of stateless
    // resets that have been sent, and tracking the path verification status of
    // a remote host, we maintain an LRU cache of the most recently seen hosts.
    // The address_lru_size parameter determines the size of that cache. The
    // default is set modestly at 10 times the default max connections per host.
    uint64_t address_lru_size = DEFAULT_MAX_SOCKETADDRESS_LRU_SIZE;

    // Similar to stateless resets, we enforce a limit on the number of retry
    // packets that can be generated and sent for a remote host. Generating
    // retry packets consumes a modest amount of resources and it's fairly
    // trivial for a malicious peer to trigger generation of a large number of
    // retries, so limiting them helps prevent a DOS vector.
    uint64_t max_retries = DEFAULT_MAX_RETRY_LIMIT;

    // The validate_address parameter instructs the Endpoint to perform explicit
    // address validation using retry tokens. This is strongly recommended and
    // should only be disabled in trusted, closed environments as a performance
    // optimization.
    bool validate_address = true;

    // The stateless reset mechanism can be disabled. This should rarely ever be
    // needed, and should only ever be done in trusted, closed environments as a
    // performance optimization.
    bool disable_stateless_reset = false;

#ifdef DEBUG
    // The rx_loss and tx_loss parameters are debugging tools that allow the
    // Endpoint to simulate random packet loss. The value for each parameter is
    // a value between 0.0 and 1.0 indicating a probability of packet loss. Each
    // time a packet is sent or received, the packet loss bit is calculated and
    // if true, the packet is silently dropped. This should only ever be used
    // for testing and debugging. There is never a reason why rx_loss and
    // tx_loss should ever be used in a production system.
    double rx_loss = 0.0;
    double tx_loss = 0.0;
#endif  // DEBUG

    // By default, when the endpoint is created, it will generate a
    // reset_token_secret at random. This is a secret used in generating
    // stateless reset tokens. In order for stateless reset to be effective,
    // however, it is necessary to use a deterministic secret that persists
    // across ngtcp2 endpoints and sessions. This means that the endpoint
    // configuration really should have a reset token secret passed in.
    TokenSecret reset_token_secret;

    // The secret used for generating new regular tokens.
    TokenSecret token_secret;

    // When the local_address specifies an IPv6 local address to bind to, the
    // ipv6_only parameter determines whether dual stack mode (supporting both
    // IPv6 and IPv4) transparently is supported. This sets the UV_UDP_IPV6ONLY
    // flag on the underlying uv_udp_t.
    bool ipv6_only = false;

    uint32_t udp_receive_buffer_size = 0;
    uint32_t udp_send_buffer_size = 0;

    // The UDP TTL configuration is the number of network hops a packet will be
    // forwarded through. The default is 64. The value is in the range 1 to 255.
    // Setting to 0 uses the default.
    uint8_t udp_ttl = 0;

    void MemoryInfo(MemoryTracker* tracker) const override;
    SET_MEMORY_INFO_NAME(Endpoint::Config)
    SET_SELF_SIZE(Options)

    static v8::Maybe<Options> From(Environment* env,
                                   v8::Local<v8::Value> value);

    std::string ToString() const;
  };

  bool HasInstance(Environment* env, v8::Local<v8::Value> value);
  static v8::Local<v8::FunctionTemplate> GetConstructorTemplate(
      Environment* env);
  static void InitPerIsolate(IsolateData* data,
                             v8::Local<v8::ObjectTemplate> target);
  static void InitPerContext(Realm* realm, v8::Local<v8::Object> target);
  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);

  Endpoint(Environment* env,
           v8::Local<v8::Object> object,
           const Endpoint::Options& options);

  inline operator Packet::Listener*() {
    return this;
  }

  inline const Options& options() const {
    return options_;
  }

  // While the busy flag is set, the Endpoint will reject all initial packets
  // with a SERVER_BUSY response. This allows us to build a circuit breaker
  // directly into the implementation, explicitly signaling that the server is
  // blocked when activity is too high.
  void MarkAsBusy(bool on = true);

  // Use the endpoint's token secret to generate a new token.
  RegularToken GenerateNewToken(uint32_t version,
                                const SocketAddress& remote_address);

  // Use the endpoint's reset token secret to generate a new stateless reset.
  StatelessResetToken GenerateNewStatelessResetToken(uint8_t* token,
                                                     const CID& cid) const;

  void AddSession(const CID& cid, BaseObjectPtr<Session> session);
  void RemoveSession(const CID& cid, const SocketAddress& remote_address);
  BaseObjectPtr<Session> FindSession(const CID& cid);

  // A single session may be associated with multiple CIDs.
  // AssociateCID registers the mapping both in the Endpoint and the inner
  // Endpoint.
  void AssociateCID(const CID& cid, const CID& scid);
  void DisassociateCID(const CID& cid);

  // Associates a given stateless reset token with the session. This allows
  // stateless reset tokens to be recognized and dispatched to the proper
  // Endpoint and Session for processing.
  void AssociateStatelessResetToken(const StatelessResetToken& token,
                                    Session* session);
  void DisassociateStatelessResetToken(const StatelessResetToken& token);

  void Send(const BaseObjectPtr<Packet>& packet);

  // Generates and sends a retry packet. This is terminal for the connection.
  // Retry packets are used to force explicit path validation by issuing a token
  // to the peer that it must thereafter include in all subsequent initial
  // packets. Upon receiving a retry packet, the peer must termination it's
  // initial attempt to establish a connection and start a new attempt.
  //
  // Retry packets will only ever be generated by QUIC servers, and only if the
  // QuicSocket is configured for explicit path validation. There is no way for
  // a client to force a retry packet to be created. However, once a client
  // determines that explicit path validation is enabled, it could attempt to
  // DOS by sending a large number of malicious initial packets to intentionally
  // ellicit retry packets (It can do so by intentionally sending initial
  // packets that ignore the retry token). To help mitigate that risk, we limit
  // the number of retries we send to a given remote endpoint.
  void SendRetry(const PathDescriptor& options);

  // Sends a version negotiation packet. This is terminal for the connection and
  // is sent only when a QUIC packet is received for an unsupported QUIC
  // version. It is possible that a malicious packet triggered this so we need
  // to be careful not to commit too many resources.
  void SendVersionNegotiation(const PathDescriptor& options);

  // Possibly generates and sends a stateless reset packet. This is terminal for
  // the connection. It is possible that a malicious packet triggered this so we
  // need to be careful not to commit too many resources.
  bool SendStatelessReset(const PathDescriptor& options, size_t source_len);

  // Shutdown a connection prematurely, before a Session is created. This should
  // only be called at the start of a session before the crypto keys have been
  // established.
  void SendImmediateConnectionClose(const PathDescriptor& options,
                                    QuicError error);

  // Listen for connections (act as a server).
  void Listen(const Session::Options& options);

  // Create a new client-side Session.
  BaseObjectPtr<Session> Connect(
      const SocketAddress& remote_address,
      const Session::Options& options,
      std::optional<SessionTicket> sessionTicket = std::nullopt);

  // Returns the local address only if the endpoint has been bound. Before
  // the endpoint is bound, or after it is closed, this will abort due to
  // a failed check so it is important to check `is_closed()` before calling.
  SocketAddress local_address() const;

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(Endpoint)
  SET_SELF_SIZE(Endpoint)

  struct Stats;
  struct State;

 private:
  class UDP final : public MemoryRetainer {
   public:
    explicit UDP(Endpoint* endpoint);
    ~UDP() override;

    int Bind(const Endpoint::Options& config);
    int Start();
    void Stop();
    void Close();
    int Send(const BaseObjectPtr<Packet>& packet);

    // Returns the local UDP socket address to which we are bound,
    // or fail with an assert if we are not bound.
    SocketAddress local_address() const;

    bool is_bound() const;
    bool is_closed() const;
    bool is_closed_or_closing() const;
    operator bool() const;

    void Ref();
    void Unref();

    void MemoryInfo(node::MemoryTracker* tracker) const override;
    SET_MEMORY_INFO_NAME(Endpoint::UDP)
    SET_SELF_SIZE(UDP)

   private:
    class Impl;

    BaseObjectWeakPtr<Impl> impl_;
    bool is_bound_ = false;
    bool is_started_ = false;
    bool is_closed_ = false;
  };

  bool is_closed() const;
  bool is_closing() const;
  bool is_listening() const;

  bool Start();

  // Destroy the endpoint if...
  // * There are no sessions,
  // * There are no sent packets with pending done callbacks, and
  // * We're not listening for new initial packets.
  void MaybeDestroy();

  // Specifies the general reason the endpoint is being destroyed.
  enum class CloseContext {
    CLOSE,
    BIND_FAILURE,
    START_FAILURE,
    RECEIVE_FAILURE,
    SEND_FAILURE,
    LISTEN_FAILURE,
  };

  void Destroy(CloseContext context = CloseContext::CLOSE, int status = 0);

  // A graceful close will destroy the endpoint once all existing sessions
  // have ended normally. Creating new sessions (inbound or outbound) will
  // be prevented.
  void CloseGracefully();

  void Release();

  void PacketDone(int status) override;

  void EmitNewSession(const BaseObjectPtr<Session>& session);
  void EmitClose(CloseContext context, int status);

  void IncrementSocketAddressCounter(const SocketAddress& address);
  void DecrementSocketAddressCounter(const SocketAddress& address);

  // JavaScript API

  // Create a new Endpoint.
  // @param Endpoint::Options options - Options to configure the Endpoint.
  static void New(const v8::FunctionCallbackInfo<v8::Value>& args);

  // Methods on the Endpoint instance:

  // Create a new client Session on this endpoint.
  // @param node::SocketAddress local_address - The local address to bind to.
  // @param Session::Options options - Options to configure the Session.
  // @param v8::ArrayBufferView session_ticket - The session ticket to use for
  // the Session.
  // @param v8::ArrayBufferView remote_transport_params - The remote transport
  // params.
  static void DoConnect(const v8::FunctionCallbackInfo<v8::Value>& args);

  // Start listening as a QUIC server
  // @param Session::Options options - Options to configure the Session.
  static void DoListen(const v8::FunctionCallbackInfo<v8::Value>& args);

  // Mark the Endpoint as busy, temporarily pausing handling of new initial
  // packets.
  // @param bool on - If true, mark the Endpoint as busy.
  static void MarkBusy(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void FastMarkBusy(v8::Local<v8::Object> receiver, bool on);

  // DoCloseGracefully is the signal that endpoint should close. Any packets
  // that are already in the queue or in flight will be allowed to finish, but
  // the EndpoingWrap will be otherwise no longer able to receive or send
  // packets.
  static void DoCloseGracefully(
      const v8::FunctionCallbackInfo<v8::Value>& args);

  // Get the local address of the Endpoint.
  // @return node::SocketAddress - The local address of the Endpoint.
  static void LocalAddress(const v8::FunctionCallbackInfo<v8::Value>& args);

  // Ref() causes a listening Endpoint to keep the event loop active.
  static void Ref(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void FastRef(v8::Local<v8::Object> receiver, bool on);

  void Receive(const uv_buf_t& buf, const SocketAddress& from);

  AliasedStruct<Stats> stats_;
  AliasedStruct<State> state_;
  const Options options_;
  UDP udp_;

  struct ServerState {
    Session::Options options;
    std::shared_ptr<TLSContext> tls_context;
  };
  // Set if/when the endpoint is configured to listen.
  std::optional<ServerState> server_state_ = std::nullopt;

  // A Session is generally identified by one or more CIDs. We use two
  // maps for this rather than one to avoid creating a whole bunch of
  // BaseObjectPtr references. The primary map (sessions_) just maps
  // the original CID to the Session, the second map (dcid_to_scid_)
  // maps the additional CIDs to the primary.
  CID::Map<BaseObjectPtr<Session>> sessions_;
  CID::Map<CID> dcid_to_scid_;
  StatelessResetToken::Map<Session*> token_map_;

  struct SocketAddressInfoTraits final {
    struct Type final {
      size_t active_connections;
      size_t reset_count;
      size_t retry_count;
      uint64_t timestamp;
      bool validated;
    };

    static bool CheckExpired(const SocketAddress& address, const Type& type);
    static void Touch(const SocketAddress& address, Type* type);
  };

  SocketAddressLRU<SocketAddressInfoTraits> addrLRU_;

  CloseContext close_context_ = CloseContext::CLOSE;
  int close_status_ = 0;

  friend class UDP;
  friend class Packet;
  friend class Session;
};

}  // namespace node::quic

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
