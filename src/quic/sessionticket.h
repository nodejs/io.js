#pragma once

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC

#include <crypto/crypto_common.h>
#include <env.h>
#include <memory_tracker.h>
#include <uv.h>
#include <v8.h>
#include "data.h"
#include "defs.h"

namespace node::quic {

// A TLS 1.3 Session resumption ticket. Encapsulates both the TLS
// ticket and the encoded QUIC transport parameters. The encoded
// structure should be considered to be opaque for end users.
// In JavaScript, the ticket will be represented as a Buffer
// instance with opaque data. To resume a session, the user code
// would pass that Buffer back into to client connection API.
class SessionTicket final : public MemoryRetainer {
 public:
  static v8::Maybe<SessionTicket> FromV8Value(Environment* env,
                                              v8::Local<v8::Value> value);

  SessionTicket() = default;
  SessionTicket(Store&& ticket, Store&& transport_params);

  const uv_buf_t ticket() const;

  const ngtcp2_vec transport_params() const;

  v8::MaybeLocal<v8::Object> encode(Environment* env) const;

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(SessionTicket)
  SET_SELF_SIZE(SessionTicket)

  class AppData;

  // The callback that OpenSSL will call when generating the session ticket
  // and it needs to collect additional application specific data.
  static int GenerateCallback(SSL* ssl, void* arg);

  // The callback that OpenSSL will call when consuming the session ticket
  // and it needs to pass embedded application data back into the app.
  static SSL_TICKET_RETURN DecryptedCallback(SSL* ssl,
                                             SSL_SESSION* session,
                                             const unsigned char* keyname,
                                             size_t keyname_len,
                                             SSL_TICKET_STATUS status,
                                             void* arg);

 private:
  Store ticket_;
  Store transport_params_;
};

// SessionTicket::AppData is a utility class that is used only during the
// generation or access of TLS stateless sesson tickets. It exists solely to
// provide a easier way for Session::Application instances to set relevant
// metadata in the session ticket when it is created, and the extract and
// subsequently verify that data when a ticket is received and is being
// validated. The app data is completely opaque to anything other than the
// server-side of the Session::Application that sets it.
class SessionTicket::AppData final {
 public:
  enum class Status {
    TICKET_IGNORE = SSL_TICKET_RETURN_IGNORE,
    TICKET_IGNORE_RENEW = SSL_TICKET_RETURN_IGNORE_RENEW,
    TICKET_USE = SSL_TICKET_RETURN_USE,
    TICKET_USE_RENEW = SSL_TICKET_RETURN_USE_RENEW,
  };

  explicit AppData(SSL* session);
  DISALLOW_COPY_AND_MOVE(AppData)

  bool Set(const uv_buf_t& data);
  std::optional<const uv_buf_t> Get() const;

  // A source of application data collected during the creation of the
  // session ticket. This interface will be implemented by the QUIC
  // Session.
  class Source {
   public:
    enum class Flag { STATUS_NONE, STATUS_RENEW };

    // Collect application data into the given AppData instance.
    virtual void CollectSessionTicketAppData(AppData* app_data) const = 0;

    // Extract application data from the given AppData instance.
    virtual Status ExtractSessionTicketAppData(
        const AppData& app_data, Flag flag = Flag::STATUS_NONE) = 0;
  };

  static void Collect(SSL* ssl);
  static Status Extract(SSL* ssl);

 private:
  bool set_ = false;
  SSL* ssl_;
};

}  // namespace node::quic

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
