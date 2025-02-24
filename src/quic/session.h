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
//
// The Session wraps an ngtcp2_conn that is initialized when the session object
// is created. This ngtcp2_conn is destroyed when the session object is freed.
// However, the session can be in a closed/destroyed state and still have a
// valid ngtcp2_conn pointer. This is important because the ngtcp2 still might
// be processsing data within the scope of an ngtcp2_conn after the session
// object itself is closed/destroyed by user code.
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

  // The ApplicationProvider optionally supplies the underlying application
  // protocol handler used by a session. The ApplicationProvider is supplied
  // in the *internal* options (that is, it is not exposed as a public, user
  // facing API. If the ApplicationProvider is not specified, then the
  // DefaultApplication is used (see application.cc).
  class ApplicationProvider : public BaseObject {
   public:
    using BaseObject::BaseObject;
    virtual std::unique_ptr<Application> Create(Session* session) = 0;
  };

  // The options used to configure a session. Most of these deal directly with
  // the transport parameters that are exchanged with the remote peer during
  // handshake.
  struct Options final : public MemoryRetainer {
    // The QUIC protocol version requested for the session.
    uint32_t version = NGTCP2_PROTO_VER_MAX;

    // Te minimum QUIC protocol version supported by this session.
    uint32_t min_version = NGTCP2_PROTO_VER_MIN;

    // By default a client session will ignore the preferred address
    // advertised by the the server. This option is only relevant for
    // client sessions.
    PreferredAddress::Policy preferred_address_strategy =
        PreferredAddress::Policy::IGNORE_PREFERRED;

    TransportParams::Options transport_params =
        TransportParams::Options::kDefault;
    TLSContext::Options tls_options = TLSContext::Options::kDefault;

    // A reference to the CID::Factory used to generate CID instances
    // for this session.
    const CID::Factory* cid_factory = &CID::Factory::random();
    // If the CID::Factory is a base object, we keep a reference to it
    // so that it cannot be garbage collected.
    BaseObjectPtr<BaseObject> cid_factory_ref;

    // If the application provider is specified, it will be used to create
    // the underlying Application instance for the session.
    BaseObjectPtr<ApplicationProvider> application_provider;

    // When true, QLog output will be enabled for the session.
    bool qlog = false;

    // The amount of time (in milliseconds) that the endpoint will wait for the
    // completion of the tls handshake.
    uint64_t handshake_timeout = UINT64_MAX;

    // Maximum initial flow control window size for a stream.
    uint64_t max_stream_window = 0;

    // Maximum initial flow control window size for the connection.
    uint64_t max_window = 0;

    // The max_payload_size is the maximum size of a serialized QUIC packet. It
    // should always be set small enough to fit within a single MTU without
    // fragmentation. The default is set by the QUIC specification at 1200. This
    // value should not be changed unless you know for sure that the entire path
    // supports a given MTU without fragmenting at any point in the path.
    uint64_t max_payload_size = kDefaultMaxPacketLength;

    // The unacknowledged_packet_threshold is the maximum number of
    // unacknowledged packets that an ngtcp2 session will accumulate before
    // sending an acknowledgement. Setting this to 0 uses the ngtcp2 defaults,
    // which is what most will want. The value can be changed to fine tune some
    // of the performance characteristics of the session. This should only be
    // changed if you have a really good reason for doing so.
    uint64_t unacknowledged_packet_threshold = 0;

    // There are several common congestion control algorithms that ngtcp2 uses
    // to determine how it manages the flow control window: RENO, CUBIC, and
    // BBR. The details of how each works is not relevant here. The choice of
    // which to use by default is arbitrary and we can choose whichever we'd
    // like. Additional performance profiling will be needed to determine which
    // is the better of the two for our needs.
    ngtcp2_cc_algo cc_algorithm = CC_ALGO_CUBIC;

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

    Config(Environment* env,
           Side side,
           const Options& options,
           uint32_t version,
           const SocketAddress& local_address,
           const SocketAddress& remote_address,
           const CID& dcid,
           const CID& scid,
           const CID& ocid = CID::kInvalid);

    Config(Environment* env,
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
  DISALLOW_COPY_AND_MOVE(Session)
  ~Session() override;

  bool is_destroyed() const;
  bool is_server() const;

  uint32_t version() const;
  Endpoint& endpoint() const;
  TLSSession& tls_session() const;
  Application& application() const;
  const Config& config() const;
  const Options& options() const;
  const SocketAddress& remote_address() const;
  const SocketAddress& local_address() const;

  std::string diagnostic_name() const override;

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(Session)
  SET_SELF_SIZE(Session)

  operator ngtcp2_conn*() const;

  // Ensures that the session/application sends pending data when the scope
  // exits. Scopes can be nested. When nested, pending data will be sent
  // only when the outermost scope is exited.
  struct SendPendingDataScope final {
    Session* session;
    explicit SendPendingDataScope(Session* session);
    explicit SendPendingDataScope(const BaseObjectPtr<Session>& session);
    ~SendPendingDataScope();
    DISALLOW_COPY_AND_MOVE(SendPendingDataScope)
  };

  struct State;
  struct Stats;

  void HandleQlog(uint32_t flags, const void* data, size_t len);

 private:
  struct Impl;

  using StreamsMap = std::unordered_map<int64_t, BaseObjectPtr<Stream>>;
  using QuicConnectionPointer = DeleteFnPtr<ngtcp2_conn, ngtcp2_conn_del>;

  struct PathValidationFlags final {
    bool preferredAddress = false;
  };

  struct DatagramReceivedFlags final {
    bool early = false;
  };

  bool Receive(Store&& store,
               const SocketAddress& local_address,
               const SocketAddress& remote_address);

  void Send(const BaseObjectPtr<Packet>& packet);
  void Send(const BaseObjectPtr<Packet>& packet, const PathStorage& path);
  uint64_t SendDatagram(Store&& data);

  // A non-const variation to allow certain modifications.
  Config& config();

  enum class CreateStreamOption {
    NOTIFY,
    DO_NOT_NOTIFY,
  };
  BaseObjectPtr<Stream> FindStream(int64_t id) const;
  BaseObjectPtr<Stream> CreateStream(
      int64_t id,
      CreateStreamOption option = CreateStreamOption::NOTIFY,
      std::shared_ptr<DataQueue> data_source = nullptr);
  void AddStream(BaseObjectPtr<Stream> stream,
                 CreateStreamOption option = CreateStreamOption::NOTIFY);
  void RemoveStream(int64_t id);
  void ResumeStream(int64_t id);
  void StreamDataBlocked(int64_t id);
  void ShutdownStream(int64_t id, QuicError error = QuicError());
  void ShutdownStreamWrite(int64_t id, QuicError code = QuicError());

  // Use the configured CID::Factory to generate a new CID.
  CID new_cid(size_t len = CID::kMaxLength) const;

  const TransportParams local_transport_params() const;
  const TransportParams remote_transport_params() const;

  bool is_destroyed_or_closing() const;
  size_t max_packet_size() const;
  void set_priority_supported(bool on = true);

  // Open a new locally-initialized stream with the specified directionality.
  // If the session is not yet in a state where the stream can be openen --
  // such as when the handshake is not yet sufficiently far along and ORTT
  // session resumption is not being used -- then the stream will be created
  // in a pending state where actually opening the stream will be deferred.
  v8::MaybeLocal<v8::Object> OpenStream(
      Direction direction, std::shared_ptr<DataQueue> data_source = nullptr);

  void ExtendStreamOffset(int64_t id, size_t amount);
  void ExtendOffset(size_t amount);
  void SetLastError(QuicError&& error);
  uint64_t max_data_left() const;

  PendingStream::PendingStreamQueue& pending_bidi_stream_queue() const;
  PendingStream::PendingStreamQueue& pending_uni_stream_queue() const;

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
  // new streams. Specifically, a stream is not in a state to create streams if
  // it has been destroyed or is closing.
  bool can_create_streams() const;

  // Returns false if the Session is currently in a state where it cannot open
  // a new locally-initiated stream. When using 0RTT session resumption, this
  // will become true immediately after the session ticket and transport params
  // have been configured. Otherwise, it becomes true after the remote transport
  // params and tx keys have been installed.
  bool can_open_streams() const;

  uint64_t max_local_streams_uni() const;
  uint64_t max_local_streams_bidi() const;

  bool wants_session_ticket() const;
  void SetStreamOpenAllowed();

  // It's a terrible name but "wrapped" here means that the Session has been
  // passed out to JavaScript and should be "wrapped" by whatever handler is
  // defined there to manage it.
  void set_wrapped();

  enum class CloseMethod {
    // Immediate close with a roundtrip through JavaScript, causing all
    // currently opened streams to be closed. An attempt will be made to
    // send a CONNECTION_CLOSE frame to the peer. If closing while within
    // the ngtcp2 callback scope, sending the CONNECTION_CLOSE will be
    // deferred until the scope exits.
    DEFAULT,
    // Same as DEFAULT except that no attempt to notify the peer will be
    // made.
    SILENT,
    // Closing gracefully disables the ability to open or accept new streams
    // for this Session. Existing streams are allowed to close naturally on
    // their own.
    // Once called, the Session will be immediately closed once there are no
    // remaining streams. No notification is given to the connected peer that
    // we are in a graceful closing state. A CONNECTION_CLOSE will be sent
    // only once FinishClose() is called.
    GRACEFUL
  };
  // Initiate closing of the session.
  void Close(CloseMethod method = CloseMethod::DEFAULT);

  void FinishClose();
  void Destroy();

  // Close the session and send a connection close packet to the peer.
  // If creating the packet fails the session will be silently closed.
  // The connection close packet will use the value of last_error_ as
  // the error code transmitted to the peer.
  void SendConnectionClose();
  void OnTimeout();

  void UpdateTimer();
  // Has to be called after certain operations that generate packets.
  void UpdatePacketTxTime();
  void UpdateDataStats();
  void UpdatePath(const PathStorage& path);

  void ProcessPendingBidiStreams();
  void ProcessPendingUniStreams();

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
  void EmitStream(const BaseObjectWeakPtr<Stream>& stream);
  void EmitVersionNegotiation(const ngtcp2_pkt_hd& hd,
                              const uint32_t* sv,
                              size_t nsv);
  void DatagramStatus(uint64_t datagramId, DatagramStatus status);
  void DatagramReceived(const uint8_t* data,
                        size_t datalen,
                        DatagramReceivedFlags flag);
  void GenerateNewConnectionId(ngtcp2_cid* cid, size_t len, uint8_t* token);
  bool HandshakeCompleted();
  void HandshakeConfirmed();
  void SelectPreferredAddress(PreferredAddress* preferredAddress);

  static std::unique_ptr<Application> SelectApplication(Session* session,
                                                        const Config& config);

  QuicConnectionPointer InitConnection();

  Side side_;
  ngtcp2_mem allocator_;
  std::unique_ptr<Impl> impl_;
  QuicConnectionPointer connection_;
  std::unique_ptr<TLSSession> tls_session_;
  BaseObjectPtr<LogStream> qlog_stream_;
  BaseObjectPtr<LogStream> keylog_stream_;

  friend class Application;
  friend class DefaultApplication;
  friend class Http3ApplicationImpl;
  friend class Endpoint;
  friend class Stream;
  friend class PendingStream;
  friend class TLSContext;
  friend class TLSSession;
  friend class TransportParams;
  friend struct Impl;
  friend struct SendPendingDataScope;
};

}  // namespace node::quic

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
