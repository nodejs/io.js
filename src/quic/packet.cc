#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC

#include "packet.h"
#include <base_object-inl.h>
#include <crypto/crypto_util.h>
#include <env-inl.h>
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <node_sockaddr-inl.h>
#include <req_wrap-inl.h>
#include <uv.h>
#include <v8.h>
#include <string>
#include "bindingdata.h"
#include "cid.h"
#include "defs.h"
#include "ncrypto.h"
#include "tokens.h"

namespace node {

using v8::FunctionTemplate;
using v8::Local;
using v8::Object;

namespace quic {

namespace {
static constexpr size_t kRandlen = NGTCP2_MIN_STATELESS_RESET_RANDLEN * 5;
static constexpr size_t kMinStatelessResetLen = 41;
static constexpr size_t kMaxFreeList = 100;
}  // namespace

std::string PathDescriptor::ToString() const {
  DebugIndentScope indent;
  auto prefix = indent.Prefix();
  std::string res = "{";
  res += prefix + "version: " + std::to_string(version);
  res += prefix + "dcid: " + dcid.ToString();
  res += prefix + "scid: " + scid.ToString();
  res += prefix + "local address: " + local_address.ToString();
  res += prefix + "remote address: " + remote_address.ToString();
  res += indent.Close();
  return res;
}

struct Packet::Data final : public MemoryRetainer {
  MaybeStackBuffer<uint8_t, kDefaultMaxPacketLength> data_;

  // The diagnostic_label_ is used only as a debugging tool when
  // logging debug information about the packet. It identifies
  // the purpose of the packet.
  const std::string diagnostic_label_;

  void MemoryInfo(MemoryTracker* tracker) const override {
    tracker->TrackFieldWithSize("data", data_.length());
  }
  SET_MEMORY_INFO_NAME(Data)
  SET_SELF_SIZE(Data)

  Data(size_t length, std::string_view diagnostic_label)
      : diagnostic_label_(diagnostic_label) {
    data_.AllocateSufficientStorage(length);
  }

  size_t length() const { return data_.length(); }
  operator uv_buf_t() {
    return uv_buf_init(reinterpret_cast<char*>(data_.out()), data_.length());
  }
  operator ngtcp2_vec() { return ngtcp2_vec{data_.out(), data_.length()}; }

  std::string ToString() const {
    return diagnostic_label_ + ", " + std::to_string(length());
  }
};

const SocketAddress& Packet::destination() const {
  return destination_;
}

size_t Packet::length() const {
  return data_ ? data_->length() : 0;
}

Packet::operator uv_buf_t() const {
  return !data_ ? uv_buf_init(nullptr, 0) : *data_;
}

Packet::operator ngtcp2_vec() const {
  return !data_ ? ngtcp2_vec{nullptr, 0} : *data_;
}

void Packet::Truncate(size_t len) {
  DCHECK(data_);
  DCHECK_LE(len, data_->length());
  data_->data_.SetLength(len);
}

Local<FunctionTemplate> Packet::GetConstructorTemplate(Environment* env) {
  auto& state = BindingData::Get(env);
  Local<FunctionTemplate> tmpl = state.packet_constructor_template();
  if (tmpl.IsEmpty()) {
    tmpl = NewFunctionTemplate(env->isolate(), IllegalConstructor);
    tmpl->Inherit(ReqWrap<uv_udp_send_t>::GetConstructorTemplate(env));
    tmpl->InstanceTemplate()->SetInternalFieldCount(kInternalFieldCount);
    tmpl->SetClassName(state.packetwrap_string());
    state.set_packet_constructor_template(tmpl);
  }
  return tmpl;
}

BaseObjectPtr<Packet> Packet::Create(Environment* env,
                                     Listener* listener,
                                     const SocketAddress& destination,
                                     size_t length,
                                     const char* diagnostic_label) {
  if (BindingData::Get(env).packet_freelist.empty()) {
    Local<Object> obj;
    if (!GetConstructorTemplate(env)
             ->InstanceTemplate()
             ->NewInstance(env->context())
             .ToLocal(&obj)) [[unlikely]] {
      return {};
    }

    return MakeBaseObject<Packet>(
        env, listener, obj, destination, length, diagnostic_label);
  }

  return FromFreeList(env,
                      std::make_shared<Data>(length, diagnostic_label),
                      listener,
                      destination);
}

BaseObjectPtr<Packet> Packet::Clone() const {
  auto& binding = BindingData::Get(env());
  if (binding.packet_freelist.empty()) {
    Local<Object> obj;
    if (!GetConstructorTemplate(env())
             ->InstanceTemplate()
             ->NewInstance(env()->context())
             .ToLocal(&obj)) [[unlikely]] {
      return {};
    }

    return MakeBaseObject<Packet>(env(), listener_, obj, destination_, data_);
  }

  return FromFreeList(env(), data_, listener_, destination_);
}

BaseObjectPtr<Packet> Packet::FromFreeList(Environment* env,
                                           std::shared_ptr<Data> data,
                                           Listener* listener,
                                           const SocketAddress& destination) {
  auto& binding = BindingData::Get(env);
  if (binding.packet_freelist.empty()) return {};
  auto obj = binding.packet_freelist.back();
  binding.packet_freelist.pop_back();
  CHECK(obj);
  CHECK_EQ(env, obj->env());
  auto packet = BaseObjectPtr<Packet>(static_cast<Packet*>(obj.get()));
  Debug(packet.get(), "Reusing packet from freelist");
  packet->data_ = std::move(data);
  packet->destination_ = destination;
  packet->listener_ = listener;
  return packet;
}

Packet::Packet(Environment* env,
               Listener* listener,
               Local<Object> object,
               const SocketAddress& destination,
               std::shared_ptr<Data> data)
    : ReqWrap<uv_udp_send_t>(env, object, PROVIDER_QUIC_PACKET),
      listener_(listener),
      destination_(destination),
      data_(std::move(data)) {
  ClearWeak();
  Debug(this, "Created a new packet");
}

Packet::Packet(Environment* env,
               Listener* listener,
               Local<Object> object,
               const SocketAddress& destination,
               size_t length,
               const char* diagnostic_label)
    : Packet(env,
             listener,
             object,
             destination,
             std::make_shared<Data>(length, diagnostic_label)) {}

void Packet::Done(int status) {
  Debug(this, "Packet is done with status %d", status);
  BaseObjectPtr<Packet> self(this);
  self->MakeWeak();

  if (listener_ != nullptr && IsDispatched()) {
    listener_->PacketDone(status);
  }
  // As a performance optimization, we add this packet to a freelist
  // rather than deleting it but only if the freelist isn't too
  // big, we don't want to accumulate these things forever.
  auto& binding = BindingData::Get(env());
  if (binding.packet_freelist.size() >= kMaxFreeList) {
    return;
  }

  Debug(this, "Returning packet to freelist");
  listener_ = nullptr;
  data_.reset();
  Reset();
  binding.packet_freelist.push_back(std::move(self));
}

std::string Packet::ToString() const {
  if (!data_) return "Packet (<empty>)";
  return "Packet (" + data_->ToString() + ")";
}

void Packet::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("destination", destination_);
  tracker->TrackField("data", data_);
}

BaseObjectPtr<Packet> Packet::CreateRetryPacket(
    Environment* env,
    Listener* listener,
    const PathDescriptor& path_descriptor,
    const TokenSecret& token_secret) {
  auto& random = CID::Factory::random();
  CID cid = random.Generate();
  RetryToken token(path_descriptor.version,
                   path_descriptor.remote_address,
                   cid,
                   path_descriptor.dcid,
                   token_secret);
  if (!token) return {};

  const ngtcp2_vec& vec = token;

  size_t pktlen =
      vec.len + (2 * NGTCP2_MAX_CIDLEN) + path_descriptor.scid.length() + 8;

  auto packet =
      Create(env, listener, path_descriptor.remote_address, pktlen, "retry");
  if (!packet) return packet;

  ngtcp2_vec dest = *packet;

  ssize_t nwrite = ngtcp2_crypto_write_retry(dest.base,
                                             pktlen,
                                             path_descriptor.version,
                                             path_descriptor.scid,
                                             cid,
                                             path_descriptor.dcid,
                                             vec.base,
                                             vec.len);
  if (nwrite <= 0) {
    packet->Done(UV_ECANCELED);
    return {};
  }
  packet->Truncate(static_cast<size_t>(nwrite));
  return packet;
}

BaseObjectPtr<Packet> Packet::CreateConnectionClosePacket(
    Environment* env,
    Listener* listener,
    const SocketAddress& destination,
    ngtcp2_conn* conn,
    const QuicError& error) {
  auto packet = Create(
      env, listener, destination, kDefaultMaxPacketLength, "connection close");
  if (!packet) return packet;
  ngtcp2_vec vec = *packet;

  ssize_t nwrite = ngtcp2_conn_write_connection_close(
      conn, nullptr, nullptr, vec.base, vec.len, error, uv_hrtime());
  if (nwrite < 0) {
    packet->Done(UV_ECANCELED);
    return {};
  }
  packet->Truncate(static_cast<size_t>(nwrite));
  return packet;
}

BaseObjectPtr<Packet> Packet::CreateImmediateConnectionClosePacket(
    Environment* env,
    Listener* listener,
    const PathDescriptor& path_descriptor,
    const QuicError& reason) {
  auto packet = Create(env,
                       listener,
                       path_descriptor.remote_address,
                       kDefaultMaxPacketLength,
                       "immediate connection close (endpoint)");
  if (!packet) return packet;
  ngtcp2_vec vec = *packet;
  ssize_t nwrite = ngtcp2_crypto_write_connection_close(
      vec.base,
      vec.len,
      path_descriptor.version,
      path_descriptor.dcid,
      path_descriptor.scid,
      reason.code(),
      // We do not bother sending a reason string here, even if
      // there is one in the QuicError
      nullptr,
      0);
  if (nwrite <= 0) {
    packet->Done(UV_ECANCELED);
    return {};
  }
  packet->Truncate(static_cast<size_t>(nwrite));
  return packet;
}

BaseObjectPtr<Packet> Packet::CreateStatelessResetPacket(
    Environment* env,
    Listener* listener,
    const PathDescriptor& path_descriptor,
    const TokenSecret& token_secret,
    size_t source_len) {
  // Per the QUIC spec, a stateless reset token must be strictly smaller than
  // the packet that triggered it. This is one of the mechanisms to prevent
  // infinite looping exchange of stateless tokens with the peer. An endpoint
  // should never send a stateless reset token smaller than 41 bytes per the
  // QUIC spec. The reason is that packets less than 41 bytes may allow an
  // observer to reliably determine that it's a stateless reset.
  size_t pktlen = source_len - 1;
  if (pktlen < kMinStatelessResetLen) return {};

  StatelessResetToken token(token_secret, path_descriptor.dcid);
  uint8_t random[kRandlen];
  CHECK(ncrypto::CSPRNG(random, kRandlen));

  auto packet = Create(env,
                       listener,
                       path_descriptor.remote_address,
                       kDefaultMaxPacketLength,
                       "stateless reset");
  if (!packet) return packet;
  ngtcp2_vec vec = *packet;

  ssize_t nwrite = ngtcp2_pkt_write_stateless_reset(
      vec.base, pktlen, token, random, kRandlen);
  if (nwrite <= static_cast<ssize_t>(kMinStatelessResetLen)) {
    packet->Done(UV_ECANCELED);
    return {};
  }

  packet->Truncate(static_cast<size_t>(nwrite));
  return packet;
}

BaseObjectPtr<Packet> Packet::CreateVersionNegotiationPacket(
    Environment* env,
    Listener* listener,
    const PathDescriptor& path_descriptor) {
  const auto generateReservedVersion = [&] {
    socklen_t addrlen = path_descriptor.remote_address.length();
    uint32_t h = 0x811C9DC5u;
    uint32_t ver = htonl(path_descriptor.version);
    const uint8_t* p = path_descriptor.remote_address.raw();
    const uint8_t* ep = p + addrlen;
    for (; p != ep; ++p) {
      h ^= *p;
      h *= 0x01000193u;
    }
    p = reinterpret_cast<const uint8_t*>(&ver);
    ep = p + sizeof(path_descriptor.version);
    for (; p != ep; ++p) {
      h ^= *p;
      h *= 0x01000193u;
    }
    h &= 0xf0f0f0f0u;
    h |= NGTCP2_RESERVED_VERSION_MASK;
    return h;
  };

  uint32_t sv[3] = {
      generateReservedVersion(), NGTCP2_PROTO_VER_MIN, NGTCP2_PROTO_VER_MAX};

  size_t pktlen = path_descriptor.dcid.length() +
                  path_descriptor.scid.length() + (sizeof(sv)) + 7;

  auto packet = Create(env,
                       listener,
                       path_descriptor.remote_address,
                       kDefaultMaxPacketLength,
                       "version negotiation");
  if (!packet) return packet;
  ngtcp2_vec vec = *packet;

  ssize_t nwrite =
      ngtcp2_pkt_write_version_negotiation(vec.base,
                                           pktlen,
                                           0,
                                           path_descriptor.dcid,
                                           path_descriptor.dcid.length(),
                                           path_descriptor.scid,
                                           path_descriptor.scid.length(),
                                           sv,
                                           arraysize(sv));
  if (nwrite <= 0) {
    packet->Done(UV_ECANCELED);
    return {};
  }
  packet->Truncate(static_cast<size_t>(nwrite));
  return packet;
}

}  // namespace quic
}  // namespace node

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
