#include "crypto/crypto_keygen.h"
#include "async_wrap-inl.h"
#include "base_object-inl.h"
#include "debug_utils-inl.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "ncrypto.h"
#include "threadpoolwork-inl.h"
#include "v8.h"

#include <cmath>

namespace node {

using ncrypto::DataPointer;
using ncrypto::EVPKeyCtxPointer;
using v8::FunctionCallbackInfo;
using v8::Int32;
using v8::JustVoid;
using v8::Local;
using v8::Maybe;
using v8::MaybeLocal;
using v8::Object;
using v8::Uint32;
using v8::Value;

namespace crypto {
// NidKeyPairGenJob input arguments:
//   1. CryptoJobMode
//   2. NID
//   3. Public Format
//   4. Public Type
//   5. Private Format
//   6. Private Type
//   7. Cipher
//   8. Passphrase
Maybe<void> NidKeyPairGenTraits::AdditionalConfig(
    CryptoJobMode mode,
    const FunctionCallbackInfo<Value>& args,
    unsigned int* offset,
    NidKeyPairGenConfig* params) {
  CHECK(args[*offset]->IsInt32());
  params->params.id = args[*offset].As<Int32>()->Value();

  *offset += 1;

  return JustVoid();
}

EVPKeyCtxPointer NidKeyPairGenTraits::Setup(NidKeyPairGenConfig* params) {
  auto ctx = EVPKeyCtxPointer::NewFromID(params->params.id);
  if (!ctx || !ctx.initForKeygen()) return {};
  return ctx;
}

void SecretKeyGenConfig::MemoryInfo(MemoryTracker* tracker) const {
  if (out) tracker->TrackFieldWithSize("out", length);
}

Maybe<void> SecretKeyGenTraits::AdditionalConfig(
    CryptoJobMode mode,
    const FunctionCallbackInfo<Value>& args,
    unsigned int* offset,
    SecretKeyGenConfig* params) {
  CHECK(args[*offset]->IsUint32());
  uint32_t bits = args[*offset].As<Uint32>()->Value();
  params->length = bits / CHAR_BIT;
  *offset += 1;
  return JustVoid();
}

KeyGenJobStatus SecretKeyGenTraits::DoKeyGen(Environment* env,
                                             SecretKeyGenConfig* params) {
  auto bytes = DataPointer::Alloc(params->length);
  if (!ncrypto::CSPRNG(static_cast<unsigned char*>(bytes.get()),
                       params->length)) {
    return KeyGenJobStatus::FAILED;
  }
  params->out = ByteSource::Allocated(bytes.release());
  return KeyGenJobStatus::OK;
}

MaybeLocal<Value> SecretKeyGenTraits::EncodeKey(Environment* env,
                                                SecretKeyGenConfig* params) {
  auto data = KeyObjectData::CreateSecret(std::move(params->out));
  return KeyObjectHandle::Create(env, data).FromMaybe(Local<Value>());
}

namespace Keygen {
void Initialize(Environment* env, Local<Object> target) {
  NidKeyPairGenJob::Initialize(env, target);
  SecretKeyGenJob::Initialize(env, target);
}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  NidKeyPairGenJob::RegisterExternalReferences(registry);
  SecretKeyGenJob::RegisterExternalReferences(registry);
}
}  // namespace Keygen
}  // namespace crypto
}  // namespace node
