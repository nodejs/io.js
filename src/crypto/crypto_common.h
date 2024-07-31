#ifndef SRC_CRYPTO_CRYPTO_COMMON_H_
#define SRC_CRYPTO_CRYPTO_COMMON_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "node_crypto.h"
#include "v8.h"
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <string>

// Some OpenSSL 1.1.1 functions unnecessarily operate on and return non-const
// pointers, whereas the same functions in OpenSSL 3 use const pointers.
#if OPENSSL_VERSION_MAJOR >= 3
#define OSSL3_CONST const
#else
#define OSSL3_CONST
#endif

namespace node {
namespace crypto {

struct StackOfX509Deleter {
  void operator()(STACK_OF(X509)* p) const { sk_X509_pop_free(p, X509_free); }
};
using StackOfX509 = std::unique_ptr<STACK_OF(X509), StackOfX509Deleter>;

struct StackOfXASN1Deleter {
  void operator()(STACK_OF(ASN1_OBJECT)* p) const {
    sk_ASN1_OBJECT_pop_free(p, ASN1_OBJECT_free);
  }
};
using StackOfASN1 = std::unique_ptr<STACK_OF(ASN1_OBJECT), StackOfXASN1Deleter>;

X509Pointer SSL_CTX_get_issuer(SSL_CTX* ctx, X509* cert);

void LogSecret(
    const SSLPointer& ssl,
    const char* name,
    const unsigned char* secret,
    size_t secretlen);

v8::MaybeLocal<v8::Value> GetSSLOCSPResponse(
    Environment* env,
    SSL* ssl,
    v8::Local<v8::Value> default_value);

bool SetTLSSession(
    const SSLPointer& ssl,
    const SSLSessionPointer& session);

SSLSessionPointer GetTLSSession(const unsigned char* buf, size_t length);

long VerifyPeerCertificate(  // NOLINT(runtime/int)
    const SSLPointer& ssl,
    long def = X509_V_ERR_UNSPECIFIED);  // NOLINT(runtime/int)

bool UseSNIContext(const SSLPointer& ssl, BaseObjectPtr<SecureContext> context);

const char* GetClientHelloALPN(const SSLPointer& ssl);

const char* GetClientHelloServerName(const SSLPointer& ssl);

const char* GetServerName(SSL* ssl);

v8::MaybeLocal<v8::Array> GetClientHelloCiphers(
    Environment* env,
    const SSLPointer& ssl);

bool SetGroups(SecureContext* sc, const char* groups);

const char* X509ErrorCode(long err);  // NOLINT(runtime/int)

v8::MaybeLocal<v8::Value> GetValidationErrorReason(Environment* env, int err);

v8::MaybeLocal<v8::Value> GetValidationErrorCode(Environment* env, int err);

v8::MaybeLocal<v8::Value> GetCert(Environment* env, const SSLPointer& ssl);

v8::MaybeLocal<v8::Object> GetCipherInfo(
    Environment* env,
    const SSLPointer& ssl);

v8::MaybeLocal<v8::Object> GetEphemeralKey(
    Environment* env,
    const SSLPointer& ssl);

v8::MaybeLocal<v8::Value> GetPeerCert(
    Environment* env,
    const SSLPointer& ssl,
    bool abbreviated = false,
    bool is_server = false);

v8::MaybeLocal<v8::Object> ECPointToBuffer(
    Environment* env,
    const EC_GROUP* group,
    const EC_POINT* point,
    point_conversion_form_t form,
    const char** error);

v8::MaybeLocal<v8::Object> X509ToObject(
    Environment* env,
    X509* cert);

v8::MaybeLocal<v8::Value> GetValidTo(
    Environment* env,
    X509* cert,
    const BIOPointer& bio);

v8::MaybeLocal<v8::Value> GetValidFrom(
    Environment* env,
    X509* cert,
    const BIOPointer& bio);

v8::MaybeLocal<v8::Value> GetFingerprintDigest(
    Environment* env,
    const EVP_MD* method,
    X509* cert);

v8::MaybeLocal<v8::Value> GetKeyUsage(Environment* env, X509* cert);
v8::MaybeLocal<v8::Value> GetCurrentCipherName(Environment* env,
                                               const SSLPointer& ssl);
v8::MaybeLocal<v8::Value> GetCurrentCipherVersion(Environment* env,
                                                  const SSLPointer& ssl);

v8::MaybeLocal<v8::Value> GetSerialNumber(Environment* env, X509* cert);

v8::MaybeLocal<v8::Value> GetRawDERCertificate(Environment* env, X509* cert);

v8::Local<v8::Value> ToV8Value(Environment* env, const BIOPointer& bio);
bool SafeX509SubjectAltNamePrint(const BIOPointer& out, X509_EXTENSION* ext);

v8::MaybeLocal<v8::Value> GetSubject(Environment* env,
                                     X509* cert,
                                     const BIOPointer& bio);

v8::MaybeLocal<v8::Value> GetIssuerString(Environment* env,
                                          X509* cert,
                                          const BIOPointer& bio);

v8::MaybeLocal<v8::Value> GetSubjectAltNameString(Environment* env,
                                                  X509* cert,
                                                  const BIOPointer& bio);

v8::MaybeLocal<v8::Value> GetInfoAccessString(Environment* env,
                                              X509* cert,
                                              const BIOPointer& bio);

}  // namespace crypto
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_CRYPTO_CRYPTO_COMMON_H_
