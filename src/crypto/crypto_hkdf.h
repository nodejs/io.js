#ifndef SRC_CRYPTO_CRYPTO_HKDF_H_
#define SRC_CRYPTO_CRYPTO_HKDF_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "async_wrap.h"
#include "base_object.h"
#include "crypto/crypto_keys.h"
#include "crypto/crypto_util.h"
#include "v8.h"

namespace node {
namespace crypto {
struct HKDFConfig final : public MemoryRetainer {
  CryptoJobMode mode;
  size_t length;
  const EVP_MD* digest;
  KeyObjectData key;
  ByteSource salt;
  ByteSource info;

  HKDFConfig() = default;

  explicit HKDFConfig(HKDFConfig&& other) noexcept;

  HKDFConfig& operator=(HKDFConfig&& other) noexcept;

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(HKDFConfig)
  SET_SELF_SIZE(HKDFConfig)
};

struct HKDFTraits final {
  using AdditionalParameters = HKDFConfig;
  static constexpr const char* JobName = "HKDFJob";
  static constexpr AsyncWrap::ProviderType Provider =
      AsyncWrap::PROVIDER_DERIVEBITSREQUEST;

  static v8::Maybe<void> AdditionalConfig(
      CryptoJobMode mode,
      const v8::FunctionCallbackInfo<v8::Value>& args,
      unsigned int offset,
      HKDFConfig* params);

  static bool DeriveBits(
      Environment* env,
      const HKDFConfig& params,
      ByteSource* out);

  static v8::MaybeLocal<v8::Value> EncodeOutput(Environment* env,
                                                const HKDFConfig& params,
                                                ByteSource* out);
};

using HKDFJob = DeriveBitsJob<HKDFTraits>;

}  // namespace crypto
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#endif  // SRC_CRYPTO_CRYPTO_HKDF_H_
