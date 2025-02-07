#include "crypto/crypto_random.h"
#include "async_wrap-inl.h"
#include "crypto/crypto_util.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "ncrypto.h"
#include "threadpoolwork-inl.h"
#include "v8.h"

#include <compare>

namespace node {

using ncrypto::BignumPointer;
using ncrypto::ClearErrorOnReturn;
using ncrypto::DataPointer;
using v8::ArrayBuffer;
using v8::Boolean;
using v8::FunctionCallbackInfo;
using v8::Int32;
using v8::JustVoid;
using v8::Local;
using v8::Maybe;
using v8::MaybeLocal;
using v8::Nothing;
using v8::Object;
using v8::Uint32;
using v8::Undefined;
using v8::Value;

namespace crypto {
namespace {
BignumPointer::PrimeCheckCallback getPrimeCheckCallback(Environment* env) {
  // The callback is used to check if the operation should be stopped.
  // Currently, the only check we perform is if env->is_stopping()
  // is true.
  return [env](int a, int b) -> bool { return !env->is_stopping(); };
}

}  // namespace
MaybeLocal<Value> RandomBytesTraits::EncodeOutput(
    Environment* env, const RandomBytesConfig& params, ByteSource* unused) {
  return Undefined(env->isolate());
}

Maybe<void> RandomBytesTraits::AdditionalConfig(
    CryptoJobMode mode,
    const FunctionCallbackInfo<Value>& args,
    unsigned int offset,
    RandomBytesConfig* params) {
  CHECK(IsAnyBufferSource(args[offset]));  // Buffer to fill
  CHECK(args[offset + 1]->IsUint32());  // Offset
  CHECK(args[offset + 2]->IsUint32());  // Size

  ArrayBufferOrViewContents<unsigned char> in(args[offset]);

  const uint32_t byte_offset = args[offset + 1].As<Uint32>()->Value();
  const uint32_t size = args[offset + 2].As<Uint32>()->Value();
  CHECK_GE(byte_offset + size, byte_offset);  // Overflow check.
  CHECK_LE(byte_offset + size, in.size());  // Bounds check.

  params->buffer = in.data() + byte_offset;
  params->size = size;

  return JustVoid();
}

bool RandomBytesTraits::DeriveBits(
    Environment* env,
    const RandomBytesConfig& params,
    ByteSource* unused) {
  return ncrypto::CSPRNG(params.buffer, params.size);
}

void RandomPrimeConfig::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackFieldWithSize("prime", prime ? bits * 8 : 0);
}

MaybeLocal<Value> RandomPrimeTraits::EncodeOutput(
    Environment* env, const RandomPrimeConfig& params, ByteSource* unused) {
  size_t size = params.prime.byteLength();
  auto store = ArrayBuffer::NewBackingStore(env->isolate(), size);
  CHECK_EQ(size,
           BignumPointer::EncodePaddedInto(
               params.prime.get(),
               reinterpret_cast<unsigned char*>(store->Data()),
               size));
  return ArrayBuffer::New(env->isolate(), std::move(store));
}

Maybe<void> RandomPrimeTraits::AdditionalConfig(
    CryptoJobMode mode,
    const FunctionCallbackInfo<Value>& args,
    unsigned int offset,
    RandomPrimeConfig* params) {
  ClearErrorOnReturn clear_error;
  Environment* env = Environment::GetCurrent(args);
  CHECK(args[offset]->IsUint32());  // Size
  CHECK(args[offset + 1]->IsBoolean());  // Safe

  const uint32_t size = args[offset].As<Uint32>()->Value();
  bool safe = args[offset + 1]->IsTrue();

  if (!args[offset + 2]->IsUndefined()) {
    ArrayBufferOrViewContents<unsigned char> add(args[offset + 2]);
    params->add.reset(add.data(), add.size());
    if (!params->add) [[unlikely]] {
      THROW_ERR_CRYPTO_OPERATION_FAILED(env, "could not generate prime");
      return Nothing<void>();
    }
  }

  if (!args[offset + 3]->IsUndefined()) {
    ArrayBufferOrViewContents<unsigned char> rem(args[offset + 3]);
    params->rem.reset(rem.data(), rem.size());
    if (!params->rem) [[unlikely]] {
      THROW_ERR_CRYPTO_OPERATION_FAILED(env, "could not generate prime");
      return Nothing<void>();
    }
  }

  // The JS interface already ensures that the (positive) size fits into an int.
  int bits = static_cast<int>(size);
  CHECK_GT(bits, 0);

  if (params->add) {
    if (BignumPointer::GetBitCount(params->add.get()) > bits) [[unlikely]] {
      // If we allowed this, the best case would be returning a static prime
      // that wasn't generated randomly. The worst case would be an infinite
      // loop within OpenSSL, blocking the main thread or one of the threads
      // in the thread pool.
      THROW_ERR_OUT_OF_RANGE(env, "invalid options.add");
      return Nothing<void>();
    }

    if (params->rem && params->add <= params->rem) [[unlikely]] {
      // This would definitely lead to an infinite loop if allowed since
      // OpenSSL does not check this condition.
      THROW_ERR_OUT_OF_RANGE(env, "invalid options.rem");
      return Nothing<void>();
    }
  }

  params->bits = bits;
  params->safe = safe;
  params->prime = BignumPointer::NewSecure();
  if (!params->prime) [[unlikely]] {
    THROW_ERR_CRYPTO_OPERATION_FAILED(env, "could not generate prime");
    return Nothing<void>();
  }

  return JustVoid();
}

bool RandomPrimeTraits::DeriveBits(Environment* env,
                                   const RandomPrimeConfig& params,
                                   ByteSource* unused) {
  return params.prime.generate(
      BignumPointer::PrimeConfig{
          .bits = params.bits,
          .safe = params.safe,
          .add = params.add,
          .rem = params.rem,
      },
      getPrimeCheckCallback(env));
}

void CheckPrimeConfig::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackFieldWithSize("prime", candidate ? candidate.byteLength() : 0);
}

Maybe<void> CheckPrimeTraits::AdditionalConfig(
    CryptoJobMode mode,
    const FunctionCallbackInfo<Value>& args,
    unsigned int offset,
    CheckPrimeConfig* params) {
  ArrayBufferOrViewContents<unsigned char> candidate(args[offset]);

  params->candidate = BignumPointer(candidate.data(), candidate.size());
  if (!params->candidate) {
    ThrowCryptoError(
        Environment::GetCurrent(args), ERR_get_error(), "BignumPointer");
    return Nothing<void>();
  }

  CHECK(args[offset + 1]->IsInt32());  // Checks
  params->checks = args[offset + 1].As<Int32>()->Value();
  CHECK_GE(params->checks, 0);

  return JustVoid();
}

bool CheckPrimeTraits::DeriveBits(
    Environment* env,
    const CheckPrimeConfig& params,
    ByteSource* out) {
  int ret = params.candidate.isPrime(params.checks, getPrimeCheckCallback(env));
  if (ret < 0) [[unlikely]]
    return false;
  auto buf = DataPointer::Alloc(1);
  static_cast<char*>(buf.get())[0] = ret;
  *out = ByteSource::Allocated(buf.release());
  return true;
}

MaybeLocal<Value> CheckPrimeTraits::EncodeOutput(Environment* env,
                                                 const CheckPrimeConfig& params,
                                                 ByteSource* out) {
  return Boolean::New(env->isolate(), out->data<char>()[0] != 0);
}

namespace Random {
void Initialize(Environment* env, Local<Object> target) {
  RandomBytesJob::Initialize(env, target);
  RandomPrimeJob::Initialize(env, target);
  CheckPrimeJob::Initialize(env, target);
}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  RandomBytesJob::RegisterExternalReferences(registry);
  RandomPrimeJob::RegisterExternalReferences(registry);
  CheckPrimeJob::RegisterExternalReferences(registry);
}
}  // namespace Random
}  // namespace crypto
}  // namespace node
