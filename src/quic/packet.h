#pragma once

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC

#include <base_object.h>
#include <env.h>
#include <ngtcp2/ngtcp2.h>
#include <node_external_reference.h>
#include <node_sockaddr.h>
#include <req_wrap.h>
#include <uv.h>
#include <v8.h>
#include <string>
#include "bindingdata.h"
#include "cid.h"
#include "data.h"
#include "defs.h"
#include "tokens.h"

namespace node::quic {

struct PathDescriptor {
  uint32_t version;
  const CID& dcid;
  const CID& scid;
  const SocketAddress& local_address;
  const SocketAddress& remote_address;
  std::string ToString() const;
};

// A Packet encapsulates serialized outbound QUIC data.
// Packets must never be larger than the path MTU. The
// default QUIC packet maximum length is 1200 bytes,
// which we assume by default. The packet storage will
// be stack allocated up to this size.
//
// Packets are maintained in a freelist held by the
// BindingData instance. When using Create() to create
// a Packet, we'll check to see if there is a free
// packet in the freelist and use it instead of starting
// fresh with a new packet. The freelist can store at
// most kMaxFreeList packets
//
// Packets are always encrypted so their content should
// be considered opaque to us. We leave it entirely up
// to ngtcp2 how to encode QUIC frames into the packet.
class Packet final : public ReqWrap<uv_udp_send_t> {
 private:
  struct Data;

 public:
  static v8::Local<v8::FunctionTemplate> GetConstructorTemplate(
      Environment* env);

  class Listener {
   public:
    virtual void PacketDone(int status) = 0;
  };

  // Do not use the Packet constructors directly to create
  // them. These are public only to support MakeBaseObject.
  // Use the Create, or Create variants to create or
  // acquire packet instances.

  Packet(Environment* env,
         Listener* listener,
         v8::Local<v8::Object> object,
         const SocketAddress& destination,
         size_t length,
         const char* diagnostic_label = "<unknown>");

  Packet(Environment* env,
         Listener* listener,
         v8::Local<v8::Object> object,
         const SocketAddress& destination,
         std::shared_ptr<Data> data);

  DISALLOW_COPY_AND_MOVE(Packet)

  const SocketAddress& destination() const;
  size_t length() const;
  operator uv_buf_t() const;
  operator ngtcp2_vec() const;

  // Modify the size of the packet after ngtcp2 has written
  // to it. len must be <= length(). We call this after we've
  // asked ngtcp2 to encode frames into the packet and ngtcp2
  // tells us how many of the packets bytes were used.
  void Truncate(size_t len);

  static BaseObjectPtr<Packet> Create(
      Environment* env,
      Listener* listener,
      const SocketAddress& destination,
      size_t length = kDefaultMaxPacketLength,
      const char* diagnostic_label = "<unknown>");

  BaseObjectPtr<Packet> Clone() const;

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(Packet)
  SET_SELF_SIZE(Packet)

  std::string ToString() const;

  static BaseObjectPtr<Packet> CreateRetryPacket(
      Environment* env,
      Listener* listener,
      const PathDescriptor& path_descriptor,
      const TokenSecret& token_secret);

  static BaseObjectPtr<Packet> CreateConnectionClosePacket(
      Environment* env,
      Listener* listener,
      const SocketAddress& destination,
      ngtcp2_conn* conn,
      const QuicError& error);

  static BaseObjectPtr<Packet> CreateImmediateConnectionClosePacket(
      Environment* env,
      Listener* listener,
      const PathDescriptor& path_descriptor,
      const QuicError& reason);

  static BaseObjectPtr<Packet> CreateStatelessResetPacket(
      Environment* env,
      Listener* listener,
      const PathDescriptor& path_descriptor,
      const TokenSecret& token_secret,
      size_t source_len);

  static BaseObjectPtr<Packet> CreateVersionNegotiationPacket(
      Environment* env,
      Listener* listener,
      const PathDescriptor& path_descriptor);

  // Called when the packet is done being sent.
  void Done(int status);

 private:
  static BaseObjectPtr<Packet> FromFreeList(Environment* env,
                                            std::shared_ptr<Data> data,
                                            Listener* listener,
                                            const SocketAddress& destination);

  Listener* listener_;
  SocketAddress destination_;
  std::shared_ptr<Data> data_;
};

}  // namespace node::quic

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
