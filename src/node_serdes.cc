#include "base_object-inl.h"
#include "node_buffer.h"
#include "node_errors.h"
#include "node_external_reference.h"
#include "node_internals.h"
#include "util-inl.h"

namespace node {

using v8::Array;
using v8::ArrayBuffer;
using v8::Context;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Integer;
using v8::Isolate;
using v8::Just;
using v8::Local;
using v8::Maybe;
using v8::MaybeLocal;
using v8::Nothing;
using v8::Object;
using v8::SharedArrayBuffer;
using v8::String;
using v8::Value;
using v8::ValueDeserializer;
using v8::ValueSerializer;

namespace serdes {

class SerializerContext : public BaseObject,
                          public ValueSerializer::Delegate {
 public:
  SerializerContext(Environment* env,
                    Local<Object> wrap);

  ~SerializerContext() override = default;

  void ThrowDataCloneError(Local<String> message) override;
  Maybe<bool> WriteHostObject(Isolate* isolate, Local<Object> object) override;
  Maybe<uint32_t> GetSharedArrayBufferId(
      Isolate* isolate, Local<SharedArrayBuffer> shared_array_buffer) override;

  static void SetTreatArrayBufferViewsAsHostObjects(
      const FunctionCallbackInfo<Value>& args);

  static void New(const FunctionCallbackInfo<Value>& args);
  static void WriteHeader(const FunctionCallbackInfo<Value>& args);
  static void WriteValue(const FunctionCallbackInfo<Value>& args);
  static void ReleaseBuffer(const FunctionCallbackInfo<Value>& args);
  static void TransferArrayBuffer(const FunctionCallbackInfo<Value>& args);
  static void WriteUint32(const FunctionCallbackInfo<Value>& args);
  static void WriteUint64(const FunctionCallbackInfo<Value>& args);
  static void WriteDouble(const FunctionCallbackInfo<Value>& args);
  static void WriteRawBytes(const FunctionCallbackInfo<Value>& args);

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(SerializerContext)
  SET_SELF_SIZE(SerializerContext)

 private:
  ValueSerializer serializer_;
};

class DeserializerContext : public BaseObject,
                            public ValueDeserializer::Delegate {
 public:
  DeserializerContext(Environment* env,
                      Local<Object> wrap,
                      Local<Value> buffer);

  ~DeserializerContext() override = default;

  MaybeLocal<Object> ReadHostObject(Isolate* isolate) override;

  static void New(const FunctionCallbackInfo<Value>& args);
  static void ReadHeader(const FunctionCallbackInfo<Value>& args);
  static void ReadValue(const FunctionCallbackInfo<Value>& args);
  static void TransferArrayBuffer(const FunctionCallbackInfo<Value>& args);
  static void GetWireFormatVersion(const FunctionCallbackInfo<Value>& args);
  static void ReadUint32(const FunctionCallbackInfo<Value>& args);
  static void ReadUint64(const FunctionCallbackInfo<Value>& args);
  static void ReadDouble(const FunctionCallbackInfo<Value>& args);
  static void ReadRawBytes(const FunctionCallbackInfo<Value>& args);

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(DeserializerContext)
  SET_SELF_SIZE(DeserializerContext)

 private:
  const uint8_t* data_;
  const size_t length_;

  ValueDeserializer deserializer_;
};

SerializerContext::SerializerContext(Environment* env, Local<Object> wrap)
  : BaseObject(env, wrap),
    serializer_(env->isolate(), this) {
  MakeWeak();
}

void SerializerContext::ThrowDataCloneError(Local<String> message) {
  Local<Value> args[1] = { message };
  Local<Value> get_data_clone_error;
  if (!object()
           ->Get(env()->context(), env()->get_data_clone_error_string())
           .ToLocal(&get_data_clone_error)) {
    // A superseding error will have been thrown by v8.
    return;
  }

  CHECK(get_data_clone_error->IsFunction());
  Local<Value> error;
  if (get_data_clone_error.As<Function>()
          ->Call(env()->context(), object(), arraysize(args), args)
          .ToLocal(&error)) {
    env()->isolate()->ThrowException(error);
  }
}

Maybe<uint32_t> SerializerContext::GetSharedArrayBufferId(
    Isolate* isolate, Local<SharedArrayBuffer> shared_array_buffer) {
  Local<Value> args[1] = { shared_array_buffer };
  Local<Value> get_shared_array_buffer_id;
  if (!object()
           ->Get(env()->context(), env()->get_shared_array_buffer_id_string())
           .ToLocal(&get_shared_array_buffer_id)) {
    return Nothing<uint32_t>();
  }

  if (!get_shared_array_buffer_id->IsFunction()) {
    return ValueSerializer::Delegate::GetSharedArrayBufferId(
        isolate, shared_array_buffer);
  }

  Local<Value> id;
  if (!get_shared_array_buffer_id.As<Function>()
           ->Call(env()->context(), object(), arraysize(args), args)
           .ToLocal(&id)) {
    return Nothing<uint32_t>();
  }

  return id->Uint32Value(env()->context());
}

Maybe<bool> SerializerContext::WriteHostObject(Isolate* isolate,
                                               Local<Object> input) {
  Local<Value> args[1] = { input };

  Local<Value> write_host_object;
  if (!object()
           ->Get(env()->context(), env()->write_host_object_string())
           .ToLocal(&write_host_object)) {
    return Nothing<bool>();
  }

  if (!write_host_object->IsFunction()) {
    return ValueSerializer::Delegate::WriteHostObject(isolate, input);
  }

  Local<Value> ret;
  if (!write_host_object.As<Function>()
           ->Call(env()->context(), object(), arraysize(args), args)
           .ToLocal(&ret)) {
    return Nothing<bool>();
  }

  return Just(true);
}

void SerializerContext::New(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  if (!args.IsConstructCall()) {
    return THROW_ERR_CONSTRUCT_CALL_REQUIRED(
        env, "Class constructor Serializer cannot be invoked without 'new'");
  }

  new SerializerContext(env, args.This());
}

void SerializerContext::WriteHeader(const FunctionCallbackInfo<Value>& args) {
  SerializerContext* ctx;
  ASSIGN_OR_RETURN_UNWRAP(&ctx, args.This());
  ctx->serializer_.WriteHeader();
}

void SerializerContext::WriteValue(const FunctionCallbackInfo<Value>& args) {
  SerializerContext* ctx;
  ASSIGN_OR_RETURN_UNWRAP(&ctx, args.This());
  bool ret;
  if (ctx->serializer_.WriteValue(ctx->env()->context(), args[0]).To(&ret)) {
    args.GetReturnValue().Set(ret);
  }
}

void SerializerContext::SetTreatArrayBufferViewsAsHostObjects(
    const FunctionCallbackInfo<Value>& args) {
  SerializerContext* ctx;
  ASSIGN_OR_RETURN_UNWRAP(&ctx, args.This());

  bool value = args[0]->BooleanValue(ctx->env()->isolate());
  ctx->serializer_.SetTreatArrayBufferViewsAsHostObjects(value);
}

void SerializerContext::ReleaseBuffer(const FunctionCallbackInfo<Value>& args) {
  SerializerContext* ctx;
  ASSIGN_OR_RETURN_UNWRAP(&ctx, args.This());

  // Note: Both ValueSerializer and this Buffer::New() variant use malloc()
  // as the underlying allocator.
  std::pair<uint8_t*, size_t> ret = ctx->serializer_.Release();
  Local<Object> buf;
  if (Buffer::New(ctx->env(), reinterpret_cast<char*>(ret.first), ret.second)
          .ToLocal(&buf)) {
    args.GetReturnValue().Set(buf);
  }
}

void SerializerContext::TransferArrayBuffer(
    const FunctionCallbackInfo<Value>& args) {
  SerializerContext* ctx;
  ASSIGN_OR_RETURN_UNWRAP(&ctx, args.This());

  uint32_t id;
  if (!args[0]->Uint32Value(ctx->env()->context()).To(&id)) {
    return;
  }

  if (!args[1]->IsArrayBuffer()) {
    return node::THROW_ERR_INVALID_ARG_TYPE(
        ctx->env(), "arrayBuffer must be an ArrayBuffer");
  }

  Local<ArrayBuffer> ab = args[1].As<ArrayBuffer>();
  ctx->serializer_.TransferArrayBuffer(id, ab);
}

void SerializerContext::WriteUint32(const FunctionCallbackInfo<Value>& args) {
  SerializerContext* ctx;
  ASSIGN_OR_RETURN_UNWRAP(&ctx, args.This());

  uint32_t value;
  if (args[0]->Uint32Value(ctx->env()->context()).To(&value)) {
    ctx->serializer_.WriteUint32(value);
  }
}

void SerializerContext::WriteUint64(const FunctionCallbackInfo<Value>& args) {
  SerializerContext* ctx;
  ASSIGN_OR_RETURN_UNWRAP(&ctx, args.This());

  uint32_t hi;
  uint32_t lo;

  if (!args[0]->Uint32Value(ctx->env()->context()).To(&hi) ||
      !args[1]->Uint32Value(ctx->env()->context()).To(&lo)) {
    return;
  }

  uint64_t hiu64 = hi;
  uint64_t lou64 = lo;
  ctx->serializer_.WriteUint64((hiu64 << 32) | lou64);
}

void SerializerContext::WriteDouble(const FunctionCallbackInfo<Value>& args) {
  SerializerContext* ctx;
  ASSIGN_OR_RETURN_UNWRAP(&ctx, args.This());

  double value;
  if (args[0]->NumberValue(ctx->env()->context()).To(&value)) {
    ctx->serializer_.WriteDouble(value);
  }
}

void SerializerContext::WriteRawBytes(const FunctionCallbackInfo<Value>& args) {
  SerializerContext* ctx;
  ASSIGN_OR_RETURN_UNWRAP(&ctx, args.This());

  if (!args[0]->IsArrayBufferView()) {
    return node::THROW_ERR_INVALID_ARG_TYPE(
        ctx->env(), "source must be a TypedArray or a DataView");
  }

  ArrayBufferViewContents<char> bytes(args[0]);
  ctx->serializer_.WriteRawBytes(bytes.data(), bytes.length());
}

DeserializerContext::DeserializerContext(Environment* env,
                                         Local<Object> wrap,
                                         Local<Value> buffer)
  : BaseObject(env, wrap),
    data_(reinterpret_cast<const uint8_t*>(Buffer::Data(buffer))),
    length_(Buffer::Length(buffer)),
    deserializer_(env->isolate(), data_, length_, this) {
  object()->Set(env->context(), env->buffer_string(), buffer).Check();

  MakeWeak();
}

MaybeLocal<Object> DeserializerContext::ReadHostObject(Isolate* isolate) {
  Local<Value> read_host_object;
  if (!object()
           ->Get(env()->context(), env()->read_host_object_string())
           .ToLocal(&read_host_object)) {
    return {};
  }

  if (!read_host_object->IsFunction()) {
    return ValueDeserializer::Delegate::ReadHostObject(isolate);
  }

  Isolate::AllowJavascriptExecutionScope allow_js(isolate);
  Local<Value> ret;
  if (!read_host_object.As<Function>()
           ->Call(env()->context(), object(), 0, nullptr)
           .ToLocal(&ret)) {
    return {};
  }

  if (!ret->IsObject()) {
    env()->ThrowTypeError("readHostObject must return an object");
    return {};
  }

  return ret.As<Object>();
}

void DeserializerContext::New(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  if (!args.IsConstructCall()) {
    return THROW_ERR_CONSTRUCT_CALL_REQUIRED(
        env, "Class constructor Deserializer cannot be invoked without 'new'");
  }

  if (!args[0]->IsArrayBufferView()) {
    return node::THROW_ERR_INVALID_ARG_TYPE(
        env, "buffer must be a TypedArray or a DataView");
  }

  new DeserializerContext(env, args.This(), args[0]);
}

void DeserializerContext::ReadHeader(const FunctionCallbackInfo<Value>& args) {
  DeserializerContext* ctx;
  ASSIGN_OR_RETURN_UNWRAP(&ctx, args.This());

  bool ret;
  if (ctx->deserializer_.ReadHeader(ctx->env()->context()).To(&ret)) {
    args.GetReturnValue().Set(ret);
  }
}

void DeserializerContext::ReadValue(const FunctionCallbackInfo<Value>& args) {
  DeserializerContext* ctx;
  ASSIGN_OR_RETURN_UNWRAP(&ctx, args.This());

  Local<Value> ret;
  if (ctx->deserializer_.ReadValue(ctx->env()->context()).ToLocal(&ret)) {
    args.GetReturnValue().Set(ret);
  }
}

void DeserializerContext::TransferArrayBuffer(
    const FunctionCallbackInfo<Value>& args) {
  DeserializerContext* ctx;
  ASSIGN_OR_RETURN_UNWRAP(&ctx, args.This());

  uint32_t id;
  if (!args[0]->Uint32Value(ctx->env()->context()).To(&id)) {
    return;
  }

  if (args[1]->IsArrayBuffer()) {
    Local<ArrayBuffer> ab = args[1].As<ArrayBuffer>();
    ctx->deserializer_.TransferArrayBuffer(id, ab);
    return;
  }

  if (args[1]->IsSharedArrayBuffer()) {
    Local<SharedArrayBuffer> sab = args[1].As<SharedArrayBuffer>();
    ctx->deserializer_.TransferSharedArrayBuffer(id, sab);
    return;
  }

  return node::THROW_ERR_INVALID_ARG_TYPE(
      ctx->env(), "arrayBuffer must be an ArrayBuffer or SharedArrayBuffer");
}

void DeserializerContext::GetWireFormatVersion(
    const FunctionCallbackInfo<Value>& args) {
  DeserializerContext* ctx;
  ASSIGN_OR_RETURN_UNWRAP(&ctx, args.This());

  args.GetReturnValue().Set(ctx->deserializer_.GetWireFormatVersion());
}

void DeserializerContext::ReadUint32(const FunctionCallbackInfo<Value>& args) {
  DeserializerContext* ctx;
  ASSIGN_OR_RETURN_UNWRAP(&ctx, args.This());

  uint32_t value;
  bool ok = ctx->deserializer_.ReadUint32(&value);
  if (!ok) return ctx->env()->ThrowError("ReadUint32() failed");
  return args.GetReturnValue().Set(value);
}

void DeserializerContext::ReadUint64(const FunctionCallbackInfo<Value>& args) {
  DeserializerContext* ctx;
  ASSIGN_OR_RETURN_UNWRAP(&ctx, args.This());

  uint64_t value;
  bool ok = ctx->deserializer_.ReadUint64(&value);
  if (!ok) return ctx->env()->ThrowError("ReadUint64() failed");

  uint32_t hi = static_cast<uint32_t>(value >> 32);
  uint32_t lo = static_cast<uint32_t>(value);

  Isolate* isolate = ctx->env()->isolate();

  Local<Value> ret[] = {
    Integer::NewFromUnsigned(isolate, hi),
    Integer::NewFromUnsigned(isolate, lo)
  };
  return args.GetReturnValue().Set(Array::New(isolate, ret, arraysize(ret)));
}

void DeserializerContext::ReadDouble(const FunctionCallbackInfo<Value>& args) {
  DeserializerContext* ctx;
  ASSIGN_OR_RETURN_UNWRAP(&ctx, args.This());

  double value;
  bool ok = ctx->deserializer_.ReadDouble(&value);
  if (!ok) return ctx->env()->ThrowError("ReadDouble() failed");
  return args.GetReturnValue().Set(value);
}

void DeserializerContext::ReadRawBytes(
    const FunctionCallbackInfo<Value>& args) {
  DeserializerContext* ctx;
  ASSIGN_OR_RETURN_UNWRAP(&ctx, args.This());

  int64_t length_arg;
  if (!args[0]->IntegerValue(ctx->env()->context()).To(&length_arg)) {
    return;
  }
  size_t length = length_arg;

  const void* data;
  bool ok = ctx->deserializer_.ReadRawBytes(length, &data);
  if (!ok) return ctx->env()->ThrowError("ReadRawBytes() failed");

  const uint8_t* position = reinterpret_cast<const uint8_t*>(data);
  CHECK_GE(position, ctx->data_);
  CHECK_LE(position + length, ctx->data_ + ctx->length_);

  const uint32_t offset = static_cast<uint32_t>(position - ctx->data_);
  CHECK_EQ(ctx->data_ + offset, position);

  args.GetReturnValue().Set(offset);
}

void Initialize(Local<Object> target,
                Local<Value> unused,
                Local<Context> context,
                void* priv) {
  Environment* env = Environment::GetCurrent(context);
  Isolate* isolate = env->isolate();

  Local<FunctionTemplate> ser =
      NewFunctionTemplate(isolate, SerializerContext::New);

  ser->InstanceTemplate()->SetInternalFieldCount(
      SerializerContext::kInternalFieldCount);

  SetProtoMethod(isolate, ser, "writeHeader", SerializerContext::WriteHeader);
  SetProtoMethod(isolate, ser, "writeValue", SerializerContext::WriteValue);
  SetProtoMethod(
      isolate, ser, "releaseBuffer", SerializerContext::ReleaseBuffer);
  SetProtoMethod(isolate,
                 ser,
                 "transferArrayBuffer",
                 SerializerContext::TransferArrayBuffer);
  SetProtoMethod(isolate, ser, "writeUint32", SerializerContext::WriteUint32);
  SetProtoMethod(isolate, ser, "writeUint64", SerializerContext::WriteUint64);
  SetProtoMethod(isolate, ser, "writeDouble", SerializerContext::WriteDouble);
  SetProtoMethod(
      isolate, ser, "writeRawBytes", SerializerContext::WriteRawBytes);
  SetProtoMethod(isolate,
                 ser,
                 "_setTreatArrayBufferViewsAsHostObjects",
                 SerializerContext::SetTreatArrayBufferViewsAsHostObjects);

  ser->ReadOnlyPrototype();
  SetConstructorFunction(context, target, "Serializer", ser);

  Local<FunctionTemplate> des =
      NewFunctionTemplate(isolate, DeserializerContext::New);

  des->InstanceTemplate()->SetInternalFieldCount(
      DeserializerContext::kInternalFieldCount);

  SetProtoMethod(isolate, des, "readHeader", DeserializerContext::ReadHeader);
  SetProtoMethod(isolate, des, "readValue", DeserializerContext::ReadValue);
  SetProtoMethod(isolate,
                 des,
                 "getWireFormatVersion",
                 DeserializerContext::GetWireFormatVersion);
  SetProtoMethod(isolate,
                 des,
                 "transferArrayBuffer",
                 DeserializerContext::TransferArrayBuffer);
  SetProtoMethod(isolate, des, "readUint32", DeserializerContext::ReadUint32);
  SetProtoMethod(isolate, des, "readUint64", DeserializerContext::ReadUint64);
  SetProtoMethod(isolate, des, "readDouble", DeserializerContext::ReadDouble);
  SetProtoMethod(
      isolate, des, "_readRawBytes", DeserializerContext::ReadRawBytes);

  des->SetLength(1);
  des->ReadOnlyPrototype();
  SetConstructorFunction(context, target, "Deserializer", des);
}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(SerializerContext::New);

  registry->Register(SerializerContext::WriteHeader);
  registry->Register(SerializerContext::WriteValue);
  registry->Register(SerializerContext::ReleaseBuffer);
  registry->Register(SerializerContext::TransferArrayBuffer);
  registry->Register(SerializerContext::WriteUint32);
  registry->Register(SerializerContext::WriteUint64);
  registry->Register(SerializerContext::WriteDouble);
  registry->Register(SerializerContext::WriteRawBytes);
  registry->Register(SerializerContext::SetTreatArrayBufferViewsAsHostObjects);

  registry->Register(DeserializerContext::New);
  registry->Register(DeserializerContext::ReadHeader);
  registry->Register(DeserializerContext::ReadValue);
  registry->Register(DeserializerContext::GetWireFormatVersion);
  registry->Register(DeserializerContext::TransferArrayBuffer);
  registry->Register(DeserializerContext::ReadUint32);
  registry->Register(DeserializerContext::ReadUint64);
  registry->Register(DeserializerContext::ReadDouble);
  registry->Register(DeserializerContext::ReadRawBytes);
}

}  // namespace serdes
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(serdes, node::serdes::Initialize)
NODE_BINDING_EXTERNAL_REFERENCE(serdes,
                                node::serdes::RegisterExternalReferences)
