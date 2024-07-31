#include "crypto/crypto_dh.h"
#include "async_wrap-inl.h"
#include "base_object-inl.h"
#include "crypto/crypto_keys.h"
#include "crypto/crypto_util.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "threadpoolwork-inl.h"
#include "v8.h"

#include <variant>

namespace node {

using v8::ArrayBuffer;
using v8::BackingStore;
using v8::ConstructorBehavior;
using v8::Context;
using v8::DontDelete;
using v8::FunctionCallback;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Int32;
using v8::Isolate;
using v8::Just;
using v8::Local;
using v8::Maybe;
using v8::Nothing;
using v8::Object;
using v8::PropertyAttribute;
using v8::ReadOnly;
using v8::SideEffectType;
using v8::Signature;
using v8::String;
using v8::Value;

namespace crypto {
namespace {
void ZeroPadDiffieHellmanSecret(size_t remainder_size,
                                char* data,
                                size_t length) {
  // DH_size returns number of bytes in a prime number.
  // DH_compute_key returns number of bytes in a remainder of exponent, which
  // may have less bytes than a prime number. Therefore add 0-padding to the
  // allocated buffer.
  const size_t prime_size = length;
  if (remainder_size != prime_size) {
    CHECK_LT(remainder_size, prime_size);
    const size_t padding = prime_size - remainder_size;
    memmove(data + padding, data, remainder_size);
    memset(data, 0, padding);
  }
}
}  // namespace

DiffieHellman::DiffieHellman(Environment* env, Local<Object> wrap)
    : BaseObject(env, wrap), verifyError_(0) {
  MakeWeak();
}

void DiffieHellman::Initialize(Environment* env, Local<Object> target) {
  Isolate* isolate = env->isolate();
  Local<Context> context = env->context();
  auto make = [&](Local<String> name, FunctionCallback callback) {
    Local<FunctionTemplate> t = NewFunctionTemplate(isolate, callback);

    const PropertyAttribute attributes =
        static_cast<PropertyAttribute>(ReadOnly | DontDelete);

    t->InstanceTemplate()->SetInternalFieldCount(
        DiffieHellman::kInternalFieldCount);

    SetProtoMethod(isolate, t, "generateKeys", GenerateKeys);
    SetProtoMethod(isolate, t, "computeSecret", ComputeSecret);
    SetProtoMethodNoSideEffect(isolate, t, "getPrime", GetPrime);
    SetProtoMethodNoSideEffect(isolate, t, "getGenerator", GetGenerator);
    SetProtoMethodNoSideEffect(isolate, t, "getPublicKey", GetPublicKey);
    SetProtoMethodNoSideEffect(isolate, t, "getPrivateKey", GetPrivateKey);
    SetProtoMethod(isolate, t, "setPublicKey", SetPublicKey);
    SetProtoMethod(isolate, t, "setPrivateKey", SetPrivateKey);

    Local<FunctionTemplate> verify_error_getter_templ =
        FunctionTemplate::New(isolate,
                              DiffieHellman::VerifyErrorGetter,
                              Local<Value>(),
                              Signature::New(env->isolate(), t),
                              /* length */ 0,
                              ConstructorBehavior::kThrow,
                              SideEffectType::kHasNoSideEffect);

    t->InstanceTemplate()->SetAccessorProperty(
        env->verify_error_string(),
        verify_error_getter_templ,
        Local<FunctionTemplate>(),
        attributes);

    SetConstructorFunction(context, target, name, t);
  };

  make(FIXED_ONE_BYTE_STRING(env->isolate(), "DiffieHellman"), New);
  make(FIXED_ONE_BYTE_STRING(env->isolate(), "DiffieHellmanGroup"),
       DiffieHellmanGroup);

  SetMethodNoSideEffect(
      context, target, "statelessDH", DiffieHellman::Stateless);
  DHKeyPairGenJob::Initialize(env, target);
  DHKeyExportJob::Initialize(env, target);
  DHBitsJob::Initialize(env, target);
}

void DiffieHellman::RegisterExternalReferences(
    ExternalReferenceRegistry* registry) {
  registry->Register(New);
  registry->Register(DiffieHellmanGroup);

  registry->Register(GenerateKeys);
  registry->Register(ComputeSecret);
  registry->Register(GetPrime);
  registry->Register(GetGenerator);
  registry->Register(GetPublicKey);
  registry->Register(GetPrivateKey);
  registry->Register(SetPublicKey);
  registry->Register(SetPrivateKey);

  registry->Register(DiffieHellman::VerifyErrorGetter);
  registry->Register(DiffieHellman::Stateless);

  DHKeyPairGenJob::RegisterExternalReferences(registry);
  DHKeyExportJob::RegisterExternalReferences(registry);
  DHBitsJob::RegisterExternalReferences(registry);
}

bool DiffieHellman::Init(int primeLength, int g) {
  dh_.reset(DH_new());
  if (!DH_generate_parameters_ex(dh_.get(), primeLength, g, nullptr))
    return false;
  return VerifyContext();
}

void DiffieHellman::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackFieldWithSize("dh", dh_ ? kSizeOf_DH : 0);
}

bool DiffieHellman::Init(BignumPointer&& bn_p, int g) {
  dh_.reset(DH_new());
  CHECK_GE(g, 2);
  BignumPointer bn_g(BN_new());
  return bn_g && BN_set_word(bn_g.get(), g) &&
         DH_set0_pqg(dh_.get(), bn_p.release(), nullptr, bn_g.release()) &&
         VerifyContext();
}

bool DiffieHellman::Init(const char* p, int p_len, int g) {
  dh_.reset(DH_new());
  if (p_len <= 0) {
    ERR_put_error(ERR_LIB_BN, BN_F_BN_GENERATE_PRIME_EX,
      BN_R_BITS_TOO_SMALL, __FILE__, __LINE__);
    return false;
  }
  if (g <= 1) {
    ERR_put_error(ERR_LIB_DH, DH_F_DH_BUILTIN_GENPARAMS,
      DH_R_BAD_GENERATOR, __FILE__, __LINE__);
    return false;
  }
  BignumPointer bn_p(
      BN_bin2bn(reinterpret_cast<const unsigned char*>(p), p_len, nullptr));
  BignumPointer bn_g(BN_new());
  if (bn_p == nullptr || bn_g == nullptr || !BN_set_word(bn_g.get(), g) ||
      !DH_set0_pqg(dh_.get(), bn_p.release(), nullptr, bn_g.release())) {
    return false;
  }
  return VerifyContext();
}

bool DiffieHellman::Init(const char* p, int p_len, const char* g, int g_len) {
  dh_.reset(DH_new());
  if (p_len <= 0) {
    ERR_put_error(ERR_LIB_BN, BN_F_BN_GENERATE_PRIME_EX,
      BN_R_BITS_TOO_SMALL, __FILE__, __LINE__);
    return false;
  }
  if (g_len <= 0) {
    ERR_put_error(ERR_LIB_DH, DH_F_DH_BUILTIN_GENPARAMS,
      DH_R_BAD_GENERATOR, __FILE__, __LINE__);
    return false;
  }
  BignumPointer bn_g(
      BN_bin2bn(reinterpret_cast<const unsigned char*>(g), g_len, nullptr));
  if (BN_is_zero(bn_g.get()) || BN_is_one(bn_g.get())) {
    ERR_put_error(ERR_LIB_DH, DH_F_DH_BUILTIN_GENPARAMS,
      DH_R_BAD_GENERATOR, __FILE__, __LINE__);
    return false;
  }
  BignumPointer bn_p(
      BN_bin2bn(reinterpret_cast<const unsigned char*>(p), p_len, nullptr));
  if (!DH_set0_pqg(dh_.get(), bn_p.get(), nullptr, bn_g.get())) {
    return false;
  }
  // The DH_set0_pqg call above takes ownership of the bignums on success,
  // so we should release them here so we don't end with a possible
  // use-after-free or double free.
  bn_p.release();
  bn_g.release();
  return VerifyContext();
}

constexpr int kStandardizedGenerator = 2;

template <BIGNUM* (*p)(BIGNUM*)>
BignumPointer InstantiateStandardizedGroup() {
  return BignumPointer(p(nullptr));
}

typedef BignumPointer (*StandardizedGroupInstantiator)();

// Returns a function that can be used to create an instance of a standardized
// Diffie-Hellman group. The generator is always kStandardizedGenerator.
inline StandardizedGroupInstantiator FindDiffieHellmanGroup(const char* name) {
#define V(n, p)                                                                \
  if (StringEqualNoCase(name, n)) return InstantiateStandardizedGroup<p>
  V("modp1", BN_get_rfc2409_prime_768);
  V("modp2", BN_get_rfc2409_prime_1024);
  V("modp5", BN_get_rfc3526_prime_1536);
  V("modp14", BN_get_rfc3526_prime_2048);
  V("modp15", BN_get_rfc3526_prime_3072);
  V("modp16", BN_get_rfc3526_prime_4096);
  V("modp17", BN_get_rfc3526_prime_6144);
  V("modp18", BN_get_rfc3526_prime_8192);
#undef V
  return nullptr;
}

void DiffieHellman::DiffieHellmanGroup(
    const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  DiffieHellman* diffieHellman = new DiffieHellman(env, args.This());

  CHECK_EQ(args.Length(), 1);
  THROW_AND_RETURN_IF_NOT_STRING(env, args[0], "Group name");

  bool initialized = false;

  const node::Utf8Value group_name(env->isolate(), args[0]);
  auto group = FindDiffieHellmanGroup(*group_name);
  if (group == nullptr)
    return THROW_ERR_CRYPTO_UNKNOWN_DH_GROUP(env);

  initialized = diffieHellman->Init(group(), kStandardizedGenerator);
  if (!initialized)
    THROW_ERR_CRYPTO_INITIALIZATION_FAILED(env);
}


void DiffieHellman::New(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  DiffieHellman* diffieHellman =
      new DiffieHellman(env, args.This());
  bool initialized = false;

  if (args.Length() == 2) {
    if (args[0]->IsInt32()) {
      if (args[1]->IsInt32()) {
        initialized = diffieHellman->Init(args[0].As<Int32>()->Value(),
                                          args[1].As<Int32>()->Value());
      }
    } else {
      ArrayBufferOrViewContents<char> arg0(args[0]);
      if (UNLIKELY(!arg0.CheckSizeInt32()))
        return THROW_ERR_OUT_OF_RANGE(env, "prime is too big");
      if (args[1]->IsInt32()) {
        initialized = diffieHellman->Init(arg0.data(),
                                          arg0.size(),
                                          args[1].As<Int32>()->Value());
      } else {
        ArrayBufferOrViewContents<char> arg1(args[1]);
        if (UNLIKELY(!arg1.CheckSizeInt32()))
          return THROW_ERR_OUT_OF_RANGE(env, "generator is too big");
        initialized = diffieHellman->Init(arg0.data(), arg0.size(),
                                          arg1.data(), arg1.size());
      }
    }
  }

  if (!initialized) {
    return ThrowCryptoError(env, ERR_get_error(), "Initialization failed");
  }
}


void DiffieHellman::GenerateKeys(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  DiffieHellman* diffieHellman;
  ASSIGN_OR_RETURN_UNWRAP(&diffieHellman, args.This());

  if (!DH_generate_key(diffieHellman->dh_.get())) {
    return ThrowCryptoError(env, ERR_get_error(), "Key generation failed");
  }

  const BIGNUM* pub_key;
  DH_get0_key(diffieHellman->dh_.get(), &pub_key, nullptr);

  std::unique_ptr<BackingStore> bs;
  {
    const int size = BN_num_bytes(pub_key);
    CHECK_GE(size, 0);
    NoArrayBufferZeroFillScope no_zero_fill_scope(env->isolate_data());
    bs = ArrayBuffer::NewBackingStore(env->isolate(), size);
  }

  CHECK_EQ(static_cast<int>(bs->ByteLength()),
           BN_bn2binpad(pub_key,
                        static_cast<unsigned char*>(bs->Data()),
                        bs->ByteLength()));

  Local<ArrayBuffer> ab = ArrayBuffer::New(env->isolate(), std::move(bs));
  Local<Value> buffer;
  if (!Buffer::New(env, ab, 0, ab->ByteLength()).ToLocal(&buffer)) return;
  args.GetReturnValue().Set(buffer);
}


void DiffieHellman::GetField(const FunctionCallbackInfo<Value>& args,
                             const BIGNUM* (*get_field)(const DH*),
                             const char* err_if_null) {
  Environment* env = Environment::GetCurrent(args);

  DiffieHellman* dh;
  ASSIGN_OR_RETURN_UNWRAP(&dh, args.This());

  const BIGNUM* num = get_field(dh->dh_.get());
  if (num == nullptr)
    return THROW_ERR_CRYPTO_INVALID_STATE(env, err_if_null);

  std::unique_ptr<BackingStore> bs;
  {
    const int size = BN_num_bytes(num);
    CHECK_GE(size, 0);
    NoArrayBufferZeroFillScope no_zero_fill_scope(env->isolate_data());
    bs = ArrayBuffer::NewBackingStore(env->isolate(), size);
  }

  CHECK_EQ(static_cast<int>(bs->ByteLength()),
           BN_bn2binpad(num,
                        static_cast<unsigned char*>(bs->Data()),
                        bs->ByteLength()));

  Local<ArrayBuffer> ab = ArrayBuffer::New(env->isolate(), std::move(bs));
  Local<Value> buffer;
  if (!Buffer::New(env, ab, 0, ab->ByteLength()).ToLocal(&buffer)) return;
  args.GetReturnValue().Set(buffer);
}

void DiffieHellman::GetPrime(const FunctionCallbackInfo<Value>& args) {
  GetField(args, [](const DH* dh) -> const BIGNUM* {
    const BIGNUM* p;
    DH_get0_pqg(dh, &p, nullptr, nullptr);
    return p;
  }, "p is null");
}

void DiffieHellman::GetGenerator(const FunctionCallbackInfo<Value>& args) {
  GetField(args, [](const DH* dh) -> const BIGNUM* {
    const BIGNUM* g;
    DH_get0_pqg(dh, nullptr, nullptr, &g);
    return g;
  }, "g is null");
}

void DiffieHellman::GetPublicKey(const FunctionCallbackInfo<Value>& args) {
  GetField(args, [](const DH* dh) -> const BIGNUM* {
    const BIGNUM* pub_key;
    DH_get0_key(dh, &pub_key, nullptr);
    return pub_key;
  }, "No public key - did you forget to generate one?");
}

void DiffieHellman::GetPrivateKey(const FunctionCallbackInfo<Value>& args) {
  GetField(args, [](const DH* dh) -> const BIGNUM* {
    const BIGNUM* priv_key;
    DH_get0_key(dh, nullptr, &priv_key);
    return priv_key;
  }, "No private key - did you forget to generate one?");
}

void DiffieHellman::ComputeSecret(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  DiffieHellman* diffieHellman;
  ASSIGN_OR_RETURN_UNWRAP(&diffieHellman, args.This());

  ClearErrorOnReturn clear_error_on_return;

  CHECK_EQ(args.Length(), 1);
  ArrayBufferOrViewContents<unsigned char> key_buf(args[0]);
  if (UNLIKELY(!key_buf.CheckSizeInt32()))
    return THROW_ERR_OUT_OF_RANGE(env, "secret is too big");
  BignumPointer key(BN_bin2bn(key_buf.data(), key_buf.size(), nullptr));

  std::unique_ptr<BackingStore> bs;
  {
    NoArrayBufferZeroFillScope no_zero_fill_scope(env->isolate_data());
    bs = ArrayBuffer::NewBackingStore(env->isolate(),
                                      DH_size(diffieHellman->dh_.get()));
  }

  int size = DH_compute_key(static_cast<unsigned char*>(bs->Data()),
                            key.get(),
                            diffieHellman->dh_.get());

  if (size == -1) {
    int checkResult;
    int checked;

    checked = DH_check_pub_key(diffieHellman->dh_.get(),
                               key.get(),
                               &checkResult);

    if (!checked) {
      return ThrowCryptoError(env, ERR_get_error(), "Invalid Key");
    } else if (checkResult) {
      if (checkResult & DH_CHECK_PUBKEY_TOO_SMALL) {
        return THROW_ERR_CRYPTO_INVALID_KEYLEN(env,
            "Supplied key is too small");
      } else if (checkResult & DH_CHECK_PUBKEY_TOO_LARGE) {
        return THROW_ERR_CRYPTO_INVALID_KEYLEN(env,
            "Supplied key is too large");
      }
    }

    return THROW_ERR_CRYPTO_INVALID_KEYTYPE(env);
  }

  CHECK_GE(size, 0);
  ZeroPadDiffieHellmanSecret(size,
                             static_cast<char*>(bs->Data()),
                             bs->ByteLength());

  Local<ArrayBuffer> ab = ArrayBuffer::New(env->isolate(), std::move(bs));
  Local<Value> buffer;
  if (!Buffer::New(env, ab, 0, ab->ByteLength()).ToLocal(&buffer)) return;
  args.GetReturnValue().Set(buffer);
}

void DiffieHellman::SetKey(const FunctionCallbackInfo<Value>& args,
                           int (*set_field)(DH*, BIGNUM*), const char* what) {
  Environment* env = Environment::GetCurrent(args);
  DiffieHellman* dh;
  ASSIGN_OR_RETURN_UNWRAP(&dh, args.This());
  CHECK_EQ(args.Length(), 1);
  ArrayBufferOrViewContents<unsigned char> buf(args[0]);
  if (UNLIKELY(!buf.CheckSizeInt32()))
    return THROW_ERR_OUT_OF_RANGE(env, "buf is too big");
  BIGNUM* num = BN_bin2bn(buf.data(), buf.size(), nullptr);
  CHECK_NOT_NULL(num);
  CHECK_EQ(1, set_field(dh->dh_.get(), num));
}

void DiffieHellman::SetPublicKey(const FunctionCallbackInfo<Value>& args) {
  SetKey(args,
         [](DH* dh, BIGNUM* num) { return DH_set0_key(dh, num, nullptr); },
         "Public key");
}

void DiffieHellman::SetPrivateKey(const FunctionCallbackInfo<Value>& args) {
  SetKey(args,
         [](DH* dh, BIGNUM* num) { return DH_set0_key(dh, nullptr, num); },
         "Private key");
}

void DiffieHellman::VerifyErrorGetter(const FunctionCallbackInfo<Value>& args) {
  HandleScope scope(args.GetIsolate());

  DiffieHellman* diffieHellman;
  ASSIGN_OR_RETURN_UNWRAP(&diffieHellman, args.This());

  args.GetReturnValue().Set(diffieHellman->verifyError_);
}

bool DiffieHellman::VerifyContext() {
  int codes;
  if (!DH_check(dh_.get(), &codes))
    return false;
  verifyError_ = codes;
  return true;
}

// The input arguments to DhKeyPairGenJob can vary
//   1. CryptoJobMode
// and either
//   2. Group name (as a string)
// or
//   2. Prime or Prime Length
//   3. Generator
// Followed by the public and private key encoding parameters:
//   * Public format
//   * Public type
//   * Private format
//   * Private type
//   * Cipher
//   * Passphrase
Maybe<bool> DhKeyGenTraits::AdditionalConfig(
    CryptoJobMode mode,
    const FunctionCallbackInfo<Value>& args,
    unsigned int* offset,
    DhKeyPairGenConfig* params) {
  Environment* env = Environment::GetCurrent(args);

  if (args[*offset]->IsString()) {
    Utf8Value group_name(env->isolate(), args[*offset]);
    auto group = FindDiffieHellmanGroup(*group_name);
    if (group == nullptr) {
      THROW_ERR_CRYPTO_UNKNOWN_DH_GROUP(env);
      return Nothing<bool>();
    }

    params->params.prime = group();
    params->params.generator = kStandardizedGenerator;
    *offset += 1;
  } else {
    if (args[*offset]->IsInt32()) {
      int size = args[*offset].As<Int32>()->Value();
      if (size < 0) {
        THROW_ERR_OUT_OF_RANGE(env, "Invalid prime size");
        return Nothing<bool>();
      }
      params->params.prime = size;
    } else {
      ArrayBufferOrViewContents<unsigned char> input(args[*offset]);
      if (UNLIKELY(!input.CheckSizeInt32())) {
        THROW_ERR_OUT_OF_RANGE(env, "prime is too big");
        return Nothing<bool>();
      }
      params->params.prime = BignumPointer(
          BN_bin2bn(input.data(), input.size(), nullptr));
    }

    CHECK(args[*offset + 1]->IsInt32());
    params->params.generator = args[*offset + 1].As<Int32>()->Value();
    *offset += 2;
  }

  return Just(true);
}

EVPKeyCtxPointer DhKeyGenTraits::Setup(DhKeyPairGenConfig* params) {
  EVPKeyPointer key_params;
  if (BignumPointer* prime_fixed_value =
          std::get_if<BignumPointer>(&params->params.prime)) {
    DHPointer dh(DH_new());
    if (!dh)
      return EVPKeyCtxPointer();

    BIGNUM* prime = prime_fixed_value->get();
    BignumPointer bn_g(BN_new());
    if (!BN_set_word(bn_g.get(), params->params.generator) ||
        !DH_set0_pqg(dh.get(), prime, nullptr, bn_g.get())) {
      return EVPKeyCtxPointer();
    }

    prime_fixed_value->release();
    bn_g.release();

    key_params = EVPKeyPointer(EVP_PKEY_new());
    CHECK(key_params);
    CHECK_EQ(EVP_PKEY_assign_DH(key_params.get(), dh.release()), 1);
  } else if (int* prime_size = std::get_if<int>(&params->params.prime)) {
    EVPKeyCtxPointer param_ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_DH, nullptr));
    EVP_PKEY* raw_params = nullptr;
    if (!param_ctx ||
        EVP_PKEY_paramgen_init(param_ctx.get()) <= 0 ||
        EVP_PKEY_CTX_set_dh_paramgen_prime_len(
            param_ctx.get(),
            *prime_size) <= 0 ||
        EVP_PKEY_CTX_set_dh_paramgen_generator(
            param_ctx.get(),
            params->params.generator) <= 0 ||
        EVP_PKEY_paramgen(param_ctx.get(), &raw_params) <= 0) {
      return EVPKeyCtxPointer();
    }

    key_params = EVPKeyPointer(raw_params);
  } else {
    UNREACHABLE();
  }

  EVPKeyCtxPointer ctx(EVP_PKEY_CTX_new(key_params.get(), nullptr));
  if (!ctx || EVP_PKEY_keygen_init(ctx.get()) <= 0)
    return EVPKeyCtxPointer();

  return ctx;
}

Maybe<bool> DHKeyExportTraits::AdditionalConfig(
    const FunctionCallbackInfo<Value>& args,
    unsigned int offset,
    DHKeyExportConfig* params) {
  return Just(true);
}

WebCryptoKeyExportStatus DHKeyExportTraits::DoExport(
    std::shared_ptr<KeyObjectData> key_data,
    WebCryptoKeyFormat format,
    const DHKeyExportConfig& params,
    ByteSource* out) {
  CHECK_NE(key_data->GetKeyType(), kKeyTypeSecret);

  switch (format) {
    case kWebCryptoKeyFormatPKCS8:
      if (key_data->GetKeyType() != kKeyTypePrivate)
        return WebCryptoKeyExportStatus::INVALID_KEY_TYPE;
      return PKEY_PKCS8_Export(key_data.get(), out);
    case kWebCryptoKeyFormatSPKI:
      if (key_data->GetKeyType() != kKeyTypePublic)
        return WebCryptoKeyExportStatus::INVALID_KEY_TYPE;
      return PKEY_SPKI_Export(key_data.get(), out);
    default:
      UNREACHABLE();
  }
}

namespace {
ByteSource StatelessDiffieHellmanThreadsafe(
    const ManagedEVPPKey& our_key,
    const ManagedEVPPKey& their_key) {
  size_t out_size;

  EVPKeyCtxPointer ctx(EVP_PKEY_CTX_new(our_key.get(), nullptr));
  if (!ctx ||
      EVP_PKEY_derive_init(ctx.get()) <= 0 ||
      EVP_PKEY_derive_set_peer(ctx.get(), their_key.get()) <= 0 ||
      EVP_PKEY_derive(ctx.get(), nullptr, &out_size) <= 0)
    return ByteSource();

  ByteSource::Builder out(out_size);
  if (EVP_PKEY_derive(ctx.get(), out.data<unsigned char>(), &out_size) <= 0) {
    return ByteSource();
  }

  ZeroPadDiffieHellmanSecret(out_size, out.data<char>(), out.size());
  return std::move(out).release();
}
}  // namespace

void DiffieHellman::Stateless(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  CHECK(args[0]->IsObject() && args[1]->IsObject());
  KeyObjectHandle* our_key_object;
  ASSIGN_OR_RETURN_UNWRAP(&our_key_object, args[0].As<Object>());
  CHECK_EQ(our_key_object->Data()->GetKeyType(), kKeyTypePrivate);
  KeyObjectHandle* their_key_object;
  ASSIGN_OR_RETURN_UNWRAP(&their_key_object, args[1].As<Object>());
  CHECK_NE(their_key_object->Data()->GetKeyType(), kKeyTypeSecret);

  ManagedEVPPKey our_key = our_key_object->Data()->GetAsymmetricKey();
  ManagedEVPPKey their_key = their_key_object->Data()->GetAsymmetricKey();

  Local<Value> out;
  if (!StatelessDiffieHellmanThreadsafe(our_key, their_key)
          .ToBuffer(env)
              .ToLocal(&out)) return;

  if (Buffer::Length(out) == 0)
    return ThrowCryptoError(env, ERR_get_error(), "diffieHellman failed");

  args.GetReturnValue().Set(out);
}

Maybe<bool> DHBitsTraits::AdditionalConfig(
    CryptoJobMode mode,
    const FunctionCallbackInfo<Value>& args,
    unsigned int offset,
    DHBitsConfig* params) {
  Environment* env = Environment::GetCurrent(args);

  CHECK(args[offset]->IsObject());  // public key
  CHECK(args[offset + 1]->IsObject());  // private key

  KeyObjectHandle* private_key;
  KeyObjectHandle* public_key;

  ASSIGN_OR_RETURN_UNWRAP(&public_key, args[offset], Nothing<bool>());
  ASSIGN_OR_RETURN_UNWRAP(&private_key, args[offset + 1], Nothing<bool>());

  if (private_key->Data()->GetKeyType() != kKeyTypePrivate ||
      public_key->Data()->GetKeyType() != kKeyTypePublic) {
    THROW_ERR_CRYPTO_INVALID_KEYTYPE(env);
    return Nothing<bool>();
  }

  params->public_key = public_key->Data();
  params->private_key = private_key->Data();

  return Just(true);
}

Maybe<bool> DHBitsTraits::EncodeOutput(
    Environment* env,
    const DHBitsConfig& params,
    ByteSource* out,
    v8::Local<v8::Value>* result) {
  *result = out->ToArrayBuffer(env);
  return Just(!result->IsEmpty());
}

bool DHBitsTraits::DeriveBits(
    Environment* env,
    const DHBitsConfig& params,
    ByteSource* out) {
  *out = StatelessDiffieHellmanThreadsafe(
      params.private_key->GetAsymmetricKey(),
      params.public_key->GetAsymmetricKey());
  return true;
}

Maybe<bool> GetDhKeyDetail(
    Environment* env,
    std::shared_ptr<KeyObjectData> key,
    Local<Object> target) {
  ManagedEVPPKey pkey = key->GetAsymmetricKey();
  CHECK_EQ(EVP_PKEY_id(pkey.get()), EVP_PKEY_DH);
  return Just(true);
}

}  // namespace crypto
}  // namespace node
