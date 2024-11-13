#include "crypto/crypto_pbkdf2.h"
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
using v8::Value;

namespace crypto {
PBKDF2Config::PBKDF2Config(PBKDF2Config&& other) noexcept
    : mode(other.mode),
      pass(std::move(other.pass)),
      salt(std::move(other.salt)),
      iterations(other.iterations),
      length(other.length),
      digest(other.digest) {}

PBKDF2Config& PBKDF2Config::operator=(PBKDF2Config&& other) noexcept {
  if (&other == this) return *this;
  this->~PBKDF2Config();
  return *new (this) PBKDF2Config(std::move(other));
}

void PBKDF2Config::MemoryInfo(MemoryTracker* tracker) const {
  // The job is sync, the PBKDF2Config does not own the data.
  if (mode == kCryptoJobAsync) {
    tracker->TrackFieldWithSize("pass", pass.size());
    tracker->TrackFieldWithSize("salt", salt.size());
  }
}

MaybeLocal<Value> PBKDF2Traits::EncodeOutput(Environment* env,
                                             const PBKDF2Config& params,
                                             ByteSource* out) {
  return out->ToArrayBuffer(env);
}

// The input arguments for the job are:
//   1. CryptoJobMode
//   2. The password
//   3. The salt
//   4. The number of iterations
//   5. The number of bytes to generate
//   6. The digest algorithm name
Maybe<void> PBKDF2Traits::AdditionalConfig(
    CryptoJobMode mode,
    const FunctionCallbackInfo<Value>& args,
    unsigned int offset,
    PBKDF2Config* params) {
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

  CHECK(args[offset + 2]->IsInt32());  // iteration_count
  CHECK(args[offset + 3]->IsInt32());  // length
  CHECK(args[offset + 4]->IsString());  // digest_name

  params->iterations = args[offset + 2].As<Int32>()->Value();
  if (params->iterations < 0) {
    THROW_ERR_OUT_OF_RANGE(env, "iterations must be <= %d", INT_MAX);
    return Nothing<void>();
  }

  params->length = args[offset + 3].As<Int32>()->Value();
  if (params->length < 0) {
    THROW_ERR_OUT_OF_RANGE(env, "length must be <= %d", INT_MAX);
    return Nothing<void>();
  }

  Utf8Value name(args.GetIsolate(), args[offset + 4]);
  params->digest = ncrypto::getDigestByName(name.ToStringView());
  if (params->digest == nullptr) {
    THROW_ERR_CRYPTO_INVALID_DIGEST(env, "Invalid digest: %s", *name);
    return Nothing<void>();
  }

  return JustVoid();
}

bool PBKDF2Traits::DeriveBits(Environment* env,
                              const PBKDF2Config& params,
                              ByteSource* out) {
  // Both pass and salt may be zero length here.
  auto dp = ncrypto::pbkdf2(params.digest,
                            ncrypto::Buffer<const char>{
                                .data = params.pass.data<const char>(),
                                .len = params.pass.size(),
                            },
                            ncrypto::Buffer<const unsigned char>{
                                .data = params.salt.data<unsigned char>(),
                                .len = params.salt.size(),
                            },
                            params.iterations,
                            params.length);

  if (!dp) return false;
  *out = ByteSource::Allocated(dp.release());
  return true;
}

}  // namespace crypto
}  // namespace node
