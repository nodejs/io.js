#pragma once

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC

#include <memory_tracker.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <node_internals.h>
#include <node_sockaddr.h>
#include "cid.h"
#include "defs.h"

namespace node::quic {

// TokenSecrets are used to generate things like stateless reset tokens,
// retry tokens, and token packets. They are always QUIC_TOKENSECRET_LEN
// bytes in length.
//
// In the default case, token secrets will always be generated randomly.
// User code will be given the option to provide a secret directly
// however.
class TokenSecret final : public MemoryRetainer {
 public:
  static constexpr int QUIC_TOKENSECRET_LEN = 16;

  // Generate a random secret.
  TokenSecret();

  // Copy the given secret. The uint8_t* is assumed
  // to be QUIC_TOKENSECRET_LEN in length. Note that
  // the length is not verified so care must be taken
  // when this constructor is used.
  explicit TokenSecret(const uint8_t* secret);
  ~TokenSecret();

  TokenSecret(const TokenSecret&) = default;
  TokenSecret& operator=(const TokenSecret&) = default;
  DISALLOW_MOVE(TokenSecret)

  operator const uint8_t*() const;
  uint8_t operator[](int pos) const;

  std::string ToString() const;

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(TokenSecret)
  SET_SELF_SIZE(TokenSecret)

 private:
  operator const char*() const;
  uint8_t buf_[QUIC_TOKENSECRET_LEN];
};

// A stateless reset token is used when a QUIC endpoint receives a QUIC packet
// with a short header but the associated connection ID cannot be matched to any
// known Session. In such cases, the receiver may choose to send a subtle opaque
// indication to the sending peer that state for the Session has apparently been
// lost. For any on- or off- path attacker, a stateless reset packet resembles
// any other QUIC packet with a short header. In order to be successfully
// handled as a stateless reset, the peer must have already seen a reset token
// issued to it associated with the given CID. The token itself is opaque to the
// peer that receives is but must be possible to statelessly recreate by the
// peer that originally created it. The actual implementation is Node.js
// specific but we currently defer to a utility function provided by ngtcp2.
//
// QUIC leaves the generation of stateless session tokens up to the
// implementation to figure out. The idea, however, is that it ought to be
// possible to generate a stateless reset token reliably even when all state
// for a connection has been lost. We use the cid as it is the only reliably
// consistent bit of data we have when a session is destroyed.
//
// StatlessResetTokens are always kStatelessTokenLen bytes,
// as are the secrets used to generate the token.
class StatelessResetToken final : public MemoryRetainer {
 public:
  static constexpr int kStatelessTokenLen = NGTCP2_STATELESS_RESET_TOKENLEN;

  StatelessResetToken();

  // Generates a stateless reset token using HKDF with the cid and token secret
  // as input. The token secret is either provided by user code when an Endpoint
  // is created or is generated randomly.
  StatelessResetToken(const TokenSecret& secret, const CID& cid);

  // Generates a stateless reset token using the given token storage.
  // The StatelessResetToken wraps the token and does not take ownership.
  // The token storage must be at least kStatelessTokenLen bytes in length.
  // The length is not verified so care must be taken when using this
  // constructor.
  StatelessResetToken(uint8_t* token,
                      const TokenSecret& secret,
                      const CID& cid);

  // Wraps the given token. Does not take over ownership of the token storage.
  // The token must be at least kStatelessTokenLen bytes in length.
  // The length is not verified so care must be taken when using this
  // constructor.
  explicit StatelessResetToken(const uint8_t* token);

  StatelessResetToken(const StatelessResetToken& other);
  DISALLOW_MOVE(StatelessResetToken)

  std::string ToString() const;

  operator const uint8_t*() const;
  operator bool() const;

  bool operator==(const StatelessResetToken& other) const;
  bool operator!=(const StatelessResetToken& other) const;

  struct Hash {
    size_t operator()(const StatelessResetToken& token) const;
  };

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(StatelessResetToken)
  SET_SELF_SIZE(StatelessResetToken)

  template <typename T>
  using Map =
      std::unordered_map<StatelessResetToken, T, StatelessResetToken::Hash>;

  static StatelessResetToken kInvalid;

 private:
  operator const char*() const;

  const uint8_t* ptr_;
  uint8_t buf_[NGTCP2_STATELESS_RESET_TOKENLEN];
};

// A RETRY packet communicates a retry token to the client. Retry tokens are
// generated only by QUIC servers for the purpose of validating the network path
// between a client and server. The content payload of the RETRY packet is
// opaque to the clientand must not be guessable by on- or off-path attackers.
//
// A QUIC server sends a RETRY token as a way of initiating explicit path
// validation in response to an initial QUIC packet. The client, upon receiving
// a RETRY, must abandon the initial connection attempt and try again with the
// received retry token included with the new initial packet sent to the server.
// If the server is performing explicit validation, it will look for the
// presence of the retry token and attempt to validate it if found. The internal
// structure of the retry token must be meaningful to the server, and the server
// must be able to validate that the token is correct without relying on any
// state left over from the previous connection attempt. We use an
// implementation that is provided by ngtcp2.
//
// The token secret must be kept private on the QUIC server that generated the
// retry. When multiple QUIC servers are used in a cluster, it cannot be
// guaranteed that the same QUIC server instance will receive the subsequent new
// Initial packet. Therefore, all QUIC servers in the cluster should either
// share or be aware of the same token secret or a mechanism needs to be
// implemented to ensure that subsequent packets are routed to the same QUIC
// server instance.
class RetryToken final : public MemoryRetainer {
 public:
  // The token prefix that is used to differentiate between a retry token
  // and a regular token.
  static constexpr uint8_t kTokenMagic = NGTCP2_CRYPTO_TOKEN_MAGIC_RETRY;
  static constexpr int kRetryTokenLen = NGTCP2_CRYPTO_MAX_RETRY_TOKENLEN;

  static constexpr uint64_t QUIC_DEFAULT_RETRYTOKEN_EXPIRATION =
      10 * NGTCP2_SECONDS;
  static constexpr uint64_t QUIC_MIN_RETRYTOKEN_EXPIRATION = 1 * NGTCP2_SECONDS;

  // Generates a new retry token.
  RetryToken(uint32_t version,
             const SocketAddress& address,
             const CID& retry_cid,
             const CID& odcid,
             const TokenSecret& token_secret);

  // Wraps the given retry token
  RetryToken(const uint8_t* token, size_t length);

  // Validates the retry token given the input. If the token is valid,
  // the embedded original CID will be extracted from the token an
  // returned. If the token is invalid, std::nullopt will be returned.
  std::optional<CID> Validate(
      uint32_t version,
      const SocketAddress& address,
      const CID& cid,
      const TokenSecret& token_secret,
      uint64_t verification_expiration = QUIC_DEFAULT_RETRYTOKEN_EXPIRATION);

  operator const ngtcp2_vec&() const;
  operator const ngtcp2_vec*() const;
  operator bool() const;

  std::string ToString() const;

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(RetryToken)
  SET_SELF_SIZE(RetryToken)

 private:
  operator const char*() const;
  uint8_t buf_[kRetryTokenLen];
  const ngtcp2_vec ptr_;
};

// A NEW_TOKEN packet communicates a regular token to a client that the server
// would like the client to send in the header of an initial packet for a
// future connection. It is similar to RETRY and used for the same purpose,
// except a NEW_TOKEN is used in advance of the client establishing a new
// connection and a RETRY is sent in response to the client trying to open
// a new connection.
class RegularToken final : public MemoryRetainer {
 public:
  // The token prefix that is used to differentiate between a retry token
  // and a regular token.
  static constexpr uint8_t kTokenMagic = NGTCP2_CRYPTO_TOKEN_MAGIC_REGULAR;
  static constexpr int kRegularTokenLen = NGTCP2_CRYPTO_MAX_REGULAR_TOKENLEN;
  static constexpr uint64_t QUIC_DEFAULT_REGULARTOKEN_EXPIRATION =
      10 * NGTCP2_SECONDS;
  static constexpr uint64_t QUIC_MIN_REGULARTOKEN_EXPIRATION =
      1 * NGTCP2_SECONDS;

  RegularToken();

  // Generates a new retry token.
  RegularToken(uint32_t version,
               const SocketAddress& address,
               const TokenSecret& token_secret);

  // Wraps the given retry token
  RegularToken(const uint8_t* token, size_t length);

  // Validates the retry token given the input.
  bool Validate(
      uint32_t version,
      const SocketAddress& address,
      const TokenSecret& token_secret,
      uint64_t verification_expiration = QUIC_DEFAULT_REGULARTOKEN_EXPIRATION);

  operator const ngtcp2_vec&() const;
  operator const ngtcp2_vec*() const;

  operator bool() const;

  std::string ToString() const;

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(RetryToken)
  SET_SELF_SIZE(RetryToken)

 private:
  operator const char*() const;
  uint8_t buf_[kRegularTokenLen];
  const ngtcp2_vec ptr_;
};

}  // namespace node::quic

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
