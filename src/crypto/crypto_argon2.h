#ifndef SRC_CRYPTO_CRYPTO_ARGON2_H_
#define SRC_CRYPTO_CRYPTO_ARGON2_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "crypto/crypto_util.h"

namespace node::crypto {
#ifndef OPENSSL_NO_ARGON2

// Argon2 is a password-based key derivation algorithm
// defined in https://datatracker.ietf.org/doc/html/rfc9106

// It takes as input a password, a salt value, and a
// handful of additional parameters that control the
// cost of the operation. In this case, the higher
// the cost, the better the result. The length parameter
// defines the number of bytes that are generated.

// The salt must be as random as possible and should be
// at least 16 bytes in length.

struct Argon2Config final : public MemoryRetainer {
  CryptoJobMode mode;
  OsslLibCtxPointer ctx;
  EVPKdfPointer kdf;
  ByteSource pass;
  ByteSource salt;
  ByteSource secret;
  ByteSource ad;
  uint32_t iter;
  uint32_t lanes;
  uint32_t memcost;
  uint32_t keylen;

  Argon2Config() = default;

  explicit Argon2Config(Argon2Config&& other) noexcept;

  Argon2Config& operator=(Argon2Config&& other) noexcept;

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(Argon2Config)
  SET_SELF_SIZE(Argon2Config)
};

struct Argon2Traits final {
  using AdditionalParameters = Argon2Config;
  static constexpr const char* JobName = "Argon2Job";
  static constexpr AsyncWrap::ProviderType Provider =
      AsyncWrap::PROVIDER_ARGON2REQUEST;

  static v8::Maybe<bool> AdditionalConfig(
      CryptoJobMode mode,
      const v8::FunctionCallbackInfo<v8::Value>& args,
      unsigned int offset,
      Argon2Config* config);

  static bool DeriveBits(Environment* env,
                         const Argon2Config& config,
                         ByteSource* out);

  static v8::Maybe<bool> EncodeOutput(Environment* env,
                                      const Argon2Config& config,
                                      ByteSource* out,
                                      v8::Local<v8::Value>* result);
};

using Argon2Job = DeriveBitsJob<Argon2Traits>;

#else
// If there is no Argon2 support, Argon2Job becomes a non-op
struct Argon2Job {
  static void Initialize(Environment* env, v8::Local<v8::Object> target) {}
};
#endif  // !OPENSSL_NO_ARGON2

}  // namespace node::crypto

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#endif  // SRC_CRYPTO_CRYPTO_ARGON2_H_
