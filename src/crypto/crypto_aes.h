#ifndef SRC_CRYPTO_CRYPTO_AES_H_
#define SRC_CRYPTO_CRYPTO_AES_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "crypto/crypto_cipher.h"
#include "crypto/crypto_keys.h"
#include "crypto/crypto_util.h"
#include "env.h"
#include "v8.h"

namespace node {
namespace crypto {
constexpr size_t kAesBlockSize = 16;
constexpr unsigned kNoAuthTagLength = static_cast<unsigned>(-1);
constexpr const char* kDefaultWrapIV = "\xa6\xa6\xa6\xa6\xa6\xa6\xa6\xa6";

enum class AESCipherMode {
  CTR,
  CBC,
  GCM,
  KW,
};

#define VARIANTS(V)                                                            \
  V(CTR_128, AES_CTR_Cipher, AESCipherMode::CTR, NID_aes_128_ctr)              \
  V(CTR_192, AES_CTR_Cipher, AESCipherMode::CTR, NID_aes_192_ctr)              \
  V(CTR_256, AES_CTR_Cipher, AESCipherMode::CTR, NID_aes_256_ctr)              \
  V(CBC_128, AES_Cipher, AESCipherMode::CBC, NID_aes_128_cbc)                  \
  V(CBC_192, AES_Cipher, AESCipherMode::CBC, NID_aes_192_cbc)                  \
  V(CBC_256, AES_Cipher, AESCipherMode::CBC, NID_aes_256_cbc)                  \
  V(GCM_128, AES_Cipher, AESCipherMode::GCM, NID_aes_128_gcm)                  \
  V(GCM_192, AES_Cipher, AESCipherMode::GCM, NID_aes_192_gcm)                  \
  V(GCM_256, AES_Cipher, AESCipherMode::GCM, NID_aes_256_gcm)                  \
  V(KW_128, AES_Cipher, AESCipherMode::KW, NID_id_aes128_wrap)                 \
  V(KW_192, AES_Cipher, AESCipherMode::KW, NID_id_aes192_wrap)                 \
  V(KW_256, AES_Cipher, AESCipherMode::KW, NID_id_aes256_wrap)

enum AESKeyVariant {
#define V(name, _, __, ___) kKeyVariantAES_##name,
  VARIANTS(V)
#undef V
};

struct AESCipherConfig final : public MemoryRetainer {
  CryptoJobMode mode;
  AESKeyVariant variant;
  const EVP_CIPHER* cipher;
  size_t length;
  ByteSource iv;  // Used for both iv or counter
  ByteSource additional_data;
  ByteSource tag;  // Used only for authenticated modes (GCM)

  AESCipherConfig() = default;

  AESCipherConfig(AESCipherConfig&& other) noexcept;

  AESCipherConfig& operator=(AESCipherConfig&& other) noexcept;

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(AESCipherConfig)
  SET_SELF_SIZE(AESCipherConfig)
};

struct AESCipherTraits final {
  static constexpr const char* JobName = "AESCipherJob";

  using AdditionalParameters = AESCipherConfig;

  static v8::Maybe<bool> AdditionalConfig(
      CryptoJobMode mode,
      const v8::FunctionCallbackInfo<v8::Value>& args,
      unsigned int offset,
      WebCryptoCipherMode cipher_mode,
      AESCipherConfig* config);

  static WebCryptoCipherStatus DoCipher(
      Environment* env,
      std::shared_ptr<KeyObjectData> key_data,
      WebCryptoCipherMode cipher_mode,
      const AESCipherConfig& params,
      const ByteSource& in,
      ByteSource* out);
};

using AESCryptoJob = CipherJob<AESCipherTraits>;

namespace AES {
void Initialize(Environment* env, v8::Local<v8::Object> target);
void RegisterExternalReferences(ExternalReferenceRegistry* registry);
}  // namespace AES
}  // namespace crypto
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#endif  // SRC_CRYPTO_CRYPTO_AES_H_
