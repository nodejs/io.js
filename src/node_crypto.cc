// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "node_crypto.h"
#include "async_wrap-inl.h"
#include "debug_utils-inl.h"
#include "memory_tracker-inl.h"
#include "node_external_reference.h"
#include "threadpoolwork-inl.h"
#include "v8.h"

namespace node {

using v8::Context;
using v8::Local;
using v8::Object;
using v8::Value;

namespace crypto {

#define CRYPTO_NAMESPACE_LIST_BASE(V)                                          \
  V(AES)                                                                       \
  V(CipherBase)                                                                \
  V(DiffieHellman)                                                             \
  V(DSAAlg)                                                                    \
  V(ECDH)                                                                      \
  V(Hash)                                                                      \
  V(HKDFJob)                                                                   \
  V(Hmac)                                                                      \
  V(Keygen)                                                                    \
  V(Keys)                                                                      \
  V(NativeKeyObject)                                                           \
  V(PBKDF2Job)                                                                 \
  V(Random)                                                                    \
  V(RSAAlg)                                                                    \
  V(SecureContext)                                                             \
  V(Sign)                                                                      \
  V(SPKAC)                                                                     \
  V(Timing)                                                                    \
  V(Util)                                                                      \
  V(Verify)                                                                    \
  V(X509Certificate)

#ifdef OPENSSL_NO_ARGON2
#define ARGON2_NAMESPACE_LIST(V)
#else
#define ARGON2_NAMESPACE_LIST(V) V(Argon2Job)
#endif  // OPENSSL_NO_ARGON2

#ifdef OPENSSL_NO_SCRYPT
#define SCRYPT_NAMESPACE_LIST(V)
#else
#define SCRYPT_NAMESPACE_LIST(V) V(ScryptJob)
#endif  // OPENSSL_NO_SCRYPT

#define CRYPTO_NAMESPACE_LIST(V)                                               \
  CRYPTO_NAMESPACE_LIST_BASE(V)                                                \
  ARGON2_NAMESPACE_LIST(V)                                                     \
  SCRYPT_NAMESPACE_LIST(V)

void Initialize(Local<Object> target,
                Local<Value> unused,
                Local<Context> context,
                void* priv) {
  Environment* env = Environment::GetCurrent(context);

  if (!InitCryptoOnce(env->isolate())) {
    return;
  }

#define V(Namespace) Namespace::Initialize(env, target);
  CRYPTO_NAMESPACE_LIST(V)
#undef V
}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
#define V(Namespace) Namespace::RegisterExternalReferences(registry);
  CRYPTO_NAMESPACE_LIST(V)
#undef V
}
}  // namespace crypto
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(crypto, node::crypto::Initialize)
NODE_BINDING_EXTERNAL_REFERENCE(crypto,
                                node::crypto::RegisterExternalReferences)
