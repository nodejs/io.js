#pragma once

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC

#include <aliased_struct.h>
#include <async_wrap.h>
#include <base_object.h>
#include <dataqueue/queue.h>
#include <env.h>
#include <memory_tracker.h>
#include <node_blob.h>
#include <node_bob.h>
#include <node_http_common.h>
#include "bindingdata.h"
#include "data.h"

namespace node {
namespace quic {

class Session;

using Ngtcp2Source = bob::SourceImpl<ngtcp2_vec>;

// QUIC Stream's are simple data flows that may be:
//
// * Bidirectional (both sides can send) or Unidirectional (one side can send)
// * Server or Client Initiated
//
// The flow direction and origin of the stream are important in determining the
// write and read state (Open or Closed). Specifically:
//
// Bidirectional Stream States:
// +--------+--------------+----------+----------+
// |   ON   | Initiated By | Readable | Writable |
// +--------+--------------+----------+----------+
// | Server |   Server     |    Y     |    Y     |
// +--------+--------------+----------+----------+
// | Server |   Client     |    Y     |    Y     |
// +--------+--------------+----------+----------+
// | Client |   Server     |    Y     |    Y     |
// +--------+--------------+----------+----------+
// | Client |   Client     |    Y     |    Y     |
// +--------+--------------+----------+----------+
//
// Unidirectional Stream States
// +--------+--------------+----------+----------+
// |   ON   | Initiated By | Readable | Writable |
// +--------+--------------+----------+----------+
// | Server |   Server     |    N     |    Y     |
// +--------+--------------+----------+----------+
// | Server |   Client     |    Y     |    N     |
// +--------+--------------+----------+----------+
// | Client |   Server     |    Y     |    N     |
// +--------+--------------+----------+----------+
// | Client |   Client     |    N     |    Y     |
// +--------+--------------+----------+----------+
//
// All data sent via the Stream is buffered internally until either receipt is
// acknowledged from the peer or attempts to send are abandoned. The fact that
// data is buffered in memory makes it essential that the flow control for the
// session and the stream are properly handled. For now, we are largely relying
// on ngtcp2's default flow control mechanisms which generally should be doing
// the right thing.
//
// A Stream may be in a fully closed state (No longer readable nor writable)
// state but still have unacknowledged data in it's inbound and outbound
// queues.
//
// A Stream is gracefully closed when (a) both read and write states are closed,
// (b) all queued data has been acknowledged.
//
// The Stream may be forcefully closed immediately using destroy(err). This
// causes all queued outbound data and pending JavaScript writes are abandoned,
// and causes the Stream to be immediately closed at the ngtcp2 level without
// waiting for any outstanding acknowledgements. Keep in mind, however, that the
// peer is not notified that the stream is destroyed and may attempt to continue
// sending data and acknowledgements.
//
// QUIC streams in general do not have headers. Some QUIC applications, however,
// may associate headers with the stream (HTTP/3 for instance).
class Stream : public AsyncWrap,
               public Ngtcp2Source,
               public DataQueue::BackpressureListener {
 public:
  using Header = NgHeaderBase<BindingData>;

  static Stream* From(void* stream_user_data);

  static bool HasInstance(Environment* env, v8::Local<v8::Value> value);
  static v8::Local<v8::FunctionTemplate> GetConstructorTemplate(
      Environment* env);
  static void Initialize(Environment* env, v8::Local<v8::Object> target);
  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);

  static BaseObjectPtr<Stream> Create(
      Session* session,
      int64_t id,
      std::shared_ptr<DataQueue> source = nullptr);

  // The constructor is only public to be visible by MakeDetachedBaseObject.
  // Call Create to create new instances of Stream.
  Stream(BaseObjectWeakPtr<Session> session,
         v8::Local<v8::Object> obj,
         int64_t id,
         std::shared_ptr<DataQueue> source);
  ~Stream() override;

  int64_t id() const;
  Side origin() const;
  Direction direction() const;
  Session& session() const;

  bool is_destroyed() const;

  // True if we've completely sent all outbound data for this stream.
  bool is_eos() const;

  bool is_readable() const;
  bool is_writable() const;

  // Called by the session/application to indicate that the specified number
  // of bytes have been acknowledged by the peer.
  void Acknowledge(size_t datalen);
  void Commit(size_t datalen);
  void EndWritable();
  void EndReadable(std::optional<uint64_t> maybe_final_size = std::nullopt);
  void EntryRead(size_t amount) override;

  // Pulls data from the internal outbound DataQueue configured for this stream.
  int DoPull(bob::Next<ngtcp2_vec> next,
             int options,
             ngtcp2_vec* data,
             size_t count,
             size_t max_count_hint) override;

  // Forcefully close the stream immediately. All queued data and pending
  // writes are abandoned, and the stream is immediately closed at the ngtcp2
  // level without waiting for any outstanding acknowledgements.
  void Destroy(QuicError error = QuicError());

  struct ReceiveDataFlags final {
    // Identifies the final chunk of data that the peer will send for the
    // stream.
    bool fin = false;
    // Indicates that this chunk of data was received in a 0RTT packet before
    // the TLS handshake completed, suggesting that is is not as secure and
    // could be replayed by an attacker.
    bool early = false;
  };

  void ReceiveData(const uint8_t* data, size_t len, ReceiveDataFlags flags);
  void ReceiveStopSending(QuicError error);
  void ReceiveStreamReset(uint64_t final_size, QuicError error);

  void BeginHeaders(HeadersKind kind);
  // Returns false if the header cannot be added. This will typically happen
  // if the application does not support headers, a maximimum number of headers
  // have already been added, or the maximum total header length is reached.
  bool AddHeader(const Header& header);
  void set_headers_kind(HeadersKind kind);

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(Stream)
  SET_SELF_SIZE(Stream)

  struct State;
  struct Stats;

  // Notifies the JavaScript side that sending data on the stream has been
  // blocked because of flow control restriction.
  void EmitBlocked();

  // Delivers the set of inbound headers that have been collected.
  void EmitHeaders();

 private:
  struct Impl;
  class Outbound;

  // Gets a reader for the data received for this stream from the peer,
  BaseObjectPtr<Blob::Reader> get_reader();

  void set_final_size(uint64_t amount);
  void set_outbound(std::shared_ptr<DataQueue> source);

  // JavaScript callouts

  // Notifies the JavaScript side that the stream has been destroyed.
  void EmitClose(const QuicError& error);

  // Notifies the JavaScript side that the stream has been reset.
  void EmitReset(const QuicError& error);

  // Notifies the JavaScript side that the application is ready to receive
  // trailing headers.
  void EmitWantTrailers();

  AliasedStruct<Stats> stats_;
  AliasedStruct<State> state_;
  BaseObjectWeakPtr<Session> session_;
  const Side origin_;
  const Direction direction_;
  std::unique_ptr<Outbound> outbound_;
  std::shared_ptr<DataQueue> inbound_;

  std::vector<v8::Local<v8::Value>> headers_;
  HeadersKind headers_kind_ = HeadersKind::INITIAL;
  size_t headers_length_ = 0;

  friend struct Impl;

 public:
  // The Queue/Schedule/Unschedule here are part of the mechanism used to
  // determine which streams have data to send on the session. When a stream
  // potentially has data available, it will be scheduled in the Queue. Then,
  // when the Session::Application starts sending pending data, it will check
  // the queue to see if there are streams waiting. If there are, it will grab
  // one and check to see if there is data to send. When a stream does not have
  // data to send (such as when it is initially created or is using an async
  // source that is still waiting for data to be pushed) it will not appear in
  // the queue.
  ListNode<Stream> stream_queue_;
  using Queue = ListHead<Stream, &Stream::stream_queue_>;

  void Schedule(Queue* queue);
  void Unschedule();
};

}  // namespace quic
}  // namespace node

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
