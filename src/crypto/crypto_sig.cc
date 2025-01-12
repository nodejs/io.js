#include "crypto/crypto_sig.h"
#include "async_wrap-inl.h"
#include "base_object-inl.h"
#include "crypto/crypto_ec.h"
#include "crypto/crypto_keys.h"
#include "crypto/crypto_util.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "openssl/ec.h"
#include "threadpoolwork-inl.h"
#include "v8.h"

namespace node {

using ncrypto::BignumPointer;
using ncrypto::ClearErrorOnReturn;
using ncrypto::ECDSASigPointer;
using ncrypto::ECKeyPointer;
using ncrypto::EVPKeyCtxPointer;
using ncrypto::EVPKeyPointer;
using ncrypto::EVPMDCtxPointer;
using v8::ArrayBuffer;
using v8::BackingStore;
using v8::Boolean;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Int32;
using v8::Isolate;
using v8::Just;
using v8::JustVoid;
using v8::Local;
using v8::Maybe;
using v8::MaybeLocal;
using v8::Nothing;
using v8::Object;
using v8::Uint32;
using v8::Value;

namespace crypto {
namespace {
bool ValidateDSAParameters(EVP_PKEY* key) {
  /* Validate DSA2 parameters from FIPS 186-4 */
  auto id = EVPKeyPointer::base_id(key);
#if OPENSSL_VERSION_MAJOR >= 3
  if (EVP_default_properties_is_fips_enabled(nullptr) && EVP_PKEY_DSA == id) {
#else
  if (FIPS_mode() && EVP_PKEY_DSA == id) {
#endif
    const DSA* dsa = EVP_PKEY_get0_DSA(key);
    const BIGNUM* p;
    const BIGNUM* q;
    DSA_get0_pqg(dsa, &p, &q, nullptr);
    int L = BignumPointer::GetBitCount(p);
    int N = BignumPointer::GetBitCount(q);

    return (L == 1024 && N == 160) ||
           (L == 2048 && N == 224) ||
           (L == 2048 && N == 256) ||
           (L == 3072 && N == 256);
  }

  return true;
}

bool ApplyRSAOptions(const EVPKeyPointer& pkey,
                     EVP_PKEY_CTX* pkctx,
                     int padding,
                     const Maybe<int>& salt_len) {
  int id = pkey.id();
  if (id == EVP_PKEY_RSA || id == EVP_PKEY_RSA2 || id == EVP_PKEY_RSA_PSS) {
    if (EVP_PKEY_CTX_set_rsa_padding(pkctx, padding) <= 0)
      return false;
    if (padding == RSA_PKCS1_PSS_PADDING && salt_len.IsJust()) {
      if (EVP_PKEY_CTX_set_rsa_pss_saltlen(pkctx, salt_len.FromJust()) <= 0)
        return false;
    }
  }

  return true;
}

std::unique_ptr<BackingStore> Node_SignFinal(Environment* env,
                                             EVPMDCtxPointer&& mdctx,
                                             const EVPKeyPointer& pkey,
                                             int padding,
                                             Maybe<int> pss_salt_len) {
  unsigned char m[EVP_MAX_MD_SIZE];
  unsigned int m_len;

  if (!EVP_DigestFinal_ex(mdctx.get(), m, &m_len))
    return nullptr;

  size_t sig_len = pkey.size();
  std::unique_ptr<BackingStore> sig;
  {
    NoArrayBufferZeroFillScope no_zero_fill_scope(env->isolate_data());
    sig = ArrayBuffer::NewBackingStore(env->isolate(), sig_len);
  }
  EVPKeyCtxPointer pkctx = pkey.newCtx();
  if (pkctx && EVP_PKEY_sign_init(pkctx.get()) > 0 &&
      ApplyRSAOptions(pkey, pkctx.get(), padding, pss_salt_len) &&
      EVP_PKEY_CTX_set_signature_md(pkctx.get(), EVP_MD_CTX_md(mdctx.get())) >
          0 &&
      EVP_PKEY_sign(pkctx.get(),
                    static_cast<unsigned char*>(sig->Data()),
                    &sig_len,
                    m,
                    m_len) > 0) {
    CHECK_LE(sig_len, sig->ByteLength());
    if (sig_len == 0) {
      sig = ArrayBuffer::NewBackingStore(env->isolate(), 0);
    } else if (sig_len != sig->ByteLength()) {
      std::unique_ptr<BackingStore> old_sig = std::move(sig);
      sig = ArrayBuffer::NewBackingStore(env->isolate(), sig_len);
      memcpy(static_cast<char*>(sig->Data()),
             static_cast<char*>(old_sig->Data()),
             sig_len);
    }
    return sig;
  }

  return nullptr;
}

int GetDefaultSignPadding(const EVPKeyPointer& m_pkey) {
  return m_pkey.id() == EVP_PKEY_RSA_PSS ? RSA_PKCS1_PSS_PADDING
                                         : RSA_PKCS1_PADDING;
}

unsigned int GetBytesOfRS(const EVPKeyPointer& pkey) {
  int bits, base_id = pkey.base_id();

  if (base_id == EVP_PKEY_DSA) {
    const DSA* dsa_key = EVP_PKEY_get0_DSA(pkey.get());
    // Both r and s are computed mod q, so their width is limited by that of q.
    bits = BignumPointer::GetBitCount(DSA_get0_q(dsa_key));
  } else if (base_id == EVP_PKEY_EC) {
    bits = EC_GROUP_order_bits(ECKeyPointer::GetGroup(pkey));
  } else {
    return kNoDsaSignature;
  }

  return (bits + 7) / 8;
}

bool ExtractP1363(
    const unsigned char* sig_data,
    unsigned char* out,
    size_t len,
    size_t n) {
  ncrypto::Buffer<const unsigned char> sig_buffer{
      .data = sig_data,
      .len = len,
  };
  auto asn1_sig = ECDSASigPointer::Parse(sig_buffer);
  if (!asn1_sig)
    return false;

  return BignumPointer::EncodePaddedInto(asn1_sig.r(), out, n) > 0 &&
         BignumPointer::EncodePaddedInto(asn1_sig.s(), out + n, n) > 0;
}

// Returns the maximum size of each of the integers (r, s) of the DSA signature.
std::unique_ptr<BackingStore> ConvertSignatureToP1363(
    Environment* env,
    const EVPKeyPointer& pkey,
    std::unique_ptr<BackingStore>&& signature) {
  unsigned int n = GetBytesOfRS(pkey);
  if (n == kNoDsaSignature)
    return std::move(signature);

  std::unique_ptr<BackingStore> buf;
  {
    NoArrayBufferZeroFillScope no_zero_fill_scope(env->isolate_data());
    buf = ArrayBuffer::NewBackingStore(env->isolate(), 2 * n);
  }
  if (!ExtractP1363(static_cast<unsigned char*>(signature->Data()),
                    static_cast<unsigned char*>(buf->Data()),
                    signature->ByteLength(), n))
    return std::move(signature);

  return buf;
}

// Returns the maximum size of each of the integers (r, s) of the DSA signature.
ByteSource ConvertSignatureToP1363(Environment* env,
                                   const EVPKeyPointer& pkey,
                                   const ByteSource& signature) {
  unsigned int n = GetBytesOfRS(pkey);
  if (n == kNoDsaSignature)
    return ByteSource();

  const unsigned char* sig_data = signature.data<unsigned char>();

  ByteSource::Builder out(n * 2);
  memset(out.data<void>(), 0, n * 2);

  if (!ExtractP1363(sig_data, out.data<unsigned char>(), signature.size(), n))
    return ByteSource();

  return std::move(out).release();
}

ByteSource ConvertSignatureToDER(const EVPKeyPointer& pkey, ByteSource&& out) {
  unsigned int n = GetBytesOfRS(pkey);
  if (n == kNoDsaSignature)
    return std::move(out);

  const unsigned char* sig_data = out.data<unsigned char>();

  if (out.size() != 2 * n)
    return ByteSource();

  auto asn1_sig = ECDSASigPointer::New();
  CHECK(asn1_sig);
  BignumPointer r(sig_data, n);
  CHECK(r);
  BignumPointer s(sig_data + n, n);
  CHECK(s);
  CHECK(asn1_sig.setParams(std::move(r), std::move(s)));

  auto buf = asn1_sig.encode();
  if (buf.len <= 0) return ByteSource();

  CHECK_NOT_NULL(buf.data);
  return ByteSource::Allocated(buf);
}

void CheckThrow(Environment* env, SignBase::Error error) {
  HandleScope scope(env->isolate());

  switch (error) {
    case SignBase::Error::kSignUnknownDigest:
      return THROW_ERR_CRYPTO_INVALID_DIGEST(env);

    case SignBase::Error::kSignNotInitialised:
      return THROW_ERR_CRYPTO_INVALID_STATE(env, "Not initialised");

    case SignBase::Error::kSignMalformedSignature:
      return THROW_ERR_CRYPTO_OPERATION_FAILED(env, "Malformed signature");

    case SignBase::Error::kSignInit:
    case SignBase::Error::kSignUpdate:
    case SignBase::Error::kSignPrivateKey:
    case SignBase::Error::kSignPublicKey:
      {
        unsigned long err = ERR_get_error();  // NOLINT(runtime/int)
        if (err)
          return ThrowCryptoError(env, err);
        switch (error) {
          case SignBase::Error::kSignInit:
            return THROW_ERR_CRYPTO_OPERATION_FAILED(env,
                "EVP_SignInit_ex failed");
          case SignBase::Error::kSignUpdate:
            return THROW_ERR_CRYPTO_OPERATION_FAILED(env,
                "EVP_SignUpdate failed");
          case SignBase::Error::kSignPrivateKey:
            return THROW_ERR_CRYPTO_OPERATION_FAILED(env,
                "PEM_read_bio_PrivateKey failed");
          case SignBase::Error::kSignPublicKey:
            return THROW_ERR_CRYPTO_OPERATION_FAILED(env,
                "PEM_read_bio_PUBKEY failed");
          default:
            ABORT();
        }
      }

    case SignBase::Error::kSignOk:
      return;
  }
}

bool IsOneShot(const EVPKeyPointer& key) {
  return key.id() == EVP_PKEY_ED25519 || key.id() == EVP_PKEY_ED448;
}

bool UseP1363Encoding(const EVPKeyPointer& key, const DSASigEnc& dsa_encoding) {
  return (key.id() == EVP_PKEY_EC || key.id() == EVP_PKEY_DSA) &&
         dsa_encoding == kSigEncP1363;
}
}  // namespace

SignBase::Error SignBase::Init(const char* sign_type) {
  CHECK_NULL(mdctx_);
  // Historically, "dss1" and "DSS1" were DSA aliases for SHA-1
  // exposed through the public API.
  if (strcmp(sign_type, "dss1") == 0 ||
      strcmp(sign_type, "DSS1") == 0) {
    sign_type = "SHA1";
  }
  const EVP_MD* md = EVP_get_digestbyname(sign_type);
  if (md == nullptr)
    return kSignUnknownDigest;

  mdctx_.reset(EVP_MD_CTX_new());
  if (!mdctx_ || !EVP_DigestInit_ex(mdctx_.get(), md, nullptr)) {
    mdctx_.reset();
    return kSignInit;
  }

  return kSignOk;
}

SignBase::Error SignBase::Update(const char* data, size_t len) {
  if (mdctx_ == nullptr)
    return kSignNotInitialised;
  if (!EVP_DigestUpdate(mdctx_.get(), data, len))
    return kSignUpdate;
  return kSignOk;
}

SignBase::SignBase(Environment* env, Local<Object> wrap)
    : BaseObject(env, wrap) {}

void SignBase::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackFieldWithSize("mdctx", mdctx_ ? kSizeOf_EVP_MD_CTX : 0);
}

Sign::Sign(Environment* env, Local<Object> wrap) : SignBase(env, wrap) {
  MakeWeak();
}

void Sign::Initialize(Environment* env, Local<Object> target) {
  Isolate* isolate = env->isolate();
  Local<FunctionTemplate> t = NewFunctionTemplate(isolate, New);

  t->InstanceTemplate()->SetInternalFieldCount(SignBase::kInternalFieldCount);

  SetProtoMethod(isolate, t, "init", SignInit);
  SetProtoMethod(isolate, t, "update", SignUpdate);
  SetProtoMethod(isolate, t, "sign", SignFinal);

  SetConstructorFunction(env->context(), target, "Sign", t);

  SignJob::Initialize(env, target);

  constexpr int kSignJobModeSign = SignConfiguration::kSign;
  constexpr int kSignJobModeVerify = SignConfiguration::kVerify;

  NODE_DEFINE_CONSTANT(target, kSignJobModeSign);
  NODE_DEFINE_CONSTANT(target, kSignJobModeVerify);
  NODE_DEFINE_CONSTANT(target, kSigEncDER);
  NODE_DEFINE_CONSTANT(target, kSigEncP1363);
  NODE_DEFINE_CONSTANT(target, RSA_PKCS1_PSS_PADDING);
}

void Sign::RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(New);
  registry->Register(SignInit);
  registry->Register(SignUpdate);
  registry->Register(SignFinal);
  SignJob::RegisterExternalReferences(registry);
}

void Sign::New(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  new Sign(env, args.This());
}

void Sign::SignInit(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Sign* sign;
  ASSIGN_OR_RETURN_UNWRAP(&sign, args.This());

  const node::Utf8Value sign_type(args.GetIsolate(), args[0]);
  crypto::CheckThrow(env, sign->Init(*sign_type));
}

void Sign::SignUpdate(const FunctionCallbackInfo<Value>& args) {
  Decode<Sign>(args, [](Sign* sign, const FunctionCallbackInfo<Value>& args,
                        const char* data, size_t size) {
    Environment* env = Environment::GetCurrent(args);
    if (size > INT_MAX) [[unlikely]]
      return THROW_ERR_OUT_OF_RANGE(env, "data is too long");
    Error err = sign->Update(data, size);
    crypto::CheckThrow(sign->env(), err);
  });
}

Sign::SignResult Sign::SignFinal(const EVPKeyPointer& pkey,
                                 int padding,
                                 const Maybe<int>& salt_len,
                                 DSASigEnc dsa_sig_enc) {
  if (!mdctx_)
    return SignResult(kSignNotInitialised);

  EVPMDCtxPointer mdctx = std::move(mdctx_);

  if (!ValidateDSAParameters(pkey.get()))
    return SignResult(kSignPrivateKey);

  std::unique_ptr<BackingStore> buffer =
      Node_SignFinal(env(), std::move(mdctx), pkey, padding, salt_len);
  Error error = buffer ? kSignOk : kSignPrivateKey;
  if (error == kSignOk && dsa_sig_enc == kSigEncP1363) {
    buffer = ConvertSignatureToP1363(env(), pkey, std::move(buffer));
    CHECK_NOT_NULL(buffer->Data());
  }
  return SignResult(error, std::move(buffer));
}

void Sign::SignFinal(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Sign* sign;
  ASSIGN_OR_RETURN_UNWRAP(&sign, args.This());

  ClearErrorOnReturn clear_error_on_return;

  unsigned int offset = 0;
  auto data = KeyObjectData::GetPrivateKeyFromJs(args, &offset, true);
  if (!data) [[unlikely]]
    return;
  const auto& key = data.GetAsymmetricKey();
  if (!key)
    return;

  if (IsOneShot(key)) {
    THROW_ERR_CRYPTO_UNSUPPORTED_OPERATION(env);
    return;
  }

  int padding = GetDefaultSignPadding(key);
  if (!args[offset]->IsUndefined()) {
    CHECK(args[offset]->IsInt32());
    padding = args[offset].As<Int32>()->Value();
  }

  Maybe<int> salt_len = Nothing<int>();
  if (!args[offset + 1]->IsUndefined()) {
    CHECK(args[offset + 1]->IsInt32());
    salt_len = Just<int>(args[offset + 1].As<Int32>()->Value());
  }

  CHECK(args[offset + 2]->IsInt32());
  DSASigEnc dsa_sig_enc =
      static_cast<DSASigEnc>(args[offset + 2].As<Int32>()->Value());

  SignResult ret = sign->SignFinal(
      key,
      padding,
      salt_len,
      dsa_sig_enc);

  if (ret.error != kSignOk)
    return crypto::CheckThrow(env, ret.error);

  Local<ArrayBuffer> ab =
      ArrayBuffer::New(env->isolate(), std::move(ret.signature));
  args.GetReturnValue().Set(
      Buffer::New(env, ab, 0, ab->ByteLength()).FromMaybe(Local<Value>()));
}

Verify::Verify(Environment* env, Local<Object> wrap)
  : SignBase(env, wrap) {
  MakeWeak();
}

void Verify::Initialize(Environment* env, Local<Object> target) {
  Isolate* isolate = env->isolate();
  Local<FunctionTemplate> t = NewFunctionTemplate(isolate, New);

  t->InstanceTemplate()->SetInternalFieldCount(SignBase::kInternalFieldCount);

  SetProtoMethod(isolate, t, "init", VerifyInit);
  SetProtoMethod(isolate, t, "update", VerifyUpdate);
  SetProtoMethod(isolate, t, "verify", VerifyFinal);

  SetConstructorFunction(env->context(), target, "Verify", t);
}

void Verify::RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(New);
  registry->Register(VerifyInit);
  registry->Register(VerifyUpdate);
  registry->Register(VerifyFinal);
}

void Verify::New(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  new Verify(env, args.This());
}

void Verify::VerifyInit(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Verify* verify;
  ASSIGN_OR_RETURN_UNWRAP(&verify, args.This());

  const node::Utf8Value verify_type(args.GetIsolate(), args[0]);
  crypto::CheckThrow(env, verify->Init(*verify_type));
}

void Verify::VerifyUpdate(const FunctionCallbackInfo<Value>& args) {
  Decode<Verify>(args, [](Verify* verify,
                          const FunctionCallbackInfo<Value>& args,
                          const char* data, size_t size) {
    Environment* env = Environment::GetCurrent(args);
    if (size > INT_MAX) [[unlikely]]
      return THROW_ERR_OUT_OF_RANGE(env, "data is too long");
    Error err = verify->Update(data, size);
    crypto::CheckThrow(verify->env(), err);
  });
}

SignBase::Error Verify::VerifyFinal(const EVPKeyPointer& pkey,
                                    const ByteSource& sig,
                                    int padding,
                                    const Maybe<int>& saltlen,
                                    bool* verify_result) {
  if (!mdctx_)
    return kSignNotInitialised;

  unsigned char m[EVP_MAX_MD_SIZE];
  unsigned int m_len;
  *verify_result = false;
  EVPMDCtxPointer mdctx = std::move(mdctx_);

  if (!EVP_DigestFinal_ex(mdctx.get(), m, &m_len))
    return kSignPublicKey;

  EVPKeyCtxPointer pkctx = pkey.newCtx();
  if (pkctx) {
    const int init_ret = EVP_PKEY_verify_init(pkctx.get());
    if (init_ret == -2) {
      return kSignPublicKey;
    }
    if (init_ret > 0 && ApplyRSAOptions(pkey, pkctx.get(), padding, saltlen) &&
        EVP_PKEY_CTX_set_signature_md(pkctx.get(), EVP_MD_CTX_md(mdctx.get())) >
            0) {
      const unsigned char* s = sig.data<unsigned char>();
      const int r = EVP_PKEY_verify(pkctx.get(), s, sig.size(), m, m_len);
      *verify_result = r == 1;
    }
  }

  return kSignOk;
}

void Verify::VerifyFinal(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  ClearErrorOnReturn clear_error_on_return;

  Verify* verify;
  ASSIGN_OR_RETURN_UNWRAP(&verify, args.This());

  unsigned int offset = 0;
  auto data = KeyObjectData::GetPublicOrPrivateKeyFromJs(args, &offset);
  if (!data) return;
  const auto& pkey = data.GetAsymmetricKey();
  if (!pkey)
    return;

  if (IsOneShot(pkey)) {
    THROW_ERR_CRYPTO_UNSUPPORTED_OPERATION(env);
    return;
  }

  ArrayBufferOrViewContents<char> hbuf(args[offset]);
  if (!hbuf.CheckSizeInt32()) [[unlikely]]
    return THROW_ERR_OUT_OF_RANGE(env, "buffer is too big");

  int padding = GetDefaultSignPadding(pkey);
  if (!args[offset + 1]->IsUndefined()) {
    CHECK(args[offset + 1]->IsInt32());
    padding = args[offset + 1].As<Int32>()->Value();
  }

  Maybe<int> salt_len = Nothing<int>();
  if (!args[offset + 2]->IsUndefined()) {
    CHECK(args[offset + 2]->IsInt32());
    salt_len = Just<int>(args[offset + 2].As<Int32>()->Value());
  }

  CHECK(args[offset + 3]->IsInt32());
  DSASigEnc dsa_sig_enc =
      static_cast<DSASigEnc>(args[offset + 3].As<Int32>()->Value());

  ByteSource signature = hbuf.ToByteSource();
  if (dsa_sig_enc == kSigEncP1363) {
    signature = ConvertSignatureToDER(pkey, hbuf.ToByteSource());
    if (signature.data() == nullptr)
      return crypto::CheckThrow(env, Error::kSignMalformedSignature);
  }

  bool verify_result;
  Error err = verify->VerifyFinal(pkey, signature, padding,
                                  salt_len, &verify_result);
  if (err != kSignOk)
    return crypto::CheckThrow(env, err);
  args.GetReturnValue().Set(verify_result);
}

SignConfiguration::SignConfiguration(SignConfiguration&& other) noexcept
    : job_mode(other.job_mode),
      mode(other.mode),
      key(std::move(other.key)),
      data(std::move(other.data)),
      signature(std::move(other.signature)),
      digest(other.digest),
      flags(other.flags),
      padding(other.padding),
      salt_length(other.salt_length),
      dsa_encoding(other.dsa_encoding) {}

SignConfiguration& SignConfiguration::operator=(
    SignConfiguration&& other) noexcept {
  if (&other == this) return *this;
  this->~SignConfiguration();
  return *new (this) SignConfiguration(std::move(other));
}

void SignConfiguration::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("key", key);
  if (job_mode == kCryptoJobAsync) {
    tracker->TrackFieldWithSize("data", data.size());
    tracker->TrackFieldWithSize("signature", signature.size());
  }
}

Maybe<void> SignTraits::AdditionalConfig(
    CryptoJobMode mode,
    const FunctionCallbackInfo<Value>& args,
    unsigned int offset,
    SignConfiguration* params) {
  ClearErrorOnReturn clear_error_on_return;
  Environment* env = Environment::GetCurrent(args);

  params->job_mode = mode;

  CHECK(args[offset]->IsUint32());  // Sign Mode

  params->mode =
      static_cast<SignConfiguration::Mode>(args[offset].As<Uint32>()->Value());

  unsigned int keyParamOffset = offset + 1;
  if (params->mode == SignConfiguration::kVerify) {
    auto data =
        KeyObjectData::GetPublicOrPrivateKeyFromJs(args, &keyParamOffset);
    if (!data) return Nothing<void>();
    params->key = std::move(data);
  } else {
    auto data = KeyObjectData::GetPrivateKeyFromJs(args, &keyParamOffset, true);
    if (!data) return Nothing<void>();
    params->key = std::move(data);
  }

  ArrayBufferOrViewContents<char> data(args[offset + 5]);
  if (!data.CheckSizeInt32()) [[unlikely]] {
    THROW_ERR_OUT_OF_RANGE(env, "data is too big");
    return Nothing<void>();
  }
  params->data = mode == kCryptoJobAsync
      ? data.ToCopy()
      : data.ToByteSource();

  if (args[offset + 6]->IsString()) {
    Utf8Value digest(env->isolate(), args[offset + 6]);
    params->digest = EVP_get_digestbyname(*digest);
    if (params->digest == nullptr) {
      THROW_ERR_CRYPTO_INVALID_DIGEST(env, "Invalid digest: %s", *digest);
      return Nothing<void>();
    }
  }

  if (args[offset + 7]->IsInt32()) {  // Salt length
    params->flags |= SignConfiguration::kHasSaltLength;
    params->salt_length = args[offset + 7].As<Int32>()->Value();
  }
  if (args[offset + 8]->IsUint32()) {  // Padding
    params->flags |= SignConfiguration::kHasPadding;
    params->padding = args[offset + 8].As<Uint32>()->Value();
  }

  if (args[offset + 9]->IsUint32()) {  // DSA Encoding
    params->dsa_encoding =
        static_cast<DSASigEnc>(args[offset + 9].As<Uint32>()->Value());
    if (params->dsa_encoding != kSigEncDER &&
        params->dsa_encoding != kSigEncP1363) {
      THROW_ERR_OUT_OF_RANGE(env, "invalid signature encoding");
      return Nothing<void>();
    }
  }

  if (params->mode == SignConfiguration::kVerify) {
    ArrayBufferOrViewContents<char> signature(args[offset + 10]);
    if (!signature.CheckSizeInt32()) [[unlikely]] {
      THROW_ERR_OUT_OF_RANGE(env, "signature is too big");
      return Nothing<void>();
    }
    // If this is an EC key (assuming ECDSA) we need to convert the
    // the signature from WebCrypto format into DER format...
    Mutex::ScopedLock lock(params->key.mutex());
    const auto& akey = params->key.GetAsymmetricKey();
    if (UseP1363Encoding(akey, params->dsa_encoding)) {
      params->signature = ConvertSignatureToDER(akey, signature.ToByteSource());
    } else {
      params->signature = mode == kCryptoJobAsync
          ? signature.ToCopy()
          : signature.ToByteSource();
    }
  }

  return JustVoid();
}

bool SignTraits::DeriveBits(
    Environment* env,
    const SignConfiguration& params,
    ByteSource* out) {
  ClearErrorOnReturn clear_error_on_return;
  EVPMDCtxPointer context(EVP_MD_CTX_new());
  EVP_PKEY_CTX* ctx = nullptr;

  const auto& key = params.key.GetAsymmetricKey();

  switch (params.mode) {
    case SignConfiguration::kSign:
      if (!EVP_DigestSignInit(
              context.get(), &ctx, params.digest, nullptr, key.get())) {
        crypto::CheckThrow(env, SignBase::Error::kSignInit);
        return false;
      }
      break;
    case SignConfiguration::kVerify:
      if (!EVP_DigestVerifyInit(
              context.get(), &ctx, params.digest, nullptr, key.get())) {
        crypto::CheckThrow(env, SignBase::Error::kSignInit);
        return false;
      }
      break;
  }

  int padding = params.flags & SignConfiguration::kHasPadding
                    ? params.padding
                    : GetDefaultSignPadding(key);

  Maybe<int> salt_length = params.flags & SignConfiguration::kHasSaltLength
      ? Just<int>(params.salt_length) : Nothing<int>();

  if (!ApplyRSAOptions(key, ctx, padding, salt_length)) {
    crypto::CheckThrow(env, SignBase::Error::kSignPrivateKey);
    return false;
  }

  switch (params.mode) {
    case SignConfiguration::kSign: {
      if (IsOneShot(key)) {
        size_t len;
        if (!EVP_DigestSign(
            context.get(),
            nullptr,
            &len,
            params.data.data<unsigned char>(),
            params.data.size())) {
          crypto::CheckThrow(env, SignBase::Error::kSignPrivateKey);
          return false;
        }
        ByteSource::Builder buf(len);
        if (!EVP_DigestSign(context.get(),
                            buf.data<unsigned char>(),
                            &len,
                            params.data.data<unsigned char>(),
                            params.data.size())) {
          crypto::CheckThrow(env, SignBase::Error::kSignPrivateKey);
          return false;
        }
        *out = std::move(buf).release(len);
      } else {
        size_t len;
        if (!EVP_DigestSignUpdate(
                context.get(),
                params.data.data<unsigned char>(),
                params.data.size()) ||
            !EVP_DigestSignFinal(context.get(), nullptr, &len)) {
          crypto::CheckThrow(env, SignBase::Error::kSignPrivateKey);
          return false;
        }
        ByteSource::Builder buf(len);
        if (!EVP_DigestSignFinal(
                context.get(), buf.data<unsigned char>(), &len)) {
          crypto::CheckThrow(env, SignBase::Error::kSignPrivateKey);
          return false;
        }

        if (UseP1363Encoding(key, params.dsa_encoding)) {
          *out = ConvertSignatureToP1363(env, key, std::move(buf).release());
        } else {
          *out = std::move(buf).release(len);
        }
      }
      break;
    }
    case SignConfiguration::kVerify: {
      ByteSource::Builder buf(1);
      buf.data<char>()[0] = 0;
      if (EVP_DigestVerify(
              context.get(),
              params.signature.data<unsigned char>(),
              params.signature.size(),
              params.data.data<unsigned char>(),
              params.data.size()) == 1) {
        buf.data<char>()[0] = 1;
      }
      *out = std::move(buf).release();
    }
  }

  return true;
}

MaybeLocal<Value> SignTraits::EncodeOutput(Environment* env,
                                           const SignConfiguration& params,
                                           ByteSource* out) {
  switch (params.mode) {
    case SignConfiguration::kSign:
      return out->ToArrayBuffer(env);
    case SignConfiguration::kVerify:
      return Boolean::New(env->isolate(), out->data<char>()[0] == 1);
  }
  UNREACHABLE();
}

}  // namespace crypto
}  // namespace node
