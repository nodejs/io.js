#ifndef SRC_CRYPTO_CRYPTO_DSA_H_
#define SRC_CRYPTO_CRYPTO_DSA_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "crypto/crypto_keys.h"
#include "crypto/crypto_keygen.h"
#include "crypto/crypto_util.h"
#include "env.h"
#include "memory_tracker.h"
#include "v8.h"

namespace node::crypto {
struct DsaKeyPairParams final : public MemoryRetainer {
  unsigned int modulus_bits;
  int divisor_bits;
  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(DsaKeyPairParams)
  SET_SELF_SIZE(DsaKeyPairParams)
};

using DsaKeyPairGenConfig = KeyPairGenConfig<DsaKeyPairParams>;

struct DsaKeyGenTraits final {
  using AdditionalParameters = DsaKeyPairGenConfig;
  static constexpr const char* JobName = "DsaKeyPairGenJob";

  static ncrypto::EVPKeyCtxPointer Setup(DsaKeyPairGenConfig* params);

  static v8::Maybe<void> AdditionalConfig(
      CryptoJobMode mode,
      const v8::FunctionCallbackInfo<v8::Value>& args,
      unsigned int* offset,
      DsaKeyPairGenConfig* params);
};

using DsaKeyPairGenJob = KeyGenJob<KeyPairGenTraits<DsaKeyGenTraits>>;

struct DSAKeyExportConfig final : public MemoryRetainer {
  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(DSAKeyExportConfig)
  SET_SELF_SIZE(DSAKeyExportConfig)
};

struct DSAKeyExportTraits final {
  static constexpr const char* JobName = "DSAKeyExportJob";
  using AdditionalParameters = DSAKeyExportConfig;

  static v8::Maybe<void> AdditionalConfig(
      const v8::FunctionCallbackInfo<v8::Value>& args,
      unsigned int offset,
      DSAKeyExportConfig* config);

  static WebCryptoKeyExportStatus DoExport(const KeyObjectData& key_data,
                                           WebCryptoKeyFormat format,
                                           const DSAKeyExportConfig& params,
                                           ByteSource* out);
};

using DSAKeyExportJob = KeyExportJob<DSAKeyExportTraits>;

bool GetDsaKeyDetail(Environment* env,
                     const KeyObjectData& key,
                     v8::Local<v8::Object> target);

namespace DSAAlg {
void Initialize(Environment* env, v8::Local<v8::Object> target);
void RegisterExternalReferences(ExternalReferenceRegistry* registry);
}  // namespace DSAAlg
}  // namespace node::crypto

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#endif  // SRC_CRYPTO_CRYPTO_DSA_H_
