#ifndef SRC_CRYPTO_CRYPTO_KEYS_H_
#define SRC_CRYPTO_CRYPTO_KEYS_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "crypto/crypto_util.h"
#include "base_object.h"
#include "env.h"
#include "memory_tracker.h"
#include "node_buffer.h"
#include "node_worker.h"
#include "v8.h"

#include <openssl/evp.h>

#include <memory>
#include <string>

namespace node {
namespace crypto {
enum PKEncodingType {
  // RSAPublicKey / RSAPrivateKey according to PKCS#1.
  kKeyEncodingPKCS1,
  // PrivateKeyInfo or EncryptedPrivateKeyInfo according to PKCS#8.
  kKeyEncodingPKCS8,
  // SubjectPublicKeyInfo according to X.509.
  kKeyEncodingSPKI,
  // ECPrivateKey according to SEC1.
  kKeyEncodingSEC1
};

enum PKFormatType {
  kKeyFormatDER,
  kKeyFormatPEM,
  kKeyFormatJWK
};

enum KeyType {
  kKeyTypeSecret,
  kKeyTypePublic,
  kKeyTypePrivate
};

enum KeyEncodingContext {
  kKeyContextInput,
  kKeyContextExport,
  kKeyContextGenerate
};

enum class ParseKeyResult {
  kParseKeyOk,
  kParseKeyNotRecognized,
  kParseKeyNeedPassphrase,
  kParseKeyFailed
};

struct AsymmetricKeyEncodingConfig {
  bool output_key_object_ = false;
  PKFormatType format_ = kKeyFormatDER;
  v8::Maybe<PKEncodingType> type_ = v8::Nothing<PKEncodingType>();
};

using PublicKeyEncodingConfig = AsymmetricKeyEncodingConfig;

struct PrivateKeyEncodingConfig : public AsymmetricKeyEncodingConfig {
  const EVP_CIPHER* cipher_;
  // The ByteSource alone is not enough to distinguish between "no passphrase"
  // and a zero-length passphrase (which can be a null pointer), therefore, we
  // use a NonCopyableMaybe.
  NonCopyableMaybe<ByteSource> passphrase_;
};

// This uses the built-in reference counter of OpenSSL to manage an EVP_PKEY
// which is slightly more efficient than using a shared pointer and easier to
// use.
class ManagedEVPPKey : public MemoryRetainer {
 public:
  ManagedEVPPKey() : mutex_(std::make_shared<Mutex>()) {}
  explicit ManagedEVPPKey(EVPKeyPointer&& pkey);
  ManagedEVPPKey(const ManagedEVPPKey& that);
  ManagedEVPPKey& operator=(const ManagedEVPPKey& that);

  operator bool() const;
  EVP_PKEY* get() const;
  inline const EVPKeyPointer& pkey() const { return pkey_; }
  Mutex* mutex() const;

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(ManagedEVPPKey)
  SET_SELF_SIZE(ManagedEVPPKey)

  static PublicKeyEncodingConfig GetPublicKeyEncodingFromJs(
      const v8::FunctionCallbackInfo<v8::Value>& args,
      unsigned int* offset,
      KeyEncodingContext context);

  static NonCopyableMaybe<PrivateKeyEncodingConfig> GetPrivateKeyEncodingFromJs(
      const v8::FunctionCallbackInfo<v8::Value>& args,
      unsigned int* offset,
      KeyEncodingContext context);

  static ManagedEVPPKey GetParsedKey(Environment* env,
                                     EVPKeyPointer&& pkey,
                                     ParseKeyResult ret,
                                     const char* default_msg);

  static ManagedEVPPKey GetPublicOrPrivateKeyFromJs(
    const v8::FunctionCallbackInfo<v8::Value>& args,
    unsigned int* offset);

  static ManagedEVPPKey GetPrivateKeyFromJs(
      const v8::FunctionCallbackInfo<v8::Value>& args,
      unsigned int* offset,
      bool allow_key_object);

  v8::Maybe<void> ToEncodedPublicKey(Environment* env,
                                     const PublicKeyEncodingConfig& config,
                                     v8::Local<v8::Value>* out);

  v8::Maybe<void> ToEncodedPrivateKey(Environment* env,
                                      const PrivateKeyEncodingConfig& config,
                                      v8::Local<v8::Value>* out);

 private:
  size_t size_of_private_key() const;
  size_t size_of_public_key() const;

  EVPKeyPointer pkey_;
  std::shared_ptr<Mutex> mutex_;
};

// Objects of this class can safely be shared among threads.
class KeyObjectData : public MemoryRetainer {
 public:
  static std::shared_ptr<KeyObjectData> CreateSecret(ByteSource key);

  static std::shared_ptr<KeyObjectData> CreateAsymmetric(
      KeyType type,
      const ManagedEVPPKey& pkey);

  KeyType GetKeyType() const;

  // These functions allow unprotected access to the raw key material and should
  // only be used to implement cryptographic operations requiring the key.
  ManagedEVPPKey GetAsymmetricKey() const;
  const char* GetSymmetricKey() const;
  size_t GetSymmetricKeySize() const;

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(KeyObjectData)
  SET_SELF_SIZE(KeyObjectData)

 private:
  explicit KeyObjectData(ByteSource symmetric_key);

  KeyObjectData(
      KeyType type,
      const ManagedEVPPKey& pkey);

  const KeyType key_type_;
  const ByteSource symmetric_key_;
  const ManagedEVPPKey asymmetric_key_;
};

class KeyObjectHandle : public BaseObject {
 public:
  static bool HasInstance(Environment* env, v8::Local<v8::Value> value);
  static v8::Local<v8::Function> Initialize(Environment* env);
  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);

  static v8::MaybeLocal<v8::Object> Create(Environment* env,
                                           std::shared_ptr<KeyObjectData> data);

  // TODO(tniessen): track the memory used by OpenSSL types
  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(KeyObjectHandle)
  SET_SELF_SIZE(KeyObjectHandle)

  const std::shared_ptr<KeyObjectData>& Data();

 protected:
  static void New(const v8::FunctionCallbackInfo<v8::Value>& args);

  static void Init(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void InitECRaw(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void InitEDRaw(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void InitJWK(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetKeyDetail(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Equals(const v8::FunctionCallbackInfo<v8::Value>& args);

  static void ExportJWK(const v8::FunctionCallbackInfo<v8::Value>& args);

  static void GetAsymmetricKeyType(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  v8::Local<v8::Value> GetAsymmetricKeyType() const;

  static void CheckEcKeyData(const v8::FunctionCallbackInfo<v8::Value>& args);
  bool CheckEcKeyData() const;

  static void GetSymmetricKeySize(
      const v8::FunctionCallbackInfo<v8::Value>& args);

  static void Export(const v8::FunctionCallbackInfo<v8::Value>& args);

  v8::MaybeLocal<v8::Value> ExportSecretKey() const;
  v8::MaybeLocal<v8::Value> ExportPublicKey(
      const PublicKeyEncodingConfig& config) const;
  v8::MaybeLocal<v8::Value> ExportPrivateKey(
      const PrivateKeyEncodingConfig& config) const;

  KeyObjectHandle(Environment* env,
                  v8::Local<v8::Object> wrap);

 private:
  std::shared_ptr<KeyObjectData> data_;
};

class NativeKeyObject : public BaseObject {
 public:
  static void Initialize(Environment* env, v8::Local<v8::Object> target);
  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);

  static void New(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void CreateNativeKeyObjectClass(
      const v8::FunctionCallbackInfo<v8::Value>& args);

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(NativeKeyObject)
  SET_SELF_SIZE(NativeKeyObject)

  class KeyObjectTransferData : public worker::TransferData {
   public:
    explicit KeyObjectTransferData(const std::shared_ptr<KeyObjectData>& data)
        : data_(data) {}

    BaseObjectPtr<BaseObject> Deserialize(
        Environment* env,
        v8::Local<v8::Context> context,
        std::unique_ptr<worker::TransferData> self) override;

    SET_MEMORY_INFO_NAME(KeyObjectTransferData)
    SET_SELF_SIZE(KeyObjectTransferData)
    SET_NO_MEMORY_INFO()

   private:
    std::shared_ptr<KeyObjectData> data_;
  };

  BaseObject::TransferMode GetTransferMode() const override;
  std::unique_ptr<worker::TransferData> CloneForMessaging() const override;

 private:
  NativeKeyObject(Environment* env,
                  v8::Local<v8::Object> wrap,
                  const std::shared_ptr<KeyObjectData>& handle_data)
    : BaseObject(env, wrap),
      handle_data_(handle_data) {
    MakeWeak();
  }

  std::shared_ptr<KeyObjectData> handle_data_;
};

enum WebCryptoKeyFormat {
  kWebCryptoKeyFormatRaw,
  kWebCryptoKeyFormatPKCS8,
  kWebCryptoKeyFormatSPKI,
  kWebCryptoKeyFormatJWK
};

enum class WebCryptoKeyExportStatus {
  OK,
  INVALID_KEY_TYPE,
  FAILED
};

template <typename KeyExportTraits>
class KeyExportJob final : public CryptoJob<KeyExportTraits> {
 public:
  using AdditionalParams = typename KeyExportTraits::AdditionalParameters;

  static void New(const v8::FunctionCallbackInfo<v8::Value>& args) {
    Environment* env = Environment::GetCurrent(args);
    CHECK(args.IsConstructCall());

    CryptoJobMode mode = GetCryptoJobMode(args[0]);

    CHECK(args[1]->IsUint32());  // Export Type
    CHECK(args[2]->IsObject());  // KeyObject

    WebCryptoKeyFormat format =
        static_cast<WebCryptoKeyFormat>(args[1].As<v8::Uint32>()->Value());

    KeyObjectHandle* key;
    ASSIGN_OR_RETURN_UNWRAP(&key, args[2]);

    CHECK_NOT_NULL(key);

    AdditionalParams params;
    if (KeyExportTraits::AdditionalConfig(args, 3, &params).IsNothing()) {
      // The KeyExportTraits::AdditionalConfig is responsible for
      // calling an appropriate THROW_CRYPTO_* variant reporting
      // whatever error caused initialization to fail.
      return;
    }

    new KeyExportJob<KeyExportTraits>(
        env,
        args.This(),
        mode,
        key->Data(),
        format,
        std::move(params));
  }

  static void Initialize(
      Environment* env,
      v8::Local<v8::Object> target) {
    CryptoJob<KeyExportTraits>::Initialize(New, env, target);
  }

  static void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
    CryptoJob<KeyExportTraits>::RegisterExternalReferences(New, registry);
  }

  KeyExportJob(
      Environment* env,
      v8::Local<v8::Object> object,
      CryptoJobMode mode,
      std::shared_ptr<KeyObjectData> key,
      WebCryptoKeyFormat format,
      AdditionalParams&& params)
      : CryptoJob<KeyExportTraits>(
            env,
            object,
            AsyncWrap::PROVIDER_KEYEXPORTREQUEST,
            mode,
            std::move(params)),
        key_(key),
        format_(format) {}

  WebCryptoKeyFormat format() const { return format_; }

  void DoThreadPoolWork() override {
    const WebCryptoKeyExportStatus status =
        KeyExportTraits::DoExport(
            key_,
            format_,
            *CryptoJob<KeyExportTraits>::params(),
            &out_);
    if (status == WebCryptoKeyExportStatus::OK) {
      // Success!
      return;
    }
    CryptoErrorStore* errors = CryptoJob<KeyExportTraits>::errors();
    errors->Capture();
    if (errors->Empty()) {
      switch (status) {
        case WebCryptoKeyExportStatus::OK:
          UNREACHABLE();
          break;
        case WebCryptoKeyExportStatus::INVALID_KEY_TYPE:
          errors->Insert(NodeCryptoError::INVALID_KEY_TYPE);
          break;
        case WebCryptoKeyExportStatus::FAILED:
          errors->Insert(NodeCryptoError::CIPHER_JOB_FAILED);
          break;
      }
    }
  }

  v8::Maybe<bool> ToResult(
      v8::Local<v8::Value>* err,
      v8::Local<v8::Value>* result) override {
    Environment* env = AsyncWrap::env();
    CryptoErrorStore* errors = CryptoJob<KeyExportTraits>::errors();
    if (out_.size() > 0) {
      CHECK(errors->Empty());
      *err = v8::Undefined(env->isolate());
      *result = out_.ToArrayBuffer(env);
      return v8::Just(!result->IsEmpty());
    }

    if (errors->Empty())
      errors->Capture();
    CHECK(!errors->Empty());
    *result = v8::Undefined(env->isolate());
    return v8::Just(errors->ToException(env).ToLocal(err));
  }

  SET_SELF_SIZE(KeyExportJob)
  void MemoryInfo(MemoryTracker* tracker) const override {
    tracker->TrackFieldWithSize("out", out_.size());
    CryptoJob<KeyExportTraits>::MemoryInfo(tracker);
  }

 private:
  std::shared_ptr<KeyObjectData> key_;
  WebCryptoKeyFormat format_;
  ByteSource out_;
};

WebCryptoKeyExportStatus PKEY_SPKI_Export(
    KeyObjectData* key_data,
    ByteSource* out);

WebCryptoKeyExportStatus PKEY_PKCS8_Export(
    KeyObjectData* key_data,
    ByteSource* out);

namespace Keys {
void Initialize(Environment* env, v8::Local<v8::Object> target);
void RegisterExternalReferences(ExternalReferenceRegistry* registry);
}  // namespace Keys

}  // namespace crypto
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#endif  // SRC_CRYPTO_CRYPTO_KEYS_H_
