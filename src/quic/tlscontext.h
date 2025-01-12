#pragma once

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC

#include <base_object.h>
#include <crypto/crypto_context.h>
#include <crypto/crypto_keys.h>
#include <memory_tracker.h>
#include <ncrypto.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include "bindingdata.h"
#include "data.h"
#include "defs.h"
#include "sessionticket.h"

namespace node::quic {

class Session;
class TLSContext;

// Every QUIC Session has exactly one TLSSession that maintains the state
// of the TLS handshake and negotiated keys after the handshake has been
// completed. It is separated out from the main Session class only as a
// convenience to help make the code more maintainable and understandable.
// A TLSSession is created from a TLSContext and maintains a reference to
// the context.
class TLSSession final : public MemoryRetainer {
 public:
  static const TLSSession& From(const SSL* ssl);

  // The constructor is public in order to satisfy the call to std::make_unique
  // in TLSContext::NewSession. It should not be called directly.
  TLSSession(Session* session,
             std::shared_ptr<TLSContext> context,
             const std::optional<SessionTicket>& maybeSessionTicket);
  DISALLOW_COPY_AND_MOVE(TLSSession)
  ~TLSSession();

  inline operator bool() const { return ssl_ != nullptr; }
  inline Session& session() const { return *session_; }
  inline TLSContext& context() const { return *context_; }

  // Returns true if the handshake has been completed and early data was
  // accepted by the TLS session. This will assert if the handshake has
  // not been completed.
  bool early_data_was_accepted() const;

  v8::MaybeLocal<v8::Object> cert(Environment* env) const;
  v8::MaybeLocal<v8::Object> peer_cert(Environment* env) const;
  v8::MaybeLocal<v8::Object> ephemeral_key(Environment* env) const;
  v8::MaybeLocal<v8::Value> cipher_name(Environment* env) const;
  v8::MaybeLocal<v8::Value> cipher_version(Environment* env) const;

  // The SNI (server name) negotiated for the session
  const std::string_view servername() const;

  // The ALPN (protocol name) negotiated for the session
  const std::string_view protocol() const;

  // Triggers key update to begin. This will fail and return false if either a
  // previous key update is in progress or if the initial handshake has not yet
  // been confirmed.
  bool InitiateKeyUpdate();

  struct PeerIdentityValidationError {
    v8::MaybeLocal<v8::Value> reason;
    v8::MaybeLocal<v8::Value> code;
  };

  // Checks the peer identity against the configured CA and CRL. If the peer
  // certificate is valid, std::nullopt is returned. Otherwise a
  // PeerIdentityValidationError is returned with the reason and code for the
  // failure.
  std::optional<PeerIdentityValidationError> VerifyPeerIdentity(
      Environment* env);

  inline const std::string_view validation_error() const {
    return validation_error_;
  }

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(TLSSession)
  SET_SELF_SIZE(TLSSession)

 private:
  operator SSL*() const;
  ncrypto::SSLPointer Initialize(
      const std::optional<SessionTicket>& maybeSessionTicket);

  static ngtcp2_conn* connection(ngtcp2_crypto_conn_ref* ref);

  ngtcp2_crypto_conn_ref ref_;
  std::shared_ptr<TLSContext> context_;
  Session* session_;
  ncrypto::SSLPointer ssl_;
  ncrypto::BIOPointer bio_trace_;
  std::string validation_error_ = "";
  bool in_key_update_ = false;
};

// The TLSContext is used to create a TLSSession. For the client, there is
// typically only a single TLSContext for each TLSSession. For the server,
// there is a single TLSContext for the server and a TLSSession for every
// QUIC session created by that server.
class TLSContext final : public MemoryRetainer,
                         public std::enable_shared_from_this<TLSContext> {
 public:
  static constexpr auto DEFAULT_CIPHERS = "TLS_AES_128_GCM_SHA256:"
                                          "TLS_AES_256_GCM_SHA384:"
                                          "TLS_CHACHA20_POLY1305_"
                                          "SHA256:TLS_AES_128_CCM_SHA256";
  static constexpr auto DEFAULT_GROUPS = "X25519:P-256:P-384:P-521";

  struct Options final : public MemoryRetainer {
    // The SNI servername to use for this session. This option is only used by
    // the client.
    std::string servername = "localhost";

    // The ALPN (protocol name) to use for this session. This option is only
    // used by the client.
    std::string protocol = NGHTTP3_ALPN_H3;

    // The list of TLS ciphers to use for this session.
    std::string ciphers = DEFAULT_CIPHERS;

    // The list of TLS groups to use for this session.
    std::string groups = DEFAULT_GROUPS;

    // When true, enables keylog output for the session.
    bool keylog = false;

    // When true, the peer certificate is verified against the list of supplied
    // CA. If verification fails, the connection will be refused. When set,
    // instructs the server session to request a client auth certificate. This
    // option is only used by the server side.
    bool verify_client = false;

    // When true, enables TLS tracing for the session. This should only be used
    // for debugging.
    // JavaScript option name "tlsTrace".
    bool enable_tls_trace = false;

    // When true, causes the private key passed in for the session to be
    // verified.
    // JavaScript option name "verifyPrivateKey"
    bool verify_private_key = false;

    // The TLS private key(s) to use for this session.
    // JavaScript option name "keys"
    std::vector<crypto::KeyObjectData> keys;

    // Collection of certificates to use for this session.
    // JavaScript option name "certs"
    std::vector<Store> certs;

    // Optional certificate authority overrides to use.
    // JavaScript option name "ca"
    std::vector<Store> ca;

    // Optional certificate revocation lists to use.
    // JavaScript option name "crl"
    std::vector<Store> crl;

    void MemoryInfo(MemoryTracker* tracker) const override;
    SET_MEMORY_INFO_NAME(TLSContext::Options)
    SET_SELF_SIZE(Options)

    // The default TLS configuration.
    static const Options kDefault;

    static v8::Maybe<Options> From(Environment* env,
                                   v8::Local<v8::Value> value);

    std::string ToString() const;
  };

  static std::shared_ptr<TLSContext> CreateClient(const Options& options);
  static std::shared_ptr<TLSContext> CreateServer(const Options& options);

  TLSContext(Side side, const Options& options);
  DISALLOW_COPY_AND_MOVE(TLSContext)

  // Each QUIC Session has exactly one TLSSession. Each TLSSession maintains
  // a reference to the TLSContext used to create it.
  std::unique_ptr<TLSSession> NewSession(
      Session* session, const std::optional<SessionTicket>& maybeSessionTicket);

  inline Side side() const { return side_; }
  inline const Options& options() const { return options_; }
  inline operator bool() const { return ctx_ != nullptr; }

  inline const std::string_view validation_error() const {
    return validation_error_;
  }

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(TLSContext)
  SET_SELF_SIZE(TLSContext)

 private:
  ncrypto::SSLCtxPointer Initialize();
  operator SSL_CTX*() const;

  static void OnKeylog(const SSL* ssl, const char* line);
  static int OnNewSession(SSL* ssl, SSL_SESSION* session);
  static int OnSelectAlpn(SSL* ssl,
                          const unsigned char** out,
                          unsigned char* outlen,
                          const unsigned char* in,
                          unsigned int inlen,
                          void* arg);
  static int OnVerifyClientCertificate(int preverify_ok, X509_STORE_CTX* ctx);

  Side side_;
  Options options_;
  ncrypto::X509Pointer cert_;
  ncrypto::X509Pointer issuer_;
  ncrypto::SSLCtxPointer ctx_;
  std::string validation_error_ = "";

  friend class TLSSession;
};

}  // namespace node::quic

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
