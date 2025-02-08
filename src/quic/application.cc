#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC

#include "application.h"
#include <async_wrap-inl.h>
#include <debug_utils-inl.h>
#include <nghttp3/nghttp3.h>
#include <ngtcp2/ngtcp2.h>
#include <node_bob.h>
#include <node_sockaddr-inl.h>
#include <uv.h>
#include <v8.h>
#include "defs.h"
#include "endpoint.h"
#include "http3.h"
#include "packet.h"
#include "session.h"

namespace node {

using v8::Just;
using v8::Local;
using v8::Maybe;
using v8::Nothing;
using v8::Object;
using v8::Value;

namespace quic {

// ============================================================================
// Session::Application_Options
const Session::Application_Options Session::Application_Options::kDefault = {};

Session::Application_Options::operator const nghttp3_settings() const {
  // In theory, Application::Options might contain options for more than just
  // HTTP/3. Here we extract only the properties that are relevant to HTTP/3.
  return nghttp3_settings{
      .max_field_section_size = max_field_section_size,
      .qpack_max_dtable_capacity =
          static_cast<size_t>(qpack_max_dtable_capacity),
      .qpack_encoder_max_dtable_capacity =
          static_cast<size_t>(qpack_encoder_max_dtable_capacity),
      .qpack_blocked_streams = static_cast<size_t>(qpack_blocked_streams),
      .enable_connect_protocol = enable_connect_protocol,
      .h3_datagram = enable_datagrams,
  };
}

std::string Session::Application_Options::ToString() const {
  DebugIndentScope indent;
  auto prefix = indent.Prefix();
  std::string res("{");
  res += prefix + "max header pairs: " + std::to_string(max_header_pairs);
  res += prefix + "max header length: " + std::to_string(max_header_length);
  res += prefix +
         "max field section size: " + std::to_string(max_field_section_size);
  res += prefix + "qpack max dtable capacity: " +
         std::to_string(qpack_max_dtable_capacity);
  res += prefix + "qpack encoder max dtable capacity: " +
         std::to_string(qpack_encoder_max_dtable_capacity);
  res += prefix +
         "qpack blocked streams: " + std::to_string(qpack_blocked_streams);
  res += prefix + "enable connect protocol: " +
         (enable_connect_protocol ? std::string("yes") : std::string("no"));
  res += prefix + "enable datagrams: " +
         (enable_datagrams ? std::string("yes") : std::string("no"));
  res += indent.Close();
  return res;
}

Maybe<Session::Application_Options> Session::Application_Options::From(
    Environment* env, Local<Value> value) {
  if (value.IsEmpty()) [[unlikely]] {
    THROW_ERR_INVALID_ARG_TYPE(env, "options must be an object");
    return Nothing<Application_Options>();
  }

  Application_Options options;
  auto& state = BindingData::Get(env);

#define SET(name)                                                              \
  SetOption<Session::Application_Options,                                      \
            &Session::Application_Options::name>(                              \
      env, &options, params, state.name##_string())

  if (!value->IsUndefined()) {
    if (!value->IsObject()) {
      THROW_ERR_INVALID_ARG_TYPE(env, "options must be an object");
      return Nothing<Application_Options>();
    }
    auto params = value.As<Object>();
    if (!SET(max_header_pairs) || !SET(max_header_length) ||
        !SET(max_field_section_size) || !SET(qpack_max_dtable_capacity) ||
        !SET(qpack_encoder_max_dtable_capacity) ||
        !SET(qpack_blocked_streams) || !SET(enable_connect_protocol) ||
        !SET(enable_datagrams)) {
      // The call to SetOption should have scheduled an exception to be thrown.
      return Nothing<Application_Options>();
    }
  }

#undef SET

  return Just<Application_Options>(options);
}

// ============================================================================

std::string Session::Application::StreamData::ToString() const {
  DebugIndentScope indent;

  size_t total_bytes = 0;
  for (size_t n = 0; n < count; n++) {
    total_bytes += data[n].len;
  }

  auto prefix = indent.Prefix();
  std::string res("{");
  res += prefix + "count: " + std::to_string(count);
  res += prefix + "id: " + std::to_string(id);
  res += prefix + "fin: " + std::to_string(fin);
  res += prefix + "total: " + std::to_string(total_bytes);
  res += indent.Close();
  return res;
}

Session::Application::Application(Session* session, const Options& options)
    : session_(session) {}

bool Session::Application::Start() {
  // By default there is nothing to do. Specific implementations may
  // override to perform more actions.
  Debug(session_, "Session application started");
  return true;
}

bool Session::Application::AcknowledgeStreamData(int64_t stream_id,
                                                 size_t datalen) {
  if (auto stream = session().FindStream(stream_id)) [[likely]] {
    stream->Acknowledge(datalen);
    return true;
  }
  return false;
}

void Session::Application::BlockStream(int64_t id) {
  // By default do nothing.
}

bool Session::Application::CanAddHeader(size_t current_count,
                                        size_t current_headers_length,
                                        size_t this_header_length) {
  // By default headers are not supported.
  return false;
}

bool Session::Application::SendHeaders(const Stream& stream,
                                       HeadersKind kind,
                                       const Local<v8::Array>& headers,
                                       HeadersFlags flags) {
  // By default do nothing.
  return false;
}

void Session::Application::ResumeStream(int64_t id) {
  // By default do nothing.
}

void Session::Application::ExtendMaxStreams(EndpointLabel label,
                                            Direction direction,
                                            uint64_t max_streams) {
  // By default do nothing.
}

void Session::Application::ExtendMaxStreamData(Stream* stream,
                                               uint64_t max_data) {
  Debug(session_, "Application extending max stream data");
  // By default do nothing.
}

void Session::Application::CollectSessionTicketAppData(
    SessionTicket::AppData* app_data) const {
  // By default do nothing.
}

SessionTicket::AppData::Status
Session::Application::ExtractSessionTicketAppData(
    const SessionTicket::AppData& app_data, Flag flag) {
  // By default we do not have any application data to retrieve.
  return flag == Flag::STATUS_RENEW
             ? SessionTicket::AppData::Status::TICKET_USE_RENEW
             : SessionTicket::AppData::Status::TICKET_USE;
}

void Session::Application::SetStreamPriority(const Stream& stream,
                                             StreamPriority priority,
                                             StreamPriorityFlags flags) {
  // By default do nothing.
}

StreamPriority Session::Application::GetStreamPriority(const Stream& stream) {
  return StreamPriority::DEFAULT;
}

BaseObjectPtr<Packet> Session::Application::CreateStreamDataPacket() {
  return Packet::Create(env(),
                        session_->endpoint(),
                        session_->remote_address(),
                        session_->max_packet_size(),
                        "stream data");
}

void Session::Application::StreamClose(Stream* stream, QuicError&& error) {
  DCHECK_NOT_NULL(stream);
  stream->Destroy(std::move(error));
}

void Session::Application::StreamStopSending(Stream* stream,
                                             QuicError&& error) {
  DCHECK_NOT_NULL(stream);
  stream->ReceiveStopSending(std::move(error));
}

void Session::Application::StreamReset(Stream* stream,
                                       uint64_t final_size,
                                       QuicError&& error) {
  stream->ReceiveStreamReset(final_size, std::move(error));
}

void Session::Application::SendPendingData() {
  DCHECK(!session().is_destroyed());
  if (!session().can_send_packets()) [[unlikely]] {
    return;
  }
  static constexpr size_t kMaxPackets = 32;
  Debug(session_, "Application sending pending data");
  PathStorage path;
  StreamData stream_data;

  auto update_stats = OnScopeLeave([&] {
    auto& s = session();
    if (!s.is_destroyed()) [[likely]] {
      s.UpdatePacketTxTime();
      s.UpdateTimer();
      s.UpdateDataStats();
    }
  });

  // The maximum size of packet to create.
  const size_t max_packet_size = session_->max_packet_size();

  // The maximum number of packets to send in this call to SendPendingData.
  const size_t max_packet_count = std::min(
      kMaxPackets, ngtcp2_conn_get_send_quantum(*session_) / max_packet_size);
  if (max_packet_count == 0) return;

  // The number of packets that have been sent in this call to SendPendingData.
  size_t packet_send_count = 0;

  BaseObjectPtr<Packet> packet;
  uint8_t* pos = nullptr;
  uint8_t* begin = nullptr;

  auto ensure_packet = [&] {
    if (!packet) {
      packet = CreateStreamDataPacket();
      if (!packet) [[unlikely]]
        return false;
      pos = begin = ngtcp2_vec(*packet).base;
    }
    DCHECK(packet);
    DCHECK_NOT_NULL(pos);
    DCHECK_NOT_NULL(begin);
    return true;
  };

  // We're going to enter a loop here to prepare and send no more than
  // max_packet_count packets.
  for (;;) {
    // ndatalen is the amount of stream data that was accepted into the packet.
    ssize_t ndatalen = 0;

    // Make sure we have a packet to write data into.
    if (!ensure_packet()) [[unlikely]] {
      Debug(session_, "Failed to create packet for stream data");
      // Doh! Could not create a packet. Time to bail.
      session_->SetLastError(QuicError::ForNgtcp2Error(NGTCP2_ERR_INTERNAL));
      return session_->Close(CloseMethod::SILENT);
    }

    // The stream_data is the next block of data from the application stream.
    if (GetStreamData(&stream_data) < 0) {
      Debug(session_, "Application failed to get stream data");
      packet->Done(UV_ECANCELED);
      session_->SetLastError(QuicError::ForNgtcp2Error(NGTCP2_ERR_INTERNAL));
      return session_->Close(CloseMethod::SILENT);
    }

    // If we got here, we were at least successful in checking for stream data.
    // There might not be any stream data to send.
    if (stream_data.id >= 0) {
      Debug(session_, "Application using stream data: %s", stream_data);
    }

    // Awesome, let's write our packet!
    ssize_t nwrite =
        WriteVStream(&path, pos, &ndatalen, max_packet_size, stream_data);

    if (ndatalen > 0) {
      Debug(session_,
            "Application accepted %zu bytes from stream %" PRIi64
            " into packet",
            ndatalen,
            stream_data.id);
    } else if (stream_data.id >= 0) {
      Debug(session_,
            "Application did not accept any bytes from stream %" PRIi64
            " into packet",
            stream_data.id);
    }

    // A negative nwrite value indicates either an error or that there is more
    // data to write into the packet.
    if (nwrite < 0) {
      switch (nwrite) {
        case NGTCP2_ERR_STREAM_DATA_BLOCKED: {
          // We could not write any data for this stream into the packet because
          // the flow control for the stream itself indicates that the stream
          // is blocked. We'll skip and move on to the next stream.
          // ndatalen = -1 means that no stream data was accepted into the
          // packet, which is what we want here.
          DCHECK_EQ(ndatalen, -1);
          // We should only have received this error if there was an actual
          // stream identified in the stream data, but let's double check.
          DCHECK_GE(stream_data.id, 0);
          session_->StreamDataBlocked(stream_data.id);
          continue;
        }
        case NGTCP2_ERR_STREAM_SHUT_WR: {
          // Indicates that the writable side of the stream should be closed
          // locally or the stream is being reset. In either case, we can't send
          // any stream data!
          Debug(session_,
                "Closing stream %" PRIi64 " for writing",
                stream_data.id);
          // ndatalen = -1 means that no stream data was accepted into the
          // packet, which is what we want here.
          DCHECK_EQ(ndatalen, -1);
          // We should only have received this error if there was an actual
          // stream identified in the stream data, but let's double check.
          DCHECK_GE(stream_data.id, 0);
          if (stream_data.stream) [[likely]] {
            stream_data.stream->EndWritable();
          }
          continue;
        }
        case NGTCP2_ERR_WRITE_MORE: {
          if (ndatalen >= 0 && !StreamCommit(&stream_data, ndatalen)) {
            Debug(session_,
                  "Failed to commit stream data while writing packets");
            packet->Done(UV_ECANCELED);
            session_->SetLastError(
                QuicError::ForNgtcp2Error(NGTCP2_ERR_INTERNAL));
            return session_->Close(CloseMethod::SILENT);
          }
          continue;
        }
      }

      // Some other type of error happened.
      DCHECK_EQ(ndatalen, -1);
      Debug(session_,
            "Application encountered error while writing packet: %s",
            ngtcp2_strerror(nwrite));
      packet->Done(UV_ECANCELED);
      session_->SetLastError(QuicError::ForNgtcp2Error(nwrite));
      return session_->Close(CloseMethod::SILENT);
    } else if (ndatalen >= 0 && !StreamCommit(&stream_data, ndatalen)) {
      packet->Done(UV_ECANCELED);
      session_->SetLastError(QuicError::ForNgtcp2Error(NGTCP2_ERR_INTERNAL));
      return session_->Close(CloseMethod::SILENT);
    }

    // When nwrite is zero, it means we are congestion limited or it is
    // just not our turn now to send something. Stop sending packets.
    if (nwrite == 0) {
      // If there was stream data selected, we should reschedule it to try
      // sending again.
      if (stream_data.id >= 0) ResumeStream(stream_data.id);

      // There might be a partial packet already prepared. If so, send it.
      size_t datalen = pos - begin;
      if (datalen) {
        Debug(session_, "Sending packet with %zu bytes", datalen);
        packet->Truncate(datalen);
        session_->Send(packet, path);
      } else {
        packet->Done(UV_ECANCELED);
      }

      return;
    }

    // At this point we have a packet prepared to send.
    pos += nwrite;
    size_t datalen = pos - begin;
    Debug(session_, "Sending packet with %zu bytes", datalen);
    packet->Truncate(datalen);
    session_->Send(packet, path);

    // If we have sent the maximum number of packets, we're done.
    if (++packet_send_count == max_packet_count) {
      return;
    }

    // Prepare to loop back around to prepare a new packet.
    packet.reset();
    pos = begin = nullptr;
  }
}

ssize_t Session::Application::WriteVStream(PathStorage* path,
                                           uint8_t* dest,
                                           ssize_t* ndatalen,
                                           size_t max_packet_size,
                                           const StreamData& stream_data) {
  DCHECK_LE(stream_data.count, kMaxVectorCount);
  uint32_t flags = NGTCP2_WRITE_STREAM_FLAG_MORE;
  if (stream_data.fin) flags |= NGTCP2_WRITE_STREAM_FLAG_FIN;
  return ngtcp2_conn_writev_stream(*session_,
                                   &path->path,
                                   nullptr,
                                   dest,
                                   max_packet_size,
                                   ndatalen,
                                   flags,
                                   stream_data.id,
                                   stream_data,
                                   stream_data.count,
                                   uv_hrtime());
}

// The DefaultApplication is the default implementation of Session::Application
// that is used for all unrecognized ALPN identifiers.
class DefaultApplication final : public Session::Application {
 public:
  // Marked NOLINT because the cpp linter gets confused about this using
  // statement not being sorted with the using v8 statements at the top
  // of the namespace.
  using Application::Application;  // NOLINT

  bool ReceiveStreamData(int64_t stream_id,
                         const uint8_t* data,
                         size_t datalen,
                         const Stream::ReceiveDataFlags& flags,
                         void* stream_user_data) override {
    BaseObjectPtr<Stream> stream;
    if (stream_user_data == nullptr) {
      // This is the first time we're seeing this stream. Implicitly create it.
      stream = session().CreateStream(stream_id);
      if (!stream) [[unlikely]] {
        // We couldn't actually create the stream for whatever reason.
        Debug(&session(), "Default application failed to create new stream");
        return false;
      }
    } else {
      stream = BaseObjectPtr<Stream>(Stream::From(stream_user_data));
      if (!stream) {
        Debug(&session(),
              "Default application failed to get existing stream "
              "from user data");
        return false;
      }
    }

    CHECK(stream);

    // Now we can actually receive the data! Woo!
    stream->ReceiveData(data, datalen, flags);
    return true;
  }

  int GetStreamData(StreamData* stream_data) override {
    // Reset the state of stream_data before proceeding...
    stream_data->id = -1;
    stream_data->count = 0;
    stream_data->fin = 0;
    stream_data->stream.reset();
    stream_data->remaining = 0;
    Debug(&session(), "Default application getting stream data");
    DCHECK_NOT_NULL(stream_data);
    // If the queue is empty, there aren't any streams with data yet
    if (stream_queue_.IsEmpty()) return 0;

    const auto get_length = [](auto vec, size_t count) {
      CHECK_NOT_NULL(vec);
      size_t len = 0;
      for (size_t n = 0; n < count; n++) len += vec[n].len;
      return len;
    };

    Stream* stream = stream_queue_.PopFront();
    CHECK_NOT_NULL(stream);
    stream_data->stream.reset(stream);
    stream_data->id = stream->id();
    auto next =
        [&](int status, const ngtcp2_vec* data, size_t count, bob::Done done) {
          switch (status) {
            case bob::Status::STATUS_BLOCK:
              // Fall through
            case bob::Status::STATUS_WAIT:
              return;
            case bob::Status::STATUS_EOS:
              stream_data->fin = 1;
          }

          // It is possible that the data pointers returned are not actually
          // the data pointers in the stream_data. If that's the case, we need
          // to copy over the pointers.
          count = std::min(count, kMaxVectorCount);
          ngtcp2_vec* dest = *stream_data;
          if (dest != data) {
            for (size_t n = 0; n < count; n++) {
              dest[n] = data[n];
            }
          }

          stream_data->count = count;

          if (count > 0) {
            stream->Schedule(&stream_queue_);
            stream_data->remaining = get_length(data, count);
          } else {
            stream_data->remaining = 0;
          }

          // Not calling done here because we defer committing
          // the data until after we're sure it's written.
        };

    if (!stream->is_eos()) [[likely]] {
      int ret = stream->Pull(std::move(next),
                             bob::Options::OPTIONS_SYNC,
                             stream_data->data,
                             arraysize(stream_data->data),
                             kMaxVectorCount);
      if (ret == bob::Status::STATUS_EOS) {
        stream_data->fin = 1;
      }
    } else {
      stream_data->fin = 1;
    }

    return 0;
  }

  void ResumeStream(int64_t id) override { ScheduleStream(id); }

  bool ShouldSetFin(const StreamData& stream_data) override {
    auto const is_empty = [](const ngtcp2_vec* vec, size_t cnt) {
      size_t i = 0;
      for (size_t n = 0; n < cnt; n++) i += vec[n].len;
      return i > 0;
    };

    return stream_data.stream && is_empty(stream_data, stream_data.count);
  }

  void BlockStream(int64_t id) override {
    if (auto stream = session().FindStream(id)) [[likely]] {
      stream->EmitBlocked();
    }
  }

  bool StreamCommit(StreamData* stream_data, size_t datalen) override {
    if (datalen == 0) return true;
    DCHECK_NOT_NULL(stream_data);
    CHECK(stream_data->stream);
    stream_data->stream->Commit(datalen);
    return true;
  }

  SET_SELF_SIZE(DefaultApplication)
  SET_MEMORY_INFO_NAME(DefaultApplication)
  SET_NO_MEMORY_INFO()

 private:
  void ScheduleStream(int64_t id) {
    if (auto stream = session().FindStream(id)) [[likely]] {
      stream->Schedule(&stream_queue_);
    }
  }

  void UnscheduleStream(int64_t id) {
    if (auto stream = session().FindStream(id)) [[likely]] {
      stream->Unschedule();
    }
  }

  Stream::Queue stream_queue_;
};

std::unique_ptr<Session::Application> Session::SelectApplication(
    Session* session, const Config& config) {
  if (config.options.application_provider) {
    return config.options.application_provider->Create(session);
  }

  return std::make_unique<DefaultApplication>(session,
                                              Application_Options::kDefault);
}

}  // namespace quic
}  // namespace node

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
