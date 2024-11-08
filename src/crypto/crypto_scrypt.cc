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
using v8::JustVoid;
using v8::Maybe;
using v8::MaybeLocal;
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

MaybeLocal<Value> ScryptTraits::EncodeOutput(Environment* env,
                                             const ScryptConfig& params,
                                             ByteSource* out) {
  return out->ToArrayBuffer(env);
}

Maybe<void> ScryptTraits::AdditionalConfig(
    CryptoJobMode mode,
    const FunctionCallbackInfo<Value>& args,
    unsigned int offset,
    ScryptConfig* params) {
  Environment* env = Environment::GetCurrent(args);

  params->mode = mode;

  ArrayBufferOrViewContents<char> pass(args[offset]);
  ArrayBufferOrViewContents<char> salt(args[offset + 1]);

  if (!pass.CheckSizeInt32()) [[unlikely]] {
    THROW_ERR_OUT_OF_RANGE(env, "pass is too large");
    return Nothing<void>();
  }

  if (!salt.CheckSizeInt32()) [[unlikely]] {
    THROW_ERR_OUT_OF_RANGE(env, "salt is too large");
    return Nothing<void>();
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

  params->length = args[offset + 6].As<Int32>()->Value();
  CHECK_GE(params->length, 0);

  if (!ncrypto::checkScryptParams(
          params->N, params->r, params->p, params->maxmem)) {
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
    return Nothing<void>();
  }

  return JustVoid();
}

bool ScryptTraits::DeriveBits(
    Environment* env,
    const ScryptConfig& params,
    ByteSource* out) {
  // If the params.length is zero-length, just return an empty buffer.
  // It's useless, yes, but allowed via the API.
  if (params.length == 0) {
    *out = ByteSource();
    return true;
  }

  auto dp = ncrypto::scrypt(
      ncrypto::Buffer<const char>{
          .data = params.pass.data<char>(),
          .len = params.pass.size(),
      },
      ncrypto::Buffer<const unsigned char>{
          .data = params.salt.data<unsigned char>(),
          .len = params.salt.size(),
      },
      params.N,
      params.r,
      params.p,
      params.maxmem,
      params.length);

  if (!dp) return false;
  *out = ByteSource::Allocated(dp.release());
  return true;
}

#endif  // !OPENSSL_NO_SCRYPT

}  // namespace crypto
}  // namespace node
