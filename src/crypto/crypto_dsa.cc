#include "crypto/crypto_dsa.h"
#include "crypto/crypto_keys.h"
#include "crypto/crypto_util.h"
#include "async_wrap-inl.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "threadpoolwork-inl.h"
#include "v8.h"

#include <openssl/bn.h>
#include <openssl/dsa.h>

#include <cstdio>

namespace node {

using ncrypto::Dsa;
using ncrypto::EVPKeyCtxPointer;
using v8::FunctionCallbackInfo;
using v8::Int32;
using v8::JustVoid;
using v8::Local;
using v8::Maybe;
using v8::Number;
using v8::Object;
using v8::Uint32;
using v8::Value;

namespace crypto {
EVPKeyCtxPointer DsaKeyGenTraits::Setup(DsaKeyPairGenConfig* params) {
#ifdef OPENSSL_IS_BORINGSSL
  // Operation is unsupported in BoringSSL
  return {};
#else
  auto param_ctx = EVPKeyCtxPointer::NewFromID(EVP_PKEY_DSA);

  if (!param_ctx.initForParamgen() ||
      !param_ctx.setDsaParameters(
          params->params.modulus_bits,
          params->params.divisor_bits != -1
              ? std::optional<int>(params->params.divisor_bits)
              : std::nullopt)) {
    return {};
  }

  auto key_params = param_ctx.paramgen();
  if (!key_params) return {};

  EVPKeyCtxPointer key_ctx = key_params.newCtx();
  if (!key_ctx.initForKeygen()) return {};
  return key_ctx;
#endif
}

// Input arguments for DsaKeyPairGenJob
//   1. CryptoJobMode
//   2. Modulus Bits
//   3. Divisor Bits
//   4. Public Format
//   5. Public Type
//   6. Private Format
//   7. Private Type
//   8. Cipher
//   9. Passphrase
Maybe<void> DsaKeyGenTraits::AdditionalConfig(
    CryptoJobMode mode,
    const FunctionCallbackInfo<Value>& args,
    unsigned int* offset,
    DsaKeyPairGenConfig* params) {
  CHECK(args[*offset]->IsUint32());  // modulus bits
  CHECK(args[*offset + 1]->IsInt32());  // divisor bits

  params->params.modulus_bits = args[*offset].As<Uint32>()->Value();
  params->params.divisor_bits = args[*offset + 1].As<Int32>()->Value();
  CHECK_GE(params->params.divisor_bits, -1);

  *offset += 2;

  return JustVoid();
}

Maybe<void> DSAKeyExportTraits::AdditionalConfig(
    const FunctionCallbackInfo<Value>& args,
    unsigned int offset,
    DSAKeyExportConfig* params) {
  return JustVoid();
}

WebCryptoKeyExportStatus DSAKeyExportTraits::DoExport(
    const KeyObjectData& key_data,
    WebCryptoKeyFormat format,
    const DSAKeyExportConfig& params,
    ByteSource* out) {
  CHECK_NE(key_data.GetKeyType(), kKeyTypeSecret);

  switch (format) {
    case kWebCryptoKeyFormatRaw:
      // Not supported for RSA keys of either type
      return WebCryptoKeyExportStatus::FAILED;
    case kWebCryptoKeyFormatPKCS8:
      if (key_data.GetKeyType() != kKeyTypePrivate)
        return WebCryptoKeyExportStatus::INVALID_KEY_TYPE;
      return PKEY_PKCS8_Export(key_data, out);
    case kWebCryptoKeyFormatSPKI:
      if (key_data.GetKeyType() != kKeyTypePublic)
        return WebCryptoKeyExportStatus::INVALID_KEY_TYPE;
      return PKEY_SPKI_Export(key_data, out);
    default:
      UNREACHABLE();
  }
}

bool GetDsaKeyDetail(Environment* env,
                     const KeyObjectData& key,
                     Local<Object> target) {
  if (!key) return false;
  Dsa dsa = key.GetAsymmetricKey();
  if (!dsa) return false;

  size_t modulus_length = dsa.getModulusLength();
  size_t divisor_length = dsa.getDivisorLength();

  return target
             ->Set(env->context(),
                   env->modulus_length_string(),
                   Number::New(env->isolate(),
                               static_cast<double>(modulus_length)))
             .IsJust() &&
         target
             ->Set(env->context(),
                   env->divisor_length_string(),
                   Number::New(env->isolate(),
                               static_cast<double>(divisor_length)))
             .IsJust();
}

namespace DSAAlg {
void Initialize(Environment* env, Local<Object> target) {
  DsaKeyPairGenJob::Initialize(env, target);
  DSAKeyExportJob::Initialize(env, target);
}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  DsaKeyPairGenJob::RegisterExternalReferences(registry);
  DSAKeyExportJob::RegisterExternalReferences(registry);
}
}  // namespace DSAAlg
}  // namespace crypto
}  // namespace node
