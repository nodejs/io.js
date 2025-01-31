#ifndef SRC_CRYPTO_CRYPTO_HMAC_H_
#define SRC_CRYPTO_CRYPTO_HMAC_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "base_object.h"
#include "crypto/crypto_keys.h"
#include "crypto/crypto_sig.h"
#include "crypto/crypto_util.h"
#include "env.h"
#include "memory_tracker.h"
#include "v8.h"

namespace node {
namespace crypto {
class Hmac : public BaseObject {
 public:
  static void Initialize(Environment* env, v8::Local<v8::Object> target);
  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(Hmac)
  SET_SELF_SIZE(Hmac)

 protected:
  void HmacInit(const char* hash_type, const char* key, int key_len);
  bool HmacUpdate(const char* data, size_t len);

  static void New(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void HmacInit(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void HmacUpdate(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void HmacDigest(const v8::FunctionCallbackInfo<v8::Value>& args);

  Hmac(Environment* env, v8::Local<v8::Object> wrap);

  static void Sign(const v8::FunctionCallbackInfo<v8::Value>& args);

 private:
  ncrypto::HMACCtxPointer ctx_;
};

struct HmacConfig final : public MemoryRetainer {
  CryptoJobMode job_mode;
  SignConfiguration::Mode mode;
  KeyObjectData key;
  ByteSource data;
  ByteSource signature;
  const EVP_MD* digest;

  HmacConfig() = default;

  explicit HmacConfig(HmacConfig&& other) noexcept;

  HmacConfig& operator=(HmacConfig&& other) noexcept;

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(HmacConfig)
  SET_SELF_SIZE(HmacConfig)
};

struct HmacTraits final {
  using AdditionalParameters = HmacConfig;
  static constexpr const char* JobName = "HmacJob";

// TODO(@jasnell): Sign request vs. Verify request

  static constexpr AsyncWrap::ProviderType Provider =
      AsyncWrap::PROVIDER_SIGNREQUEST;

  static v8::Maybe<void> AdditionalConfig(
      CryptoJobMode mode,
      const v8::FunctionCallbackInfo<v8::Value>& args,
      unsigned int offset,
      HmacConfig* params);

  static bool DeriveBits(
      Environment* env,
      const HmacConfig& params,
      ByteSource* out);

  static v8::MaybeLocal<v8::Value> EncodeOutput(Environment* env,
                                                const HmacConfig& params,
                                                ByteSource* out);
};

using HmacJob = DeriveBitsJob<HmacTraits>;

}  // namespace crypto
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#endif  // SRC_CRYPTO_CRYPTO_HMAC_H_
