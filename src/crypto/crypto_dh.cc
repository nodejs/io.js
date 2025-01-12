#include "crypto/crypto_dh.h"
#include "async_wrap-inl.h"
#include "base_object-inl.h"
#include "crypto/crypto_keys.h"
#include "crypto/crypto_util.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "ncrypto.h"
#include "node_errors.h"
#include "openssl/bnerr.h"
#include "openssl/dh.h"
#include "threadpoolwork-inl.h"
#include "v8.h"

namespace node {

using ncrypto::BignumPointer;
using ncrypto::DataPointer;
using ncrypto::DHPointer;
using ncrypto::EVPKeyCtxPointer;
using ncrypto::EVPKeyPointer;
using v8::ArrayBuffer;
using v8::ConstructorBehavior;
using v8::Context;
using v8::DontDelete;
using v8::FunctionCallback;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Int32;
using v8::Isolate;
using v8::JustVoid;
using v8::Local;
using v8::Maybe;
using v8::MaybeLocal;
using v8::Nothing;
using v8::Object;
using v8::PropertyAttribute;
using v8::ReadOnly;
using v8::SideEffectType;
using v8::Signature;
using v8::String;
using v8::Value;

namespace crypto {
DiffieHellman::DiffieHellman(Environment* env, Local<Object> wrap, DHPointer dh)
    : BaseObject(env, wrap), dh_(std::move(dh)) {
  MakeWeak();
}

void DiffieHellman::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackFieldWithSize("dh", dh_ ? kSizeOf_DH : 0);
}

namespace {
MaybeLocal<Value> DataPointerToBuffer(Environment* env, DataPointer&& data) {
  auto backing = ArrayBuffer::NewBackingStore(
      data.get(),
      data.size(),
      [](void* data, size_t len, void* ptr) { DataPointer free_me(data, len); },
      nullptr);
  data.release();

  auto ab = ArrayBuffer::New(env->isolate(), std::move(backing));
  return Buffer::New(env, ab, 0, ab->ByteLength()).FromMaybe(Local<Value>());
}

void DiffieHellmanGroup(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK_EQ(args.Length(), 1);
  THROW_AND_RETURN_IF_NOT_STRING(env, args[0], "Group name");
  const node::Utf8Value group_name(env->isolate(), args[0]);

  DHPointer dh = DHPointer::FromGroup(group_name.ToStringView());
  if (!dh) {
    return THROW_ERR_CRYPTO_UNKNOWN_DH_GROUP(env);
  }

  new DiffieHellman(env, args.This(), std::move(dh));
}

void New(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  if (args.Length() != 2) {
    return THROW_ERR_MISSING_ARGS(env, "Constructor must have two arguments");
  }

  if (args[0]->IsInt32()) {
    int32_t bits = args[0].As<Int32>()->Value();
    if (bits < 2) {
#if OPENSSL_VERSION_MAJOR >= 3
      ERR_put_error(ERR_LIB_DH, 0, DH_R_MODULUS_TOO_SMALL, __FILE__, __LINE__);
#else
      ERR_put_error(ERR_LIB_BN, 0, BN_R_BITS_TOO_SMALL, __FILE__, __LINE__);
#endif
      return ThrowCryptoError(env, ERR_get_error(), "Invalid prime length");
    }

    // If the first argument is an Int32 then we are generating a new
    // prime and then using that to generate the Diffie-Hellman parameters.
    // The second argument must be an Int32 as well.
    if (!args[1]->IsInt32()) {
      return THROW_ERR_INVALID_ARG_TYPE(env,
                                        "Second argument must be an int32");
    }
    int32_t generator = args[1].As<Int32>()->Value();
    if (generator < 2) {
      ERR_put_error(ERR_LIB_DH, 0, DH_R_BAD_GENERATOR, __FILE__, __LINE__);
      return ThrowCryptoError(env, ERR_get_error(), "Invalid generator");
    }

    auto dh = DHPointer::New(bits, generator);
    if (!dh) {
      return THROW_ERR_INVALID_ARG_VALUE(env, "Invalid DH parameters");
    }
    new DiffieHellman(env, args.This(), std::move(dh));
    return;
  }

  // The first argument must be an ArrayBuffer or ArrayBufferView with the
  // prime, and the second argument must be an int32 with the generator
  // or an ArrayBuffer or ArrayBufferView with the generator.

  ArrayBufferOrViewContents<char> arg0(args[0]);
  if (!arg0.CheckSizeInt32()) [[unlikely]]
    return THROW_ERR_OUT_OF_RANGE(env, "prime is too big");

  BignumPointer bn_p(reinterpret_cast<uint8_t*>(arg0.data()), arg0.size());
  BignumPointer bn_g;
  if (!bn_p) {
    return THROW_ERR_INVALID_ARG_VALUE(env, "Invalid prime");
  }

  if (args[1]->IsInt32()) {
    int32_t generator = args[1].As<Int32>()->Value();
    if (generator < 2) {
      ERR_put_error(ERR_LIB_DH, 0, DH_R_BAD_GENERATOR, __FILE__, __LINE__);
      return ThrowCryptoError(env, ERR_get_error(), "Invalid generator");
    }
    bn_g = BignumPointer::New();
    if (!bn_g.setWord(generator)) {
      ERR_put_error(ERR_LIB_DH, 0, DH_R_BAD_GENERATOR, __FILE__, __LINE__);
      return ThrowCryptoError(env, ERR_get_error(), "Invalid generator");
    }
  } else {
    ArrayBufferOrViewContents<char> arg1(args[1]);
    if (!arg1.CheckSizeInt32()) [[unlikely]]
      return THROW_ERR_OUT_OF_RANGE(env, "generator is too big");
    bn_g = BignumPointer(reinterpret_cast<uint8_t*>(arg1.data()), arg1.size());
    if (!bn_g) {
      ERR_put_error(ERR_LIB_DH, 0, DH_R_BAD_GENERATOR, __FILE__, __LINE__);
      return ThrowCryptoError(env, ERR_get_error(), "Invalid generator");
    }
    if (bn_g.getWord() < 2) {
      ERR_put_error(ERR_LIB_DH, 0, DH_R_BAD_GENERATOR, __FILE__, __LINE__);
      return ThrowCryptoError(env, ERR_get_error(), "Invalid generator");
    }
  }

  auto dh = DHPointer::New(std::move(bn_p), std::move(bn_g));
  if (!dh) {
    return THROW_ERR_INVALID_ARG_VALUE(env, "Invalid DH parameters");
  }
  new DiffieHellman(env, args.This(), std::move(dh));
}

void GenerateKeys(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  DiffieHellman* diffieHellman;
  ASSIGN_OR_RETURN_UNWRAP(&diffieHellman, args.This());
  DHPointer& dh = *diffieHellman;

  auto dp = dh.generateKeys();
  if (!dp) {
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env, "Key generation failed");
  }

  Local<Value> buffer;
  if (DataPointerToBuffer(env, std::move(dp)).ToLocal(&buffer)) {
    args.GetReturnValue().Set(buffer);
  }
}

void GetPrime(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  DiffieHellman* diffieHellman;
  ASSIGN_OR_RETURN_UNWRAP(&diffieHellman, args.This());
  DHPointer& dh = *diffieHellman;

  auto dp = dh.getPrime();
  if (!dp) {
    return THROW_ERR_CRYPTO_INVALID_STATE(env, "p is null");
  }
  Local<Value> buffer;
  if (DataPointerToBuffer(env, std::move(dp)).ToLocal(&buffer)) {
    args.GetReturnValue().Set(buffer);
  }
}

void GetGenerator(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  DiffieHellman* diffieHellman;
  ASSIGN_OR_RETURN_UNWRAP(&diffieHellman, args.This());
  DHPointer& dh = *diffieHellman;

  auto dp = dh.getGenerator();
  if (!dp) {
    return THROW_ERR_CRYPTO_INVALID_STATE(env, "g is null");
  }
  Local<Value> buffer;
  if (DataPointerToBuffer(env, std::move(dp)).ToLocal(&buffer)) {
    args.GetReturnValue().Set(buffer);
  }
}

void GetPublicKey(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  DiffieHellman* diffieHellman;
  ASSIGN_OR_RETURN_UNWRAP(&diffieHellman, args.This());
  DHPointer& dh = *diffieHellman;

  auto dp = dh.getPublicKey();
  if (!dp) {
    return THROW_ERR_CRYPTO_INVALID_STATE(
        env, "No public key - did you forget to generate one?");
  }
  Local<Value> buffer;
  if (DataPointerToBuffer(env, std::move(dp)).ToLocal(&buffer)) {
    args.GetReturnValue().Set(buffer);
  }
}

void GetPrivateKey(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  DiffieHellman* diffieHellman;
  ASSIGN_OR_RETURN_UNWRAP(&diffieHellman, args.This());
  DHPointer& dh = *diffieHellman;

  auto dp = dh.getPrivateKey();
  if (!dp) {
    return THROW_ERR_CRYPTO_INVALID_STATE(
        env, "No private key - did you forget to generate one?");
  }
  Local<Value> buffer;
  if (DataPointerToBuffer(env, std::move(dp)).ToLocal(&buffer)) {
    args.GetReturnValue().Set(buffer);
  }
}

void ComputeSecret(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  DiffieHellman* diffieHellman;
  ASSIGN_OR_RETURN_UNWRAP(&diffieHellman, args.This());
  DHPointer& dh = *diffieHellman;

  CHECK_EQ(args.Length(), 1);
  ArrayBufferOrViewContents<unsigned char> key_buf(args[0]);
  if (!key_buf.CheckSizeInt32()) [[unlikely]]
    return THROW_ERR_OUT_OF_RANGE(env, "secret is too big");
  BignumPointer key(key_buf.data(), key_buf.size());

  switch (dh.checkPublicKey(key)) {
    case DHPointer::CheckPublicKeyResult::INVALID:
      // Fall-through
    case DHPointer::CheckPublicKeyResult::CHECK_FAILED:
      return THROW_ERR_CRYPTO_INVALID_KEYTYPE(env,
                                              "Unspecified validation error");
    case DHPointer::CheckPublicKeyResult::TOO_SMALL:
      return THROW_ERR_CRYPTO_INVALID_KEYLEN(env, "Supplied key is too small");
    case DHPointer::CheckPublicKeyResult::TOO_LARGE:
      return THROW_ERR_CRYPTO_INVALID_KEYLEN(env, "Supplied key is too large");
    case DHPointer::CheckPublicKeyResult::NONE:
      break;
  }

  auto dp = dh.computeSecret(key);

  Local<Value> buffer;
  if (DataPointerToBuffer(env, std::move(dp)).ToLocal(&buffer)) {
    args.GetReturnValue().Set(buffer);
  }
}

void SetPublicKey(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  DiffieHellman* diffieHellman;
  ASSIGN_OR_RETURN_UNWRAP(&diffieHellman, args.This());
  DHPointer& dh = *diffieHellman;
  CHECK_EQ(args.Length(), 1);
  ArrayBufferOrViewContents<unsigned char> buf(args[0]);
  if (!buf.CheckSizeInt32()) [[unlikely]]
    return THROW_ERR_OUT_OF_RANGE(env, "buf is too big");
  BignumPointer num(buf.data(), buf.size());
  CHECK(num);
  CHECK(dh.setPublicKey(std::move(num)));
}

void SetPrivateKey(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  DiffieHellman* diffieHellman;
  ASSIGN_OR_RETURN_UNWRAP(&diffieHellman, args.This());
  DHPointer& dh = *diffieHellman;
  CHECK_EQ(args.Length(), 1);
  ArrayBufferOrViewContents<unsigned char> buf(args[0]);
  if (!buf.CheckSizeInt32()) [[unlikely]]
    return THROW_ERR_OUT_OF_RANGE(env, "buf is too big");
  BignumPointer num(buf.data(), buf.size());
  CHECK(num);
  CHECK(dh.setPrivateKey(std::move(num)));
}

void Check(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  DiffieHellman* diffieHellman;
  ASSIGN_OR_RETURN_UNWRAP(&diffieHellman, args.This());

  DHPointer& dh = *diffieHellman;
  auto result = dh.check();
  if (result == DHPointer::CheckResult::CHECK_FAILED) {
    return THROW_ERR_CRYPTO_OPERATION_FAILED(env,
                                             "Checking DH parameters failed");
  }

  args.GetReturnValue().Set(static_cast<int>(result));
}

}  // namespace

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
Maybe<void> DhKeyGenTraits::AdditionalConfig(
    CryptoJobMode mode,
    const FunctionCallbackInfo<Value>& args,
    unsigned int* offset,
    DhKeyPairGenConfig* params) {
  Environment* env = Environment::GetCurrent(args);

  if (args[*offset]->IsString()) {
    Utf8Value group_name(env->isolate(), args[*offset]);
    auto group = DHPointer::FindGroup(group_name.ToStringView());
    if (!group) {
      THROW_ERR_CRYPTO_UNKNOWN_DH_GROUP(env);
      return Nothing<void>();
    }

    static constexpr int kStandardizedGenerator = 2;

    params->params.prime = std::move(group);
    params->params.generator = kStandardizedGenerator;
    *offset += 1;
  } else {
    if (args[*offset]->IsInt32()) {
      int size = args[*offset].As<Int32>()->Value();
      if (size < 0) {
        THROW_ERR_OUT_OF_RANGE(env, "Invalid prime size");
        return Nothing<void>();
      }
      params->params.prime = size;
    } else {
      ArrayBufferOrViewContents<unsigned char> input(args[*offset]);
      if (!input.CheckSizeInt32()) [[unlikely]] {
        THROW_ERR_OUT_OF_RANGE(env, "prime is too big");
        return Nothing<void>();
      }
      params->params.prime = BignumPointer(input.data(), input.size());
    }

    CHECK(args[*offset + 1]->IsInt32());
    params->params.generator = args[*offset + 1].As<Int32>()->Value();
    *offset += 2;
  }

  return JustVoid();
}

EVPKeyCtxPointer DhKeyGenTraits::Setup(DhKeyPairGenConfig* params) {
  EVPKeyPointer key_params;
  if (BignumPointer* prime_fixed_value =
          std::get_if<BignumPointer>(&params->params.prime)) {
    auto prime = prime_fixed_value->clone();
    auto bn_g = BignumPointer::New();
    if (!prime || !bn_g || !bn_g.setWord(params->params.generator)) {
      return {};
    }
    auto dh = DHPointer::New(std::move(prime), std::move(bn_g));
    if (!dh) return {};

    key_params = EVPKeyPointer::New();
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
      return {};
    }

    key_params = EVPKeyPointer(raw_params);
  } else {
    UNREACHABLE();
  }

  EVPKeyCtxPointer ctx = key_params.newCtx();
  if (!ctx || EVP_PKEY_keygen_init(ctx.get()) <= 0) return {};

  return ctx;
}

Maybe<void> DHKeyExportTraits::AdditionalConfig(
    const FunctionCallbackInfo<Value>& args,
    unsigned int offset,
    DHKeyExportConfig* params) {
  return JustVoid();
}

WebCryptoKeyExportStatus DHKeyExportTraits::DoExport(
    const KeyObjectData& key_data,
    WebCryptoKeyFormat format,
    const DHKeyExportConfig& params,
    ByteSource* out) {
  CHECK_NE(key_data.GetKeyType(), kKeyTypeSecret);

  switch (format) {
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

namespace {
ByteSource StatelessDiffieHellmanThreadsafe(const EVPKeyPointer& our_key,
                                            const EVPKeyPointer& their_key) {
  auto dp = DHPointer::stateless(our_key, their_key);
  if (!dp) return {};

  return ByteSource::Allocated(dp.release());
}

void Stateless(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  CHECK(args[0]->IsObject() && args[1]->IsObject());
  KeyObjectHandle* our_key_object;
  ASSIGN_OR_RETURN_UNWRAP(&our_key_object, args[0].As<Object>());
  CHECK_EQ(our_key_object->Data().GetKeyType(), kKeyTypePrivate);
  KeyObjectHandle* their_key_object;
  ASSIGN_OR_RETURN_UNWRAP(&their_key_object, args[1].As<Object>());
  CHECK_NE(their_key_object->Data().GetKeyType(), kKeyTypeSecret);

  const auto& our_key = our_key_object->Data().GetAsymmetricKey();
  const auto& their_key = their_key_object->Data().GetAsymmetricKey();

  Local<Value> out;
  if (!StatelessDiffieHellmanThreadsafe(our_key, their_key)
          .ToBuffer(env)
              .ToLocal(&out)) return;

  if (Buffer::Length(out) == 0)
    return ThrowCryptoError(env, ERR_get_error(), "diffieHellman failed");

  args.GetReturnValue().Set(out);
}
}  // namespace

Maybe<void> DHBitsTraits::AdditionalConfig(
    CryptoJobMode mode,
    const FunctionCallbackInfo<Value>& args,
    unsigned int offset,
    DHBitsConfig* params) {
  Environment* env = Environment::GetCurrent(args);

  CHECK(args[offset]->IsObject());  // public key
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

  params->public_key = public_key->Data().addRef();
  params->private_key = private_key->Data().addRef();

  return JustVoid();
}

MaybeLocal<Value> DHBitsTraits::EncodeOutput(Environment* env,
                                             const DHBitsConfig& params,
                                             ByteSource* out) {
  return out->ToArrayBuffer(env);
}

bool DHBitsTraits::DeriveBits(
    Environment* env,
    const DHBitsConfig& params,
    ByteSource* out) {
  *out = StatelessDiffieHellmanThreadsafe(params.private_key.GetAsymmetricKey(),
                                          params.public_key.GetAsymmetricKey());
  return true;
}

Maybe<void> GetDhKeyDetail(Environment* env,
                           const KeyObjectData& key,
                           Local<Object> target) {
  CHECK_EQ(key.GetAsymmetricKey().id(), EVP_PKEY_DH);
  return JustVoid();
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
                              Check,
                              Local<Value>(),
                              Signature::New(env->isolate(), t),
                              /* length */ 0,
                              ConstructorBehavior::kThrow,
                              SideEffectType::kHasNoSideEffect);

    t->InstanceTemplate()->SetAccessorProperty(env->verify_error_string(),
                                               verify_error_getter_templ,
                                               Local<FunctionTemplate>(),
                                               attributes);

    SetConstructorFunction(context, target, name, t);
  };

  make(FIXED_ONE_BYTE_STRING(env->isolate(), "DiffieHellman"), New);
  make(FIXED_ONE_BYTE_STRING(env->isolate(), "DiffieHellmanGroup"),
       DiffieHellmanGroup);

  SetMethodNoSideEffect(context, target, "statelessDH", Stateless);
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

  registry->Register(Check);
  registry->Register(Stateless);

  DHKeyPairGenJob::RegisterExternalReferences(registry);
  DHKeyExportJob::RegisterExternalReferences(registry);
  DHBitsJob::RegisterExternalReferences(registry);
}

}  // namespace crypto
}  // namespace node
