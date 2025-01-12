#include "crypto/crypto_ec.h"
#include "async_wrap-inl.h"
#include "base_object-inl.h"
#include "crypto/crypto_common.h"
#include "crypto/crypto_util.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "node_buffer.h"
#include "string_bytes.h"
#include "threadpoolwork-inl.h"
#include "v8.h"

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdh.h>

#include <algorithm>

namespace node {

using ncrypto::BignumPointer;
using ncrypto::DataPointer;
using ncrypto::ECGroupPointer;
using ncrypto::ECKeyPointer;
using ncrypto::ECPointPointer;
using ncrypto::EVPKeyCtxPointer;
using ncrypto::EVPKeyPointer;
using ncrypto::MarkPopErrorOnReturn;
using v8::Array;
using v8::ArrayBuffer;
using v8::BackingStore;
using v8::Context;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Int32;
using v8::Isolate;
using v8::JustVoid;
using v8::Local;
using v8::LocalVector;
using v8::Maybe;
using v8::MaybeLocal;
using v8::Nothing;
using v8::Object;
using v8::String;
using v8::Uint32;
using v8::Value;

namespace crypto {

int GetCurveFromName(const char* name) {
  int nid = EC_curve_nist2nid(name);
  if (nid == NID_undef)
    nid = OBJ_sn2nid(name);
  return nid;
}

void ECDH::Initialize(Environment* env, Local<Object> target) {
  Isolate* isolate = env->isolate();
  Local<Context> context = env->context();

  Local<FunctionTemplate> t = NewFunctionTemplate(isolate, New);

  t->InstanceTemplate()->SetInternalFieldCount(ECDH::kInternalFieldCount);

  SetProtoMethod(isolate, t, "generateKeys", GenerateKeys);
  SetProtoMethod(isolate, t, "computeSecret", ComputeSecret);
  SetProtoMethodNoSideEffect(isolate, t, "getPublicKey", GetPublicKey);
  SetProtoMethodNoSideEffect(isolate, t, "getPrivateKey", GetPrivateKey);
  SetProtoMethod(isolate, t, "setPublicKey", SetPublicKey);
  SetProtoMethod(isolate, t, "setPrivateKey", SetPrivateKey);

  SetConstructorFunction(context, target, "ECDH", t);

  SetMethodNoSideEffect(context, target, "ECDHConvertKey", ECDH::ConvertKey);
  SetMethodNoSideEffect(context, target, "getCurves", ECDH::GetCurves);

  ECDHBitsJob::Initialize(env, target);
  ECKeyPairGenJob::Initialize(env, target);
  ECKeyExportJob::Initialize(env, target);

  NODE_DEFINE_CONSTANT(target, OPENSSL_EC_NAMED_CURVE);
  NODE_DEFINE_CONSTANT(target, OPENSSL_EC_EXPLICIT_CURVE);
}

void ECDH::RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(New);
  registry->Register(GenerateKeys);
  registry->Register(ComputeSecret);
  registry->Register(GetPublicKey);
  registry->Register(GetPrivateKey);
  registry->Register(SetPublicKey);
  registry->Register(SetPrivateKey);
  registry->Register(ECDH::ConvertKey);
  registry->Register(ECDH::GetCurves);

  ECDHBitsJob::RegisterExternalReferences(registry);
  ECKeyPairGenJob::RegisterExternalReferences(registry);
  ECKeyExportJob::RegisterExternalReferences(registry);
}

void ECDH::GetCurves(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  const size_t num_curves = EC_get_builtin_curves(nullptr, 0);
  std::vector<EC_builtin_curve> curves(num_curves);
  CHECK_EQ(EC_get_builtin_curves(curves.data(), num_curves), num_curves);

  LocalVector<Value> arr(env->isolate(), num_curves);
  std::transform(curves.begin(), curves.end(), arr.begin(), [env](auto& curve) {
    return OneByteString(env->isolate(), OBJ_nid2sn(curve.nid));
  });
  args.GetReturnValue().Set(Array::New(env->isolate(), arr.data(), arr.size()));
}

ECDH::ECDH(Environment* env, Local<Object> wrap, ECKeyPointer&& key)
    : BaseObject(env, wrap), key_(std::move(key)), group_(key_.getGroup()) {
  MakeWeak();
  CHECK_NOT_NULL(group_);
}

void ECDH::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackFieldWithSize("key", key_ ? kSizeOf_EC_KEY : 0);
}

void ECDH::New(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  MarkPopErrorOnReturn mark_pop_error_on_return;

  // TODO(indutny): Support raw curves?
  CHECK(args[0]->IsString());
  node::Utf8Value curve(env->isolate(), args[0]);

  int nid = OBJ_sn2nid(*curve);
  if (nid == NID_undef)
    return THROW_ERR_CRYPTO_INVALID_CURVE(env);

  auto key = ECKeyPointer::NewByCurveName(nid);
  if (!key)
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env,
      "Failed to create key using named curve");

  new ECDH(env, args.This(), std::move(key));
}

void ECDH::GenerateKeys(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  ECDH* ecdh;
  ASSIGN_OR_RETURN_UNWRAP(&ecdh, args.This());

  if (!ecdh->key_.generate()) {
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env, "Failed to generate key");
  }
}

ECPointPointer ECDH::BufferToPoint(Environment* env,
                                   const EC_GROUP* group,
                                   Local<Value> buf) {
  ArrayBufferOrViewContents<unsigned char> input(buf);
  if (!input.CheckSizeInt32()) [[unlikely]] {
    THROW_ERR_OUT_OF_RANGE(env, "buffer is too big");
    return {};
  }

  auto pub = ECPointPointer::New(group);
  if (!pub) {
    THROW_ERR_CRYPTO_OPERATION_FAILED(env,
        "Failed to allocate EC_POINT for a public key");
    return pub;
  }

  ncrypto::Buffer<const unsigned char> buffer{
      .data = input.data(),
      .len = input.size(),
  };
  if (!pub.setFromBuffer(buffer, group)) {
    return {};
  }

  return pub;
}

void ECDH::ComputeSecret(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  CHECK(IsAnyBufferSource(args[0]));

  ECDH* ecdh;
  ASSIGN_OR_RETURN_UNWRAP(&ecdh, args.This());

  MarkPopErrorOnReturn mark_pop_error_on_return;

  if (!ecdh->IsKeyPairValid())
    return THROW_ERR_CRYPTO_INVALID_KEYPAIR(env);

  auto pub = ECDH::BufferToPoint(env, ecdh->group_, args[0]);
  if (!pub) {
    args.GetReturnValue().Set(
        FIXED_ONE_BYTE_STRING(env->isolate(),
        "ERR_CRYPTO_ECDH_INVALID_PUBLIC_KEY"));
    return;
  }

  std::unique_ptr<BackingStore> bs;
  {
    NoArrayBufferZeroFillScope no_zero_fill_scope(env->isolate_data());
    // NOTE: field_size is in bits
    int field_size = EC_GROUP_get_degree(ecdh->group_);
    size_t out_len = (field_size + 7) / 8;
    bs = ArrayBuffer::NewBackingStore(env->isolate(), out_len);
  }

  if (!ECDH_compute_key(
          bs->Data(), bs->ByteLength(), pub, ecdh->key_.get(), nullptr))
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env, "Failed to compute ECDH key");

  Local<ArrayBuffer> ab = ArrayBuffer::New(env->isolate(), std::move(bs));
  Local<Value> buffer;
  if (!Buffer::New(env, ab, 0, ab->ByteLength()).ToLocal(&buffer)) return;
  args.GetReturnValue().Set(buffer);
}

void ECDH::GetPublicKey(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  // Conversion form
  CHECK_EQ(args.Length(), 1);

  ECDH* ecdh;
  ASSIGN_OR_RETURN_UNWRAP(&ecdh, args.This());

  const auto group = ecdh->key_.getGroup();
  const auto pub = ecdh->key_.getPublicKey();
  if (pub == nullptr)
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env,
        "Failed to get ECDH public key");

  CHECK(args[0]->IsUint32());
  uint32_t val = args[0].As<Uint32>()->Value();
  point_conversion_form_t form = static_cast<point_conversion_form_t>(val);

  const char* error;
  Local<Object> buf;
  if (!ECPointToBuffer(env, group, pub, form, &error).ToLocal(&buf))
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env, error);
  args.GetReturnValue().Set(buf);
}

void ECDH::GetPrivateKey(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  ECDH* ecdh;
  ASSIGN_OR_RETURN_UNWRAP(&ecdh, args.This());

  auto b = ecdh->key_.getPrivateKey();
  if (b == nullptr)
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env,
        "Failed to get ECDH private key");

  std::unique_ptr<BackingStore> bs;
  {
    NoArrayBufferZeroFillScope no_zero_fill_scope(env->isolate_data());
    bs = ArrayBuffer::NewBackingStore(env->isolate(),
                                      BignumPointer::GetByteCount(b));
  }
  CHECK_EQ(bs->ByteLength(),
           BignumPointer::EncodePaddedInto(
               b, static_cast<unsigned char*>(bs->Data()), bs->ByteLength()));

  Local<ArrayBuffer> ab = ArrayBuffer::New(env->isolate(), std::move(bs));
  Local<Value> buffer;
  if (!Buffer::New(env, ab, 0, ab->ByteLength()).ToLocal(&buffer)) return;
  args.GetReturnValue().Set(buffer);
}

void ECDH::SetPrivateKey(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  ECDH* ecdh;
  ASSIGN_OR_RETURN_UNWRAP(&ecdh, args.This());

  ArrayBufferOrViewContents<unsigned char> priv_buffer(args[0]);
  if (!priv_buffer.CheckSizeInt32()) [[unlikely]]
    return THROW_ERR_OUT_OF_RANGE(env, "key is too big");

  BignumPointer priv(priv_buffer.data(), priv_buffer.size());
  if (!priv) {
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env,
        "Failed to convert Buffer to BN");
  }

  if (!ecdh->IsKeyValidForCurve(priv)) {
    return THROW_ERR_CRYPTO_INVALID_KEYTYPE(env,
        "Private key is not valid for specified curve.");
  }

  auto new_key = ecdh->key_.clone();
  CHECK(new_key);

  bool result = new_key.setPrivateKey(priv);
  priv.reset();

  if (!result) {
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env,
        "Failed to convert BN to a private key");
  }

  MarkPopErrorOnReturn mark_pop_error_on_return;
  USE(&mark_pop_error_on_return);

  auto priv_key = new_key.getPrivateKey();
  CHECK_NOT_NULL(priv_key);

  auto pub = ECPointPointer::New(ecdh->group_);
  CHECK(pub);

  if (!pub.mul(ecdh->group_, priv_key)) {
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env,
        "Failed to generate ECDH public key");
  }

  if (!new_key.setPublicKey(pub)) {
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env,
        "Failed to set generated public key");
  }

  ecdh->key_ = std::move(new_key);
  ecdh->group_ = ecdh->key_.getGroup();
}

void ECDH::SetPublicKey(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  ECDH* ecdh;
  ASSIGN_OR_RETURN_UNWRAP(&ecdh, args.This());

  CHECK(IsAnyBufferSource(args[0]));

  MarkPopErrorOnReturn mark_pop_error_on_return;

  auto pub = ECDH::BufferToPoint(env, ecdh->group_, args[0]);
  if (!pub) {
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env,
        "Failed to convert Buffer to EC_POINT");
  }

  if (!ecdh->key_.setPublicKey(pub)) {
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env,
        "Failed to set EC_POINT as the public key");
  }
}

bool ECDH::IsKeyValidForCurve(const BignumPointer& private_key) {
  CHECK(group_);
  CHECK(private_key);
  // Private keys must be in the range [1, n-1].
  // Ref: Section 3.2.1 - http://www.secg.org/sec1-v2.pdf
  if (private_key < BignumPointer::One()) {
    return false;
  }
  auto order = BignumPointer::New();
  CHECK(order);
  return EC_GROUP_get_order(group_, order.get(), nullptr) &&
         private_key < order;
}

bool ECDH::IsKeyPairValid() {
  MarkPopErrorOnReturn mark_pop_error_on_return;
  return key_.checkKey();
}

// Convert the input public key to compressed, uncompressed, or hybrid formats.
void ECDH::ConvertKey(const FunctionCallbackInfo<Value>& args) {
  MarkPopErrorOnReturn mark_pop_error_on_return;
  Environment* env = Environment::GetCurrent(args);

  CHECK_EQ(args.Length(), 3);
  CHECK(IsAnyBufferSource(args[0]));

  ArrayBufferOrViewContents<char> args0(args[0]);
  if (!args0.CheckSizeInt32()) [[unlikely]]
    return THROW_ERR_OUT_OF_RANGE(env, "key is too big");
  if (args0.empty()) return args.GetReturnValue().SetEmptyString();

  node::Utf8Value curve(env->isolate(), args[1]);

  int nid = OBJ_sn2nid(*curve);
  if (nid == NID_undef)
    return THROW_ERR_CRYPTO_INVALID_CURVE(env);

  auto group = ECGroupPointer::NewByCurveName(nid);
  if (!group)
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env, "Failed to get EC_GROUP");

  auto pub = ECDH::BufferToPoint(env, group, args[0]);
  if (!pub) {
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env,
        "Failed to convert Buffer to EC_POINT");
  }

  CHECK(args[2]->IsUint32());
  uint32_t val = args[2].As<Uint32>()->Value();
  point_conversion_form_t form = static_cast<point_conversion_form_t>(val);

  const char* error;
  Local<Object> buf;
  if (!ECPointToBuffer(env, group, pub, form, &error).ToLocal(&buf))
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env, error);
  args.GetReturnValue().Set(buf);
}

void ECDHBitsConfig::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("public", public_);
  tracker->TrackField("private", private_);
}

MaybeLocal<Value> ECDHBitsTraits::EncodeOutput(Environment* env,
                                               const ECDHBitsConfig& params,
                                               ByteSource* out) {
  return out->ToArrayBuffer(env);
}

Maybe<void> ECDHBitsTraits::AdditionalConfig(
    CryptoJobMode mode,
    const FunctionCallbackInfo<Value>& args,
    unsigned int offset,
    ECDHBitsConfig* params) {
  Environment* env = Environment::GetCurrent(args);

  CHECK(args[offset]->IsObject());      // public key
  CHECK(args[offset + 1]->IsObject());  // private key

  KeyObjectHandle* private_key;
  KeyObjectHandle* public_key;

  ASSIGN_OR_RETURN_UNWRAP(&public_key, args[offset], Nothing<void>());
  ASSIGN_OR_RETURN_UNWRAP(&private_key, args[offset + 1], Nothing<void>());

  if (private_key->Data().GetKeyType() != kKeyTypePrivate ||
      public_key->Data().GetKeyType() != kKeyTypePublic) {
    THROW_ERR_CRYPTO_INVALID_KEYTYPE(env);
    return Nothing<void>();
  }

  params->private_ = private_key->Data().addRef();
  params->public_ = public_key->Data().addRef();

  return JustVoid();
}

bool ECDHBitsTraits::DeriveBits(Environment* env,
                                const ECDHBitsConfig& params,
                                ByteSource* out) {
  size_t len = 0;
  const auto& m_privkey = params.private_.GetAsymmetricKey();
  const auto& m_pubkey = params.public_.GetAsymmetricKey();

  switch (m_privkey.id()) {
    case EVP_PKEY_X25519:
      // Fall through
    case EVP_PKEY_X448: {
      EVPKeyCtxPointer ctx = m_privkey.newCtx();
      Mutex::ScopedLock pub_lock(params.public_.mutex());
      if (EVP_PKEY_derive_init(ctx.get()) <= 0 ||
          EVP_PKEY_derive_set_peer(
              ctx.get(),
              m_pubkey.get()) <= 0 ||
          EVP_PKEY_derive(ctx.get(), nullptr, &len) <= 0) {
        return false;
      }

      ByteSource::Builder buf(len);

      if (EVP_PKEY_derive(ctx.get(), buf.data<unsigned char>(), &len) <= 0) {
        return false;
      }

      *out = std::move(buf).release(len);

      break;
    }
    default: {
      const EC_KEY* private_key;
      {
        Mutex::ScopedLock priv_lock(params.private_.mutex());
        private_key = m_privkey;
      }

      Mutex::ScopedLock pub_lock(params.public_.mutex());
      const EC_KEY* public_key = m_pubkey;

      const auto group = ECKeyPointer::GetGroup(private_key);
      if (group == nullptr)
        return false;

      CHECK(ECKeyPointer::Check(private_key));
      CHECK(ECKeyPointer::Check(public_key));
      const auto pub = ECKeyPointer::GetPublicKey(public_key);
      int field_size = EC_GROUP_get_degree(group);
      len = (field_size + 7) / 8;
      ByteSource::Builder buf(len);
      CHECK_NOT_NULL(pub);
      CHECK_NOT_NULL(private_key);
      if (ECDH_compute_key(buf.data<char>(), len, pub, private_key, nullptr) <=
          0) {
        return false;
      }

      *out = std::move(buf).release();
    }
  }

  return true;
}

EVPKeyCtxPointer EcKeyGenTraits::Setup(EcKeyPairGenConfig* params) {
  EVPKeyCtxPointer key_ctx;
  switch (params->params.curve_nid) {
    case EVP_PKEY_ED25519:
      // Fall through
    case EVP_PKEY_ED448:
      // Fall through
    case EVP_PKEY_X25519:
      // Fall through
    case EVP_PKEY_X448:
      key_ctx.reset(EVP_PKEY_CTX_new_id(params->params.curve_nid, nullptr));
      break;
    default: {
      EVPKeyCtxPointer param_ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr));
      EVP_PKEY* raw_params = nullptr;
      if (!param_ctx ||
          EVP_PKEY_paramgen_init(param_ctx.get()) <= 0 ||
          EVP_PKEY_CTX_set_ec_paramgen_curve_nid(
              param_ctx.get(), params->params.curve_nid) <= 0 ||
          EVP_PKEY_CTX_set_ec_param_enc(
              param_ctx.get(), params->params.param_encoding) <= 0 ||
          EVP_PKEY_paramgen(param_ctx.get(), &raw_params) <= 0) {
        return EVPKeyCtxPointer();
      }
      EVPKeyPointer key_params(raw_params);
      key_ctx = key_params.newCtx();
    }
  }

  if (key_ctx && EVP_PKEY_keygen_init(key_ctx.get()) <= 0)
    key_ctx.reset();

  return key_ctx;
}

// EcKeyPairGenJob input arguments
//   1. CryptoJobMode
//   2. Curve Name
//   3. Param Encoding
//   4. Public Format
//   5. Public Type
//   6. Private Format
//   7. Private Type
//   8. Cipher
//   9. Passphrase
Maybe<void> EcKeyGenTraits::AdditionalConfig(
    CryptoJobMode mode,
    const FunctionCallbackInfo<Value>& args,
    unsigned int* offset,
    EcKeyPairGenConfig* params) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(args[*offset]->IsString());  // curve name
  CHECK(args[*offset + 1]->IsInt32());  // param encoding

  Utf8Value curve_name(env->isolate(), args[*offset]);
  params->params.curve_nid = GetCurveFromName(*curve_name);
  if (params->params.curve_nid == NID_undef) {
    THROW_ERR_CRYPTO_INVALID_CURVE(env);
    return Nothing<void>();
  }

  params->params.param_encoding = args[*offset + 1].As<Int32>()->Value();
  if (params->params.param_encoding != OPENSSL_EC_NAMED_CURVE &&
      params->params.param_encoding != OPENSSL_EC_EXPLICIT_CURVE) {
    THROW_ERR_OUT_OF_RANGE(env, "Invalid param_encoding specified");
    return Nothing<void>();
  }

  *offset += 2;

  return JustVoid();
}

namespace {
WebCryptoKeyExportStatus EC_Raw_Export(const KeyObjectData& key_data,
                                       const ECKeyExportConfig& params,
                                       ByteSource* out) {
  const auto& m_pkey = key_data.GetAsymmetricKey();
  CHECK(m_pkey);
  Mutex::ScopedLock lock(key_data.mutex());

  const EC_KEY* ec_key = m_pkey;

  if (ec_key == nullptr) {
    switch (key_data.GetKeyType()) {
      case kKeyTypePrivate: {
        auto data = m_pkey.rawPrivateKey();
        if (!data) return WebCryptoKeyExportStatus::INVALID_KEY_TYPE;
        *out = ByteSource::Allocated(data.release());
        break;
      }
      case kKeyTypePublic: {
        auto data = m_pkey.rawPublicKey();
        if (!data) return WebCryptoKeyExportStatus::INVALID_KEY_TYPE;
        *out = ByteSource::Allocated(data.release());
        break;
      }
      case kKeyTypeSecret:
        UNREACHABLE();
    }
  } else {
    if (key_data.GetKeyType() != kKeyTypePublic)
      return WebCryptoKeyExportStatus::INVALID_KEY_TYPE;
    const auto group = ECKeyPointer::GetGroup(ec_key);
    const auto point = ECKeyPointer::GetPublicKey(ec_key);
    point_conversion_form_t form = POINT_CONVERSION_UNCOMPRESSED;

    // Get the allocated data size...
    size_t len = EC_POINT_point2oct(group, point, form, nullptr, 0, nullptr);
    if (len == 0)
      return WebCryptoKeyExportStatus::FAILED;
    ByteSource::Builder data(len);
    size_t check_len = EC_POINT_point2oct(
        group, point, form, data.data<unsigned char>(), len, nullptr);
    if (check_len == 0)
      return WebCryptoKeyExportStatus::FAILED;

    CHECK_EQ(len, check_len);
    *out = std::move(data).release();
  }

  return WebCryptoKeyExportStatus::OK;
}
}  // namespace

Maybe<void> ECKeyExportTraits::AdditionalConfig(
    const FunctionCallbackInfo<Value>& args,
    unsigned int offset,
    ECKeyExportConfig* params) {
  return JustVoid();
}

WebCryptoKeyExportStatus ECKeyExportTraits::DoExport(
    const KeyObjectData& key_data,
    WebCryptoKeyFormat format,
    const ECKeyExportConfig& params,
    ByteSource* out) {
  CHECK_NE(key_data.GetKeyType(), kKeyTypeSecret);

  switch (format) {
    case kWebCryptoKeyFormatRaw:
      return EC_Raw_Export(key_data, params, out);
    case kWebCryptoKeyFormatPKCS8:
      if (key_data.GetKeyType() != kKeyTypePrivate)
        return WebCryptoKeyExportStatus::INVALID_KEY_TYPE;
      return PKEY_PKCS8_Export(key_data, out);
    case kWebCryptoKeyFormatSPKI: {
      if (key_data.GetKeyType() != kKeyTypePublic)
        return WebCryptoKeyExportStatus::INVALID_KEY_TYPE;

      const auto& m_pkey = key_data.GetAsymmetricKey();
      if (m_pkey.id() != EVP_PKEY_EC) {
        return PKEY_SPKI_Export(key_data, out);
      } else {
        // Ensure exported key is in uncompressed point format.
        // The temporary EC key is so we can have i2d_PUBKEY_bio() write out
        // the header but it is a somewhat silly hoop to jump through because
        // the header is for all practical purposes a static 26 byte sequence
        // where only the second byte changes.
        Mutex::ScopedLock lock(key_data.mutex());
        const auto group = ECKeyPointer::GetGroup(m_pkey);
        const auto point = ECKeyPointer::GetPublicKey(m_pkey);
        const point_conversion_form_t form = POINT_CONVERSION_UNCOMPRESSED;
        const size_t need =
            EC_POINT_point2oct(group, point, form, nullptr, 0, nullptr);
        if (need == 0) return WebCryptoKeyExportStatus::FAILED;
        ByteSource::Builder data(need);
        const size_t have = EC_POINT_point2oct(
            group, point, form, data.data<unsigned char>(), need, nullptr);
        if (have == 0) return WebCryptoKeyExportStatus::FAILED;
        auto ec = ECKeyPointer::New(group);
        CHECK(ec);
        auto uncompressed = ECPointPointer::New(group);
        ncrypto::Buffer<const unsigned char> buffer{
            .data = data.data<unsigned char>(),
            .len = data.size(),
        };
        CHECK(uncompressed.setFromBuffer(buffer, group));
        CHECK(ec.setPublicKey(uncompressed));
        auto pkey = EVPKeyPointer::New();
        CHECK(pkey.set(ec));
        auto bio = pkey.derPublicKey();
        if (!bio) return WebCryptoKeyExportStatus::FAILED;
        *out = ByteSource::FromBIO(bio);
        return WebCryptoKeyExportStatus::OK;
      }
    }
    default:
      UNREACHABLE();
  }
}

Maybe<void> ExportJWKEcKey(Environment* env,
                           const KeyObjectData& key,
                           Local<Object> target) {
  Mutex::ScopedLock lock(key.mutex());
  const auto& m_pkey = key.GetAsymmetricKey();
  CHECK_EQ(m_pkey.id(), EVP_PKEY_EC);

  const EC_KEY* ec = m_pkey;
  CHECK_NOT_NULL(ec);

  const auto pub = ECKeyPointer::GetPublicKey(ec);
  const auto group = ECKeyPointer::GetGroup(ec);

  int degree_bits = EC_GROUP_get_degree(group);
  int degree_bytes =
    (degree_bits / CHAR_BIT) + (7 + (degree_bits % CHAR_BIT)) / 8;

  auto x = BignumPointer::New();
  auto y = BignumPointer::New();

  if (!EC_POINT_get_affine_coordinates(group, pub, x.get(), y.get(), nullptr)) {
    ThrowCryptoError(env, ERR_get_error(),
                     "Failed to get elliptic-curve point coordinates");
    return Nothing<void>();
  }

  if (target->Set(
          env->context(),
          env->jwk_kty_string(),
          env->jwk_ec_string()).IsNothing()) {
    return Nothing<void>();
  }

  if (SetEncodedValue(
          env,
          target,
          env->jwk_x_string(),
          x.get(),
          degree_bytes).IsNothing() ||
      SetEncodedValue(
          env,
          target,
          env->jwk_y_string(),
          y.get(),
          degree_bytes).IsNothing()) {
    return Nothing<void>();
  }

  Local<String> crv_name;
  const int nid = EC_GROUP_get_curve_name(group);
  switch (nid) {
    case NID_X9_62_prime256v1:
      crv_name = FIXED_ONE_BYTE_STRING(env->isolate(), "P-256");
      break;
    case NID_secp256k1:
      crv_name = FIXED_ONE_BYTE_STRING(env->isolate(), "secp256k1");
      break;
    case NID_secp384r1:
      crv_name = FIXED_ONE_BYTE_STRING(env->isolate(), "P-384");
      break;
    case NID_secp521r1:
      crv_name = FIXED_ONE_BYTE_STRING(env->isolate(), "P-521");
      break;
    default: {
      THROW_ERR_CRYPTO_JWK_UNSUPPORTED_CURVE(
          env, "Unsupported JWK EC curve: %s.", OBJ_nid2sn(nid));
      return Nothing<void>();
    }
  }
  if (target->Set(
      env->context(),
      env->jwk_crv_string(),
      crv_name).IsNothing()) {
    return Nothing<void>();
  }

  if (key.GetKeyType() == kKeyTypePrivate) {
    auto pvt = ECKeyPointer::GetPrivateKey(ec);
    return SetEncodedValue(env, target, env->jwk_d_string(), pvt, degree_bytes);
  }

  return JustVoid();
}

Maybe<void> ExportJWKEdKey(Environment* env,
                           const KeyObjectData& key,
                           Local<Object> target) {
  Mutex::ScopedLock lock(key.mutex());
  const auto& pkey = key.GetAsymmetricKey();

  const char* curve = ([&] {
    switch (pkey.id()) {
      case EVP_PKEY_ED25519:
        return "Ed25519";
      case EVP_PKEY_ED448:
        return "Ed448";
      case EVP_PKEY_X25519:
        return "X25519";
      case EVP_PKEY_X448:
        return "X448";
      default:
        UNREACHABLE();
    }
  })();

  static constexpr auto trySetKey = [](Environment* env,
                                       DataPointer data,
                                       Local<Object> target,
                                       Local<String> key) {
    Local<Value> encoded;
    Local<Value> error;
    if (!data) return false;
    const ncrypto::Buffer<const char> out = data;
    if (!StringBytes::Encode(
             env->isolate(), out.data, out.len, BASE64URL, &error)
             .ToLocal(&encoded) ||
        target->Set(env->context(), key, encoded).IsNothing()) {
      if (!error.IsEmpty()) env->isolate()->ThrowException(error);
      return false;
    }
    return true;
  };

  if (target
          ->Set(env->context(),
                env->jwk_crv_string(),
                OneByteString(env->isolate(), curve))
          .IsNothing() ||
      (key.GetKeyType() == kKeyTypePrivate &&
       !trySetKey(env, pkey.rawPrivateKey(), target, env->jwk_d_string())) ||
      !trySetKey(env, pkey.rawPublicKey(), target, env->jwk_x_string()) ||
      target->Set(env->context(), env->jwk_kty_string(), env->jwk_okp_string())
          .IsNothing()) {
    return Nothing<void>();
  }

  return JustVoid();
}

KeyObjectData ImportJWKEcKey(Environment* env,
                             Local<Object> jwk,
                             const FunctionCallbackInfo<Value>& args,
                             unsigned int offset) {
  CHECK(args[offset]->IsString());  // curve name
  Utf8Value curve(env->isolate(), args[offset].As<String>());

  int nid = GetCurveFromName(*curve);
  if (nid == NID_undef) {  // Unknown curve
    THROW_ERR_CRYPTO_INVALID_CURVE(env);
    return {};
  }

  Local<Value> x_value;
  Local<Value> y_value;
  Local<Value> d_value;

  if (!jwk->Get(env->context(), env->jwk_x_string()).ToLocal(&x_value) ||
      !jwk->Get(env->context(), env->jwk_y_string()).ToLocal(&y_value) ||
      !jwk->Get(env->context(), env->jwk_d_string()).ToLocal(&d_value)) {
    return {};
  }

  if (!x_value->IsString() ||
      !y_value->IsString() ||
      (!d_value->IsUndefined() && !d_value->IsString())) {
    THROW_ERR_CRYPTO_INVALID_JWK(env, "Invalid JWK EC key");
    return {};
  }

  KeyType type = d_value->IsString() ? kKeyTypePrivate : kKeyTypePublic;

  auto ec = ECKeyPointer::NewByCurveName(nid);
  if (!ec) {
    THROW_ERR_CRYPTO_INVALID_JWK(env, "Invalid JWK EC key");
    return {};
  }

  ByteSource x = ByteSource::FromEncodedString(env, x_value.As<String>());
  ByteSource y = ByteSource::FromEncodedString(env, y_value.As<String>());

  if (!ec.setPublicKeyRaw(x.ToBN(), y.ToBN())) {
    THROW_ERR_CRYPTO_INVALID_JWK(env, "Invalid JWK EC key");
    return {};
  }

  if (type == kKeyTypePrivate) {
    ByteSource d = ByteSource::FromEncodedString(env, d_value.As<String>());
    if (!ec.setPrivateKey(d.ToBN())) {
      THROW_ERR_CRYPTO_INVALID_JWK(env, "Invalid JWK EC key");
      return {};
    }
  }

  auto pkey = EVPKeyPointer::New();
  if (!pkey) return {};
  CHECK(pkey.set(ec));

  return KeyObjectData::CreateAsymmetric(type, std::move(pkey));
}

Maybe<void> GetEcKeyDetail(Environment* env,
                           const KeyObjectData& key,
                           Local<Object> target) {
  Mutex::ScopedLock lock(key.mutex());
  const auto& m_pkey = key.GetAsymmetricKey();
  CHECK_EQ(m_pkey.id(), EVP_PKEY_EC);

  const EC_KEY* ec = m_pkey;
  CHECK_NOT_NULL(ec);

  const auto group = ECKeyPointer::GetGroup(ec);
  int nid = EC_GROUP_get_curve_name(group);

  if (target
          ->Set(env->context(),
                env->named_curve_string(),
                OneByteString(env->isolate(), OBJ_nid2sn(nid)))
          .IsNothing()) {
    return Nothing<void>();
  }
  return JustVoid();
}

// WebCrypto requires a different format for ECDSA signatures than
// what OpenSSL produces, so we need to convert between them. The
// implementation here is a adapted from Chromium's impl here:
// https://github.com/chromium/chromium/blob/7af6cfd/components/webcrypto/algorithms/ecdsa.cc

size_t GroupOrderSize(const EVPKeyPointer& key) {
  const EC_KEY* ec = key;
  CHECK_NOT_NULL(ec);
  auto order = BignumPointer::New();
  CHECK(EC_GROUP_get_order(ECKeyPointer::GetGroup(ec), order.get(), nullptr));
  return order.byteLength();
}
}  // namespace crypto
}  // namespace node
