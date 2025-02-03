#pragma once

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC

#include "base_object.h"
#include "bindingdata.h"
#include "defs.h"
#include "session.h"
#include "sessionticket.h"
#include "streams.h"

namespace node::quic {

// An Application implements the ALPN-protocol specific semantics on behalf
// of a QUIC Session.
class Session::Application : public MemoryRetainer {
 public:
  using Options = Session::Application_Options;

  Application(Session* session, const Options& options);
  DISALLOW_COPY_AND_MOVE(Application)

  virtual bool Start();

  // Session will forward all received stream data immediately on to the
  // Application. The only additional processing the Session does is to
  // automatically adjust the session-level flow control window. It is up to
  // the Application to do the same for the Stream-level flow control.
  virtual bool ReceiveStreamData(int64_t stream_id,
                                 const uint8_t* data,
                                 size_t datalen,
                                 const Stream::ReceiveDataFlags& flags,
                                 void* stream_user_data) = 0;

  // Session will forward all data acknowledgements for a stream to the
  // Application.
  virtual bool AcknowledgeStreamData(int64_t stream_id, size_t datalen);

  // Called to determine if a Header can be added to this application.
  // Applications that do not support headers will always return false.
  virtual bool CanAddHeader(size_t current_count,
                            size_t current_headers_length,
                            size_t this_header_length);

  // Called to mark the identified stream as being blocked. Not all
  // Application types will support blocked streams, and those that do will do
  // so differently.
  virtual void BlockStream(int64_t id);

  // Called when the session determines that there is outbound data available
  // to send for the given stream.
  virtual void ResumeStream(int64_t id);

  // Called when the Session determines that the maximum number of
  // remotely-initiated unidirectional streams has been extended. Not all
  // Application types will require this notification so the default is to do
  // nothing.
  virtual void ExtendMaxStreams(EndpointLabel label,
                                Direction direction,
                                uint64_t max_streams);

  // Called when the Session determines that the flow control window for the
  // given stream has been expanded. Not all Application types will require
  // this notification so the default is to do nothing.
  virtual void ExtendMaxStreamData(Stream* stream, uint64_t max_data);

  // Different Applications may wish to set some application data in the
  // session ticket (e.g. http/3 would set server settings in the application
  // data). By default, there's nothing to set.
  virtual void CollectSessionTicketAppData(
      SessionTicket::AppData* app_data) const;

  // Different Applications may set some application data in the session
  // ticket (e.g. http/3 would set server settings in the application data).
  // By default, there's nothing to get.
  virtual SessionTicket::AppData::Status ExtractSessionTicketAppData(
      const SessionTicket::AppData& app_data,
      SessionTicket::AppData::Source::Flag flag);

  // Notifies the Application that the identified stream has been closed.
  virtual void StreamClose(Stream* stream, QuicError&& error = QuicError());

  // Notifies the Application that the identified stream has been reset.
  virtual void StreamReset(Stream* stream,
                           uint64_t final_size,
                           QuicError&& error = QuicError());

  // Notifies the Application that the identified stream should stop sending.
  virtual void StreamStopSending(Stream* stream,
                                 QuicError&& error = QuicError());

  // Submits an outbound block of headers for the given stream. Not all
  // Application types will support headers, in which case this function
  // should return false.
  virtual bool SendHeaders(const Stream& stream,
                           HeadersKind kind,
                           const v8::Local<v8::Array>& headers,
                           HeadersFlags flags = HeadersFlags::NONE);

  // Signals to the Application that it should serialize and transmit any
  // pending session and stream packets it has accumulated.
  void SendPendingData();

  // Set the priority level of the stream if supported by the application. Not
  // all applications support priorities, in which case this function is a
  // non-op.
  virtual void SetStreamPriority(
      const Stream& stream,
      StreamPriority priority = StreamPriority::DEFAULT,
      StreamPriorityFlags flags = StreamPriorityFlags::NONE);

  // Get the priority level of the stream if supported by the application. Not
  // all applications support priorities, in which case this function returns
  // the default stream priority.
  virtual StreamPriority GetStreamPriority(const Stream& stream);

  struct StreamData;

  virtual int GetStreamData(StreamData* data) = 0;
  virtual bool StreamCommit(StreamData* data, size_t datalen) = 0;
  virtual bool ShouldSetFin(const StreamData& data) = 0;

  inline Environment* env() const { return session_->env(); }
  inline Session& session() { return *session_; }
  inline const Session& session() const { return *session_; }

 private:
  BaseObjectPtr<Packet> CreateStreamDataPacket();

  // Write the given stream_data into the buffer.
  ssize_t WriteVStream(PathStorage* path,
                       uint8_t* buf,
                       ssize_t* ndatalen,
                       size_t max_packet_size,
                       const StreamData& stream_data);

  Session* session_;
};

struct Session::Application::StreamData final {
  // The actual number of vectors in the struct, up to kMaxVectorCount.
  size_t count = 0;
  size_t remaining = 0;
  // The stream identifier. If this is a negative value then no stream is
  // identified.
  int64_t id = -1;
  int fin = 0;
  ngtcp2_vec data[kMaxVectorCount]{};
  BaseObjectPtr<Stream> stream;

  inline operator nghttp3_vec*() {
    return reinterpret_cast<nghttp3_vec*>(data);
  }

  inline operator const ngtcp2_vec*() const { return data; }
  inline operator ngtcp2_vec*() { return data; }

  std::string ToString() const;
};

}  // namespace node::quic

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
