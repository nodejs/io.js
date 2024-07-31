#ifndef SRC_CRYPTO_CRYPTO_SIG_H_
#define SRC_CRYPTO_CRYPTO_SIG_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "base_object.h"
#include "crypto/crypto_keys.h"
#include "crypto/crypto_util.h"
#include "env.h"
#include "memory_tracker.h"

namespace node {
namespace crypto {
static const unsigned int kNoDsaSignature = static_cast<unsigned int>(-1);

enum DSASigEnc {
  kSigEncDER,
  kSigEncP1363
};

class SignBase : public BaseObject {
 public:
  enum Error {
    kSignOk,
    kSignUnknownDigest,
    kSignInit,
    kSignNotInitialised,
    kSignUpdate,
    kSignPrivateKey,
    kSignPublicKey,
    kSignMalformedSignature
  };

  SignBase(Environment* env, v8::Local<v8::Object> wrap);

  Error Init(const char* sign_type);
  Error Update(const char* data, size_t len);

  // TODO(joyeecheung): track the memory used by OpenSSL types
  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(SignBase)
  SET_SELF_SIZE(SignBase)

 protected:
  EVPMDCtxPointer mdctx_;
};

class Sign : public SignBase {
 public:
  static void Initialize(Environment* env, v8::Local<v8::Object> target);
  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);

  struct SignResult {
    Error error;
    std::unique_ptr<v8::BackingStore> signature;

    explicit SignResult(
        Error err,
        std::unique_ptr<v8::BackingStore>&& sig = nullptr)
      : error(err), signature(std::move(sig)) {}
  };

  SignResult SignFinal(
      const ManagedEVPPKey& pkey,
      int padding,
      const v8::Maybe<int>& saltlen,
      DSASigEnc dsa_sig_enc);

  static void SignSync(const v8::FunctionCallbackInfo<v8::Value>& args);

 protected:
  static void New(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void SignInit(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void SignUpdate(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void SignFinal(const v8::FunctionCallbackInfo<v8::Value>& args);

  Sign(Environment* env, v8::Local<v8::Object> wrap);
};

class Verify : public SignBase {
 public:
  static void Initialize(Environment* env, v8::Local<v8::Object> target);
  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);

  Error VerifyFinal(const ManagedEVPPKey& key,
                    const ByteSource& sig,
                    int padding,
                    const v8::Maybe<int>& saltlen,
                    bool* verify_result);

  static void VerifySync(const v8::FunctionCallbackInfo<v8::Value>& args);

 protected:
  static void New(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void VerifyInit(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void VerifyUpdate(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void VerifyFinal(const v8::FunctionCallbackInfo<v8::Value>& args);

  Verify(Environment* env, v8::Local<v8::Object> wrap);
};

struct SignConfiguration final : public MemoryRetainer {
  enum Mode {
    kSign,
    kVerify
  };
  enum Flags {
    kHasNone = 0,
    kHasSaltLength = 1,
    kHasPadding = 2
  };

  CryptoJobMode job_mode;
  Mode mode;
  ManagedEVPPKey key;
  ByteSource data;
  ByteSource signature;
  const EVP_MD* digest = nullptr;
  int flags = SignConfiguration::kHasNone;
  int padding = 0;
  int salt_length = 0;
  DSASigEnc dsa_encoding = kSigEncDER;

  SignConfiguration() = default;

  explicit SignConfiguration(SignConfiguration&& other) noexcept;

  SignConfiguration& operator=(SignConfiguration&& other) noexcept;

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(SignConfiguration)
  SET_SELF_SIZE(SignConfiguration)
};

struct SignTraits final {
  using AdditionalParameters = SignConfiguration;
  static constexpr const char* JobName = "SignJob";

// TODO(@jasnell): Sign request vs. Verify request

  static constexpr AsyncWrap::ProviderType Provider =
      AsyncWrap::PROVIDER_SIGNREQUEST;

  static v8::Maybe<bool> AdditionalConfig(
      CryptoJobMode mode,
      const v8::FunctionCallbackInfo<v8::Value>& args,
      unsigned int offset,
      SignConfiguration* params);

  static bool DeriveBits(
      Environment* env,
      const SignConfiguration& params,
      ByteSource* out);

  static v8::Maybe<bool> EncodeOutput(
      Environment* env,
      const SignConfiguration& params,
      ByteSource* out,
      v8::Local<v8::Value>* result);
};

using SignJob = DeriveBitsJob<SignTraits>;

}  // namespace crypto
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#endif  // SRC_CRYPTO_CRYPTO_SIG_H_
