#ifndef SRC_CRYPTO_CRYPTO_COMMON_H_
#define SRC_CRYPTO_CRYPTO_COMMON_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include "ncrypto.h"
#include "node_crypto.h"
#include "v8.h"

#include <string>

namespace node {
namespace crypto {

ncrypto::SSLSessionPointer GetTLSSession(const unsigned char* buf,
                                         size_t length);

long VerifyPeerCertificate(  // NOLINT(runtime/int)
    const ncrypto::SSLPointer& ssl,
    long def = X509_V_ERR_UNSPECIFIED);  // NOLINT(runtime/int)

bool UseSNIContext(const ncrypto::SSLPointer& ssl,
                   BaseObjectPtr<SecureContext> context);

bool SetGroups(SecureContext* sc, const char* groups);

v8::MaybeLocal<v8::Value> GetValidationErrorReason(Environment* env, int err);

v8::MaybeLocal<v8::Value> GetValidationErrorCode(Environment* env, int err);

v8::MaybeLocal<v8::Value> GetCert(Environment* env,
                                  const ncrypto::SSLPointer& ssl);

v8::MaybeLocal<v8::Object> GetCipherInfo(Environment* env,
                                         const ncrypto::SSLPointer& ssl);

v8::MaybeLocal<v8::Object> GetEphemeralKey(Environment* env,
                                           const ncrypto::SSLPointer& ssl);

v8::MaybeLocal<v8::Value> GetPeerCert(Environment* env,
                                      const ncrypto::SSLPointer& ssl,
                                      bool abbreviated = false,
                                      bool is_server = false);

v8::MaybeLocal<v8::Object> ECPointToBuffer(
    Environment* env,
    const EC_GROUP* group,
    const EC_POINT* point,
    point_conversion_form_t form,
    const char** error);

v8::MaybeLocal<v8::Value> GetCurrentCipherName(Environment* env,
                                               const ncrypto::SSLPointer& ssl);
v8::MaybeLocal<v8::Value> GetCurrentCipherVersion(
    Environment* env, const ncrypto::SSLPointer& ssl);

}  // namespace crypto
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_CRYPTO_CRYPTO_COMMON_H_
