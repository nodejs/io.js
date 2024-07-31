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

using v8::Array;
using v8::ArrayBuffer;
using v8::BackingStore;
using v8::Context;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Int32;
using v8::Isolate;
using v8::Just;
using v8::JustVoid;
using v8::Local;
using v8::Maybe;
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

  std::vector<Local<Value>> arr(num_curves);
  std::transform(curves.begin(), curves.end(), arr.begin(), [env](auto& curve) {
    return OneByteString(env->isolate(), OBJ_nid2sn(curve.nid));
  });
  args.GetReturnValue().Set(Array::New(env->isolate(), arr.data(), arr.size()));
}

ECDH::ECDH(Environment* env, Local<Object> wrap, ECKeyPointer&& key)
    : BaseObject(env, wrap),
    key_(std::move(key)),
    group_(EC_KEY_get0_group(key_.get())) {
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

  ECKeyPointer key(EC_KEY_new_by_curve_name(nid));
  if (!key)
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env,
      "Failed to create key using named curve");

  new ECDH(env, args.This(), std::move(key));
}

void ECDH::GenerateKeys(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  ECDH* ecdh;
  ASSIGN_OR_RETURN_UNWRAP(&ecdh, args.This());

  if (!EC_KEY_generate_key(ecdh->key_.get()))
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env, "Failed to generate key");
}

ECPointPointer ECDH::BufferToPoint(Environment* env,
                                   const EC_GROUP* group,
                                   Local<Value> buf) {
  int r;

  ECPointPointer pub(EC_POINT_new(group));
  if (!pub) {
    THROW_ERR_CRYPTO_OPERATION_FAILED(env,
        "Failed to allocate EC_POINT for a public key");
    return pub;
  }

  ArrayBufferOrViewContents<unsigned char> input(buf);
  if (UNLIKELY(!input.CheckSizeInt32())) {
    THROW_ERR_OUT_OF_RANGE(env, "buffer is too big");
    return ECPointPointer();
  }
  r = EC_POINT_oct2point(
      group,
      pub.get(),
      input.data(),
      input.size(),
      nullptr);
  if (!r)
    return ECPointPointer();

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

  ECPointPointer pub(
      ECDH::BufferToPoint(env,
                          ecdh->group_,
                          args[0]));
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
          bs->Data(), bs->ByteLength(), pub.get(), ecdh->key_.get(), nullptr))
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

  const EC_GROUP* group = EC_KEY_get0_group(ecdh->key_.get());
  const EC_POINT* pub = EC_KEY_get0_public_key(ecdh->key_.get());
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

  const BIGNUM* b = EC_KEY_get0_private_key(ecdh->key_.get());
  if (b == nullptr)
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env,
        "Failed to get ECDH private key");

  std::unique_ptr<BackingStore> bs;
  {
    NoArrayBufferZeroFillScope no_zero_fill_scope(env->isolate_data());
    bs = ArrayBuffer::NewBackingStore(env->isolate(), BN_num_bytes(b));
  }
  CHECK_EQ(static_cast<int>(bs->ByteLength()),
           BN_bn2binpad(
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
  if (UNLIKELY(!priv_buffer.CheckSizeInt32()))
    return THROW_ERR_OUT_OF_RANGE(env, "key is too big");

  BignumPointer priv(BN_bin2bn(
      priv_buffer.data(), priv_buffer.size(), nullptr));
  if (!priv) {
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env,
        "Failed to convert Buffer to BN");
  }

  if (!ecdh->IsKeyValidForCurve(priv)) {
    return THROW_ERR_CRYPTO_INVALID_KEYTYPE(env,
        "Private key is not valid for specified curve.");
  }

  ECKeyPointer new_key(EC_KEY_dup(ecdh->key_.get()));
  CHECK(new_key);

  int result = EC_KEY_set_private_key(new_key.get(), priv.get());
  priv.reset();

  if (!result) {
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env,
        "Failed to convert BN to a private key");
  }

  MarkPopErrorOnReturn mark_pop_error_on_return;
  USE(&mark_pop_error_on_return);

  const BIGNUM* priv_key = EC_KEY_get0_private_key(new_key.get());
  CHECK_NOT_NULL(priv_key);

  ECPointPointer pub(EC_POINT_new(ecdh->group_));
  CHECK(pub);

  if (!EC_POINT_mul(ecdh->group_, pub.get(), priv_key,
                    nullptr, nullptr, nullptr)) {
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env,
        "Failed to generate ECDH public key");
  }

  if (!EC_KEY_set_public_key(new_key.get(), pub.get()))
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env,
        "Failed to set generated public key");

  ecdh->key_ = std::move(new_key);
  ecdh->group_ = EC_KEY_get0_group(ecdh->key_.get());
}

void ECDH::SetPublicKey(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  ECDH* ecdh;
  ASSIGN_OR_RETURN_UNWRAP(&ecdh, args.This());

  CHECK(IsAnyBufferSource(args[0]));

  MarkPopErrorOnReturn mark_pop_error_on_return;

  ECPointPointer pub(
      ECDH::BufferToPoint(env,
                          ecdh->group_,
                          args[0]));
  if (!pub) {
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env,
        "Failed to convert Buffer to EC_POINT");
  }

  int r = EC_KEY_set_public_key(ecdh->key_.get(), pub.get());
  if (!r) {
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env,
        "Failed to set EC_POINT as the public key");
  }
}

bool ECDH::IsKeyValidForCurve(const BignumPointer& private_key) {
  CHECK(group_);
  CHECK(private_key);
  // Private keys must be in the range [1, n-1].
  // Ref: Section 3.2.1 - http://www.secg.org/sec1-v2.pdf
  if (BN_cmp(private_key.get(), BN_value_one()) < 0) {
    return false;
  }
  BignumPointer order(BN_new());
  CHECK(order);
  return EC_GROUP_get_order(group_, order.get(), nullptr) &&
         BN_cmp(private_key.get(), order.get()) < 0;
}

bool ECDH::IsKeyPairValid() {
  MarkPopErrorOnReturn mark_pop_error_on_return;
  USE(&mark_pop_error_on_return);
  return 1 == EC_KEY_check_key(key_.get());
}

// Convert the input public key to compressed, uncompressed, or hybrid formats.
void ECDH::ConvertKey(const FunctionCallbackInfo<Value>& args) {
  MarkPopErrorOnReturn mark_pop_error_on_return;
  Environment* env = Environment::GetCurrent(args);

  CHECK_EQ(args.Length(), 3);
  CHECK(IsAnyBufferSource(args[0]));

  ArrayBufferOrViewContents<char> args0(args[0]);
  if (UNLIKELY(!args0.CheckSizeInt32()))
    return THROW_ERR_OUT_OF_RANGE(env, "key is too big");
  if (args0.empty()) return args.GetReturnValue().SetEmptyString();

  node::Utf8Value curve(env->isolate(), args[1]);

  int nid = OBJ_sn2nid(*curve);
  if (nid == NID_undef)
    return THROW_ERR_CRYPTO_INVALID_CURVE(env);

  ECGroupPointer group(
      EC_GROUP_new_by_curve_name(nid));
  if (group == nullptr)
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env, "Failed to get EC_GROUP");

  ECPointPointer pub(
      ECDH::BufferToPoint(env,
                          group.get(),
                          args[0]));

  if (pub == nullptr) {
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env,
        "Failed to convert Buffer to EC_POINT");
  }

  CHECK(args[2]->IsUint32());
  uint32_t val = args[2].As<Uint32>()->Value();
  point_conversion_form_t form = static_cast<point_conversion_form_t>(val);

  const char* error;
  Local<Object> buf;
  if (!ECPointToBuffer(env, group.get(), pub.get(), form, &error).ToLocal(&buf))
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env, error);
  args.GetReturnValue().Set(buf);
}

void ECDHBitsConfig::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("public", public_);
  tracker->TrackField("private", private_);
}

Maybe<bool> ECDHBitsTraits::EncodeOutput(
    Environment* env,
    const ECDHBitsConfig& params,
    ByteSource* out,
    v8::Local<v8::Value>* result) {
  *result = out->ToArrayBuffer(env);
  return Just(!result->IsEmpty());
}

Maybe<bool> ECDHBitsTraits::AdditionalConfig(
    CryptoJobMode mode,
    const FunctionCallbackInfo<Value>& args,
    unsigned int offset,
    ECDHBitsConfig* params) {
  Environment* env = Environment::GetCurrent(args);

  CHECK(args[offset]->IsString());  // curve name
  CHECK(args[offset + 1]->IsObject());  // public key
  CHECK(args[offset + 2]->IsObject());  // private key

  KeyObjectHandle* private_key;
  KeyObjectHandle* public_key;

  Utf8Value name(env->isolate(), args[offset]);

  ASSIGN_OR_RETURN_UNWRAP(&public_key, args[offset + 1], Nothing<bool>());
  ASSIGN_OR_RETURN_UNWRAP(&private_key, args[offset + 2], Nothing<bool>());

  if (private_key->Data()->GetKeyType() != kKeyTypePrivate ||
      public_key->Data()->GetKeyType() != kKeyTypePublic) {
    THROW_ERR_CRYPTO_INVALID_KEYTYPE(env);
    return Nothing<bool>();
  }

  params->id_ = GetOKPCurveFromName(*name);
  params->private_ = private_key->Data();
  params->public_ = public_key->Data();

  return Just(true);
}

bool ECDHBitsTraits::DeriveBits(Environment* env,
                                const ECDHBitsConfig& params,
                                ByteSource* out) {
  size_t len = 0;
  ManagedEVPPKey m_privkey = params.private_->GetAsymmetricKey();
  ManagedEVPPKey m_pubkey = params.public_->GetAsymmetricKey();

  switch (params.id_) {
    case EVP_PKEY_X25519:
      // Fall through
    case EVP_PKEY_X448: {
      EVPKeyCtxPointer ctx = nullptr;
      {
        ctx.reset(EVP_PKEY_CTX_new(m_privkey.get(), nullptr));
      }
      Mutex::ScopedLock pub_lock(*m_pubkey.mutex());
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
        Mutex::ScopedLock priv_lock(*m_privkey.mutex());
        private_key = EVP_PKEY_get0_EC_KEY(m_privkey.get());
      }

      Mutex::ScopedLock pub_lock(*m_pubkey.mutex());
      const EC_KEY* public_key = EVP_PKEY_get0_EC_KEY(m_pubkey.get());

      const EC_GROUP* group = EC_KEY_get0_group(private_key);
      if (group == nullptr)
        return false;

      CHECK_EQ(EC_KEY_check_key(private_key), 1);
      CHECK_EQ(EC_KEY_check_key(public_key), 1);
      const EC_POINT* pub = EC_KEY_get0_public_key(public_key);
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
      key_ctx.reset(EVP_PKEY_CTX_new(key_params.get(), nullptr));
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
Maybe<bool> EcKeyGenTraits::AdditionalConfig(
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
    return Nothing<bool>();
  }

  params->params.param_encoding = args[*offset + 1].As<Int32>()->Value();
  if (params->params.param_encoding != OPENSSL_EC_NAMED_CURVE &&
      params->params.param_encoding != OPENSSL_EC_EXPLICIT_CURVE) {
    THROW_ERR_OUT_OF_RANGE(env, "Invalid param_encoding specified");
    return Nothing<bool>();
  }

  *offset += 2;

  return Just(true);
}

namespace {
WebCryptoKeyExportStatus EC_Raw_Export(
    KeyObjectData* key_data,
    const ECKeyExportConfig& params,
    ByteSource* out) {
  ManagedEVPPKey m_pkey = key_data->GetAsymmetricKey();
  CHECK(m_pkey);
  Mutex::ScopedLock lock(*m_pkey.mutex());

  const EC_KEY* ec_key = EVP_PKEY_get0_EC_KEY(m_pkey.get());

  size_t len = 0;

  if (ec_key == nullptr) {
    typedef int (*export_fn)(const EVP_PKEY*, unsigned char*, size_t* len);
    export_fn fn = nullptr;
    switch (key_data->GetKeyType()) {
      case kKeyTypePrivate:
        fn = EVP_PKEY_get_raw_private_key;
        break;
      case kKeyTypePublic:
        fn = EVP_PKEY_get_raw_public_key;
        break;
      case kKeyTypeSecret:
        UNREACHABLE();
    }
    CHECK_NOT_NULL(fn);
    // Get the size of the raw key data
    if (fn(m_pkey.get(), nullptr, &len) == 0)
      return WebCryptoKeyExportStatus::INVALID_KEY_TYPE;
    ByteSource::Builder data(len);
    if (fn(m_pkey.get(), data.data<unsigned char>(), &len) == 0)
      return WebCryptoKeyExportStatus::INVALID_KEY_TYPE;
    *out = std::move(data).release(len);
  } else {
    if (key_data->GetKeyType() != kKeyTypePublic)
      return WebCryptoKeyExportStatus::INVALID_KEY_TYPE;
    const EC_GROUP* group = EC_KEY_get0_group(ec_key);
    const EC_POINT* point = EC_KEY_get0_public_key(ec_key);
    point_conversion_form_t form = POINT_CONVERSION_UNCOMPRESSED;

    // Get the allocated data size...
    len = EC_POINT_point2oct(group, point, form, nullptr, 0, nullptr);
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

Maybe<bool> ECKeyExportTraits::AdditionalConfig(
    const FunctionCallbackInfo<Value>& args,
    unsigned int offset,
    ECKeyExportConfig* params) {
  return Just(true);
}

WebCryptoKeyExportStatus ECKeyExportTraits::DoExport(
    std::shared_ptr<KeyObjectData> key_data,
    WebCryptoKeyFormat format,
    const ECKeyExportConfig& params,
    ByteSource* out) {
  CHECK_NE(key_data->GetKeyType(), kKeyTypeSecret);

  switch (format) {
    case kWebCryptoKeyFormatRaw:
      return EC_Raw_Export(key_data.get(), params, out);
    case kWebCryptoKeyFormatPKCS8:
      if (key_data->GetKeyType() != kKeyTypePrivate)
        return WebCryptoKeyExportStatus::INVALID_KEY_TYPE;
      return PKEY_PKCS8_Export(key_data.get(), out);
    case kWebCryptoKeyFormatSPKI: {
      if (key_data->GetKeyType() != kKeyTypePublic)
        return WebCryptoKeyExportStatus::INVALID_KEY_TYPE;

      ManagedEVPPKey m_pkey = key_data->GetAsymmetricKey();
      if (EVP_PKEY_id(m_pkey.get()) != EVP_PKEY_EC) {
        return PKEY_SPKI_Export(key_data.get(), out);
      } else {
        // Ensure exported key is in uncompressed point format.
        // The temporary EC key is so we can have i2d_PUBKEY_bio() write out
        // the header but it is a somewhat silly hoop to jump through because
        // the header is for all practical purposes a static 26 byte sequence
        // where only the second byte changes.
        Mutex::ScopedLock lock(*m_pkey.mutex());
        const EC_KEY* ec_key = EVP_PKEY_get0_EC_KEY(m_pkey.get());
        const EC_GROUP* group = EC_KEY_get0_group(ec_key);
        const EC_POINT* point = EC_KEY_get0_public_key(ec_key);
        const point_conversion_form_t form = POINT_CONVERSION_UNCOMPRESSED;
        const size_t need =
            EC_POINT_point2oct(group, point, form, nullptr, 0, nullptr);
        if (need == 0) return WebCryptoKeyExportStatus::FAILED;
        ByteSource::Builder data(need);
        const size_t have = EC_POINT_point2oct(
            group, point, form, data.data<unsigned char>(), need, nullptr);
        if (have == 0) return WebCryptoKeyExportStatus::FAILED;
        ECKeyPointer ec(EC_KEY_new());
        CHECK_EQ(1, EC_KEY_set_group(ec.get(), group));
        ECPointPointer uncompressed(EC_POINT_new(group));
        CHECK_EQ(1,
                 EC_POINT_oct2point(group,
                                    uncompressed.get(),
                                    data.data<unsigned char>(),
                                    data.size(),
                                    nullptr));
        CHECK_EQ(1, EC_KEY_set_public_key(ec.get(), uncompressed.get()));
        EVPKeyPointer pkey(EVP_PKEY_new());
        CHECK_EQ(1, EVP_PKEY_set1_EC_KEY(pkey.get(), ec.get()));
        BIOPointer bio(BIO_new(BIO_s_mem()));
        CHECK(bio);
        if (!i2d_PUBKEY_bio(bio.get(), pkey.get()))
          return WebCryptoKeyExportStatus::FAILED;
        *out = ByteSource::FromBIO(bio);
        return WebCryptoKeyExportStatus::OK;
      }
    }
    default:
      UNREACHABLE();
  }
}

Maybe<void> ExportJWKEcKey(
    Environment* env,
    std::shared_ptr<KeyObjectData> key,
    Local<Object> target) {
  ManagedEVPPKey m_pkey = key->GetAsymmetricKey();
  Mutex::ScopedLock lock(*m_pkey.mutex());
  CHECK_EQ(EVP_PKEY_id(m_pkey.get()), EVP_PKEY_EC);

  const EC_KEY* ec = EVP_PKEY_get0_EC_KEY(m_pkey.get());
  CHECK_NOT_NULL(ec);

  const EC_POINT* pub = EC_KEY_get0_public_key(ec);
  const EC_GROUP* group = EC_KEY_get0_group(ec);

  int degree_bits = EC_GROUP_get_degree(group);
  int degree_bytes =
    (degree_bits / CHAR_BIT) + (7 + (degree_bits % CHAR_BIT)) / 8;

  BignumPointer x(BN_new());
  BignumPointer y(BN_new());

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
      crv_name = OneByteString(env->isolate(), "P-256");
      break;
    case NID_secp256k1:
      crv_name = OneByteString(env->isolate(), "secp256k1");
      break;
    case NID_secp384r1:
      crv_name = OneByteString(env->isolate(), "P-384");
      break;
    case NID_secp521r1:
      crv_name = OneByteString(env->isolate(), "P-521");
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

  if (key->GetKeyType() == kKeyTypePrivate) {
    const BIGNUM* pvt = EC_KEY_get0_private_key(ec);
    return SetEncodedValue(
      env,
      target,
      env->jwk_d_string(),
      pvt,
      degree_bytes).IsJust() ? JustVoid() : Nothing<void>();
  }

  return JustVoid();
}

Maybe<void> ExportJWKEdKey(Environment* env,
                           std::shared_ptr<KeyObjectData> key,
                           Local<Object> target) {
  ManagedEVPPKey pkey = key->GetAsymmetricKey();
  Mutex::ScopedLock lock(*pkey.mutex());

  const char* curve = nullptr;
  switch (EVP_PKEY_id(pkey.get())) {
    case EVP_PKEY_ED25519:
      curve = "Ed25519";
      break;
    case EVP_PKEY_ED448:
      curve = "Ed448";
      break;
    case EVP_PKEY_X25519:
      curve = "X25519";
      break;
    case EVP_PKEY_X448:
      curve = "X448";
      break;
    default:
      UNREACHABLE();
  }
  if (target->Set(
          env->context(),
          env->jwk_crv_string(),
          OneByteString(env->isolate(), curve)).IsNothing()) {
    return Nothing<void>();
  }

  size_t len = 0;
  Local<Value> encoded;
  Local<Value> error;

  if (!EVP_PKEY_get_raw_public_key(pkey.get(), nullptr, &len))
    return Nothing<void>();

  ByteSource::Builder out(len);

  if (key->GetKeyType() == kKeyTypePrivate) {
    if (!EVP_PKEY_get_raw_private_key(
            pkey.get(), out.data<unsigned char>(), &len) ||
        !StringBytes::Encode(
             env->isolate(), out.data<const char>(), len, BASE64URL, &error)
             .ToLocal(&encoded) ||
        !target->Set(env->context(), env->jwk_d_string(), encoded).IsJust()) {
      if (!error.IsEmpty())
        env->isolate()->ThrowException(error);
      return Nothing<void>();
    }
  }

  if (!EVP_PKEY_get_raw_public_key(
          pkey.get(), out.data<unsigned char>(), &len) ||
      !StringBytes::Encode(
           env->isolate(), out.data<const char>(), len, BASE64URL, &error)
           .ToLocal(&encoded) ||
      !target->Set(env->context(), env->jwk_x_string(), encoded).IsJust()) {
    if (!error.IsEmpty())
      env->isolate()->ThrowException(error);
    return Nothing<void>();
  }

  if (target->Set(
          env->context(),
          env->jwk_kty_string(),
          env->jwk_okp_string()).IsNothing()) {
    return Nothing<void>();
  }

  return JustVoid();
}

std::shared_ptr<KeyObjectData> ImportJWKEcKey(
    Environment* env,
    Local<Object> jwk,
    const FunctionCallbackInfo<Value>& args,
    unsigned int offset) {
  CHECK(args[offset]->IsString());  // curve name
  Utf8Value curve(env->isolate(), args[offset].As<String>());

  int nid = GetCurveFromName(*curve);
  if (nid == NID_undef) {  // Unknown curve
    THROW_ERR_CRYPTO_INVALID_CURVE(env);
    return std::shared_ptr<KeyObjectData>();
  }

  Local<Value> x_value;
  Local<Value> y_value;
  Local<Value> d_value;

  if (!jwk->Get(env->context(), env->jwk_x_string()).ToLocal(&x_value) ||
      !jwk->Get(env->context(), env->jwk_y_string()).ToLocal(&y_value) ||
      !jwk->Get(env->context(), env->jwk_d_string()).ToLocal(&d_value)) {
    return std::shared_ptr<KeyObjectData>();
  }

  if (!x_value->IsString() ||
      !y_value->IsString() ||
      (!d_value->IsUndefined() && !d_value->IsString())) {
    THROW_ERR_CRYPTO_INVALID_JWK(env, "Invalid JWK EC key");
    return std::shared_ptr<KeyObjectData>();
  }

  KeyType type = d_value->IsString() ? kKeyTypePrivate : kKeyTypePublic;

  ECKeyPointer ec(EC_KEY_new_by_curve_name(nid));
  if (!ec) {
    THROW_ERR_CRYPTO_INVALID_JWK(env, "Invalid JWK EC key");
    return std::shared_ptr<KeyObjectData>();
  }

  ByteSource x = ByteSource::FromEncodedString(env, x_value.As<String>());
  ByteSource y = ByteSource::FromEncodedString(env, y_value.As<String>());

  if (!EC_KEY_set_public_key_affine_coordinates(
          ec.get(),
          x.ToBN().get(),
          y.ToBN().get())) {
    THROW_ERR_CRYPTO_INVALID_JWK(env, "Invalid JWK EC key");
    return std::shared_ptr<KeyObjectData>();
  }

  if (type == kKeyTypePrivate) {
    ByteSource d = ByteSource::FromEncodedString(env, d_value.As<String>());
    if (!EC_KEY_set_private_key(ec.get(), d.ToBN().get())) {
      THROW_ERR_CRYPTO_INVALID_JWK(env, "Invalid JWK EC key");
      return std::shared_ptr<KeyObjectData>();
    }
  }

  EVPKeyPointer pkey(EVP_PKEY_new());
  CHECK_EQ(EVP_PKEY_set1_EC_KEY(pkey.get(), ec.get()), 1);

  return KeyObjectData::CreateAsymmetric(type, ManagedEVPPKey(std::move(pkey)));
}

Maybe<bool> GetEcKeyDetail(
    Environment* env,
    std::shared_ptr<KeyObjectData> key,
    Local<Object> target) {
  ManagedEVPPKey m_pkey = key->GetAsymmetricKey();
  Mutex::ScopedLock lock(*m_pkey.mutex());
  CHECK_EQ(EVP_PKEY_id(m_pkey.get()), EVP_PKEY_EC);

  const EC_KEY* ec = EVP_PKEY_get0_EC_KEY(m_pkey.get());
  CHECK_NOT_NULL(ec);

  const EC_GROUP* group = EC_KEY_get0_group(ec);
  int nid = EC_GROUP_get_curve_name(group);

  return target->Set(
      env->context(),
      env->named_curve_string(),
      OneByteString(env->isolate(), OBJ_nid2sn(nid)));
}

// WebCrypto requires a different format for ECDSA signatures than
// what OpenSSL produces, so we need to convert between them. The
// implementation here is a adapted from Chromium's impl here:
// https://github.com/chromium/chromium/blob/7af6cfd/components/webcrypto/algorithms/ecdsa.cc

size_t GroupOrderSize(const ManagedEVPPKey& key) {
  const EC_KEY* ec = EVP_PKEY_get0_EC_KEY(key.get());
  CHECK_NOT_NULL(ec);
  const EC_GROUP* group = EC_KEY_get0_group(ec);
  BignumPointer order(BN_new());
  CHECK(EC_GROUP_get_order(group, order.get(), nullptr));
  return BN_num_bytes(order.get());
}
}  // namespace crypto
}  // namespace node
