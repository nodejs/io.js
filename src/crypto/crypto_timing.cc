#include "crypto/crypto_timing.h"
#include "crypto/crypto_util.h"
#include "env-inl.h"
#include "node.h"
#include "node_debug.h"
#include "node_errors.h"
#include "v8.h"

#include <openssl/crypto.h>

namespace node {

using v8::CFunction;
using v8::FastApiCallbackOptions;
using v8::FunctionCallbackInfo;
using v8::HandleScope;
using v8::Local;
using v8::Object;
using v8::Value;

namespace crypto {
namespace Timing {
void TimingSafeEqual(const FunctionCallbackInfo<Value>& args) {
  // Moving the type checking into JS leads to test failures, most likely due
  // to V8 inlining certain parts of the wrapper. Therefore, keep them in C++.
  // Refs: https://github.com/nodejs/node/issues/34073.
  Environment* env = Environment::GetCurrent(args);
  if (!IsAnyBufferSource(args[0])) {
    THROW_ERR_INVALID_ARG_TYPE(
      env, "The \"buf1\" argument must be an instance of "
      "ArrayBuffer, Buffer, TypedArray, or DataView.");
    return;
  }
  if (!IsAnyBufferSource(args[1])) {
    THROW_ERR_INVALID_ARG_TYPE(
      env, "The \"buf2\" argument must be an instance of "
      "ArrayBuffer, Buffer, TypedArray, or DataView.");
    return;
  }

  ArrayBufferOrViewContents<char> buf1(args[0]);
  ArrayBufferOrViewContents<char> buf2(args[1]);

  if (buf1.size() != buf2.size()) {
    THROW_ERR_CRYPTO_TIMING_SAFE_EQUAL_LENGTH(env);
    return;
  }

  return args.GetReturnValue().Set(
      CRYPTO_memcmp(buf1.data(), buf2.data(), buf1.size()) == 0);
}

bool FastTimingSafeEqual(Local<Value> receiver,
                         Local<Value> a_obj,
                         Local<Value> b_obj,
                         // NOLINTNEXTLINE(runtime/references)
                         FastApiCallbackOptions& options) {
  HandleScope scope(options.isolate);
  ArrayBufferViewContents<uint8_t> a(a_obj);
  ArrayBufferViewContents<uint8_t> b(b_obj);
  if (a.length() != b.length()) {
    TRACK_V8_FAST_API_CALL("crypto.timingSafeEqual.error");
    THROW_ERR_CRYPTO_TIMING_SAFE_EQUAL_LENGTH(options.isolate);
    return false;
  }

  TRACK_V8_FAST_API_CALL("crypto.timingSafeEqual.ok");
  return CRYPTO_memcmp(a.data(), b.data(), a.length()) == 0;
}

static CFunction fast_equal(CFunction::Make(FastTimingSafeEqual));

void Initialize(Environment* env, Local<Object> target) {
  SetFastMethodNoSideEffect(
      env->context(), target, "timingSafeEqual", TimingSafeEqual, &fast_equal);
}
void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(TimingSafeEqual);
  registry->Register(FastTimingSafeEqual);
  registry->Register(fast_equal.GetTypeInfo());
}
}  // namespace Timing

}  // namespace crypto
}  // namespace node
