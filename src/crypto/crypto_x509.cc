#include "crypto/crypto_x509.h"
#include "base_object-inl.h"
#include "crypto/crypto_common.h"
#include "crypto/crypto_keys.h"
#include "crypto/crypto_util.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "ncrypto.h"
#include "node_errors.h"
#include "util-inl.h"
#include "v8.h"

#include <string>
#include <vector>

namespace node {

using ncrypto::BignumPointer;
using ncrypto::BIOPointer;
using ncrypto::ClearErrorOnReturn;
using ncrypto::DataPointer;
using ncrypto::ECKeyPointer;
using ncrypto::SSLPointer;
using ncrypto::StackOfASN1;
using ncrypto::X509Pointer;
using ncrypto::X509View;
using v8::Array;
using v8::ArrayBuffer;
using v8::ArrayBufferView;
using v8::BackingStore;
using v8::Boolean;
using v8::Context;
using v8::Date;
using v8::EscapableHandleScope;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::MaybeLocal;
using v8::NewStringType;
using v8::Object;
using v8::String;
using v8::Uint32;
using v8::Value;

namespace crypto {

ManagedX509::ManagedX509(X509Pointer&& cert) : cert_(std::move(cert)) {}

ManagedX509::ManagedX509(const ManagedX509& that) {
  *this = that;
}

ManagedX509& ManagedX509::operator=(const ManagedX509& that) {
  cert_.reset(that.get());

  if (cert_)
    X509_up_ref(cert_.get());

  return *this;
}

void ManagedX509::MemoryInfo(MemoryTracker* tracker) const {
  // This is an approximation based on the der encoding size.
  int size = i2d_X509(cert_.get(), nullptr);
  tracker->TrackFieldWithSize("cert", size);
}

namespace {
MaybeLocal<Value> GetFingerprintDigest(Environment* env,
                                       const EVP_MD* method,
                                       const X509View& cert) {
  auto fingerprint = cert.getFingerprint(method);
  // Returning an empty string indicates that the digest failed for
  // some reason.
  if (!fingerprint.has_value()) [[unlikely]] {
    return Undefined(env->isolate());
  }
  auto& fp = fingerprint.value();
  return OneByteString(env->isolate(), fp.data(), fp.length());
}

template <const EVP_MD* (*algo)()>
void Fingerprint(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  X509Certificate* cert;
  ASSIGN_OR_RETURN_UNWRAP(&cert, args.This());
  Local<Value> ret;
  if (GetFingerprintDigest(env, algo(), cert->view()).ToLocal(&ret)) {
    args.GetReturnValue().Set(ret);
  }
}

MaybeLocal<Value> ToV8Value(Local<Context> context, BIOPointer&& bio) {
  if (!bio) return {};
  BUF_MEM* mem = bio;
  Local<Value> ret;
  if (!String::NewFromUtf8(context->GetIsolate(),
                           mem->data,
                           NewStringType::kNormal,
                           mem->length)
           .ToLocal(&ret))
    return {};
  return ret;
}

MaybeLocal<Value> ToV8Value(Local<Context> context, const ASN1_OBJECT* obj) {
  // If OpenSSL knows the type, use the short name of the type as the key, and
  // the numeric representation of the type's OID otherwise.
  int nid = OBJ_obj2nid(obj);
  char buf[80];
  const char* str;
  if (nid != NID_undef) {
    str = OBJ_nid2sn(nid);
    CHECK_NOT_NULL(str);
  } else {
    OBJ_obj2txt(buf, sizeof(buf), obj, true);
    str = buf;
  }

  Local<Value> result;
  if (!String::NewFromUtf8(context->GetIsolate(), str).ToLocal(&result)) {
    return {};
  }
  return result;
}

MaybeLocal<Value> ToV8Value(Local<Context> context, const ASN1_STRING* str) {
  // The previous implementation used X509_NAME_print_ex, which escapes some
  // characters in the value. The old implementation did not decode/unescape
  // values correctly though, leading to ambiguous and incorrect
  // representations. The new implementation only converts to Unicode and does
  // not escape anything.
  unsigned char* value_str;
  int value_str_size = ASN1_STRING_to_UTF8(&value_str, str);
  if (value_str_size < 0) {
    return Undefined(context->GetIsolate());
  }
  DataPointer free_value_str(value_str, value_str_size);

  Local<Value> result;
  if (!String::NewFromUtf8(context->GetIsolate(),
                           reinterpret_cast<const char*>(value_str),
                           NewStringType::kNormal,
                           value_str_size)
           .ToLocal(&result)) {
    return {};
  }
  return result;
}

MaybeLocal<Value> ToV8Value(Local<Context> context, const BIOPointer& bio) {
  if (!bio) return {};
  BUF_MEM* mem = bio;
  Local<Value> ret;
  if (!String::NewFromUtf8(context->GetIsolate(),
                           mem->data,
                           NewStringType::kNormal,
                           mem->length)
           .ToLocal(&ret))
    return {};
  return ret;
}

MaybeLocal<Value> ToBuffer(Environment* env, BIOPointer* bio) {
  if (bio == nullptr || !*bio) return {};
  BUF_MEM* mem = *bio;
  auto backing = ArrayBuffer::NewBackingStore(
      mem->data,
      mem->length,
      [](void*, size_t, void* data) {
        BIOPointer free_me(static_cast<BIO*>(data));
      },
      bio->release());
  auto ab = ArrayBuffer::New(env->isolate(), std::move(backing));
  Local<Value> ret;
  if (!Buffer::New(env, ab, 0, ab->ByteLength()).ToLocal(&ret)) return {};
  return ret;
}

MaybeLocal<Value> GetDer(Environment* env, const X509View& view) {
  Local<Value> ret;
  auto bio = view.toDER();
  if (!bio) return Undefined(env->isolate());
  if (!ToBuffer(env, &bio).ToLocal(&ret)) {
    return {};
  }
  return ret;
}

MaybeLocal<Value> GetSubjectAltNameString(Environment* env,
                                          const X509View& view) {
  Local<Value> ret;
  auto bio = view.getSubjectAltName();
  if (!bio) return Undefined(env->isolate());
  if (!ToV8Value(env->context(), bio).ToLocal(&ret)) return {};
  return ret;
}

MaybeLocal<Value> GetInfoAccessString(Environment* env, const X509View& view) {
  Local<Value> ret;
  auto bio = view.getInfoAccess();
  if (!bio) return Undefined(env->isolate());
  if (!ToV8Value(env->context(), bio).ToLocal(&ret)) {
    return {};
  }
  return ret;
}

MaybeLocal<Value> GetValidFrom(Environment* env, const X509View& view) {
  Local<Value> ret;
  auto bio = view.getValidFrom();
  if (!bio) return Undefined(env->isolate());
  if (!ToV8Value(env->context(), bio).ToLocal(&ret)) {
    return {};
  }
  return ret;
}

MaybeLocal<Value> GetValidTo(Environment* env, const X509View& view) {
  Local<Value> ret;
  auto bio = view.getValidTo();
  if (!bio) return Undefined(env->isolate());
  if (!ToV8Value(env->context(), bio).ToLocal(&ret)) {
    return {};
  }
  return ret;
}

MaybeLocal<Value> GetValidFromDate(Environment* env, const X509View& view) {
  int64_t validFromTime = view.getValidFromTime();
  return Date::New(env->context(), validFromTime * 1000.);
}

MaybeLocal<Value> GetValidToDate(Environment* env, const X509View& view) {
  int64_t validToTime = view.getValidToTime();
  return Date::New(env->context(), validToTime * 1000.);
}

MaybeLocal<Value> GetSerialNumber(Environment* env, const X509View& view) {
  if (auto serial = view.getSerialNumber()) {
    return OneByteString(env->isolate(),
                         static_cast<unsigned char*>(serial.get()));
  }
  return Undefined(env->isolate());
}

MaybeLocal<Value> GetKeyUsage(Environment* env, const X509View& cert) {
  StackOfASN1 eku(static_cast<STACK_OF(ASN1_OBJECT)*>(
      X509_get_ext_d2i(cert.get(), NID_ext_key_usage, nullptr, nullptr)));
  if (eku) {
    const int count = sk_ASN1_OBJECT_num(eku.get());
    MaybeStackBuffer<Local<Value>, 16> ext_key_usage(count);
    char buf[256];

    int j = 0;
    for (int i = 0; i < count; i++) {
      if (OBJ_obj2txt(
              buf, sizeof(buf), sk_ASN1_OBJECT_value(eku.get(), i), 1) >= 0) {
        ext_key_usage[j++] = OneByteString(env->isolate(), buf);
      }
    }

    return Array::New(env->isolate(), ext_key_usage.out(), count);
  }

  return Undefined(env->isolate());
}

void Pem(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  X509Certificate* cert;
  ASSIGN_OR_RETURN_UNWRAP(&cert, args.This());
  Local<Value> ret;
  if (ToV8Value(env->context(), cert->view().toPEM()).ToLocal(&ret)) {
    args.GetReturnValue().Set(ret);
  }
}

void Der(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  X509Certificate* cert;
  ASSIGN_OR_RETURN_UNWRAP(&cert, args.This());
  Local<Value> ret;
  if (GetDer(env, cert->view()).ToLocal(&ret)) {
    args.GetReturnValue().Set(ret);
  }
}

void Subject(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  X509Certificate* cert;
  ASSIGN_OR_RETURN_UNWRAP(&cert, args.This());
  Local<Value> ret;
  if (ToV8Value(env->context(), cert->view().getSubject()).ToLocal(&ret)) {
    args.GetReturnValue().Set(ret);
  }
}

void SubjectAltName(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  X509Certificate* cert;
  ASSIGN_OR_RETURN_UNWRAP(&cert, args.This());
  Local<Value> ret;
  if (GetSubjectAltNameString(env, cert->view()).ToLocal(&ret)) {
    args.GetReturnValue().Set(ret);
  }
}

void Issuer(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  X509Certificate* cert;
  ASSIGN_OR_RETURN_UNWRAP(&cert, args.This());
  Local<Value> ret;
  if (ToV8Value(env->context(), cert->view().getIssuer()).ToLocal(&ret)) {
    args.GetReturnValue().Set(ret);
  }
}

void InfoAccess(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  X509Certificate* cert;
  ASSIGN_OR_RETURN_UNWRAP(&cert, args.This());
  Local<Value> ret;
  if (GetInfoAccessString(env, cert->view()).ToLocal(&ret)) {
    args.GetReturnValue().Set(ret);
  }
}

void ValidFrom(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  X509Certificate* cert;
  ASSIGN_OR_RETURN_UNWRAP(&cert, args.This());
  Local<Value> ret;
  if (GetValidFrom(env, cert->view()).ToLocal(&ret)) {
    args.GetReturnValue().Set(ret);
  }
}

void ValidTo(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  X509Certificate* cert;
  ASSIGN_OR_RETURN_UNWRAP(&cert, args.This());
  Local<Value> ret;
  if (GetValidTo(env, cert->view()).ToLocal(&ret)) {
    args.GetReturnValue().Set(ret);
  }
}

void ValidFromDate(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  X509Certificate* cert;
  ASSIGN_OR_RETURN_UNWRAP(&cert, args.This());
  Local<Value> ret;
  if (GetValidFromDate(env, cert->view()).ToLocal(&ret)) {
    args.GetReturnValue().Set(ret);
  }
}

void ValidToDate(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  X509Certificate* cert;
  ASSIGN_OR_RETURN_UNWRAP(&cert, args.This());
  Local<Value> ret;
  if (GetValidToDate(env, cert->view()).ToLocal(&ret)) {
    args.GetReturnValue().Set(ret);
  }
}

void SerialNumber(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  X509Certificate* cert;
  ASSIGN_OR_RETURN_UNWRAP(&cert, args.This());
  Local<Value> ret;
  if (GetSerialNumber(env, cert->view()).ToLocal(&ret)) {
    args.GetReturnValue().Set(ret);
  }
}

void PublicKey(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  X509Certificate* cert;
  ASSIGN_OR_RETURN_UNWRAP(&cert, args.This());

  // TODO(tniessen): consider checking X509_get_pubkey() when the
  // X509Certificate object is being created.
  auto result = cert->view().getPublicKey();
  if (!result.value) {
    ThrowCryptoError(env, result.error.value_or(0));
    return;
  }
  auto key_data =
      KeyObjectData::CreateAsymmetric(kKeyTypePublic, std::move(result.value));

  Local<Value> ret;
  if (key_data && KeyObjectHandle::Create(env, key_data).ToLocal(&ret)) {
    args.GetReturnValue().Set(ret);
  }
}

void KeyUsage(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  X509Certificate* cert;
  ASSIGN_OR_RETURN_UNWRAP(&cert, args.This());
  Local<Value> ret;
  if (GetKeyUsage(env, cert->view()).ToLocal(&ret)) {
    args.GetReturnValue().Set(ret);
  }
}

void CheckCA(const FunctionCallbackInfo<Value>& args) {
  X509Certificate* cert;
  ASSIGN_OR_RETURN_UNWRAP(&cert, args.This());
  args.GetReturnValue().Set(cert->view().isCA());
}

void CheckIssued(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  X509Certificate* cert;
  ASSIGN_OR_RETURN_UNWRAP(&cert, args.This());
  CHECK(args[0]->IsObject());
  CHECK(X509Certificate::HasInstance(env, args[0].As<Object>()));
  X509Certificate* issuer;
  ASSIGN_OR_RETURN_UNWRAP(&issuer, args[0]);
  args.GetReturnValue().Set(cert->view().isIssuedBy(issuer->view()));
}

void CheckPrivateKey(const FunctionCallbackInfo<Value>& args) {
  X509Certificate* cert;
  ASSIGN_OR_RETURN_UNWRAP(&cert, args.This());
  CHECK(args[0]->IsObject());
  KeyObjectHandle* key;
  ASSIGN_OR_RETURN_UNWRAP(&key, args[0]);
  CHECK_EQ(key->Data().GetKeyType(), kKeyTypePrivate);
  args.GetReturnValue().Set(
      cert->view().checkPrivateKey(key->Data().GetAsymmetricKey()));
}

void CheckPublicKey(const FunctionCallbackInfo<Value>& args) {
  X509Certificate* cert;
  ASSIGN_OR_RETURN_UNWRAP(&cert, args.This());

  CHECK(args[0]->IsObject());
  KeyObjectHandle* key;
  ASSIGN_OR_RETURN_UNWRAP(&key, args[0]);
  // A Public Key can be derived from a private key, so we allow both.
  CHECK_NE(key->Data().GetKeyType(), kKeyTypeSecret);

  args.GetReturnValue().Set(
      cert->view().checkPublicKey(key->Data().GetAsymmetricKey()));
}

void CheckHost(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  X509Certificate* cert;
  ASSIGN_OR_RETURN_UNWRAP(&cert, args.This());

  CHECK(args[0]->IsString());  // name
  CHECK(args[1]->IsUint32());  // flags

  Utf8Value name(env->isolate(), args[0]);
  uint32_t flags = args[1].As<Uint32>()->Value();
  DataPointer peername;

  switch (cert->view().checkHost(name.ToStringView(), flags, &peername)) {
    case X509View::CheckMatch::MATCH: {  // Match!
      Local<Value> ret = args[0];
      if (peername) {
        ret = OneByteString(env->isolate(),
                            static_cast<const char*>(peername.get()),
                            peername.size());
      }
      return args.GetReturnValue().Set(ret);
    }
    case X509View::CheckMatch::NO_MATCH:  // No Match!
      return;  // No return value is set
    case X509View::CheckMatch::INVALID_NAME:  // Error!
      return THROW_ERR_INVALID_ARG_VALUE(env, "Invalid name");
    default:  // Error!
      return THROW_ERR_CRYPTO_OPERATION_FAILED(env);
  }
}

void CheckEmail(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  X509Certificate* cert;
  ASSIGN_OR_RETURN_UNWRAP(&cert, args.This());

  CHECK(args[0]->IsString());  // name
  CHECK(args[1]->IsUint32());  // flags

  Utf8Value name(env->isolate(), args[0]);
  uint32_t flags = args[1].As<Uint32>()->Value();

  switch (cert->view().checkEmail(name.ToStringView(), flags)) {
    case X509View::CheckMatch::MATCH:  // Match!
      return args.GetReturnValue().Set(args[0]);
    case X509View::CheckMatch::NO_MATCH:  // No Match!
      return;  // No return value is set
    case X509View::CheckMatch::INVALID_NAME:  // Error!
      return THROW_ERR_INVALID_ARG_VALUE(env, "Invalid name");
    default:  // Error!
      return THROW_ERR_CRYPTO_OPERATION_FAILED(env);
  }
}

void CheckIP(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  X509Certificate* cert;
  ASSIGN_OR_RETURN_UNWRAP(&cert, args.This());

  CHECK(args[0]->IsString());  // IP
  CHECK(args[1]->IsUint32());  // flags

  Utf8Value name(env->isolate(), args[0]);
  uint32_t flags = args[1].As<Uint32>()->Value();

  switch (cert->view().checkIp(name.ToStringView(), flags)) {
    case X509View::CheckMatch::MATCH:  // Match!
      return args.GetReturnValue().Set(args[0]);
    case X509View::CheckMatch::NO_MATCH:  // No Match!
      return;  // No return value is set
    case X509View::CheckMatch::INVALID_NAME:  // Error!
      return THROW_ERR_INVALID_ARG_VALUE(env, "Invalid IP");
    default:  // Error!
      return THROW_ERR_CRYPTO_OPERATION_FAILED(env);
  }
}

void GetIssuerCert(const FunctionCallbackInfo<Value>& args) {
  X509Certificate* cert;
  ASSIGN_OR_RETURN_UNWRAP(&cert, args.This());
  auto issuer = cert->getIssuerCert();
  if (issuer) args.GetReturnValue().Set(issuer->object());
}

void Parse(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(args[0]->IsArrayBufferView());
  ArrayBufferViewContents<unsigned char> buf(args[0].As<ArrayBufferView>());
  Local<Object> cert;

  auto result = X509Pointer::Parse(ncrypto::Buffer<const unsigned char>{
      .data = buf.data(),
      .len = buf.length(),
  });

  if (!result.value) return ThrowCryptoError(env, result.error.value_or(0));

  if (X509Certificate::New(env, std::move(result.value)).ToLocal(&cert)) {
    args.GetReturnValue().Set(cert);
  }
}

void ToLegacy(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  X509Certificate* cert;
  ASSIGN_OR_RETURN_UNWRAP(&cert, args.This());
  ClearErrorOnReturn clear_error_on_return;
  Local<Value> ret;
  if (cert->toObject(env).ToLocal(&ret)) {
    args.GetReturnValue().Set(ret);
  }
}

template <typename T>
bool Set(Environment* env,
         Local<Object> target,
         Local<Value> name,
         MaybeLocal<T> maybe_value) {
  Local<Value> value;
  if (!maybe_value.ToLocal(&value)) return false;

  // Undefined is ignored, but still considered successful
  if (value->IsUndefined()) return true;

  return !target->Set(env->context(), name, value).IsNothing();
}

template <typename T>
bool Set(Environment* env,
         Local<Object> target,
         uint32_t index,
         MaybeLocal<T> maybe_value) {
  Local<Value> value;
  if (!maybe_value.ToLocal(&value)) return false;

  // Undefined is ignored, but still considered successful
  if (value->IsUndefined()) return true;

  return !target->Set(env->context(), index, value).IsNothing();
}

// Convert an X509_NAME* into a JavaScript object.
// Each entry of the name is converted into a property of the object.
// The property value may be a single string or an array of strings.
template <X509_NAME* get_name(const X509*)>
static MaybeLocal<Value> GetX509NameObject(Environment* env,
                                           const X509View& cert) {
  X509_NAME* name = get_name(cert.get());
  CHECK_NOT_NULL(name);

  int cnt = X509_NAME_entry_count(name);
  CHECK_GE(cnt, 0);

  Local<Value> v8_name;
  Local<Value> v8_value;
  // Note the the resulting object uses a null prototype.
  Local<Object> result =
      Object::New(env->isolate(), Null(env->isolate()), nullptr, nullptr, 0);
  if (result.IsEmpty()) return {};

  for (int i = 0; i < cnt; i++) {
    X509_NAME_ENTRY* entry = X509_NAME_get_entry(name, i);
    CHECK_NOT_NULL(entry);

    if (!ToV8Value(env->context(), X509_NAME_ENTRY_get_object(entry))
             .ToLocal(&v8_name) ||
        !ToV8Value(env->context(), X509_NAME_ENTRY_get_data(entry))
             .ToLocal(&v8_value)) {
      return {};
    }

    // For backward compatibility, we only create arrays if multiple values
    // exist for the same key. That is not great but there is not much we can
    // change here without breaking things. Note that this creates nested data
    // structures, yet still does not allow representing Distinguished Names
    // accurately.
    bool multiple;
    if (!result->Has(env->context(), v8_name).To(&multiple)) {
      return {};
    }

    if (multiple) {
      Local<Value> accum;
      if (!result->Get(env->context(), v8_name).ToLocal(&accum)) {
        return {};
      }
      if (!accum->IsArray()) {
        Local<Value> items[] = {
            accum,
            v8_value,
        };
        accum = Array::New(env->isolate(), items, arraysize(items));
        if (!Set<Value>(env, result, v8_name, accum)) {
          return {};
        }
      } else {
        Local<Array> array = accum.As<Array>();
        if (!Set<Value>(env, array, array->Length(), v8_value)) {
          return {};
        }
      }
      continue;
    }

    if (!Set<Value>(env, result, v8_name, v8_value)) {
      return {};
    }
  }

  return result;
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

MaybeLocal<Value> GetModulusString(Environment* env, const BIGNUM* n) {
  auto bio = BIOPointer::New(n);
  if (!bio) return {};
  return ToV8Value(env->context(), bio);
}

MaybeLocal<Value> GetExponentString(Environment* env, const BIGNUM* e) {
  uint64_t exponent_word = static_cast<uint64_t>(BignumPointer::GetWord(e));
  auto bio = BIOPointer::NewMem();
  if (!bio) return {};
  BIO_printf(bio.get(), "0x%" PRIx64, exponent_word);
  return ToV8Value(env->context(), bio);
}

MaybeLocal<Value> GetECPubKey(Environment* env,
                              const EC_GROUP* group,
                              OSSL3_CONST EC_KEY* ec) {
  const auto pubkey = ECKeyPointer::GetPublicKey(ec);
  if (pubkey == nullptr) return Undefined(env->isolate());

  return ECPointToBuffer(env, group, pubkey, EC_KEY_get_conv_form(ec), nullptr)
      .FromMaybe(Local<Object>());
}

MaybeLocal<Value> GetECGroupBits(Environment* env, const EC_GROUP* group) {
  if (group == nullptr) return Undefined(env->isolate());

  int bits = EC_GROUP_order_bits(group);
  if (bits <= 0) return Undefined(env->isolate());

  return Integer::New(env->isolate(), bits);
}

template <const char* (*nid2string)(int nid)>
MaybeLocal<Value> GetCurveName(Environment* env, const int nid) {
  const char* name = nid2string(nid);
  return name != nullptr
             ? MaybeLocal<Value>(OneByteString(env->isolate(), name))
             : MaybeLocal<Value>(Undefined(env->isolate()));
}

MaybeLocal<Object> X509ToObject(Environment* env, const X509View& cert) {
  EscapableHandleScope scope(env->isolate());
  Local<Object> info = Object::New(env->isolate());

  if (!Set<Value>(env,
                  info,
                  env->subject_string(),
                  GetX509NameObject<X509_get_subject_name>(env, cert)) ||
      !Set<Value>(env,
                  info,
                  env->issuer_string(),
                  GetX509NameObject<X509_get_issuer_name>(env, cert)) ||
      !Set<Value>(env,
                  info,
                  env->subjectaltname_string(),
                  GetSubjectAltNameString(env, cert)) ||
      !Set<Value>(env,
                  info,
                  env->infoaccess_string(),
                  GetInfoAccessString(env, cert)) ||
      !Set<Boolean>(env,
                    info,
                    env->ca_string(),
                    Boolean::New(env->isolate(), cert.isCA()))) {
    return {};
  }

  OSSL3_CONST EVP_PKEY* pkey = X509_get0_pubkey(cert.get());
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
    if (!Set<Value>(
            env, info, env->modulus_string(), GetModulusString(env, n)) ||
        !Set<Value>(
            env,
            info,
            env->bits_string(),
            Integer::New(env->isolate(), BignumPointer::GetBitCount(n))) ||
        !Set<Value>(
            env, info, env->exponent_string(), GetExponentString(env, e)) ||
        !Set<Object>(env, info, env->pubkey_string(), GetPubKey(env, rsa))) {
      return {};
    }
  } else if (ec) {
    const auto group = ECKeyPointer::GetGroup(ec);

    if (!Set<Value>(
            env, info, env->bits_string(), GetECGroupBits(env, group)) ||
        !Set<Value>(
            env, info, env->pubkey_string(), GetECPubKey(env, group, ec))) {
      return {};
    }

    const int nid = EC_GROUP_get_curve_name(group);
    if (nid != 0) {
      // Curve is well-known, get its OID and NIST nick-name (if it has one).

      if (!Set<Value>(env,
                      info,
                      env->asn1curve_string(),
                      GetCurveName<OBJ_nid2sn>(env, nid)) ||
          !Set<Value>(env,
                      info,
                      env->nistcurve_string(),
                      GetCurveName<EC_curve_nid2nist>(env, nid))) {
        return {};
      }
    } else {
      // Unnamed curves can be described by their mathematical properties,
      // but aren't used much (at all?) with X.509/TLS. Support later if needed.
    }
  }

  if (!Set<Value>(
          env, info, env->valid_from_string(), GetValidFrom(env, cert)) ||
      !Set<Value>(env, info, env->valid_to_string(), GetValidTo(env, cert)) ||
      !Set<Value>(env,
                  info,
                  env->fingerprint_string(),
                  GetFingerprintDigest(env, EVP_sha1(), cert)) ||
      !Set<Value>(env,
                  info,
                  env->fingerprint256_string(),
                  GetFingerprintDigest(env, EVP_sha256(), cert)) ||
      !Set<Value>(env,
                  info,
                  env->fingerprint512_string(),
                  GetFingerprintDigest(env, EVP_sha512(), cert)) ||
      !Set<Value>(
          env, info, env->ext_key_usage_string(), GetKeyUsage(env, cert)) ||
      !Set<Value>(
          env, info, env->serial_number_string(), GetSerialNumber(env, cert)) ||
      !Set<Value>(env, info, env->raw_string(), GetDer(env, cert))) {
    return {};
  }

  return scope.Escape(info);
}
}  // namespace

Local<FunctionTemplate> X509Certificate::GetConstructorTemplate(
    Environment* env) {
  Local<FunctionTemplate> tmpl = env->x509_constructor_template();
  if (tmpl.IsEmpty()) {
    Isolate* isolate = env->isolate();
    tmpl = NewFunctionTemplate(isolate, nullptr);
    tmpl->InstanceTemplate()->SetInternalFieldCount(
        BaseObject::kInternalFieldCount);
    tmpl->SetClassName(
        FIXED_ONE_BYTE_STRING(env->isolate(), "X509Certificate"));
    SetProtoMethodNoSideEffect(isolate, tmpl, "subject", Subject);
    SetProtoMethodNoSideEffect(isolate, tmpl, "subjectAltName", SubjectAltName);
    SetProtoMethodNoSideEffect(isolate, tmpl, "infoAccess", InfoAccess);
    SetProtoMethodNoSideEffect(isolate, tmpl, "issuer", Issuer);
    SetProtoMethodNoSideEffect(isolate, tmpl, "validTo", ValidTo);
    SetProtoMethodNoSideEffect(isolate, tmpl, "validFrom", ValidFrom);
    SetProtoMethodNoSideEffect(isolate, tmpl, "validToDate", ValidToDate);
    SetProtoMethodNoSideEffect(isolate, tmpl, "validFromDate", ValidFromDate);
    SetProtoMethodNoSideEffect(
        isolate, tmpl, "fingerprint", Fingerprint<EVP_sha1>);
    SetProtoMethodNoSideEffect(
        isolate, tmpl, "fingerprint256", Fingerprint<EVP_sha256>);
    SetProtoMethodNoSideEffect(
        isolate, tmpl, "fingerprint512", Fingerprint<EVP_sha512>);
    SetProtoMethodNoSideEffect(isolate, tmpl, "keyUsage", KeyUsage);
    SetProtoMethodNoSideEffect(isolate, tmpl, "serialNumber", SerialNumber);
    SetProtoMethodNoSideEffect(isolate, tmpl, "pem", Pem);
    SetProtoMethodNoSideEffect(isolate, tmpl, "raw", Der);
    SetProtoMethodNoSideEffect(isolate, tmpl, "publicKey", PublicKey);
    SetProtoMethodNoSideEffect(isolate, tmpl, "checkCA", CheckCA);
    SetProtoMethodNoSideEffect(isolate, tmpl, "checkHost", CheckHost);
    SetProtoMethodNoSideEffect(isolate, tmpl, "checkEmail", CheckEmail);
    SetProtoMethodNoSideEffect(isolate, tmpl, "checkIP", CheckIP);
    SetProtoMethodNoSideEffect(isolate, tmpl, "checkIssued", CheckIssued);
    SetProtoMethodNoSideEffect(
        isolate, tmpl, "checkPrivateKey", CheckPrivateKey);
    SetProtoMethodNoSideEffect(isolate, tmpl, "verify", CheckPublicKey);
    SetProtoMethodNoSideEffect(isolate, tmpl, "toLegacy", ToLegacy);
    SetProtoMethodNoSideEffect(isolate, tmpl, "getIssuerCert", GetIssuerCert);
    env->set_x509_constructor_template(tmpl);
  }
  return tmpl;
}

bool X509Certificate::HasInstance(Environment* env, Local<Object> object) {
  return GetConstructorTemplate(env)->HasInstance(object);
}

MaybeLocal<Object> X509Certificate::New(Environment* env,
                                        X509Pointer cert,
                                        STACK_OF(X509) * issuer_chain) {
  std::shared_ptr<ManagedX509> mcert(new ManagedX509(std::move(cert)));
  return New(env, std::move(mcert), issuer_chain);
}

MaybeLocal<Object> X509Certificate::New(Environment* env,
                                        std::shared_ptr<ManagedX509> cert,
                                        STACK_OF(X509) * issuer_chain) {
  EscapableHandleScope scope(env->isolate());
  Local<Function> ctor;
  if (!GetConstructorTemplate(env)->GetFunction(env->context()).ToLocal(&ctor))
    return MaybeLocal<Object>();

  Local<Object> obj;
  if (!ctor->NewInstance(env->context()).ToLocal(&obj))
    return MaybeLocal<Object>();

  new X509Certificate(env, obj, std::move(cert), issuer_chain);
  return scope.Escape(obj);
}

MaybeLocal<Object> X509Certificate::GetCert(Environment* env,
                                            const SSLPointer& ssl) {
  auto cert = X509View::From(ssl);
  if (!cert) return {};
  return New(env, cert.clone());
}

MaybeLocal<Object> X509Certificate::GetPeerCert(Environment* env,
                                                const SSLPointer& ssl,
                                                GetPeerCertificateFlag flag) {
  ClearErrorOnReturn clear_error_on_return;
  MaybeLocal<Object> maybe_cert;

  X509Pointer cert;
  if ((flag & GetPeerCertificateFlag::SERVER) ==
      GetPeerCertificateFlag::SERVER) {
    cert = X509Pointer::PeerFrom(ssl);
  }

  STACK_OF(X509)* ssl_certs = SSL_get_peer_cert_chain(ssl.get());
  if (!cert && (ssl_certs == nullptr || sk_X509_num(ssl_certs) == 0))
    return MaybeLocal<Object>();

  if (!cert) {
    cert.reset(sk_X509_value(ssl_certs, 0));
    sk_X509_delete(ssl_certs, 0);
  }

  return sk_X509_num(ssl_certs) ? New(env, std::move(cert), ssl_certs)
                                : New(env, std::move(cert));
}

v8::MaybeLocal<v8::Value> X509Certificate::toObject(Environment* env) {
  return toObject(env, view());
}

v8::MaybeLocal<v8::Value> X509Certificate::toObject(Environment* env,
                                                    const X509View& cert) {
  if (!cert) return {};
  return X509ToObject(env, cert).FromMaybe(Local<Value>());
}

X509Certificate::X509Certificate(
    Environment* env,
    Local<Object> object,
    std::shared_ptr<ManagedX509> cert,
    STACK_OF(X509)* issuer_chain)
    : BaseObject(env, object),
      cert_(std::move(cert)) {
  MakeWeak();

  if (issuer_chain != nullptr && sk_X509_num(issuer_chain)) {
    X509Pointer cert(X509_dup(sk_X509_value(issuer_chain, 0)));
    sk_X509_delete(issuer_chain, 0);
    Local<Object> obj = sk_X509_num(issuer_chain)
        ? X509Certificate::New(env, std::move(cert), issuer_chain)
            .ToLocalChecked()
        : X509Certificate::New(env, std::move(cert))
            .ToLocalChecked();
    issuer_cert_.reset(Unwrap<X509Certificate>(obj));
  }
}

void X509Certificate::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("cert", cert_);
}

BaseObjectPtr<BaseObject>
X509Certificate::X509CertificateTransferData::Deserialize(
    Environment* env,
    Local<Context> context,
    std::unique_ptr<worker::TransferData> self) {
  if (context != env->context()) {
    THROW_ERR_MESSAGE_TARGET_CONTEXT_UNAVAILABLE(env);
    return {};
  }

  Local<Value> handle;
  if (!X509Certificate::New(env, data_).ToLocal(&handle))
    return {};

  return BaseObjectPtr<BaseObject>(
      Unwrap<X509Certificate>(handle.As<Object>()));
}

BaseObject::TransferMode X509Certificate::GetTransferMode() const {
  return BaseObject::TransferMode::kCloneable;
}

std::unique_ptr<worker::TransferData> X509Certificate::CloneForMessaging()
    const {
  return std::make_unique<X509CertificateTransferData>(cert_);
}

void X509Certificate::Initialize(Environment* env, Local<Object> target) {
  SetMethod(env->context(), target, "parseX509", Parse);

  NODE_DEFINE_CONSTANT(target, X509_CHECK_FLAG_ALWAYS_CHECK_SUBJECT);
  NODE_DEFINE_CONSTANT(target, X509_CHECK_FLAG_NEVER_CHECK_SUBJECT);
  NODE_DEFINE_CONSTANT(target, X509_CHECK_FLAG_NO_WILDCARDS);
  NODE_DEFINE_CONSTANT(target, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
  NODE_DEFINE_CONSTANT(target, X509_CHECK_FLAG_MULTI_LABEL_WILDCARDS);
  NODE_DEFINE_CONSTANT(target, X509_CHECK_FLAG_SINGLE_LABEL_SUBDOMAINS);
}

void X509Certificate::RegisterExternalReferences(
    ExternalReferenceRegistry* registry) {
  registry->Register(Parse);
  registry->Register(Subject);
  registry->Register(SubjectAltName);
  registry->Register(InfoAccess);
  registry->Register(Issuer);
  registry->Register(ValidTo);
  registry->Register(ValidFrom);
  registry->Register(ValidToDate);
  registry->Register(ValidFromDate);
  registry->Register(Fingerprint<EVP_sha1>);
  registry->Register(Fingerprint<EVP_sha256>);
  registry->Register(Fingerprint<EVP_sha512>);
  registry->Register(KeyUsage);
  registry->Register(SerialNumber);
  registry->Register(Pem);
  registry->Register(Der);
  registry->Register(PublicKey);
  registry->Register(CheckCA);
  registry->Register(CheckHost);
  registry->Register(CheckEmail);
  registry->Register(CheckIP);
  registry->Register(CheckIssued);
  registry->Register(CheckPrivateKey);
  registry->Register(CheckPublicKey);
  registry->Register(ToLegacy);
  registry->Register(GetIssuerCert);
}
}  // namespace crypto
}  // namespace node
