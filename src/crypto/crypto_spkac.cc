#include "crypto/crypto_spkac.h"
#include "crypto/crypto_common.h"
#include "crypto/crypto_util.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "ncrypto.h"
#include "node.h"
#include "v8.h"

namespace node {

using ncrypto::BIOPointer;
using v8::Context;
using v8::FunctionCallbackInfo;
using v8::Local;
using v8::Object;
using v8::Value;

namespace crypto {
namespace SPKAC {

void VerifySpkac(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  ArrayBufferOrViewContents<char> input(args[0]);
  if (input.empty()) return args.GetReturnValue().SetEmptyString();

  if (!input.CheckSizeInt32()) [[unlikely]]
    return THROW_ERR_OUT_OF_RANGE(env, "spkac is too large");

  args.GetReturnValue().Set(ncrypto::VerifySpkac(input.data(), input.size()));
}

void ExportPublicKey(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  ArrayBufferOrViewContents<char> input(args[0]);
  if (input.empty()) return args.GetReturnValue().SetEmptyString();

  if (!input.CheckSizeInt32()) [[unlikely]]
    return THROW_ERR_OUT_OF_RANGE(env, "spkac is too large");

  BIOPointer bio = ncrypto::ExportPublicKey(input.data(), input.size());
  if (!bio) return args.GetReturnValue().SetEmptyString();

  auto pkey = ByteSource::FromBIO(bio);
  args.GetReturnValue().Set(pkey.ToBuffer(env).FromMaybe(Local<Value>()));
}

void ExportChallenge(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  ArrayBufferOrViewContents<char> input(args[0]);
  if (input.empty()) return args.GetReturnValue().SetEmptyString();

  if (!input.CheckSizeInt32()) [[unlikely]]
    return THROW_ERR_OUT_OF_RANGE(env, "spkac is too large");

  auto cert = ByteSource::Allocated(
      ncrypto::ExportChallenge(input.data(), input.size()));
  if (!cert)
    return args.GetReturnValue().SetEmptyString();

  Local<Value> outString =
      Encode(env->isolate(), cert.data<char>(), cert.size(), BUFFER);

  args.GetReturnValue().Set(outString);
}

void Initialize(Environment* env, Local<Object> target) {
  Local<Context> context = env->context();
  SetMethodNoSideEffect(context, target, "certVerifySpkac", VerifySpkac);
  SetMethodNoSideEffect(
      context, target, "certExportPublicKey", ExportPublicKey);
  SetMethodNoSideEffect(
      context, target, "certExportChallenge", ExportChallenge);
}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(VerifySpkac);
  registry->Register(ExportPublicKey);
  registry->Register(ExportChallenge);
}
}  // namespace SPKAC
}  // namespace crypto
}  // namespace node
