#include "crypto/crypto_keys.h"
#include "crypto/crypto_common.h"
#include "crypto/crypto_dsa.h"
#include "crypto/crypto_ec.h"
#include "crypto/crypto_dh.h"
#include "crypto/crypto_rsa.h"
#include "crypto/crypto_util.h"
#include "async_wrap-inl.h"
#include "base_object-inl.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "node.h"
#include "node_buffer.h"
#include "string_bytes.h"
#include "threadpoolwork-inl.h"
#include "util-inl.h"
#include "v8.h"

namespace node {

using ncrypto::BIOPointer;
using ncrypto::ECKeyPointer;
using ncrypto::EVPKeyCtxPointer;
using ncrypto::EVPKeyPointer;
using ncrypto::MarkPopErrorOnReturn;
using ncrypto::PKCS8Pointer;
using v8::Array;
using v8::Context;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Int32;
using v8::Isolate;
using v8::Just;
using v8::JustVoid;
using v8::Local;
using v8::Maybe;
using v8::MaybeLocal;
using v8::NewStringType;
using v8::Nothing;
using v8::Number;
using v8::Object;
using v8::String;
using v8::Uint32;
using v8::Undefined;
using v8::Value;

namespace crypto {
namespace {
Maybe<EVPKeyPointer::AsymmetricKeyEncodingConfig> GetKeyFormatAndTypeFromJs(
    const FunctionCallbackInfo<Value>& args,
    unsigned int* offset,
    KeyEncodingContext context) {
  EVPKeyPointer::AsymmetricKeyEncodingConfig config;
  // During key pair generation, it is possible not to specify a key encoding,
  // which will lead to a key object being returned.
  if (args[*offset]->IsUndefined()) {
    CHECK_EQ(context, kKeyContextGenerate);
    CHECK(args[*offset + 1]->IsUndefined());
    config.output_key_object = true;
  } else {
    config.output_key_object = false;

    CHECK(args[*offset]->IsInt32());
    config.format = static_cast<EVPKeyPointer::PKFormatType>(
        args[*offset].As<Int32>()->Value());

    if (args[*offset + 1]->IsInt32()) {
      config.type = static_cast<EVPKeyPointer::PKEncodingType>(
          args[*offset + 1].As<Int32>()->Value());
    } else {
      CHECK((context == kKeyContextInput &&
             config.format == EVPKeyPointer::PKFormatType::PEM) ||
            (context == kKeyContextGenerate &&
             config.format == EVPKeyPointer::PKFormatType::JWK));
      CHECK(args[*offset + 1]->IsNullOrUndefined());
      config.type = EVPKeyPointer::PKEncodingType::PKCS1;
    }
  }

  *offset += 2;
  return Just(config);
}

MaybeLocal<Value> BIOToStringOrBuffer(
    Environment* env,
    const BIOPointer& bio,
    const EVPKeyPointer::AsymmetricKeyEncodingConfig& config) {
  BUF_MEM* bptr = bio;
  if (config.format == EVPKeyPointer::PKFormatType::PEM) {
    // PEM is an ASCII format, so we will return it as a string.
    return String::NewFromUtf8(
               env->isolate(), bptr->data, NewStringType::kNormal, bptr->length)
        .FromMaybe(Local<Value>());
  }

  CHECK_EQ(config.format, EVPKeyPointer::PKFormatType::DER);
  // DER is binary, return it as a buffer.
  return Buffer::Copy(env, bptr->data, bptr->length).FromMaybe(Local<Value>());
}

MaybeLocal<Value> WritePrivateKey(
    Environment* env,
    const EVPKeyPointer& pkey,
    const EVPKeyPointer::PrivateKeyEncodingConfig& config) {
  CHECK(pkey);
  auto res = pkey.writePrivateKey(config);
  if (res) {
    return BIOToStringOrBuffer(env, std::move(res.value), config);
  }

  ThrowCryptoError(
      env, res.openssl_error.value_or(0), "Failed to encode private key");
  return MaybeLocal<Value>();
}

MaybeLocal<Value> WritePublicKey(
    Environment* env,
    const EVPKeyPointer& pkey,
    const EVPKeyPointer::PublicKeyEncodingConfig& config) {
  CHECK(pkey);
  auto res = pkey.writePublicKey(config);
  if (res) {
    return BIOToStringOrBuffer(env, res.value, config);
  }

  ThrowCryptoError(
      env, res.openssl_error.value_or(0), "Failed to encode public key");
  return MaybeLocal<Value>();
}

Maybe<void> ExportJWKSecretKey(Environment* env,
                               const KeyObjectData& key,
                               Local<Object> target) {
  CHECK_EQ(key.GetKeyType(), kKeyTypeSecret);

  Local<Value> error;
  Local<Value> raw;
  MaybeLocal<Value> key_data = StringBytes::Encode(env->isolate(),
                                                   key.GetSymmetricKey(),
                                                   key.GetSymmetricKeySize(),
                                                   BASE64URL,
                                                   &error);
  if (!key_data.ToLocal(&raw)) {
    CHECK(!error.IsEmpty());
    env->isolate()->ThrowException(error);
    return Nothing<void>();
  }

  if (target->Set(
          env->context(),
          env->jwk_kty_string(),
          env->jwk_oct_string()).IsNothing() ||
      target->Set(
          env->context(),
          env->jwk_k_string(),
          raw).IsNothing()) {
    return Nothing<void>();
  }

  return JustVoid();
}

KeyObjectData ImportJWKSecretKey(Environment* env, Local<Object> jwk) {
  Local<Value> key;
  if (!jwk->Get(env->context(), env->jwk_k_string()).ToLocal(&key) ||
      !key->IsString()) {
    THROW_ERR_CRYPTO_INVALID_JWK(env, "Invalid JWK secret key format");
    return {};
  }

  static_assert(String::kMaxLength <= INT_MAX);
  return KeyObjectData::CreateSecret(
      ByteSource::FromEncodedString(env, key.As<String>()));
}

Maybe<void> ExportJWKAsymmetricKey(Environment* env,
                                   const KeyObjectData& key,
                                   Local<Object> target,
                                   bool handleRsaPss) {
  switch (key.GetAsymmetricKey().id()) {
    case EVP_PKEY_RSA_PSS: {
      if (handleRsaPss) return ExportJWKRsaKey(env, key, target);
      break;
    }
    case EVP_PKEY_RSA:
      return ExportJWKRsaKey(env, key, target);
    case EVP_PKEY_EC:
      return ExportJWKEcKey(env, key, target);
    case EVP_PKEY_ED25519:
      // Fall through
    case EVP_PKEY_ED448:
      // Fall through
    case EVP_PKEY_X25519:
      // Fall through
    case EVP_PKEY_X448:
      return ExportJWKEdKey(env, key, target);
  }
  THROW_ERR_CRYPTO_JWK_UNSUPPORTED_KEY_TYPE(env);
  return Nothing<void>();
}

KeyObjectData ImportJWKAsymmetricKey(Environment* env,
                                     Local<Object> jwk,
                                     std::string_view kty,
                                     const FunctionCallbackInfo<Value>& args,
                                     unsigned int offset) {
  if (kty == "RSA") {
    return ImportJWKRsaKey(env, jwk, args, offset);
  } else if (kty == "EC") {
    return ImportJWKEcKey(env, jwk, args, offset);
  }

  THROW_ERR_CRYPTO_INVALID_JWK(
      env, "%s is not a supported JWK key type", kty.data());
  return {};
}

Maybe<void> GetSecretKeyDetail(Environment* env,
                               const KeyObjectData& key,
                               Local<Object> target) {
  // For the secret key detail, all we care about is the length,
  // converted to bits.
  size_t length = key.GetSymmetricKeySize() * CHAR_BIT;
  if (target
          ->Set(env->context(),
                env->length_string(),
                Number::New(env->isolate(), static_cast<double>(length)))
          .IsNothing()) {
    return Nothing<void>();
  }
  return JustVoid();
}

Maybe<void> GetAsymmetricKeyDetail(Environment* env,
                                   const KeyObjectData& key,
                                   Local<Object> target) {
  switch (key.GetAsymmetricKey().id()) {
    case EVP_PKEY_RSA:
      // Fall through
    case EVP_PKEY_RSA_PSS: return GetRsaKeyDetail(env, key, target);
    case EVP_PKEY_DSA: return GetDsaKeyDetail(env, key, target);
    case EVP_PKEY_EC: return GetEcKeyDetail(env, key, target);
    case EVP_PKEY_DH: return GetDhKeyDetail(env, key, target);
  }
  THROW_ERR_CRYPTO_INVALID_KEYTYPE(env);
  return Nothing<void>();
}

KeyObjectData TryParsePrivateKey(
    Environment* env,
    const EVPKeyPointer::PrivateKeyEncodingConfig& config,
    const ncrypto::Buffer<const unsigned char>& buffer) {
  auto res = EVPKeyPointer::TryParsePrivateKey(config, buffer);
  if (res) {
    return KeyObjectData::CreateAsymmetric(KeyType::kKeyTypePrivate,
                                           std::move(res.value));
  }

  if (res.error.value() == EVPKeyPointer::PKParseError::NEED_PASSPHRASE) {
    THROW_ERR_MISSING_PASSPHRASE(env, "Passphrase required for encrypted key");
  } else {
    ThrowCryptoError(
        env, res.openssl_error.value_or(0), "Failed to read private key");
  }
  return {};
}

// This maps true to JustVoid and false to Nothing<void>().
static inline Maybe<void> NothingIfFalse(bool b) {
  return b ? JustVoid() : Nothing<void>();
}

Maybe<void> ExportJWKInner(Environment* env,
                           const KeyObjectData& key,
                           Local<Value> result,
                           bool handleRsaPss) {
  switch (key.GetKeyType()) {
    case kKeyTypeSecret:
      return ExportJWKSecretKey(env, key, result.As<Object>());
    case kKeyTypePublic:
      // Fall through
    case kKeyTypePrivate:
      return ExportJWKAsymmetricKey(
        env, key, result.As<Object>(), handleRsaPss);
    default:
      UNREACHABLE();
  }
}
}  // namespace

Maybe<void> KeyObjectData::ToEncodedPublicKey(
    Environment* env,
    const EVPKeyPointer::PublicKeyEncodingConfig& config,
    Local<Value>* out) {
  CHECK(key_type_ != KeyType::kKeyTypeSecret);
  if (config.output_key_object) {
    // Note that this has the downside of containing sensitive data of the
    // private key.
    return NothingIfFalse(
        KeyObjectHandle::Create(env, addRefWithType(KeyType::kKeyTypePublic))
            .ToLocal(out));
  } else if (config.format == EVPKeyPointer::PKFormatType::JWK) {
    *out = Object::New(env->isolate());
    return ExportJWKInner(
        env, addRefWithType(KeyType::kKeyTypePublic), *out, false);
  }

  return NothingIfFalse(
      WritePublicKey(env, GetAsymmetricKey(), config).ToLocal(out));
}

Maybe<void> KeyObjectData::ToEncodedPrivateKey(
    Environment* env,
    const EVPKeyPointer::PrivateKeyEncodingConfig& config,
    Local<Value>* out) {
  CHECK(key_type_ != KeyType::kKeyTypeSecret);
  if (config.output_key_object) {
    return NothingIfFalse(
        KeyObjectHandle::Create(env, addRefWithType(KeyType::kKeyTypePrivate))
            .ToLocal(out));
  } else if (config.format == EVPKeyPointer::PKFormatType::JWK) {
    *out = Object::New(env->isolate());
    return ExportJWKInner(
        env, addRefWithType(KeyType::kKeyTypePrivate), *out, false);
  }

  return NothingIfFalse(
      WritePrivateKey(env, GetAsymmetricKey(), config).ToLocal(out));
}

Maybe<EVPKeyPointer::PrivateKeyEncodingConfig>
KeyObjectData::GetPrivateKeyEncodingFromJs(
    const FunctionCallbackInfo<Value>& args,
    unsigned int* offset,
    KeyEncodingContext context) {
  Environment* env = Environment::GetCurrent(args);

  EVPKeyPointer::PrivateKeyEncodingConfig config;
  if (!GetKeyFormatAndTypeFromJs(args, offset, context).To(&config)) {
    return Nothing<EVPKeyPointer::PrivateKeyEncodingConfig>();
  }

  if (config.output_key_object) {
    if (context != kKeyContextInput)
      (*offset)++;
  } else {
    bool needs_passphrase = false;
    if (context != kKeyContextInput) {
      if (args[*offset]->IsString()) {
        Utf8Value cipher_name(env->isolate(), args[*offset]);
        config.cipher = EVP_get_cipherbyname(*cipher_name);
        if (config.cipher == nullptr) {
          THROW_ERR_CRYPTO_UNKNOWN_CIPHER(env);
          return Nothing<EVPKeyPointer::PrivateKeyEncodingConfig>();
        }
        needs_passphrase = true;
      } else {
        CHECK(args[*offset]->IsNullOrUndefined());
        config.cipher = nullptr;
      }
      (*offset)++;
    }

    if (IsAnyBufferSource(args[*offset])) {
      CHECK_IMPLIES(context != kKeyContextInput, config.cipher != nullptr);
      ArrayBufferOrViewContents<char> passphrase(args[*offset]);
      if (!passphrase.CheckSizeInt32()) [[unlikely]] {
        THROW_ERR_OUT_OF_RANGE(env, "passphrase is too big");
        return Nothing<EVPKeyPointer::PrivateKeyEncodingConfig>();
      }
      config.passphrase = passphrase.ToDataPointer();
    } else {
      CHECK(args[*offset]->IsNullOrUndefined() && !needs_passphrase);
    }
  }

  (*offset)++;
  return Just<EVPKeyPointer::PrivateKeyEncodingConfig>(std::move(config));
}

Maybe<EVPKeyPointer::PublicKeyEncodingConfig>
KeyObjectData::GetPublicKeyEncodingFromJs(
    const FunctionCallbackInfo<Value>& args,
    unsigned int* offset,
    KeyEncodingContext context) {
  return GetKeyFormatAndTypeFromJs(args, offset, context);
}

KeyObjectData KeyObjectData::GetPrivateKeyFromJs(
    const v8::FunctionCallbackInfo<v8::Value>& args,
    unsigned int* offset,
    bool allow_key_object) {
  if (args[*offset]->IsString() || IsAnyBufferSource(args[*offset])) {
    Environment* env = Environment::GetCurrent(args);
    auto key = ByteSource::FromStringOrBuffer(env, args[(*offset)++]);

    EVPKeyPointer::PrivateKeyEncodingConfig config;
    if (!GetPrivateKeyEncodingFromJs(args, offset, kKeyContextInput)
             .To(&config)) {
      return {};
    }

    return TryParsePrivateKey(
        env,
        config,
        ncrypto::Buffer<const unsigned char>{
            .data = reinterpret_cast<const unsigned char*>(key.data()),
            .len = key.size(),
        });
  }

  CHECK(args[*offset]->IsObject() && allow_key_object);
  KeyObjectHandle* key;
  ASSIGN_OR_RETURN_UNWRAP(&key, args[*offset].As<Object>(), KeyObjectData());
  CHECK_EQ(key->Data().GetKeyType(), kKeyTypePrivate);
  (*offset) += 4;
  return key->Data().addRef();
}

KeyObjectData KeyObjectData::GetPublicOrPrivateKeyFromJs(
    const FunctionCallbackInfo<Value>& args, unsigned int* offset) {
  if (IsAnyBufferSource(args[*offset])) {
    Environment* env = Environment::GetCurrent(args);
    ArrayBufferOrViewContents<char> data(args[(*offset)++]);
    if (!data.CheckSizeInt32()) [[unlikely]] {
      THROW_ERR_OUT_OF_RANGE(env, "keyData is too big");
      return {};
    }

    EVPKeyPointer::PrivateKeyEncodingConfig config;
    if (!KeyObjectData::GetPrivateKeyEncodingFromJs(
             args, offset, kKeyContextInput)
             .To(&config)) {
      return {};
    }

    ncrypto::Buffer<const unsigned char> buffer = {
        .data = reinterpret_cast<const unsigned char*>(data.data()),
        .len = data.size(),
    };

    if (config.format == EVPKeyPointer::PKFormatType::PEM) {
      // For PEM, we can easily determine whether it is a public or private key
      // by looking for the respective PEM tags.
      auto res = EVPKeyPointer::TryParsePublicKeyPEM(buffer);
      if (res) {
        return CreateAsymmetric(kKeyTypePublic, std::move(res.value));
      }

      if (res.error.value() == EVPKeyPointer::PKParseError::NOT_RECOGNIZED) {
        return TryParsePrivateKey(env, config, buffer);
      }
      ThrowCryptoError(
          env, res.openssl_error.value_or(0), "Failed to read asymmetric key");
      return {};
    }

    // For DER, the type determines how to parse it. SPKI, PKCS#8 and SEC1 are
    // easy, but PKCS#1 can be a public key or a private key.
    static const auto is_public = [](const auto& config,
                                     const auto& buffer) -> bool {
      switch (config.type) {
        case EVPKeyPointer::PKEncodingType::PKCS1:
          return !EVPKeyPointer::IsRSAPrivateKey(buffer);
        case EVPKeyPointer::PKEncodingType::SPKI:
          return true;
        case EVPKeyPointer::PKEncodingType::PKCS8:
          return false;
        case EVPKeyPointer::PKEncodingType::SEC1:
          return false;
        default:
          UNREACHABLE("Invalid key encoding type");
      }
    };

    if (is_public(config, buffer)) {
      auto res = EVPKeyPointer::TryParsePublicKey(config, buffer);
      if (res) {
        return CreateAsymmetric(KeyType::kKeyTypePublic, std::move(res.value));
      }

      ThrowCryptoError(
          env, res.openssl_error.value_or(0), "Failed to read asymmetric key");
      return {};
    }

    return TryParsePrivateKey(env, config, buffer);
  }

  CHECK(args[*offset]->IsObject());
  KeyObjectHandle* key =
      BaseObject::Unwrap<KeyObjectHandle>(args[*offset].As<Object>());
  CHECK_NOT_NULL(key);
  CHECK_NE(key->Data().GetKeyType(), kKeyTypeSecret);
  (*offset) += 4;
  return key->Data().addRef();
}

KeyObjectData KeyObjectData::GetParsedKey(KeyType type,
                                          Environment* env,
                                          EVPKeyPointer&& pkey,
                                          ParseKeyResult ret,
                                          const char* default_msg) {
  switch (ret) {
    case ParseKeyResult::kParseKeyOk: {
      return CreateAsymmetric(type, std::move(pkey));
    }
    case ParseKeyResult::kParseKeyNeedPassphrase: {
      THROW_ERR_MISSING_PASSPHRASE(env,
                                   "Passphrase required for encrypted key");
      return {};
    }
    default: {
      ThrowCryptoError(env, ERR_get_error(), default_msg);
      return {};
    }
  }
}

KeyObjectData::KeyObjectData(std::nullptr_t)
    : key_type_(KeyType::kKeyTypeSecret) {}

KeyObjectData::KeyObjectData(ByteSource symmetric_key)
    : key_type_(KeyType::kKeyTypeSecret),
      data_(std::make_shared<Data>(std::move(symmetric_key))) {}

KeyObjectData::KeyObjectData(KeyType type, EVPKeyPointer&& pkey)
    : key_type_(type), data_(std::make_shared<Data>(std::move(pkey))) {}

void KeyObjectData::MemoryInfo(MemoryTracker* tracker) const {
  if (!*this) return;
  switch (GetKeyType()) {
    case kKeyTypeSecret: {
      if (data_->symmetric_key) {
        tracker->TrackFieldWithSize("symmetric_key",
                                    data_->symmetric_key.size());
      }
      break;
    }
    case kKeyTypePrivate:
      // Fall through
    case kKeyTypePublic: {
      if (data_->asymmetric_key) {
        tracker->TrackFieldWithSize(
            "key",
            kSizeOf_EVP_PKEY + data_->asymmetric_key.rawPublicKeySize() +
                data_->asymmetric_key.rawPrivateKeySize());
      }
      break;
    }
    default:
      UNREACHABLE();
  }
}

Mutex& KeyObjectData::mutex() const {
  if (!mutex_) mutex_ = std::make_shared<Mutex>();
  return *mutex_.get();
}

KeyObjectData KeyObjectData::CreateSecret(ByteSource key) {
  return KeyObjectData(std::move(key));
}

KeyObjectData KeyObjectData::CreateAsymmetric(KeyType key_type,
                                              EVPKeyPointer&& pkey) {
  CHECK(pkey);
  return KeyObjectData(key_type, std::move(pkey));
}

KeyType KeyObjectData::GetKeyType() const {
  CHECK(data_);
  return key_type_;
}

const EVPKeyPointer& KeyObjectData::GetAsymmetricKey() const {
  CHECK_NE(key_type_, kKeyTypeSecret);
  CHECK(data_);
  return data_->asymmetric_key;
}

const char* KeyObjectData::GetSymmetricKey() const {
  CHECK_EQ(key_type_, kKeyTypeSecret);
  CHECK(data_);
  return data_->symmetric_key.data<char>();
}

size_t KeyObjectData::GetSymmetricKeySize() const {
  CHECK_EQ(key_type_, kKeyTypeSecret);
  CHECK(data_);
  return data_->symmetric_key.size();
}

bool KeyObjectHandle::HasInstance(Environment* env, Local<Value> value) {
  Local<FunctionTemplate> t = env->crypto_key_object_handle_constructor();
  return !t.IsEmpty() && t->HasInstance(value);
}

v8::Local<v8::Function> KeyObjectHandle::Initialize(Environment* env) {
  Local<FunctionTemplate> templ = env->crypto_key_object_handle_constructor();
  if (templ.IsEmpty()) {
    Isolate* isolate = env->isolate();
    templ = NewFunctionTemplate(isolate, New);
    templ->InstanceTemplate()->SetInternalFieldCount(
        KeyObjectHandle::kInternalFieldCount);

    SetProtoMethod(isolate, templ, "init", Init);
    SetProtoMethodNoSideEffect(
        isolate, templ, "getSymmetricKeySize", GetSymmetricKeySize);
    SetProtoMethodNoSideEffect(
        isolate, templ, "getAsymmetricKeyType", GetAsymmetricKeyType);
    SetProtoMethodNoSideEffect(
        isolate, templ, "checkEcKeyData", CheckEcKeyData);
    SetProtoMethod(isolate, templ, "export", Export);
    SetProtoMethod(isolate, templ, "exportJwk", ExportJWK);
    SetProtoMethod(isolate, templ, "initECRaw", InitECRaw);
    SetProtoMethod(isolate, templ, "initEDRaw", InitEDRaw);
    SetProtoMethod(isolate, templ, "initJwk", InitJWK);
    SetProtoMethod(isolate, templ, "keyDetail", GetKeyDetail);
    SetProtoMethod(isolate, templ, "equals", Equals);

    env->set_crypto_key_object_handle_constructor(templ);
  }
  return templ->GetFunction(env->context()).ToLocalChecked();
}

void KeyObjectHandle::RegisterExternalReferences(
    ExternalReferenceRegistry* registry) {
  registry->Register(New);
  registry->Register(Init);
  registry->Register(GetSymmetricKeySize);
  registry->Register(GetAsymmetricKeyType);
  registry->Register(CheckEcKeyData);
  registry->Register(Export);
  registry->Register(ExportJWK);
  registry->Register(InitECRaw);
  registry->Register(InitEDRaw);
  registry->Register(InitJWK);
  registry->Register(GetKeyDetail);
  registry->Register(Equals);
}

MaybeLocal<Object> KeyObjectHandle::Create(Environment* env,
                                           const KeyObjectData& data) {
  Local<Object> obj;
  Local<Function> ctor = KeyObjectHandle::Initialize(env);
  CHECK(!env->crypto_key_object_handle_constructor().IsEmpty());
  if (!ctor->NewInstance(env->context(), 0, nullptr).ToLocal(&obj))
    return MaybeLocal<Object>();

  KeyObjectHandle* key = Unwrap<KeyObjectHandle>(obj);
  CHECK_NOT_NULL(key);
  key->data_ = data.addRef();
  return obj;
}

const KeyObjectData& KeyObjectHandle::Data() {
  return data_;
}

void KeyObjectHandle::New(const FunctionCallbackInfo<Value>& args) {
  CHECK(args.IsConstructCall());
  Environment* env = Environment::GetCurrent(args);
  new KeyObjectHandle(env, args.This());
}

KeyObjectHandle::KeyObjectHandle(Environment* env,
                                 Local<Object> wrap)
    : BaseObject(env, wrap) {
  MakeWeak();
}

void KeyObjectHandle::Init(const FunctionCallbackInfo<Value>& args) {
  KeyObjectHandle* key;
  ASSIGN_OR_RETURN_UNWRAP(&key, args.This());
  MarkPopErrorOnReturn mark_pop_error_on_return;

  CHECK(args[0]->IsInt32());
  KeyType type = static_cast<KeyType>(args[0].As<Uint32>()->Value());

  unsigned int offset;

  switch (type) {
  case kKeyTypeSecret: {
    CHECK_EQ(args.Length(), 2);
    ArrayBufferOrViewContents<char> buf(args[1]);
    key->data_ = KeyObjectData::CreateSecret(buf.ToCopy());
    break;
  }
  case kKeyTypePublic: {
    CHECK_EQ(args.Length(), 5);

    offset = 1;
    auto data = KeyObjectData::GetPublicOrPrivateKeyFromJs(args, &offset);
    if (!data) return;
    key->data_ = data.addRefWithType(kKeyTypePublic);
    break;
  }
  case kKeyTypePrivate: {
    CHECK_EQ(args.Length(), 5);
    offset = 1;
    if (auto data = KeyObjectData::GetPrivateKeyFromJs(args, &offset, false)) {
      key->data_ = std::move(data);
    }
    break;
  }
  default:
    UNREACHABLE();
  }
}

void KeyObjectHandle::InitJWK(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  KeyObjectHandle* key;
  ASSIGN_OR_RETURN_UNWRAP(&key, args.This());
  MarkPopErrorOnReturn mark_pop_error_on_return;

  // The argument must be a JavaScript object that we will inspect
  // to get the JWK properties from.
  CHECK(args[0]->IsObject());

  // Step one, Secret key or not?
  Local<Object> input = args[0].As<Object>();

  Local<Value> kty;
  if (!input->Get(env->context(), env->jwk_kty_string()).ToLocal(&kty) ||
      !kty->IsString()) {
    return THROW_ERR_CRYPTO_INVALID_JWK(env);
  }

  Utf8Value kty_string(env->isolate(), kty);

  if (kty_string == "oct") {
    // Secret key
    key->data_ = ImportJWKSecretKey(env, input);
    if (!key->data_) {
      // ImportJWKSecretKey is responsible for throwing an appropriate error
      return;
    }
  } else {
    key->data_ = ImportJWKAsymmetricKey(env, input, *kty_string, args, 1);
    if (!key->data_) {
      // ImportJWKAsymmetricKey is responsible for throwing an appropriate error
      return;
    }
  }

  args.GetReturnValue().Set(key->data_.GetKeyType());
}

void KeyObjectHandle::InitECRaw(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  KeyObjectHandle* key;
  ASSIGN_OR_RETURN_UNWRAP(&key, args.This());

  CHECK(args[0]->IsString());
  Utf8Value name(env->isolate(), args[0]);

  MarkPopErrorOnReturn mark_pop_error_on_return;

  int id = OBJ_txt2nid(*name);
  auto eckey = ECKeyPointer::NewByCurveName(id);
  if (!eckey)
    return args.GetReturnValue().Set(false);

  const auto group = eckey.getGroup();
  auto pub = ECDH::BufferToPoint(env, group, args[1]);

  if (!pub || !eckey || !eckey.setPublicKey(pub)) {
    return args.GetReturnValue().Set(false);
  }

  auto pkey = EVPKeyPointer::New();
  if (!pkey.assign(eckey)) {
    args.GetReturnValue().Set(false);
  }

  eckey.release();  // Release ownership of the key

  key->data_ = KeyObjectData::CreateAsymmetric(kKeyTypePublic, std::move(pkey));

  args.GetReturnValue().Set(true);
}

int GetOKPCurveFromName(const char* name) {
  int nid;
  if (strcmp(name, "Ed25519") == 0) {
    nid = EVP_PKEY_ED25519;
  } else if (strcmp(name, "Ed448") == 0) {
    nid = EVP_PKEY_ED448;
  } else if (strcmp(name, "X25519") == 0) {
    nid = EVP_PKEY_X25519;
  } else if (strcmp(name, "X448") == 0) {
    nid = EVP_PKEY_X448;
  } else {
    nid = NID_undef;
  }
  return nid;
}

void KeyObjectHandle::InitEDRaw(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  KeyObjectHandle* key;
  ASSIGN_OR_RETURN_UNWRAP(&key, args.This());

  CHECK(args[0]->IsString());
  Utf8Value name(env->isolate(), args[0]);

  ArrayBufferOrViewContents<unsigned char> key_data(args[1]);
  KeyType type = static_cast<KeyType>(args[2].As<Int32>()->Value());

  MarkPopErrorOnReturn mark_pop_error_on_return;

  typedef EVPKeyPointer (*new_key_fn)(
      int, const ncrypto::Buffer<const unsigned char>&);
  new_key_fn fn = type == kKeyTypePrivate ? EVPKeyPointer::NewRawPrivate
                                          : EVPKeyPointer::NewRawPublic;

  int id = GetOKPCurveFromName(*name);

  switch (id) {
    case EVP_PKEY_X25519:
    case EVP_PKEY_X448:
    case EVP_PKEY_ED25519:
    case EVP_PKEY_ED448: {
      auto pkey = fn(id,
                     ncrypto::Buffer<const unsigned char>{
                         .data = key_data.data(),
                         .len = key_data.size(),
                     });
      if (!pkey) {
        return args.GetReturnValue().Set(false);
      }
      key->data_ = KeyObjectData::CreateAsymmetric(type, std::move(pkey));
      CHECK(key->data_);
      break;
    }
    default:
      UNREACHABLE();
  }

  args.GetReturnValue().Set(true);
}

void KeyObjectHandle::Equals(const FunctionCallbackInfo<Value>& args) {
  KeyObjectHandle* self_handle;
  KeyObjectHandle* arg_handle;
  ASSIGN_OR_RETURN_UNWRAP(&self_handle, args.This());
  ASSIGN_OR_RETURN_UNWRAP(&arg_handle, args[0].As<Object>());
  const auto& key = self_handle->Data();
  const auto& key2 = arg_handle->Data();

  KeyType key_type = key.GetKeyType();
  CHECK_EQ(key_type, key2.GetKeyType());

  bool ret;
  switch (key_type) {
    case kKeyTypeSecret: {
      size_t size = key.GetSymmetricKeySize();
      if (size == key2.GetSymmetricKeySize()) {
        ret = CRYPTO_memcmp(
                  key.GetSymmetricKey(), key2.GetSymmetricKey(), size) == 0;
      } else {
        ret = false;
      }
      break;
    }
    case kKeyTypePublic:
    case kKeyTypePrivate: {
      EVP_PKEY* pkey = key.GetAsymmetricKey().get();
      EVP_PKEY* pkey2 = key2.GetAsymmetricKey().get();
#if OPENSSL_VERSION_MAJOR >= 3
      int ok = EVP_PKEY_eq(pkey, pkey2);
#else
      int ok = EVP_PKEY_cmp(pkey, pkey2);
#endif
      if (ok == -2) {
        Environment* env = Environment::GetCurrent(args);
        return THROW_ERR_CRYPTO_UNSUPPORTED_OPERATION(env);
      }
      ret = ok == 1;
      break;
    }
    default:
      UNREACHABLE("unsupported key type");
  }

  args.GetReturnValue().Set(ret);
}

void KeyObjectHandle::GetKeyDetail(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  KeyObjectHandle* key;
  ASSIGN_OR_RETURN_UNWRAP(&key, args.This());

  CHECK(args[0]->IsObject());

  const auto& data = key->Data();

  switch (data.GetKeyType()) {
    case kKeyTypeSecret:
      if (GetSecretKeyDetail(env, data, args[0].As<Object>()).IsNothing())
        return;
      break;
    case kKeyTypePublic:
      // Fall through
    case kKeyTypePrivate:
      if (GetAsymmetricKeyDetail(env, data, args[0].As<Object>()).IsNothing())
        return;
      break;
    default:
      UNREACHABLE();
  }

  args.GetReturnValue().Set(args[0]);
}

Local<Value> KeyObjectHandle::GetAsymmetricKeyType() const {
  switch (data_.GetAsymmetricKey().id()) {
    case EVP_PKEY_RSA:
      return env()->crypto_rsa_string();
    case EVP_PKEY_RSA_PSS:
      return env()->crypto_rsa_pss_string();
    case EVP_PKEY_DSA:
      return env()->crypto_dsa_string();
    case EVP_PKEY_DH:
      return env()->crypto_dh_string();
    case EVP_PKEY_EC:
      return env()->crypto_ec_string();
    case EVP_PKEY_ED25519:
      return env()->crypto_ed25519_string();
    case EVP_PKEY_ED448:
      return env()->crypto_ed448_string();
    case EVP_PKEY_X25519:
      return env()->crypto_x25519_string();
    case EVP_PKEY_X448:
      return env()->crypto_x448_string();
    default:
      return Undefined(env()->isolate());
  }
}

void KeyObjectHandle::GetAsymmetricKeyType(
    const FunctionCallbackInfo<Value>& args) {
  KeyObjectHandle* key;
  ASSIGN_OR_RETURN_UNWRAP(&key, args.This());

  args.GetReturnValue().Set(key->GetAsymmetricKeyType());
}

bool KeyObjectHandle::CheckEcKeyData() const {
  MarkPopErrorOnReturn mark_pop_error_on_return;

  const auto& key = data_.GetAsymmetricKey();
  EVPKeyCtxPointer ctx = key.newCtx();
  CHECK(ctx);
  CHECK_EQ(key.id(), EVP_PKEY_EC);

  if (data_.GetKeyType() == kKeyTypePrivate) {
    return EVP_PKEY_check(ctx.get()) == 1;
  }

#if OPENSSL_VERSION_MAJOR >= 3
  return EVP_PKEY_public_check_quick(ctx.get()) == 1;
#else
  return EVP_PKEY_public_check(ctx.get()) == 1;
#endif
}

void KeyObjectHandle::CheckEcKeyData(const FunctionCallbackInfo<Value>& args) {
  KeyObjectHandle* key;
  ASSIGN_OR_RETURN_UNWRAP(&key, args.This());

  args.GetReturnValue().Set(key->CheckEcKeyData());
}

void KeyObjectHandle::GetSymmetricKeySize(
    const FunctionCallbackInfo<Value>& args) {
  KeyObjectHandle* key;
  ASSIGN_OR_RETURN_UNWRAP(&key, args.This());
  args.GetReturnValue().Set(
      static_cast<uint32_t>(key->Data().GetSymmetricKeySize()));
}

void KeyObjectHandle::Export(const FunctionCallbackInfo<Value>& args) {
  KeyObjectHandle* key;
  ASSIGN_OR_RETURN_UNWRAP(&key, args.This());

  KeyType type = key->Data().GetKeyType();

  MaybeLocal<Value> result;
  if (type == kKeyTypeSecret) {
    result = key->ExportSecretKey();
  } else if (type == kKeyTypePublic) {
    unsigned int offset = 0;
    EVPKeyPointer::PublicKeyEncodingConfig config;
    if (!KeyObjectData::GetPublicKeyEncodingFromJs(
             args, &offset, kKeyContextExport)
             .To(&config)) {
      return;
    }
    CHECK_EQ(offset, static_cast<unsigned int>(args.Length()));
    result = key->ExportPublicKey(config);
  } else {
    CHECK_EQ(type, kKeyTypePrivate);
    unsigned int offset = 0;
    EVPKeyPointer::PrivateKeyEncodingConfig config;
    if (!KeyObjectData::GetPrivateKeyEncodingFromJs(
             args, &offset, kKeyContextExport)
             .To(&config)) {
      return;
    }
    CHECK_EQ(offset, static_cast<unsigned int>(args.Length()));
    result = key->ExportPrivateKey(config);
  }

  if (!result.IsEmpty())
    args.GetReturnValue().Set(result.FromMaybe(Local<Value>()));
}

MaybeLocal<Value> KeyObjectHandle::ExportSecretKey() const {
  const char* buf = data_.GetSymmetricKey();
  unsigned int len = data_.GetSymmetricKeySize();
  return Buffer::Copy(env(), buf, len).FromMaybe(Local<Value>());
}

MaybeLocal<Value> KeyObjectHandle::ExportPublicKey(
    const EVPKeyPointer::PublicKeyEncodingConfig& config) const {
  return WritePublicKey(env(), data_.GetAsymmetricKey(), config);
}

MaybeLocal<Value> KeyObjectHandle::ExportPrivateKey(
    const EVPKeyPointer::PrivateKeyEncodingConfig& config) const {
  return WritePrivateKey(env(), data_.GetAsymmetricKey(), config);
}

void KeyObjectHandle::ExportJWK(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  KeyObjectHandle* key;
  ASSIGN_OR_RETURN_UNWRAP(&key, args.This());

  CHECK(args[0]->IsObject());
  CHECK(args[1]->IsBoolean());

  ExportJWKInner(env, key->Data(), args[0], args[1]->IsTrue());

  args.GetReturnValue().Set(args[0]);
}

void NativeKeyObject::Initialize(Environment* env, Local<Object> target) {
  SetMethod(env->context(),
            target,
            "createNativeKeyObjectClass",
            NativeKeyObject::CreateNativeKeyObjectClass);
}

void NativeKeyObject::RegisterExternalReferences(
    ExternalReferenceRegistry* registry) {
  registry->Register(NativeKeyObject::CreateNativeKeyObjectClass);
  registry->Register(NativeKeyObject::New);
}

void NativeKeyObject::New(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK_EQ(args.Length(), 1);
  CHECK(args[0]->IsObject());
  KeyObjectHandle* handle = Unwrap<KeyObjectHandle>(args[0].As<Object>());
  new NativeKeyObject(env, args.This(), handle->Data());
}

void NativeKeyObject::CreateNativeKeyObjectClass(
    const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();

  CHECK_EQ(args.Length(), 1);
  Local<Value> callback = args[0];
  CHECK(callback->IsFunction());

  Local<FunctionTemplate> t =
      NewFunctionTemplate(isolate, NativeKeyObject::New);
  t->InstanceTemplate()->SetInternalFieldCount(
      KeyObjectHandle::kInternalFieldCount);

  Local<Value> ctor;
  if (!t->GetFunction(env->context()).ToLocal(&ctor))
    return;

  Local<Value> recv = Undefined(env->isolate());
  Local<Value> ret_v;
  if (!callback.As<Function>()->Call(
          env->context(), recv, 1, &ctor).ToLocal(&ret_v)) {
    return;
  }
  Local<Array> ret = ret_v.As<Array>();
  if (!ret->Get(env->context(), 1).ToLocal(&ctor)) return;
  env->set_crypto_key_object_secret_constructor(ctor.As<Function>());
  if (!ret->Get(env->context(), 2).ToLocal(&ctor)) return;
  env->set_crypto_key_object_public_constructor(ctor.As<Function>());
  if (!ret->Get(env->context(), 3).ToLocal(&ctor)) return;
  env->set_crypto_key_object_private_constructor(ctor.As<Function>());
  args.GetReturnValue().Set(ret);
}

BaseObjectPtr<BaseObject> NativeKeyObject::KeyObjectTransferData::Deserialize(
        Environment* env,
        Local<Context> context,
        std::unique_ptr<worker::TransferData> self) {
  if (context != env->context()) {
    THROW_ERR_MESSAGE_TARGET_CONTEXT_UNAVAILABLE(env);
    return {};
  }

  Local<Value> handle;
  if (!KeyObjectHandle::Create(env, data_).ToLocal(&handle))
    return {};

  Local<Function> key_ctor;
  Local<Value> arg = FIXED_ONE_BYTE_STRING(env->isolate(),
                                           "internal/crypto/keys");
  if (env->builtin_module_require()
          ->Call(context, Null(env->isolate()), 1, &arg)
          .IsEmpty()) {
    return {};
  }
  switch (data_.GetKeyType()) {
    case kKeyTypeSecret:
      key_ctor = env->crypto_key_object_secret_constructor();
      break;
    case kKeyTypePublic:
      key_ctor = env->crypto_key_object_public_constructor();
      break;
    case kKeyTypePrivate:
      key_ctor = env->crypto_key_object_private_constructor();
      break;
    default:
      UNREACHABLE();
  }

  Local<Value> key;
  if (!key_ctor->NewInstance(context, 1, &handle).ToLocal(&key))
    return {};

  return BaseObjectPtr<BaseObject>(Unwrap<KeyObjectHandle>(key.As<Object>()));
}

BaseObject::TransferMode NativeKeyObject::GetTransferMode() const {
  return BaseObject::TransferMode::kCloneable;
}

std::unique_ptr<worker::TransferData> NativeKeyObject::CloneForMessaging()
    const {
  return std::make_unique<KeyObjectTransferData>(handle_data_);
}

WebCryptoKeyExportStatus PKEY_SPKI_Export(const KeyObjectData& key_data,
                                          ByteSource* out) {
  CHECK_EQ(key_data.GetKeyType(), kKeyTypePublic);
  Mutex::ScopedLock lock(key_data.mutex());
  auto bio = key_data.GetAsymmetricKey().derPublicKey();
  if (!bio) return WebCryptoKeyExportStatus::FAILED;
  *out = ByteSource::FromBIO(bio);
  return WebCryptoKeyExportStatus::OK;
}

WebCryptoKeyExportStatus PKEY_PKCS8_Export(const KeyObjectData& key_data,
                                           ByteSource* out) {
  CHECK_EQ(key_data.GetKeyType(), kKeyTypePrivate);
  Mutex::ScopedLock lock(key_data.mutex());
  const auto& m_pkey = key_data.GetAsymmetricKey();

  auto bio = BIOPointer::NewMem();
  CHECK(bio);
  PKCS8Pointer p8inf(EVP_PKEY2PKCS8(m_pkey.get()));
  if (!i2d_PKCS8_PRIV_KEY_INFO_bio(bio.get(), p8inf.get()))
    return WebCryptoKeyExportStatus::FAILED;

  *out = ByteSource::FromBIO(bio);
  return WebCryptoKeyExportStatus::OK;
}

namespace Keys {
void Initialize(Environment* env, Local<Object> target) {
  target->Set(env->context(),
              FIXED_ONE_BYTE_STRING(env->isolate(), "KeyObjectHandle"),
              KeyObjectHandle::Initialize(env)).Check();

  constexpr int kKeyEncodingPKCS1 =
      static_cast<int>(EVPKeyPointer::PKEncodingType::PKCS1);
  constexpr int kKeyEncodingPKCS8 =
      static_cast<int>(EVPKeyPointer::PKEncodingType::PKCS8);
  constexpr int kKeyEncodingSPKI =
      static_cast<int>(EVPKeyPointer::PKEncodingType::SPKI);
  constexpr int kKeyEncodingSEC1 =
      static_cast<int>(EVPKeyPointer::PKEncodingType::SEC1);
  constexpr int kKeyFormatDER =
      static_cast<int>(EVPKeyPointer::PKFormatType::DER);
  constexpr int kKeyFormatPEM =
      static_cast<int>(EVPKeyPointer::PKFormatType::PEM);
  constexpr int kKeyFormatJWK =
      static_cast<int>(EVPKeyPointer::PKFormatType::JWK);

  NODE_DEFINE_CONSTANT(target, kWebCryptoKeyFormatRaw);
  NODE_DEFINE_CONSTANT(target, kWebCryptoKeyFormatPKCS8);
  NODE_DEFINE_CONSTANT(target, kWebCryptoKeyFormatSPKI);
  NODE_DEFINE_CONSTANT(target, kWebCryptoKeyFormatJWK);
  NODE_DEFINE_CONSTANT(target, EVP_PKEY_ED25519);
  NODE_DEFINE_CONSTANT(target, EVP_PKEY_ED448);
  NODE_DEFINE_CONSTANT(target, EVP_PKEY_X25519);
  NODE_DEFINE_CONSTANT(target, EVP_PKEY_X448);
  NODE_DEFINE_CONSTANT(target, kKeyEncodingPKCS1);
  NODE_DEFINE_CONSTANT(target, kKeyEncodingPKCS8);
  NODE_DEFINE_CONSTANT(target, kKeyEncodingSPKI);
  NODE_DEFINE_CONSTANT(target, kKeyEncodingSEC1);
  NODE_DEFINE_CONSTANT(target, kKeyFormatDER);
  NODE_DEFINE_CONSTANT(target, kKeyFormatPEM);
  NODE_DEFINE_CONSTANT(target, kKeyFormatJWK);
  NODE_DEFINE_CONSTANT(target, kKeyTypeSecret);
  NODE_DEFINE_CONSTANT(target, kKeyTypePublic);
  NODE_DEFINE_CONSTANT(target, kKeyTypePrivate);
  NODE_DEFINE_CONSTANT(target, kSigEncDER);
  NODE_DEFINE_CONSTANT(target, kSigEncP1363);
}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  KeyObjectHandle::RegisterExternalReferences(registry);
}
}  // namespace Keys

}  // namespace crypto
}  // namespace node
