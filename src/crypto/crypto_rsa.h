#ifndef SRC_CRYPTO_CRYPTO_RSA_H_
#define SRC_CRYPTO_CRYPTO_RSA_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "crypto/crypto_cipher.h"
#include "crypto/crypto_keygen.h"
#include "crypto/crypto_keys.h"
#include "crypto/crypto_util.h"
#include "env.h"
#include "memory_tracker.h"
#include "v8.h"

namespace node {
namespace crypto {
enum RSAKeyVariant {
  kKeyVariantRSA_SSA_PKCS1_v1_5,
  kKeyVariantRSA_PSS,
  kKeyVariantRSA_OAEP
};

struct RsaKeyPairParams final : public MemoryRetainer {
  RSAKeyVariant variant;
  unsigned int modulus_bits;
  unsigned int exponent;

  // The following options are used for RSA-PSS. If any of them are set, a
  // RSASSA-PSS-params sequence will be added to the key.
  const EVP_MD* md = nullptr;
  const EVP_MD* mgf1_md = nullptr;
  int saltlen = -1;

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(RsaKeyPairParams)
  SET_SELF_SIZE(RsaKeyPairParams)
};

using RsaKeyPairGenConfig = KeyPairGenConfig<RsaKeyPairParams>;

struct RsaKeyGenTraits final {
  using AdditionalParameters = RsaKeyPairGenConfig;
  static constexpr const char* JobName = "RsaKeyPairGenJob";

  static ncrypto::EVPKeyCtxPointer Setup(RsaKeyPairGenConfig* params);

  static v8::Maybe<void> AdditionalConfig(
      CryptoJobMode mode,
      const v8::FunctionCallbackInfo<v8::Value>& args,
      unsigned int* offset,
      RsaKeyPairGenConfig* params);
};

using RSAKeyPairGenJob = KeyGenJob<KeyPairGenTraits<RsaKeyGenTraits>>;

struct RSAKeyExportConfig final : public MemoryRetainer {
  RSAKeyVariant variant = kKeyVariantRSA_SSA_PKCS1_v1_5;
  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(RSAKeyExportConfig)
  SET_SELF_SIZE(RSAKeyExportConfig)
};

struct RSAKeyExportTraits final {
  static constexpr const char* JobName = "RSAKeyExportJob";
  using AdditionalParameters = RSAKeyExportConfig;

  static v8::Maybe<void> AdditionalConfig(
      const v8::FunctionCallbackInfo<v8::Value>& args,
      unsigned int offset,
      RSAKeyExportConfig* config);

  static WebCryptoKeyExportStatus DoExport(const KeyObjectData& key_data,
                                           WebCryptoKeyFormat format,
                                           const RSAKeyExportConfig& params,
                                           ByteSource* out);
};

using RSAKeyExportJob = KeyExportJob<RSAKeyExportTraits>;

struct RSACipherConfig final : public MemoryRetainer {
  CryptoJobMode mode;
  ByteSource label;
  int padding = 0;
  const EVP_MD* digest = nullptr;

  RSACipherConfig() = default;

  RSACipherConfig(RSACipherConfig&& other) noexcept;

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(RSACipherConfig)
  SET_SELF_SIZE(RSACipherConfig)
};

struct RSACipherTraits final {
  static constexpr const char* JobName = "RSACipherJob";
  using AdditionalParameters = RSACipherConfig;

  static v8::Maybe<void> AdditionalConfig(
      CryptoJobMode mode,
      const v8::FunctionCallbackInfo<v8::Value>& args,
      unsigned int offset,
      WebCryptoCipherMode cipher_mode,
      RSACipherConfig* config);

  static WebCryptoCipherStatus DoCipher(Environment* env,
                                        const KeyObjectData& key_data,
                                        WebCryptoCipherMode cipher_mode,
                                        const RSACipherConfig& params,
                                        const ByteSource& in,
                                        ByteSource* out);
};

using RSACipherJob = CipherJob<RSACipherTraits>;

v8::Maybe<void> ExportJWKRsaKey(Environment* env,
                                const KeyObjectData& key,
                                v8::Local<v8::Object> target);

KeyObjectData ImportJWKRsaKey(Environment* env,
                              v8::Local<v8::Object> jwk,
                              const v8::FunctionCallbackInfo<v8::Value>& args,
                              unsigned int offset);

v8::Maybe<void> GetRsaKeyDetail(Environment* env,
                                const KeyObjectData& key,
                                v8::Local<v8::Object> target);

namespace RSAAlg {
void Initialize(Environment* env, v8::Local<v8::Object> target);
void RegisterExternalReferences(ExternalReferenceRegistry* registry);
}  // namespace RSAAlg
}  // namespace crypto
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#endif  // SRC_CRYPTO_CRYPTO_RSA_H_
