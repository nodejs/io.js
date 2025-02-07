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
using ncrypto::DataPointer;
using ncrypto::ECDSASigPointer;
using ncrypto::EVPKeyCtxPointer;
using ncrypto::EVPKeyPointer;
using ncrypto::EVPMDCtxPointer;
using v8::ArrayBuffer;
using v8::BackingStore;
using v8::BackingStoreInitializationMode;
using v8::Boolean;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Int32;
using v8::Isolate;
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
int GetPaddingFromJS(const EVPKeyPointer& key, Local<Value> val) {
  int padding = key.getDefaultSignPadding();
  if (!val->IsUndefined()) [[likely]] {
    CHECK(val->IsInt32());
    padding = val.As<Int32>()->Value();
  }
  return padding;
}

std::optional<int> GetSaltLenFromJS(Local<Value> val) {
  std::optional<int> salt_len;
  if (!val->IsUndefined()) [[likely]] {
    CHECK(val->IsInt32());
    salt_len = val.As<Int32>()->Value();
  }
  return salt_len;
}

DSASigEnc GetDSASigEncFromJS(Local<Value> val) {
  CHECK(val->IsInt32());
  int i = val.As<Int32>()->Value();
  if (i < 0 || i >= static_cast<int>(DSASigEnc::Invalid)) [[unlikely]] {
    return DSASigEnc::Invalid;
  }
  return static_cast<DSASigEnc>(val.As<Int32>()->Value());
}

bool ApplyRSAOptions(const EVPKeyPointer& pkey,
                     EVP_PKEY_CTX* pkctx,
                     int padding,
                     std::optional<int> salt_len) {
  if (pkey.isRsaVariant()) {
    return EVPKeyCtxPointer::setRsaPadding(pkctx, padding, salt_len);
  }
  return true;
}

std::unique_ptr<BackingStore> Node_SignFinal(Environment* env,
                                             EVPMDCtxPointer&& mdctx,
                                             const EVPKeyPointer& pkey,
                                             int padding,
                                             std::optional<int> pss_salt_len) {
  auto data = mdctx.digestFinal(mdctx.getExpectedSize());
  if (!data) [[unlikely]]
    return nullptr;

  auto sig = ArrayBuffer::NewBackingStore(env->isolate(), pkey.size());
  ncrypto::Buffer<unsigned char> sig_buf{
      .data = static_cast<unsigned char*>(sig->Data()),
      .len = pkey.size(),
  };

  EVPKeyCtxPointer pkctx = pkey.newCtx();
  if (pkctx.initForSign() > 0 &&
      ApplyRSAOptions(pkey, pkctx.get(), padding, pss_salt_len) &&
      pkctx.setSignatureMd(mdctx) && pkctx.signInto(data, &sig_buf))
      [[likely]] {
    CHECK_LE(sig_buf.len, sig->ByteLength());
    if (sig_buf.len < sig->ByteLength()) {
      auto new_sig = ArrayBuffer::NewBackingStore(env->isolate(), sig_buf.len);
      if (sig_buf.len > 0) [[likely]] {
        memcpy(static_cast<char*>(new_sig->Data()),
               static_cast<char*>(sig->Data()),
               sig_buf.len);
      }
      sig = std::move(new_sig);
    }
    return sig;
  }

  return nullptr;
}

// Returns the maximum size of each of the integers (r, s) of the DSA signature.
std::unique_ptr<BackingStore> ConvertSignatureToP1363(
    Environment* env,
    const EVPKeyPointer& pkey,
    std::unique_ptr<BackingStore>&& signature) {
  uint32_t n = pkey.getBytesOfRS().value_or(kNoDsaSignature);
  if (n == kNoDsaSignature) return std::move(signature);

  auto buf = ArrayBuffer::NewBackingStore(
      env->isolate(), 2 * n, BackingStoreInitializationMode::kUninitialized);

  ncrypto::Buffer<const unsigned char> sig_buffer{
      .data = static_cast<const unsigned char*>(signature->Data()),
      .len = signature->ByteLength(),
  };

  if (!ncrypto::extractP1363(
          sig_buffer, static_cast<unsigned char*>(buf->Data()), n)) {
    return std::move(signature);
  }

  return buf;
}

// Returns the maximum size of each of the integers (r, s) of the DSA signature.
ByteSource ConvertSignatureToP1363(Environment* env,
                                   const EVPKeyPointer& pkey,
                                   const ByteSource& signature) {
  unsigned int n = pkey.getBytesOfRS().value_or(kNoDsaSignature);
  if (n == kNoDsaSignature) [[unlikely]]
    return {};

  auto data = DataPointer::Alloc(n * 2);
  if (!data) [[unlikely]]
    return {};
  unsigned char* out = static_cast<unsigned char*>(data.get());

  // Extracting the signature may not actually use all of the allocated space.
  // We need to ensure that the buffer is zeroed out before use.
  data.zero();

  if (!ncrypto::extractP1363(signature, out, n)) [[unlikely]] {
    return {};
  }

  return ByteSource::Allocated(data.release());
}

ByteSource ConvertSignatureToDER(const EVPKeyPointer& pkey, ByteSource&& out) {
  unsigned int n = pkey.getBytesOfRS().value_or(kNoDsaSignature);
  if (n == kNoDsaSignature) return std::move(out);

  const unsigned char* sig_data = out.data<unsigned char>();

  if (out.size() != 2 * n) return {};

  auto asn1_sig = ECDSASigPointer::New();
  CHECK(asn1_sig);
  BignumPointer r(sig_data, n);
  CHECK(r);
  BignumPointer s(sig_data + n, n);
  CHECK(s);
  CHECK(asn1_sig.setParams(std::move(r), std::move(s)));

  auto buf = asn1_sig.encode();
  if (buf.len <= 0) [[unlikely]]
    return {};

  CHECK_NOT_NULL(buf.data);
  return ByteSource::Allocated(buf);
}

void CheckThrow(Environment* env, SignBase::Error error) {
  HandleScope scope(env->isolate());

  switch (error) {
    case SignBase::Error::UnknownDigest:
      return THROW_ERR_CRYPTO_INVALID_DIGEST(env);

    case SignBase::Error::NotInitialised:
      return THROW_ERR_CRYPTO_INVALID_STATE(env, "Not initialised");

    case SignBase::Error::MalformedSignature:
      return THROW_ERR_CRYPTO_OPERATION_FAILED(env, "Malformed signature");

    case SignBase::Error::Init:
    case SignBase::Error::Update:
    case SignBase::Error::PrivateKey:
    case SignBase::Error::PublicKey: {
      unsigned long err = ERR_get_error();  // NOLINT(runtime/int)
      if (err) return ThrowCryptoError(env, err);
      switch (error) {
        case SignBase::Error::Init:
          return THROW_ERR_CRYPTO_OPERATION_FAILED(env,
                                                   "EVP_SignInit_ex failed");
        case SignBase::Error::Update:
          return THROW_ERR_CRYPTO_OPERATION_FAILED(env,
                                                   "EVP_SignUpdate failed");
        case SignBase::Error::PrivateKey:
          return THROW_ERR_CRYPTO_OPERATION_FAILED(
              env, "PEM_read_bio_PrivateKey failed");
        case SignBase::Error::PublicKey:
          return THROW_ERR_CRYPTO_OPERATION_FAILED(
              env, "PEM_read_bio_PUBKEY failed");
        default:
          ABORT();
      }
    }

    case SignBase::Error::Ok:
      return;
  }
}

bool UseP1363Encoding(const EVPKeyPointer& key, const DSASigEnc dsa_encoding) {
  return key.isSigVariant() && dsa_encoding == DSASigEnc::P1363;
}
}  // namespace

SignBase::Error SignBase::Init(std::string_view digest) {
  CHECK_NULL(mdctx_);
  auto md = ncrypto::getDigestByName(digest);
  if (md == nullptr) [[unlikely]]
    return Error::UnknownDigest;

  mdctx_ = EVPMDCtxPointer::New();

  if (!mdctx_.digestInit(md)) [[unlikely]] {
    mdctx_.reset();
    return Error::Init;
  }

  return Error::Ok;
}

SignBase::Error SignBase::Update(const char* data, size_t len) {
  if (mdctx_ == nullptr) [[unlikely]]
    return Error::NotInitialised;

  ncrypto::Buffer<const void> buf{
      .data = data,
      .len = len,
  };

  return mdctx_.digestUpdate(buf) ? Error::Ok : Error::Update;
}

SignBase::SignBase(Environment* env, Local<Object> wrap)
    : BaseObject(env, wrap) {
  MakeWeak();
}

void SignBase::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackFieldWithSize("mdctx", mdctx_ ? kSizeOf_EVP_MD_CTX : 0);
}

Sign::Sign(Environment* env, Local<Object> wrap) : SignBase(env, wrap) {}

void Sign::Initialize(Environment* env, Local<Object> target) {
  Isolate* isolate = env->isolate();
  Local<FunctionTemplate> t = NewFunctionTemplate(isolate, New);

  t->InstanceTemplate()->SetInternalFieldCount(SignBase::kInternalFieldCount);

  SetProtoMethod(isolate, t, "init", SignInit);
  SetProtoMethod(isolate, t, "update", SignUpdate);
  SetProtoMethod(isolate, t, "sign", SignFinal);

  SetConstructorFunction(env->context(), target, "Sign", t);

  SignJob::Initialize(env, target);

  constexpr int kSignJobModeSign =
      static_cast<int>(SignConfiguration::Mode::Sign);
  constexpr int kSignJobModeVerify =
      static_cast<int>(SignConfiguration::Mode::Verify);

  constexpr auto kSigEncDER = DSASigEnc::DER;
  constexpr auto kSigEncP1363 = DSASigEnc::P1363;

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

  const node::Utf8Value sign_type(env->isolate(), args[0]);
  crypto::CheckThrow(env, sign->Init(sign_type.ToStringView()));
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
                                 std::optional<int> salt_len,
                                 DSASigEnc dsa_sig_enc) {
  if (!mdctx_) [[unlikely]] {
    return SignResult(Error::NotInitialised);
  }

  EVPMDCtxPointer mdctx = std::move(mdctx_);

  if (!pkey.validateDsaParameters()) {
    return SignResult(Error::PrivateKey);
  }

  auto buffer =
      Node_SignFinal(env(), std::move(mdctx), pkey, padding, salt_len);
  Error error = buffer ? Error::Ok : Error::PrivateKey;
  if (error == Error::Ok && dsa_sig_enc == DSASigEnc::P1363) {
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
  if (!key) [[unlikely]]
    return;

  if (key.isOneShotVariant()) [[unlikely]] {
    THROW_ERR_CRYPTO_UNSUPPORTED_OPERATION(env);
    return;
  }

  int padding = GetPaddingFromJS(key, args[offset]);
  std::optional<int> salt_len = GetSaltLenFromJS(args[offset + 1]);
  DSASigEnc dsa_sig_enc = GetDSASigEncFromJS(args[offset + 2]);
  if (dsa_sig_enc == DSASigEnc::Invalid) [[unlikely]] {
    THROW_ERR_OUT_OF_RANGE(env, "invalid signature encoding");
    return;
  }

  SignResult ret = sign->SignFinal(key, padding, salt_len, dsa_sig_enc);

  if (ret.error != Error::Ok) [[unlikely]] {
    return crypto::CheckThrow(env, ret.error);
  }

  auto ab = ArrayBuffer::New(env->isolate(), std::move(ret.signature));
  args.GetReturnValue().Set(
      Buffer::New(env, ab, 0, ab->ByteLength()).FromMaybe(Local<Value>()));
}

Verify::Verify(Environment* env, Local<Object> wrap) : SignBase(env, wrap) {}

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

  const node::Utf8Value verify_type(env->isolate(), args[0]);
  crypto::CheckThrow(env, verify->Init(verify_type.ToStringView()));
}

void Verify::VerifyUpdate(const FunctionCallbackInfo<Value>& args) {
  Decode<Verify>(args, [](Verify* verify,
                          const FunctionCallbackInfo<Value>& args,
                          const char* data, size_t size) {
    Environment* env = Environment::GetCurrent(args);
    if (size > INT_MAX) [[unlikely]] {
      return THROW_ERR_OUT_OF_RANGE(env, "data is too long");
    }
    Error err = verify->Update(data, size);
    crypto::CheckThrow(verify->env(), err);
  });
}

SignBase::Error Verify::VerifyFinal(const EVPKeyPointer& pkey,
                                    const ByteSource& sig,
                                    int padding,
                                    std::optional<int> saltlen,
                                    bool* verify_result) {
  if (!mdctx_) [[unlikely]]
    return Error::NotInitialised;

  *verify_result = false;
  EVPMDCtxPointer mdctx = std::move(mdctx_);

  auto data = mdctx.digestFinal(mdctx.getExpectedSize());
  if (!data) [[unlikely]]
    return Error::PublicKey;

  EVPKeyCtxPointer pkctx = pkey.newCtx();
  if (pkctx) [[likely]] {
    const int init_ret = pkctx.initForVerify();
    if (init_ret == -2) [[unlikely]]
      return Error::PublicKey;
    if (init_ret > 0 && ApplyRSAOptions(pkey, pkctx.get(), padding, saltlen) &&
        pkctx.setSignatureMd(mdctx)) {
      *verify_result = pkctx.verify(sig, data);
    }
  }

  return Error::Ok;
}

void Verify::VerifyFinal(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  ClearErrorOnReturn clear_error_on_return;

  Verify* verify;
  ASSIGN_OR_RETURN_UNWRAP(&verify, args.This());

  unsigned int offset = 0;
  auto data = KeyObjectData::GetPublicOrPrivateKeyFromJs(args, &offset);
  if (!data) [[unlikely]]
    return;
  const auto& key = data.GetAsymmetricKey();
  if (!key) [[unlikely]]
    return;

  if (key.isOneShotVariant()) [[unlikely]] {
    THROW_ERR_CRYPTO_UNSUPPORTED_OPERATION(env);
    return;
  }

  ArrayBufferOrViewContents<char> hbuf(args[offset]);
  if (!hbuf.CheckSizeInt32()) [[unlikely]] {
    return THROW_ERR_OUT_OF_RANGE(env, "buffer is too big");
  }

  int padding = GetPaddingFromJS(key, args[offset + 1]);
  std::optional<int> salt_len = GetSaltLenFromJS(args[offset + 2]);
  DSASigEnc dsa_sig_enc = GetDSASigEncFromJS(args[offset + 3]);
  if (dsa_sig_enc == DSASigEnc::Invalid) [[unlikely]] {
    THROW_ERR_OUT_OF_RANGE(env, "invalid signature encoding");
    return;
  }

  ByteSource signature = hbuf.ToByteSource();
  if (dsa_sig_enc == DSASigEnc::P1363) {
    signature = ConvertSignatureToDER(key, hbuf.ToByteSource());
    if (signature.data() == nullptr) [[unlikely]] {
      return crypto::CheckThrow(env, Error::MalformedSignature);
    }
  }

  bool verify_result;
  Error err =
      verify->VerifyFinal(key, signature, padding, salt_len, &verify_result);
  if (err != Error::Ok) [[unlikely]]
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
  if (params->mode == SignConfiguration::Mode::Verify) {
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
    params->digest = ncrypto::getDigestByName(digest.ToStringView());
    if (params->digest == nullptr) [[unlikely]] {
      THROW_ERR_CRYPTO_INVALID_DIGEST(env, "Invalid digest: %s", *digest);
      return Nothing<void>();
    }
  }

  if (args[offset + 7]->IsInt32()) {  // Salt length
    params->flags |= SignConfiguration::kHasSaltLength;
    params->salt_length =
        GetSaltLenFromJS(args[offset + 7]).value_or(params->salt_length);
  }
  if (args[offset + 8]->IsUint32()) {  // Padding
    params->flags |= SignConfiguration::kHasPadding;
    params->padding =
        GetPaddingFromJS(params->key.GetAsymmetricKey(), args[offset + 8]);
  }

  if (args[offset + 9]->IsUint32()) {  // DSA Encoding
    params->dsa_encoding = GetDSASigEncFromJS(args[offset + 9]);
    if (params->dsa_encoding == DSASigEnc::Invalid) [[unlikely]] {
      THROW_ERR_OUT_OF_RANGE(env, "invalid signature encoding");
      return Nothing<void>();
    }
  }

  if (params->mode == SignConfiguration::Mode::Verify) {
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
  auto context = EVPMDCtxPointer::New();
  if (!context) [[unlikely]]
    return false;
  const auto& key = params.key.GetAsymmetricKey();

  auto ctx = ([&] {
    switch (params.mode) {
      case SignConfiguration::Mode::Sign:
        return context.signInit(key, params.digest);
      case SignConfiguration::Mode::Verify:
        return context.verifyInit(key, params.digest);
    }
    UNREACHABLE();
  })();

  if (!ctx.has_value()) [[unlikely]] {
    crypto::CheckThrow(env, SignBase::Error::Init);
    return false;
  }

  int padding = params.flags & SignConfiguration::kHasPadding
                    ? params.padding
                    : key.getDefaultSignPadding();

  std::optional<int> salt_length =
      params.flags & SignConfiguration::kHasSaltLength
          ? std::optional<int>(params.salt_length)
          : std::nullopt;

  if (!ApplyRSAOptions(key, *ctx, padding, salt_length)) {
    crypto::CheckThrow(env, SignBase::Error::PrivateKey);
    return false;
  }

  switch (params.mode) {
    case SignConfiguration::Mode::Sign: {
      if (key.isOneShotVariant()) {
        auto data = context.signOneShot(params.data);
        if (!data) [[unlikely]] {
          crypto::CheckThrow(env, SignBase::Error::PrivateKey);
          return false;
        }
        *out = ByteSource::Allocated(data.release());
      } else {
        auto data = context.sign(params.data);
        if (!data) [[unlikely]] {
          crypto::CheckThrow(env, SignBase::Error::PrivateKey);
          return false;
        }
        auto bs = ByteSource::Allocated(data.release());

        if (UseP1363Encoding(key, params.dsa_encoding)) {
          *out = ConvertSignatureToP1363(env, key, std::move(bs));
        } else {
          *out = std::move(bs);
        }
      }
      break;
    }
    case SignConfiguration::Mode::Verify: {
      auto buf = DataPointer::Alloc(1);
      static_cast<char*>(buf.get())[0] = 0;
      if (context.verify(params.data, params.signature)) {
        static_cast<char*>(buf.get())[0] = 1;
      }
      *out = ByteSource::Allocated(buf.release());
    }
  }

  return true;
}

MaybeLocal<Value> SignTraits::EncodeOutput(Environment* env,
                                           const SignConfiguration& params,
                                           ByteSource* out) {
  switch (params.mode) {
    case SignConfiguration::Mode::Sign:
      return out->ToArrayBuffer(env);
    case SignConfiguration::Mode::Verify:
      return Boolean::New(env->isolate(), out->data<char>()[0] == 1);
  }
  UNREACHABLE();
}

}  // namespace crypto
}  // namespace node
