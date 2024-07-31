#include "crypto/crypto_common.h"
#include "base_object-inl.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "nbytes.h"
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
using v8::Boolean;
using v8::Context;
using v8::EscapableHandleScope;
using v8::Integer;
using v8::Local;
using v8::MaybeLocal;
using v8::NewStringType;
using v8::Object;
using v8::String;
using v8::Undefined;
using v8::Value;

namespace crypto {
static constexpr int kX509NameFlagsMultiline =
    ASN1_STRFLGS_ESC_2253 |
    ASN1_STRFLGS_ESC_CTRL |
    ASN1_STRFLGS_UTF8_CONVERT |
    XN_FLAG_SEP_MULTILINE |
    XN_FLAG_FN_SN;

static constexpr int kX509NameFlagsRFC2253WithinUtf8JSON =
    XN_FLAG_RFC2253 &
    ~ASN1_STRFLGS_ESC_MSB &
    ~ASN1_STRFLGS_ESC_CTRL;

X509Pointer SSL_CTX_get_issuer(SSL_CTX* ctx, X509* cert) {
  X509_STORE* store = SSL_CTX_get_cert_store(ctx);
  DeleteFnPtr<X509_STORE_CTX, X509_STORE_CTX_free> store_ctx(
      X509_STORE_CTX_new());
  X509Pointer result;
  X509* issuer;
  if (store_ctx.get() != nullptr &&
      X509_STORE_CTX_init(store_ctx.get(), store, nullptr, nullptr) == 1 &&
      X509_STORE_CTX_get1_issuer(&issuer, store_ctx.get(), cert) == 1) {
    result.reset(issuer);
  }
  return result;
}

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
  if (X509* peer_cert = SSL_get_peer_certificate(ssl.get())) {
    X509_free(peer_cert);
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
  SSL_CTX* ctx = context->ctx().get();
  X509* x509 = SSL_CTX_get0_certificate(ctx);
  EVP_PKEY* pkey = SSL_CTX_get0_privatekey(ctx);
  STACK_OF(X509)* chain;

  int err = SSL_CTX_get0_chain_certs(ctx, &chain);
  if (err == 1) err = SSL_use_certificate(ssl.get(), x509);
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
  X509* cert = SSL_get_certificate(ssl.get());
  if (cert == nullptr)
    return Undefined(env->isolate());

  MaybeLocal<Object> maybe_cert = X509ToObject(env, cert);
  return maybe_cert.FromMaybe<Value>(Local<Value>());
}

Local<Value> ToV8Value(Environment* env, const BIOPointer& bio) {
  BUF_MEM* mem;
  BIO_get_mem_ptr(bio.get(), &mem);
  MaybeLocal<String> ret =
      String::NewFromUtf8(
          env->isolate(),
          mem->data,
          NewStringType::kNormal,
          mem->length);
  CHECK_EQ(BIO_reset(bio.get()), 1);
  return ret.FromMaybe(Local<Value>());
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

MaybeLocal<Object> AddIssuerChainToObject(
    X509Pointer* cert,
    Local<Object> object,
    StackOfX509&& peer_certs,
    Environment* const env) {
  Local<Context> context = env->isolate()->GetCurrentContext();
  cert->reset(sk_X509_delete(peer_certs.get(), 0));
  for (;;) {
    int i;
    for (i = 0; i < sk_X509_num(peer_certs.get()); i++) {
      X509* ca = sk_X509_value(peer_certs.get(), i);
      if (X509_check_issued(ca, cert->get()) != X509_V_OK)
        continue;

      Local<Object> ca_info;
      MaybeLocal<Object> maybe_ca_info = X509ToObject(env, ca);
      if (!maybe_ca_info.ToLocal(&ca_info))
        return MaybeLocal<Object>();

      if (!Set<Object>(context, object, env->issuercert_string(), ca_info))
        return MaybeLocal<Object>();
      object = ca_info;

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
  Local<Context> context = env->isolate()->GetCurrentContext();
  while (X509_check_issued(cert->get(), cert->get()) != X509_V_OK) {
    X509Pointer ca;
    if (!(ca = SSL_CTX_get_issuer(SSL_get_SSL_CTX(ssl.get()), cert->get())))
      break;

    Local<Object> ca_info;
    MaybeLocal<Object> maybe_ca_info = X509ToObject(env, ca.get());
    if (!maybe_ca_info.ToLocal(&ca_info))
      return MaybeLocal<Object>();

    if (!Set<Object>(context, issuer_chain, env->issuercert_string(), ca_info))
      return MaybeLocal<Object>();
    issuer_chain = ca_info;

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

void AddFingerprintDigest(
    const unsigned char* md,
    unsigned int md_size,
    char fingerprint[3 * EVP_MAX_MD_SIZE]) {
  unsigned int i;
  const char hex[] = "0123456789ABCDEF";

  for (i = 0; i < md_size; i++) {
    fingerprint[3*i] = hex[(md[i] & 0xf0) >> 4];
    fingerprint[(3*i)+1] = hex[(md[i] & 0x0f)];
    fingerprint[(3*i)+2] = ':';
  }

  DCHECK_GT(md_size, 0);
  fingerprint[(3 * (md_size - 1)) + 2] = '\0';
}

template <const char* (*nid2string)(int nid)>
MaybeLocal<Value> GetCurveName(Environment* env, const int nid) {
  const char* name = nid2string(nid);
  return name != nullptr ?
      MaybeLocal<Value>(OneByteString(env->isolate(), name)) :
      MaybeLocal<Value>(Undefined(env->isolate()));
}

MaybeLocal<Value> GetECPubKey(Environment* env,
                              const EC_GROUP* group,
                              OSSL3_CONST EC_KEY* ec) {
  const EC_POINT* pubkey = EC_KEY_get0_public_key(ec);
  if (pubkey == nullptr)
    return Undefined(env->isolate());

  return ECPointToBuffer(env, group, pubkey, EC_KEY_get_conv_form(ec), nullptr)
      .FromMaybe(Local<Object>());
}

MaybeLocal<Value> GetECGroupBits(Environment* env, const EC_GROUP* group) {
  if (group == nullptr)
    return Undefined(env->isolate());

  int bits = EC_GROUP_order_bits(group);
  if (bits <= 0)
    return Undefined(env->isolate());

  return Integer::New(env->isolate(), bits);
}

MaybeLocal<Object> GetPubKey(Environment* env, OSSL3_CONST RSA* rsa) {
  int size = i2d_RSA_PUBKEY(rsa, nullptr);
  CHECK_GE(size, 0);

  std::unique_ptr<BackingStore> bs;
  {
    NoArrayBufferZeroFillScope no_zero_fill_scope(env->isolate_data());
    bs = ArrayBuffer::NewBackingStore(env->isolate(), size);
  }

  unsigned char* serialized = reinterpret_cast<unsigned char*>(bs->Data());
  CHECK_GE(i2d_RSA_PUBKEY(rsa, &serialized), 0);

  Local<ArrayBuffer> ab = ArrayBuffer::New(env->isolate(), std::move(bs));
  return Buffer::New(env, ab, 0, ab->ByteLength()).FromMaybe(Local<Object>());
}

MaybeLocal<Value> GetExponentString(
    Environment* env,
    const BIOPointer& bio,
    const BIGNUM* e) {
  uint64_t exponent_word = static_cast<uint64_t>(BN_get_word(e));
  BIO_printf(bio.get(), "0x%" PRIx64, exponent_word);
  return ToV8Value(env, bio);
}

Local<Value> GetBits(Environment* env, const BIGNUM* n) {
  return Integer::New(env->isolate(), BN_num_bits(n));
}

MaybeLocal<Value> GetModulusString(
    Environment* env,
    const BIOPointer& bio,
    const BIGNUM* n) {
  BN_print(bio.get(), n);
  return ToV8Value(env, bio);
}
}  // namespace

MaybeLocal<Value> GetRawDERCertificate(Environment* env, X509* cert) {
  int size = i2d_X509(cert, nullptr);

  std::unique_ptr<BackingStore> bs;
  {
    NoArrayBufferZeroFillScope no_zero_fill_scope(env->isolate_data());
    bs = ArrayBuffer::NewBackingStore(env->isolate(), size);
  }

  unsigned char* serialized = reinterpret_cast<unsigned char*>(bs->Data());
  CHECK_GE(i2d_X509(cert, &serialized), 0);

  Local<ArrayBuffer> ab = ArrayBuffer::New(env->isolate(), std::move(bs));
  return Buffer::New(env, ab, 0, ab->ByteLength()).FromMaybe(Local<Object>());
}

MaybeLocal<Value> GetSerialNumber(Environment* env, X509* cert) {
  if (ASN1_INTEGER* serial_number = X509_get_serialNumber(cert)) {
    BignumPointer bn(ASN1_INTEGER_to_BN(serial_number, nullptr));
    if (bn) {
      char* data = BN_bn2hex(bn.get());
      ByteSource buf = ByteSource::Allocated(data, strlen(data));
      if (buf) return OneByteString(env->isolate(), buf.data<unsigned char>());
    }
  }

  return Undefined(env->isolate());
}

MaybeLocal<Value> GetKeyUsage(Environment* env, X509* cert) {
  StackOfASN1 eku(static_cast<STACK_OF(ASN1_OBJECT)*>(
      X509_get_ext_d2i(cert, NID_ext_key_usage, nullptr, nullptr)));
  if (eku) {
    const int count = sk_ASN1_OBJECT_num(eku.get());
    MaybeStackBuffer<Local<Value>, 16> ext_key_usage(count);
    char buf[256];

    int j = 0;
    for (int i = 0; i < count; i++) {
      if (OBJ_obj2txt(buf,
                      sizeof(buf),
                      sk_ASN1_OBJECT_value(eku.get(), i), 1) >= 0) {
        ext_key_usage[j++] = OneByteString(env->isolate(), buf);
      }
    }

    return Array::New(env->isolate(), ext_key_usage.out(), count);
  }

  return Undefined(env->isolate());
}

MaybeLocal<Value> GetCurrentCipherName(Environment* env,
                                       const SSLPointer& ssl) {
  return GetCipherName(env, SSL_get_current_cipher(ssl.get()));
}

MaybeLocal<Value> GetCurrentCipherVersion(Environment* env,
                                          const SSLPointer& ssl) {
  return GetCipherVersion(env, SSL_get_current_cipher(ssl.get()));
}

MaybeLocal<Value> GetFingerprintDigest(
    Environment* env,
    const EVP_MD* method,
    X509* cert) {
  unsigned char md[EVP_MAX_MD_SIZE];
  unsigned int md_size;
  char fingerprint[EVP_MAX_MD_SIZE * 3];

  if (X509_digest(cert, method, md, &md_size)) {
    AddFingerprintDigest(md, md_size, fingerprint);
    return OneByteString(env->isolate(), fingerprint);
  }
  return Undefined(env->isolate());
}

MaybeLocal<Value> GetValidTo(
    Environment* env,
    X509* cert,
    const BIOPointer& bio) {
  ASN1_TIME_print(bio.get(), X509_get0_notAfter(cert));
  return ToV8Value(env, bio);
}

MaybeLocal<Value> GetValidFrom(
    Environment* env,
    X509* cert,
    const BIOPointer& bio) {
  ASN1_TIME_print(bio.get(), X509_get0_notBefore(cert));
  return ToV8Value(env, bio);
}

static inline bool IsSafeAltName(const char* name, size_t length, bool utf8) {
  for (size_t i = 0; i < length; i++) {
    char c = name[i];
    switch (c) {
    case '"':
    case '\\':
      // These mess with encoding rules.
      // Fall through.
    case ',':
      // Commas make it impossible to split the list of subject alternative
      // names unambiguously, which is why we have to escape.
      // Fall through.
    case '\'':
      // Single quotes are unlikely to appear in any legitimate values, but they
      // could be used to make a value look like it was escaped (i.e., enclosed
      // in single/double quotes).
      return false;
    default:
      if (utf8) {
        // In UTF8 strings, we require escaping for any ASCII control character,
        // but NOT for non-ASCII characters. Note that all bytes of any code
        // point that consists of more than a single byte have their MSB set.
        if (static_cast<unsigned char>(c) < ' ' || c == '\x7f') {
          return false;
        }
      } else {
        // Check if the char is a control character or non-ASCII character. Note
        // that char may or may not be a signed type. Regardless, non-ASCII
        // values will always be outside of this range.
        if (c < ' ' || c > '~') {
          return false;
        }
      }
    }
  }
  return true;
}

static inline void PrintAltName(const BIOPointer& out, const char* name,
                                size_t length, bool utf8,
                                const char* safe_prefix) {
  if (IsSafeAltName(name, length, utf8)) {
    // For backward-compatibility, append "safe" names without any
    // modifications.
    if (safe_prefix != nullptr) {
      BIO_printf(out.get(), "%s:", safe_prefix);
    }
    BIO_write(out.get(), name, length);
  } else {
    // If a name is not "safe", we cannot embed it without special
    // encoding. This does not usually happen, but we don't want to hide
    // it from the user either. We use JSON compatible escaping here.
    BIO_write(out.get(), "\"", 1);
    if (safe_prefix != nullptr) {
      BIO_printf(out.get(), "%s:", safe_prefix);
    }
    for (size_t j = 0; j < length; j++) {
      char c = static_cast<char>(name[j]);
      if (c == '\\') {
        BIO_write(out.get(), "\\\\", 2);
      } else if (c == '"') {
        BIO_write(out.get(), "\\\"", 2);
      } else if ((c >= ' ' && c != ',' && c <= '~') || (utf8 && (c & 0x80))) {
        // Note that the above condition explicitly excludes commas, which means
        // that those are encoded as Unicode escape sequences in the "else"
        // block. That is not strictly necessary, and Node.js itself would parse
        // it correctly either way. We only do this to account for third-party
        // code that might be splitting the string at commas (as Node.js itself
        // used to do).
        BIO_write(out.get(), &c, 1);
      } else {
        // Control character or non-ASCII character. We treat everything as
        // Latin-1, which corresponds to the first 255 Unicode code points.
        const char hex[] = "0123456789abcdef";
        char u[] = { '\\', 'u', '0', '0', hex[(c & 0xf0) >> 4], hex[c & 0x0f] };
        BIO_write(out.get(), u, sizeof(u));
      }
    }
    BIO_write(out.get(), "\"", 1);
  }
}

static inline void PrintLatin1AltName(const BIOPointer& out,
                                      const ASN1_IA5STRING* name,
                                      const char* safe_prefix = nullptr) {
  PrintAltName(out, reinterpret_cast<const char*>(name->data), name->length,
               false, safe_prefix);
}

static inline void PrintUtf8AltName(const BIOPointer& out,
                                    const ASN1_UTF8STRING* name,
                                    const char* safe_prefix = nullptr) {
  PrintAltName(out, reinterpret_cast<const char*>(name->data), name->length,
               true, safe_prefix);
}

// This function emulates the behavior of i2v_GENERAL_NAME in a safer and less
// ambiguous way. "othername:" entries use the GENERAL_NAME_print format.
static bool PrintGeneralName(const BIOPointer& out, const GENERAL_NAME* gen) {
  if (gen->type == GEN_DNS) {
    ASN1_IA5STRING* name = gen->d.dNSName;
    BIO_write(out.get(), "DNS:", 4);
    // Note that the preferred name syntax (see RFCs 5280 and 1034) with
    // wildcards is a subset of what we consider "safe", so spec-compliant DNS
    // names will never need to be escaped.
    PrintLatin1AltName(out, name);
  } else if (gen->type == GEN_EMAIL) {
    ASN1_IA5STRING* name = gen->d.rfc822Name;
    BIO_write(out.get(), "email:", 6);
    PrintLatin1AltName(out, name);
  } else if (gen->type == GEN_URI) {
    ASN1_IA5STRING* name = gen->d.uniformResourceIdentifier;
    BIO_write(out.get(), "URI:", 4);
    // The set of "safe" names was designed to include just about any URI,
    // with a few exceptions, most notably URIs that contains commas (see
    // RFC 2396). In other words, most legitimate URIs will not require
    // escaping.
    PrintLatin1AltName(out, name);
  } else if (gen->type == GEN_DIRNAME) {
    // Earlier versions of Node.js used X509_NAME_oneline to print the X509_NAME
    // object. The format was non standard and should be avoided. The use of
    // X509_NAME_oneline is discouraged by OpenSSL but was required for backward
    // compatibility. Conveniently, X509_NAME_oneline produced ASCII and the
    // output was unlikely to contains commas or other characters that would
    // require escaping. However, it SHOULD NOT produce ASCII output since an
    // RFC5280 AttributeValue may be a UTF8String.
    // Newer versions of Node.js have since switched to X509_NAME_print_ex to
    // produce a better format at the cost of backward compatibility. The new
    // format may contain Unicode characters and it is likely to contain commas,
    // which require escaping. Fortunately, the recently safeguarded function
    // PrintAltName handles all of that safely.
    BIO_printf(out.get(), "DirName:");
    BIOPointer tmp(BIO_new(BIO_s_mem()));
    CHECK(tmp);
    if (X509_NAME_print_ex(tmp.get(),
                           gen->d.dirn,
                           0,
                           kX509NameFlagsRFC2253WithinUtf8JSON) < 0) {
      return false;
    }
    char* oline = nullptr;
    long n_bytes = BIO_get_mem_data(tmp.get(), &oline);  // NOLINT(runtime/int)
    CHECK_GE(n_bytes, 0);
    CHECK_IMPLIES(n_bytes != 0, oline != nullptr);
    PrintAltName(out, oline, static_cast<size_t>(n_bytes), true, nullptr);
  } else if (gen->type == GEN_IPADD) {
    BIO_printf(out.get(), "IP Address:");
    const ASN1_OCTET_STRING* ip = gen->d.ip;
    const unsigned char* b = ip->data;
    if (ip->length == 4) {
      BIO_printf(out.get(), "%d.%d.%d.%d", b[0], b[1], b[2], b[3]);
    } else if (ip->length == 16) {
      for (unsigned int j = 0; j < 8; j++) {
        uint16_t pair = (b[2 * j] << 8) | b[2 * j + 1];
        BIO_printf(out.get(), (j == 0) ? "%X" : ":%X", pair);
      }
    } else {
#if OPENSSL_VERSION_MAJOR >= 3
      BIO_printf(out.get(), "<invalid length=%d>", ip->length);
#else
      BIO_printf(out.get(), "<invalid>");
#endif
    }
  } else if (gen->type == GEN_RID) {
    // Unlike OpenSSL's default implementation, never print the OID as text and
    // instead always print its numeric representation.
    char oline[256];
    OBJ_obj2txt(oline, sizeof(oline), gen->d.rid, true);
    BIO_printf(out.get(), "Registered ID:%s", oline);
  } else if (gen->type == GEN_OTHERNAME) {
    // The format that is used here is based on OpenSSL's implementation of
    // GENERAL_NAME_print (as of OpenSSL 3.0.1). Earlier versions of Node.js
    // instead produced the same format as i2v_GENERAL_NAME, which was somewhat
    // awkward, especially when passed to translatePeerCertificate.
    bool unicode = true;
    const char* prefix = nullptr;
    // OpenSSL 1.1.1 does not support othername in GENERAL_NAME_print and may
    // not define these NIDs.
#if OPENSSL_VERSION_MAJOR >= 3
    int nid = OBJ_obj2nid(gen->d.otherName->type_id);
    switch (nid) {
      case NID_id_on_SmtpUTF8Mailbox:
        prefix = "SmtpUTF8Mailbox";
        break;
      case NID_XmppAddr:
        prefix = "XmppAddr";
        break;
      case NID_SRVName:
        prefix = "SRVName";
        unicode = false;
        break;
      case NID_ms_upn:
        prefix = "UPN";
        break;
      case NID_NAIRealm:
        prefix = "NAIRealm";
        break;
    }
#endif  // OPENSSL_VERSION_MAJOR >= 3
    int val_type = gen->d.otherName->value->type;
    if (prefix == nullptr ||
        (unicode && val_type != V_ASN1_UTF8STRING) ||
        (!unicode && val_type != V_ASN1_IA5STRING)) {
      BIO_printf(out.get(), "othername:<unsupported>");
    } else {
      BIO_printf(out.get(), "othername:");
      if (unicode) {
        PrintUtf8AltName(out, gen->d.otherName->value->value.utf8string,
                         prefix);
      } else {
        PrintLatin1AltName(out, gen->d.otherName->value->value.ia5string,
                           prefix);
      }
    }
  } else if (gen->type == GEN_X400) {
    // TODO(tniessen): this is what OpenSSL does, implement properly instead
    BIO_printf(out.get(), "X400Name:<unsupported>");
  } else if (gen->type == GEN_EDIPARTY) {
    // TODO(tniessen): this is what OpenSSL does, implement properly instead
    BIO_printf(out.get(), "EdiPartyName:<unsupported>");
  } else {
    // This is safe because X509V3_EXT_d2i would have returned nullptr in this
    // case already.
    UNREACHABLE();
  }

  return true;
}

bool SafeX509SubjectAltNamePrint(const BIOPointer& out, X509_EXTENSION* ext) {
  CHECK_EQ(OBJ_obj2nid(X509_EXTENSION_get_object(ext)), NID_subject_alt_name);

  GENERAL_NAMES* names = static_cast<GENERAL_NAMES*>(X509V3_EXT_d2i(ext));
  if (names == nullptr)
    return false;

  bool ok = true;

  for (int i = 0; i < sk_GENERAL_NAME_num(names); i++) {
    GENERAL_NAME* gen = sk_GENERAL_NAME_value(names, i);

    if (i != 0)
      BIO_write(out.get(), ", ", 2);

    if (!(ok = PrintGeneralName(out, gen))) {
      break;
    }
  }
  sk_GENERAL_NAME_pop_free(names, GENERAL_NAME_free);

  return ok;
}

bool SafeX509InfoAccessPrint(const BIOPointer& out, X509_EXTENSION* ext) {
  CHECK_EQ(OBJ_obj2nid(X509_EXTENSION_get_object(ext)), NID_info_access);

  AUTHORITY_INFO_ACCESS* descs =
      static_cast<AUTHORITY_INFO_ACCESS*>(X509V3_EXT_d2i(ext));
  if (descs == nullptr)
    return false;

  bool ok = true;

  for (int i = 0; i < sk_ACCESS_DESCRIPTION_num(descs); i++) {
    ACCESS_DESCRIPTION* desc = sk_ACCESS_DESCRIPTION_value(descs, i);

    if (i != 0)
      BIO_write(out.get(), "\n", 1);

    char objtmp[80];
    i2t_ASN1_OBJECT(objtmp, sizeof(objtmp), desc->method);
    BIO_printf(out.get(), "%s - ", objtmp);
    if (!(ok = PrintGeneralName(out, desc->location))) {
      break;
    }
  }
  sk_ACCESS_DESCRIPTION_pop_free(descs, ACCESS_DESCRIPTION_free);

#if OPENSSL_VERSION_MAJOR < 3
  BIO_write(out.get(), "\n", 1);
#endif

  return ok;
}

v8::MaybeLocal<v8::Value> GetSubjectAltNameString(Environment* env,
                                                  X509* cert,
                                                  const BIOPointer& bio) {
  int index = X509_get_ext_by_NID(cert, NID_subject_alt_name, -1);
  if (index < 0)
    return Undefined(env->isolate());

  X509_EXTENSION* ext = X509_get_ext(cert, index);
  CHECK_NOT_NULL(ext);

  if (!SafeX509SubjectAltNamePrint(bio, ext)) {
    CHECK_EQ(BIO_reset(bio.get()), 1);
    return v8::Null(env->isolate());
  }

  return ToV8Value(env, bio);
}

v8::MaybeLocal<v8::Value> GetInfoAccessString(Environment* env,
                                              X509* cert,
                                              const BIOPointer& bio) {
  int index = X509_get_ext_by_NID(cert, NID_info_access, -1);
  if (index < 0)
    return Undefined(env->isolate());

  X509_EXTENSION* ext = X509_get_ext(cert, index);
  CHECK_NOT_NULL(ext);

  if (!SafeX509InfoAccessPrint(bio, ext)) {
    CHECK_EQ(BIO_reset(bio.get()), 1);
    return v8::Null(env->isolate());
  }

  return ToV8Value(env, bio);
}

MaybeLocal<Value> GetIssuerString(Environment* env,
                                  X509* cert,
                                  const BIOPointer& bio) {
  X509_NAME* issuer_name = X509_get_issuer_name(cert);
  if (X509_NAME_print_ex(
          bio.get(),
          issuer_name,
          0,
          kX509NameFlagsMultiline) <= 0) {
    CHECK_EQ(BIO_reset(bio.get()), 1);
    return Undefined(env->isolate());
  }

  return ToV8Value(env, bio);
}

MaybeLocal<Value> GetSubject(Environment* env,
                             X509* cert,
                             const BIOPointer& bio) {
  if (X509_NAME_print_ex(
          bio.get(),
          X509_get_subject_name(cert),
          0,
          kX509NameFlagsMultiline) <= 0) {
    CHECK_EQ(BIO_reset(bio.get()), 1);
    return Undefined(env->isolate());
  }

  return ToV8Value(env, bio);
}

template <X509_NAME* get_name(const X509*)>
static MaybeLocal<Value> GetX509NameObject(Environment* env, X509* cert) {
  X509_NAME* name = get_name(cert);
  CHECK_NOT_NULL(name);

  int cnt = X509_NAME_entry_count(name);
  CHECK_GE(cnt, 0);

  Local<Object> result =
      Object::New(env->isolate(), Null(env->isolate()), nullptr, nullptr, 0);
  if (result.IsEmpty()) {
    return MaybeLocal<Value>();
  }

  for (int i = 0; i < cnt; i++) {
    X509_NAME_ENTRY* entry = X509_NAME_get_entry(name, i);
    CHECK_NOT_NULL(entry);

    // We intentionally ignore the value of X509_NAME_ENTRY_set because the
    // representation as an object does not allow grouping entries into sets
    // anyway, and multi-value RDNs are rare, i.e., the vast majority of
    // Relative Distinguished Names contains a single type-value pair only.
    const ASN1_OBJECT* type = X509_NAME_ENTRY_get_object(entry);
    const ASN1_STRING* value = X509_NAME_ENTRY_get_data(entry);

    // If OpenSSL knows the type, use the short name of the type as the key, and
    // the numeric representation of the type's OID otherwise.
    int type_nid = OBJ_obj2nid(type);
    char type_buf[80];
    const char* type_str;
    if (type_nid != NID_undef) {
      type_str = OBJ_nid2sn(type_nid);
      CHECK_NOT_NULL(type_str);
    } else {
      OBJ_obj2txt(type_buf, sizeof(type_buf), type, true);
      type_str = type_buf;
    }

    Local<String> v8_name;
    if (!String::NewFromUtf8(env->isolate(), type_str).ToLocal(&v8_name)) {
      return MaybeLocal<Value>();
    }

    // The previous implementation used X509_NAME_print_ex, which escapes some
    // characters in the value. The old implementation did not decode/unescape
    // values correctly though, leading to ambiguous and incorrect
    // representations. The new implementation only converts to Unicode and does
    // not escape anything.
    unsigned char* value_str;
    int value_str_size = ASN1_STRING_to_UTF8(&value_str, value);
    if (value_str_size < 0) {
      return Undefined(env->isolate());
    }
    auto free_value_str = OnScopeLeave([&]() { OPENSSL_free(value_str); });

    Local<String> v8_value;
    if (!String::NewFromUtf8(env->isolate(),
                             reinterpret_cast<const char*>(value_str),
                             NewStringType::kNormal,
                             value_str_size)
             .ToLocal(&v8_value)) {
      return MaybeLocal<Value>();
    }

    // For backward compatibility, we only create arrays if multiple values
    // exist for the same key. That is not great but there is not much we can
    // change here without breaking things. Note that this creates nested data
    // structures, yet still does not allow representing Distinguished Names
    // accurately.
    bool multiple;
    if (!result->HasOwnProperty(env->context(), v8_name).To(&multiple)) {
      return MaybeLocal<Value>();
    } else if (multiple) {
      Local<Value> accum;
      if (!result->Get(env->context(), v8_name).ToLocal(&accum)) {
        return MaybeLocal<Value>();
      }
      if (!accum->IsArray()) {
        accum = Array::New(env->isolate(), &accum, 1);
        if (result->Set(env->context(), v8_name, accum).IsNothing()) {
          return MaybeLocal<Value>();
        }
      }
      Local<Array> array = accum.As<Array>();
      if (array->Set(env->context(), array->Length(), v8_value).IsNothing()) {
        return MaybeLocal<Value>();
      }
    } else if (result->Set(env->context(), v8_name, v8_value).IsNothing()) {
      return MaybeLocal<Value>();
    }
  }

  return result;
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
  Local<Object> result;
  MaybeLocal<Object> maybe_cert;

  // NOTE: This is because of the odd OpenSSL behavior. On client `cert_chain`
  // contains the `peer_certificate`, but on server it doesn't.
  X509Pointer cert(is_server ? SSL_get_peer_certificate(ssl.get()) : nullptr);
  STACK_OF(X509)* ssl_certs = SSL_get_peer_cert_chain(ssl.get());
  if (!cert && (ssl_certs == nullptr || sk_X509_num(ssl_certs) == 0))
    return Undefined(env->isolate());

  // Short result requested.
  if (abbreviated) {
    maybe_cert =
        X509ToObject(env, cert ? cert.get() : sk_X509_value(ssl_certs, 0));
    return maybe_cert.ToLocal(&result) ? result : MaybeLocal<Value>();
  }

  StackOfX509 peer_certs = CloneSSLCerts(std::move(cert), ssl_certs);
  if (peer_certs == nullptr)
    return Undefined(env->isolate());

  // First and main certificate.
  X509Pointer first_cert(sk_X509_value(peer_certs.get(), 0));
  CHECK(first_cert);
  maybe_cert = X509ToObject(env, first_cert.release());
  if (!maybe_cert.ToLocal(&result))
    return MaybeLocal<Value>();

  Local<Object> issuer_chain;
  MaybeLocal<Object> maybe_issuer_chain;

  maybe_issuer_chain =
      AddIssuerChainToObject(
          &cert,
          result,
          std::move(peer_certs),
          env);
  if (!maybe_issuer_chain.ToLocal(&issuer_chain))
    return MaybeLocal<Value>();

  maybe_issuer_chain =
      GetLastIssuedCert(
          &cert,
          ssl,
          issuer_chain,
          env);

  issuer_chain.Clear();
  if (!maybe_issuer_chain.ToLocal(&issuer_chain))
    return MaybeLocal<Value>();

  // Last certificate should be self-signed.
  if (X509_check_issued(cert.get(), cert.get()) == X509_V_OK &&
      !Set<Object>(env->context(),
           issuer_chain,
           env->issuercert_string(),
           issuer_chain)) {
    return MaybeLocal<Value>();
  }

  return result;
}

MaybeLocal<Object> X509ToObject(
    Environment* env,
    X509* cert) {
  EscapableHandleScope scope(env->isolate());
  Local<Context> context = env->context();
  Local<Object> info = Object::New(env->isolate());

  BIOPointer bio(BIO_new(BIO_s_mem()));
  CHECK(bio);

  // X509_check_ca() returns a range of values. Only 1 means "is a CA"
  auto is_ca = Boolean::New(env->isolate(), 1 == X509_check_ca(cert));
  if (!Set<Value>(context,
                  info,
                  env->subject_string(),
                  GetX509NameObject<X509_get_subject_name>(env, cert)) ||
      !Set<Value>(context,
                  info,
                  env->issuer_string(),
                  GetX509NameObject<X509_get_issuer_name>(env, cert)) ||
      !Set<Value>(context,
                  info,
                  env->subjectaltname_string(),
                  GetSubjectAltNameString(env, cert, bio)) ||
      !Set<Value>(context,
                  info,
                  env->infoaccess_string(),
                  GetInfoAccessString(env, cert, bio)) ||
      !Set<Boolean>(context, info, env->ca_string(), is_ca)) {
    return MaybeLocal<Object>();
  }

  OSSL3_CONST EVP_PKEY* pkey = X509_get0_pubkey(cert);
  OSSL3_CONST RSA* rsa = nullptr;
  OSSL3_CONST EC_KEY* ec = nullptr;
  if (pkey != nullptr) {
    switch (EVP_PKEY_id(pkey)) {
      case EVP_PKEY_RSA:
        rsa = EVP_PKEY_get0_RSA(pkey);
        break;
      case EVP_PKEY_EC:
        ec = EVP_PKEY_get0_EC_KEY(pkey);
        break;
    }
  }

  if (rsa) {
    const BIGNUM* n;
    const BIGNUM* e;
    RSA_get0_key(rsa, &n, &e, nullptr);
    if (!Set<Value>(context,
                    info,
                    env->modulus_string(),
                    GetModulusString(env, bio, n)) ||
        !Set<Value>(context, info, env->bits_string(), GetBits(env, n)) ||
        !Set<Value>(context,
                    info,
                    env->exponent_string(),
                    GetExponentString(env, bio, e)) ||
        !Set<Object>(context,
                     info,
                     env->pubkey_string(),
                     GetPubKey(env, rsa))) {
      return MaybeLocal<Object>();
    }
  } else if (ec) {
    const EC_GROUP* group = EC_KEY_get0_group(ec);

    if (!Set<Value>(
            context, info, env->bits_string(), GetECGroupBits(env, group)) ||
        !Set<Value>(
            context, info, env->pubkey_string(), GetECPubKey(env, group, ec))) {
      return MaybeLocal<Object>();
    }

    const int nid = EC_GROUP_get_curve_name(group);
    if (nid != 0) {
      // Curve is well-known, get its OID and NIST nick-name (if it has one).

      if (!Set<Value>(context,
                      info,
                      env->asn1curve_string(),
                      GetCurveName<OBJ_nid2sn>(env, nid)) ||
          !Set<Value>(context,
                      info,
                      env->nistcurve_string(),
                      GetCurveName<EC_curve_nid2nist>(env, nid))) {
        return MaybeLocal<Object>();
      }
    } else {
      // Unnamed curves can be described by their mathematical properties,
      // but aren't used much (at all?) with X.509/TLS. Support later if needed.
    }
  }

  if (!Set<Value>(context,
                  info,
                  env->valid_from_string(),
                  GetValidFrom(env, cert, bio)) ||
      !Set<Value>(context,
                  info,
                  env->valid_to_string(),
                  GetValidTo(env, cert, bio))) {
    return MaybeLocal<Object>();
  }

  // bio is no longer needed
  bio.reset();

  if (!Set<Value>(context,
                  info,
                  env->fingerprint_string(),
                  GetFingerprintDigest(env, EVP_sha1(), cert)) ||
      !Set<Value>(context,
                  info,
                  env->fingerprint256_string(),
                  GetFingerprintDigest(env, EVP_sha256(), cert)) ||
      !Set<Value>(context,
                  info,
                  env->fingerprint512_string(),
                  GetFingerprintDigest(env, EVP_sha512(), cert)) ||
      !Set<Value>(
          context, info, env->ext_key_usage_string(), GetKeyUsage(env, cert)) ||
      !Set<Value>(context,
                  info,
                  env->serial_number_string(),
                  GetSerialNumber(env, cert)) ||
      !Set<Value>(
          context, info, env->raw_string(), GetRawDERCertificate(env, cert))) {
    return MaybeLocal<Object>();
  }

  return scope.Escape(info);
}

}  // namespace crypto
}  // namespace node
