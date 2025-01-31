#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC

#include "logstream.h"
#include <async_wrap-inl.h>
#include <base_object-inl.h>
#include <env-inl.h>
#include <memory_tracker-inl.h>
#include <node_external_reference.h>
#include <stream_base-inl.h>
#include <uv.h>
#include <v8.h>
#include "bindingdata.h"

namespace node {

using v8::FunctionTemplate;
using v8::Local;
using v8::Object;

namespace quic {

Local<FunctionTemplate> LogStream::GetConstructorTemplate(Environment* env) {
  auto& state = BindingData::Get(env);
  auto tmpl = state.logstream_constructor_template();
  if (tmpl.IsEmpty()) {
    tmpl = FunctionTemplate::New(env->isolate());
    tmpl->Inherit(AsyncWrap::GetConstructorTemplate(env));
    tmpl->InstanceTemplate()->SetInternalFieldCount(
        StreamBase::kInternalFieldCount);
    tmpl->SetClassName(state.logstream_string());
    StreamBase::AddMethods(env, tmpl);
    state.set_logstream_constructor_template(tmpl);
  }
  return tmpl;
}

BaseObjectPtr<LogStream> LogStream::Create(Environment* env) {
  v8::Local<v8::Object> obj;
  if (!GetConstructorTemplate(env)
           ->InstanceTemplate()
           ->NewInstance(env->context())
           .ToLocal(&obj)) {
    return {};
  }
  return MakeDetachedBaseObject<LogStream>(env, obj);
}

LogStream::LogStream(Environment* env, Local<Object> obj)
    : AsyncWrap(env, obj, AsyncWrap::PROVIDER_QUIC_LOGSTREAM), StreamBase(env) {
  MakeWeak();
  StreamBase::AttachToObject(GetObject());
}

void LogStream::Emit(const uint8_t* data, size_t len, EmitOption option) {
  if (fin_seen_) return;
  fin_seen_ = option == EmitOption::FIN;

  size_t remaining = len;
  // If the len is greater than the size of the buffer returned by
  // EmitAlloc then EmitRead will be called multiple times.
  while (remaining != 0) {
    uv_buf_t buf = EmitAlloc(len);
    size_t len = std::min<size_t>(remaining, buf.len);
    memcpy(buf.base, data, len);
    remaining -= len;
    data += len;
    // If we are actively reading from the stream, we'll call emit
    // read immediately. Otherwise we buffer the chunk and will push
    // the chunks out the next time ReadStart() is called.
    if (reading_) {
      EmitRead(len, buf);
    } else {
      // The total measures the total memory used so we always
      // increment but buf.len and not chunk len.
      ensure_space(buf.len);
      total_ += buf.len;
      buffer_.push_back(Chunk{len, buf});
    }
  }

  if (ended_ && reading_) {
    EmitRead(UV_EOF);
  }
}

void LogStream::Emit(const std::string_view line, EmitOption option) {
  Emit(reinterpret_cast<const uint8_t*>(line.data()), line.length(), option);
}

void LogStream::End() {
  ended_ = true;
}

int LogStream::ReadStart() {
  if (reading_) return 0;
  // Flush any chunks that have already been buffered.
  for (const auto& chunk : buffer_) EmitRead(chunk.len, chunk.buf);
  total_ = 0;
  buffer_.clear();
  if (fin_seen_) {
    // If we've already received the fin, there's nothing else to wait for.
    EmitRead(UV_EOF);
    return ReadStop();
  }
  // Otherwise, we're going to wait for more chunks to be written.
  reading_ = true;
  return 0;
}

int LogStream::ReadStop() {
  reading_ = false;
  return 0;
}

// We do not use either of these.
int LogStream::DoShutdown(ShutdownWrap* req_wrap) {
  UNREACHABLE();
}
int LogStream::DoWrite(WriteWrap* w,
                       uv_buf_t* bufs,
                       size_t count,
                       uv_stream_t* send_handle) {
  UNREACHABLE();
}

bool LogStream::IsAlive() {
  return !ended_;
}

bool LogStream::IsClosing() {
  return ended_;
}

AsyncWrap* LogStream::GetAsyncWrap() {
  return this;
}

void LogStream::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackFieldWithSize("buffer", total_);
}

// The LogStream buffer enforces a maximum size of kMaxLogStreamBuffer.
void LogStream::ensure_space(size_t amt) {
  while (total_ + amt > kMaxLogStreamBuffer) {
    total_ -= buffer_.front().buf.len;
    buffer_.pop_front();
  }
}
}  // namespace quic
}  // namespace node

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
