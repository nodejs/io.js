#include "crypto/crypto_hkdf.h"
#include "async_wrap-inl.h"
#include "base_object-inl.h"
#include "crypto/crypto_keys.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "threadpoolwork-inl.h"
#include "v8.h"

namespace node {

using v8::FunctionCallbackInfo;
using v8::JustVoid;
using v8::Maybe;
using v8::MaybeLocal;
using v8::Nothing;
using v8::Uint32;
using v8::Value;

namespace crypto {
HKDFConfig::HKDFConfig(HKDFConfig&& other) noexcept
    : mode(other.mode),
      length(other.length),
      digest(other.digest),
      key(std::move(other.key)),
      salt(std::move(other.salt)),
      info(std::move(other.info)) {}

HKDFConfig& HKDFConfig::operator=(HKDFConfig&& other) noexcept {
  if (&other == this) return *this;
  this->~HKDFConfig();
  return *new (this) HKDFConfig(std::move(other));
}

MaybeLocal<Value> HKDFTraits::EncodeOutput(Environment* env,
                                           const HKDFConfig& params,
                                           ByteSource* out) {
  return out->ToArrayBuffer(env);
}

Maybe<void> HKDFTraits::AdditionalConfig(
    CryptoJobMode mode,
    const FunctionCallbackInfo<Value>& args,
    unsigned int offset,
    HKDFConfig* params) {
  Environment* env = Environment::GetCurrent(args);

  params->mode = mode;

  CHECK(args[offset]->IsString());  // Hash
  CHECK(args[offset + 1]->IsObject());  // Key
  CHECK(IsAnyBufferSource(args[offset + 2]));  // Salt
  CHECK(IsAnyBufferSource(args[offset + 3]));  // Info
  CHECK(args[offset + 4]->IsUint32());  // Length

  Utf8Value hash(env->isolate(), args[offset]);
  params->digest = ncrypto::getDigestByName(hash.ToStringView());
  if (params->digest == nullptr) {
    THROW_ERR_CRYPTO_INVALID_DIGEST(env, "Invalid digest: %s", *hash);
    return Nothing<void>();
  }

  KeyObjectHandle* key;
  ASSIGN_OR_RETURN_UNWRAP(&key, args[offset + 1], Nothing<void>());
  params->key = key->Data().addRef();

  ArrayBufferOrViewContents<char> salt(args[offset + 2]);
  ArrayBufferOrViewContents<char> info(args[offset + 3]);

  if (!salt.CheckSizeInt32()) [[unlikely]] {
    THROW_ERR_OUT_OF_RANGE(env, "salt is too big");
    return Nothing<void>();
  }
  if (!info.CheckSizeInt32()) [[unlikely]] {
    THROW_ERR_OUT_OF_RANGE(env, "info is too big");
    return Nothing<void>();
  }

  params->salt = mode == kCryptoJobAsync
      ? salt.ToCopy()
      : salt.ToByteSource();

  params->info = mode == kCryptoJobAsync
      ? info.ToCopy()
      : info.ToByteSource();

  params->length = args[offset + 4].As<Uint32>()->Value();
  // HKDF-Expand computes up to 255 HMAC blocks, each having as many bits as the
  // output of the hash function. 255 is a hard limit because HKDF appends an
  // 8-bit counter to each HMAC'd message, starting at 1.
  if (!ncrypto::checkHkdfLength(params->digest, params->length)) {
    THROW_ERR_CRYPTO_INVALID_KEYLEN(env);
    return Nothing<void>();
  }

  return JustVoid();
}

bool HKDFTraits::DeriveBits(
    Environment* env,
    const HKDFConfig& params,
    ByteSource* out) {
  auto dp = ncrypto::hkdf(params.digest,
                          ncrypto::Buffer<const unsigned char>{
                              .data = reinterpret_cast<const unsigned char*>(
                                  params.key.GetSymmetricKey()),
                              .len = params.key.GetSymmetricKeySize(),
                          },
                          ncrypto::Buffer<const unsigned char>{
                              .data = params.info.data<const unsigned char>(),
                              .len = params.info.size(),
                          },
                          ncrypto::Buffer<const unsigned char>{
                              .data = params.salt.data<const unsigned char>(),
                              .len = params.salt.size(),
                          },
                          params.length);
  if (!dp) return false;

  *out = ByteSource::Allocated(dp.release());
  return true;
}

void HKDFConfig::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("key", key);
  // If the job is sync, then the HKDFConfig does not own the data
  if (mode == kCryptoJobAsync) {
    tracker->TrackFieldWithSize("salt", salt.size());
    tracker->TrackFieldWithSize("info", info.size());
  }
}

}  // namespace crypto
}  // namespace node
