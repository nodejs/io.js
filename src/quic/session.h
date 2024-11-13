#pragma once

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC

#include <async_wrap.h>
#include <base_object.h>
#include <env.h>
#include <memory_tracker.h>
#include <ngtcp2/ngtcp2.h>
#include <node_http_common.h>
#include <node_sockaddr.h>
#include <timer_wrap.h>
#include <util.h>
#include <optional>
#include "bindingdata.h"
#include "cid.h"
#include "data.h"
#include "defs.h"
#include "logstream.h"
#include "packet.h"
#include "preferredaddress.h"
#include "sessionticket.h"
#include "streams.h"
#include "tlscontext.h"
#include "transportparams.h"

namespace node::quic {

class Endpoint;

// A Session represents one half of a persistent connection between two QUIC
// peers. Every Session is established first by performing a TLS handshake in
// which the client sends an initial packet to the server containing a TLS
// client hello. Once the TLS handshake has been completed, the Session can be
// used to open one or more Streams for the actual data flow back and forth.
//
// While client and server Sessions are created in slightly different ways,
// their lifecycles are generally identical:
//
// A Session is either acting as a Client or as a Server.
//
// Client Sessions are always created using Endpoint::Connect()
//
// Server Sessions are always created by an Endpoint receiving a valid initial
// request received from a remote client.
//
// As soon as Sessions of either type are created, they will immediately start
// working through the TLS handshake to establish the crypographic keys used to
// secure the communication. Once those keys are established, the Session can be
// used to open Streams. Based on how the Session is configured, any number of
// Streams can exist concurrently on a single Session.
class Session final : public AsyncWrap, private SessionTicket::AppData::Source {
 public:
  // For simplicity, we use the same Application::Options struct for all
  // Application types. This may change in the future. Not all of the options
  // are going to be relevant for all Application types.
  struct Application_Options final : public MemoryRetainer {
    // The maximum number of header pairs permitted for a Stream.
    // Only relevant if the selected application supports headers.
    uint64_t max_header_pairs = DEFAULT_MAX_HEADER_LIST_PAIRS;

    // The maximum total number of header bytes (including header
    // name and value) permitted for a Stream.
    // Only relevant if the selected application supports headers.
    uint64_t max_header_length = DEFAULT_MAX_HEADER_LENGTH;

    // HTTP/3 specific options.
    uint64_t max_field_section_size = 0;
    uint64_t qpack_max_dtable_capacity = 0;
    uint64_t qpack_encoder_max_dtable_capacity = 0;
    uint64_t qpack_blocked_streams = 0;

    bool enable_connect_protocol = true;
    bool enable_datagrams = true;

    operator const nghttp3_settings() const;

    SET_NO_MEMORY_INFO()
    SET_MEMORY_INFO_NAME(Application::Options)
    SET_SELF_SIZE(Options)

    static v8::Maybe<Application_Options> From(Environment* env,
                                               v8::Local<v8::Value> value);

    std::string ToString() const;

    static const Application_Options kDefault;
  };

  // An Application implements the ALPN-protocol specific semantics on behalf
  // of a QUIC Session.
  class Application;

  // The options used to configure a session. Most of these deal directly with
  // the transport parameters that are exchanged with the remote peer during
  // handshake.
  struct Options final : public MemoryRetainer {
    // The QUIC protocol version requested for the session.
    uint32_t version = NGTCP2_PROTO_VER_MAX;

    // Te minimum QUIC protocol version supported by this session.
    uint32_t min_version = NGTCP2_PROTO_VER_MIN;

    // By default a client session will use the preferred address advertised by
    // the the server. This option is only relevant for client sessions.
    PreferredAddress::Policy preferred_address_strategy =
        PreferredAddress::Policy::USE_PREFERRED;

    TransportParams::Options transport_params =
        TransportParams::Options::kDefault;
    TLSContext::Options tls_options = TLSContext::Options::kDefault;
    Application_Options application_options = Application_Options::kDefault;

    // A reference to the CID::Factory used to generate CID instances
    // for this session.
    const CID::Factory* cid_factory = &CID::Factory::random();
    // If the CID::Factory is a base object, we keep a reference to it
    // so that it cannot be garbage collected.
    BaseObjectPtr<BaseObject> cid_factory_ref = BaseObjectPtr<BaseObject>();

    // When true, QLog output will be enabled for the session.
    bool qlog = false;

    void MemoryInfo(MemoryTracker* tracker) const override;
    SET_MEMORY_INFO_NAME(Session::Options)
    SET_SELF_SIZE(Options)

    static v8::Maybe<Options> From(Environment* env,
                                   v8::Local<v8::Value> value);

    std::string ToString() const;
  };

  // The additional configuration settings used to create a specific session.
  // while the Options above can be used to configure multiple sessions, a
  // single Config is used to create a single session, which is why they are
  // kept separate.
  struct Config final : MemoryRetainer {
    // Is the Session acting as a client or a server?
    Side side;

    // The options to use for this session.
    Options options;

    // The actual QUIC version identified for this session.
    uint32_t version;

    SocketAddress local_address;
    SocketAddress remote_address;

    // The destination CID, identifying the remote peer. This value is always
    // provided by the remote peer.
    CID dcid = CID::kInvalid;

    // The source CID, identifying this session. This value is always created
    // locally.
    CID scid = CID::kInvalid;

    // Used only by client sessions to identify the original DCID
    // used to initiate the connection.
    CID ocid = CID::kInvalid;
    CID retry_scid = CID::kInvalid;
    CID preferred_address_cid = CID::kInvalid;

    ngtcp2_settings settings = {};
    operator ngtcp2_settings*() { return &settings; }
    operator const ngtcp2_settings*() const { return &settings; }

    Config(Side side,
           const Endpoint& endpoint,
           const Options& options,
           uint32_t version,
           const SocketAddress& local_address,
           const SocketAddress& remote_address,
           const CID& dcid,
           const CID& scid,
           const CID& ocid = CID::kInvalid);

    Config(const Endpoint& endpoint,
           const Options& options,
           const SocketAddress& local_address,
           const SocketAddress& remote_address,
           const CID& ocid = CID::kInvalid);

    void set_token(const uint8_t* token,
                   size_t len,
                   ngtcp2_token_type type = NGTCP2_TOKEN_TYPE_UNKNOWN);
    void set_token(const RetryToken& token);
    void set_token(const RegularToken& token);

    void MemoryInfo(MemoryTracker* tracker) const override;
    SET_MEMORY_INFO_NAME(Session::Config)
    SET_SELF_SIZE(Config)

    std::string ToString() const;
  };

  static bool HasInstance(Environment* env, v8::Local<v8::Value> value);
  static v8::Local<v8::FunctionTemplate> GetConstructorTemplate(
      Environment* env);
  static void InitPerIsolate(IsolateData* isolate_data,
                             v8::Local<v8::ObjectTemplate> target);
  static void InitPerContext(Realm* env, v8::Local<v8::Object> target);
  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);

  static BaseObjectPtr<Session> Create(
      Endpoint* endpoint,
      const Config& config,
      TLSContext* tls_context,
      const std::optional<SessionTicket>& ticket);

  // Really should be private but MakeDetachedBaseObject needs visibility.
  Session(Endpoint* endpoint,
          v8::Local<v8::Object> object,
          const Config& config,
          TLSContext* tls_context,
          const std::optional<SessionTicket>& ticket);
  ~Session() override;

  uint32_t version() const;
  Endpoint& endpoint() const;
  TLSSession& tls_session();
  Application& application();
  const Config& config() const;
  const Options& options() const;
  const SocketAddress& remote_address() const;
  const SocketAddress& local_address() const;

  bool is_closing() const;
  bool is_graceful_closing() const;
  bool is_silent_closing() const;
  bool is_destroyed() const;
  bool is_server() const;

  size_t max_packet_size() const;

  void set_priority_supported(bool on = true);

  std::string diagnostic_name() const override;

  // Use the configured CID::Factory to generate a new CID.
  CID new_cid(size_t len = CID::kMaxLength) const;

  void HandleQlog(uint32_t flags, const void* data, size_t len);

  TransportParams GetLocalTransportParams() const;
  TransportParams GetRemoteTransportParams() const;
  void UpdatePacketTxTime();

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(Session)
  SET_SELF_SIZE(Session)

  struct State;
  struct Stats;

  operator ngtcp2_conn*() const;

  BaseObjectPtr<Stream> FindStream(int64_t id) const;
  BaseObjectPtr<Stream> CreateStream(int64_t id);
  BaseObjectPtr<Stream> OpenStream(Direction direction);
  void ExtendStreamOffset(int64_t id, size_t amount);
  void ExtendOffset(size_t amount);
  void SetLastError(QuicError&& error);
  uint64_t max_data_left() const;

  enum class CloseMethod {
    // Roundtrip through JavaScript, causing all currently opened streams
    // to be closed. An attempt will be made to send a CONNECTION_CLOSE
    // frame to the peer. If closing while within the ngtcp2 callback scope,
    // sending the CONNECTION_CLOSE will be deferred until the scope exits.
    DEFAULT,
    // The connected peer will not be notified.
    SILENT,
    // Closing gracefully disables the ability to open or accept new streams for
    // this Session. Existing streams are allowed to close naturally on their
    // own.
    // Once called, the Session will be immediately closed once there are no
    // remaining streams. No notification is given to the connected peer that we
    // are in a graceful closing state. A CONNECTION_CLOSE will be sent only
    // once
    // Close() is called.
    GRACEFUL
  };
  void Close(CloseMethod method = CloseMethod::DEFAULT);

  struct SendPendingDataScope {
    Session* session;
    explicit SendPendingDataScope(Session* session);
    explicit SendPendingDataScope(const BaseObjectPtr<Session>& session);
    DISALLOW_COPY_AND_MOVE(SendPendingDataScope)
    ~SendPendingDataScope();
  };

 private:
  struct Impl;
  struct MaybeCloseConnectionScope;

  using StreamsMap = std::unordered_map<int64_t, BaseObjectPtr<Stream>>;
  using QuicConnectionPointer = DeleteFnPtr<ngtcp2_conn, ngtcp2_conn_del>;

  struct PathValidationFlags {
    bool preferredAddress = false;
  };

  struct DatagramReceivedFlags {
    bool early = false;
  };

  void Destroy();

  bool Receive(Store&& store,
               const SocketAddress& local_address,
               const SocketAddress& remote_address);

  void Send(Packet* packet);
  void Send(Packet* packet, const PathStorage& path);
  uint64_t SendDatagram(Store&& data);

  void AddStream(const BaseObjectPtr<Stream>& stream);
  void RemoveStream(int64_t id);
  void ResumeStream(int64_t id);
  void ShutdownStream(int64_t id, QuicError error);
  void StreamDataBlocked(int64_t id);
  void ShutdownStreamWrite(int64_t id, QuicError code = QuicError());

  // Implementation of SessionTicket::AppData::Source
  void CollectSessionTicketAppData(
      SessionTicket::AppData* app_data) const override;
  SessionTicket::AppData::Status ExtractSessionTicketAppData(
      const SessionTicket::AppData& app_data,
      SessionTicket::AppData::Source::Flag flag) override;

  // Returns true if the Session has entered the closing period after sending a
  // CONNECTION_CLOSE. While true, the Session is only permitted to transmit
  // CONNECTION_CLOSE frames until either the idle timeout period elapses or
  // until the Session is explicitly destroyed.
  bool is_in_closing_period() const;

  // Returns true if the Session has received a CONNECTION_CLOSE frame from the
  // peer. Once in the draining period, the Session is not permitted to send any
  // frames to the peer. The Session will be silently closed after either the
  // idle timeout period elapses or until the Session is explicitly destroyed.
  bool is_in_draining_period() const;

  // Returns false if the Session is currently in a state where it is unable to
  // transmit any packets.
  bool can_send_packets() const;

  // Returns false if the Session is currently in a state where it cannot create
  // new streams.
  bool can_create_streams() const;
  uint64_t max_local_streams_uni() const;
  uint64_t max_local_streams_bidi() const;

  bool wants_session_ticket() const;
  void SetStreamOpenAllowed();

  // It's a terrible name but "wrapped" here means that the Session has been
  // passed out to JavaScript and should be "wrapped" by whatever handler is
  // defined there to manage it.
  void set_wrapped();

  void DoClose(bool silent = false);
  void UpdateDataStats();
  void SendConnectionClose();
  void OnTimeout();
  void UpdateTimer();
  bool StartClosingPeriod();

  // JavaScript callouts

  void EmitClose(const QuicError& error = QuicError());
  void EmitDatagram(Store&& datagram, DatagramReceivedFlags flag);
  void EmitDatagramStatus(uint64_t id, DatagramStatus status);
  void EmitHandshakeComplete();
  void EmitKeylog(const char* line);

  struct ValidatedPath {
    std::shared_ptr<SocketAddress> local;
    std::shared_ptr<SocketAddress> remote;
  };

  void EmitPathValidation(PathValidationResult result,
                          PathValidationFlags flags,
                          const ValidatedPath& newPath,
                          const std::optional<ValidatedPath>& oldPath);
  void EmitSessionTicket(Store&& ticket);
  void EmitStream(BaseObjectPtr<Stream> stream);
  void EmitVersionNegotiation(const ngtcp2_pkt_hd& hd,
                              const uint32_t* sv,
                              size_t nsv);

  void DatagramStatus(uint64_t datagramId, DatagramStatus status);
  void DatagramReceived(const uint8_t* data,
                        size_t datalen,
                        DatagramReceivedFlags flag);
  bool GenerateNewConnectionId(ngtcp2_cid* cid, size_t len, uint8_t* token);
  bool HandshakeCompleted();
  void HandshakeConfirmed();
  void SelectPreferredAddress(PreferredAddress* preferredAddress);
  void UpdatePath(const PathStorage& path);

  QuicConnectionPointer InitConnection();

  std::unique_ptr<Application> select_application();

  AliasedStruct<Stats> stats_;
  AliasedStruct<State> state_;
  ngtcp2_mem allocator_;
  BaseObjectWeakPtr<Endpoint> endpoint_;
  Config config_;
  SocketAddress local_address_;
  SocketAddress remote_address_;
  QuicConnectionPointer connection_;
  std::unique_ptr<TLSSession> tls_session_;
  std::unique_ptr<Application> application_;
  StreamsMap streams_;
  TimerWrapHandle timer_;
  size_t send_scope_depth_ = 0;
  size_t connection_close_depth_ = 0;
  QuicError last_error_;
  Packet* conn_closebuf_;
  BaseObjectPtr<LogStream> qlog_stream_;
  BaseObjectPtr<LogStream> keylog_stream_;

  friend class Application;
  friend class DefaultApplication;
  friend class Endpoint;
  friend struct Impl;
  friend struct MaybeCloseConnectionScope;
  friend struct SendPendingDataScope;
  friend class Stream;
  friend class TLSContext;
  friend class TLSSession;
  friend class TransportParams;
};

}  // namespace node::quic

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
