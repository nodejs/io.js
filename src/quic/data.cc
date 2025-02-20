#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC

#include "data.h"
#include <env-inl.h>
#include <memory_tracker-inl.h>
#include <ngtcp2/ngtcp2.h>
#include <node_sockaddr-inl.h>
#include <string_bytes.h>
#include <v8.h>
#include "defs.h"
#include "util.h"

namespace node {

using v8::Array;
using v8::BigInt;
using v8::Integer;
using v8::Local;
using v8::MaybeLocal;
using v8::Uint8Array;
using v8::Undefined;
using v8::Value;

namespace quic {

Path::Path(const SocketAddress& local, const SocketAddress& remote) {
  ngtcp2_addr_init(&this->local, local.data(), local.length());
  ngtcp2_addr_init(&this->remote, remote.data(), remote.length());
}

std::string Path::ToString() const {
  DebugIndentScope indent;
  auto prefix = indent.Prefix();

  const sockaddr* local_in = reinterpret_cast<const sockaddr*>(local.addr);
  auto local_addr = SocketAddress::GetAddress(local_in);
  auto local_port = SocketAddress::GetPort(local_in);

  const sockaddr* remote_in = reinterpret_cast<const sockaddr*>(remote.addr);
  auto remote_addr = SocketAddress::GetAddress(remote_in);
  auto remote_port = SocketAddress::GetPort(remote_in);

  std::string res("{");
  res += prefix + "local: " + local_addr + ":" + std::to_string(local_port);
  res += prefix + "remote: " + remote_addr + ":" + std::to_string(remote_port);
  res += indent.Close();
  return res;
}

PathStorage::PathStorage() {
  Reset();
}
PathStorage::operator ngtcp2_path() {
  return path;
}

void PathStorage::Reset() {
  ngtcp2_path_storage_zero(this);
}

void PathStorage::CopyTo(PathStorage* path) const {
  ngtcp2_path_copy(&path->path, &this->path);
}

bool PathStorage::operator==(const PathStorage& other) const {
  return ngtcp2_path_eq(&path, &other.path) != 0;
}

bool PathStorage::operator!=(const PathStorage& other) const {
  return ngtcp2_path_eq(&path, &other.path) == 0;
}

// ============================================================================

Store::Store(std::shared_ptr<v8::BackingStore> store,
             size_t length,
             size_t offset)
    : store_(std::move(store)), length_(length), offset_(offset) {
  CHECK_LE(offset_, store_->ByteLength());
  CHECK_LE(length_, store_->ByteLength() - offset_);
}

Store::Store(std::unique_ptr<v8::BackingStore> store,
             size_t length,
             size_t offset)
    : store_(std::move(store)), length_(length), offset_(offset) {
  CHECK_LE(offset_, store_->ByteLength());
  CHECK_LE(length_, store_->ByteLength() - offset_);
}

Store::Store(Local<v8::ArrayBuffer> buffer, Option option)
    : Store(buffer->GetBackingStore(), buffer->ByteLength()) {
  if (option == Option::DETACH) {
    USE(buffer->Detach(Local<Value>()));
  }
}

Store::Store(Local<v8::ArrayBufferView> view, Option option)
    : Store(view->Buffer()->GetBackingStore(),
            view->ByteLength(),
            view->ByteOffset()) {
  if (option == Option::DETACH) {
    USE(view->Buffer()->Detach(Local<Value>()));
  }
}

Local<Uint8Array> Store::ToUint8Array(Environment* env) const {
  return !store_
             ? Uint8Array::New(v8::ArrayBuffer::New(env->isolate(), 0), 0, 0)
             : Uint8Array::New(v8::ArrayBuffer::New(env->isolate(), store_),
                               offset_,
                               length_);
}

Store::operator bool() const {
  return store_ != nullptr;
}
size_t Store::length() const {
  return length_;
}

template <typename T, typename t>
T Store::convert() const {
  T buf;
  buf.base =
      store_ != nullptr ? static_cast<t*>(store_->Data()) + offset_ : nullptr;
  buf.len = length_;
  return buf;
}

Store::operator uv_buf_t() const {
  return convert<uv_buf_t, char>();
}

Store::operator ngtcp2_vec() const {
  return convert<ngtcp2_vec, uint8_t>();
}

Store::operator nghttp3_vec() const {
  return convert<nghttp3_vec, uint8_t>();
}

void Store::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("store", store_);
}

// ============================================================================

namespace {
std::string TypeName(QuicError::Type type) {
  switch (type) {
    case QuicError::Type::APPLICATION:
      return "APPLICATION";
    case QuicError::Type::TRANSPORT:
      return "TRANSPORT";
    case QuicError::Type::VERSION_NEGOTIATION:
      return "VERSION_NEGOTIATION";
    case QuicError::Type::IDLE_CLOSE:
      return "IDLE_CLOSE";
  }
  UNREACHABLE();
}
}  // namespace

QuicError::QuicError(const std::string& reason)
    : reason_(reason), error_(), ptr_(&error_) {
  ngtcp2_ccerr_default(&error_);
}

QuicError::QuicError(const ngtcp2_ccerr* ptr)
    : reason_(reinterpret_cast<const char*>(ptr->reason), ptr->reasonlen),
      error_(),
      ptr_(ptr) {}

QuicError::QuicError(const ngtcp2_ccerr& error)
    : reason_(reinterpret_cast<const char*>(error.reason), error.reasonlen),
      error_(error),
      ptr_(&error_) {}

QuicError::operator bool() const {
  if ((code() == QUIC_NO_ERROR && type() == Type::TRANSPORT) ||
      ((code() == QUIC_APP_NO_ERROR && type() == Type::APPLICATION))) {
    return false;
  }
  return true;
}

const uint8_t* QuicError::reason_c_str() const {
  return reinterpret_cast<const uint8_t*>(reason_.c_str());
}

bool QuicError::operator!=(const QuicError& other) const {
  return !(*this == other);
}

bool QuicError::operator==(const QuicError& other) const {
  if (this == &other) return true;
  return type() == other.type() && code() == other.code() &&
         frame_type() == other.frame_type();
}

QuicError::Type QuicError::type() const {
  return static_cast<Type>(ptr_->type);
}

error_code QuicError::code() const {
  return ptr_->error_code;
}

uint64_t QuicError::frame_type() const {
  return ptr_->frame_type;
}

const std::string_view QuicError::reason() const {
  return reason_;
}

QuicError::operator const ngtcp2_ccerr&() const {
  return *ptr_;
}

QuicError::operator const ngtcp2_ccerr*() const {
  return ptr_;
}

std::string QuicError::reason_for_liberr(int liberr) {
  return ngtcp2_strerror(liberr);
}

std::string QuicError::reason_for_h3_liberr(int liberr) {
  return nghttp3_strerror(liberr);
}

bool QuicError::is_fatal_liberror(int liberr) {
  return ngtcp2_err_is_fatal(liberr) != 0;
}

bool QuicError::is_fatal_h3_liberror(int liberr) {
  return nghttp3_err_is_fatal(liberr) != 0;
}

error_code QuicError::liberr_to_code(int liberr) {
  return ngtcp2_err_infer_quic_transport_error_code(liberr);
}

error_code QuicError::h3_liberr_to_code(int liberr) {
  return nghttp3_err_infer_quic_app_error_code(liberr);
}

bool QuicError::is_crypto() const {
  return code() & NGTCP2_CRYPTO_ERROR;
}

std::optional<int> QuicError::crypto_error() const {
  if (!is_crypto()) return std::nullopt;
  return code() & ~NGTCP2_CRYPTO_ERROR;
}

MaybeLocal<Value> QuicError::ToV8Value(Environment* env) const {
  if ((type() == Type::TRANSPORT && code() == NGTCP2_NO_ERROR) ||
      (type() == Type::APPLICATION && code() == NGTCP2_APP_NOERROR) ||
      (type() == Type::APPLICATION && code() == NGHTTP3_H3_NO_ERROR)) {
    return Undefined(env->isolate());
  }

  Local<Value> argv[] = {
      Integer::New(env->isolate(), static_cast<int>(type())),
      BigInt::NewFromUnsigned(env->isolate(), code()),
      Undefined(env->isolate()),
  };

  if (reason_.length() > 0 &&
      !node::ToV8Value(env->context(), reason()).ToLocal(&argv[2])) {
    return MaybeLocal<Value>();
  }

  return Array::New(env->isolate(), argv, arraysize(argv)).As<Value>();
}

std::string QuicError::ToString() const {
  std::string str = "QuicError(";
  str += TypeName(type()) + ") ";
  str += std::to_string(code());
  if (!reason_.empty()) str += ": " + reason_;
  return str;
}

void QuicError::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("reason", reason_.length());
}

QuicError QuicError::ForTransport(error_code code, std::string reason) {
  QuicError error(std::move(reason));
  ngtcp2_ccerr_set_transport_error(
      &error.error_, code, error.reason_c_str(), error.reason().length());
  return error;
}

QuicError QuicError::ForApplication(error_code code, std::string reason) {
  QuicError error(std::move(reason));
  ngtcp2_ccerr_set_application_error(
      &error.error_, code, error.reason_c_str(), error.reason().length());
  return error;
}

QuicError QuicError::ForVersionNegotiation(std::string reason) {
  return ForNgtcp2Error(NGTCP2_ERR_RECV_VERSION_NEGOTIATION, std::move(reason));
}

QuicError QuicError::ForIdleClose(std::string reason) {
  return ForNgtcp2Error(NGTCP2_ERR_IDLE_CLOSE, std::move(reason));
}

QuicError QuicError::ForNgtcp2Error(int code, std::string reason) {
  QuicError error(std::move(reason));
  ngtcp2_ccerr_set_liberr(
      &error.error_, code, error.reason_c_str(), error.reason().length());
  return error;
}

QuicError QuicError::ForTlsAlert(int code, std::string reason) {
  QuicError error(std::move(reason));
  ngtcp2_ccerr_set_tls_alert(
      &error.error_, code, error.reason_c_str(), error.reason().length());
  return error;
}

QuicError QuicError::FromConnectionClose(ngtcp2_conn* session) {
  return QuicError(ngtcp2_conn_get_ccerr(session));
}

QuicError QuicError::TRANSPORT_NO_ERROR = ForTransport(QUIC_NO_ERROR);
QuicError QuicError::APPLICATION_NO_ERROR = ForApplication(QUIC_APP_NO_ERROR);
QuicError QuicError::VERSION_NEGOTIATION = ForVersionNegotiation();
QuicError QuicError::IDLE_CLOSE = ForIdleClose();
QuicError QuicError::INTERNAL_ERROR = ForNgtcp2Error(NGTCP2_ERR_INTERNAL);

}  // namespace quic
}  // namespace node

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
