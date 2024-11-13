#ifndef SRC_CRYPTO_CRYPTO_SCRYPT_H_
#define SRC_CRYPTO_CRYPTO_SCRYPT_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "crypto/crypto_util.h"
#include "env.h"
#include "memory_tracker.h"
#include "v8.h"

namespace node {
namespace crypto {
#ifndef OPENSSL_NO_SCRYPT

// Scrypt is a password-based key derivation algorithm
// defined in https://tools.ietf.org/html/rfc7914

// It takes as input a password, a salt value, and a
// handful of additional parameters that control the
// cost of the operation. In this case, the higher
// the cost, the better the result. The length parameter
// defines the number of bytes that are generated.

// The salt must be as random as possible and should be
// at least 16 bytes in length.

struct ScryptConfig final : public MemoryRetainer {
  CryptoJobMode mode;
  ByteSource pass;
  ByteSource salt;
  uint32_t N;
  uint32_t r;
  uint32_t p;
  uint64_t maxmem;
  int32_t length;

  ScryptConfig() = default;

  explicit ScryptConfig(ScryptConfig&& other) noexcept;

  ScryptConfig& operator=(ScryptConfig&& other) noexcept;

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(ScryptConfig)
  SET_SELF_SIZE(ScryptConfig)
};

struct ScryptTraits final {
  using AdditionalParameters = ScryptConfig;
  static constexpr const char* JobName = "ScryptJob";
  static constexpr AsyncWrap::ProviderType Provider =
      AsyncWrap::PROVIDER_SCRYPTREQUEST;

  static v8::Maybe<void> AdditionalConfig(
      CryptoJobMode mode,
      const v8::FunctionCallbackInfo<v8::Value>& args,
      unsigned int offset,
      ScryptConfig* params);

  static bool DeriveBits(
      Environment* env,
      const ScryptConfig& params,
      ByteSource* out);

  static v8::MaybeLocal<v8::Value> EncodeOutput(Environment* env,
                                                const ScryptConfig& params,
                                                ByteSource* out);
};

using ScryptJob = DeriveBitsJob<ScryptTraits>;

#else
// If there is no Scrypt support, ScryptJob becomes a non-op
struct ScryptJob {
  static void Initialize(
      Environment* env,
      v8::Local<v8::Object> target) {}
};
#endif  // !OPENSSL_NO_SCRYPT

}  // namespace crypto
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#endif  // SRC_CRYPTO_CRYPTO_SCRYPT_H_
