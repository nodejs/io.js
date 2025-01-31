#ifndef SRC_CRYPTO_CRYPTO_EC_H_
#define SRC_CRYPTO_CRYPTO_EC_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "async_wrap.h"
#include "base_object.h"
#include "crypto/crypto_keygen.h"
#include "crypto/crypto_keys.h"
#include "crypto/crypto_util.h"
#include "env.h"
#include "memory_tracker.h"
#include "node_internals.h"
#include "v8.h"

namespace node {
namespace crypto {
int GetCurveFromName(const char* name);

class ECDH final : public BaseObject {
 public:
  ~ECDH() override = default;

  static void Initialize(Environment* env, v8::Local<v8::Object> target);
  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);

  static ncrypto::ECPointPointer BufferToPoint(Environment* env,
                                               const EC_GROUP* group,
                                               v8::Local<v8::Value> buf);

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(ECDH)
  SET_SELF_SIZE(ECDH)

  static void ConvertKey(const v8::FunctionCallbackInfo<v8::Value>& args);

  static void GetCurves(const v8::FunctionCallbackInfo<v8::Value>& args);

 protected:
  ECDH(Environment* env,
       v8::Local<v8::Object> wrap,
       ncrypto::ECKeyPointer&& key);

  static void New(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GenerateKeys(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void ComputeSecret(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetPrivateKey(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void SetPrivateKey(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetPublicKey(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void SetPublicKey(const v8::FunctionCallbackInfo<v8::Value>& args);

  bool IsKeyPairValid();
  bool IsKeyValidForCurve(const ncrypto::BignumPointer& private_key);

  ncrypto::ECKeyPointer key_;
  const EC_GROUP* group_;
};

struct ECDHBitsConfig final : public MemoryRetainer {
  int id_;
  KeyObjectData private_;
  KeyObjectData public_;

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(ECDHBitsConfig)
  SET_SELF_SIZE(ECDHBitsConfig)
};

struct ECDHBitsTraits final {
  using AdditionalParameters = ECDHBitsConfig;
  static constexpr const char* JobName = "ECDHBitsJob";
  static constexpr AsyncWrap::ProviderType Provider =
      AsyncWrap::PROVIDER_DERIVEBITSREQUEST;

  static v8::Maybe<void> AdditionalConfig(
      CryptoJobMode mode,
      const v8::FunctionCallbackInfo<v8::Value>& args,
      unsigned int offset,
      ECDHBitsConfig* params);

  static bool DeriveBits(
      Environment* env,
      const ECDHBitsConfig& params,
      ByteSource* out_);

  static v8::MaybeLocal<v8::Value> EncodeOutput(Environment* env,
                                                const ECDHBitsConfig& params,
                                                ByteSource* out);
};

using ECDHBitsJob = DeriveBitsJob<ECDHBitsTraits>;

struct EcKeyPairParams final : public MemoryRetainer {
  int curve_nid;
  int param_encoding;
  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(EcKeyPairParams)
  SET_SELF_SIZE(EcKeyPairParams)
};

using EcKeyPairGenConfig = KeyPairGenConfig<EcKeyPairParams>;

struct EcKeyGenTraits final {
  using AdditionalParameters = EcKeyPairGenConfig;
  static constexpr const char* JobName = "EcKeyPairGenJob";

  static ncrypto::EVPKeyCtxPointer Setup(EcKeyPairGenConfig* params);

  static v8::Maybe<void> AdditionalConfig(
      CryptoJobMode mode,
      const v8::FunctionCallbackInfo<v8::Value>& args,
      unsigned int* offset,
      EcKeyPairGenConfig* params);
};

using ECKeyPairGenJob = KeyGenJob<KeyPairGenTraits<EcKeyGenTraits>>;

// There is currently no additional information that the
// ECKeyExport needs to collect, but we need to provide
// the base struct anyway.
struct ECKeyExportConfig final : public MemoryRetainer {
  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(ECKeyExportConfig)
  SET_SELF_SIZE(ECKeyExportConfig)
};

struct ECKeyExportTraits final {
  static constexpr const char* JobName = "ECKeyExportJob";
  using AdditionalParameters = ECKeyExportConfig;

  static v8::Maybe<void> AdditionalConfig(
      const v8::FunctionCallbackInfo<v8::Value>& args,
      unsigned int offset,
      ECKeyExportConfig* config);

  static WebCryptoKeyExportStatus DoExport(const KeyObjectData& key_data,
                                           WebCryptoKeyFormat format,
                                           const ECKeyExportConfig& params,
                                           ByteSource* out);
};

using ECKeyExportJob = KeyExportJob<ECKeyExportTraits>;

v8::Maybe<void> ExportJWKEcKey(Environment* env,
                               const KeyObjectData& key,
                               v8::Local<v8::Object> target);

v8::Maybe<void> ExportJWKEdKey(Environment* env,
                               const KeyObjectData& key,
                               v8::Local<v8::Object> target);

KeyObjectData ImportJWKEcKey(Environment* env,
                             v8::Local<v8::Object> jwk,
                             const v8::FunctionCallbackInfo<v8::Value>& args,
                             unsigned int offset);

v8::Maybe<void> GetEcKeyDetail(Environment* env,
                               const KeyObjectData& key,
                               v8::Local<v8::Object> target);
}  // namespace crypto
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#endif  // SRC_CRYPTO_CRYPTO_EC_H_
