#ifndef SRC_CRYPTO_CRYPTO_PBKDF2_H_
#define SRC_CRYPTO_CRYPTO_PBKDF2_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "crypto/crypto_util.h"
#include "async_wrap.h"
#include "env.h"
#include "memory_tracker.h"
#include "v8.h"

namespace node {
namespace crypto {
// PBKDF2 is a pseudo-random key derivation scheme defined
// in https://tools.ietf.org/html/rfc8018
//
// The algorithm takes as input a password and salt
// (both of which may, but should not be zero-length),
// a number of iterations, a hash digest algorithm, and
// an output length.
//
// The salt should be as unique as possible, and should
// be at least 16 bytes in length.
//
// The iteration count should be as high as possible.

struct PBKDF2Config final : public MemoryRetainer {
  CryptoJobMode mode;
  ByteSource pass;
  ByteSource salt;
  int32_t iterations;
  int32_t length;
  const EVP_MD* digest = nullptr;

  PBKDF2Config() = default;

  explicit PBKDF2Config(PBKDF2Config&& other) noexcept;

  PBKDF2Config& operator=(PBKDF2Config&& other) noexcept;

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(PBKDF2Config)
  SET_SELF_SIZE(PBKDF2Config)
};

struct PBKDF2Traits final {
  using AdditionalParameters = PBKDF2Config;
  static constexpr const char* JobName = "PBKDF2Job";
  static constexpr AsyncWrap::ProviderType Provider =
      AsyncWrap::PROVIDER_PBKDF2REQUEST;

  static v8::Maybe<void> AdditionalConfig(
      CryptoJobMode mode,
      const v8::FunctionCallbackInfo<v8::Value>& args,
      unsigned int offset,
      PBKDF2Config* params);

  static bool DeriveBits(
      Environment* env,
      const PBKDF2Config& params,
      ByteSource* out);

  static v8::MaybeLocal<v8::Value> EncodeOutput(Environment* env,
                                                const PBKDF2Config& params,
                                                ByteSource* out);
};

using PBKDF2Job = DeriveBitsJob<PBKDF2Traits>;

}  // namespace crypto
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#endif  // SRC_CRYPTO_CRYPTO_PBKDF2_H_
