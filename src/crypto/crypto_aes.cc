#include "crypto/crypto_aes.h"
#include "async_wrap-inl.h"
#include "base_object-inl.h"
#include "crypto/crypto_cipher.h"
#include "crypto/crypto_keys.h"
#include "crypto/crypto_util.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "threadpoolwork-inl.h"
#include "v8.h"

#include <openssl/bn.h>
#include <openssl/aes.h>

#include <vector>

namespace node {

using ncrypto::BignumPointer;
using ncrypto::Cipher;
using ncrypto::CipherCtxPointer;
using v8::FunctionCallbackInfo;
using v8::Just;
using v8::JustVoid;
using v8::Local;
using v8::Maybe;
using v8::Nothing;
using v8::Object;
using v8::Uint32;
using v8::Value;

namespace crypto {
namespace {
// Implements general AES encryption and decryption for CBC
// The key_data must be a secret key.
// On success, this function sets out to a new ByteSource
// instance containing the results and returns WebCryptoCipherStatus::OK.
WebCryptoCipherStatus AES_Cipher(Environment* env,
                                 const KeyObjectData& key_data,
                                 WebCryptoCipherMode cipher_mode,
                                 const AESCipherConfig& params,
                                 const ByteSource& in,
                                 ByteSource* out) {
  CHECK_EQ(key_data.GetKeyType(), kKeyTypeSecret);

  const int mode = params.cipher.getMode();

  auto ctx = CipherCtxPointer::New();
  if (mode == EVP_CIPH_WRAP_MODE) {
    ctx.setFlags(EVP_CIPHER_CTX_FLAG_WRAP_ALLOW);
  }

  const bool encrypt = cipher_mode == kWebCryptoCipherEncrypt;

  if (!ctx.init(params.cipher, encrypt)) {
    // Cipher init failed
    return WebCryptoCipherStatus::FAILED;
  }

  if (mode == EVP_CIPH_GCM_MODE && !ctx.setIvLength(params.iv.size())) {
    return WebCryptoCipherStatus::FAILED;
  }

  if (!ctx.setKeyLength(key_data.GetSymmetricKeySize()) ||
      !ctx.init(
          Cipher(),
          encrypt,
          reinterpret_cast<const unsigned char*>(key_data.GetSymmetricKey()),
          params.iv.data<unsigned char>())) {
    return WebCryptoCipherStatus::FAILED;
  }

  size_t tag_len = 0;

  if (mode == EVP_CIPH_GCM_MODE) {
    switch (cipher_mode) {
      case kWebCryptoCipherDecrypt: {
        // If in decrypt mode, the auth tag must be set in the params.tag.
        CHECK(params.tag);
        ncrypto::Buffer<const char> buffer = {
            .data = params.tag.data<char>(),
            .len = params.tag.size(),
        };
        if (!ctx.setAeadTag(buffer)) {
          return WebCryptoCipherStatus::FAILED;
        }
        break;
      }
      case kWebCryptoCipherEncrypt: {
        // In decrypt mode, we grab the tag length here. We'll use it to
        // ensure that that allocated buffer has enough room for both the
        // final block and the auth tag. Unlike our other AES-GCM implementation
        // in CipherBase, in WebCrypto, the auth tag is concatenated to the end
        // of the generated ciphertext and returned in the same ArrayBuffer.
        tag_len = params.length;
        break;
      }
      default:
        UNREACHABLE();
    }
  }

  size_t total = 0;
  int buf_len = in.size() + ctx.getBlockSize() + tag_len;
  int out_len;

  ncrypto::Buffer<const unsigned char> buffer = {
      .data = params.additional_data.data<unsigned char>(),
      .len = params.additional_data.size(),
  };
  if (mode == EVP_CIPH_GCM_MODE && params.additional_data.size() &&
      !ctx.update(buffer, nullptr, &out_len)) {
    return WebCryptoCipherStatus::FAILED;
  }

  ByteSource::Builder buf(buf_len);

  // In some outdated version of OpenSSL (e.g.
  // ubi81_sharedlibs_openssl111fips_x64) may be used in sharedlib mode, the
  // logic will be failed when input size is zero. The newly OpenSSL has fixed
  // it up. But we still have to regard zero as special in Node.js code to
  // prevent old OpenSSL failure.
  //
  // Refs: https://github.com/openssl/openssl/commit/420cb707b880e4fb649094241371701013eeb15f
  // Refs: https://github.com/nodejs/node/pull/38913#issuecomment-866505244
  buffer = {
      .data = in.data<unsigned char>(),
      .len = in.size(),
  };
  if (in.empty()) {
    out_len = 0;
  } else if (!ctx.update(buffer, buf.data<unsigned char>(), &out_len)) {
    return WebCryptoCipherStatus::FAILED;
  }

  total += out_len;
  CHECK_LE(out_len, buf_len);
  out_len = ctx.getBlockSize();
  if (!ctx.update({}, buf.data<unsigned char>() + total, &out_len, true)) {
    return WebCryptoCipherStatus::FAILED;
  }
  total += out_len;

  // If using AES_GCM, grab the generated auth tag and append
  // it to the end of the ciphertext.
  if (cipher_mode == kWebCryptoCipherEncrypt && mode == EVP_CIPH_GCM_MODE) {
    if (!ctx.getAeadTag(tag_len, buf.data<unsigned char>() + total)) {
      return WebCryptoCipherStatus::FAILED;
    }
    total += tag_len;
  }

  // It's possible that we haven't used the full allocated space. Size down.
  *out = std::move(buf).release(total);

  return WebCryptoCipherStatus::OK;
}

// The AES_CTR implementation here takes it's inspiration from the chromium
// implementation here:
// https://github.com/chromium/chromium/blob/7af6cfd/components/webcrypto/algorithms/aes_ctr.cc

template <typename T>
T CeilDiv(T a, T b) {
  return a == 0 ? 0 : 1 + (a - 1) / b;
}

BignumPointer GetCounter(const AESCipherConfig& params) {
  unsigned int remainder = (params.length % CHAR_BIT);
  const unsigned char* data = params.iv.data<unsigned char>();

  if (remainder == 0) {
    unsigned int byte_length = params.length / CHAR_BIT;
    return BignumPointer(data + params.iv.size() - byte_length, byte_length);
  }

  unsigned int byte_length =
      CeilDiv(params.length, static_cast<size_t>(CHAR_BIT));

  std::vector<unsigned char> counter(
      data + params.iv.size() - byte_length,
      data + params.iv.size());
  counter[0] &= ~(0xFF << remainder);

  return BignumPointer(counter.data(), counter.size());
}

std::vector<unsigned char> BlockWithZeroedCounter(
    const AESCipherConfig& params) {
  unsigned int length_bytes = params.length / CHAR_BIT;
  unsigned int remainder = params.length % CHAR_BIT;

  const unsigned char* data = params.iv.data<unsigned char>();

  std::vector<unsigned char> new_counter_block(data, data + params.iv.size());

  size_t index = new_counter_block.size() - length_bytes;
  memset(&new_counter_block.front() + index, 0, length_bytes);

  if (remainder)
    new_counter_block[index - 1] &= 0xFF << remainder;

  return new_counter_block;
}

WebCryptoCipherStatus AES_CTR_Cipher2(const KeyObjectData& key_data,
                                      WebCryptoCipherMode cipher_mode,
                                      const AESCipherConfig& params,
                                      const ByteSource& in,
                                      unsigned const char* counter,
                                      unsigned char* out) {
  auto ctx = CipherCtxPointer::New();
  if (!ctx) {
    return WebCryptoCipherStatus::FAILED;
  }
  const bool encrypt = cipher_mode == kWebCryptoCipherEncrypt;

  if (!ctx.init(
          params.cipher,
          encrypt,
          reinterpret_cast<const unsigned char*>(key_data.GetSymmetricKey()),
          counter)) {
    // Cipher init failed
    return WebCryptoCipherStatus::FAILED;
  }

  int out_len = 0;
  int final_len = 0;
  ncrypto::Buffer<const unsigned char> buffer = {
      .data = in.data<unsigned char>(),
      .len = in.size(),
  };
  if (!ctx.update(buffer, out, &out_len)) {
    return WebCryptoCipherStatus::FAILED;
  }

  if (!ctx.update({}, out + out_len, &final_len, true)) {
    return WebCryptoCipherStatus::FAILED;
  }

  out_len += final_len;
  if (static_cast<unsigned>(out_len) != in.size())
    return WebCryptoCipherStatus::FAILED;

  return WebCryptoCipherStatus::OK;
}

WebCryptoCipherStatus AES_CTR_Cipher(Environment* env,
                                     const KeyObjectData& key_data,
                                     WebCryptoCipherMode cipher_mode,
                                     const AESCipherConfig& params,
                                     const ByteSource& in,
                                     ByteSource* out) {
  auto num_counters = BignumPointer::NewLShift(params.length);
  if (!num_counters) return WebCryptoCipherStatus::FAILED;

  BignumPointer current_counter = GetCounter(params);

  auto num_output = BignumPointer::New();

  if (!num_output.setWord(CeilDiv(in.size(), kAesBlockSize)))
    return WebCryptoCipherStatus::FAILED;

  // Just like in chromium's implementation, if the counter will
  // be incremented more than there are counter values, we fail.
  if (num_output > num_counters) return WebCryptoCipherStatus::FAILED;

  auto remaining_until_reset =
      BignumPointer::NewSub(num_counters, current_counter);
  if (!remaining_until_reset) {
    return WebCryptoCipherStatus::FAILED;
  }

  // Output size is identical to the input size.
  ByteSource::Builder buf(in.size());

  // Also just like in chromium's implementation, if we can process
  // the input without wrapping the counter, we'll do it as a single
  // call here. If we can't, we'll fallback to the a two-step approach
  if (remaining_until_reset >= num_output) {
    auto status = AES_CTR_Cipher2(key_data,
                                  cipher_mode,
                                  params,
                                  in,
                                  params.iv.data<unsigned char>(),
                                  buf.data<unsigned char>());
    if (status == WebCryptoCipherStatus::OK) *out = std::move(buf).release();
    return status;
  }

  BN_ULONG input_size_part1 = remaining_until_reset.getWord() * kAesBlockSize;

  // Encrypt the first part...
  auto status =
      AES_CTR_Cipher2(key_data,
                      cipher_mode,
                      params,
                      ByteSource::Foreign(in.data<char>(), input_size_part1),
                      params.iv.data<unsigned char>(),
                      buf.data<unsigned char>());

  if (status != WebCryptoCipherStatus::OK)
    return status;

  // Wrap the counter around to zero
  std::vector<unsigned char> new_counter_block = BlockWithZeroedCounter(params);

  // Encrypt the second part...
  status =
      AES_CTR_Cipher2(key_data,
                      cipher_mode,
                      params,
                      ByteSource::Foreign(in.data<char>() + input_size_part1,
                                          in.size() - input_size_part1),
                      new_counter_block.data(),
                      buf.data<unsigned char>() + input_size_part1);

  if (status == WebCryptoCipherStatus::OK) *out = std::move(buf).release();

  return status;
}

bool ValidateIV(
    Environment* env,
    CryptoJobMode mode,
    Local<Value> value,
    AESCipherConfig* params) {
  ArrayBufferOrViewContents<char> iv(value);
  if (!iv.CheckSizeInt32()) [[unlikely]] {
    THROW_ERR_OUT_OF_RANGE(env, "iv is too big");
    return false;
  }
  params->iv = (mode == kCryptoJobAsync)
      ? iv.ToCopy()
      : iv.ToByteSource();
  return true;
}

bool ValidateCounter(
  Environment* env,
  Local<Value> value,
  AESCipherConfig* params) {
  CHECK(value->IsUint32());  // Length
  params->length = value.As<Uint32>()->Value();
  if (params->iv.size() != 16 ||
      params->length == 0 ||
      params->length > 128) {
    THROW_ERR_CRYPTO_INVALID_COUNTER(env);
    return false;
  }
  return true;
}

bool ValidateAuthTag(
    Environment* env,
    CryptoJobMode mode,
    WebCryptoCipherMode cipher_mode,
    Local<Value> value,
    AESCipherConfig* params) {
  switch (cipher_mode) {
    case kWebCryptoCipherDecrypt: {
      if (!IsAnyBufferSource(value)) {
        THROW_ERR_CRYPTO_INVALID_TAG_LENGTH(env);
        return false;
      }
      ArrayBufferOrViewContents<char> tag_contents(value);
      if (!tag_contents.CheckSizeInt32()) [[unlikely]] {
        THROW_ERR_OUT_OF_RANGE(env, "tagLength is too big");
        return false;
      }
      params->tag = mode == kCryptoJobAsync
          ? tag_contents.ToCopy()
          : tag_contents.ToByteSource();
      break;
    }
    case kWebCryptoCipherEncrypt: {
      if (!value->IsUint32()) {
        THROW_ERR_CRYPTO_INVALID_TAG_LENGTH(env);
        return false;
      }
      params->length = value.As<Uint32>()->Value();
      if (params->length > 128) {
        THROW_ERR_CRYPTO_INVALID_TAG_LENGTH(env);
        return false;
      }
      break;
    }
    default:
      UNREACHABLE();
  }
  return true;
}

bool ValidateAdditionalData(
    Environment* env,
    CryptoJobMode mode,
    Local<Value> value,
    AESCipherConfig* params) {
  // Additional Data
  if (IsAnyBufferSource(value)) {
    ArrayBufferOrViewContents<char> additional(value);
    if (!additional.CheckSizeInt32()) [[unlikely]] {
      THROW_ERR_OUT_OF_RANGE(env, "additionalData is too big");
      return false;
    }
    params->additional_data = mode == kCryptoJobAsync
        ? additional.ToCopy()
        : additional.ToByteSource();
  }
  return true;
}

void UseDefaultIV(AESCipherConfig* params) {
  params->iv = ByteSource::Foreign(kDefaultWrapIV, strlen(kDefaultWrapIV));
}
}  // namespace

AESCipherConfig::AESCipherConfig(AESCipherConfig&& other) noexcept
    : mode(other.mode),
      variant(other.variant),
      cipher(other.cipher),
      length(other.length),
      iv(std::move(other.iv)),
      additional_data(std::move(other.additional_data)),
      tag(std::move(other.tag)) {}

AESCipherConfig& AESCipherConfig::operator=(AESCipherConfig&& other) noexcept {
  if (&other == this) return *this;
  this->~AESCipherConfig();
  return *new (this) AESCipherConfig(std::move(other));
}

void AESCipherConfig::MemoryInfo(MemoryTracker* tracker) const {
  // If mode is sync, then the data in each of these properties
  // is not owned by the AESCipherConfig, so we ignore it.
  if (mode == kCryptoJobAsync) {
    tracker->TrackFieldWithSize("iv", iv.size());
    tracker->TrackFieldWithSize("additional_data", additional_data.size());
    tracker->TrackFieldWithSize("tag", tag.size());
  }
}

Maybe<void> AESCipherTraits::AdditionalConfig(
    CryptoJobMode mode,
    const FunctionCallbackInfo<Value>& args,
    unsigned int offset,
    WebCryptoCipherMode cipher_mode,
    AESCipherConfig* params) {
  Environment* env = Environment::GetCurrent(args);

  params->mode = mode;

  CHECK(args[offset]->IsUint32());  // Key Variant
  params->variant =
      static_cast<AESKeyVariant>(args[offset].As<Uint32>()->Value());

  int cipher_nid;
#define V(name, _, nid)                                                        \
  case kKeyVariantAES_##name: {                                                \
    cipher_nid = nid;                                                          \
    break;                                                                     \
  }
  switch (params->variant) {
    VARIANTS(V)
    default:
      UNREACHABLE();
  }
#undef V

  params->cipher = Cipher::FromNid(cipher_nid);
  if (!params->cipher) {
    THROW_ERR_CRYPTO_UNKNOWN_CIPHER(env);
    return Nothing<void>();
  }

  int cipher_op_mode = params->cipher.getMode();
  if (cipher_op_mode != EVP_CIPH_WRAP_MODE) {
    if (!ValidateIV(env, mode, args[offset + 1], params)) {
      return Nothing<void>();
    }
    if (cipher_op_mode == EVP_CIPH_CTR_MODE) {
      if (!ValidateCounter(env, args[offset + 2], params)) {
        return Nothing<void>();
      }
    } else if (cipher_op_mode == EVP_CIPH_GCM_MODE) {
      if (!ValidateAuthTag(env, mode, cipher_mode, args[offset + 2], params) ||
          !ValidateAdditionalData(env, mode, args[offset + 3], params)) {
        return Nothing<void>();
      }
    }
  } else {
    UseDefaultIV(params);
  }

  if (params->iv.size() < static_cast<size_t>(params->cipher.getIvLength())) {
    THROW_ERR_CRYPTO_INVALID_IV(env);
    return Nothing<void>();
  }

  return JustVoid();
}

WebCryptoCipherStatus AESCipherTraits::DoCipher(Environment* env,
                                                const KeyObjectData& key_data,
                                                WebCryptoCipherMode cipher_mode,
                                                const AESCipherConfig& params,
                                                const ByteSource& in,
                                                ByteSource* out) {
#define V(name, fn, _)                                                         \
  case kKeyVariantAES_##name:                                                  \
    return fn(env, key_data, cipher_mode, params, in, out);
  switch (params.variant) {
    VARIANTS(V)
    default:
      UNREACHABLE();
  }
#undef V
}

void AES::Initialize(Environment* env, Local<Object> target) {
  AESCryptoJob::Initialize(env, target);

#define V(name, _, __) NODE_DEFINE_CONSTANT(target, kKeyVariantAES_##name);
  VARIANTS(V)
#undef V
}

void AES::RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  AESCryptoJob::RegisterExternalReferences(registry);
}

}  // namespace crypto
}  // namespace node
