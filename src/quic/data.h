#pragma once

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC

#include <env.h>
#include <memory_tracker.h>
#include <nghttp3/nghttp3.h>
#include <ngtcp2/ngtcp2.h>
#include <node_internals.h>
#include <node_sockaddr.h>
#include <v8.h>
#include <string>
#include "defs.h"

namespace node::quic {

struct Path final : public ngtcp2_path {
  Path(const SocketAddress& local, const SocketAddress& remote);
  inline operator ngtcp2_path*() { return this; }
  std::string ToString() const;
};

struct PathStorage final : public ngtcp2_path_storage {
  PathStorage();
  operator ngtcp2_path();

  void Reset();
  void CopyTo(PathStorage* path) const;

  bool operator==(const PathStorage& other) const;
  bool operator!=(const PathStorage& other) const;
};

class Store final : public MemoryRetainer {
 public:
  Store() = default;

  Store(std::shared_ptr<v8::BackingStore> store,
        size_t length,
        size_t offset = 0);
  Store(std::unique_ptr<v8::BackingStore> store,
        size_t length,
        size_t offset = 0);

  enum class Option {
    NONE,
    DETACH,
  };

  Store(v8::Local<v8::ArrayBuffer> buffer, Option option = Option::NONE);
  Store(v8::Local<v8::ArrayBufferView> view, Option option = Option::NONE);

  v8::Local<v8::Uint8Array> ToUint8Array(Environment* env) const;

  operator uv_buf_t() const;
  operator ngtcp2_vec() const;
  operator nghttp3_vec() const;
  operator bool() const;
  size_t length() const;

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(Store)
  SET_SELF_SIZE(Store)

 private:
  template <typename T, typename t>
  T convert() const;
  std::shared_ptr<v8::BackingStore> store_;
  size_t length_ = 0;
  size_t offset_ = 0;
};

class QuicError final : public MemoryRetainer {
 public:
  static constexpr error_code QUIC_NO_ERROR = NGTCP2_NO_ERROR;
  static constexpr error_code QUIC_APP_NO_ERROR = 65280;

  enum class Type {
    TRANSPORT = NGTCP2_CCERR_TYPE_TRANSPORT,
    APPLICATION = NGTCP2_CCERR_TYPE_APPLICATION,
    VERSION_NEGOTIATION = NGTCP2_CCERR_TYPE_VERSION_NEGOTIATION,
    IDLE_CLOSE = NGTCP2_CCERR_TYPE_IDLE_CLOSE,
  };

  explicit QuicError(const std::string& reason = "");
  explicit QuicError(const ngtcp2_ccerr* ptr);
  explicit QuicError(const ngtcp2_ccerr& error);

  Type type() const;
  error_code code() const;
  const std::string_view reason() const;
  uint64_t frame_type() const;

  operator const ngtcp2_ccerr&() const;
  operator const ngtcp2_ccerr*() const;

  // Returns false if the QuicError uses a no_error code with type
  // transport or application.
  operator bool() const;

  bool is_crypto() const;
  std::optional<int> crypto_error() const;

  bool operator==(const QuicError& other) const;
  bool operator!=(const QuicError& other) const;

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(QuicError)
  SET_SELF_SIZE(QuicError)

  std::string ToString() const;
  v8::MaybeLocal<v8::Value> ToV8Value(Environment* env) const;

  static std::string reason_for_liberr(int liberr);
  static std::string reason_for_h3_liberr(int liberr);
  static bool is_fatal_liberror(int liberr);
  static bool is_fatal_h3_liberror(int liberr);
  static error_code liberr_to_code(int liberr);
  static error_code h3_liberr_to_code(int liberr);

  static QuicError ForTransport(error_code code, std::string reason = "");
  static QuicError ForApplication(error_code code, std::string reason = "");
  static QuicError ForVersionNegotiation(std::string reason = "");
  static QuicError ForIdleClose(std::string reason = "");
  static QuicError ForNgtcp2Error(int code, std::string reason = "");
  static QuicError ForTlsAlert(int code, std::string reason = "");

  static QuicError FromConnectionClose(ngtcp2_conn* session);

  static QuicError TRANSPORT_NO_ERROR;
  static QuicError APPLICATION_NO_ERROR;
  static QuicError VERSION_NEGOTIATION;
  static QuicError IDLE_CLOSE;
  static QuicError INTERNAL_ERROR;

 private:
  const uint8_t* reason_c_str() const;

  std::string reason_;
  ngtcp2_ccerr error_;
  const ngtcp2_ccerr* ptr_ = nullptr;
};

}  // namespace node::quic

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
