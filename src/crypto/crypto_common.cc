#include "crypto/crypto_common.h"
#include "base_object-inl.h"
#include "crypto/crypto_util.h"
#include "crypto/crypto_x509.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "nbytes.h"
#include "ncrypto.h"
#include "node.h"
#include "node_buffer.h"
#include "node_crypto.h"
#include "node_internals.h"
#include "string_bytes.h"
#include "v8.h"

#include <openssl/ec.h>
#include <openssl/ecdh.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/pkcs12.h>

#include <string>
#include <unordered_map>

namespace node {

using v8::Array;
using v8::ArrayBuffer;
using v8::BackingStore;
using v8::Context;
using v8::EscapableHandleScope;
using v8::Integer;
using v8::Local;
using v8::MaybeLocal;
using v8::Object;
using v8::String;
using v8::Undefined;
using v8::Value;

namespace crypto {
void LogSecret(
    const SSLPointer& ssl,
    const char* name,
    const unsigned char* secret,
    size_t secretlen) {
  auto keylog_cb = SSL_CTX_get_keylog_callback(SSL_get_SSL_CTX(ssl.get()));
  // All supported versions of TLS/SSL fix the client random to the same size.
  constexpr size_t kTlsClientRandomSize = SSL3_RANDOM_SIZE;
  unsigned char crandom[kTlsClientRandomSize];

  if (keylog_cb == nullptr ||
      SSL_get_client_random(ssl.get(), crandom, kTlsClientRandomSize) !=
          kTlsClientRandomSize) {
    return;
  }

  std::string line = name;
  line += " " + nbytes::HexEncode(reinterpret_cast<const char*>(crandom),
                                  kTlsClientRandomSize);
  line +=
      " " + nbytes::HexEncode(reinterpret_cast<const char*>(secret), secretlen);
  keylog_cb(ssl.get(), line.c_str());
}

MaybeLocal<Value> GetSSLOCSPResponse(
    Environment* env,
    SSL* ssl,
    Local<Value> default_value) {
  const unsigned char* resp;
  int len = SSL_get_tlsext_status_ocsp_resp(ssl, &resp);
  if (resp == nullptr)
    return default_value;

  Local<Value> ret;
  MaybeLocal<Object> maybe_buffer =
      Buffer::Copy(env, reinterpret_cast<const char*>(resp), len);

  if (!maybe_buffer.ToLocal(&ret))
    return MaybeLocal<Value>();

  return ret;
}

bool SetTLSSession(
    const SSLPointer& ssl,
    const SSLSessionPointer& session) {
  return session != nullptr && SSL_set_session(ssl.get(), session.get()) == 1;
}

SSLSessionPointer GetTLSSession(const unsigned char* buf, size_t length) {
  return SSLSessionPointer(d2i_SSL_SESSION(nullptr, &buf, length));
}

long VerifyPeerCertificate(  // NOLINT(runtime/int)
    const SSLPointer& ssl,
    long def) {  // NOLINT(runtime/int)
  long err = def;  // NOLINT(runtime/int)
  if (X509Pointer::PeerFrom(ssl)) {
    err = SSL_get_verify_result(ssl.get());
  } else {
    const SSL_CIPHER* curr_cipher = SSL_get_current_cipher(ssl.get());
    const SSL_SESSION* sess = SSL_get_session(ssl.get());
    // Allow no-cert for PSK authentication in TLS1.2 and lower.
    // In TLS1.3 check that session was reused because TLS1.3 PSK
    // looks like session resumption.
    if (SSL_CIPHER_get_auth_nid(curr_cipher) == NID_auth_psk ||
        (SSL_SESSION_get_protocol_version(sess) == TLS1_3_VERSION &&
         SSL_session_reused(ssl.get()))) {
      return X509_V_OK;
    }
  }
  return err;
}

bool UseSNIContext(
    const SSLPointer& ssl, BaseObjectPtr<SecureContext> context) {
  auto x509 = ncrypto::X509View::From(context->ctx());
  if (!x509) return false;
  SSL_CTX* ctx = context->ctx().get();
  EVP_PKEY* pkey = SSL_CTX_get0_privatekey(ctx);
  STACK_OF(X509)* chain;

  int err = SSL_CTX_get0_chain_certs(ctx, &chain);
  if (err == 1) err = SSL_use_certificate(ssl.get(), x509.get());
  if (err == 1) err = SSL_use_PrivateKey(ssl.get(), pkey);
  if (err == 1 && chain != nullptr) err = SSL_set1_chain(ssl.get(), chain);
  return err == 1;
}

const char* GetClientHelloALPN(const SSLPointer& ssl) {
  const unsigned char* buf;
  size_t len;
  size_t rem;

  if (!SSL_client_hello_get0_ext(
          ssl.get(),
          TLSEXT_TYPE_application_layer_protocol_negotiation,
          &buf,
          &rem) ||
      rem < 2) {
    return nullptr;
  }

  len = (buf[0] << 8) | buf[1];
  if (len + 2 != rem) return nullptr;
  return reinterpret_cast<const char*>(buf + 3);
}

const char* GetClientHelloServerName(const SSLPointer& ssl) {
  const unsigned char* buf;
  size_t len;
  size_t rem;

  if (!SSL_client_hello_get0_ext(
          ssl.get(),
          TLSEXT_TYPE_server_name,
          &buf,
          &rem) || rem <= 2) {
    return nullptr;
  }

  len = (*buf << 8) | *(buf + 1);
  if (len + 2 != rem)
    return nullptr;
  rem = len;

  if (rem == 0 || *(buf + 2) != TLSEXT_NAMETYPE_host_name) return nullptr;
  rem--;
  if (rem <= 2)
    return nullptr;
  len = (*(buf + 3) << 8) | *(buf + 4);
  if (len + 2 > rem)
    return nullptr;
  return reinterpret_cast<const char*>(buf + 5);
}

const char* GetServerName(SSL* ssl) {
  return SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
}

bool SetGroups(SecureContext* sc, const char* groups) {
  return SSL_CTX_set1_groups_list(sc->ctx().get(), groups) == 1;
}

// When adding or removing errors below, please also update the list in the API
// documentation. See the "OpenSSL Error Codes" section of doc/api/errors.md
const char* X509ErrorCode(long err) {  // NOLINT(runtime/int)
  const char* code = "UNSPECIFIED";
#define CASE_X509_ERR(CODE) case X509_V_ERR_##CODE: code = #CODE; break;
  switch (err) {
    // if you modify anything in here, *please* update the respective section in
    // doc/api/tls.md as well
    CASE_X509_ERR(UNABLE_TO_GET_ISSUER_CERT)
    CASE_X509_ERR(UNABLE_TO_GET_CRL)
    CASE_X509_ERR(UNABLE_TO_DECRYPT_CERT_SIGNATURE)
    CASE_X509_ERR(UNABLE_TO_DECRYPT_CRL_SIGNATURE)
    CASE_X509_ERR(UNABLE_TO_DECODE_ISSUER_PUBLIC_KEY)
    CASE_X509_ERR(CERT_SIGNATURE_FAILURE)
    CASE_X509_ERR(CRL_SIGNATURE_FAILURE)
    CASE_X509_ERR(CERT_NOT_YET_VALID)
    CASE_X509_ERR(CERT_HAS_EXPIRED)
    CASE_X509_ERR(CRL_NOT_YET_VALID)
    CASE_X509_ERR(CRL_HAS_EXPIRED)
    CASE_X509_ERR(ERROR_IN_CERT_NOT_BEFORE_FIELD)
    CASE_X509_ERR(ERROR_IN_CERT_NOT_AFTER_FIELD)
    CASE_X509_ERR(ERROR_IN_CRL_LAST_UPDATE_FIELD)
    CASE_X509_ERR(ERROR_IN_CRL_NEXT_UPDATE_FIELD)
    CASE_X509_ERR(OUT_OF_MEM)
    CASE_X509_ERR(DEPTH_ZERO_SELF_SIGNED_CERT)
    CASE_X509_ERR(SELF_SIGNED_CERT_IN_CHAIN)
    CASE_X509_ERR(UNABLE_TO_GET_ISSUER_CERT_LOCALLY)
    CASE_X509_ERR(UNABLE_TO_VERIFY_LEAF_SIGNATURE)
    CASE_X509_ERR(CERT_CHAIN_TOO_LONG)
    CASE_X509_ERR(CERT_REVOKED)
    CASE_X509_ERR(INVALID_CA)
    CASE_X509_ERR(PATH_LENGTH_EXCEEDED)
    CASE_X509_ERR(INVALID_PURPOSE)
    CASE_X509_ERR(CERT_UNTRUSTED)
    CASE_X509_ERR(CERT_REJECTED)
    CASE_X509_ERR(HOSTNAME_MISMATCH)
  }
#undef CASE_X509_ERR
  return code;
}

MaybeLocal<Value> GetValidationErrorReason(Environment* env, int err) {
  if (err == 0)
    return Undefined(env->isolate());
  const char* reason = X509_verify_cert_error_string(err);
  return OneByteString(env->isolate(), reason);
}

MaybeLocal<Value> GetValidationErrorCode(Environment* env, int err) {
  if (err == 0)
    return Undefined(env->isolate());
  return OneByteString(env->isolate(), X509ErrorCode(err));
}

MaybeLocal<Value> GetCert(Environment* env, const SSLPointer& ssl) {
  ClearErrorOnReturn clear_error_on_return;
  ncrypto::X509View cert(SSL_get_certificate(ssl.get()));
  if (!cert) return Undefined(env->isolate());
  return X509Certificate::toObject(env, cert);
}

namespace {
template <typename T>
bool Set(
    Local<Context> context,
    Local<Object> target,
    Local<Value> name,
    MaybeLocal<T> maybe_value) {
  Local<Value> value;
  if (!maybe_value.ToLocal(&value))
    return false;

  // Undefined is ignored, but still considered successful
  if (value->IsUndefined())
    return true;

  return !target->Set(context, name, value).IsNothing();
}

template <const char* (*getstr)(const SSL_CIPHER* cipher)>
MaybeLocal<Value> GetCipherValue(Environment* env, const SSL_CIPHER* cipher) {
  if (cipher == nullptr)
    return Undefined(env->isolate());

  return OneByteString(env->isolate(), getstr(cipher));
}

constexpr auto GetCipherName = GetCipherValue<SSL_CIPHER_get_name>;
constexpr auto GetCipherStandardName = GetCipherValue<SSL_CIPHER_standard_name>;
constexpr auto GetCipherVersion = GetCipherValue<SSL_CIPHER_get_version>;

StackOfX509 CloneSSLCerts(X509Pointer&& cert,
                          const STACK_OF(X509)* const ssl_certs) {
  StackOfX509 peer_certs(sk_X509_new(nullptr));
  if (!peer_certs) return StackOfX509();
  if (cert && !sk_X509_push(peer_certs.get(), cert.release()))
    return StackOfX509();
  for (int i = 0; i < sk_X509_num(ssl_certs); i++) {
    X509Pointer cert(X509_dup(sk_X509_value(ssl_certs, i)));
    if (!cert || !sk_X509_push(peer_certs.get(), cert.get()))
      return StackOfX509();
    // `cert` is now managed by the stack.
    cert.release();
  }
  return peer_certs;
}

MaybeLocal<Object> AddIssuerChainToObject(X509Pointer* cert,
                                          Local<Object> object,
                                          StackOfX509&& peer_certs,
                                          Environment* const env) {
  cert->reset(sk_X509_delete(peer_certs.get(), 0));
  for (;;) {
    int i;
    for (i = 0; i < sk_X509_num(peer_certs.get()); i++) {
      ncrypto::X509View ca(sk_X509_value(peer_certs.get(), i));
      if (!cert->view().isIssuedBy(ca)) continue;

      Local<Value> ca_info;
      if (!X509Certificate::toObject(env, ca).ToLocal(&ca_info)) return {};
      CHECK(ca_info->IsObject());

      if (!Set<Object>(env->context(),
                       object,
                       env->issuercert_string(),
                       ca_info.As<Object>())) {
        return {};
      }
      object = ca_info.As<Object>();

      // NOTE: Intentionally freeing cert that is not used anymore.
      // Delete cert and continue aggregating issuers.
      cert->reset(sk_X509_delete(peer_certs.get(), i));
      break;
    }

    // Issuer not found, break out of the loop.
    if (i == sk_X509_num(peer_certs.get()))
      break;
  }
  return MaybeLocal<Object>(object);
}

MaybeLocal<Object> GetLastIssuedCert(
    X509Pointer* cert,
    const SSLPointer& ssl,
    Local<Object> issuer_chain,
    Environment* const env) {
  Local<Value> ca_info;
  while (!cert->view().isIssuedBy(cert->view())) {
    auto ca = X509Pointer::IssuerFrom(ssl, cert->view());
    if (!ca) break;

    if (!X509Certificate::toObject(env, ca.view()).ToLocal(&ca_info)) return {};

    CHECK(ca_info->IsObject());

    if (!Set<Object>(env->context(),
                     issuer_chain,
                     env->issuercert_string(),
                     ca_info.As<Object>())) {
      return {};
    }
    issuer_chain = ca_info.As<Object>();

    // For self-signed certificates whose keyUsage field does not include
    // keyCertSign, X509_check_issued() will return false. Avoid going into an
    // infinite loop by checking if SSL_CTX_get_issuer() returned the same
    // certificate.
    if (cert->get() == ca.get()) break;

    // Delete previous cert and continue aggregating issuers.
    *cert = std::move(ca);
  }
  return MaybeLocal<Object>(issuer_chain);
}

}  // namespace

MaybeLocal<Value> GetCurrentCipherName(Environment* env,
                                       const SSLPointer& ssl) {
  return GetCipherName(env, SSL_get_current_cipher(ssl.get()));
}

MaybeLocal<Value> GetCurrentCipherVersion(Environment* env,
                                          const SSLPointer& ssl) {
  return GetCipherVersion(env, SSL_get_current_cipher(ssl.get()));
}

template <MaybeLocal<Value> (*Get)(Environment* env, const SSL_CIPHER* cipher)>
MaybeLocal<Value> GetCurrentCipherValue(Environment* env,
                                        const SSLPointer& ssl) {
  return Get(env, SSL_get_current_cipher(ssl.get()));
}

MaybeLocal<Array> GetClientHelloCiphers(
    Environment* env,
    const SSLPointer& ssl) {
  EscapableHandleScope scope(env->isolate());
  const unsigned char* buf;
  size_t len = SSL_client_hello_get0_ciphers(ssl.get(), &buf);
  size_t count = len / 2;
  MaybeStackBuffer<Local<Value>, 16> ciphers(count);
  int j = 0;
  for (size_t n = 0; n < len; n += 2) {
    const SSL_CIPHER* cipher = SSL_CIPHER_find(ssl.get(), buf);
    buf += 2;
    Local<Object> obj = Object::New(env->isolate());
    if (!Set(env->context(),
             obj,
             env->name_string(),
             GetCipherName(env, cipher)) ||
        !Set(env->context(),
             obj,
             env->standard_name_string(),
             GetCipherStandardName(env, cipher)) ||
        !Set(env->context(),
             obj,
             env->version_string(),
             GetCipherVersion(env, cipher))) {
      return MaybeLocal<Array>();
    }
    ciphers[j++] = obj;
  }
  Local<Array> ret = Array::New(env->isolate(), ciphers.out(), count);
  return scope.Escape(ret);
}


MaybeLocal<Object> GetCipherInfo(Environment* env, const SSLPointer& ssl) {
  if (SSL_get_current_cipher(ssl.get()) == nullptr)
    return MaybeLocal<Object>();
  EscapableHandleScope scope(env->isolate());
  Local<Object> info = Object::New(env->isolate());

  if (!Set<Value>(env->context(),
                  info,
                  env->name_string(),
                  GetCurrentCipherValue<GetCipherName>(env, ssl)) ||
      !Set<Value>(env->context(),
                  info,
                  env->standard_name_string(),
                  GetCurrentCipherValue<GetCipherStandardName>(env, ssl)) ||
      !Set<Value>(env->context(),
                  info,
                  env->version_string(),
                  GetCurrentCipherValue<GetCipherVersion>(env, ssl))) {
    return MaybeLocal<Object>();
  }

  return scope.Escape(info);
}

MaybeLocal<Object> GetEphemeralKey(Environment* env, const SSLPointer& ssl) {
  CHECK_EQ(SSL_is_server(ssl.get()), 0);
  EVP_PKEY* raw_key;

  EscapableHandleScope scope(env->isolate());
  Local<Object> info = Object::New(env->isolate());
  if (!SSL_get_peer_tmp_key(ssl.get(), &raw_key)) return scope.Escape(info);

  Local<Context> context = env->context();
  crypto::EVPKeyPointer key(raw_key);

  int kid = EVP_PKEY_id(key.get());
  int bits = EVP_PKEY_bits(key.get());
  switch (kid) {
    case EVP_PKEY_DH:
      if (!Set<String>(context, info, env->type_string(), env->dh_string()) ||
          !Set<Integer>(context,
               info,
               env->size_string(),
               Integer::New(env->isolate(), bits))) {
        return MaybeLocal<Object>();
      }
      break;
    case EVP_PKEY_EC:
    case EVP_PKEY_X25519:
    case EVP_PKEY_X448:
      {
        const char* curve_name;
        if (kid == EVP_PKEY_EC) {
          OSSL3_CONST EC_KEY* ec = EVP_PKEY_get0_EC_KEY(key.get());
          int nid = EC_GROUP_get_curve_name(EC_KEY_get0_group(ec));
          curve_name = OBJ_nid2sn(nid);
        } else {
          curve_name = OBJ_nid2sn(kid);
        }
        if (!Set<String>(context,
                         info,
                         env->type_string(),
                         env->ecdh_string()) ||
            !Set<String>(context,
                info,
                env->name_string(),
                OneByteString(env->isolate(), curve_name)) ||
            !Set<Integer>(context,
                 info,
                 env->size_string(),
                 Integer::New(env->isolate(), bits))) {
          return MaybeLocal<Object>();
        }
      }
      break;
  }

  return scope.Escape(info);
}

MaybeLocal<Object> ECPointToBuffer(Environment* env,
                                   const EC_GROUP* group,
                                   const EC_POINT* point,
                                   point_conversion_form_t form,
                                   const char** error) {
  size_t len = EC_POINT_point2oct(group, point, form, nullptr, 0, nullptr);
  if (len == 0) {
    if (error != nullptr) *error = "Failed to get public key length";
    return MaybeLocal<Object>();
  }

  std::unique_ptr<BackingStore> bs;
  {
    NoArrayBufferZeroFillScope no_zero_fill_scope(env->isolate_data());
    bs = ArrayBuffer::NewBackingStore(env->isolate(), len);
  }

  len = EC_POINT_point2oct(group,
                           point,
                           form,
                           reinterpret_cast<unsigned char*>(bs->Data()),
                           bs->ByteLength(),
                           nullptr);
  if (len == 0) {
    if (error != nullptr) *error = "Failed to get public key";
    return MaybeLocal<Object>();
  }

  Local<ArrayBuffer> ab = ArrayBuffer::New(env->isolate(), std::move(bs));
  return Buffer::New(env, ab, 0, ab->ByteLength()).FromMaybe(Local<Object>());
}

MaybeLocal<Value> GetPeerCert(
    Environment* env,
    const SSLPointer& ssl,
    bool abbreviated,
    bool is_server) {
  ClearErrorOnReturn clear_error_on_return;

  // NOTE: This is because of the odd OpenSSL behavior. On client `cert_chain`
  // contains the `peer_certificate`, but on server it doesn't.
  X509Pointer cert(is_server ? SSL_get_peer_certificate(ssl.get()) : nullptr);
  STACK_OF(X509)* ssl_certs = SSL_get_peer_cert_chain(ssl.get());
  if (!cert && (ssl_certs == nullptr || sk_X509_num(ssl_certs) == 0))
    return Undefined(env->isolate());

  // Short result requested.
  if (abbreviated) {
    if (cert) {
      return X509Certificate::toObject(env, cert.view());
    }
    return X509Certificate::toObject(
        env, ncrypto::X509View(sk_X509_value(ssl_certs, 0)));
  }

  StackOfX509 peer_certs = CloneSSLCerts(std::move(cert), ssl_certs);
  if (peer_certs == nullptr)
    return Undefined(env->isolate());

  // First and main certificate.
  Local<Value> result;
  ncrypto::X509View first_cert(sk_X509_value(peer_certs.get(), 0));
  CHECK(first_cert);
  if (!X509Certificate::toObject(env, first_cert).ToLocal(&result)) return {};
  CHECK(result->IsObject());

  Local<Object> issuer_chain;
  MaybeLocal<Object> maybe_issuer_chain;

  maybe_issuer_chain = AddIssuerChainToObject(
      &cert, result.As<Object>(), std::move(peer_certs), env);
  if (!maybe_issuer_chain.ToLocal(&issuer_chain)) return {};

  maybe_issuer_chain =
      GetLastIssuedCert(
          &cert,
          ssl,
          issuer_chain,
          env);

  issuer_chain.Clear();
  if (!maybe_issuer_chain.ToLocal(&issuer_chain)) return {};

  // Last certificate should be self-signed.
  if (cert.view().isIssuedBy(cert.view()) &&
      !Set<Object>(env->context(),
                   issuer_chain,
                   env->issuercert_string(),
                   issuer_chain)) {
    return {};
  }

  return result;
}

}  // namespace crypto
}  // namespace node
