#include "crypto/crypto_cipher.h"
#include "base_object-inl.h"
#include "crypto/crypto_util.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "node_buffer.h"
#include "node_internals.h"
#include "node_process-inl.h"
#include "v8.h"

namespace node {

using ncrypto::Cipher;
using ncrypto::CipherCtxPointer;
using ncrypto::EVPKeyCtxPointer;
using ncrypto::EVPKeyPointer;
using ncrypto::MarkPopErrorOnReturn;
using ncrypto::SSLCtxPointer;
using ncrypto::SSLPointer;
using v8::Array;
using v8::ArrayBuffer;
using v8::BackingStore;
using v8::Context;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Int32;
using v8::Isolate;
using v8::Local;
using v8::LocalVector;
using v8::Object;
using v8::Uint32;
using v8::Value;

namespace crypto {
namespace {
bool IsValidGCMTagLength(unsigned int tag_len) {
  return tag_len == 4 || tag_len == 8 || (tag_len >= 12 && tag_len <= 16);
}

// Collects and returns information on the given cipher
void GetCipherInfo(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(args[0]->IsObject());
  Local<Object> info = args[0].As<Object>();

  CHECK(args[1]->IsString() || args[1]->IsInt32());

  const auto cipher = ([&] {
    if (args[1]->IsString()) {
      Utf8Value name(env->isolate(), args[1]);
      return Cipher::FromName(*name);
    } else {
      int nid = args[1].As<Int32>()->Value();
      return Cipher::FromNid(nid);
    }
  })();

  if (!cipher) return;

  int iv_length = cipher.getIvLength();
  int key_length = cipher.getKeyLength();
  int block_length = cipher.getBlockSize();
  auto mode_label = cipher.getModeLabel();
  auto name = cipher.getName();

  // If the testKeyLen and testIvLen arguments are specified,
  // then we will make an attempt to see if they are usable for
  // the cipher in question, returning undefined if they are not.
  // If they are, the info object will be returned with the values
  // given.
  if (args[2]->IsInt32() || args[3]->IsInt32()) {
    // Test and input IV or key length to determine if it's acceptable.
    // If it is, then the getCipherInfo will succeed with the given
    // values.
    auto ctx = CipherCtxPointer::New();
    if (!ctx.init(cipher, true)) {
      return;
    }

    if (args[2]->IsInt32()) {
      int check_len = args[2].As<Int32>()->Value();
      if (!ctx.setKeyLength(check_len)) {
        return;
      }
      key_length = check_len;
    }

    if (args[3]->IsInt32()) {
      int check_len = args[3].As<Int32>()->Value();
      // For CCM modes, the IV may be between 7 and 13 bytes.
      // For GCM and OCB modes, we'll check by attempting to
      // set the value. For everything else, just check that
      // check_len == iv_length.
      switch (cipher.getMode()) {
        case EVP_CIPH_CCM_MODE:
          if (check_len < 7 || check_len > 13)
            return;
          break;
        case EVP_CIPH_GCM_MODE:
          // Fall through
        case EVP_CIPH_OCB_MODE:
          if (!ctx.setIvLength(check_len)) {
            return;
          }
          break;
        default:
          if (check_len != iv_length)
            return;
      }
      iv_length = check_len;
    }
  }

  if (mode_label.length() &&
      info->Set(env->context(),
                FIXED_ONE_BYTE_STRING(env->isolate(), "mode"),
                OneByteString(
                    env->isolate(), mode_label.data(), mode_label.length()))
          .IsNothing()) {
    return;
  }

  if (info->Set(env->context(),
                env->name_string(),
                OneByteString(env->isolate(), name.data(), name.length()))
          .IsNothing()) {
    return;
  }

  if (info->Set(env->context(),
                FIXED_ONE_BYTE_STRING(env->isolate(), "nid"),
                Int32::New(env->isolate(), cipher.getNid()))
          .IsNothing()) {
    return;
  }

  // Stream ciphers do not have a meaningful block size
  if (cipher.getMode() != EVP_CIPH_STREAM_CIPHER &&
      info->Set(env->context(),
                FIXED_ONE_BYTE_STRING(env->isolate(), "blockSize"),
                Int32::New(env->isolate(), block_length))
          .IsNothing()) {
    return;
  }

  // Ciphers that do not use an IV shouldn't report a length
  if (iv_length != 0 &&
      info->Set(
          env->context(),
          FIXED_ONE_BYTE_STRING(env->isolate(), "ivLength"),
          Int32::New(env->isolate(), iv_length)).IsNothing()) {
    return;
  }

  if (info->Set(
          env->context(),
          FIXED_ONE_BYTE_STRING(env->isolate(), "keyLength"),
          Int32::New(env->isolate(), key_length)).IsNothing()) {
    return;
  }

  args.GetReturnValue().Set(info);
}
}  // namespace

void CipherBase::GetSSLCiphers(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  auto ctx = SSLCtxPointer::New();
  if (!ctx) {
    return ThrowCryptoError(env, ERR_get_error(), "SSL_CTX_new");
  }

  auto ssl = SSLPointer::New(ctx);
  if (!ssl) {
    return ThrowCryptoError(env, ERR_get_error(), "SSL_new");
  }

  LocalVector<Value> arr(env->isolate());
  ssl.getCiphers([&](const std::string_view name) {
    arr.push_back(OneByteString(env->isolate(), name.data(), name.length()));
  });

  args.GetReturnValue().Set(Array::New(env->isolate(), arr.data(), arr.size()));
}

void CipherBase::GetCiphers(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  MarkPopErrorOnReturn mark_pop_error_on_return;
  CipherPushContext ctx(env);
  EVP_CIPHER_do_all_sorted(
#if OPENSSL_VERSION_MAJOR >= 3
    array_push_back<EVP_CIPHER,
                    EVP_CIPHER_fetch,
                    EVP_CIPHER_free,
                    EVP_get_cipherbyname,
                    EVP_CIPHER_get0_name>,
#else
    array_push_back<EVP_CIPHER>,
#endif
    &ctx);
  args.GetReturnValue().Set(ctx.ToJSArray());
}

CipherBase::CipherBase(Environment* env,
                       Local<Object> wrap,
                       CipherKind kind)
    : BaseObject(env, wrap),
      ctx_(nullptr),
      kind_(kind),
      auth_tag_state_(kAuthTagUnknown),
      auth_tag_len_(kNoAuthTagLength),
      pending_auth_failed_(false) {
  MakeWeak();
}

void CipherBase::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackFieldWithSize("context", ctx_ ? kSizeOf_EVP_CIPHER_CTX : 0);
}

void CipherBase::Initialize(Environment* env, Local<Object> target) {
  Isolate* isolate = env->isolate();
  Local<Context> context = env->context();

  Local<FunctionTemplate> t = NewFunctionTemplate(isolate, New);

  t->InstanceTemplate()->SetInternalFieldCount(CipherBase::kInternalFieldCount);

  SetProtoMethod(isolate, t, "init", Init);
  SetProtoMethod(isolate, t, "initiv", InitIv);
  SetProtoMethod(isolate, t, "update", Update);
  SetProtoMethod(isolate, t, "final", Final);
  SetProtoMethod(isolate, t, "setAutoPadding", SetAutoPadding);
  SetProtoMethodNoSideEffect(isolate, t, "getAuthTag", GetAuthTag);
  SetProtoMethod(isolate, t, "setAuthTag", SetAuthTag);
  SetProtoMethod(isolate, t, "setAAD", SetAAD);
  SetConstructorFunction(context, target, "CipherBase", t);

  SetMethodNoSideEffect(context, target, "getSSLCiphers", GetSSLCiphers);
  SetMethodNoSideEffect(context, target, "getCiphers", GetCiphers);

  SetMethod(context,
            target,
            "publicEncrypt",
            PublicKeyCipher::Cipher<PublicKeyCipher::kPublic,
                                    EVP_PKEY_encrypt_init,
                                    EVP_PKEY_encrypt>);
  SetMethod(context,
            target,
            "privateDecrypt",
            PublicKeyCipher::Cipher<PublicKeyCipher::kPrivate,
                                    EVP_PKEY_decrypt_init,
                                    EVP_PKEY_decrypt>);
  SetMethod(context,
            target,
            "privateEncrypt",
            PublicKeyCipher::Cipher<PublicKeyCipher::kPrivate,
                                    EVP_PKEY_sign_init,
                                    EVP_PKEY_sign>);
  SetMethod(context,
            target,
            "publicDecrypt",
            PublicKeyCipher::Cipher<PublicKeyCipher::kPublic,
                                    EVP_PKEY_verify_recover_init,
                                    EVP_PKEY_verify_recover>);

  SetMethodNoSideEffect(context, target, "getCipherInfo", GetCipherInfo);

  NODE_DEFINE_CONSTANT(target, kWebCryptoCipherEncrypt);
  NODE_DEFINE_CONSTANT(target, kWebCryptoCipherDecrypt);
}

void CipherBase::RegisterExternalReferences(
    ExternalReferenceRegistry* registry) {
  registry->Register(New);

  registry->Register(Init);
  registry->Register(InitIv);
  registry->Register(Update);
  registry->Register(Final);
  registry->Register(SetAutoPadding);
  registry->Register(GetAuthTag);
  registry->Register(SetAuthTag);
  registry->Register(SetAAD);

  registry->Register(GetSSLCiphers);
  registry->Register(GetCiphers);

  registry->Register(PublicKeyCipher::Cipher<PublicKeyCipher::kPublic,
                                             EVP_PKEY_encrypt_init,
                                             EVP_PKEY_encrypt>);
  registry->Register(PublicKeyCipher::Cipher<PublicKeyCipher::kPrivate,
                                             EVP_PKEY_decrypt_init,
                                             EVP_PKEY_decrypt>);
  registry->Register(PublicKeyCipher::Cipher<PublicKeyCipher::kPrivate,
                                             EVP_PKEY_sign_init,
                                             EVP_PKEY_sign>);
  registry->Register(PublicKeyCipher::Cipher<PublicKeyCipher::kPublic,
                                             EVP_PKEY_verify_recover_init,
                                             EVP_PKEY_verify_recover>);

  registry->Register(GetCipherInfo);
}

void CipherBase::New(const FunctionCallbackInfo<Value>& args) {
  CHECK(args.IsConstructCall());
  Environment* env = Environment::GetCurrent(args);
  new CipherBase(env, args.This(), args[0]->IsTrue() ? kCipher : kDecipher);
}

void CipherBase::CommonInit(const char* cipher_type,
                            const ncrypto::Cipher& cipher,
                            const unsigned char* key,
                            int key_len,
                            const unsigned char* iv,
                            int iv_len,
                            unsigned int auth_tag_len) {
  CHECK(!ctx_);
  ctx_ = CipherCtxPointer::New();
  CHECK(ctx_);

  if (cipher.getMode() == EVP_CIPH_WRAP_MODE) {
    ctx_.setFlags(EVP_CIPHER_CTX_FLAG_WRAP_ALLOW);
  }

  const bool encrypt = (kind_ == kCipher);
  if (!ctx_.init(cipher, encrypt)) {
    return ThrowCryptoError(env(), ERR_get_error(),
                            "Failed to initialize cipher");
  }

  if (cipher.isSupportedAuthenticatedMode()) {
    CHECK_GE(iv_len, 0);
    if (!InitAuthenticated(cipher_type, iv_len, auth_tag_len))
      return;
  }

  if (!ctx_.setKeyLength(key_len)) {
    ctx_.reset();
    return THROW_ERR_CRYPTO_INVALID_KEYLEN(env());
  }

  if (!ctx_.init(Cipher(), encrypt, key, iv)) {
    return ThrowCryptoError(env(), ERR_get_error(),
                            "Failed to initialize cipher");
  }
}

void CipherBase::Init(const char* cipher_type,
                      const ArrayBufferOrViewContents<unsigned char>& key_buf,
                      unsigned int auth_tag_len) {
  HandleScope scope(env()->isolate());
  MarkPopErrorOnReturn mark_pop_error_on_return;
  auto cipher = Cipher::FromName(cipher_type);
  if (!cipher) {
    return THROW_ERR_CRYPTO_UNKNOWN_CIPHER(env());
  }

  unsigned char key[EVP_MAX_KEY_LENGTH];
  unsigned char iv[EVP_MAX_IV_LENGTH];

  int key_len = EVP_BytesToKey(cipher,
                               EVP_md5(),
                               nullptr,
                               key_buf.data(),
                               key_buf.size(),
                               1,
                               key,
                               iv);
  CHECK_NE(key_len, 0);

  const int mode = cipher.getMode();
  if (kind_ == kCipher && (mode == EVP_CIPH_CTR_MODE ||
                           mode == EVP_CIPH_GCM_MODE ||
                           mode == EVP_CIPH_CCM_MODE)) {
    // Ignore the return value (i.e. possible exception) because we are
    // not calling back into JS anyway.
    ProcessEmitWarning(env(),
                       "Use Cipheriv for counter mode of %s",
                       cipher_type);
  }

  CommonInit(cipher_type,
             cipher,
             key,
             key_len,
             iv,
             cipher.getIvLength(),
             auth_tag_len);
}

void CipherBase::Init(const FunctionCallbackInfo<Value>& args) {
  CipherBase* cipher;
  ASSIGN_OR_RETURN_UNWRAP(&cipher, args.This());
  Environment* env = Environment::GetCurrent(args);

  CHECK_GE(args.Length(), 3);

  const Utf8Value cipher_type(args.GetIsolate(), args[0]);
  ArrayBufferOrViewContents<unsigned char> key_buf(args[1]);
  if (!key_buf.CheckSizeInt32())
    return THROW_ERR_OUT_OF_RANGE(env, "password is too large");

  // Don't assign to cipher->auth_tag_len_ directly; the value might not
  // represent a valid length at this point.
  unsigned int auth_tag_len;
  if (args[2]->IsUint32()) {
    auth_tag_len = args[2].As<Uint32>()->Value();
  } else {
    CHECK(args[2]->IsInt32() && args[2].As<Int32>()->Value() == -1);
    auth_tag_len = kNoAuthTagLength;
  }

  cipher->Init(*cipher_type, key_buf, auth_tag_len);
}

void CipherBase::InitIv(const char* cipher_type,
                        const ByteSource& key_buf,
                        const ArrayBufferOrViewContents<unsigned char>& iv_buf,
                        unsigned int auth_tag_len) {
  HandleScope scope(env()->isolate());
  MarkPopErrorOnReturn mark_pop_error_on_return;

  auto cipher = Cipher::FromName(cipher_type);
  if (!cipher) return THROW_ERR_CRYPTO_UNKNOWN_CIPHER(env());

  const int expected_iv_len = cipher.getIvLength();
  const bool has_iv = iv_buf.size() > 0;

  // Throw if no IV was passed and the cipher requires an IV
  if (!has_iv && expected_iv_len != 0)
    return THROW_ERR_CRYPTO_INVALID_IV(env());

  // Throw if an IV was passed which does not match the cipher's fixed IV length
  // static_cast<int> for the iv_buf.size() is safe because we've verified
  // prior that the value is not larger than INT_MAX.
  if (!cipher.isSupportedAuthenticatedMode() && has_iv &&
      static_cast<int>(iv_buf.size()) != expected_iv_len) {
    return THROW_ERR_CRYPTO_INVALID_IV(env());
  }

  if (cipher.getNid() == NID_chacha20_poly1305) {
    CHECK(has_iv);
    // Check for invalid IV lengths, since OpenSSL does not under some
    // conditions:
    //   https://www.openssl.org/news/secadv/20190306.txt.
    if (iv_buf.size() > 12)
      return THROW_ERR_CRYPTO_INVALID_IV(env());
  }

  CommonInit(
      cipher_type,
      cipher,
      key_buf.data<unsigned char>(),
      key_buf.size(),
      iv_buf.data(),
      iv_buf.size(),
      auth_tag_len);
}

void CipherBase::InitIv(const FunctionCallbackInfo<Value>& args) {
  CipherBase* cipher;
  ASSIGN_OR_RETURN_UNWRAP(&cipher, args.This());
  Environment* env = cipher->env();

  CHECK_GE(args.Length(), 4);

  const Utf8Value cipher_type(env->isolate(), args[0]);

  // The argument can either be a KeyObjectHandle or a byte source
  // (e.g. ArrayBuffer, TypedArray, etc). Whichever it is, grab the
  // raw bytes and proceed...
  const ByteSource key_buf = ByteSource::FromSecretKeyBytes(env, args[1]);

  if (key_buf.size() > INT_MAX) [[unlikely]] {
    return THROW_ERR_OUT_OF_RANGE(env, "key is too big");
  }

  ArrayBufferOrViewContents<unsigned char> iv_buf(
      !args[2]->IsNull() ? args[2] : Local<Value>());

  if (!iv_buf.CheckSizeInt32()) [[unlikely]] {
    return THROW_ERR_OUT_OF_RANGE(env, "iv is too big");
  }
  // Don't assign to cipher->auth_tag_len_ directly; the value might not
  // represent a valid length at this point.
  unsigned int auth_tag_len;
  if (args[3]->IsUint32()) {
    auth_tag_len = args[3].As<Uint32>()->Value();
  } else {
    CHECK(args[3]->IsInt32() && args[3].As<Int32>()->Value() == -1);
    auth_tag_len = kNoAuthTagLength;
  }

  cipher->InitIv(*cipher_type, key_buf, iv_buf, auth_tag_len);
}

bool CipherBase::InitAuthenticated(
    const char* cipher_type,
    int iv_len,
    unsigned int auth_tag_len) {
  CHECK(IsAuthenticatedMode());
  MarkPopErrorOnReturn mark_pop_error_on_return;

  if (!ctx_.setIvLength(iv_len)) {
    THROW_ERR_CRYPTO_INVALID_IV(env());
    return false;
  }

  const int mode = ctx_.getMode();
  if (mode == EVP_CIPH_GCM_MODE) {
    if (auth_tag_len != kNoAuthTagLength) {
      if (!IsValidGCMTagLength(auth_tag_len)) {
        THROW_ERR_CRYPTO_INVALID_AUTH_TAG(
          env(),
          "Invalid authentication tag length: %u",
          auth_tag_len);
        return false;
      }

      // Remember the given authentication tag length for later.
      auth_tag_len_ = auth_tag_len;
    }
  } else {
    if (auth_tag_len == kNoAuthTagLength) {
      // We treat ChaCha20-Poly1305 specially. Like GCM, the authentication tag
      // length defaults to 16 bytes when encrypting. Unlike GCM, the
      // authentication tag length also defaults to 16 bytes when decrypting,
      // whereas GCM would accept any valid authentication tag length.
      if (ctx_.getNid() == NID_chacha20_poly1305) {
        auth_tag_len = 16;
      } else {
        THROW_ERR_CRYPTO_INVALID_AUTH_TAG(
          env(), "authTagLength required for %s", cipher_type);
        return false;
      }
    }

    // TODO(tniessen) Support CCM decryption in FIPS mode

#if OPENSSL_VERSION_MAJOR >= 3
    if (mode == EVP_CIPH_CCM_MODE && kind_ == kDecipher &&
        EVP_default_properties_is_fips_enabled(nullptr)) {
#else
    if (mode == EVP_CIPH_CCM_MODE && kind_ == kDecipher && FIPS_mode()) {
#endif
      THROW_ERR_CRYPTO_UNSUPPORTED_OPERATION(env(),
          "CCM encryption not supported in FIPS mode");
      return false;
    }

    // Tell OpenSSL about the desired length.
    if (!ctx_.setAeadTagLength(auth_tag_len)) {
      THROW_ERR_CRYPTO_INVALID_AUTH_TAG(
          env(), "Invalid authentication tag length: %u", auth_tag_len);
      return false;
    }

    // Remember the given authentication tag length for later.
    auth_tag_len_ = auth_tag_len;

    if (mode == EVP_CIPH_CCM_MODE) {
      // Restrict the message length to min(INT_MAX, 2^(8*(15-iv_len))-1) bytes.
      CHECK(iv_len >= 7 && iv_len <= 13);
      max_message_size_ = INT_MAX;
      if (iv_len == 12) max_message_size_ = 16777215;
      if (iv_len == 13) max_message_size_ = 65535;
    }
  }

  return true;
}

bool CipherBase::CheckCCMMessageLength(int message_len) {
  CHECK(ctx_);
  CHECK(ctx_.getMode() == EVP_CIPH_CCM_MODE);

  if (message_len > max_message_size_) {
    THROW_ERR_CRYPTO_INVALID_MESSAGELEN(env());
    return false;
  }

  return true;
}

bool CipherBase::IsAuthenticatedMode() const {
  // Check if this cipher operates in an AEAD mode that we support.
  CHECK(ctx_);
  return ncrypto::Cipher::FromCtx(ctx_).isSupportedAuthenticatedMode();
}

void CipherBase::GetAuthTag(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CipherBase* cipher;
  ASSIGN_OR_RETURN_UNWRAP(&cipher, args.This());

  // Only callable after Final and if encrypting.
  if (cipher->ctx_ ||
      cipher->kind_ != kCipher ||
      cipher->auth_tag_len_ == kNoAuthTagLength) {
    return;
  }

  args.GetReturnValue().Set(
      Buffer::Copy(env, cipher->auth_tag_, cipher->auth_tag_len_)
          .FromMaybe(Local<Value>()));
}

void CipherBase::SetAuthTag(const FunctionCallbackInfo<Value>& args) {
  CipherBase* cipher;
  ASSIGN_OR_RETURN_UNWRAP(&cipher, args.This());
  Environment* env = Environment::GetCurrent(args);

  if (!cipher->ctx_ ||
      !cipher->IsAuthenticatedMode() ||
      cipher->kind_ != kDecipher ||
      cipher->auth_tag_state_ != kAuthTagUnknown) {
    return args.GetReturnValue().Set(false);
  }

  ArrayBufferOrViewContents<char> auth_tag(args[0]);
  if (!auth_tag.CheckSizeInt32()) [[unlikely]] {
    return THROW_ERR_OUT_OF_RANGE(env, "buffer is too big");
  }
  unsigned int tag_len = auth_tag.size();

  const int mode = cipher->ctx_.getMode();
  bool is_valid;
  if (mode == EVP_CIPH_GCM_MODE) {
    // Restrict GCM tag lengths according to NIST 800-38d, page 9.
    is_valid = (cipher->auth_tag_len_ == kNoAuthTagLength ||
                cipher->auth_tag_len_ == tag_len) &&
               IsValidGCMTagLength(tag_len);
  } else {
    // At this point, the tag length is already known and must match the
    // length of the given authentication tag.
    CHECK(Cipher::FromCtx(cipher->ctx_).isSupportedAuthenticatedMode());
    CHECK_NE(cipher->auth_tag_len_, kNoAuthTagLength);
    is_valid = cipher->auth_tag_len_ == tag_len;
  }

  if (!is_valid) {
    return THROW_ERR_CRYPTO_INVALID_AUTH_TAG(
      env, "Invalid authentication tag length: %u", tag_len);
  }

  if (mode == EVP_CIPH_GCM_MODE && cipher->auth_tag_len_ == kNoAuthTagLength &&
      tag_len != 16 && env->EmitProcessEnvWarning()) {
    if (ProcessEmitDeprecationWarning(
            env,
            "Using AES-GCM authentication tags of less than 128 bits without "
            "specifying the authTagLength option when initializing decryption "
            "is deprecated.",
            "DEP0182")
            .IsNothing())
      return;
  }

  cipher->auth_tag_len_ = tag_len;
  cipher->auth_tag_state_ = kAuthTagKnown;
  CHECK_LE(cipher->auth_tag_len_, sizeof(cipher->auth_tag_));

  memset(cipher->auth_tag_, 0, sizeof(cipher->auth_tag_));
  auth_tag.CopyTo(cipher->auth_tag_, cipher->auth_tag_len_);

  args.GetReturnValue().Set(true);
}

bool CipherBase::MaybePassAuthTagToOpenSSL() {
  if (auth_tag_state_ == kAuthTagKnown) {
    ncrypto::Buffer<const char> buffer{
        .data = auth_tag_,
        .len = auth_tag_len_,
    };
    if (!ctx_.setAeadTag(buffer)) {
      return false;
    }
    auth_tag_state_ = kAuthTagPassedToOpenSSL;
  }
  return true;
}

bool CipherBase::SetAAD(
    const ArrayBufferOrViewContents<unsigned char>& data,
    int plaintext_len) {
  if (!ctx_ || !IsAuthenticatedMode())
    return false;
  MarkPopErrorOnReturn mark_pop_error_on_return;

  int outlen;
  const int mode = ctx_.getMode();

  // When in CCM mode, we need to set the authentication tag and the plaintext
  // length in advance.
  if (mode == EVP_CIPH_CCM_MODE) {
    if (plaintext_len < 0) {
      THROW_ERR_MISSING_ARGS(env(),
          "options.plaintextLength required for CCM mode with AAD");
      return false;
    }

    if (!CheckCCMMessageLength(plaintext_len))
      return false;

    if (kind_ == kDecipher) {
      if (!MaybePassAuthTagToOpenSSL())
        return false;
    }

    ncrypto::Buffer<const unsigned char> buffer{
        .data = nullptr,
        .len = static_cast<size_t>(plaintext_len),
    };
    // Specify the plaintext length.
    if (!ctx_.update(buffer, nullptr, &outlen)) {
      return false;
    }
  }

  ncrypto::Buffer<const unsigned char> buffer{
      .data = data.data(),
      .len = data.size(),
  };
  return ctx_.update(buffer, nullptr, &outlen);
}

void CipherBase::SetAAD(const FunctionCallbackInfo<Value>& args) {
  CipherBase* cipher;
  ASSIGN_OR_RETURN_UNWRAP(&cipher, args.This());
  Environment* env = Environment::GetCurrent(args);

  CHECK_EQ(args.Length(), 2);
  CHECK(args[1]->IsInt32());
  int plaintext_len = args[1].As<Int32>()->Value();
  ArrayBufferOrViewContents<unsigned char> buf(args[0]);

  if (!buf.CheckSizeInt32()) [[unlikely]] {
    return THROW_ERR_OUT_OF_RANGE(env, "buffer is too big");
  }
  args.GetReturnValue().Set(cipher->SetAAD(buf, plaintext_len));
}

CipherBase::UpdateResult CipherBase::Update(
    const char* data,
    size_t len,
    std::unique_ptr<BackingStore>* out) {
  if (!ctx_ || len > INT_MAX)
    return kErrorState;
  MarkPopErrorOnReturn mark_pop_error_on_return;

  const int mode = ctx_.getMode();

  if (mode == EVP_CIPH_CCM_MODE && !CheckCCMMessageLength(len))
    return kErrorMessageSize;

  // Pass the authentication tag to OpenSSL if possible. This will only happen
  // once, usually on the first update.
  if (kind_ == kDecipher && IsAuthenticatedMode())
    CHECK(MaybePassAuthTagToOpenSSL());

  const int block_size = ctx_.getBlockSize();
  CHECK_GT(block_size, 0);
  if (len + block_size > INT_MAX) return kErrorState;
  int buf_len = len + block_size;

  ncrypto::Buffer<const unsigned char> buffer = {
      .data = reinterpret_cast<const unsigned char*>(data),
      .len = len,
  };
  if (kind_ == kCipher && mode == EVP_CIPH_WRAP_MODE &&
      !ctx_.update(buffer, nullptr, &buf_len)) {
    return kErrorState;
  }

  {
    NoArrayBufferZeroFillScope no_zero_fill_scope(env()->isolate_data());
    *out = ArrayBuffer::NewBackingStore(env()->isolate(), buf_len);
  }

  buffer = {
      .data = reinterpret_cast<const unsigned char*>(data),
      .len = len,
  };

  bool r = ctx_.update(
      buffer, static_cast<unsigned char*>((*out)->Data()), &buf_len);

  CHECK_LE(static_cast<size_t>(buf_len), (*out)->ByteLength());
  if (buf_len == 0) {
    *out = ArrayBuffer::NewBackingStore(env()->isolate(), 0);
  } else if (static_cast<size_t>(buf_len) != (*out)->ByteLength()) {
    std::unique_ptr<BackingStore> old_out = std::move(*out);
    *out = ArrayBuffer::NewBackingStore(env()->isolate(), buf_len);
    memcpy(static_cast<char*>((*out)->Data()),
           static_cast<char*>(old_out->Data()),
           buf_len);
  }

  // When in CCM mode, EVP_CipherUpdate will fail if the authentication tag is
  // invalid. In that case, remember the error and throw in final().
  if (!r && kind_ == kDecipher && mode == EVP_CIPH_CCM_MODE) {
    pending_auth_failed_ = true;
    return kSuccess;
  }
  return r == 1 ? kSuccess : kErrorState;
}

void CipherBase::Update(const FunctionCallbackInfo<Value>& args) {
  Decode<CipherBase>(args, [](CipherBase* cipher,
                              const FunctionCallbackInfo<Value>& args,
                              const char* data, size_t size) {
    std::unique_ptr<BackingStore> out;
    Environment* env = Environment::GetCurrent(args);

    if (size > INT_MAX) [[unlikely]] {
      return THROW_ERR_OUT_OF_RANGE(env, "data is too long");
    }
    UpdateResult r = cipher->Update(data, size, &out);

    if (r != kSuccess) {
      if (r == kErrorState) {
        ThrowCryptoError(env, ERR_get_error(),
                         "Trying to add data in unsupported state");
      }
      return;
    }

    Local<ArrayBuffer> ab = ArrayBuffer::New(env->isolate(), std::move(out));
    args.GetReturnValue().Set(
        Buffer::New(env, ab, 0, ab->ByteLength()).FromMaybe(Local<Value>()));
  });
}

bool CipherBase::SetAutoPadding(bool auto_padding) {
  if (!ctx_)
    return false;
  MarkPopErrorOnReturn mark_pop_error_on_return;
  return ctx_.setPadding(auto_padding);
}

void CipherBase::SetAutoPadding(const FunctionCallbackInfo<Value>& args) {
  CipherBase* cipher;
  ASSIGN_OR_RETURN_UNWRAP(&cipher, args.This());

  bool b = cipher->SetAutoPadding(args.Length() < 1 || args[0]->IsTrue());
  args.GetReturnValue().Set(b);  // Possibly report invalid state failure
}

bool CipherBase::Final(std::unique_ptr<BackingStore>* out) {
  if (!ctx_)
    return false;

  const int mode = ctx_.getMode();

  {
    NoArrayBufferZeroFillScope no_zero_fill_scope(env()->isolate_data());
    *out = ArrayBuffer::NewBackingStore(
        env()->isolate(), static_cast<size_t>(ctx_.getBlockSize()));
  }

  if (kind_ == kDecipher &&
      Cipher::FromCtx(ctx_).isSupportedAuthenticatedMode()) {
    MaybePassAuthTagToOpenSSL();
  }

  // OpenSSL v1.x doesn't verify the presence of the auth tag so do
  // it ourselves, see https://github.com/nodejs/node/issues/45874.
  if (OPENSSL_VERSION_NUMBER < 0x30000000L && kind_ == kDecipher &&
      NID_chacha20_poly1305 == ctx_.getNid() &&
      auth_tag_state_ != kAuthTagPassedToOpenSSL) {
    return false;
  }

  // In CCM mode, final() only checks whether authentication failed in update().
  // EVP_CipherFinal_ex must not be called and will fail.
  bool ok;
  if (kind_ == kDecipher && mode == EVP_CIPH_CCM_MODE) {
    ok = !pending_auth_failed_;
    *out = ArrayBuffer::NewBackingStore(env()->isolate(), 0);
  } else {
    int out_len = (*out)->ByteLength();
    ok = ctx_.update(
        {}, static_cast<unsigned char*>((*out)->Data()), &out_len, true);

    CHECK_LE(static_cast<size_t>(out_len), (*out)->ByteLength());
    if (out_len == 0) {
      *out = ArrayBuffer::NewBackingStore(env()->isolate(), 0);
    } else if (static_cast<size_t>(out_len) != (*out)->ByteLength()) {
      std::unique_ptr<BackingStore> old_out = std::move(*out);
      *out = ArrayBuffer::NewBackingStore(env()->isolate(), out_len);
      memcpy(static_cast<char*>((*out)->Data()),
             static_cast<char*>(old_out->Data()),
             out_len);
    }

    if (ok && kind_ == kCipher && IsAuthenticatedMode()) {
      // In GCM mode, the authentication tag length can be specified in advance,
      // but defaults to 16 bytes when encrypting. In CCM and OCB mode, it must
      // always be given by the user.
      if (auth_tag_len_ == kNoAuthTagLength) {
        CHECK(mode == EVP_CIPH_GCM_MODE);
        auth_tag_len_ = sizeof(auth_tag_);
      }
      ok = ctx_.getAeadTag(auth_tag_len_,
                           reinterpret_cast<unsigned char*>(auth_tag_));
    }
  }

  ctx_.reset();

  return ok;
}

void CipherBase::Final(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  CipherBase* cipher;
  ASSIGN_OR_RETURN_UNWRAP(&cipher, args.This());
  if (cipher->ctx_ == nullptr)
    return THROW_ERR_CRYPTO_INVALID_STATE(env);

  std::unique_ptr<BackingStore> out;

  // Check IsAuthenticatedMode() first, Final() destroys the EVP_CIPHER_CTX.
  const bool is_auth_mode = cipher->IsAuthenticatedMode();
  bool r = cipher->Final(&out);

  if (!r) {
    const char* msg = is_auth_mode
                          ? "Unsupported state or unable to authenticate data"
                          : "Unsupported state";

    return ThrowCryptoError(env, ERR_get_error(), msg);
  }

  Local<ArrayBuffer> ab = ArrayBuffer::New(env->isolate(), std::move(out));
  args.GetReturnValue().Set(
      Buffer::New(env, ab, 0, ab->ByteLength()).FromMaybe(Local<Value>()));
}

template <PublicKeyCipher::Operation operation,
          PublicKeyCipher::EVP_PKEY_cipher_init_t EVP_PKEY_cipher_init,
          PublicKeyCipher::EVP_PKEY_cipher_t EVP_PKEY_cipher>
bool PublicKeyCipher::Cipher(
    Environment* env,
    const EVPKeyPointer& pkey,
    int padding,
    const EVP_MD* digest,
    const ArrayBufferOrViewContents<unsigned char>& oaep_label,
    const ArrayBufferOrViewContents<unsigned char>& data,
    std::unique_ptr<BackingStore>* out) {
  EVPKeyCtxPointer ctx = pkey.newCtx();
  if (!ctx)
    return false;
  if (EVP_PKEY_cipher_init(ctx.get()) <= 0)
    return false;
  if (EVP_PKEY_CTX_set_rsa_padding(ctx.get(), padding) <= 0)
    return false;

  if (digest != nullptr) {
    if (EVP_PKEY_CTX_set_rsa_oaep_md(ctx.get(), digest) <= 0)
      return false;
  }

  if (!SetRsaOaepLabel(ctx, oaep_label.ToByteSource())) return false;

  size_t out_len = 0;
  if (EVP_PKEY_cipher(
          ctx.get(),
          nullptr,
          &out_len,
          data.data(),
          data.size()) <= 0) {
    return false;
  }

  {
    NoArrayBufferZeroFillScope no_zero_fill_scope(env->isolate_data());
    *out = ArrayBuffer::NewBackingStore(env->isolate(), out_len);
  }

  if (EVP_PKEY_cipher(
          ctx.get(),
          static_cast<unsigned char*>((*out)->Data()),
          &out_len,
          data.data(),
          data.size()) <= 0) {
    return false;
  }

  CHECK_LE(out_len, (*out)->ByteLength());
  if (out_len == 0) {
    *out = ArrayBuffer::NewBackingStore(env->isolate(), 0);
  } else if (out_len != (*out)->ByteLength()) {
    std::unique_ptr<BackingStore> old_out = std::move(*out);
    *out = ArrayBuffer::NewBackingStore(env->isolate(), out_len);
    memcpy(static_cast<char*>((*out)->Data()),
           static_cast<char*>(old_out->Data()),
           out_len);
  }

  return true;
}

template <PublicKeyCipher::Operation operation,
          PublicKeyCipher::EVP_PKEY_cipher_init_t EVP_PKEY_cipher_init,
          PublicKeyCipher::EVP_PKEY_cipher_t EVP_PKEY_cipher>
void PublicKeyCipher::Cipher(const FunctionCallbackInfo<Value>& args) {
  MarkPopErrorOnReturn mark_pop_error_on_return;
  Environment* env = Environment::GetCurrent(args);

  unsigned int offset = 0;
  auto data = KeyObjectData::GetPublicOrPrivateKeyFromJs(args, &offset);
  if (!data) return;
  const auto& pkey = data.GetAsymmetricKey();
  if (!pkey)
    return;

  ArrayBufferOrViewContents<unsigned char> buf(args[offset]);
  if (!buf.CheckSizeInt32()) [[unlikely]] {
    return THROW_ERR_OUT_OF_RANGE(env, "buffer is too long");
  }
  uint32_t padding;
  if (!args[offset + 1]->Uint32Value(env->context()).To(&padding)) return;

  if (EVP_PKEY_cipher == EVP_PKEY_decrypt &&
      operation == PublicKeyCipher::kPrivate && padding == RSA_PKCS1_PADDING) {
    EVPKeyCtxPointer ctx = pkey.newCtx();
    CHECK(ctx);

    if (EVP_PKEY_decrypt_init(ctx.get()) <= 0) {
      return ThrowCryptoError(env, ERR_get_error());
    }

    int rsa_pkcs1_implicit_rejection =
        EVP_PKEY_CTX_ctrl_str(ctx.get(), "rsa_pkcs1_implicit_rejection", "1");
    // From the doc -2 means that the option is not supported.
    // The default for the option is enabled and if it has been
    // specifically disabled we want to respect that so we will
    // not throw an error if the option is supported regardless
    // of how it is set. The call to set the value
    // will not affect what is used since a different context is
    // used in the call if the option is supported
    if (rsa_pkcs1_implicit_rejection <= 0) {
      return THROW_ERR_INVALID_ARG_VALUE(
          env,
          "RSA_PKCS1_PADDING is no longer supported for private decryption");
    }
  }

  const EVP_MD* digest = nullptr;
  if (args[offset + 2]->IsString()) {
    const Utf8Value oaep_str(env->isolate(), args[offset + 2]);
    digest = EVP_get_digestbyname(*oaep_str);
    if (digest == nullptr)
      return THROW_ERR_OSSL_EVP_INVALID_DIGEST(env);
  }

  ArrayBufferOrViewContents<unsigned char> oaep_label(
      !args[offset + 3]->IsUndefined() ? args[offset + 3] : Local<Value>());
  if (!oaep_label.CheckSizeInt32()) [[unlikely]] {
    return THROW_ERR_OUT_OF_RANGE(env, "oaepLabel is too big");
  }
  std::unique_ptr<BackingStore> out;
  if (!Cipher<operation, EVP_PKEY_cipher_init, EVP_PKEY_cipher>(
          env, pkey, padding, digest, oaep_label, buf, &out)) {
    return ThrowCryptoError(env, ERR_get_error());
  }

  Local<ArrayBuffer> ab = ArrayBuffer::New(env->isolate(), std::move(out));
  args.GetReturnValue().Set(
      Buffer::New(env, ab, 0, ab->ByteLength()).FromMaybe(Local<Value>()));
}

}  // namespace crypto
}  // namespace node
