#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC

#include "http3.h"
#include <async_wrap-inl.h>
#include <base_object-inl.h>
#include <debug_utils-inl.h>
#include <env-inl.h>
#include <memory_tracker-inl.h>
#include <nghttp3/nghttp3.h>
#include <ngtcp2/ngtcp2.h>
#include <node_http_common-inl.h>
#include <node_sockaddr-inl.h>
#include <util-inl.h>
#include "application.h"
#include "bindingdata.h"
#include "defs.h"
#include "session.h"
#include "sessionticket.h"

namespace node::quic {
namespace {

struct Http3HeadersTraits {
  typedef nghttp3_nv nv_t;
};

struct Http3RcBufferPointerTraits {
  typedef nghttp3_rcbuf rcbuf_t;
  typedef nghttp3_vec vector_t;

  static void inc(rcbuf_t* buf) {
    CHECK_NOT_NULL(buf);
    nghttp3_rcbuf_incref(buf);
  }
  static void dec(rcbuf_t* buf) {
    CHECK_NOT_NULL(buf);
    nghttp3_rcbuf_decref(buf);
  }
  static vector_t get_vec(rcbuf_t* buf) {
    CHECK_NOT_NULL(buf);
    return nghttp3_rcbuf_get_buf(buf);
  }
  static bool is_static(const rcbuf_t* buf) {
    CHECK_NOT_NULL(buf);
    return nghttp3_rcbuf_is_static(buf);
  }
};

using Http3ConnectionPointer = DeleteFnPtr<nghttp3_conn, nghttp3_conn_del>;
using Http3Headers = NgHeaders<Http3HeadersTraits>;
using Http3RcBufferPointer = NgRcBufPointer<Http3RcBufferPointerTraits>;

struct Http3HeaderTraits {
  typedef Http3RcBufferPointer rcbufferpointer_t;
  typedef BindingData allocator_t;

  static const char* ToHttpHeaderName(int32_t token) {
    switch (token) {
      case -1:
        return nullptr;
#define V(key, name)                                                           \
  case NGHTTP3_QPACK_TOKEN__##key:                                             \
    return name;
        HTTP_SPECIAL_HEADERS(V)
#undef V
#define V(key, name)                                                           \
  case NGHTTP3_QPACK_TOKEN_##key:                                              \
    return name;
        HTTP_REGULAR_HEADERS(V)
#undef V
    }
    return nullptr;
  }
};

using Http3Header = NgHeader<Http3HeaderTraits>;

// Implements the low-level HTTP/3 Application semantics.
class Http3Application final : public Session::Application {
 public:
  Http3Application(Session* session,
                   const Session::Application_Options& options)
      : Application(session, options),
        allocator_(BindingData::Get(env())),
        options_(options),
        conn_(InitializeConnection()) {
    session->set_priority_supported();
  }

  bool Start() override {
    CHECK(!started_);
    started_ = true;
    Debug(&session(), "Starting HTTP/3 application.");
    auto params = ngtcp2_conn_get_remote_transport_params(session());
    if (params == nullptr) {
      // The params are not available yet. Cannot start.
      Debug(&session(),
            "Cannot start HTTP/3 application yet. No remote transport params");
      return false;
    }

    if (params->initial_max_streams_uni < 3) {
      // If the initial max unidirectional stream limit is not at least three,
      // we cannot actually use it since we need to create the control streams.
      Debug(&session(),
            "Cannot start HTTP/3 application. Initial max "
            "unidirectional streams is too low");
      return false;
    }

    if (session().is_server()) {
      nghttp3_conn_set_max_client_streams_bidi(
          *this, params->initial_max_streams_bidi);
    }

    return CreateAndBindControlStreams();
  }

  bool ReceiveStreamData(Stream* stream,
                         const uint8_t* data,
                         size_t datalen,
                         Stream::ReceiveDataFlags flags) override {
    Debug(&session(), "HTTP/3 application received %zu bytes of data", datalen);
    ssize_t nread = nghttp3_conn_read_stream(
        *this, stream->id(), data, datalen, flags.fin ? 1 : 0);

    if (nread < 0) {
      Debug(&session(),
            "HTTP/3 application failed to read stream data: %s",
            nghttp3_strerror(nread));
      return false;
    }

    Debug(&session(),
          "Extending stream and connection offset by %zd bytes",
          nread);
    session().ExtendStreamOffset(stream->id(), nread);
    session().ExtendOffset(nread);

    return true;
  }

  void AcknowledgeStreamData(Stream* stream, size_t datalen) override {
    Debug(&session(),
          "HTTP/3 application received acknowledgement for %zu bytes of data",
          datalen);
    CHECK_EQ(nghttp3_conn_add_ack_offset(*this, stream->id(), datalen), 0);
  }

  bool CanAddHeader(size_t current_count,
                    size_t current_headers_length,
                    size_t this_header_length) override {
    // We cannot add the header if we've either reached
    // * the max number of header pairs or
    // * the max number of header bytes
    bool answer = (current_count < options_.max_header_pairs) &&
                  (current_headers_length + this_header_length) <=
                      options_.max_header_length;
    IF_QUIC_DEBUG(env()) {
      if (answer) {
        Debug(&session(), "HTTP/3 application can add header");
      } else {
        Debug(&session(), "HTTP/3 application cannot add header");
      }
    }
    return answer;
  }

  void BlockStream(int64_t id) override {
    nghttp3_conn_block_stream(*this, id);
    Application::BlockStream(id);
  }

  void ResumeStream(int64_t id) override {
    nghttp3_conn_resume_stream(*this, id);
    Application::ResumeStream(id);
  }

  void ExtendMaxStreams(EndpointLabel label,
                        Direction direction,
                        uint64_t max_streams) override {
    switch (label) {
      case EndpointLabel::LOCAL:
        return;
      case EndpointLabel::REMOTE: {
        switch (direction) {
          case Direction::BIDIRECTIONAL: {
            Debug(&session(),
                  "HTTP/3 application extending max bidi streams to %" PRIu64,
                  max_streams);
            ngtcp2_conn_extend_max_streams_bidi(
                session(), static_cast<size_t>(max_streams));
            break;
          }
          case Direction::UNIDIRECTIONAL: {
            Debug(&session(),
                  "HTTP/3 application extending max uni streams to %" PRIu64,
                  max_streams);
            ngtcp2_conn_extend_max_streams_uni(
                session(), static_cast<size_t>(max_streams));
            break;
          }
        }
      }
    }
  }

  void ExtendMaxStreamData(Stream* stream, uint64_t max_data) override {
    Debug(&session(),
          "HTTP/3 application extending max stream data to %" PRIu64,
          max_data);
    nghttp3_conn_unblock_stream(*this, stream->id());
  }

  void CollectSessionTicketAppData(
      SessionTicket::AppData* app_data) const override {
    // TODO(@jasnell): There's currently nothing to store but there may be
    // later.
  }

  SessionTicket::AppData::Status ExtractSessionTicketAppData(
      const SessionTicket::AppData& app_data,
      SessionTicket::AppData::Source::Flag flag) override {
    // There's currently nothing stored here but we might do so later.
    return flag == SessionTicket::AppData::Source::Flag::STATUS_RENEW
               ? SessionTicket::AppData::Status::TICKET_USE_RENEW
               : SessionTicket::AppData::Status::TICKET_USE;
  }

  void StreamClose(Stream* stream, QuicError error = QuicError()) override {
    Debug(
        &session(), "HTTP/3 application closing stream %" PRIi64, stream->id());
    uint64_t code = NGHTTP3_H3_NO_ERROR;
    if (error) {
      CHECK_EQ(error.type(), QuicError::Type::APPLICATION);
      code = error.code();
    }

    int rv = nghttp3_conn_close_stream(*this, stream->id(), code);
    // If the call is successful, Http3Application::OnStreamClose callback will
    // be invoked when the stream is ready to be closed. We'll handle destroying
    // the actual Stream object there.
    if (rv == 0) return;

    if (rv == NGHTTP3_ERR_STREAM_NOT_FOUND) {
      ExtendMaxStreams(EndpointLabel::REMOTE, stream->direction(), 1);
      return;
    }

    session().SetLastError(
        QuicError::ForApplication(nghttp3_err_infer_quic_app_error_code(rv)));
    session().Close();
  }

  void StreamReset(Stream* stream,
                   uint64_t final_size,
                   QuicError error) override {
    // We are shutting down the readable side of the local stream here.
    Debug(&session(),
          "HTTP/3 application resetting stream %" PRIi64,
          stream->id());
    int rv = nghttp3_conn_shutdown_stream_read(*this, stream->id());
    if (rv == 0) {
      stream->ReceiveStreamReset(final_size, error);
      return;
    }

    session().SetLastError(
        QuicError::ForApplication(nghttp3_err_infer_quic_app_error_code(rv)));
    session().Close();
  }

  void StreamStopSending(Stream* stream, QuicError error) override {
    Application::StreamStopSending(stream, error);
  }

  bool SendHeaders(const Stream& stream,
                   HeadersKind kind,
                   const v8::Local<v8::Array>& headers,
                   HeadersFlags flags = HeadersFlags::NONE) override {
    Session::SendPendingDataScope send_scope(&session());
    Http3Headers nva(env(), headers);

    switch (kind) {
      case HeadersKind::HINTS: {
        if (!session().is_server()) {
          // Client side cannot send hints
          return false;
        }
        Debug(&session(),
              "Submitting early hints for stream " PRIi64,
              stream.id());
        return nghttp3_conn_submit_info(
                   *this, stream.id(), nva.data(), nva.length()) == 0;
        break;
      }
      case HeadersKind::INITIAL: {
        static constexpr nghttp3_data_reader reader = {on_read_data_callback};
        const nghttp3_data_reader* reader_ptr = nullptr;

        // If the terminal flag is set, that means that we know we're only
        // sending headers and no body and the stream writable side should be
        // closed immediately because there is no nghttp3_data_reader provided.
        if (flags != HeadersFlags::TERMINAL) reader_ptr = &reader;

        if (session().is_server()) {
          // If this is a server, we're submitting a response...
          Debug(&session(),
                "Submitting response headers for stream " PRIi64,
                stream.id());
          return nghttp3_conn_submit_response(
              *this, stream.id(), nva.data(), nva.length(), reader_ptr);
        } else {
          // Otherwise we're submitting a request...
          Debug(&session(),
                "Submitting request headers for stream " PRIi64,
                stream.id());
          return nghttp3_conn_submit_request(*this,
                                             stream.id(),
                                             nva.data(),
                                             nva.length(),
                                             reader_ptr,
                                             const_cast<Stream*>(&stream)) == 0;
        }
        break;
      }
      case HeadersKind::TRAILING: {
        return nghttp3_conn_submit_trailers(
                   *this, stream.id(), nva.data(), nva.length()) == 0;
        break;
      }
    }

    return false;
  }

  StreamPriority GetStreamPriority(const Stream& stream) override {
    nghttp3_pri pri;
    if (nghttp3_conn_get_stream_priority(*this, &pri, stream.id()) == 0) {
      // TODO(@jasnell): Support the incremental flag
      switch (pri.urgency) {
        case NGHTTP3_URGENCY_HIGH:
          return StreamPriority::HIGH;
        case NGHTTP3_URGENCY_LOW:
          return StreamPriority::LOW;
        default:
          return StreamPriority::DEFAULT;
      }
    }
    return StreamPriority::DEFAULT;
  }

  int GetStreamData(StreamData* data) override {
    ssize_t ret = 0;
    Debug(&session(), "HTTP/3 application getting stream data");
    if (conn_ && session().max_data_left()) {
      nghttp3_vec vec = *data;
      ret = nghttp3_conn_writev_stream(
          *this, &data->id, &data->fin, &vec, data->count);
      if (ret < 0) {
        return static_cast<int>(ret);
      } else {
        data->remaining = data->count = static_cast<size_t>(ret);
        if (data->id > 0) {
          data->stream = session().FindStream(data->id);
        }
      }
    }
    DCHECK_NOT_NULL(data->buf);
    return 0;
  }

  bool StreamCommit(StreamData* data, size_t datalen) override {
    Debug(&session(),
          "HTTP/3 application committing stream %" PRIi64 " data %zu",
          data->id,
          datalen);
    int err = nghttp3_conn_add_write_offset(*this, data->id, datalen);
    if (err != 0) {
      session().SetLastError(QuicError::ForApplication(
          nghttp3_err_infer_quic_app_error_code(err)));
      return false;
    }
    return true;
  }

  bool ShouldSetFin(const StreamData& data) override {
    return data.id > -1 && !is_control_stream(data.id) && data.fin == 1;
  }

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(Http3Application)
  SET_SELF_SIZE(Http3Application)

 private:
  inline operator nghttp3_conn*() const {
    DCHECK_NOT_NULL(conn_.get());
    return conn_.get();
  }

  bool CreateAndBindControlStreams() {
    Debug(&session(), "Creating and binding HTTP/3 control streams");
    auto stream = session().OpenStream(Direction::UNIDIRECTIONAL);
    if (!stream) return false;
    if (nghttp3_conn_bind_control_stream(*this, stream->id()) != 0) {
      return false;
    }

    auto enc_stream = session().OpenStream(Direction::UNIDIRECTIONAL);
    if (!enc_stream) return false;

    auto dec_stream = session().OpenStream(Direction::UNIDIRECTIONAL);
    if (!dec_stream) return false;

    bool bound = nghttp3_conn_bind_qpack_streams(
                     *this, enc_stream->id(), dec_stream->id()) == 0;
    control_stream_id_ = stream->id();
    qpack_enc_stream_id_ = enc_stream->id();
    qpack_dec_stream_id_ = dec_stream->id();
    return bound;
  }

  inline bool is_control_stream(int64_t id) const {
    return id == control_stream_id_ || id == qpack_dec_stream_id_ ||
           id == qpack_enc_stream_id_;
  }

  bool is_destroyed() const { return session().is_destroyed(); }

  Http3ConnectionPointer InitializeConnection() {
    nghttp3_conn* conn = nullptr;
    nghttp3_settings settings = options_;
    if (session().is_server()) {
      CHECK_EQ(nghttp3_conn_server_new(
                   &conn, &kCallbacks, &settings, &allocator_, this),
               0);
    } else {
      CHECK_EQ(nghttp3_conn_client_new(
                   &conn, &kCallbacks, &settings, &allocator_, this),
               0);
    }
    return Http3ConnectionPointer(conn);
  }

  void OnStreamClose(Stream* stream, uint64_t app_error_code) {
    if (stream->is_destroyed()) return;
    Debug(&session(),
          "HTTP/3 application received stream close for stream %" PRIi64,
          stream->id());
    auto direction = stream->direction();
    stream->Destroy(QuicError::ForApplication(app_error_code));
    ExtendMaxStreams(EndpointLabel::REMOTE, direction, 1);
  }

  void OnReceiveData(Stream* stream, const nghttp3_vec& vec) {
    if (stream->is_destroyed()) return;
    Debug(&session(), "HTTP/3 application received %zu bytes of data", vec.len);
    stream->ReceiveData(vec.base, vec.len, Stream::ReceiveDataFlags{});
  }

  void OnDeferredConsume(Stream* stream, size_t consumed) {
    auto& sess = session();
    Debug(
        &session(), "HTTP/3 application deferred consume %zu bytes", consumed);
    if (!stream->is_destroyed()) {
      sess.ExtendStreamOffset(stream->id(), consumed);
    }
    sess.ExtendOffset(consumed);
  }

  void OnBeginHeaders(Stream* stream) {
    if (stream->is_destroyed()) return;
    Debug(&session(),
          "HTTP/3 application beginning initial block of headers for stream "
          "%" PRIi64,
          stream->id());
    stream->BeginHeaders(HeadersKind::INITIAL);
  }

  void OnReceiveHeader(Stream* stream, Http3Header&& header) {
    if (stream->is_destroyed()) return;
    if (header.name() == ":status") {
      if (header.value()[0] == '1') {
        Debug(
            &session(),
            "HTTP/3 application switching to hints headers for stream %" PRIi64,
            stream->id());
        stream->set_headers_kind(HeadersKind::HINTS);
      }
    }
    stream->AddHeader(std::move(header));
  }

  void OnEndHeaders(Stream* stream, int fin) {
    Debug(&session(),
          "HTTP/3 application received end of headers for stream %" PRIi64,
          stream->id());
    stream->EmitHeaders();
    if (fin != 0) {
      // The stream is done. There's no more data to receive!
      Debug(&session(), "Headers are final for stream %" PRIi64, stream->id());
      OnEndStream(stream);
    }
  }

  void OnBeginTrailers(Stream* stream) {
    if (stream->is_destroyed()) return;
    Debug(&session(),
          "HTTP/3 application beginning block of trailers for stream %" PRIi64,
          stream->id());
    stream->BeginHeaders(HeadersKind::TRAILING);
  }

  void OnReceiveTrailer(Stream* stream, Http3Header&& header) {
    stream->AddHeader(header);
  }

  void OnEndTrailers(Stream* stream, int fin) {
    if (stream->is_destroyed()) return;
    Debug(&session(),
          "HTTP/3 application received end of trailers for stream %" PRIi64,
          stream->id());
    stream->EmitHeaders();
    if (fin != 0) {
      Debug(&session(), "Trailers are final for stream %" PRIi64, stream->id());
      // The stream is done. There's no more data to receive!
      stream->ReceiveData(nullptr,
                          0,
                          Stream::ReceiveDataFlags{/* .fin = */ true,
                                                   /* .early = */ false});
    }
  }

  void OnEndStream(Stream* stream) {
    if (stream->is_destroyed()) return;
    Debug(&session(),
          "HTTP/3 application received end of stream for stream %" PRIi64,
          stream->id());
    stream->ReceiveData(nullptr,
                        0,
                        Stream::ReceiveDataFlags{/* .fin = */ true,
                                                 /* .early = */ false});
  }

  void OnStopSending(Stream* stream, uint64_t app_error_code) {
    if (stream->is_destroyed()) return;
    Debug(&session(),
          "HTTP/3 application received stop sending for stream %" PRIi64,
          stream->id());
    stream->ReceiveStopSending(QuicError::ForApplication(app_error_code));
  }

  void OnResetStream(Stream* stream, uint64_t app_error_code) {
    if (stream->is_destroyed()) return;
    Debug(&session(),
          "HTTP/3 application received reset stream for stream %" PRIi64,
          stream->id());
    stream->ReceiveStreamReset(0, QuicError::ForApplication(app_error_code));
  }

  void OnShutdown() {
    // This callback is invoked when we receive a request to gracefully shutdown
    // the http3 connection. For client, the id is the stream id of a client
    // initiated stream. For server, the id is the stream id of a server
    // initiated stream. Once received, the other side is guaranteed not to
    // process any more data.

    // On the client side, if id is equal to NGHTTP3_SHUTDOWN_NOTICE_STREAM_ID,
    // or on the server if the id is equal to NGHTTP3_SHUTDOWN_NOTICE_PUSH_ID,
    // then this is a request to begin a graceful shutdown.

    // This can be called multiple times but the id can only stay the same or
    // *decrease*.

    // TODO(@jasnell): Need to determine exactly how to handle.
    Debug(&session(), "HTTP/3 application received shutdown notice");
  }

  void OnReceiveSettings(const nghttp3_settings* settings) {
    options_.enable_connect_protocol = settings->enable_connect_protocol;
    options_.enable_datagrams = settings->h3_datagram;
    options_.max_field_section_size = settings->max_field_section_size;
    options_.qpack_blocked_streams = settings->qpack_blocked_streams;
    options_.qpack_encoder_max_dtable_capacity =
        settings->qpack_encoder_max_dtable_capacity;
    options_.qpack_max_dtable_capacity = settings->qpack_max_dtable_capacity;
    Debug(
        &session(), "HTTP/3 application received updated settings ", options_);
  }

  bool started_ = false;
  nghttp3_mem allocator_;
  Session::Application_Options options_;
  Http3ConnectionPointer conn_;
  int64_t control_stream_id_ = -1;
  int64_t qpack_dec_stream_id_ = -1;
  int64_t qpack_enc_stream_id_ = -1;

  // ==========================================================================
  // Static callbacks

  static Http3Application* From(nghttp3_conn* conn, void* user_data) {
    DCHECK_NOT_NULL(user_data);
    auto app = static_cast<Http3Application*>(user_data);
    DCHECK_EQ(conn, app->conn_.get());
    return app;
  }

  static Stream* From(int64_t stream_id, void* stream_user_data) {
    DCHECK_NOT_NULL(stream_user_data);
    auto stream = static_cast<Stream*>(stream_user_data);
    DCHECK_EQ(stream_id, stream->id());
    return stream;
  }

#define NGHTTP3_CALLBACK_SCOPE(name)                                           \
  auto name = From(conn, conn_user_data);                                      \
  if (name->is_destroyed()) [[unlikely]] {                                     \
    return NGHTTP3_ERR_CALLBACK_FAILURE;                                       \
  }                                                                            \
  NgHttp3CallbackScope scope(name->env());

  static nghttp3_ssize on_read_data_callback(nghttp3_conn* conn,
                                             int64_t stream_id,
                                             nghttp3_vec* vec,
                                             size_t veccnt,
                                             uint32_t* pflags,
                                             void* conn_user_data,
                                             void* stream_user_data) {
    return 0;
  }

  static int on_acked_stream_data(nghttp3_conn* conn,
                                  int64_t stream_id,
                                  uint64_t datalen,
                                  void* conn_user_data,
                                  void* stream_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    auto stream = From(stream_id, stream_user_data);
    if (stream == nullptr) return NGHTTP3_ERR_CALLBACK_FAILURE;
    app->AcknowledgeStreamData(stream, static_cast<size_t>(datalen));
    return NGTCP2_SUCCESS;
  }

  static int on_stream_close(nghttp3_conn* conn,
                             int64_t stream_id,
                             uint64_t app_error_code,
                             void* conn_user_data,
                             void* stream_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    auto stream = From(stream_id, stream_user_data);
    if (stream == nullptr) return NGHTTP3_ERR_CALLBACK_FAILURE;
    app->OnStreamClose(stream, app_error_code);
    return NGTCP2_SUCCESS;
  }

  static int on_receive_data(nghttp3_conn* conn,
                             int64_t stream_id,
                             const uint8_t* data,
                             size_t datalen,
                             void* conn_user_data,
                             void* stream_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    auto stream = From(stream_id, stream_user_data);
    if (stream == nullptr) return NGHTTP3_ERR_CALLBACK_FAILURE;
    app->OnReceiveData(stream,
                       nghttp3_vec{const_cast<uint8_t*>(data), datalen});
    return NGTCP2_SUCCESS;
  }

  static int on_deferred_consume(nghttp3_conn* conn,
                                 int64_t stream_id,
                                 size_t consumed,
                                 void* conn_user_data,
                                 void* stream_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    auto stream = From(stream_id, stream_user_data);
    if (stream == nullptr) return NGHTTP3_ERR_CALLBACK_FAILURE;
    app->OnDeferredConsume(stream, consumed);
    return NGTCP2_SUCCESS;
  }

  static int on_begin_headers(nghttp3_conn* conn,
                              int64_t stream_id,
                              void* conn_user_data,
                              void* stream_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    auto stream = From(stream_id, stream_user_data);
    if (stream == nullptr) return NGHTTP3_ERR_CALLBACK_FAILURE;
    app->OnBeginHeaders(stream);
    return NGTCP2_SUCCESS;
  }

  static int on_receive_header(nghttp3_conn* conn,
                               int64_t stream_id,
                               int32_t token,
                               nghttp3_rcbuf* name,
                               nghttp3_rcbuf* value,
                               uint8_t flags,
                               void* conn_user_data,
                               void* stream_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    auto stream = From(stream_id, stream_user_data);
    if (stream == nullptr) return NGHTTP3_ERR_CALLBACK_FAILURE;
    if (Http3Header::IsZeroLength(token, name, value)) return NGTCP2_SUCCESS;
    app->OnReceiveHeader(stream,
                         Http3Header(app->env(), token, name, value, flags));
    return NGTCP2_SUCCESS;
  }

  static int on_end_headers(nghttp3_conn* conn,
                            int64_t stream_id,
                            int fin,
                            void* conn_user_data,
                            void* stream_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    auto stream = From(stream_id, stream_user_data);
    if (stream == nullptr) return NGHTTP3_ERR_CALLBACK_FAILURE;
    app->OnEndHeaders(stream, fin);
    return NGTCP2_SUCCESS;
  }

  static int on_begin_trailers(nghttp3_conn* conn,
                               int64_t stream_id,
                               void* conn_user_data,
                               void* stream_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    auto stream = From(stream_id, stream_user_data);
    if (stream == nullptr) return NGHTTP3_ERR_CALLBACK_FAILURE;
    app->OnBeginTrailers(stream);
    return NGTCP2_SUCCESS;
  }

  static int on_receive_trailer(nghttp3_conn* conn,
                                int64_t stream_id,
                                int32_t token,
                                nghttp3_rcbuf* name,
                                nghttp3_rcbuf* value,
                                uint8_t flags,
                                void* conn_user_data,
                                void* stream_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    auto stream = From(stream_id, stream_user_data);
    if (stream == nullptr) return NGHTTP3_ERR_CALLBACK_FAILURE;
    if (Http3Header::IsZeroLength(token, name, value)) return NGTCP2_SUCCESS;
    app->OnReceiveTrailer(stream,
                          Http3Header(app->env(), token, name, value, flags));
    return NGTCP2_SUCCESS;
  }

  static int on_end_trailers(nghttp3_conn* conn,
                             int64_t stream_id,
                             int fin,
                             void* conn_user_data,
                             void* stream_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    auto stream = From(stream_id, stream_user_data);
    if (stream == nullptr) return NGHTTP3_ERR_CALLBACK_FAILURE;
    app->OnEndTrailers(stream, fin);
    return NGTCP2_SUCCESS;
  }

  static int on_end_stream(nghttp3_conn* conn,
                           int64_t stream_id,
                           void* conn_user_data,
                           void* stream_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    auto stream = From(stream_id, stream_user_data);
    if (stream == nullptr) return NGHTTP3_ERR_CALLBACK_FAILURE;
    app->OnEndStream(stream);
    return NGTCP2_SUCCESS;
  }

  static int on_stop_sending(nghttp3_conn* conn,
                             int64_t stream_id,
                             uint64_t app_error_code,
                             void* conn_user_data,
                             void* stream_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    auto stream = From(stream_id, stream_user_data);
    if (stream == nullptr) return NGHTTP3_ERR_CALLBACK_FAILURE;
    app->OnStopSending(stream, app_error_code);
    return NGTCP2_SUCCESS;
  }

  static int on_reset_stream(nghttp3_conn* conn,
                             int64_t stream_id,
                             uint64_t app_error_code,
                             void* conn_user_data,
                             void* stream_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    auto stream = From(stream_id, stream_user_data);
    if (stream == nullptr) return NGHTTP3_ERR_CALLBACK_FAILURE;
    app->OnResetStream(stream, app_error_code);
    return NGTCP2_SUCCESS;
  }

  static int on_shutdown(nghttp3_conn* conn, int64_t id, void* conn_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    app->OnShutdown();
    return NGTCP2_SUCCESS;
  }

  static int on_receive_settings(nghttp3_conn* conn,
                                 const nghttp3_settings* settings,
                                 void* conn_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    app->OnReceiveSettings(settings);
    return NGTCP2_SUCCESS;
  }

  static constexpr nghttp3_callbacks kCallbacks = {on_acked_stream_data,
                                                   on_stream_close,
                                                   on_receive_data,
                                                   on_deferred_consume,
                                                   on_begin_headers,
                                                   on_receive_header,
                                                   on_end_headers,
                                                   on_begin_trailers,
                                                   on_receive_trailer,
                                                   on_end_trailers,
                                                   on_stop_sending,
                                                   on_end_stream,
                                                   on_reset_stream,
                                                   on_shutdown,
                                                   on_receive_settings};
};
}  // namespace

std::unique_ptr<Session::Application> createHttp3Application(
    Session* session, const Session::Application_Options& options) {
  return std::make_unique<Http3Application>(session, options);
}

}  // namespace node::quic

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
