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

namespace node {

using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Local;
using v8::Object;
using v8::ObjectTemplate;
using v8::Value;

namespace quic {

// ============================================================================

bool Http3Application::HasInstance(Environment* env, Local<Value> value) {
  return GetConstructorTemplate(env)->HasInstance(value);
}

Local<FunctionTemplate> Http3Application::GetConstructorTemplate(
    Environment* env) {
  auto& state = BindingData::Get(env);
  auto tmpl = state.http3application_constructor_template();
  if (tmpl.IsEmpty()) {
    auto isolate = env->isolate();
    tmpl = NewFunctionTemplate(isolate, New);
    tmpl->SetClassName(state.http3application_string());
    tmpl->InstanceTemplate()->SetInternalFieldCount(kInternalFieldCount);
    state.set_http3application_constructor_template(tmpl);
  }
  return tmpl;
}

void Http3Application::InitPerIsolate(IsolateData* isolate_data,
                                      Local<ObjectTemplate> target) {
  // TODO(@jasnell): Implement the per-isolate state
}

void Http3Application::InitPerContext(Realm* realm, Local<Object> target) {
  SetConstructorFunction(realm->context(),
                         target,
                         "Http3Application",
                         GetConstructorTemplate(realm->env()));
}

void Http3Application::RegisterExternalReferences(
    ExternalReferenceRegistry* registry) {
  registry->Register(New);
}

Http3Application::Http3Application(Environment* env,
                                   Local<Object> object,
                                   const Session::Application::Options& options)
    : ApplicationProvider(env, object), options_(options) {
  MakeWeak();
}

void Http3Application::New(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(args.IsConstructCall());

  Local<Object> obj;
  if (!GetConstructorTemplate(env)
           ->InstanceTemplate()
           ->NewInstance(env->context())
           .ToLocal(&obj)) {
    return;
  }

  Session::Application::Options options;
  if (!args[0]->IsUndefined() &&
      !Session::Application::Options::From(env, args[0]).To(&options)) {
    return;
  }

  if (auto app = MakeBaseObject<Http3Application>(env, obj, options)) {
    args.GetReturnValue().Set(app->object());
  }
}

void Http3Application::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("options", options_);
}

std::string Http3Application::ToString() const {
  DebugIndentScope indent;
  auto prefix = indent.Prefix();
  std::string res("{");
  res += prefix + "options: " + options_.ToString();
  res += indent.Close();
  return res;
}

// ============================================================================

struct Http3HeadersTraits {
  using nv_t = nghttp3_nv;
};

struct Http3RcBufferPointerTraits {
  using rcbuf_t = nghttp3_rcbuf;
  using vector_t = nghttp3_vec;

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
class Http3ApplicationImpl final : public Session::Application {
 public:
  Http3ApplicationImpl(Session* session, const Options& options)
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
    if (params == nullptr) [[unlikely]] {
      // The params are not available yet. Cannot start.
      Debug(&session(),
            "Cannot start HTTP/3 application yet. No remote transport params");
      return false;
    }

    if (params->initial_max_streams_uni < 3) {
      // HTTP3 requires 3 unidirectional control streams to be opened in each
      // direction in additional to the bidirectional streams that are used to
      // actually carry request and response payload back and forth.
      // See:
      // https://nghttp2.org/nghttp3/programmers-guide.html#binding-control-streams
      Debug(&session(),
            "Cannot start HTTP/3 application. Initial max "
            "unidirectional streams [%zu] is too low. Must be at least 3",
            params->initial_max_streams_uni);
      return false;
    }

    // If this is a server session, then set the maximum number of
    // bidirectional streams that can be created. This determines the number
    // of requests that the client can actually created.
    if (session().is_server()) {
      nghttp3_conn_set_max_client_streams_bidi(
          *this, params->initial_max_streams_bidi);
    }

    Debug(&session(), "Creating and binding HTTP/3 control streams");
    bool ret =
        ngtcp2_conn_open_uni_stream(session(), &control_stream_id_, nullptr) ==
            0 &&
        ngtcp2_conn_open_uni_stream(
            session(), &qpack_enc_stream_id_, nullptr) == 0 &&
        ngtcp2_conn_open_uni_stream(
            session(), &qpack_dec_stream_id_, nullptr) == 0 &&
        nghttp3_conn_bind_control_stream(*this, control_stream_id_) == 0 &&
        nghttp3_conn_bind_qpack_streams(
            *this, qpack_enc_stream_id_, qpack_dec_stream_id_) == 0;

    if (env()->enabled_debug_list()->enabled(DebugCategory::QUIC) && ret) {
      Debug(&session(),
            "Created and bound control stream %" PRIi64,
            control_stream_id_);
      Debug(&session(),
            "Created and bound qpack enc stream %" PRIi64,
            qpack_enc_stream_id_);
      Debug(&session(),
            "Created and bound qpack dec streams %" PRIi64,
            qpack_dec_stream_id_);
    }

    return ret;
  }

  bool ReceiveStreamData(int64_t stream_id,
                         const uint8_t* data,
                         size_t datalen,
                         const Stream::ReceiveDataFlags& flags,
                         void* unused) override {
    Debug(&session(),
          "HTTP/3 application received %zu bytes of data "
          "on stream %" PRIi64 ". Is final? %d",
          datalen,
          stream_id,
          flags.fin);

    ssize_t nread = nghttp3_conn_read_stream(
        *this, stream_id, data, datalen, flags.fin ? 1 : 0);

    if (nread < 0) {
      Debug(&session(),
            "HTTP/3 application failed to read stream data: %s",
            nghttp3_strerror(nread));
      return false;
    }

    if (nread > 0) {
      Debug(&session(),
            "Extending stream and connection offset by %zd bytes",
            nread);
      session().ExtendStreamOffset(stream_id, nread);
      session().ExtendOffset(nread);
    }

    return true;
  }

  bool AcknowledgeStreamData(int64_t stream_id, size_t datalen) override {
    Debug(&session(),
          "HTTP/3 application received acknowledgement for %zu bytes of data "
          "on stream %" PRIi64,
          datalen,
          stream_id);
    return nghttp3_conn_add_ack_offset(*this, stream_id, datalen) == 0;
  }

  bool CanAddHeader(size_t current_count,
                    size_t current_headers_length,
                    size_t this_header_length) override {
    // We cannot add the header if we've either reached
    // * the max number of header pairs or
    // * the max number of header bytes
    return (current_count < options_.max_header_pairs) &&
           (current_headers_length + this_header_length) <=
               options_.max_header_length;
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
                  "HTTP/3 application extending max bidi streams by %" PRIu64,
                  max_streams);
            ngtcp2_conn_extend_max_streams_bidi(
                session(), static_cast<size_t>(max_streams));
            break;
          }
          case Direction::UNIDIRECTIONAL: {
            Debug(&session(),
                  "HTTP/3 application extending max uni streams by %" PRIu64,
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

  void StreamClose(Stream* stream, QuicError&& error = QuicError()) override {
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
                   QuicError&& error = QuicError()) override {
    // We are shutting down the readable side of the local stream here.
    Debug(&session(),
          "HTTP/3 application resetting stream %" PRIi64,
          stream->id());
    int rv = nghttp3_conn_shutdown_stream_read(*this, stream->id());
    if (rv == 0) {
      stream->ReceiveStreamReset(final_size, std::move(error));
      return;
    }

    session().SetLastError(
        QuicError::ForApplication(nghttp3_err_infer_quic_app_error_code(rv)));
    session().Close();
  }

  void StreamStopSending(Stream* stream,
                         QuicError&& error = QuicError()) override {
    Application::StreamStopSending(stream, std::move(error));
  }

  bool SendHeaders(const Stream& stream,
                   HeadersKind kind,
                   const Local<v8::Array>& headers,
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
              "Submitting %" PRIu64 " early hints for stream %" PRIu64,
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
        if (flags != HeadersFlags::TERMINAL) {
          reader_ptr = &reader;
        }

        if (session().is_server()) {
          // If this is a server, we're submitting a response...
          Debug(&session(),
                "Submitting %" PRIu64 " response headers for stream %" PRIu64,
                nva.length(),
                stream.id());
          return nghttp3_conn_submit_response(
              *this, stream.id(), nva.data(), nva.length(), reader_ptr);
        } else {
          // Otherwise we're submitting a request...
          Debug(&session(),
                "Submitting %" PRIu64 " request headers for stream %" PRIu64,
                nva.length(),
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
        Debug(&session(),
              "Submitting %" PRIu64 " trailing headers for stream %" PRIu64,
              nva.length(),
              stream.id());
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
    data->count = kMaxVectorCount;
    ssize_t ret = 0;
    Debug(&session(), "HTTP/3 application getting stream data");
    if (conn_ && session().max_data_left()) {
      ret = nghttp3_conn_writev_stream(
          *this, &data->id, &data->fin, *data, data->count);
      // A negative return value indicates an error.
      if (ret < 0) {
        return static_cast<int>(ret);
      }

      data->count = static_cast<size_t>(ret);
      if (data->id > 0 && data->id != control_stream_id_ &&
          data->id != qpack_dec_stream_id_ &&
          data->id != qpack_enc_stream_id_) {
        data->stream = session().FindStream(data->id);
      }
    }

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
  SET_MEMORY_INFO_NAME(Http3ApplicationImpl)
  SET_SELF_SIZE(Http3ApplicationImpl)

 private:
  inline operator nghttp3_conn*() const {
    DCHECK_NOT_NULL(conn_.get());
    return conn_.get();
  }

  inline bool is_control_stream(int64_t id) const {
    return id == control_stream_id_ || id == qpack_dec_stream_id_ ||
           id == qpack_enc_stream_id_;
  }

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
    if (app_error_code != NGHTTP3_H3_NO_ERROR) {
      Debug(&session(),
            "HTTP/3 application received stream close for stream %" PRIi64
            " with code %" PRIu64,
            stream->id(),
            app_error_code);
    }
    auto direction = stream->direction();
    stream->Destroy(QuicError::ForApplication(app_error_code));
    ExtendMaxStreams(EndpointLabel::REMOTE, direction, 1);
  }

  void OnBeginHeaders(int64_t stream_id) {
    auto stream = session().FindStream(stream_id);
    // If the stream does not exist or is destroyed, ignore!
    if (!stream) [[unlikely]]
      return;
    Debug(&session(),
          "HTTP/3 application beginning initial block of headers for stream "
          "%" PRIi64,
          stream_id);
    stream->BeginHeaders(HeadersKind::INITIAL);
  }

  void OnReceiveHeader(int64_t stream_id, Http3Header&& header) {
    auto stream = session().FindStream(stream_id);

    if (!stream) [[unlikely]]
      return;
    if (header.name() == ":status" && header.value()[0] == '1') {
      Debug(&session(),
            "HTTP/3 application switching to hints headers for stream %" PRIi64,
            stream->id());
      stream->set_headers_kind(HeadersKind::HINTS);
    }
    IF_QUIC_DEBUG(env()) {
      Debug(&session(),
            "Received header \"%s: %s\"",
            header.name(),
            header.value());
    }
    stream->AddHeader(std::move(header));
  }

  void OnEndHeaders(int64_t stream_id, int fin) {
    auto stream = session().FindStream(stream_id);
    if (!stream) [[unlikely]]
      return;
    Debug(&session(),
          "HTTP/3 application received end of headers for stream %" PRIi64,
          stream_id);
    stream->EmitHeaders();
    if (fin) {
      // The stream is done. There's no more data to receive!
      Debug(&session(), "Headers are final for stream %" PRIi64, stream_id);
      Stream::ReceiveDataFlags flags{
          .fin = true,
          .early = false,
      };
      stream->ReceiveData(nullptr, 0, flags);
    }
  }

  void OnBeginTrailers(int64_t stream_id) {
    auto stream = session().FindStream(stream_id);
    if (!stream) [[unlikely]]
      return;
    Debug(&session(),
          "HTTP/3 application beginning block of trailers for stream %" PRIi64,
          stream_id);
    stream->BeginHeaders(HeadersKind::TRAILING);
  }

  void OnReceiveTrailer(int64_t stream_id, Http3Header&& header) {
    auto stream = session().FindStream(stream_id);
    if (!stream) [[unlikely]]
      return;
    IF_QUIC_DEBUG(env()) {
      Debug(&session(),
            "Received header \"%s: %s\"",
            header.name(),
            header.value());
    }
    stream->AddHeader(header);
  }

  void OnEndTrailers(int64_t stream_id, int fin) {
    auto stream = session().FindStream(stream_id);
    if (!stream) [[unlikely]]
      return;
    Debug(&session(),
          "HTTP/3 application received end of trailers for stream %" PRIi64,
          stream_id);
    stream->EmitHeaders();
    if (fin) {
      Debug(&session(), "Trailers are final for stream %" PRIi64, stream_id);
      Stream::ReceiveDataFlags flags{
          .fin = true,
          .early = false,
      };
      stream->ReceiveData(nullptr, 0, flags);
    }
  }

  void OnEndStream(int64_t stream_id) {
    auto stream = session().FindStream(stream_id);
    if (!stream) [[unlikely]]
      return;
    Debug(&session(),
          "HTTP/3 application received end of stream for stream %" PRIi64,
          stream_id);
    Stream::ReceiveDataFlags flags{
        .fin = true,
        .early = false,
    };
    stream->ReceiveData(nullptr, 0, flags);
  }

  void OnStopSending(int64_t stream_id, uint64_t app_error_code) {
    auto stream = session().FindStream(stream_id);
    if (!stream) [[unlikely]]
      return;
    Debug(&session(),
          "HTTP/3 application received stop sending for stream %" PRIi64,
          stream_id);
    stream->ReceiveStopSending(QuicError::ForApplication(app_error_code));
  }

  void OnResetStream(int64_t stream_id, uint64_t app_error_code) {
    auto stream = session().FindStream(stream_id);
    if (!stream) [[unlikely]]
      return;
    Debug(&session(),
          "HTTP/3 application received reset stream for stream %" PRIi64,
          stream_id);
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
    Debug(&session(),
          "HTTP/3 application received updated settings: %s",
          options_);
  }

  bool started_ = false;
  nghttp3_mem allocator_;
  Options options_;
  Http3ConnectionPointer conn_;
  int64_t control_stream_id_ = -1;
  int64_t qpack_dec_stream_id_ = -1;
  int64_t qpack_enc_stream_id_ = -1;

  // ==========================================================================
  // Static callbacks

  static Http3ApplicationImpl* From(nghttp3_conn* conn, void* user_data) {
    DCHECK_NOT_NULL(user_data);
    auto app = static_cast<Http3ApplicationImpl*>(user_data);
    DCHECK_EQ(conn, app->conn_.get());
    return app;
  }

  static BaseObjectWeakPtr<Stream> FindOrCreateStream(nghttp3_conn* conn,
                                                      Session* session,
                                                      int64_t stream_id) {
    if (auto stream = session->FindStream(stream_id)) {
      return stream;
    }
    if (auto stream = session->CreateStream(stream_id)) {
      return stream;
    }
    return {};
  }

#define NGHTTP3_CALLBACK_SCOPE(name)                                           \
  auto ptr = From(conn, conn_user_data);                                       \
  CHECK_NOT_NULL(ptr);                                                         \
  auto& name = *ptr;                                                           \
  NgHttp3CallbackScope scope(name.env());

  static nghttp3_ssize on_read_data_callback(nghttp3_conn* conn,
                                             int64_t stream_id,
                                             nghttp3_vec* vec,
                                             size_t veccnt,
                                             uint32_t* pflags,
                                             void* conn_user_data,
                                             void* stream_user_data) {
    return NGTCP2_SUCCESS;
  }

  static int on_acked_stream_data(nghttp3_conn* conn,
                                  int64_t stream_id,
                                  uint64_t datalen,
                                  void* conn_user_data,
                                  void* stream_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    return app.AcknowledgeStreamData(stream_id, static_cast<size_t>(datalen))
               ? NGTCP2_SUCCESS
               : NGHTTP3_ERR_CALLBACK_FAILURE;
  }

  static int on_stream_close(nghttp3_conn* conn,
                             int64_t stream_id,
                             uint64_t app_error_code,
                             void* conn_user_data,
                             void* stream_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    if (auto stream = app.session().FindStream(stream_id)) {
      app.OnStreamClose(stream.get(), app_error_code);
    }
    return NGTCP2_SUCCESS;
  }

  static int on_receive_data(nghttp3_conn* conn,
                             int64_t stream_id,
                             const uint8_t* data,
                             size_t datalen,
                             void* conn_user_data,
                             void* stream_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    // The on_receive_data callback will never be called for control streams,
    // so we know that if we get here, the data received is for a stream that
    // we know is for an HTTP payload.
    if (app.is_control_stream(stream_id)) [[unlikely]] {
      return NGHTTP3_ERR_CALLBACK_FAILURE;
    }
    auto& session = app.session();
    if (auto stream = FindOrCreateStream(conn, &session, stream_id))
        [[likely]] {
      stream->ReceiveData(data, datalen, Stream::ReceiveDataFlags{});
      return NGTCP2_SUCCESS;
    }
    return NGHTTP3_ERR_CALLBACK_FAILURE;
  }

  static int on_deferred_consume(nghttp3_conn* conn,
                                 int64_t stream_id,
                                 size_t consumed,
                                 void* conn_user_data,
                                 void* stream_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    auto& session = app.session();
    Debug(&session, "HTTP/3 application deferred consume %zu bytes", consumed);
    session.ExtendStreamOffset(stream_id, consumed);
    session.ExtendOffset(consumed);
    return NGTCP2_SUCCESS;
  }

  static int on_begin_headers(nghttp3_conn* conn,
                              int64_t stream_id,
                              void* conn_user_data,
                              void* stream_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    if (app.is_control_stream(stream_id)) [[unlikely]] {
      return NGHTTP3_ERR_CALLBACK_FAILURE;
    }
    app.OnBeginHeaders(stream_id);
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
    if (app.is_control_stream(stream_id)) [[unlikely]] {
      return NGHTTP3_ERR_CALLBACK_FAILURE;
    }
    if (Http3Header::IsZeroLength(token, name, value)) return NGTCP2_SUCCESS;
    app.OnReceiveHeader(stream_id,
                        Http3Header(app.env(), token, name, value, flags));
    return NGTCP2_SUCCESS;
  }

  static int on_end_headers(nghttp3_conn* conn,
                            int64_t stream_id,
                            int fin,
                            void* conn_user_data,
                            void* stream_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    if (app.is_control_stream(stream_id)) [[unlikely]] {
      return NGHTTP3_ERR_CALLBACK_FAILURE;
    }
    app.OnEndHeaders(stream_id, fin);
    return NGTCP2_SUCCESS;
  }

  static int on_begin_trailers(nghttp3_conn* conn,
                               int64_t stream_id,
                               void* conn_user_data,
                               void* stream_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    if (app.is_control_stream(stream_id)) [[unlikely]] {
      return NGHTTP3_ERR_CALLBACK_FAILURE;
    }
    app.OnBeginTrailers(stream_id);
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
    if (app.is_control_stream(stream_id)) [[unlikely]] {
      return NGHTTP3_ERR_CALLBACK_FAILURE;
    }
    if (Http3Header::IsZeroLength(token, name, value)) return NGTCP2_SUCCESS;
    app.OnReceiveTrailer(stream_id,
                         Http3Header(app.env(), token, name, value, flags));
    return NGTCP2_SUCCESS;
  }

  static int on_end_trailers(nghttp3_conn* conn,
                             int64_t stream_id,
                             int fin,
                             void* conn_user_data,
                             void* stream_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    if (app.is_control_stream(stream_id)) [[unlikely]] {
      return NGHTTP3_ERR_CALLBACK_FAILURE;
    }
    app.OnEndTrailers(stream_id, fin);
    return NGTCP2_SUCCESS;
  }

  static int on_end_stream(nghttp3_conn* conn,
                           int64_t stream_id,
                           void* conn_user_data,
                           void* stream_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    if (app.is_control_stream(stream_id)) [[unlikely]] {
      return NGHTTP3_ERR_CALLBACK_FAILURE;
    }
    app.OnEndStream(stream_id);
    return NGTCP2_SUCCESS;
  }

  static int on_stop_sending(nghttp3_conn* conn,
                             int64_t stream_id,
                             uint64_t app_error_code,
                             void* conn_user_data,
                             void* stream_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    if (app.is_control_stream(stream_id)) [[unlikely]] {
      return NGHTTP3_ERR_CALLBACK_FAILURE;
    }
    app.OnStopSending(stream_id, app_error_code);
    return NGTCP2_SUCCESS;
  }

  static int on_reset_stream(nghttp3_conn* conn,
                             int64_t stream_id,
                             uint64_t app_error_code,
                             void* conn_user_data,
                             void* stream_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    if (app.is_control_stream(stream_id)) [[unlikely]] {
      return NGHTTP3_ERR_CALLBACK_FAILURE;
    }
    app.OnResetStream(stream_id, app_error_code);
    return NGTCP2_SUCCESS;
  }

  static int on_shutdown(nghttp3_conn* conn, int64_t id, void* conn_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    app.OnShutdown();
    return NGTCP2_SUCCESS;
  }

  static int on_receive_settings(nghttp3_conn* conn,
                                 const nghttp3_settings* settings,
                                 void* conn_user_data) {
    NGHTTP3_CALLBACK_SCOPE(app);
    app.OnReceiveSettings(settings);
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

std::unique_ptr<Session::Application> Http3Application::Create(
    Session* session) {
  Debug(session, "Selecting HTTP/3 application");
  return std::make_unique<Http3ApplicationImpl>(session, options_);
}

}  // namespace quic
}  // namespace node

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
