#include "crypto/crypto_hmac.h"
#include "async_wrap-inl.h"
#include "base_object-inl.h"
#include "crypto/crypto_keys.h"
#include "crypto/crypto_sig.h"
#include "crypto/crypto_util.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "node_buffer.h"
#include "string_bytes.h"
#include "threadpoolwork-inl.h"
#include "v8.h"

namespace node {

using ncrypto::HMACCtxPointer;
using v8::Boolean;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Isolate;
using v8::JustVoid;
using v8::Local;
using v8::Maybe;
using v8::MaybeLocal;
using v8::Nothing;
using v8::Object;
using v8::Uint32;
using v8::Value;

namespace crypto {
Hmac::Hmac(Environment* env, Local<Object> wrap)
    : BaseObject(env, wrap),
      ctx_(nullptr) {
  MakeWeak();
}

void Hmac::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackFieldWithSize("context", ctx_ ? kSizeOf_HMAC_CTX : 0);
}

void Hmac::Initialize(Environment* env, Local<Object> target) {
  Isolate* isolate = env->isolate();
  Local<FunctionTemplate> t = NewFunctionTemplate(isolate, New);

  t->InstanceTemplate()->SetInternalFieldCount(Hmac::kInternalFieldCount);

  SetProtoMethod(isolate, t, "init", HmacInit);
  SetProtoMethod(isolate, t, "update", HmacUpdate);
  SetProtoMethod(isolate, t, "digest", HmacDigest);

  SetConstructorFunction(env->context(), target, "Hmac", t);

  HmacJob::Initialize(env, target);
}

void Hmac::RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(New);
  registry->Register(HmacInit);
  registry->Register(HmacUpdate);
  registry->Register(HmacDigest);
  HmacJob::RegisterExternalReferences(registry);
}

void Hmac::New(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  new Hmac(env, args.This());
}

void Hmac::HmacInit(const char* hash_type, const char* key, int key_len) {
  HandleScope scope(env()->isolate());

  const EVP_MD* md = ncrypto::getDigestByName(hash_type);
  if (md == nullptr) [[unlikely]] {
    return THROW_ERR_CRYPTO_INVALID_DIGEST(
        env(), "Invalid digest: %s", hash_type);
  }
  if (key_len == 0) {
    key = "";
  }

  ctx_ = HMACCtxPointer::New();
  ncrypto::Buffer<const void> key_buf{
      .data = key,
      .len = static_cast<size_t>(key_len),
  };
  if (!ctx_.init(key_buf, md)) [[unlikely]] {
    ctx_.reset();
    return ThrowCryptoError(env(), ERR_get_error());
  }
}

void Hmac::HmacInit(const FunctionCallbackInfo<Value>& args) {
  Hmac* hmac;
  ASSIGN_OR_RETURN_UNWRAP(&hmac, args.This());
  Environment* env = hmac->env();

  const node::Utf8Value hash_type(env->isolate(), args[0]);
  ByteSource key = ByteSource::FromSecretKeyBytes(env, args[1]);
  hmac->HmacInit(*hash_type, key.data<char>(), key.size());
}

bool Hmac::HmacUpdate(const char* data, size_t len) {
  ncrypto::Buffer<const void> buf{
      .data = data,
      .len = len,
  };
  return ctx_.update(buf);
}

void Hmac::HmacUpdate(const FunctionCallbackInfo<Value>& args) {
  Decode<Hmac>(args, [](Hmac* hmac, const FunctionCallbackInfo<Value>& args,
                        const char* data, size_t size) {
    Environment* env = Environment::GetCurrent(args);
    if (size > INT_MAX) [[unlikely]]
      return THROW_ERR_OUT_OF_RANGE(env, "data is too long");
    bool r = hmac->HmacUpdate(data, size);
    args.GetReturnValue().Set(r);
  });
}

void Hmac::HmacDigest(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  Hmac* hmac;
  ASSIGN_OR_RETURN_UNWRAP(&hmac, args.This());

  enum encoding encoding = BUFFER;
  if (args.Length() >= 1) {
    encoding = ParseEncoding(env->isolate(), args[0], BUFFER);
  }

  unsigned char md_value[EVP_MAX_MD_SIZE];
  ncrypto::Buffer<void> buf{
      .data = md_value,
      .len = sizeof(md_value),
  };

  if (hmac->ctx_) {
    if (!hmac->ctx_.digestInto(&buf)) [[unlikely]] {
      hmac->ctx_.reset();
      return ThrowCryptoError(env, ERR_get_error(), "Failed to finalize HMAC");
    }
    hmac->ctx_.reset();
  }

  Local<Value> error;
  MaybeLocal<Value> rc =
      StringBytes::Encode(env->isolate(),
                          reinterpret_cast<const char*>(md_value),
                          buf.len,
                          encoding,
                          &error);
  if (rc.IsEmpty()) [[unlikely]] {
    CHECK(!error.IsEmpty());
    env->isolate()->ThrowException(error);
    return;
  }
  args.GetReturnValue().Set(rc.FromMaybe(Local<Value>()));
}

HmacConfig::HmacConfig(HmacConfig&& other) noexcept
    : job_mode(other.job_mode),
      mode(other.mode),
      key(std::move(other.key)),
      data(std::move(other.data)),
      signature(std::move(other.signature)),
      digest(other.digest) {}

HmacConfig& HmacConfig::operator=(HmacConfig&& other) noexcept {
  if (&other == this) return *this;
  this->~HmacConfig();
  return *new (this) HmacConfig(std::move(other));
}

void HmacConfig::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("key", key);
  // If the job is sync, then the HmacConfig does not own the data
  if (job_mode == kCryptoJobAsync) {
    tracker->TrackFieldWithSize("data", data.size());
    tracker->TrackFieldWithSize("signature", signature.size());
  }
}

Maybe<void> HmacTraits::AdditionalConfig(
    CryptoJobMode mode,
    const FunctionCallbackInfo<Value>& args,
    unsigned int offset,
    HmacConfig* params) {
  Environment* env = Environment::GetCurrent(args);

  params->job_mode = mode;

  CHECK(args[offset]->IsUint32());  // SignConfiguration::Mode
  params->mode =
    static_cast<SignConfiguration::Mode>(args[offset].As<Uint32>()->Value());

  CHECK(args[offset + 1]->IsString());  // Hash
  CHECK(args[offset + 2]->IsObject());  // Key

  Utf8Value digest(env->isolate(), args[offset + 1]);
  params->digest = ncrypto::getDigestByName(digest.ToStringView());
  if (params->digest == nullptr) [[unlikely]] {
    THROW_ERR_CRYPTO_INVALID_DIGEST(env, "Invalid digest: %s", *digest);
    return Nothing<void>();
  }

  KeyObjectHandle* key;
  ASSIGN_OR_RETURN_UNWRAP(&key, args[offset + 2], Nothing<void>());
  params->key = key->Data().addRef();

  ArrayBufferOrViewContents<char> data(args[offset + 3]);
  if (!data.CheckSizeInt32()) [[unlikely]] {
    THROW_ERR_OUT_OF_RANGE(env, "data is too big");
    return Nothing<void>();
  }
  params->data = mode == kCryptoJobAsync
      ? data.ToCopy()
      : data.ToByteSource();

  if (!args[offset + 4]->IsUndefined()) {
    ArrayBufferOrViewContents<char> signature(args[offset + 4]);
    if (!signature.CheckSizeInt32()) [[unlikely]] {
      THROW_ERR_OUT_OF_RANGE(env, "signature is too big");
      return Nothing<void>();
    }
    params->signature = mode == kCryptoJobAsync
        ? signature.ToCopy()
        : signature.ToByteSource();
  }

  return JustVoid();
}

bool HmacTraits::DeriveBits(
    Environment* env,
    const HmacConfig& params,
    ByteSource* out) {
  auto ctx = HMACCtxPointer::New();

  ncrypto::Buffer<const void> key_buf{
      .data = params.key.GetSymmetricKey(),
      .len = params.key.GetSymmetricKeySize(),
  };
  if (!ctx.init(key_buf, params.digest)) [[unlikely]] {
    return false;
  }

  ncrypto::Buffer<const void> buffer{
      .data = params.data.data(),
      .len = params.data.size(),
  };
  if (!ctx.update(buffer)) [[unlikely]] {
    return false;
  }

  auto buf = ctx.digest();
  if (!buf) [[unlikely]]
    return false;

  *out = ByteSource::Allocated(buf.release());

  return true;
}

MaybeLocal<Value> HmacTraits::EncodeOutput(Environment* env,
                                           const HmacConfig& params,
                                           ByteSource* out) {
  switch (params.mode) {
    case SignConfiguration::Mode::Sign:
      return out->ToArrayBuffer(env);
    case SignConfiguration::Mode::Verify:
      return Boolean::New(
          env->isolate(),
          out->size() > 0 && out->size() == params.signature.size() &&
              memcmp(out->data(), params.signature.data(), out->size()) == 0);
  }
  UNREACHABLE();
}

}  // namespace crypto
}  // namespace node
