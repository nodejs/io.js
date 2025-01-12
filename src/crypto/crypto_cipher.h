#ifndef SRC_CRYPTO_CRYPTO_CIPHER_H_
#define SRC_CRYPTO_CRYPTO_CIPHER_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "crypto/crypto_keys.h"
#include "crypto/crypto_util.h"
#include "base_object.h"
#include "env.h"
#include "memory_tracker.h"
#include "v8.h"

#include <string>

namespace node {
namespace crypto {
class CipherBase : public BaseObject {
 public:
  static void GetSSLCiphers(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetCiphers(const v8::FunctionCallbackInfo<v8::Value>& args);

  static void Initialize(Environment* env, v8::Local<v8::Object> target);
  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(CipherBase)
  SET_SELF_SIZE(CipherBase)

 protected:
  enum CipherKind {
    kCipher,
    kDecipher
  };
  enum UpdateResult {
    kSuccess,
    kErrorMessageSize,
    kErrorState
  };
  enum AuthTagState {
    kAuthTagUnknown,
    kAuthTagKnown,
    kAuthTagPassedToOpenSSL
  };
  static const unsigned kNoAuthTagLength = static_cast<unsigned>(-1);

  void CommonInit(const char* cipher_type,
                  const ncrypto::Cipher& cipher,
                  const unsigned char* key,
                  int key_len,
                  const unsigned char* iv,
                  int iv_len,
                  unsigned int auth_tag_len);
  void Init(const char* cipher_type,
            const ArrayBufferOrViewContents<unsigned char>& key_buf,
            unsigned int auth_tag_len);
  void InitIv(const char* cipher_type,
              const ByteSource& key_buf,
              const ArrayBufferOrViewContents<unsigned char>& iv_buf,
              unsigned int auth_tag_len);
  bool InitAuthenticated(const char* cipher_type, int iv_len,
                         unsigned int auth_tag_len);
  bool CheckCCMMessageLength(int message_len);
  UpdateResult Update(const char* data, size_t len,
                      std::unique_ptr<v8::BackingStore>* out);
  bool Final(std::unique_ptr<v8::BackingStore>* out);
  bool SetAutoPadding(bool auto_padding);

  bool IsAuthenticatedMode() const;
  bool SetAAD(
      const ArrayBufferOrViewContents<unsigned char>& data,
      int plaintext_len);
  bool MaybePassAuthTagToOpenSSL();

  static void New(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Init(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void InitIv(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Update(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Final(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void SetAutoPadding(const v8::FunctionCallbackInfo<v8::Value>& args);

  static void GetAuthTag(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void SetAuthTag(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void SetAAD(const v8::FunctionCallbackInfo<v8::Value>& args);

  CipherBase(Environment* env, v8::Local<v8::Object> wrap, CipherKind kind);

 private:
  ncrypto::CipherCtxPointer ctx_;
  const CipherKind kind_;
  AuthTagState auth_tag_state_;
  unsigned int auth_tag_len_;
  char auth_tag_[EVP_GCM_TLS_TAG_LEN];
  bool pending_auth_failed_;
  int max_message_size_;
};

class PublicKeyCipher {
 public:
  typedef int (*EVP_PKEY_cipher_init_t)(EVP_PKEY_CTX* ctx);
  typedef int (*EVP_PKEY_cipher_t)(EVP_PKEY_CTX* ctx,
                                   unsigned char* out, size_t* outlen,
                                   const unsigned char* in, size_t inlen);

  enum Operation {
    kPublic,
    kPrivate
  };

  template <Operation operation,
            EVP_PKEY_cipher_init_t EVP_PKEY_cipher_init,
            EVP_PKEY_cipher_t EVP_PKEY_cipher>
  static bool Cipher(Environment* env,
                     const ncrypto::EVPKeyPointer& pkey,
                     int padding,
                     const EVP_MD* digest,
                     const ArrayBufferOrViewContents<unsigned char>& oaep_label,
                     const ArrayBufferOrViewContents<unsigned char>& data,
                     std::unique_ptr<v8::BackingStore>* out);

  template <Operation operation,
            EVP_PKEY_cipher_init_t EVP_PKEY_cipher_init,
            EVP_PKEY_cipher_t EVP_PKEY_cipher>
  static void Cipher(const v8::FunctionCallbackInfo<v8::Value>& args);
};

enum WebCryptoCipherMode {
  kWebCryptoCipherEncrypt,
  kWebCryptoCipherDecrypt
};

enum class WebCryptoCipherStatus {
  OK,
  INVALID_KEY_TYPE,
  FAILED
};

// CipherJob is a base implementation class for implementations of
// one-shot sync and async ciphers. It has been added primarily to
// support the AES and RSA ciphers underlying the WebCrypt API.
//
// See the crypto_aes and crypto_rsa headers for examples of how to
// use CipherJob.
template <typename CipherTraits>
class CipherJob final : public CryptoJob<CipherTraits> {
 public:
  using AdditionalParams = typename CipherTraits::AdditionalParameters;

  static void New(const v8::FunctionCallbackInfo<v8::Value>& args) {
    Environment* env = Environment::GetCurrent(args);
    CHECK(args.IsConstructCall());

    CryptoJobMode mode = GetCryptoJobMode(args[0]);

    CHECK(args[1]->IsUint32());  // Cipher Mode

    uint32_t cmode = args[1].As<v8::Uint32>()->Value();
    CHECK_LE(cmode, WebCryptoCipherMode::kWebCryptoCipherDecrypt);
    WebCryptoCipherMode cipher_mode = static_cast<WebCryptoCipherMode>(cmode);

    CHECK(args[2]->IsObject());  // KeyObject
    KeyObjectHandle* key;
    ASSIGN_OR_RETURN_UNWRAP(&key, args[2]);
    CHECK_NOT_NULL(key);

    ArrayBufferOrViewContents<char> data(args[3]);  // data to operate on
    if (!data.CheckSizeInt32())
      return THROW_ERR_OUT_OF_RANGE(env, "data is too large");

    AdditionalParams params;
    if (CipherTraits::AdditionalConfig(mode, args, 4, cipher_mode, &params)
            .IsNothing()) {
      // The CipherTraits::AdditionalConfig is responsible for
      // calling an appropriate THROW_CRYPTO_* variant reporting
      // whatever error caused initialization to fail.
      return;
    }

    new CipherJob<CipherTraits>(
        env,
        args.This(),
        mode,
        key,
        cipher_mode,
        data,
        std::move(params));
  }

  static void Initialize(
      Environment* env,
      v8::Local<v8::Object> target) {
    CryptoJob<CipherTraits>::Initialize(New, env, target);
  }

  static void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
    CryptoJob<CipherTraits>::RegisterExternalReferences(New, registry);
  }

  CipherJob(Environment* env,
            v8::Local<v8::Object> object,
            CryptoJobMode mode,
            KeyObjectHandle* key,
            WebCryptoCipherMode cipher_mode,
            const ArrayBufferOrViewContents<char>& data,
            AdditionalParams&& params)
      : CryptoJob<CipherTraits>(env,
                                object,
                                AsyncWrap::PROVIDER_CIPHERREQUEST,
                                mode,
                                std::move(params)),
        key_(key->Data().addRef()),
        cipher_mode_(cipher_mode),
        in_(mode == kCryptoJobAsync ? data.ToCopy() : data.ToByteSource()) {}

  const KeyObjectData& key() const { return key_; }

  WebCryptoCipherMode cipher_mode() const { return cipher_mode_; }

  void DoThreadPoolWork() override {
    const WebCryptoCipherStatus status =
        CipherTraits::DoCipher(
            AsyncWrap::env(),
            key(),
            cipher_mode_,
            *CryptoJob<CipherTraits>::params(),
            in_,
            &out_);
    if (status == WebCryptoCipherStatus::OK) {
      // Success!
      return;
    }
    CryptoErrorStore* errors = CryptoJob<CipherTraits>::errors();
    errors->Capture();
    if (errors->Empty()) {
      switch (status) {
        case WebCryptoCipherStatus::OK:
          UNREACHABLE();
          break;
        case WebCryptoCipherStatus::INVALID_KEY_TYPE:
          errors->Insert(NodeCryptoError::INVALID_KEY_TYPE);
          break;
        case WebCryptoCipherStatus::FAILED:
          errors->Insert(NodeCryptoError::CIPHER_JOB_FAILED);
          break;
      }
    }
  }

  v8::Maybe<void> ToResult(v8::Local<v8::Value>* err,
                           v8::Local<v8::Value>* result) override {
    Environment* env = AsyncWrap::env();
    CryptoErrorStore* errors = CryptoJob<CipherTraits>::errors();

    if (errors->Empty())
      errors->Capture();

    if (out_.size() > 0 || errors->Empty()) {
      CHECK(errors->Empty());
      *err = v8::Undefined(env->isolate());
      *result = out_.ToArrayBuffer(env);
      if (result->IsEmpty()) {
        return v8::Nothing<void>();
      }
    } else {
      *result = v8::Undefined(env->isolate());
      if (!errors->ToException(env).ToLocal(err)) {
        return v8::Nothing<void>();
      }
    }
    CHECK(!result->IsEmpty());
    CHECK(!err->IsEmpty());
    return v8::JustVoid();
  }

  SET_SELF_SIZE(CipherJob)
  void MemoryInfo(MemoryTracker* tracker) const override {
    if (CryptoJob<CipherTraits>::mode() == kCryptoJobAsync)
      tracker->TrackFieldWithSize("in", in_.size());
    tracker->TrackFieldWithSize("out", out_.size());
    CryptoJob<CipherTraits>::MemoryInfo(tracker);
  }

 private:
  KeyObjectData key_;
  WebCryptoCipherMode cipher_mode_;
  ByteSource in_;
  ByteSource out_;
};

}  // namespace crypto
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#endif  // SRC_CRYPTO_CRYPTO_CIPHER_H_
