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
#include <util.h>
#include "bindingdata.h"
#include "data.h"

namespace node::quic {

class Session;
class Stream;

using Ngtcp2Source = bob::SourceImpl<ngtcp2_vec>;

// When a request to open a stream is made before a Session is able to actually
// open a stream (either because the handshake is not yet sufficiently complete
// or concurrency limits are temporarily reached) then the request to open the
// stream is represented as a queued PendingStream.
//
// The PendingStream instance itself is held by the stream but sits in a linked
// list in the session.
//
// The PendingStream request can be canceled by dropping the PendingStream
// instance before it can be fulfilled, at which point it is removed from the
// pending stream queue.
//
// Note that only locally initiated streams can be created in a pending state.
class PendingStream final {
 public:
  PendingStream(Direction direction,
                Stream* stream,
                BaseObjectWeakPtr<Session> session);
  DISALLOW_COPY_AND_MOVE(PendingStream)
  ~PendingStream();

  // Called when the stream has been opened. Transitions the stream from a
  // pending state to an opened state.
  void fulfill(int64_t id);

  // Called when opening the stream fails or is canceled. Transitions the
  // stream into a closed/destroyed state.
  void reject(QuicError error = QuicError());

  inline Direction direction() const { return direction_; }

 private:
  Direction direction_;
  Stream* stream_;
  BaseObjectWeakPtr<Session> session_;
  bool waiting_ = true;

  ListNode<PendingStream> pending_stream_queue_;

 public:
  using PendingStreamQueue =
      ListHead<PendingStream, &PendingStream::pending_stream_queue_>;
};

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
// state but still have unacknowledged data in both the inbound and outbound
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
//
// Streams may be created in a pending state. This means that while the Stream
// object is created, it has not yet been opened in ngtcp2 and therefore has
// no official status yet. Certain operations can still be performed on the
// stream object such as providing data and headers, and destroying the stream.
//
// When a stream is created the data source for the stream must be given.
// If no data source is given, then the stream is assumed to not have any
// outbound data. The data source can be fixed length or may support
// streaming. What this means practically is, when a stream is opened,
// you must already have a sense of whether that will provide data or
// not. When in doubt, specify a streaming data source, which can produce
// zero-length output.
class Stream final : public AsyncWrap,
                     public Ngtcp2Source,
                     public DataQueue::BackpressureListener {
 public:
  using Header = NgHeaderBase<BindingData>;

  static v8::Maybe<std::shared_ptr<DataQueue>> GetDataQueueFromSource(
      Environment* env, v8::Local<v8::Value> value);

  static Stream* From(void* stream_user_data);

  static bool HasInstance(Environment* env, v8::Local<v8::Value> value);
  static v8::Local<v8::FunctionTemplate> GetConstructorTemplate(
      Environment* env);
  static void InitPerIsolate(IsolateData* data,
                             v8::Local<v8::ObjectTemplate> target);
  static void InitPerContext(Realm* realm, v8::Local<v8::Object> target);
  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);

  // Creates a new non-pending stream.
  static BaseObjectPtr<Stream> Create(
      Session* session,
      int64_t id,
      std::shared_ptr<DataQueue> source = nullptr);

  // Creates a new pending stream.
  static BaseObjectPtr<Stream> Create(
      Session* session,
      Direction direction,
      std::shared_ptr<DataQueue> source = nullptr);

  // The constructor is only public to be visible by MakeDetachedBaseObject.
  // Call Create to create new instances of Stream.
  Stream(BaseObjectWeakPtr<Session> session,
         v8::Local<v8::Object> obj,
         int64_t id,
         std::shared_ptr<DataQueue> source);

  // Creates the stream in a pending state. The constructor is only public
  // to be visible to MakeDetachedBaseObject. Call Create to create new
  // instances of Stream.
  Stream(BaseObjectWeakPtr<Session> session,
         v8::Local<v8::Object> obj,
         Direction direction,
         std::shared_ptr<DataQueue> source);
  DISALLOW_COPY_AND_MOVE(Stream)
  ~Stream() override;

  // While the stream is still pending, the id will be -1.
  int64_t id() const;

  // While the stream is still pending, the origin will be invalid.
  Side origin() const;

  Direction direction() const;

  Session& session() const;

  // True if this stream was created in a pending state and is still waiting
  // to be created.
  bool is_pending() const;

  // True if we've completely sent all outbound data for this stream.
  // Importantly, this does not necessarily mean that we are completely
  // done with the outbound data. We may still be waiting on outbound
  // data to be acknowledged by the remote peer.
  bool is_eos() const;

  // True if this stream is still in a readable state.
  bool is_readable() const;

  // True if this stream is still in a writable state.
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

  // Forcefully close the stream immediately. Data already queued in the
  // inbound is preserved but new data will not be accepted. All pending
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

  // Currently, only HTTP/3 streams support headers. These methods are here
  // to support that. They are not used when using any other QUIC application.

  void BeginHeaders(HeadersKind kind);
  void set_headers_kind(HeadersKind kind);
  // Returns false if the header cannot be added. This will typically happen
  // if the application does not support headers, a maximum number of headers
  // have already been added, or the maximum total header length is reached.
  bool AddHeader(const Header& header);

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(Stream)
  SET_SELF_SIZE(Stream)

  struct State;
  struct Stats;

 private:
  struct Impl;
  struct PendingHeaders;

  class Outbound;

  // Gets a reader for the data received for this stream from the peer,
  BaseObjectPtr<Blob::Reader> get_reader();

  void set_final_size(uint64_t amount);
  void set_outbound(std::shared_ptr<DataQueue> source);

  bool is_local_unidirectional() const;
  bool is_remote_unidirectional() const;

  // JavaScript callouts

  // Notifies the JavaScript side that the stream has been destroyed.
  void EmitClose(const QuicError& error);

  // Notifies the JavaScript side that the stream has been reset.
  void EmitReset(const QuicError& error);

  // Notifies the JavaScript side that the application is ready to receive
  // trailing headers.
  void EmitWantTrailers();

  // Notifies the JavaScript side that sending data on the stream has been
  // blocked because of flow control restriction.
  void EmitBlocked();

  // Delivers the set of inbound headers that have been collected.
  void EmitHeaders();

  void NotifyReadableEnded(uint64_t code);
  void NotifyWritableEnded(uint64_t code);

  // When a pending stream is finally opened, the NotifyStreamOpened method
  // will be called and the id will be assigned.
  void NotifyStreamOpened(int64_t id);
  void EnqueuePendingHeaders(HeadersKind kind,
                             v8::Local<v8::Array> headers,
                             HeadersFlags flags);

  AliasedStruct<Stats> stats_;
  AliasedStruct<State> state_;
  BaseObjectWeakPtr<Session> session_;
  std::unique_ptr<Outbound> outbound_;
  std::shared_ptr<DataQueue> inbound_;

  // If the stream cannot be opened yet, it will be created in a pending state.
  // Once the owning session is able to, it will complete opening of the stream
  // and the stream id will be assigned.
  std::optional<std::unique_ptr<PendingStream>> maybe_pending_stream_ =
      std::nullopt;
  std::vector<std::unique_ptr<PendingHeaders>> pending_headers_queue_;
  uint64_t pending_close_read_code_ = NGTCP2_APP_NOERROR;
  uint64_t pending_close_write_code_ = NGTCP2_APP_NOERROR;

  struct PendingPriority {
    StreamPriority priority;
    StreamPriorityFlags flags;
  };
  std::optional<PendingPriority> pending_priority_ = std::nullopt;

  // The headers_ field holds a block of headers that have been received and
  // are being buffered for delivery to the JavaScript side.
  // TODO(@jasnell): Use v8::Global instead of v8::Local here.
  std::vector<v8::Local<v8::Value>> headers_;

  // The headers_kind_ field indicates the kind of headers that are being
  // buffered.
  HeadersKind headers_kind_ = HeadersKind::INITIAL;

  // The headers_length_ field holds the total length of the headers that have
  // been buffered.
  size_t headers_length_ = 0;

  friend struct Impl;
  friend class PendingStream;
  friend class Http3ApplicationImpl;
  friend class DefaultApplication;

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

}  // namespace node::quic

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
