#include "crypto/crypto_hash.h"
#include "async_wrap-inl.h"
#include "base_object-inl.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "string_bytes.h"
#include "threadpoolwork-inl.h"
#include "v8.h"

#include <cstdio>

namespace node {

using ncrypto::EVPMDCtxPointer;
using ncrypto::MarkPopErrorOnReturn;
using v8::Context;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Int32;
using v8::Isolate;
using v8::Just;
using v8::JustVoid;
using v8::Local;
using v8::LocalVector;
using v8::Maybe;
using v8::MaybeLocal;
using v8::Name;
using v8::Nothing;
using v8::Object;
using v8::Uint32;
using v8::Value;

namespace crypto {
Hash::Hash(Environment* env, Local<Object> wrap) : BaseObject(env, wrap) {
  MakeWeak();
}

void Hash::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackFieldWithSize("mdctx", mdctx_ ? kSizeOf_EVP_MD_CTX : 0);
  tracker->TrackFieldWithSize("md", digest_ ? md_len_ : 0);
}

#if OPENSSL_VERSION_MAJOR >= 3
void PushAliases(const char* name, void* data) {
  static_cast<std::vector<std::string>*>(data)->push_back(name);
}

EVP_MD* GetCachedMDByID(Environment* env, size_t id) {
  CHECK_LT(id, env->evp_md_cache.size());
  EVP_MD* result = env->evp_md_cache[id].get();
  CHECK_NOT_NULL(result);
  return result;
}

struct MaybeCachedMD {
  EVP_MD* explicit_md = nullptr;
  const EVP_MD* implicit_md = nullptr;
  int32_t cache_id = -1;
};

MaybeCachedMD FetchAndMaybeCacheMD(Environment* env, const char* search_name) {
  const EVP_MD* implicit_md = EVP_get_digestbyname(search_name);
  if (!implicit_md) return {nullptr, nullptr, -1};

  const char* real_name = EVP_MD_get0_name(implicit_md);
  if (!real_name) return {nullptr, implicit_md, -1};

  auto it = env->alias_to_md_id_map.find(real_name);
  if (it != env->alias_to_md_id_map.end()) {
    size_t id = it->second;
    return {GetCachedMDByID(env, id), implicit_md, static_cast<int32_t>(id)};
  }

  // EVP_*_fetch() does not support alias names, so we need to pass it the
  // real/original algorithm name.
  // We use EVP_*_fetch() as a filter here because it will only return an
  // instance if the algorithm is supported by the public OpenSSL APIs (some
  // algorithms are used internally by OpenSSL and are also passed to this
  // callback).
  EVP_MD* explicit_md = EVP_MD_fetch(nullptr, real_name, nullptr);
  if (!explicit_md) return {nullptr, implicit_md, -1};

  // Cache the EVP_MD* fetched.
  env->evp_md_cache.emplace_back(explicit_md);
  size_t id = env->evp_md_cache.size() - 1;

  // Add all the aliases to the map to speed up next lookup.
  std::vector<std::string> aliases;
  EVP_MD_names_do_all(explicit_md, PushAliases, &aliases);
  for (const auto& alias : aliases) {
    env->alias_to_md_id_map.emplace(alias, id);
  }
  env->alias_to_md_id_map.emplace(search_name, id);

  return {explicit_md, implicit_md, static_cast<int32_t>(id)};
}

void SaveSupportedHashAlgorithmsAndCacheMD(const EVP_MD* md,
                                           const char* from,
                                           const char* to,
                                           void* arg) {
  if (!from) return;
  Environment* env = static_cast<Environment*>(arg);
  auto result = FetchAndMaybeCacheMD(env, from);
  if (result.explicit_md) {
    env->supported_hash_algorithms.push_back(from);
  }
}

#else
void SaveSupportedHashAlgorithms(const EVP_MD* md,
                                 const char* from,
                                 const char* to,
                                 void* arg) {
  if (!from) return;
  Environment* env = static_cast<Environment*>(arg);
  env->supported_hash_algorithms.push_back(from);
}
#endif  // OPENSSL_VERSION_MAJOR >= 3

const std::vector<std::string>& GetSupportedHashAlgorithms(Environment* env) {
  if (env->supported_hash_algorithms.empty()) {
    MarkPopErrorOnReturn mark_pop_error_on_return;
#if OPENSSL_VERSION_MAJOR >= 3
    // Since we'll fetch the EVP_MD*, cache them along the way to speed up
    // later lookups instead of throwing them away immediately.
    EVP_MD_do_all_sorted(SaveSupportedHashAlgorithmsAndCacheMD, env);
#else
    EVP_MD_do_all_sorted(SaveSupportedHashAlgorithms, env);
#endif
  }
  return env->supported_hash_algorithms;
}

void Hash::GetHashes(const FunctionCallbackInfo<Value>& args) {
  Local<Context> context = args.GetIsolate()->GetCurrentContext();
  Environment* env = Environment::GetCurrent(context);
  const std::vector<std::string>& results = GetSupportedHashAlgorithms(env);

  Local<Value> ret;
  if (ToV8Value(context, results).ToLocal(&ret)) {
    args.GetReturnValue().Set(ret);
  }
}

void Hash::GetCachedAliases(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Context> context = args.GetIsolate()->GetCurrentContext();
  Environment* env = Environment::GetCurrent(context);
  size_t size = env->alias_to_md_id_map.size();
  LocalVector<Name> names(isolate);
  LocalVector<Value> values(isolate);
#if OPENSSL_VERSION_MAJOR >= 3
  names.reserve(size);
  values.reserve(size);
  for (auto& [alias, id] : env->alias_to_md_id_map) {
    names.push_back(OneByteString(isolate, alias));
    values.push_back(v8::Uint32::New(isolate, id));
  }
#else
  CHECK(env->alias_to_md_id_map.empty());
#endif
  Local<Value> prototype = v8::Null(isolate);
  Local<Object> result =
      Object::New(isolate, prototype, names.data(), values.data(), size);
  args.GetReturnValue().Set(result);
}

const EVP_MD* GetDigestImplementation(Environment* env,
                                      Local<Value> algorithm,
                                      Local<Value> cache_id_val,
                                      Local<Value> algorithm_cache) {
  CHECK(algorithm->IsString());
  CHECK(cache_id_val->IsInt32());
  CHECK(algorithm_cache->IsObject());

#if OPENSSL_VERSION_MAJOR >= 3
  int32_t cache_id = cache_id_val.As<Int32>()->Value();
  if (cache_id != -1) {  // Alias already cached, return the cached EVP_MD*.
    return GetCachedMDByID(env, cache_id);
  }

  // Only decode the algorithm when we don't have it cached to avoid
  // unnecessary overhead.
  Isolate* isolate = env->isolate();
  Utf8Value utf8(isolate, algorithm);

  auto result = FetchAndMaybeCacheMD(env, *utf8);
  if (result.cache_id != -1) {
    // Add the alias to both C++ side and JS side to speedup the lookup
    // next time.
    env->alias_to_md_id_map.emplace(*utf8, result.cache_id);
    if (algorithm_cache.As<Object>()
            ->Set(isolate->GetCurrentContext(),
                  algorithm,
                  v8::Int32::New(isolate, result.cache_id))
            .IsNothing()) {
      return nullptr;
    }
  }

  return result.explicit_md ? result.explicit_md : result.implicit_md;
#else
  Utf8Value utf8(env->isolate(), algorithm);
  return EVP_get_digestbyname(*utf8);
#endif
}

// crypto.digest(algorithm, algorithmId, algorithmCache,
//               input, outputEncoding, outputEncodingId)
void Hash::OneShotDigest(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();
  CHECK_EQ(args.Length(), 6);
  CHECK(args[0]->IsString());                                  // algorithm
  CHECK(args[1]->IsInt32());                                   // algorithmId
  CHECK(args[2]->IsObject());                                  // algorithmCache
  CHECK(args[3]->IsString() || args[3]->IsArrayBufferView());  // input
  CHECK(args[4]->IsString());                                  // outputEncoding
  CHECK(args[5]->IsUint32() || args[5]->IsUndefined());  // outputEncodingId

  const EVP_MD* md = GetDigestImplementation(env, args[0], args[1], args[2]);
  if (md == nullptr) {
    Utf8Value method(isolate, args[0]);
    std::string message =
        "Digest method " + method.ToString() + " is not supported";
    return ThrowCryptoError(env, ERR_get_error(), message.c_str());
  }

  enum encoding output_enc = ParseEncoding(isolate, args[4], args[5], HEX);

  int md_len = EVP_MD_size(md);
  unsigned int result_size;
  ByteSource::Builder output(md_len);
  int success;
  // On smaller inputs, EVP_Digest() can be slower than the
  // deprecated helpers e.g SHA256_XXX. The speedup may not
  // be worth using deprecated APIs, however, so we use
  // EVP_Digest(), unless there's a better alternative
  // in the future.
  // https://github.com/openssl/openssl/issues/19612
  if (args[3]->IsString()) {
    Utf8Value utf8(isolate, args[3]);
    success = EVP_Digest(utf8.out(),
                         utf8.length(),
                         output.data<unsigned char>(),
                         &result_size,
                         md,
                         nullptr);
  } else {
    ArrayBufferViewContents<unsigned char> input(args[3]);
    success = EVP_Digest(input.data(),
                         input.length(),
                         output.data<unsigned char>(),
                         &result_size,
                         md,
                         nullptr);
  }
  if (!success) {
    return ThrowCryptoError(env, ERR_get_error());
  }

  Local<Value> error;
  MaybeLocal<Value> rc = StringBytes::Encode(
      env->isolate(), output.data<char>(), md_len, output_enc, &error);
  if (rc.IsEmpty()) {
    CHECK(!error.IsEmpty());
    env->isolate()->ThrowException(error);
    return;
  }
  args.GetReturnValue().Set(rc.FromMaybe(Local<Value>()));
}

void Hash::Initialize(Environment* env, Local<Object> target) {
  Isolate* isolate = env->isolate();
  Local<Context> context = env->context();
  Local<FunctionTemplate> t = NewFunctionTemplate(isolate, New);

  t->InstanceTemplate()->SetInternalFieldCount(Hash::kInternalFieldCount);

  SetProtoMethod(isolate, t, "update", HashUpdate);
  SetProtoMethod(isolate, t, "digest", HashDigest);

  SetConstructorFunction(context, target, "Hash", t);

  SetMethodNoSideEffect(context, target, "getHashes", GetHashes);
  SetMethodNoSideEffect(context, target, "getCachedAliases", GetCachedAliases);
  SetMethodNoSideEffect(context, target, "oneShotDigest", OneShotDigest);

  HashJob::Initialize(env, target);

  SetMethodNoSideEffect(
      context, target, "internalVerifyIntegrity", InternalVerifyIntegrity);
}

void Hash::RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(New);
  registry->Register(HashUpdate);
  registry->Register(HashDigest);
  registry->Register(GetHashes);
  registry->Register(GetCachedAliases);
  registry->Register(OneShotDigest);

  HashJob::RegisterExternalReferences(registry);

  registry->Register(InternalVerifyIntegrity);
}

// new Hash(algorithm, algorithmId, xofLen, algorithmCache)
void Hash::New(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  const Hash* orig = nullptr;
  const EVP_MD* md = nullptr;
  if (args[0]->IsObject()) {
    ASSIGN_OR_RETURN_UNWRAP(&orig, args[0].As<Object>());
    md = EVP_MD_CTX_md(orig->mdctx_.get());
  } else {
    md = GetDigestImplementation(env, args[0], args[2], args[3]);
  }

  Maybe<unsigned int> xof_md_len = Nothing<unsigned int>();
  if (!args[1]->IsUndefined()) {
    CHECK(args[1]->IsUint32());
    xof_md_len = Just<unsigned int>(args[1].As<Uint32>()->Value());
  }

  Hash* hash = new Hash(env, args.This());
  if (md == nullptr || !hash->HashInit(md, xof_md_len)) {
    return ThrowCryptoError(env, ERR_get_error(),
                            "Digest method not supported");
  }

  if (orig != nullptr &&
      0 >= EVP_MD_CTX_copy(hash->mdctx_.get(), orig->mdctx_.get())) {
    return ThrowCryptoError(env, ERR_get_error(), "Digest copy error");
  }
}

bool Hash::HashInit(const EVP_MD* md, Maybe<unsigned int> xof_md_len) {
  mdctx_.reset(EVP_MD_CTX_new());
  if (!mdctx_ || EVP_DigestInit_ex(mdctx_.get(), md, nullptr) <= 0) {
    mdctx_.reset();
    return false;
  }

  md_len_ = EVP_MD_size(md);
  if (xof_md_len.IsJust() && xof_md_len.FromJust() != md_len_) {
    // This is a little hack to cause createHash to fail when an incorrect
    // hashSize option was passed for a non-XOF hash function.
    if ((EVP_MD_flags(md) & EVP_MD_FLAG_XOF) == 0) {
      EVPerr(EVP_F_EVP_DIGESTFINALXOF, EVP_R_NOT_XOF_OR_INVALID_LENGTH);
      return false;
    }
    md_len_ = xof_md_len.FromJust();
  }

  return true;
}

bool Hash::HashUpdate(const char* data, size_t len) {
  if (!mdctx_)
    return false;
  return EVP_DigestUpdate(mdctx_.get(), data, len) == 1;
}

void Hash::HashUpdate(const FunctionCallbackInfo<Value>& args) {
  Decode<Hash>(args,
               [](Hash* hash,
                  const FunctionCallbackInfo<Value>& args,
                  const char* data,
                  size_t size) {
                 Environment* env = Environment::GetCurrent(args);
                 if (size > INT_MAX) [[unlikely]]
                   return THROW_ERR_OUT_OF_RANGE(env, "data is too long");
                 bool r = hash->HashUpdate(data, size);
                 args.GetReturnValue().Set(r);
               });
}

void Hash::HashDigest(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  Hash* hash;
  ASSIGN_OR_RETURN_UNWRAP(&hash, args.This());

  enum encoding encoding = BUFFER;
  if (args.Length() >= 1) {
    encoding = ParseEncoding(env->isolate(), args[0], BUFFER);
  }

  unsigned int len = hash->md_len_;

  // TODO(tniessen): SHA3_squeeze does not work for zero-length outputs on all
  // platforms and will cause a segmentation fault if called. This workaround
  // causes hash.digest() to correctly return an empty buffer / string.
  // See https://github.com/openssl/openssl/issues/9431.

  if (!hash->digest_ && len > 0) {
    // Some hash algorithms such as SHA3 do not support calling
    // EVP_DigestFinal_ex more than once, however, Hash._flush
    // and Hash.digest can both be used to retrieve the digest,
    // so we need to cache it.
    // See https://github.com/nodejs/node/issues/28245.

    ByteSource::Builder digest(len);

    size_t default_len = EVP_MD_CTX_size(hash->mdctx_.get());
    int ret;
    if (len == default_len) {
      ret = EVP_DigestFinal_ex(
          hash->mdctx_.get(), digest.data<unsigned char>(), &len);
      // The output length should always equal hash->md_len_
      CHECK_EQ(len, hash->md_len_);
    } else {
      ret = EVP_DigestFinalXOF(
          hash->mdctx_.get(), digest.data<unsigned char>(), len);
    }

    if (ret != 1)
      return ThrowCryptoError(env, ERR_get_error());

    hash->digest_ = std::move(digest).release();
  }

  Local<Value> error;
  MaybeLocal<Value> rc = StringBytes::Encode(
      env->isolate(), hash->digest_.data<char>(), len, encoding, &error);
  if (rc.IsEmpty()) {
    CHECK(!error.IsEmpty());
    env->isolate()->ThrowException(error);
    return;
  }
  args.GetReturnValue().Set(rc.FromMaybe(Local<Value>()));
}

HashConfig::HashConfig(HashConfig&& other) noexcept
    : mode(other.mode),
      in(std::move(other.in)),
      digest(other.digest),
      length(other.length) {}

HashConfig& HashConfig::operator=(HashConfig&& other) noexcept {
  if (&other == this) return *this;
  this->~HashConfig();
  return *new (this) HashConfig(std::move(other));
}

void HashConfig::MemoryInfo(MemoryTracker* tracker) const {
  // If the Job is sync, then the HashConfig does not own the data.
  if (mode == kCryptoJobAsync)
    tracker->TrackFieldWithSize("in", in.size());
}

MaybeLocal<Value> HashTraits::EncodeOutput(Environment* env,
                                           const HashConfig& params,
                                           ByteSource* out) {
  return out->ToArrayBuffer(env);
}

Maybe<void> HashTraits::AdditionalConfig(
    CryptoJobMode mode,
    const FunctionCallbackInfo<Value>& args,
    unsigned int offset,
    HashConfig* params) {
  Environment* env = Environment::GetCurrent(args);

  params->mode = mode;

  CHECK(args[offset]->IsString());  // Hash algorithm
  Utf8Value digest(env->isolate(), args[offset]);
  params->digest = EVP_get_digestbyname(*digest);
  if (params->digest == nullptr) [[unlikely]] {
    THROW_ERR_CRYPTO_INVALID_DIGEST(env, "Invalid digest: %s", *digest);
    return Nothing<void>();
  }

  ArrayBufferOrViewContents<char> data(args[offset + 1]);
  if (!data.CheckSizeInt32()) [[unlikely]] {
    THROW_ERR_OUT_OF_RANGE(env, "data is too big");
    return Nothing<void>();
  }
  params->in = mode == kCryptoJobAsync
      ? data.ToCopy()
      : data.ToByteSource();

  unsigned int expected = EVP_MD_size(params->digest);
  params->length = expected;
  if (args[offset + 2]->IsUint32()) [[unlikely]] {
    // length is expressed in terms of bits
    params->length =
        static_cast<uint32_t>(args[offset + 2]
            .As<Uint32>()->Value()) / CHAR_BIT;
    if (params->length != expected) {
      if ((EVP_MD_flags(params->digest) & EVP_MD_FLAG_XOF) == 0) {
        THROW_ERR_CRYPTO_INVALID_DIGEST(env, "Digest method not supported");
        return Nothing<void>();
      }
    }
  }

  return JustVoid();
}

bool HashTraits::DeriveBits(
    Environment* env,
    const HashConfig& params,
    ByteSource* out) {
  EVPMDCtxPointer ctx(EVP_MD_CTX_new());

  if (!ctx || EVP_DigestInit_ex(ctx.get(), params.digest, nullptr) <= 0 ||
      EVP_DigestUpdate(ctx.get(), params.in.data<char>(), params.in.size()) <=
          0) [[unlikely]] {
    return false;
  }

  if (params.length > 0) [[likely]] {
    unsigned int length = params.length;
    ByteSource::Builder buf(length);

    size_t expected = EVP_MD_CTX_size(ctx.get());

    int ret =
        (length == expected)
            ? EVP_DigestFinal_ex(ctx.get(), buf.data<unsigned char>(), &length)
            : EVP_DigestFinalXOF(ctx.get(), buf.data<unsigned char>(), length);

    if (ret != 1) [[unlikely]]
      return false;

    *out = std::move(buf).release();
  }

  return true;
}

void InternalVerifyIntegrity(const v8::FunctionCallbackInfo<v8::Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  CHECK_EQ(args.Length(), 3);

  CHECK(args[0]->IsString());
  Utf8Value algorithm(env->isolate(), args[0]);

  CHECK(args[1]->IsString() || IsAnyBufferSource(args[1]));
  ByteSource content = ByteSource::FromStringOrBuffer(env, args[1]);

  CHECK(args[2]->IsArrayBufferView());
  ArrayBufferOrViewContents<unsigned char> expected(args[2]);

  const EVP_MD* md_type = EVP_get_digestbyname(*algorithm);
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int digest_size;
  if (md_type == nullptr || EVP_Digest(content.data(),
                                       content.size(),
                                       digest,
                                       &digest_size,
                                       md_type,
                                       nullptr) != 1) {
    return ThrowCryptoError(
        env, ERR_get_error(), "Digest method not supported");
  }

  if (digest_size != expected.size() ||
      CRYPTO_memcmp(digest, expected.data(), digest_size) != 0) {
    Local<Value> error;
    MaybeLocal<Value> rc =
        StringBytes::Encode(env->isolate(),
                            reinterpret_cast<const char*>(digest),
                            digest_size,
                            BASE64,
                            &error);
    if (rc.IsEmpty()) {
      CHECK(!error.IsEmpty());
      env->isolate()->ThrowException(error);
      return;
    }
    args.GetReturnValue().Set(rc.FromMaybe(Local<Value>()));
  }
}
}  // namespace crypto
}  // namespace node
