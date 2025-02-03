#include "stream_pipe.h"
#include "stream_base-inl.h"
#include "node_buffer.h"
#include "util-inl.h"

namespace node {

using v8::BackingStore;
using v8::Context;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Isolate;
using v8::Just;
using v8::Local;
using v8::Maybe;
using v8::Nothing;
using v8::Object;
using v8::Value;

StreamPipe::StreamPipe(StreamBase* source,
                       StreamBase* sink,
                       Local<Object> obj)
    : AsyncWrap(source->stream_env(), obj, AsyncWrap::PROVIDER_STREAMPIPE) {
  MakeWeak();

  CHECK_NOT_NULL(sink);
  CHECK_NOT_NULL(source);

  source->PushStreamListener(&readable_listener_);
  sink->PushStreamListener(&writable_listener_);

  uses_wants_write_ = sink->HasWantsWrite();
}

StreamPipe::~StreamPipe() {
  Unpipe(true);
}

StreamBase* StreamPipe::source() {
  return static_cast<StreamBase*>(readable_listener_.stream());
}

StreamBase* StreamPipe::sink() {
  return static_cast<StreamBase*>(writable_listener_.stream());
}

void StreamPipe::Unpipe(bool is_in_deletion) {
  if (is_closed_)
    return;

  // Note that we possibly cannot use virtual methods on `source` and `sink`
  // here, because this function can be called from their destructors via
  // `OnStreamDestroy()`.
  if (!source_destroyed_)
    source()->ReadStop();

  is_closed_ = true;
  is_reading_ = false;
  source()->RemoveStreamListener(&readable_listener_);
  if (pending_writes_ == 0)
    sink()->RemoveStreamListener(&writable_listener_);

  if (is_in_deletion) return;

  // Delay the JS-facing part with SetImmediate, because this might be from
  // inside the garbage collector, so we can’t run JS here.
  HandleScope handle_scope(env()->isolate());
  BaseObjectPtr<StreamPipe> strong_ref{this};
  env()->SetImmediate([this, strong_ref](Environment* env) {
    HandleScope handle_scope(env->isolate());
    Context::Scope context_scope(env->context());
    Local<Object> object = this->object();

    Local<Value> onunpipe;
    if (!object->Get(env->context(), env->onunpipe_string()).ToLocal(&onunpipe))
      return;
    if (onunpipe->IsFunction() &&
        MakeCallback(onunpipe.As<Function>(), 0, nullptr).IsEmpty()) {
      return;
    }

    // Set all the links established in the constructor to `null`.
    Local<Value> null = Null(env->isolate());

    Local<Value> source_v;
    Local<Value> sink_v;
    if (!object->Get(env->context(), env->source_string()).ToLocal(&source_v) ||
        !object->Get(env->context(), env->sink_string()).ToLocal(&sink_v) ||
        !source_v->IsObject() || !sink_v->IsObject()) {
      return;
    }

    if (object->Set(env->context(), env->source_string(), null).IsNothing() ||
        object->Set(env->context(), env->sink_string(), null).IsNothing() ||
        source_v.As<Object>()
            ->Set(env->context(), env->pipe_target_string(), null)
            .IsNothing() ||
        sink_v.As<Object>()
            ->Set(env->context(), env->pipe_source_string(), null)
            .IsNothing()) {
      return;
    }
  });
}

uv_buf_t StreamPipe::ReadableListener::OnStreamAlloc(size_t suggested_size) {
  StreamPipe* pipe = ContainerOf(&StreamPipe::readable_listener_, this);
  size_t size = std::min(suggested_size, pipe->wanted_data_);
  CHECK_GT(size, 0);
  return pipe->env()->allocate_managed_buffer(size);
}

void StreamPipe::ReadableListener::OnStreamRead(ssize_t nread,
                                                const uv_buf_t& buf_) {
  StreamPipe* pipe = ContainerOf(&StreamPipe::readable_listener_, this);
  std::unique_ptr<BackingStore> bs = pipe->env()->release_managed_buffer(buf_);
  if (nread < 0) {
    // EOF or error; stop reading and pass the error to the previous listener
    // (which might end up in JS).
    pipe->is_eof_ = true;
    // Cache `sink()` here because the previous listener might do things
    // that eventually lead to an `Unpipe()` call.
    StreamBase* sink = pipe->sink();
    stream()->ReadStop();
    CHECK_NOT_NULL(previous_listener_);
    previous_listener_->OnStreamRead(nread, uv_buf_init(nullptr, 0));
    // If we’re not writing, close now. Otherwise, we’ll do that in
    // `OnStreamAfterWrite()`.
    if (pipe->pending_writes_ == 0) {
      sink->Shutdown();
      pipe->Unpipe();
    }
    return;
  }

  pipe->ProcessData(nread, std::move(bs));
}

void StreamPipe::ProcessData(size_t nread,
                             std::unique_ptr<BackingStore> bs) {
  CHECK(uses_wants_write_ || pending_writes_ == 0);
  uv_buf_t buffer = uv_buf_init(static_cast<char*>(bs->Data()), nread);
  StreamWriteResult res = sink()->Write(&buffer, 1);
  pending_writes_++;
  if (!res.async) {
    writable_listener_.OnStreamAfterWrite(nullptr, res.err);
  } else {
    is_reading_ = false;
    res.wrap->SetBackingStore(std::move(bs));
    if (source() != nullptr)
      source()->ReadStop();
  }
}

void StreamPipe::WritableListener::OnStreamAfterWrite(WriteWrap* w,
                                                      int status) {
  StreamPipe* pipe = ContainerOf(&StreamPipe::writable_listener_, this);
  pipe->pending_writes_--;
  if (pipe->is_closed_) {
    if (pipe->pending_writes_ == 0) {
      Environment* env = pipe->env();
      HandleScope handle_scope(env->isolate());
      Context::Scope context_scope(env->context());
      if (pipe->MakeCallback(env->oncomplete_string(), 0, nullptr).IsEmpty())
        return;
      stream()->RemoveStreamListener(this);
    }
    return;
  }

  if (pipe->is_eof_) {
    HandleScope handle_scope(pipe->env()->isolate());
    InternalCallbackScope callback_scope(pipe,
        InternalCallbackScope::kSkipTaskQueues);
    pipe->sink()->Shutdown();
    pipe->Unpipe();
    return;
  }

  if (status != 0) {
    CHECK_NOT_NULL(previous_listener_);
    StreamListener* prev = previous_listener_;
    pipe->Unpipe();
    prev->OnStreamAfterWrite(w, status);
    return;
  }

  if (!pipe->uses_wants_write_) {
    OnStreamWantsWrite(65536);
  }
}

void StreamPipe::WritableListener::OnStreamAfterShutdown(ShutdownWrap* w,
                                                         int status) {
  StreamPipe* pipe = ContainerOf(&StreamPipe::writable_listener_, this);
  CHECK_NOT_NULL(previous_listener_);
  StreamListener* prev = previous_listener_;
  pipe->Unpipe();
  prev->OnStreamAfterShutdown(w, status);
}

void StreamPipe::ReadableListener::OnStreamDestroy() {
  StreamPipe* pipe = ContainerOf(&StreamPipe::readable_listener_, this);
  pipe->source_destroyed_ = true;
  if (!pipe->is_eof_) {
    OnStreamRead(UV_EPIPE, uv_buf_init(nullptr, 0));
  }
}

void StreamPipe::WritableListener::OnStreamDestroy() {
  StreamPipe* pipe = ContainerOf(&StreamPipe::writable_listener_, this);
  pipe->sink_destroyed_ = true;
  pipe->is_eof_ = true;
  pipe->pending_writes_ = 0;
  pipe->Unpipe();
}

void StreamPipe::WritableListener::OnStreamWantsWrite(size_t suggested_size) {
  StreamPipe* pipe = ContainerOf(&StreamPipe::writable_listener_, this);
  pipe->wanted_data_ = suggested_size;
  if (pipe->is_reading_ || pipe->is_closed_)
    return;
  HandleScope handle_scope(pipe->env()->isolate());
  InternalCallbackScope callback_scope(pipe,
      InternalCallbackScope::kSkipTaskQueues);
  pipe->is_reading_ = true;
  pipe->source()->ReadStart();
}

uv_buf_t StreamPipe::WritableListener::OnStreamAlloc(size_t suggested_size) {
  CHECK_NOT_NULL(previous_listener_);
  return previous_listener_->OnStreamAlloc(suggested_size);
}

void StreamPipe::WritableListener::OnStreamRead(ssize_t nread,
                                                const uv_buf_t& buf) {
  CHECK_NOT_NULL(previous_listener_);
  return previous_listener_->OnStreamRead(nread, buf);
}

Maybe<StreamPipe*> StreamPipe::New(StreamBase* source,
                                   StreamBase* sink,
                                   Local<Object> obj) {
  std::unique_ptr<StreamPipe> stream_pipe(new StreamPipe(source, sink, obj));

  // Set up links between this object and the source/sink objects.
  // In particular, this makes sure that they are garbage collected as a group,
  // if that applies to the given streams (for example, Http2Streams use
  // weak references).
  Environment* env = source->stream_env();
  if (obj->Set(env->context(), env->source_string(), source->GetObject())
          .IsNothing()) {
    return Nothing<StreamPipe*>();
  }
  if (source->GetObject()
          ->Set(env->context(), env->pipe_target_string(), obj)
          .IsNothing()) {
    return Nothing<StreamPipe*>();
  }
  if (obj->Set(env->context(), env->sink_string(), sink->GetObject())
          .IsNothing()) {
    return Nothing<StreamPipe*>();
  }
  if (sink->GetObject()
          ->Set(env->context(), env->pipe_source_string(), obj)
          .IsNothing()) {
    return Nothing<StreamPipe*>();
  }

  return Just(stream_pipe.release());
}

void StreamPipe::New(const FunctionCallbackInfo<Value>& args) {
  CHECK(args.IsConstructCall());
  CHECK(args[0]->IsObject());
  CHECK(args[1]->IsObject());
  StreamBase* source = StreamBase::FromObject(args[0].As<Object>());
  StreamBase* sink = StreamBase::FromObject(args[1].As<Object>());

  if (StreamPipe::New(source, sink, args.This()).IsNothing()) return;
}

void StreamPipe::Start(const FunctionCallbackInfo<Value>& args) {
  StreamPipe* pipe;
  ASSIGN_OR_RETURN_UNWRAP(&pipe, args.This());
  pipe->is_closed_ = false;
  pipe->writable_listener_.OnStreamWantsWrite(65536);
}

void StreamPipe::Unpipe(const FunctionCallbackInfo<Value>& args) {
  StreamPipe* pipe;
  ASSIGN_OR_RETURN_UNWRAP(&pipe, args.This());
  pipe->Unpipe();
}

void StreamPipe::IsClosed(const FunctionCallbackInfo<Value>& args) {
  StreamPipe* pipe;
  ASSIGN_OR_RETURN_UNWRAP(&pipe, args.This());
  args.GetReturnValue().Set(pipe->is_closed_);
}

void StreamPipe::PendingWrites(const FunctionCallbackInfo<Value>& args) {
  StreamPipe* pipe;
  ASSIGN_OR_RETURN_UNWRAP(&pipe, args.This());
  args.GetReturnValue().Set(pipe->pending_writes_);
}

namespace {

void InitializeStreamPipe(Local<Object> target,
                          Local<Value> unused,
                          Local<Context> context,
                          void* priv) {
  Environment* env = Environment::GetCurrent(context);
  Isolate* isolate = env->isolate();

  // Create FunctionTemplate for FileHandle::CloseReq
  Local<FunctionTemplate> pipe = NewFunctionTemplate(isolate, StreamPipe::New);
  SetProtoMethod(isolate, pipe, "unpipe", StreamPipe::Unpipe);
  SetProtoMethod(isolate, pipe, "start", StreamPipe::Start);
  SetProtoMethod(isolate, pipe, "isClosed", StreamPipe::IsClosed);
  SetProtoMethod(isolate, pipe, "pendingWrites", StreamPipe::PendingWrites);
  pipe->Inherit(AsyncWrap::GetConstructorTemplate(env));
  pipe->InstanceTemplate()->SetInternalFieldCount(
      StreamPipe::kInternalFieldCount);
  SetConstructorFunction(context, target, "StreamPipe", pipe);
}

}  // anonymous namespace

}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(stream_pipe, node::InitializeStreamPipe)
