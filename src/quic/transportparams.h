#pragma once

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC

#include <env.h>
#include <memory_tracker.h>
#include <ngtcp2/ngtcp2.h>
#include <node_sockaddr.h>
#include <optional>
#include "bindingdata.h"
#include "cid.h"
#include "data.h"
#include "tokens.h"

namespace node::quic {

class Endpoint;
class Session;

// The Transport Params are the set of configuration options that are sent to
// the remote peer. They communicate the protocol options the other peer
// should use when communicating with this session.
class TransportParams final {
 public:
  static void Initialize(Environment* env, v8::Local<v8::Object> target);

  static constexpr int QUIC_TRANSPORT_PARAMS_V1 = NGTCP2_TRANSPORT_PARAMS_V1;
  static constexpr int QUIC_TRANSPORT_PARAMS_VERSION =
      NGTCP2_TRANSPORT_PARAMS_VERSION;
  static constexpr uint64_t DEFAULT_MAX_STREAM_DATA = 256 * 1024;
  static constexpr uint64_t DEFAULT_MAX_DATA = 1 * 1024 * 1024;
  static constexpr uint64_t DEFAULT_MAX_IDLE_TIMEOUT = 10;  // seconds
  static constexpr uint64_t DEFAULT_MAX_STREAMS_BIDI = 100;
  static constexpr uint64_t DEFAULT_MAX_STREAMS_UNI = 3;
  static constexpr uint64_t DEFAULT_ACTIVE_CONNECTION_ID_LIMIT = 2;

  struct Config {
    Side side;
    const CID& ocid;
    const CID& retry_scid;
    Config(Side side,
           const CID& ocid = CID::kInvalid,
           const CID& retry_scid = CID::kInvalid);
  };

  struct Options : public MemoryRetainer {
    int transportParamsVersion = QUIC_TRANSPORT_PARAMS_V1;

    // Set only on server Sessions, the preferred address communicates the IP
    // address and port that the server would prefer the client to use when
    // communicating with it. See the QUIC specification for more detail on how
    // the preferred address mechanism works.
    std::optional<SocketAddress> preferred_address_ipv4{};
    std::optional<SocketAddress> preferred_address_ipv6{};

    // The initial size of the flow control window of locally initiated streams.
    // This is the maximum number of bytes that the *remote* endpoint can send
    // when the connection is started.
    uint64_t initial_max_stream_data_bidi_local = DEFAULT_MAX_STREAM_DATA;

    // The initial size of the flow control window of remotely initiated
    // streams. This is the maximum number of bytes that the remote endpoint can
    // send when the connection is started.
    uint64_t initial_max_stream_data_bidi_remote = DEFAULT_MAX_STREAM_DATA;

    // The initial size of the flow control window of remotely initiated
    // unidirectional streams. This is the maximum number of bytes that the
    // remote endpoint can send when the connection is started.
    uint64_t initial_max_stream_data_uni = DEFAULT_MAX_STREAM_DATA;

    // The initial size of the session-level flow control window.
    uint64_t initial_max_data = DEFAULT_MAX_DATA;

    // The initial maximum number of concurrent bidirectional streams the remote
    // endpoint is permitted to open.
    uint64_t initial_max_streams_bidi = DEFAULT_MAX_STREAMS_BIDI;

    // The initial maximum number of concurrent unidirectional streams the
    // remote endpoint is permitted to open.
    uint64_t initial_max_streams_uni = DEFAULT_MAX_STREAMS_UNI;

    // The maximum amount of time that a Session is permitted to remain idle
    // before it is silently closed and state is discarded.
    uint64_t max_idle_timeout = DEFAULT_MAX_IDLE_TIMEOUT;

    // The maximum number of Connection IDs that the peer can store. A single
    // Session may have several connection IDs over it's lifetime.
    uint64_t active_connection_id_limit = DEFAULT_ACTIVE_CONNECTION_ID_LIMIT;

    // Establishes the exponent used in ACK Delay field in the ACK frame. See
    // the QUIC specification for details. This is an advanced option that
    // should rarely be modified and only if there is really good reason.
    uint64_t ack_delay_exponent = NGTCP2_DEFAULT_ACK_DELAY_EXPONENT;

    // The maximum amount of time by which the endpoint will delay sending
    // acknowledgements. This is an advanced option that should rarely be
    // modified and only if there is a really good reason. It is used to
    // determine how long a Session will wait to determine that a packet has
    // been lost.
    uint64_t max_ack_delay = NGTCP2_DEFAULT_MAX_ACK_DELAY;

    // The maximum size of DATAGRAM frames that the endpoint will accept.
    // Setting the value to 0 will disable DATAGRAM support.
    uint64_t max_datagram_frame_size = kDefaultMaxPacketLength;

    // When true, communicates that the Session does not support active
    // connection migration. See the QUIC specification for more details on
    // connection migration.
    // TODO(@jasnell): We currently do not implementation active migration.
    bool disable_active_migration = true;

    static const Options kDefault;

    void MemoryInfo(MemoryTracker* tracker) const override;
    SET_MEMORY_INFO_NAME(TransportParams::Options)
    SET_SELF_SIZE(Options)

    static v8::Maybe<Options> From(Environment* env,
                                   v8::Local<v8::Value> value);

    std::string ToString() const;
  };

  explicit TransportParams();

  // Creates an instance of TransportParams wrapping the existing const
  // ngtcp2_transport_params pointer.
  TransportParams(const ngtcp2_transport_params* ptr);

  TransportParams(const Config& config, const Options& options);

  // Creates an instance of TransportParams by decoding the given buffer.
  // If the parameters cannot be successfully decoded, the error()
  // property will be set with an appropriate QuicError and the bool()
  // operator will return false.
  TransportParams(const ngtcp2_vec& buf,
                  int version = QUIC_TRANSPORT_PARAMS_V1);

  void GenerateSessionTokens(Session* session);
  void GenerateStatelessResetToken(const Endpoint& endpoint, const CID& cid);
  void GeneratePreferredAddressToken(Session* session);
  void SetPreferredAddress(const SocketAddress& address);

  operator const ngtcp2_transport_params&() const;
  operator const ngtcp2_transport_params*() const;

  operator bool() const;

  const QuicError& error() const;

  // Returns an ArrayBuffer containing the encoded transport parameters.
  // If an error occurs during encoding, an empty shared_ptr will be returned
  // and the error() property will be set to an appropriate QuicError.
  Store Encode(Environment* env, int version = QUIC_TRANSPORT_PARAMS_V1) const;

 private:
  ngtcp2_transport_params params_{};
  const ngtcp2_transport_params* ptr_;
  QuicError error_ = QuicError::TRANSPORT_NO_ERROR;
};

}  // namespace node::quic

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
