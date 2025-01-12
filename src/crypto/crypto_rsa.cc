#include "crypto/crypto_rsa.h"
#include "async_wrap-inl.h"
#include "base_object-inl.h"
#include "crypto/crypto_bio.h"
#include "crypto/crypto_keys.h"
#include "crypto/crypto_util.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "threadpoolwork-inl.h"
#include "v8.h"

#include <openssl/bn.h>
#include <openssl/rsa.h>

namespace node {

using ncrypto::BignumPointer;
using ncrypto::EVPKeyCtxPointer;
using ncrypto::EVPKeyPointer;
using ncrypto::RSAPointer;
using v8::ArrayBuffer;
using v8::BackingStore;
using v8::FunctionCallbackInfo;
using v8::Int32;
using v8::JustVoid;
using v8::Local;
using v8::Maybe;
using v8::Nothing;
using v8::Number;
using v8::Object;
using v8::String;
using v8::Uint32;
using v8::Value;

namespace crypto {
EVPKeyCtxPointer RsaKeyGenTraits::Setup(RsaKeyPairGenConfig* params) {
  EVPKeyCtxPointer ctx(
      EVP_PKEY_CTX_new_id(
          params->params.variant == kKeyVariantRSA_PSS
              ? EVP_PKEY_RSA_PSS
              : EVP_PKEY_RSA,
          nullptr));

  if (EVP_PKEY_keygen_init(ctx.get()) <= 0)
    return EVPKeyCtxPointer();

  if (EVP_PKEY_CTX_set_rsa_keygen_bits(
          ctx.get(),
          params->params.modulus_bits) <= 0) {
    return EVPKeyCtxPointer();
  }

  // 0x10001 is the default RSA exponent.
  if (params->params.exponent != 0x10001) {
    auto bn = BignumPointer::New();
    CHECK(bn.setWord(params->params.exponent));
    // EVP_CTX accepts ownership of bn on success.
    if (EVP_PKEY_CTX_set_rsa_keygen_pubexp(ctx.get(), bn.get()) <= 0)
      return EVPKeyCtxPointer();

    bn.release();
  }

  if (params->params.variant == kKeyVariantRSA_PSS) {
    if (params->params.md != nullptr &&
        EVP_PKEY_CTX_set_rsa_pss_keygen_md(ctx.get(), params->params.md) <= 0) {
      return EVPKeyCtxPointer();
    }

    // TODO(tniessen): This appears to only be necessary in OpenSSL 3, while
    // OpenSSL 1.1.1 behaves as recommended by RFC 8017 and defaults the MGF1
    // hash algorithm to the RSA-PSS hashAlgorithm. Remove this code if the
    // behavior of OpenSSL 3 changes.
    const EVP_MD* mgf1_md = params->params.mgf1_md;
    if (mgf1_md == nullptr && params->params.md != nullptr) {
      mgf1_md = params->params.md;
    }

    if (mgf1_md != nullptr &&
        EVP_PKEY_CTX_set_rsa_pss_keygen_mgf1_md(
            ctx.get(),
            mgf1_md) <= 0) {
      return EVPKeyCtxPointer();
    }

    int saltlen = params->params.saltlen;
    if (saltlen < 0 && params->params.md != nullptr) {
      saltlen = EVP_MD_size(params->params.md);
    }

    if (saltlen >= 0 &&
        EVP_PKEY_CTX_set_rsa_pss_keygen_saltlen(
            ctx.get(),
            saltlen) <= 0) {
      return EVPKeyCtxPointer();
    }
  }

  return ctx;
}

// Input parameters to the RsaKeyGenJob:
// For key variants RSA-OAEP and RSA-SSA-PKCS1-v1_5
//   1. CryptoJobMode
//   2. Key Variant
//   3. Modulus Bits
//   4. Public Exponent
//   5. Public Format
//   6. Public Type
//   7. Private Format
//   8. Private Type
//   9. Cipher
//   10. Passphrase
//
// For RSA-PSS variant
//   1. CryptoJobMode
//   2. Key Variant
//   3. Modulus Bits
//   4. Public Exponent
//   5. Digest
//   6. mgf1 Digest
//   7. Salt length
//   8. Public Format
//   9. Public Type
//   10. Private Format
//   11. Private Type
//   12. Cipher
//   13. Passphrase
Maybe<void> RsaKeyGenTraits::AdditionalConfig(
    CryptoJobMode mode,
    const FunctionCallbackInfo<Value>& args,
    unsigned int* offset,
    RsaKeyPairGenConfig* params) {
  Environment* env = Environment::GetCurrent(args);

  CHECK(args[*offset]->IsUint32());  // Variant
  CHECK(args[*offset + 1]->IsUint32());  // Modulus bits
  CHECK(args[*offset + 2]->IsUint32());  // Exponent

  params->params.variant =
      static_cast<RSAKeyVariant>(args[*offset].As<Uint32>()->Value());

  CHECK_IMPLIES(params->params.variant != kKeyVariantRSA_PSS,
                args.Length() == 10);
  CHECK_IMPLIES(params->params.variant == kKeyVariantRSA_PSS,
                args.Length() == 13);

  params->params.modulus_bits = args[*offset + 1].As<Uint32>()->Value();
  params->params.exponent = args[*offset + 2].As<Uint32>()->Value();

  *offset += 3;

  if (params->params.variant == kKeyVariantRSA_PSS) {
    if (!args[*offset]->IsUndefined()) {
      CHECK(args[*offset]->IsString());
      Utf8Value digest(env->isolate(), args[*offset]);
      params->params.md = EVP_get_digestbyname(*digest);
      if (params->params.md == nullptr) {
        THROW_ERR_CRYPTO_INVALID_DIGEST(env, "Invalid digest: %s", *digest);
        return Nothing<void>();
      }
    }

    if (!args[*offset + 1]->IsUndefined()) {
      CHECK(args[*offset + 1]->IsString());
      Utf8Value digest(env->isolate(), args[*offset + 1]);
      params->params.mgf1_md = EVP_get_digestbyname(*digest);
      if (params->params.mgf1_md == nullptr) {
        THROW_ERR_CRYPTO_INVALID_DIGEST(
            env, "Invalid MGF1 digest: %s", *digest);
        return Nothing<void>();
      }
    }

    if (!args[*offset + 2]->IsUndefined()) {
      CHECK(args[*offset + 2]->IsInt32());
      params->params.saltlen = args[*offset + 2].As<Int32>()->Value();
      if (params->params.saltlen < 0) {
        THROW_ERR_OUT_OF_RANGE(
          env,
          "salt length is out of range");
        return Nothing<void>();
      }
    }

    *offset += 3;
  }

  return JustVoid();
}

namespace {
WebCryptoKeyExportStatus RSA_JWK_Export(const KeyObjectData& key_data,
                                        const RSAKeyExportConfig& params,
                                        ByteSource* out) {
  return WebCryptoKeyExportStatus::FAILED;
}

template <PublicKeyCipher::EVP_PKEY_cipher_init_t init,
          PublicKeyCipher::EVP_PKEY_cipher_t cipher>
WebCryptoCipherStatus RSA_Cipher(Environment* env,
                                 const KeyObjectData& key_data,
                                 const RSACipherConfig& params,
                                 const ByteSource& in,
                                 ByteSource* out) {
  CHECK_NE(key_data.GetKeyType(), kKeyTypeSecret);
  Mutex::ScopedLock lock(key_data.mutex());
  const auto& m_pkey = key_data.GetAsymmetricKey();

  EVPKeyCtxPointer ctx = m_pkey.newCtx();

  if (!ctx || init(ctx.get()) <= 0)
    return WebCryptoCipherStatus::FAILED;

  if (EVP_PKEY_CTX_set_rsa_padding(ctx.get(), params.padding) <= 0) {
    return WebCryptoCipherStatus::FAILED;
  }

  if (params.digest != nullptr &&
      (EVP_PKEY_CTX_set_rsa_oaep_md(ctx.get(), params.digest) <= 0 ||
       EVP_PKEY_CTX_set_rsa_mgf1_md(ctx.get(), params.digest) <= 0)) {
    return WebCryptoCipherStatus::FAILED;
  }

  if (!SetRsaOaepLabel(ctx, params.label)) return WebCryptoCipherStatus::FAILED;

  size_t out_len = 0;
  if (cipher(
          ctx.get(),
          nullptr,
          &out_len,
          in.data<unsigned char>(),
          in.size()) <= 0) {
    return WebCryptoCipherStatus::FAILED;
  }

  ByteSource::Builder buf(out_len);

  if (cipher(ctx.get(),
             buf.data<unsigned char>(),
             &out_len,
             in.data<unsigned char>(),
             in.size()) <= 0) {
    return WebCryptoCipherStatus::FAILED;
  }

  *out = std::move(buf).release(out_len);
  return WebCryptoCipherStatus::OK;
}
}  // namespace

Maybe<void> RSAKeyExportTraits::AdditionalConfig(
    const FunctionCallbackInfo<Value>& args,
    unsigned int offset,
    RSAKeyExportConfig* params) {
  CHECK(args[offset]->IsUint32());  // RSAKeyVariant
  params->variant =
      static_cast<RSAKeyVariant>(args[offset].As<Uint32>()->Value());
  return JustVoid();
}

WebCryptoKeyExportStatus RSAKeyExportTraits::DoExport(
    const KeyObjectData& key_data,
    WebCryptoKeyFormat format,
    const RSAKeyExportConfig& params,
    ByteSource* out) {
  CHECK_NE(key_data.GetKeyType(), kKeyTypeSecret);

  switch (format) {
    case kWebCryptoKeyFormatRaw:
      // Not supported for RSA keys of either type
      return WebCryptoKeyExportStatus::FAILED;
    case kWebCryptoKeyFormatJWK:
      return RSA_JWK_Export(key_data, params, out);
    case kWebCryptoKeyFormatPKCS8:
      if (key_data.GetKeyType() != kKeyTypePrivate)
        return WebCryptoKeyExportStatus::INVALID_KEY_TYPE;
      return PKEY_PKCS8_Export(key_data, out);
    case kWebCryptoKeyFormatSPKI:
      if (key_data.GetKeyType() != kKeyTypePublic)
        return WebCryptoKeyExportStatus::INVALID_KEY_TYPE;
      return PKEY_SPKI_Export(key_data, out);
    default:
      UNREACHABLE();
  }
}

RSACipherConfig::RSACipherConfig(RSACipherConfig&& other) noexcept
    : mode(other.mode),
      label(std::move(other.label)),
      padding(other.padding),
      digest(other.digest) {}

void RSACipherConfig::MemoryInfo(MemoryTracker* tracker) const {
  if (mode == kCryptoJobAsync)
    tracker->TrackFieldWithSize("label", label.size());
}

Maybe<void> RSACipherTraits::AdditionalConfig(
    CryptoJobMode mode,
    const FunctionCallbackInfo<Value>& args,
    unsigned int offset,
    WebCryptoCipherMode cipher_mode,
    RSACipherConfig* params) {
  Environment* env = Environment::GetCurrent(args);

  params->mode = mode;
  params->padding = RSA_PKCS1_OAEP_PADDING;

  CHECK(args[offset]->IsUint32());
  RSAKeyVariant variant =
      static_cast<RSAKeyVariant>(args[offset].As<Uint32>()->Value());

  switch (variant) {
    case kKeyVariantRSA_OAEP: {
      CHECK(args[offset + 1]->IsString());  // digest
      Utf8Value digest(env->isolate(), args[offset + 1]);

      params->digest = EVP_get_digestbyname(*digest);
      if (params->digest == nullptr) {
        THROW_ERR_CRYPTO_INVALID_DIGEST(env, "Invalid digest: %s", *digest);
        return Nothing<void>();
      }

      if (IsAnyBufferSource(args[offset + 2])) {
        ArrayBufferOrViewContents<char> label(args[offset + 2]);
        if (!label.CheckSizeInt32()) [[unlikely]] {
          THROW_ERR_OUT_OF_RANGE(env, "label is too big");
          return Nothing<void>();
        }
        params->label = label.ToCopy();
      }
      break;
    }
    default:
      THROW_ERR_CRYPTO_INVALID_KEYTYPE(env);
      return Nothing<void>();
  }

  return JustVoid();
}

WebCryptoCipherStatus RSACipherTraits::DoCipher(Environment* env,
                                                const KeyObjectData& key_data,
                                                WebCryptoCipherMode cipher_mode,
                                                const RSACipherConfig& params,
                                                const ByteSource& in,
                                                ByteSource* out) {
  switch (cipher_mode) {
    case kWebCryptoCipherEncrypt:
      CHECK_EQ(key_data.GetKeyType(), kKeyTypePublic);
      return RSA_Cipher<EVP_PKEY_encrypt_init, EVP_PKEY_encrypt>(
          env, key_data, params, in, out);
    case kWebCryptoCipherDecrypt:
      CHECK_EQ(key_data.GetKeyType(), kKeyTypePrivate);
      return RSA_Cipher<EVP_PKEY_decrypt_init, EVP_PKEY_decrypt>(
          env, key_data, params, in, out);
  }
  return WebCryptoCipherStatus::FAILED;
}

Maybe<void> ExportJWKRsaKey(Environment* env,
                            const KeyObjectData& key,
                            Local<Object> target) {
  Mutex::ScopedLock lock(key.mutex());
  const auto& m_pkey = key.GetAsymmetricKey();
  int type = m_pkey.id();
  CHECK(type == EVP_PKEY_RSA || type == EVP_PKEY_RSA_PSS);

  // TODO(tniessen): Remove the "else" branch once we drop support for OpenSSL
  // versions older than 1.1.1e via FIPS / dynamic linking.
  const RSA* rsa;
  if (OpenSSL_version_num() >= 0x1010105fL) {
    rsa = EVP_PKEY_get0_RSA(m_pkey.get());
  } else {
    rsa = static_cast<const RSA*>(EVP_PKEY_get0(m_pkey.get()));
  }
  CHECK_NOT_NULL(rsa);

  const BIGNUM* n;
  const BIGNUM* e;
  const BIGNUM* d;
  const BIGNUM* p;
  const BIGNUM* q;
  const BIGNUM* dp;
  const BIGNUM* dq;
  const BIGNUM* qi;
  RSA_get0_key(rsa, &n, &e, &d);

  if (target->Set(
          env->context(),
          env->jwk_kty_string(),
          env->jwk_rsa_string()).IsNothing()) {
    return Nothing<void>();
  }

  if (SetEncodedValue(env, target, env->jwk_n_string(), n).IsNothing() ||
      SetEncodedValue(env, target, env->jwk_e_string(), e).IsNothing()) {
    return Nothing<void>();
  }

  if (key.GetKeyType() == kKeyTypePrivate) {
    RSA_get0_factors(rsa, &p, &q);
    RSA_get0_crt_params(rsa, &dp, &dq, &qi);
    if (SetEncodedValue(env, target, env->jwk_d_string(), d).IsNothing() ||
        SetEncodedValue(env, target, env->jwk_p_string(), p).IsNothing() ||
        SetEncodedValue(env, target, env->jwk_q_string(), q).IsNothing() ||
        SetEncodedValue(env, target, env->jwk_dp_string(), dp).IsNothing() ||
        SetEncodedValue(env, target, env->jwk_dq_string(), dq).IsNothing() ||
        SetEncodedValue(env, target, env->jwk_qi_string(), qi).IsNothing()) {
      return Nothing<void>();
    }
  }

  return JustVoid();
}

KeyObjectData ImportJWKRsaKey(Environment* env,
                              Local<Object> jwk,
                              const FunctionCallbackInfo<Value>& args,
                              unsigned int offset) {
  Local<Value> n_value;
  Local<Value> e_value;
  Local<Value> d_value;

  if (!jwk->Get(env->context(), env->jwk_n_string()).ToLocal(&n_value) ||
      !jwk->Get(env->context(), env->jwk_e_string()).ToLocal(&e_value) ||
      !jwk->Get(env->context(), env->jwk_d_string()).ToLocal(&d_value) ||
      !n_value->IsString() ||
      !e_value->IsString()) {
    THROW_ERR_CRYPTO_INVALID_JWK(env, "Invalid JWK RSA key");
    return {};
  }

  if (!d_value->IsUndefined() && !d_value->IsString()) {
    THROW_ERR_CRYPTO_INVALID_JWK(env, "Invalid JWK RSA key");
    return {};
  }

  KeyType type = d_value->IsString() ? kKeyTypePrivate : kKeyTypePublic;

  RSAPointer rsa(RSA_new());

  ByteSource n = ByteSource::FromEncodedString(env, n_value.As<String>());
  ByteSource e = ByteSource::FromEncodedString(env, e_value.As<String>());

  if (!RSA_set0_key(
          rsa.get(),
          n.ToBN().release(),
          e.ToBN().release(),
          nullptr)) {
    THROW_ERR_CRYPTO_INVALID_JWK(env, "Invalid JWK RSA key");
    return {};
  }

  if (type == kKeyTypePrivate) {
    Local<Value> p_value;
    Local<Value> q_value;
    Local<Value> dp_value;
    Local<Value> dq_value;
    Local<Value> qi_value;

    if (!jwk->Get(env->context(), env->jwk_p_string()).ToLocal(&p_value) ||
        !jwk->Get(env->context(), env->jwk_q_string()).ToLocal(&q_value) ||
        !jwk->Get(env->context(), env->jwk_dp_string()).ToLocal(&dp_value) ||
        !jwk->Get(env->context(), env->jwk_dq_string()).ToLocal(&dq_value) ||
        !jwk->Get(env->context(), env->jwk_qi_string()).ToLocal(&qi_value)) {
      THROW_ERR_CRYPTO_INVALID_JWK(env, "Invalid JWK RSA key");
      return {};
    }

    if (!p_value->IsString() ||
        !q_value->IsString() ||
        !dp_value->IsString() ||
        !dq_value->IsString() ||
        !qi_value->IsString()) {
      THROW_ERR_CRYPTO_INVALID_JWK(env, "Invalid JWK RSA key");
      return {};
    }

    ByteSource d = ByteSource::FromEncodedString(env, d_value.As<String>());
    ByteSource q = ByteSource::FromEncodedString(env, q_value.As<String>());
    ByteSource p = ByteSource::FromEncodedString(env, p_value.As<String>());
    ByteSource dp = ByteSource::FromEncodedString(env, dp_value.As<String>());
    ByteSource dq = ByteSource::FromEncodedString(env, dq_value.As<String>());
    ByteSource qi = ByteSource::FromEncodedString(env, qi_value.As<String>());

    if (!RSA_set0_key(rsa.get(), nullptr, nullptr, d.ToBN().release()) ||
        !RSA_set0_factors(rsa.get(), p.ToBN().release(), q.ToBN().release()) ||
        !RSA_set0_crt_params(
            rsa.get(),
            dp.ToBN().release(),
            dq.ToBN().release(),
            qi.ToBN().release())) {
      THROW_ERR_CRYPTO_INVALID_JWK(env, "Invalid JWK RSA key");
      return {};
    }
  }

  auto pkey = EVPKeyPointer::New();
  CHECK_EQ(EVP_PKEY_set1_RSA(pkey.get(), rsa.get()), 1);

  return KeyObjectData::CreateAsymmetric(type, std::move(pkey));
}

Maybe<void> GetRsaKeyDetail(Environment* env,
                            const KeyObjectData& key,
                            Local<Object> target) {
  const BIGNUM* e;  // Public Exponent
  const BIGNUM* n;  // Modulus

  Mutex::ScopedLock lock(key.mutex());
  const auto& m_pkey = key.GetAsymmetricKey();
  int type = m_pkey.id();
  CHECK(type == EVP_PKEY_RSA || type == EVP_PKEY_RSA_PSS);

  // TODO(tniessen): Remove the "else" branch once we drop support for OpenSSL
  // versions older than 1.1.1e via FIPS / dynamic linking.
  const RSA* rsa;
  if (OpenSSL_version_num() >= 0x1010105fL) {
    rsa = EVP_PKEY_get0_RSA(m_pkey.get());
  } else {
    rsa = static_cast<const RSA*>(EVP_PKEY_get0(m_pkey.get()));
  }
  CHECK_NOT_NULL(rsa);

  RSA_get0_key(rsa, &n, &e, nullptr);

  if (target
          ->Set(env->context(),
                env->modulus_length_string(),
                Number::New(env->isolate(),
                            static_cast<double>(BignumPointer::GetBitCount(n))))
          .IsNothing()) {
    return Nothing<void>();
  }

  std::unique_ptr<BackingStore> public_exponent;
  {
    NoArrayBufferZeroFillScope no_zero_fill_scope(env->isolate_data());
    public_exponent = ArrayBuffer::NewBackingStore(
        env->isolate(), BignumPointer::GetByteCount(e));
  }
  CHECK_EQ(BignumPointer::EncodePaddedInto(
               e,
               static_cast<unsigned char*>(public_exponent->Data()),
               public_exponent->ByteLength()),
           public_exponent->ByteLength());

  if (target
          ->Set(env->context(),
                env->public_exponent_string(),
                ArrayBuffer::New(env->isolate(), std::move(public_exponent)))
          .IsNothing()) {
    return Nothing<void>();
  }

  if (type == EVP_PKEY_RSA_PSS) {
    // Due to the way ASN.1 encoding works, default values are omitted when
    // encoding the data structure. However, there are also RSA-PSS keys for
    // which no parameters are set. In that case, the ASN.1 RSASSA-PSS-params
    // sequence will be missing entirely and RSA_get0_pss_params will return
    // nullptr. If parameters are present but all parameters are set to their
    // default values, an empty sequence will be stored in the ASN.1 structure.
    // In that case, RSA_get0_pss_params does not return nullptr but all fields
    // of the returned RSA_PSS_PARAMS will be set to nullptr.

    const RSA_PSS_PARAMS* params = RSA_get0_pss_params(rsa);
    if (params != nullptr) {
      int hash_nid = NID_sha1;
      int mgf_nid = NID_mgf1;
      int mgf1_hash_nid = NID_sha1;
      int64_t salt_length = 20;

      if (params->hashAlgorithm != nullptr) {
        const ASN1_OBJECT* hash_obj;
        X509_ALGOR_get0(&hash_obj, nullptr, nullptr, params->hashAlgorithm);
        hash_nid = OBJ_obj2nid(hash_obj);
      }

      if (target
              ->Set(
                  env->context(),
                  env->hash_algorithm_string(),
                  OneByteString(env->isolate(), OBJ_nid2ln(hash_nid)))
              .IsNothing()) {
        return Nothing<void>();
      }

      if (params->maskGenAlgorithm != nullptr) {
        const ASN1_OBJECT* mgf_obj;
        X509_ALGOR_get0(&mgf_obj, nullptr, nullptr, params->maskGenAlgorithm);
        mgf_nid = OBJ_obj2nid(mgf_obj);
        if (mgf_nid == NID_mgf1) {
          const ASN1_OBJECT* mgf1_hash_obj;
          X509_ALGOR_get0(&mgf1_hash_obj, nullptr, nullptr, params->maskHash);
          mgf1_hash_nid = OBJ_obj2nid(mgf1_hash_obj);
        }
      }

      // If, for some reason, the MGF is not MGF1, then the MGF1 hash function
      // is intentionally not added to the object.
      if (mgf_nid == NID_mgf1) {
        if (target
                ->Set(
                    env->context(),
                    env->mgf1_hash_algorithm_string(),
                    OneByteString(env->isolate(), OBJ_nid2ln(mgf1_hash_nid)))
                .IsNothing()) {
          return Nothing<void>();
        }
      }

      if (params->saltLength != nullptr) {
        if (ASN1_INTEGER_get_int64(&salt_length, params->saltLength) != 1) {
          ThrowCryptoError(env, ERR_get_error(), "ASN1_INTEGER_get_in64 error");
          return Nothing<void>();
        }
      }

      if (target
              ->Set(
                  env->context(),
                  env->salt_length_string(),
                  Number::New(env->isolate(), static_cast<double>(salt_length)))
              .IsNothing()) {
        return Nothing<void>();
      }
    }
  }

  return JustVoid();
}

namespace RSAAlg {
void Initialize(Environment* env, Local<Object> target) {
  RSAKeyPairGenJob::Initialize(env, target);
  RSAKeyExportJob::Initialize(env, target);
  RSACipherJob::Initialize(env, target);

  NODE_DEFINE_CONSTANT(target, kKeyVariantRSA_SSA_PKCS1_v1_5);
  NODE_DEFINE_CONSTANT(target, kKeyVariantRSA_PSS);
  NODE_DEFINE_CONSTANT(target, kKeyVariantRSA_OAEP);
}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  RSAKeyPairGenJob::RegisterExternalReferences(registry);
  RSAKeyExportJob::RegisterExternalReferences(registry);
  RSACipherJob::RegisterExternalReferences(registry);
}
}  // namespace RSAAlg
}  // namespace crypto
}  // namespace node
