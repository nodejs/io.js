#include "crypto/crypto_scrypt.h"
#include "async_wrap-inl.h"
#include "crypto/crypto_util.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "node_buffer.h"
#include "threadpoolwork-inl.h"
#include "v8.h"

namespace node {

using v8::FunctionCallbackInfo;
using v8::Int32;
using v8::Just;
using v8::Maybe;
using v8::Nothing;
using v8::Uint32;
using v8::Value;

namespace crypto {
#ifndef OPENSSL_NO_SCRYPT

ScryptConfig::ScryptConfig(ScryptConfig&& other) noexcept
  : mode(other.mode),
    pass(std::move(other.pass)),
    salt(std::move(other.salt)),
    N(other.N),
    r(other.r),
    p(other.p),
    maxmem(other.maxmem),
    length(other.length) {}

ScryptConfig& ScryptConfig::operator=(ScryptConfig&& other) noexcept {
  if (&other == this) return *this;
  this->~ScryptConfig();
  return *new (this) ScryptConfig(std::move(other));
}

void ScryptConfig::MemoryInfo(MemoryTracker* tracker) const {
  if (mode == kCryptoJobAsync) {
    tracker->TrackFieldWithSize("pass", pass.size());
    tracker->TrackFieldWithSize("salt", salt.size());
  }
}

Maybe<bool> ScryptTraits::EncodeOutput(
    Environment* env,
    const ScryptConfig& params,
    ByteSource* out,
    v8::Local<v8::Value>* result) {
  *result = out->ToArrayBuffer(env);
  return Just(!result->IsEmpty());
}

Maybe<bool> ScryptTraits::AdditionalConfig(
    CryptoJobMode mode,
    const FunctionCallbackInfo<Value>& args,
    unsigned int offset,
    ScryptConfig* params) {
  Environment* env = Environment::GetCurrent(args);

  params->mode = mode;

  ArrayBufferOrViewContents<char> pass(args[offset]);
  ArrayBufferOrViewContents<char> salt(args[offset + 1]);

  if (UNLIKELY(!pass.CheckSizeInt32())) {
    THROW_ERR_OUT_OF_RANGE(env, "pass is too large");
    return Nothing<bool>();
  }

  if (UNLIKELY(!salt.CheckSizeInt32())) {
    THROW_ERR_OUT_OF_RANGE(env, "salt is too large");
    return Nothing<bool>();
  }

  params->pass = mode == kCryptoJobAsync
      ? pass.ToCopy()
      : pass.ToByteSource();

  params->salt = mode == kCryptoJobAsync
      ? salt.ToCopy()
      : salt.ToByteSource();

  CHECK(args[offset + 2]->IsUint32());  // N
  CHECK(args[offset + 3]->IsUint32());  // r
  CHECK(args[offset + 4]->IsUint32());  // p
  CHECK(args[offset + 5]->IsNumber());  // maxmem
  CHECK(args[offset + 6]->IsInt32());  // length

  params->N = args[offset + 2].As<Uint32>()->Value();
  params->r = args[offset + 3].As<Uint32>()->Value();
  params->p = args[offset + 4].As<Uint32>()->Value();
  params->maxmem = args[offset + 5]->IntegerValue(env->context()).ToChecked();

  if (EVP_PBE_scrypt(
          nullptr,
          0,
          nullptr,
          0,
          params->N,
          params->r,
          params->p,
          params->maxmem,
          nullptr,
          0) != 1) {
    // Do not use CryptoErrorStore or ThrowCryptoError here in order to maintain
    // backward compatibility with ERR_CRYPTO_INVALID_SCRYPT_PARAMS.
    uint32_t err = ERR_peek_last_error();
    if (err != 0) {
      char buf[256];
      ERR_error_string_n(err, buf, sizeof(buf));
      THROW_ERR_CRYPTO_INVALID_SCRYPT_PARAMS(
          env, "Invalid scrypt params: %s", buf);
    } else {
      THROW_ERR_CRYPTO_INVALID_SCRYPT_PARAMS(env);
    }
    return Nothing<bool>();
  }

  params->length = args[offset + 6].As<Int32>()->Value();
  CHECK_GE(params->length, 0);

  return Just(true);
}

bool ScryptTraits::DeriveBits(
    Environment* env,
    const ScryptConfig& params,
    ByteSource* out) {
  ByteSource::Builder buf(params.length);

  // Both the pass and salt may be zero-length at this point

  if (!EVP_PBE_scrypt(params.pass.data<char>(),
                      params.pass.size(),
                      params.salt.data<unsigned char>(),
                      params.salt.size(),
                      params.N,
                      params.r,
                      params.p,
                      params.maxmem,
                      buf.data<unsigned char>(),
                      params.length)) {
    return false;
  }
  *out = std::move(buf).release();
  return true;
}

#endif  // !OPENSSL_NO_SCRYPT

}  // namespace crypto
}  // namespace node
