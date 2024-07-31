#include "stream_base.h"  // NOLINT(build/include_inline)
#include "stream_base-inl.h"
#include "stream_wrap.h"

#include "env-inl.h"
#include "js_stream.h"
#include "node.h"
#include "node_buffer.h"
#include "node_errors.h"
#include "node_external_reference.h"
#include "string_bytes.h"
#include "util-inl.h"
#include "v8.h"

#include <climits>  // INT_MAX

namespace node {

using v8::Array;
using v8::ArrayBuffer;
using v8::BackingStore;
using v8::ConstructorBehavior;
using v8::Context;
using v8::DontDelete;
using v8::DontEnum;
using v8::External;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::MaybeLocal;
using v8::Object;
using v8::PropertyAttribute;
using v8::ReadOnly;
using v8::SideEffectType;
using v8::Signature;
using v8::String;
using v8::Value;

int StreamBase::Shutdown(v8::Local<v8::Object> req_wrap_obj) {
  Environment* env = stream_env();

  v8::HandleScope handle_scope(env->isolate());

  if (req_wrap_obj.IsEmpty()) {
    if (!env->shutdown_wrap_template()
             ->NewInstance(env->context())
             .ToLocal(&req_wrap_obj)) {
      return UV_EBUSY;
    }
    StreamReq::ResetObject(req_wrap_obj);
  }

  BaseObjectPtr<AsyncWrap> req_wrap_ptr;
  AsyncHooks::DefaultTriggerAsyncIdScope trigger_scope(GetAsyncWrap());
  ShutdownWrap* req_wrap = CreateShutdownWrap(req_wrap_obj);
  if (req_wrap != nullptr) req_wrap_ptr.reset(req_wrap->GetAsyncWrap());
  int err = DoShutdown(req_wrap);

  if (err != 0 && req_wrap != nullptr) {
    req_wrap->Dispose();
  }

  const char* msg = Error();
  if (msg != nullptr) {
    if (req_wrap_obj
            ->Set(env->context(),
                  env->error_string(),
                  OneByteString(env->isolate(), msg))
            .IsNothing()) {
      return UV_EBUSY;
    }
    ClearError();
  }

  return err;
}

StreamWriteResult StreamBase::Write(uv_buf_t* bufs,
                                    size_t count,
                                    uv_stream_t* send_handle,
                                    v8::Local<v8::Object> req_wrap_obj,
                                    bool skip_try_write) {
  Environment* env = stream_env();
  int err;

  size_t total_bytes = 0;
  for (size_t i = 0; i < count; ++i) total_bytes += bufs[i].len;
  bytes_written_ += total_bytes;

  if (send_handle == nullptr && HasDoTryWrite() && !skip_try_write) {
    err = DoTryWrite(&bufs, &count);
    if (err != 0 || count == 0) {
      return StreamWriteResult{false, err, nullptr, total_bytes, {}};
    }
  }

  v8::HandleScope handle_scope(env->isolate());

  if (req_wrap_obj.IsEmpty()) {
    if (!env->write_wrap_template()
             ->NewInstance(env->context())
             .ToLocal(&req_wrap_obj)) {
      return StreamWriteResult{false, UV_EBUSY, nullptr, 0, {}};
    }
    StreamReq::ResetObject(req_wrap_obj);
  }

  AsyncHooks::DefaultTriggerAsyncIdScope trigger_scope(GetAsyncWrap());
  WriteWrap* req_wrap = CreateWriteWrap(req_wrap_obj);
  BaseObjectPtr<AsyncWrap> req_wrap_ptr(req_wrap->GetAsyncWrap());

  err = DoWrite(req_wrap, bufs, count, send_handle);
  bool async = err == 0;

  if (!async) {
    req_wrap->Dispose();
    req_wrap = nullptr;
  }

  const char* msg = Error();
  if (msg != nullptr) {
    if (req_wrap_obj
            ->Set(env->context(),
                  env->error_string(),
                  OneByteString(env->isolate(), msg))
            .IsNothing()) {
      return StreamWriteResult{false, UV_EBUSY, nullptr, 0, {}};
    }
    ClearError();
  }

  return StreamWriteResult{
      async, err, req_wrap, total_bytes, std::move(req_wrap_ptr)};
}

template int StreamBase::WriteString<ASCII>(
    const FunctionCallbackInfo<Value>& args);
template int StreamBase::WriteString<UTF8>(
    const FunctionCallbackInfo<Value>& args);
template int StreamBase::WriteString<UCS2>(
    const FunctionCallbackInfo<Value>& args);
template int StreamBase::WriteString<LATIN1>(
    const FunctionCallbackInfo<Value>& args);


int StreamBase::ReadStartJS(const FunctionCallbackInfo<Value>& args) {
  return ReadStart();
}


int StreamBase::ReadStopJS(const FunctionCallbackInfo<Value>& args) {
  return ReadStop();
}

int StreamBase::UseUserBuffer(const FunctionCallbackInfo<Value>& args) {
  CHECK(Buffer::HasInstance(args[0]));

  uv_buf_t buf = uv_buf_init(Buffer::Data(args[0]), Buffer::Length(args[0]));
  PushStreamListener(new CustomBufferJSListener(buf));
  return 0;
}

int StreamBase::Shutdown(const FunctionCallbackInfo<Value>& args) {
  CHECK(args[0]->IsObject());
  Local<Object> req_wrap_obj = args[0].As<Object>();

  return Shutdown(req_wrap_obj);
}

void StreamBase::SetWriteResult(const StreamWriteResult& res) {
  env_->stream_base_state()[kBytesWritten] = res.bytes;
  env_->stream_base_state()[kLastWriteWasAsync] = res.async;
}

int StreamBase::Writev(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();
  Local<Context> context = env->context();

  CHECK(args[0]->IsObject());
  CHECK(args[1]->IsArray());

  Local<Object> req_wrap_obj = args[0].As<Object>();
  Local<Array> chunks = args[1].As<Array>();
  bool all_buffers = args[2]->IsTrue();

  size_t count;
  if (all_buffers)
    count = chunks->Length();
  else
    count = chunks->Length() >> 1;

  MaybeStackBuffer<uv_buf_t, 16> bufs(count);

  size_t storage_size = 0;
  size_t offset;

  if (!all_buffers) {
    // Determine storage size first
    for (size_t i = 0; i < count; i++) {
      Local<Value> chunk;
      if (!chunks->Get(context, i * 2).ToLocal(&chunk))
        return -1;

      if (Buffer::HasInstance(chunk))
        continue;
        // Buffer chunk, no additional storage required

      // String chunk
      Local<String> string;
      if (!chunk->ToString(context).ToLocal(&string))
        return -1;
      Local<Value> next_chunk;
      if (!chunks->Get(context, i * 2 + 1).ToLocal(&next_chunk))
        return -1;
      enum encoding encoding = ParseEncoding(isolate, next_chunk);
      size_t chunk_size;
      if ((encoding == UTF8 &&
             string->Length() > 65535 &&
             !StringBytes::Size(isolate, string, encoding).To(&chunk_size)) ||
              !StringBytes::StorageSize(isolate, string, encoding)
                  .To(&chunk_size)) {
        return -1;
      }
      storage_size += chunk_size;
    }

    if (storage_size > INT_MAX)
      return UV_ENOBUFS;
  } else {
    for (size_t i = 0; i < count; i++) {
      Local<Value> chunk;
      if (!chunks->Get(context, i).ToLocal(&chunk))
        return -1;
      bufs[i].base = Buffer::Data(chunk);
      bufs[i].len = Buffer::Length(chunk);
    }
  }

  std::unique_ptr<BackingStore> bs;
  if (storage_size > 0) {
    NoArrayBufferZeroFillScope no_zero_fill_scope(env->isolate_data());
    bs = ArrayBuffer::NewBackingStore(isolate, storage_size);
  }

  offset = 0;
  if (!all_buffers) {
    for (size_t i = 0; i < count; i++) {
      Local<Value> chunk;
      if (!chunks->Get(context, i * 2).ToLocal(&chunk))
        return -1;

      // Write buffer
      if (Buffer::HasInstance(chunk)) {
        bufs[i].base = Buffer::Data(chunk);
        bufs[i].len = Buffer::Length(chunk);
        continue;
      }

      // Write string
      CHECK_LE(offset, storage_size);
      char* str_storage =
          static_cast<char*>(bs ? bs->Data() : nullptr) + offset;
      size_t str_size = (bs ? bs->ByteLength() : 0) - offset;

      Local<String> string;
      if (!chunk->ToString(context).ToLocal(&string))
        return -1;
      Local<Value> next_chunk;
      if (!chunks->Get(context, i * 2 + 1).ToLocal(&next_chunk))
        return -1;
      enum encoding encoding = ParseEncoding(isolate, next_chunk);
      str_size = StringBytes::Write(isolate,
                                    str_storage,
                                    str_size,
                                    string,
                                    encoding);
      bufs[i].base = str_storage;
      bufs[i].len = str_size;
      offset += str_size;
    }
  }

  StreamWriteResult res = Write(*bufs, count, nullptr, req_wrap_obj);
  SetWriteResult(res);
  if (res.wrap != nullptr && storage_size > 0)
    res.wrap->SetBackingStore(std::move(bs));
  return res.err;
}


int StreamBase::WriteBuffer(const FunctionCallbackInfo<Value>& args) {
  CHECK(args[0]->IsObject());

  Environment* env = Environment::GetCurrent(args);

  if (!args[1]->IsUint8Array()) {
    node::THROW_ERR_INVALID_ARG_TYPE(env, "Second argument must be a buffer");
    return 0;
  }

  Local<Object> req_wrap_obj = args[0].As<Object>();
  uv_buf_t buf;
  buf.base = Buffer::Data(args[1]);
  buf.len = Buffer::Length(args[1]);

  uv_stream_t* send_handle = nullptr;

  if (args[2]->IsObject() && IsIPCPipe()) {
    Local<Object> send_handle_obj = args[2].As<Object>();

    HandleWrap* wrap;
    ASSIGN_OR_RETURN_UNWRAP(&wrap, send_handle_obj, UV_EINVAL);
    send_handle = reinterpret_cast<uv_stream_t*>(wrap->GetHandle());
    // Reference LibuvStreamWrap instance to prevent it from being garbage
    // collected before `AfterWrite` is called.
    if (req_wrap_obj->Set(env->context(),
                          env->handle_string(),
                          send_handle_obj).IsNothing()) {
      return -1;
    }
  }

  StreamWriteResult res = Write(&buf, 1, send_handle, req_wrap_obj);
  SetWriteResult(res);

  return res.err;
}


template <enum encoding enc>
int StreamBase::WriteString(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();
  CHECK(args[0]->IsObject());
  CHECK(args[1]->IsString());

  Local<Object> req_wrap_obj = args[0].As<Object>();
  Local<String> string = args[1].As<String>();
  Local<Object> send_handle_obj;
  if (args[2]->IsObject())
    send_handle_obj = args[2].As<Object>();

  // Compute the size of the storage that the string will be flattened into.
  // For UTF8 strings that are very long, go ahead and take the hit for
  // computing their actual size, rather than tripling the storage.
  size_t storage_size;
  if ((enc == UTF8 &&
         string->Length() > 65535 &&
         !StringBytes::Size(isolate, string, enc).To(&storage_size)) ||
          !StringBytes::StorageSize(isolate, string, enc).To(&storage_size)) {
    return -1;
  }

  if (storage_size > INT_MAX)
    return UV_ENOBUFS;

  // Try writing immediately if write size isn't too big
  char stack_storage[16384];  // 16kb
  size_t data_size;
  size_t synchronously_written = 0;
  uv_buf_t buf;

  bool try_write = HasDoTryWrite() && storage_size <= sizeof(stack_storage) &&
                   (!IsIPCPipe() || send_handle_obj.IsEmpty());
  if (try_write) {
    data_size = StringBytes::Write(isolate,
                                   stack_storage,
                                   storage_size,
                                   string,
                                   enc);
    buf = uv_buf_init(stack_storage, data_size);

    uv_buf_t* bufs = &buf;
    size_t count = 1;
    const int err = DoTryWrite(&bufs, &count);
    // Keep track of the bytes written here, because we're taking a shortcut
    // by using `DoTryWrite()` directly instead of using the utilities
    // provided by `Write()`.
    synchronously_written = count == 0 ? data_size : data_size - buf.len;
    bytes_written_ += synchronously_written;

    // Immediate failure or success
    if (err != 0 || count == 0) {
      SetWriteResult(StreamWriteResult { false, err, nullptr, data_size, {} });
      return err;
    }

    // Partial write
    CHECK_EQ(count, 1);
  }

  std::unique_ptr<BackingStore> bs;

  if (try_write) {
    // Copy partial data
    NoArrayBufferZeroFillScope no_zero_fill_scope(env->isolate_data());
    bs = ArrayBuffer::NewBackingStore(isolate, buf.len);
    memcpy(static_cast<char*>(bs->Data()), buf.base, buf.len);
    data_size = buf.len;
  } else {
    // Write it
    NoArrayBufferZeroFillScope no_zero_fill_scope(env->isolate_data());
    bs = ArrayBuffer::NewBackingStore(isolate, storage_size);
    data_size = StringBytes::Write(isolate,
                                   static_cast<char*>(bs->Data()),
                                   storage_size,
                                   string,
                                   enc);
  }

  CHECK_LE(data_size, storage_size);

  buf = uv_buf_init(static_cast<char*>(bs->Data()), data_size);

  uv_stream_t* send_handle = nullptr;

  if (IsIPCPipe() && !send_handle_obj.IsEmpty()) {
    HandleWrap* wrap;
    ASSIGN_OR_RETURN_UNWRAP(&wrap, send_handle_obj, UV_EINVAL);
    send_handle = reinterpret_cast<uv_stream_t*>(wrap->GetHandle());
    // Reference LibuvStreamWrap instance to prevent it from being garbage
    // collected before `AfterWrite` is called.
    if (req_wrap_obj->Set(env->context(),
                          env->handle_string(),
                          send_handle_obj).IsNothing()) {
      return -1;
    }
  }

  StreamWriteResult res = Write(&buf, 1, send_handle, req_wrap_obj, try_write);
  res.bytes += synchronously_written;

  SetWriteResult(res);
  if (res.wrap != nullptr)
    res.wrap->SetBackingStore(std::move(bs));

  return res.err;
}


MaybeLocal<Value> StreamBase::CallJSOnreadMethod(ssize_t nread,
                                                 Local<ArrayBuffer> ab,
                                                 size_t offset,
                                                 StreamBaseJSChecks checks) {
  Environment* env = env_;

  DCHECK_EQ(static_cast<int32_t>(nread), nread);
  DCHECK_LE(offset, INT32_MAX);

  if (checks == DONT_SKIP_NREAD_CHECKS) {
    if (ab.IsEmpty()) {
      DCHECK_EQ(offset, 0);
      DCHECK_LE(nread, 0);
    } else {
      DCHECK_GE(nread, 0);
    }
  }

  env->stream_base_state()[kReadBytesOrError] = static_cast<int32_t>(nread);
  env->stream_base_state()[kArrayBufferOffset] = offset;

  Local<Value> argv[] = {
    ab.IsEmpty() ? Undefined(env->isolate()).As<Value>() : ab.As<Value>()
  };

  AsyncWrap* wrap = GetAsyncWrap();
  CHECK_NOT_NULL(wrap);
  Local<Value> onread = wrap->object()
                            ->GetInternalField(StreamBase::kOnReadFunctionField)
                            .As<Value>();
  CHECK(onread->IsFunction());
  return wrap->MakeCallback(onread.As<Function>(), arraysize(argv), argv);
}


bool StreamBase::IsIPCPipe() {
  return false;
}


int StreamBase::GetFD() {
  return -1;
}


Local<Object> StreamBase::GetObject() {
  return GetAsyncWrap()->object();
}

void StreamBase::AddAccessor(v8::Isolate* isolate,
                             v8::Local<v8::Signature> signature,
                             enum v8::PropertyAttribute attributes,
                             v8::Local<v8::FunctionTemplate> t,
                             JSMethodFunction* getter,
                             JSMethodFunction* setter,
                             v8::Local<v8::String> string) {
  Local<FunctionTemplate> getter_templ =
      NewFunctionTemplate(isolate,
                          getter,
                          signature,
                          ConstructorBehavior::kThrow,
                          SideEffectType::kHasNoSideEffect);
  Local<FunctionTemplate> setter_templ =
      NewFunctionTemplate(isolate,
                          setter,
                          signature,
                          ConstructorBehavior::kThrow,
                          SideEffectType::kHasSideEffect);
  t->PrototypeTemplate()->SetAccessorProperty(
      string, getter_templ, setter_templ, attributes);
}

void StreamBase::AddMethod(Isolate* isolate,
                           Local<Signature> signature,
                           enum PropertyAttribute attributes,
                           Local<FunctionTemplate> t,
                           JSMethodFunction* stream_method,
                           Local<String> string) {
  Local<FunctionTemplate> templ =
      NewFunctionTemplate(isolate,
                          stream_method,
                          signature,
                          ConstructorBehavior::kThrow,
                          SideEffectType::kHasNoSideEffect);
  t->PrototypeTemplate()->SetAccessorProperty(
      string, templ, Local<FunctionTemplate>(), attributes);
}

void StreamBase::AddMethods(Environment* env, Local<FunctionTemplate> t) {
  AddMethods(env->isolate_data(), t);
}

void StreamBase::AddMethods(IsolateData* isolate_data,
                            Local<FunctionTemplate> t) {
  Isolate* isolate = isolate_data->isolate();
  HandleScope scope(isolate);

  enum PropertyAttribute attributes =
      static_cast<PropertyAttribute>(ReadOnly | DontDelete | DontEnum);
  Local<Signature> sig = Signature::New(isolate, t);

  AddMethod(isolate, sig, attributes, t, GetFD, isolate_data->fd_string());
  AddMethod(isolate,
            sig,
            attributes,
            t,
            GetExternal,
            isolate_data->external_stream_string());
  AddMethod(isolate,
            sig,
            attributes,
            t,
            GetBytesRead,
            isolate_data->bytes_read_string());
  AddMethod(isolate,
            sig,
            attributes,
            t,
            GetBytesWritten,
            isolate_data->bytes_written_string());
  SetProtoMethod(isolate, t, "readStart", JSMethod<&StreamBase::ReadStartJS>);
  SetProtoMethod(isolate, t, "readStop", JSMethod<&StreamBase::ReadStopJS>);
  SetProtoMethod(isolate, t, "shutdown", JSMethod<&StreamBase::Shutdown>);
  SetProtoMethod(
      isolate, t, "useUserBuffer", JSMethod<&StreamBase::UseUserBuffer>);
  SetProtoMethod(isolate, t, "writev", JSMethod<&StreamBase::Writev>);
  SetProtoMethod(isolate, t, "writeBuffer", JSMethod<&StreamBase::WriteBuffer>);
  SetProtoMethod(isolate,
                 t,
                 "writeAsciiString",
                 JSMethod<&StreamBase::WriteString<ASCII>>);
  SetProtoMethod(
      isolate, t, "writeUtf8String", JSMethod<&StreamBase::WriteString<UTF8>>);
  SetProtoMethod(
      isolate, t, "writeUcs2String", JSMethod<&StreamBase::WriteString<UCS2>>);
  SetProtoMethod(isolate,
                 t,
                 "writeLatin1String",
                 JSMethod<&StreamBase::WriteString<LATIN1>>);
  t->PrototypeTemplate()->Set(FIXED_ONE_BYTE_STRING(isolate, "isStreamBase"),
                              True(isolate));
  AddAccessor(isolate,
              sig,
              static_cast<PropertyAttribute>(DontDelete | DontEnum),
              t,
              BaseObject::InternalFieldGet<StreamBase::kOnReadFunctionField>,
              BaseObject::InternalFieldSet<StreamBase::kOnReadFunctionField,
                                           &Value::IsFunction>,
              FIXED_ONE_BYTE_STRING(isolate, "onread"));
}

void StreamBase::RegisterExternalReferences(
    ExternalReferenceRegistry* registry) {
  // This function is called by a single thread during start up, so it is safe
  // to use a local static variable here.
  static bool is_registered = false;
  if (is_registered) return;
  registry->Register(GetFD);
  registry->Register(GetExternal);
  registry->Register(GetBytesRead);
  registry->Register(GetBytesWritten);
  registry->Register(JSMethod<&StreamBase::ReadStartJS>);
  registry->Register(JSMethod<&StreamBase::ReadStopJS>);
  registry->Register(JSMethod<&StreamBase::Shutdown>);
  registry->Register(JSMethod<&StreamBase::UseUserBuffer>);
  registry->Register(JSMethod<&StreamBase::Writev>);
  registry->Register(JSMethod<&StreamBase::WriteBuffer>);
  registry->Register(JSMethod<&StreamBase::WriteString<ASCII>>);
  registry->Register(JSMethod<&StreamBase::WriteString<UTF8>>);
  registry->Register(JSMethod<&StreamBase::WriteString<UCS2>>);
  registry->Register(JSMethod<&StreamBase::WriteString<LATIN1>>);
  registry->Register(
      BaseObject::InternalFieldGet<StreamBase::kOnReadFunctionField>);
  registry->Register(
      BaseObject::InternalFieldSet<StreamBase::kOnReadFunctionField,
                                   &Value::IsFunction>);
  is_registered = true;
}

void StreamBase::GetFD(const FunctionCallbackInfo<Value>& args) {
  // Mimic implementation of StreamBase::GetFD() and UDPWrap::GetFD().
  StreamBase* wrap = StreamBase::FromObject(args.This().As<Object>());
  if (wrap == nullptr) return args.GetReturnValue().Set(UV_EINVAL);

  if (!wrap->IsAlive()) return args.GetReturnValue().Set(UV_EINVAL);

  args.GetReturnValue().Set(wrap->GetFD());
}

void StreamBase::GetBytesRead(const FunctionCallbackInfo<Value>& args) {
  StreamBase* wrap = StreamBase::FromObject(args.This().As<Object>());
  if (wrap == nullptr) return args.GetReturnValue().Set(0);

  // uint64_t -> double. 53bits is enough for all real cases.
  args.GetReturnValue().Set(static_cast<double>(wrap->bytes_read_));
}

void StreamBase::GetBytesWritten(const FunctionCallbackInfo<Value>& args) {
  StreamBase* wrap = StreamBase::FromObject(args.This().As<Object>());
  if (wrap == nullptr) return args.GetReturnValue().Set(0);

  // uint64_t -> double. 53bits is enough for all real cases.
  args.GetReturnValue().Set(static_cast<double>(wrap->bytes_written_));
}

void StreamBase::GetExternal(const FunctionCallbackInfo<Value>& args) {
  StreamBase* wrap = StreamBase::FromObject(args.This().As<Object>());
  if (wrap == nullptr) return;

  Local<External> ext = External::New(args.GetIsolate(), wrap);
  args.GetReturnValue().Set(ext);
}

template <int (StreamBase::*Method)(const FunctionCallbackInfo<Value>& args)>
void StreamBase::JSMethod(const FunctionCallbackInfo<Value>& args) {
  StreamBase* wrap = StreamBase::FromObject(args.This().As<Object>());
  if (wrap == nullptr) return;

  if (!wrap->IsAlive()) return args.GetReturnValue().Set(UV_EINVAL);

  AsyncHooks::DefaultTriggerAsyncIdScope trigger_scope(wrap->GetAsyncWrap());
  args.GetReturnValue().Set((wrap->*Method)(args));
}

int StreamResource::DoTryWrite(uv_buf_t** bufs, size_t* count) {
  // No TryWrite by default
  return 0;
}


const char* StreamResource::Error() const {
  return nullptr;
}


void StreamResource::ClearError() {
  // No-op
}


uv_buf_t EmitToJSStreamListener::OnStreamAlloc(size_t suggested_size) {
  CHECK_NOT_NULL(stream_);
  Environment* env = static_cast<StreamBase*>(stream_)->stream_env();
  return env->allocate_managed_buffer(suggested_size);
}

void EmitToJSStreamListener::OnStreamRead(ssize_t nread, const uv_buf_t& buf_) {
  CHECK_NOT_NULL(stream_);
  StreamBase* stream = static_cast<StreamBase*>(stream_);
  Environment* env = stream->stream_env();
  Isolate* isolate = env->isolate();
  HandleScope handle_scope(isolate);
  Context::Scope context_scope(env->context());
  std::unique_ptr<BackingStore> bs = env->release_managed_buffer(buf_);

  if (nread <= 0)  {
    if (nread < 0)
      stream->CallJSOnreadMethod(nread, Local<ArrayBuffer>());
    return;
  }

  CHECK_LE(static_cast<size_t>(nread), bs->ByteLength());
  if (static_cast<size_t>(nread) != bs->ByteLength()) {
    std::unique_ptr<BackingStore> old_bs = std::move(bs);
    bs = ArrayBuffer::NewBackingStore(isolate, nread);
    memcpy(static_cast<char*>(bs->Data()),
           static_cast<char*>(old_bs->Data()),
           nread);
  }

  stream->CallJSOnreadMethod(nread, ArrayBuffer::New(isolate, std::move(bs)));
}


uv_buf_t CustomBufferJSListener::OnStreamAlloc(size_t suggested_size) {
  return buffer_;
}


void CustomBufferJSListener::OnStreamRead(ssize_t nread, const uv_buf_t& buf) {
  CHECK_NOT_NULL(stream_);

  StreamBase* stream = static_cast<StreamBase*>(stream_);
  Environment* env = stream->stream_env();
  HandleScope handle_scope(env->isolate());
  Context::Scope context_scope(env->context());

  // In the case that there's an error and buf is null, return immediately.
  // This can happen on unices when POLLHUP is received and UV_EOF is returned
  // or when getting an error while performing a UV_HANDLE_ZERO_READ on Windows.
  if (buf.base == nullptr && nread < 0) {
    stream->CallJSOnreadMethod(nread, Local<ArrayBuffer>());
    return;
  }

  CHECK_EQ(buf.base, buffer_.base);

  MaybeLocal<Value> ret = stream->CallJSOnreadMethod(nread,
                             Local<ArrayBuffer>(),
                             0,
                             StreamBase::SKIP_NREAD_CHECKS);
  Local<Value> next_buf_v;
  if (ret.ToLocal(&next_buf_v) && !next_buf_v->IsUndefined()) {
    buffer_.base = Buffer::Data(next_buf_v);
    buffer_.len = Buffer::Length(next_buf_v);
  }
}


void ReportWritesToJSStreamListener::OnStreamAfterReqFinished(
    StreamReq* req_wrap, int status) {
  StreamBase* stream = static_cast<StreamBase*>(stream_);
  Environment* env = stream->stream_env();
  if (!env->can_call_into_js()) return;
  AsyncWrap* async_wrap = req_wrap->GetAsyncWrap();
  HandleScope handle_scope(env->isolate());
  Context::Scope context_scope(env->context());
  CHECK(!async_wrap->persistent().IsEmpty());
  Local<Object> req_wrap_obj = async_wrap->object();

  Local<Value> argv[] = {
    Integer::New(env->isolate(), status),
    stream->GetObject(),
    Undefined(env->isolate())
  };

  const char* msg = stream->Error();
  if (msg != nullptr) {
    argv[2] = OneByteString(env->isolate(), msg);
    stream->ClearError();
  }

  if (req_wrap_obj->Has(env->context(), env->oncomplete_string()).FromJust())
    async_wrap->MakeCallback(env->oncomplete_string(), arraysize(argv), argv);
}

void ReportWritesToJSStreamListener::OnStreamAfterWrite(
    WriteWrap* req_wrap, int status) {
  OnStreamAfterReqFinished(req_wrap, status);
}

void ReportWritesToJSStreamListener::OnStreamAfterShutdown(
    ShutdownWrap* req_wrap, int status) {
  OnStreamAfterReqFinished(req_wrap, status);
}

void ShutdownWrap::OnDone(int status) {
  stream()->EmitAfterShutdown(this, status);
  Dispose();
}

void WriteWrap::OnDone(int status) {
  stream()->EmitAfterWrite(this, status);
  Dispose();
}

StreamListener::~StreamListener() {
  if (stream_ != nullptr)
    stream_->RemoveStreamListener(this);
}

void StreamListener::OnStreamAfterShutdown(ShutdownWrap* w, int status) {
  CHECK_NOT_NULL(previous_listener_);
  previous_listener_->OnStreamAfterShutdown(w, status);
}

void StreamListener::OnStreamAfterWrite(WriteWrap* w, int status) {
  CHECK_NOT_NULL(previous_listener_);
  previous_listener_->OnStreamAfterWrite(w, status);
}

StreamResource::~StreamResource() {
  while (listener_ != nullptr) {
    StreamListener* listener = listener_;
    listener->OnStreamDestroy();
    // Remove the listener if it didn’t remove itself. This makes the logic
    // in `OnStreamDestroy()` implementations easier, because they
    // may call generic cleanup functions which can just remove the
    // listener unconditionally.
    if (listener == listener_)
      RemoveStreamListener(listener_);
  }
}

void StreamResource::RemoveStreamListener(StreamListener* listener) {
  CHECK_NOT_NULL(listener);

  StreamListener* previous;
  StreamListener* current;

  // Remove from the linked list.
  // No loop condition because we want a crash if listener is not found.
  for (current = listener_, previous = nullptr;;
       previous = current, current = current->previous_listener_) {
    CHECK_NOT_NULL(current);
    if (current == listener) {
      if (previous != nullptr)
        previous->previous_listener_ = current->previous_listener_;
      else
        listener_ = listener->previous_listener_;
      break;
    }
  }

  listener->stream_ = nullptr;
  listener->previous_listener_ = nullptr;
}

ShutdownWrap* StreamBase::CreateShutdownWrap(
    Local<Object> object) {
  auto* wrap = new SimpleShutdownWrap<AsyncWrap>(this, object);
  wrap->MakeWeak();
  return wrap;
}

WriteWrap* StreamBase::CreateWriteWrap(
    Local<Object> object) {
  auto* wrap = new SimpleWriteWrap<AsyncWrap>(this, object);
  wrap->MakeWeak();
  return wrap;
}

void StreamReq::Done(int status, const char* error_str) {
  AsyncWrap* async_wrap = GetAsyncWrap();
  Environment* env = async_wrap->env();
  if (error_str != nullptr) {
    v8::HandleScope handle_scope(env->isolate());
    if (async_wrap->object()
            ->Set(env->context(),
                  env->error_string(),
                  OneByteString(env->isolate(), error_str))
            .IsNothing()) {
      return;
    }
  }

  OnDone(status);
}

}  // namespace node
