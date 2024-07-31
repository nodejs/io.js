#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
#include "streams.h"
#include <aliased_struct-inl.h>
#include <async_wrap-inl.h>
#include <base_object-inl.h>
#include <env-inl.h>
#include <memory_tracker-inl.h>
#include <node_blob.h>
#include <node_bob-inl.h>
#include <node_sockaddr-inl.h>
#include "application.h"
#include "bindingdata.h"
#include "defs.h"
#include "session.h"

namespace node {

using v8::Array;
using v8::ArrayBuffer;
using v8::ArrayBufferView;
using v8::BigInt;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Integer;
using v8::Just;
using v8::Local;
using v8::Maybe;
using v8::Nothing;
using v8::Object;
using v8::PropertyAttribute;
using v8::SharedArrayBuffer;
using v8::Uint32;
using v8::Value;

namespace quic {

#define STREAM_STATE(V)                                                        \
  V(ID, id, int64_t)                                                           \
  V(FIN_SENT, fin_sent, uint8_t)                                               \
  V(FIN_RECEIVED, fin_received, uint8_t)                                       \
  V(READ_ENDED, read_ended, uint8_t)                                           \
  V(WRITE_ENDED, write_ended, uint8_t)                                         \
  V(DESTROYED, destroyed, uint8_t)                                             \
  V(PAUSED, paused, uint8_t)                                                   \
  V(RESET, reset, uint8_t)                                                     \
  V(HAS_READER, has_reader, uint8_t)                                           \
  /* Set when the stream has a block event handler */                          \
  V(WANTS_BLOCK, wants_block, uint8_t)                                         \
  /* Set when the stream has a headers event handler */                        \
  V(WANTS_HEADERS, wants_headers, uint8_t)                                     \
  /* Set when the stream has a reset event handler */                          \
  V(WANTS_RESET, wants_reset, uint8_t)                                         \
  /* Set when the stream has a trailers event handler */                       \
  V(WANTS_TRAILERS, wants_trailers, uint8_t)

#define STREAM_STATS(V)                                                        \
  V(CREATED_AT, created_at)                                                    \
  V(RECEIVED_AT, received_at)                                                  \
  V(ACKED_AT, acked_at)                                                        \
  V(CLOSING_AT, closing_at)                                                    \
  V(DESTROYED_AT, destroyed_at)                                                \
  V(BYTES_RECEIVED, bytes_received)                                            \
  V(BYTES_SENT, bytes_sent)                                                    \
  V(MAX_OFFSET, max_offset)                                                    \
  V(MAX_OFFSET_ACK, max_offset_ack)                                            \
  V(MAX_OFFSET_RECV, max_offset_received)                                      \
  V(FINAL_SIZE, final_size)

#define STREAM_JS_METHODS(V)                                                   \
  V(AttachSource, attachSource, false)                                         \
  V(Destroy, destroy, false)                                                   \
  V(SendHeaders, sendHeaders, false)                                           \
  V(StopSending, stopSending, false)                                           \
  V(ResetStream, resetStream, false)                                           \
  V(SetPriority, setPriority, false)                                           \
  V(GetPriority, getPriority, true)                                            \
  V(GetReader, getReader, false)

struct Stream::State {
#define V(_, name, type) type name;
  STREAM_STATE(V)
#undef V
};

STAT_STRUCT(Stream, STREAM)

// ============================================================================

namespace {
Maybe<std::shared_ptr<DataQueue>> GetDataQueueFromSource(Environment* env,
                                                         Local<Value> value) {
  DCHECK_IMPLIES(!value->IsUndefined(), value->IsObject());
  if (value->IsUndefined()) {
    return Just(std::shared_ptr<DataQueue>());
  } else if (value->IsArrayBuffer()) {
    auto buffer = value.As<ArrayBuffer>();
    std::vector<std::unique_ptr<DataQueue::Entry>> entries(1);
    entries.push_back(DataQueue::CreateInMemoryEntryFromBackingStore(
        buffer->GetBackingStore(), 0, buffer->ByteLength()));
    return Just(DataQueue::CreateIdempotent(std::move(entries)));
  } else if (value->IsSharedArrayBuffer()) {
    auto buffer = value.As<SharedArrayBuffer>();
    std::vector<std::unique_ptr<DataQueue::Entry>> entries(1);
    entries.push_back(DataQueue::CreateInMemoryEntryFromBackingStore(
        buffer->GetBackingStore(), 0, buffer->ByteLength()));
    return Just(DataQueue::CreateIdempotent(std::move(entries)));
  } else if (value->IsArrayBufferView()) {
    std::vector<std::unique_ptr<DataQueue::Entry>> entries(1);
    entries.push_back(
        DataQueue::CreateInMemoryEntryFromView(value.As<ArrayBufferView>()));
    return Just(DataQueue::CreateIdempotent(std::move(entries)));
  } else if (Blob::HasInstance(env, value)) {
    Blob* blob;
    ASSIGN_OR_RETURN_UNWRAP(
        &blob, value, Nothing<std::shared_ptr<DataQueue>>());
    return Just(blob->getDataQueue().slice(0));
  }
  // TODO(jasnell): Add streaming sources...
  THROW_ERR_INVALID_ARG_TYPE(env, "Invalid data source type");
  return Nothing<std::shared_ptr<DataQueue>>();
}
}  // namespace

struct Stream::Impl {
  static void AttachSource(const FunctionCallbackInfo<Value>& args) {
    Environment* env = Environment::GetCurrent(args);

    std::shared_ptr<DataQueue> dataqueue;
    if (GetDataQueueFromSource(env, args[0]).To(&dataqueue)) {
      Stream* stream;
      ASSIGN_OR_RETURN_UNWRAP(&stream, args.This());
      stream->set_outbound(std::move(dataqueue));
    }
  }

  static void Destroy(const FunctionCallbackInfo<Value>& args) {
    Stream* stream;
    ASSIGN_OR_RETURN_UNWRAP(&stream, args.This());
    if (args.Length() > 1) {
      CHECK(args[0]->IsBigInt());
      bool unused = false;
      stream->Destroy(QuicError::ForApplication(
          args[0].As<BigInt>()->Uint64Value(&unused)));
    } else {
      stream->Destroy();
    }
  }

  static void SendHeaders(const FunctionCallbackInfo<Value>& args) {
    Stream* stream;
    ASSIGN_OR_RETURN_UNWRAP(&stream, args.This());
    CHECK(args[0]->IsUint32());  // Kind
    CHECK(args[1]->IsArray());   // Headers
    CHECK(args[2]->IsUint32());  // Flags

    HeadersKind kind = static_cast<HeadersKind>(args[0].As<Uint32>()->Value());
    Local<Array> headers = args[1].As<Array>();
    HeadersFlags flags =
        static_cast<HeadersFlags>(args[2].As<Uint32>()->Value());

    if (stream->is_destroyed()) return args.GetReturnValue().Set(false);

    args.GetReturnValue().Set(stream->session().application().SendHeaders(
        *stream, kind, headers, flags));
  }

  // Tells the peer to stop sending data for this stream. This has the effect
  // of shutting down the readable side of the stream for this peer. Any data
  // that has already been received is still readable.
  static void StopSending(const FunctionCallbackInfo<Value>& args) {
    Stream* stream;
    ASSIGN_OR_RETURN_UNWRAP(&stream, args.This());
    uint64_t code = NGTCP2_APP_NOERROR;
    CHECK_IMPLIES(!args[0]->IsUndefined(), args[0]->IsBigInt());
    if (!args[0]->IsUndefined()) {
      bool lossless = false;  // not used but still necessary.
      code = args[0].As<BigInt>()->Uint64Value(&lossless);
    }

    if (stream->is_destroyed()) return;
    stream->EndReadable();
    Session::SendPendingDataScope send_scope(&stream->session());
    ngtcp2_conn_shutdown_stream_read(stream->session(), 0, stream->id(), code);
  }

  // Sends a reset stream to the peer to tell it we will not be sending any
  // more data for this stream. This has the effect of shutting down the
  // writable side of the stream for this peer. Any data that is held in the
  // outbound queue will be dropped. The stream may still be readable.
  static void ResetStream(const FunctionCallbackInfo<Value>& args) {
    Stream* stream;
    ASSIGN_OR_RETURN_UNWRAP(&stream, args.This());
    uint64_t code = NGTCP2_APP_NOERROR;
    CHECK_IMPLIES(!args[0]->IsUndefined(), args[0]->IsBigInt());
    if (!args[0]->IsUndefined()) {
      bool lossless = false;  // not used but still necessary.
      code = args[0].As<BigInt>()->Uint64Value(&lossless);
    }

    if (stream->is_destroyed() || stream->state_->reset == 1) return;
    stream->EndWritable();
    // We can release our outbound here now. Since the stream is being reset
    // on the ngtcp2 side, we do not need to keep any of the data around
    // waiting for acknowledgement that will never come.
    stream->outbound_.reset();
    stream->state_->reset = 1;
    Session::SendPendingDataScope send_scope(&stream->session());
    ngtcp2_conn_shutdown_stream_write(stream->session(), 0, stream->id(), code);
  }

  static void SetPriority(const FunctionCallbackInfo<Value>& args) {
    Stream* stream;
    ASSIGN_OR_RETURN_UNWRAP(&stream, args.This());
    CHECK(args[0]->IsUint32());  // Priority
    CHECK(args[1]->IsUint32());  // Priority flag

    StreamPriority priority =
        static_cast<StreamPriority>(args[0].As<Uint32>()->Value());
    StreamPriorityFlags flags =
        static_cast<StreamPriorityFlags>(args[1].As<Uint32>()->Value());

    stream->session().application().SetStreamPriority(*stream, priority, flags);
  }

  static void GetPriority(const FunctionCallbackInfo<Value>& args) {
    Stream* stream;
    ASSIGN_OR_RETURN_UNWRAP(&stream, args.This());
    auto priority = stream->session().application().GetStreamPriority(*stream);
    args.GetReturnValue().Set(static_cast<uint32_t>(priority));
  }

  static void GetReader(const FunctionCallbackInfo<Value>& args) {
    Stream* stream;
    ASSIGN_OR_RETURN_UNWRAP(&stream, args.This());
    BaseObjectPtr<Blob::Reader> reader = stream->get_reader();
    if (reader) return args.GetReturnValue().Set(reader->object());
    THROW_ERR_INVALID_STATE(Environment::GetCurrent(args),
                            "Unable to get a reader for the stream");
  }
};

// ============================================================================

class Stream::Outbound final : public MemoryRetainer {
 public:
  Outbound(Stream* stream, std::shared_ptr<DataQueue> queue)
      : stream_(stream),
        queue_(std::move(queue)),
        reader_(queue_->get_reader()) {}

  void Acknowledge(size_t amount) {
    size_t remaining = std::min(amount, total_ - uncommitted_);
    while (remaining > 0 && head_ != nullptr) {
      DCHECK_LE(head_->ack_offset, head_->offset);
      // The amount to acknowledge in this chunk is the lesser of the total
      // amount remaining to acknowledge or the total remaining unacknowledged
      // bytes in the chunk.
      size_t amount_to_ack =
          std::min(remaining, head_->offset - head_->ack_offset);
      // If the amount to ack is zero here, it means our ack offset has caught
      // up to our commit offset, which means there's nothing left to
      // acknowledge yet. We could treat this as an error but let's just stop
      // here.
      if (amount_to_ack == 0) break;

      // Adjust our remaining down and our ack_offset up...
      remaining -= amount_to_ack;
      head_->ack_offset += amount_to_ack;

      // If we've fully acknowledged this chunk, free it and decrement total.
      if (head_->ack_offset == head_->buf.len) {
        DCHECK_GE(total_, head_->buf.len);
        total_ -= head_->buf.len;
        // if tail_ == head_ here, it means we've fully acknowledged our current
        // buffer. Set tail to nullptr since we're freeing it here.
        if (head_.get() == tail_) {
          // In this case, commit_head_ should have already been set to nullptr.
          // Because we should only have hit this case if the entire buffer
          // had been committed.
          DCHECK(commit_head_ == nullptr);
          tail_ = nullptr;
        }
        head_ = std::move(head_->next);
        DCHECK_IMPLIES(head_ == nullptr, tail_ == nullptr);
      }
    }
  }

  void Commit(size_t amount) {
    // Commit amount number of bytes from the current uncommitted
    // byte queue. Importantly, this does not remove the bytes
    // from the byte queue.
    size_t remaining = std::min(uncommitted_, amount);
    // There's nothing to commit.
    while (remaining > 0 && commit_head_ != nullptr) {
      // The amount to commit is the lesser of the total amount remaining to
      // commit and the remaining uncommitted bytes in this chunk.
      size_t amount_to_commit = std::min(
          remaining,
          static_cast<size_t>(commit_head_->buf.len - commit_head_->offset));

      // The amount to commit here should never be zero because that means we
      // should have already advanced the commit head.
      DCHECK_NE(amount_to_commit, 0);
      uncommitted_ -= amount_to_commit;
      remaining -= amount_to_commit;
      commit_head_->offset += amount_to_commit;
      if (commit_head_->offset == commit_head_->buf.len) {
        count_--;
        commit_head_ = commit_head_->next.get();
      }
    }
  }

  void Cap() {
    // Calling cap without a value halts the ability to add any
    // new data to the queue if it is not idempotent. If it is
    // idempotent, it's a non-op.
    queue_->cap();
  }

  int Pull(bob::Next<ngtcp2_vec> next,
           int options,
           ngtcp2_vec* data,
           size_t count,
           size_t max_count_hint) {
    if (next_pending_) {
      std::move(next)(bob::Status::STATUS_BLOCK, nullptr, 0, [](int) {});
      return bob::Status::STATUS_BLOCK;
    }

    if (errored_) {
      std::move(next)(UV_EBADF, nullptr, 0, [](int) {});
      return UV_EBADF;
    }

    // If eos_ is true and there are no uncommitted bytes we'll return eos,
    // otherwise, return whatever is in the uncommitted queue.
    if (eos_) {
      if (uncommitted_ > 0) {
        PullUncommitted(std::move(next));
        return bob::Status::STATUS_CONTINUE;
      }
      std::move(next)(bob::Status::STATUS_EOS, nullptr, 0, [](int) {});
      return bob::Status::STATUS_EOS;
    }

    // If there are uncommitted bytes in the queue_, and there are enough to
    // fill a full data packet, then pull will just return the current
    // uncommitted bytes currently in the queue rather than reading more from
    // the queue.
    if (uncommitted_ >= kDefaultMaxPacketLength) {
      PullUncommitted(std::move(next));
      return bob::Status::STATUS_CONTINUE;
    }

    DCHECK(queue_);
    DCHECK(reader_);

    // At this point, we know our reader hasn't finished yet, there might be
    // uncommitted bytes but we want to go ahead and pull some more. We request
    // that the pull is sync but allow for it to be async.
    int ret = reader_->Pull(
        [this](auto status, auto vecs, auto count, auto done) {
          // Always make sure next_pending_ is false when we're done.
          auto on_exit = OnScopeLeave([this] { next_pending_ = false; });

          // The status should never be wait here.
          DCHECK_NE(status, bob::Status::STATUS_WAIT);

          if (status < 0) {
            // If next_pending_ is true then a pull from the reader ended up
            // being asynchronous, our stream is blocking waiting for the data,
            // but we have an error! oh no! We need to error the stream.
            if (next_pending_) {
              stream_->Destroy(
                  QuicError::ForNgtcp2Error(NGTCP2_INTERNAL_ERROR));
              // We do not need to worry about calling MarkErrored in this case
              // since we are immediately destroying the stream which will
              // release the outbound buffer anyway.
            }
            return;
          }

          if (status == bob::Status::STATUS_EOS) {
            DCHECK_EQ(count, 0);
            DCHECK_NULL(vecs);
            MarkEnded();
            // If next_pending_ is true then a pull from the reader ended up
            // being asynchronous, our stream is blocking waiting for the data.
            // Here, there is no more data to read, but we will might have data
            // in the uncommitted queue. We'll resume the stream so that the
            // session will try to read from it again.
            if (next_pending_ && !stream_->is_destroyed()) {
              stream_->session().ResumeStream(stream_->id());
            }
            return;
          }

          if (status == bob::Status::STATUS_BLOCK) {
            DCHECK_EQ(count, 0);
            DCHECK_NULL(vecs);
            // If next_pending_ is true then a pull from the reader ended up
            // being asynchronous, our stream is blocking waiting for the data.
            // Here, we're still blocking! so there's nothing left for us to do!
            return;
          }

          DCHECK_EQ(status, bob::Status::STATUS_CONTINUE);
          // If the read returns bytes, those will be added to the uncommitted
          // bytes in the queue.
          Append(vecs, count, std::move(done));

          // If next_pending_ is true, then a pull from the reader ended up
          // being asynchronous, our stream is blocking waiting for the data.
          // Now that we have data, let's resume the stream so the session will
          // pull from it again.
          if (next_pending_ && !stream_->is_destroyed()) {
            stream_->session().ResumeStream(stream_->id());
          }
        },
        bob::OPTIONS_SYNC,
        nullptr,
        0,
        kMaxVectorCount);

    // There was an error. We'll report that immediately. We do not have
    // to destroy the stream here since that will be taken care of by
    // the caller.
    if (ret < 0) {
      MarkErrored();
      std::move(next)(ret, nullptr, 0, [](int) {});
      // Since we are erroring and won't be able to make use of this DataQueue
      // any longer, let's free both it and the reader and put ourselves into
      // an errored state. Further attempts to read from the outbound will
      // result in a UV_EBADF error. The caller, however, should handle this by
      // closing down the stream so that doesn't happen.
      return ret;
    }

    if (ret == bob::Status::STATUS_EOS) {
      // Here, we know we are done with the DataQueue and the Reader, but we
      // might not yet have committed or acknowledged all of the queued data.
      // We'll release our references to the queue_ and reader_ but everything
      // else is untouched.
      MarkEnded();
      if (uncommitted_ > 0) {
        // If the read returns eos, and there are uncommitted bytes in the
        // queue, we'll set eos_ to true and return the current set of
        // uncommitted bytes.
        PullUncommitted(std::move(next));
        return bob::STATUS_CONTINUE;
      }
      // If the read returns eos, and there are no uncommitted bytes in the
      // queue, we'll return eos with no data.
      std::move(next)(bob::Status::STATUS_EOS, nullptr, 0, [](int) {});
      return bob::Status::STATUS_EOS;
    }

    if (ret == bob::Status::STATUS_BLOCK) {
      // If the read returns blocked, and there are uncommitted bytes in the
      // queue, we'll return the current set of uncommitted bytes.
      if (uncommitted_ > 0) {
        PullUncommitted(std::move(next));
        return bob::Status::STATUS_CONTINUE;
      }
      // If the read returns blocked, and there are no uncommitted bytes in the
      // queue, we'll return blocked.
      std::move(next)(bob::Status::STATUS_BLOCK, nullptr, 0, [](int) {});
      return bob::Status::STATUS_BLOCK;
    }

    // Reads here are generally expected to be synchronous. If we have a reader
    // that insists on providing data asynchronously, then we'll have to block
    // until the data is actually available.
    if (ret == bob::Status::STATUS_WAIT) {
      next_pending_ = true;
      std::move(next)(bob::Status::STATUS_BLOCK, nullptr, 0, [](int) {});
      return bob::Status::STATUS_BLOCK;
    }

    DCHECK_EQ(ret, bob::Status::STATUS_CONTINUE);
    PullUncommitted(std::move(next));
    return bob::Status::STATUS_CONTINUE;
  }

  void MemoryInfo(MemoryTracker* tracker) const override {
    tracker->TrackField("queue", queue_);
    tracker->TrackField("reader", reader_);
    tracker->TrackFieldWithSize("buffer", total_);
  }

  SET_MEMORY_INFO_NAME(Stream::Outbound)
  SET_SELF_SIZE(Outbound)

 private:
  struct OnComplete {
    bob::Done done;
    explicit OnComplete(bob::Done done) : done(std::move(done)) {}
    ~OnComplete() { std::move(done)(0); }
  };

  void PullUncommitted(bob::Next<ngtcp2_vec> next) {
    MaybeStackBuffer<ngtcp2_vec, 16> chunks;
    chunks.AllocateSufficientStorage(count_);
    auto head = commit_head_;
    size_t n = 0;
    while (head != nullptr && n < count_) {
      // There might only be one byte here but there should never be zero.
      DCHECK_LT(head->offset, head->buf.len);
      chunks[n].base = head->buf.base + head->offset;
      chunks[n].len = head->buf.len - head->offset;
      head = head->next.get();
      n++;
    }
    std::move(next)(bob::Status::STATUS_CONTINUE, chunks.out(), n, [](int) {});
  }

  void MarkErrored() {
    errored_ = true;
    head_.reset();
    tail_ = nullptr;
    commit_head_ = nullptr;
    total_ = 0;
    count_ = 0;
    uncommitted_ = 0;
    MarkEnded();
  }

  void MarkEnded() {
    eos_ = true;
    queue_.reset();
    reader_.reset();
  }

  void Append(const DataQueue::Vec* vectors, size_t count, bob::Done done) {
    if (count == 0) return;
    // The done callback should only be invoked after we're done with
    // all of the vectors passed in this call. To ensure of that, we
    // wrap it with a shared pointer that calls done when the final
    // instance is dropped.
    auto on_complete = std::make_shared<OnComplete>(std::move(done));
    for (size_t n = 0; n < count; n++) {
      if (vectors[n].len == 0 || vectors[n].base == nullptr) continue;
      auto entry = std::make_unique<Entry>(vectors[n], on_complete);
      if (tail_ == nullptr) {
        head_ = std::move(entry);
        tail_ = head_.get();
        commit_head_ = head_.get();
      } else {
        DCHECK_NULL(tail_->next);
        tail_->next = std::move(entry);
        tail_ = tail_->next.get();
        if (commit_head_ == nullptr) commit_head_ = tail_;
      }
      count_++;
      total_ += vectors[n].len;
      uncommitted_ += vectors[n].len;
    }
  }

  Stream* stream_;
  std::shared_ptr<DataQueue> queue_;
  std::shared_ptr<DataQueue::Reader> reader_;

  bool errored_ = false;

  // Will be set to true if the reader_ ends up providing a pull result
  // asynchronously.
  bool next_pending_ = false;

  // Will be set to true once reader_ has returned eos.
  bool eos_ = false;

  // The collection of buffers that we have pulled from reader_ and that we
  // are holding onto until they are acknowledged.
  struct Entry {
    size_t offset = 0;
    size_t ack_offset = 0;
    DataQueue::Vec buf;
    std::shared_ptr<OnComplete> on_complete;
    std::unique_ptr<Entry> next;
    Entry(DataQueue::Vec buf, std::shared_ptr<OnComplete> on_complete)
        : buf(buf), on_complete(std::move(on_complete)) {}
  };

  std::unique_ptr<Entry> head_ = nullptr;
  Entry* commit_head_ = nullptr;
  Entry* tail_ = nullptr;

  // The total number of uncommitted chunks.
  size_t count_ = 0;

  // The total number of bytes currently held in the buffer.
  size_t total_ = 0;

  // The current byte offset of buffer_ that has been confirmed to have been
  // sent. Any offset lower than this represents bytes that we are currently
  // waiting to be acknowledged. When we receive acknowledgement, we will
  // automatically free held bytes from the buffer.
  size_t uncommitted_ = 0;
};

// ============================================================================

bool Stream::HasInstance(Environment* env, Local<Value> value) {
  return GetConstructorTemplate(env)->HasInstance(value);
}

Local<FunctionTemplate> Stream::GetConstructorTemplate(Environment* env) {
  auto& state = BindingData::Get(env);
  auto tmpl = state.stream_constructor_template();
  if (tmpl.IsEmpty()) {
    auto isolate = env->isolate();
    tmpl = NewFunctionTemplate(isolate, IllegalConstructor);
    tmpl->SetClassName(state.stream_string());
    tmpl->Inherit(AsyncWrap::GetConstructorTemplate(env));
    tmpl->InstanceTemplate()->SetInternalFieldCount(
        Stream::kInternalFieldCount);
#define V(name, key, no_side_effect)                                           \
  if (no_side_effect) {                                                        \
    SetProtoMethodNoSideEffect(isolate, tmpl, #key, Impl::name);               \
  } else {                                                                     \
    SetProtoMethod(isolate, tmpl, #key, Impl::name);                           \
  }

    STREAM_JS_METHODS(V)

#undef V
    state.set_stream_constructor_template(tmpl);
  }
  return tmpl;
}

void Stream::RegisterExternalReferences(ExternalReferenceRegistry* registry) {
#define V(name, _, __) registry->Register(Impl::name);
  STREAM_JS_METHODS(V)
#undef V
}

void Stream::Initialize(Environment* env, Local<Object> target) {
  USE(GetConstructorTemplate(env));

#define V(name, _) IDX_STATS_STREAM_##name,
  enum StreamStatsIdx { STREAM_STATS(V) IDX_STATS_STREAM_COUNT };
#undef V

#define V(name, key, __)                                                       \
  auto IDX_STATE_STREAM_##name = offsetof(Stream::State, key);
  STREAM_STATE(V)
#undef V

#define V(name, _) NODE_DEFINE_CONSTANT(target, IDX_STATS_STREAM_##name);
  STREAM_STATS(V)
#undef V
#define V(name, _, __) NODE_DEFINE_CONSTANT(target, IDX_STATE_STREAM_##name);
  STREAM_STATE(V)
#undef V

  constexpr int QUIC_STREAM_HEADERS_KIND_HINTS =
      static_cast<int>(HeadersKind::HINTS);
  constexpr int QUIC_STREAM_HEADERS_KIND_INITIAL =
      static_cast<int>(HeadersKind::INITIAL);
  constexpr int QUIC_STREAM_HEADERS_KIND_TRAILING =
      static_cast<int>(HeadersKind::TRAILING);

  constexpr int QUIC_STREAM_HEADERS_FLAGS_NONE =
      static_cast<int>(HeadersFlags::NONE);
  constexpr int QUIC_STREAM_HEADERS_FLAGS_TERMINAL =
      static_cast<int>(HeadersFlags::TERMINAL);

  NODE_DEFINE_CONSTANT(target, QUIC_STREAM_HEADERS_KIND_HINTS);
  NODE_DEFINE_CONSTANT(target, QUIC_STREAM_HEADERS_KIND_INITIAL);
  NODE_DEFINE_CONSTANT(target, QUIC_STREAM_HEADERS_KIND_TRAILING);

  NODE_DEFINE_CONSTANT(target, QUIC_STREAM_HEADERS_FLAGS_NONE);
  NODE_DEFINE_CONSTANT(target, QUIC_STREAM_HEADERS_FLAGS_TERMINAL);
}

Stream* Stream::From(void* stream_user_data) {
  DCHECK_NOT_NULL(stream_user_data);
  return static_cast<Stream*>(stream_user_data);
}

BaseObjectPtr<Stream> Stream::Create(Session* session,
                                     int64_t id,
                                     std::shared_ptr<DataQueue> source) {
  DCHECK_GE(id, 0);
  DCHECK_NOT_NULL(session);
  Local<Object> obj;
  if (!GetConstructorTemplate(session->env())
           ->InstanceTemplate()
           ->NewInstance(session->env()->context())
           .ToLocal(&obj)) {
    return BaseObjectPtr<Stream>();
  }

  return MakeDetachedBaseObject<Stream>(
      BaseObjectWeakPtr<Session>(session), obj, id, std::move(source));
}

Stream::Stream(BaseObjectWeakPtr<Session> session,
               v8::Local<v8::Object> object,
               int64_t id,
               std::shared_ptr<DataQueue> source)
    : AsyncWrap(session->env(), object, AsyncWrap::PROVIDER_QUIC_STREAM),
      stats_(env()->isolate()),
      state_(env()->isolate()),
      session_(std::move(session)),
      origin_(id & 0b01 ? Side::SERVER : Side::CLIENT),
      direction_(id & 0b10 ? Direction::UNIDIRECTIONAL
                           : Direction::BIDIRECTIONAL),
      inbound_(DataQueue::Create()) {
  MakeWeak();
  state_->id = id;

  // Allows us to be notified when data is actually read from the
  // inbound queue so that we can update the stream flow control.
  inbound_->addBackpressureListener(this);

  const auto defineProperty = [&](auto name, auto value) {
    object
        ->DefineOwnProperty(
            env()->context(), name, value, PropertyAttribute::ReadOnly)
        .Check();
  };

  defineProperty(env()->state_string(), state_.GetArrayBuffer());
  defineProperty(env()->stats_string(), stats_.GetArrayBuffer());

  set_outbound(std::move(source));

  auto params = ngtcp2_conn_get_local_transport_params(this->session());
  STAT_SET(Stats, max_offset, params->initial_max_data);
}

Stream::~Stream() {
  // Make sure that Destroy() was called before Stream is destructed.
  DCHECK(is_destroyed());
}

int64_t Stream::id() const {
  return state_->id;
}

Side Stream::origin() const {
  return origin_;
}

Direction Stream::direction() const {
  return direction_;
}

Session& Stream::session() const {
  return *session_;
}

bool Stream::is_destroyed() const {
  return state_->destroyed;
}

bool Stream::is_eos() const {
  return state_->fin_sent;
}

bool Stream::is_writable() const {
  if (direction() == Direction::UNIDIRECTIONAL) {
    switch (origin()) {
      case Side::CLIENT: {
        if (session_->is_server()) return false;
        break;
      }
      case Side::SERVER: {
        if (!session_->is_server()) return false;
        break;
      }
    }
  }
  return state_->write_ended == 0;
}

bool Stream::is_readable() const {
  if (direction() == Direction::UNIDIRECTIONAL) {
    switch (origin()) {
      case Side::CLIENT: {
        if (!session_->is_server()) return false;
        break;
      }
      case Side::SERVER: {
        if (session_->is_server()) return false;
        break;
      }
    }
  }
  return state_->read_ended == 0;
}

BaseObjectPtr<Blob::Reader> Stream::get_reader() {
  if (!is_readable() || state_->has_reader)
    return BaseObjectPtr<Blob::Reader>();
  state_->has_reader = 1;
  return Blob::Reader::Create(env(), Blob::Create(env(), inbound_));
}

void Stream::set_final_size(uint64_t final_size) {
  DCHECK_IMPLIES(state_->fin_received == 1,
                 final_size <= STAT_GET(Stats, final_size));
  state_->fin_received = 1;
  STAT_SET(Stats, final_size, final_size);
}

void Stream::set_outbound(std::shared_ptr<DataQueue> source) {
  if (!source || is_destroyed() || !is_writable()) return;
  DCHECK_NULL(outbound_);
  outbound_ = std::make_unique<Outbound>(this, std::move(source));
  session_->ResumeStream(id());
}

void Stream::EntryRead(size_t amount) {
  // Tells us that amount bytes were read from inbound_
  // We use this as a signal to extend the flow control
  // window to receive more bytes.
  if (!is_destroyed() && session_) session_->ExtendStreamOffset(id(), amount);
}

int Stream::DoPull(bob::Next<ngtcp2_vec> next,
                   int options,
                   ngtcp2_vec* data,
                   size_t count,
                   size_t max_count_hint) {
  if (is_destroyed() || is_eos()) {
    std::move(next)(bob::Status::STATUS_EOS, nullptr, 0, [](int) {});
    return bob::Status::STATUS_EOS;
  }

  // If an outbound source has not yet been attached, block until one is
  // available. When AttachOutboundSource() is called the stream will be
  // resumed. Note that when we say "block" here we don't mean it in the
  // traditional "block the thread" sense. Instead, this will inform the
  // Session to not try to send any more data from this stream until there
  // is a source attached.
  if (outbound_ == nullptr) {
    std::move(next)(bob::Status::STATUS_BLOCK, nullptr, 0, [](size_t len) {});
    return bob::Status::STATUS_BLOCK;
  }

  return outbound_->Pull(std::move(next), options, data, count, max_count_hint);
}

void Stream::BeginHeaders(HeadersKind kind) {
  if (is_destroyed()) return;
  headers_length_ = 0;
  headers_.clear();
  set_headers_kind(kind);
}

void Stream::set_headers_kind(HeadersKind kind) {
  headers_kind_ = kind;
}

bool Stream::AddHeader(const Header& header) {
  size_t len = header.length();
  if (is_destroyed() || !session_->application().CanAddHeader(
                            headers_.size(), headers_length_, len)) {
    return false;
  }

  headers_length_ += len;

  auto& state = BindingData::Get(env());

  const auto push = [&](auto raw) {
    Local<Value> value;
    if (UNLIKELY(!raw.ToLocal(&value))) return false;
    headers_.push_back(value);
    return true;
  };

  return push(header.GetName(&state)) && push(header.GetValue(&state));
}

void Stream::Acknowledge(size_t datalen) {
  if (is_destroyed() || outbound_ == nullptr) return;

  // ngtcp2 guarantees that offset must always be greater than the previously
  // received offset.
  DCHECK_GE(datalen, STAT_GET(Stats, max_offset_ack));
  STAT_SET(Stats, max_offset_ack, datalen);

  // // Consumes the given number of bytes in the buffer.
  outbound_->Acknowledge(datalen);
}

void Stream::Commit(size_t datalen) {
  if (!is_destroyed() && outbound_) outbound_->Commit(datalen);
}

void Stream::EndWritable() {
  if (is_destroyed() || !is_writable()) return;
  // If an outbound_ has been attached, we want to mark it as being ended.
  // If the outbound_ is wrapping an idempotent DataQueue, then capping
  // will be a non-op since we're not going to be writing any more data
  // into it anyway.
  if (outbound_ != nullptr) outbound_->Cap();
  state_->write_ended = 1;
}

void Stream::EndReadable(std::optional<uint64_t> maybe_final_size) {
  if (is_destroyed() || !is_readable()) return;
  state_->read_ended = 1;
  set_final_size(maybe_final_size.value_or(STAT_GET(Stats, bytes_received)));
  inbound_->cap(STAT_GET(Stats, final_size));
}

void Stream::Destroy(QuicError error) {
  if (is_destroyed()) return;
  DCHECK_NOT_NULL(session_.get());
  Debug(this, "Stream %" PRIi64 " being destroyed with error %s", id(), error);

  // End the writable before marking as destroyed.
  EndWritable();

  // Also end the readable side if it isn't already.
  EndReadable();

  state_->destroyed = 1;

  EmitClose(error);

  // We are going to release our reference to the outbound_ queue here.
  outbound_.reset();

  // We reset the inbound here also. However, it's important to note that
  // the JavaScript side could still have a reader on the inbound DataQueue,
  // which may keep that data alive a bit longer.
  inbound_->removeBackpressureListener(this);

  inbound_.reset();

  CHECK_NOT_NULL(session_.get());

  // Finally, remove the stream from the session and clear our reference
  // to the session.
  session_->RemoveStream(id());
}

void Stream::ReceiveData(const uint8_t* data,
                         size_t len,
                         ReceiveDataFlags flags) {
  if (is_destroyed()) return;

  // If reading has ended, or there is no data, there's nothing to do but maybe
  // end the readable side if this is the last bit of data we've received.
  if (state_->read_ended == 1 || len == 0) {
    if (flags.fin) EndReadable();
    return;
  }

  STAT_INCREMENT_N(Stats, bytes_received, len);
  auto backing = ArrayBuffer::NewBackingStore(env()->isolate(), len);
  memcpy(backing->Data(), data, len);
  inbound_->append(DataQueue::CreateInMemoryEntryFromBackingStore(
      std::move(backing), 0, len));
  if (flags.fin) EndReadable();
}

void Stream::ReceiveStopSending(QuicError error) {
  // Note that this comes from *this* endpoint, not the other side. We handle it
  // if we haven't already shutdown our *receiving* side of the stream.
  if (is_destroyed() || state_->read_ended) return;
  ngtcp2_conn_shutdown_stream_read(session(), 0, id(), error.code());
  EndReadable();
}

void Stream::ReceiveStreamReset(uint64_t final_size, QuicError error) {
  // Importantly, reset stream only impacts the inbound data flow. It has no
  // impact on the outbound data flow. It is essentially a signal that the peer
  // has abruptly terminated the writable end of their stream with an error.
  // Any data we have received up to this point remains in the queue waiting to
  // be read.
  EndReadable(final_size);
  EmitReset(error);
}

// ============================================================================

void Stream::EmitBlocked() {
  // state_->wants_block will be set from the javascript side if the
  // stream object has a handler for the blocked event.
  if (is_destroyed() || !env()->can_call_into_js() ||
      state_->wants_block == 0) {
    return;
  }
  CallbackScope<Stream> cb_scope(this);
  MakeCallback(BindingData::Get(env()).stream_blocked_callback(), 0, nullptr);
}

void Stream::EmitClose(const QuicError& error) {
  if (is_destroyed() || !env()->can_call_into_js()) return;
  CallbackScope<Stream> cb_scope(this);
  Local<Value> err;
  if (!error.ToV8Value(env()).ToLocal(&err)) return;

  MakeCallback(BindingData::Get(env()).stream_close_callback(), 1, &err);
}

void Stream::EmitHeaders() {
  if (is_destroyed() || !env()->can_call_into_js() ||
      state_->wants_headers == 0) {
    return;
  }
  CallbackScope<Stream> cb_scope(this);

  Local<Value> argv[] = {
      Array::New(env()->isolate(), headers_.data(), headers_.size()),
      Integer::NewFromUnsigned(env()->isolate(),
                               static_cast<uint32_t>(headers_kind_))};

  headers_.clear();

  MakeCallback(
      BindingData::Get(env()).stream_headers_callback(), arraysize(argv), argv);
}

void Stream::EmitReset(const QuicError& error) {
  if (is_destroyed() || !env()->can_call_into_js() ||
      state_->wants_reset == 0) {
    return;
  }
  CallbackScope<Stream> cb_scope(this);
  Local<Value> err;
  if (!error.ToV8Value(env()).ToLocal(&err)) return;

  MakeCallback(BindingData::Get(env()).stream_reset_callback(), 1, &err);
}

void Stream::EmitWantTrailers() {
  if (is_destroyed() || !env()->can_call_into_js() ||
      state_->wants_trailers == 0) {
    return;
  }
  CallbackScope<Stream> cb_scope(this);
  MakeCallback(BindingData::Get(env()).stream_trailers_callback(), 0, nullptr);
}

// ============================================================================

void Stream::Schedule(Stream::Queue* queue) {
  // If this stream is not already in the queue to send data, add it.
  if (!is_destroyed() && outbound_ && stream_queue_.IsEmpty())
    queue->PushBack(this);
}

void Stream::Unschedule() {
  stream_queue_.Remove();
}

}  // namespace quic
}  // namespace node

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
