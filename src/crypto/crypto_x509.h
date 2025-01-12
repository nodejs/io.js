#ifndef SRC_CRYPTO_CRYPTO_X509_H_
#define SRC_CRYPTO_CRYPTO_X509_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "base_object.h"
#include "crypto/crypto_util.h"
#include "env.h"
#include "memory_tracker.h"
#include "ncrypto.h"
#include "node_worker.h"
#include "v8.h"

namespace node {
namespace crypto {

// The ManagedX509 class is essentially a smart pointer for
// X509 objects that allows an X509Certificate instance to
// be cloned at the JS level while pointing at the same
// underlying X509 instance.
class ManagedX509 final : public MemoryRetainer {
 public:
  ManagedX509() = default;
  explicit ManagedX509(ncrypto::X509Pointer&& cert);
  ManagedX509(const ManagedX509& that);
  ManagedX509& operator=(const ManagedX509& that);

  operator bool() const { return !!cert_; }
  X509* get() const { return cert_.get(); }
  ncrypto::X509View view() const { return cert_; }
  operator ncrypto::X509View() const { return cert_; }

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(ManagedX509)
  SET_SELF_SIZE(ManagedX509)

 private:
  ncrypto::X509Pointer cert_;
};

class X509Certificate final : public BaseObject {
 public:
  enum class GetPeerCertificateFlag {
    NONE,
    SERVER
  };

  static void Initialize(Environment* env, v8::Local<v8::Object> target);
  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);
  static v8::Local<v8::FunctionTemplate> GetConstructorTemplate(
      Environment* env);
  static bool HasInstance(Environment* env, v8::Local<v8::Object> object);

  static v8::MaybeLocal<v8::Object> New(
      Environment* env,
      ncrypto::X509Pointer cert,
      STACK_OF(X509) * issuer_chain = nullptr);

  static v8::MaybeLocal<v8::Object> New(
      Environment* env,
      std::shared_ptr<ManagedX509> cert,
      STACK_OF(X509)* issuer_chain = nullptr);

  static v8::MaybeLocal<v8::Object> GetCert(Environment* env,
                                            const ncrypto::SSLPointer& ssl);

  static v8::MaybeLocal<v8::Object> GetPeerCert(Environment* env,
                                                const ncrypto::SSLPointer& ssl,
                                                GetPeerCertificateFlag flag);

  static v8::Local<v8::Object> Wrap(Environment* env,
                                    v8::Local<v8::Object> object,
                                    ncrypto::X509Pointer cert);

  inline BaseObjectPtr<X509Certificate> getIssuerCert() const {
    return issuer_cert_;
  }

  inline ncrypto::X509View view() const { return *cert_; }
  X509* get() { return cert_->get(); }

  v8::MaybeLocal<v8::Value> toObject(Environment* env);
  static v8::MaybeLocal<v8::Value> toObject(Environment* env,
                                            const ncrypto::X509View& cert);

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(X509Certificate)
  SET_SELF_SIZE(X509Certificate)

  class X509CertificateTransferData : public worker::TransferData {
   public:
    explicit X509CertificateTransferData(
        const std::shared_ptr<ManagedX509>& data)
        : data_(data) {}

    BaseObjectPtr<BaseObject> Deserialize(
        Environment* env,
        v8::Local<v8::Context> context,
        std::unique_ptr<worker::TransferData> self) override;

    SET_MEMORY_INFO_NAME(X509CertificateTransferData)
    SET_SELF_SIZE(X509CertificateTransferData)
    SET_NO_MEMORY_INFO()

   private:
    std::shared_ptr<ManagedX509> data_;
  };

  BaseObject::TransferMode GetTransferMode() const override;
  std::unique_ptr<worker::TransferData> CloneForMessaging() const override;

 private:
  X509Certificate(
      Environment* env,
      v8::Local<v8::Object> object,
      std::shared_ptr<ManagedX509> cert,
      STACK_OF(X509)* issuer_chain = nullptr);

  std::shared_ptr<ManagedX509> cert_;
  BaseObjectPtr<X509Certificate> issuer_cert_;
};

inline X509Certificate::GetPeerCertificateFlag operator|(
    X509Certificate::GetPeerCertificateFlag lhs,
    X509Certificate::GetPeerCertificateFlag rhs) {
  return static_cast<X509Certificate::GetPeerCertificateFlag>(
      static_cast<int>(lhs) | static_cast<int>(rhs));
}

inline X509Certificate::GetPeerCertificateFlag operator&(
    X509Certificate::GetPeerCertificateFlag lhs,
    X509Certificate::GetPeerCertificateFlag rhs) {
  return static_cast<X509Certificate::GetPeerCertificateFlag>(
      static_cast<int>(lhs) & static_cast<int>(rhs));
}

}  // namespace crypto
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
#endif  // SRC_CRYPTO_CRYPTO_X509_H_
