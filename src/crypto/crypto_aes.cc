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

using v8::FunctionCallbackInfo;
using v8::Just;
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
WebCryptoCipherStatus AES_Cipher(
    Environment* env,
    KeyObjectData* key_data,
    WebCryptoCipherMode cipher_mode,
    const AESCipherConfig& params,
    const ByteSource& in,
    ByteSource* out) {
  CHECK_NOT_NULL(key_data);
  CHECK_EQ(key_data->GetKeyType(), kKeyTypeSecret);

  const int mode = EVP_CIPHER_mode(params.cipher);

  CipherCtxPointer ctx(EVP_CIPHER_CTX_new());
  EVP_CIPHER_CTX_init(ctx.get());
  if (mode == EVP_CIPH_WRAP_MODE)
    EVP_CIPHER_CTX_set_flags(ctx.get(), EVP_CIPHER_CTX_FLAG_WRAP_ALLOW);

  const bool encrypt = cipher_mode == kWebCryptoCipherEncrypt;

  if (!EVP_CipherInit_ex(
          ctx.get(),
          params.cipher,
          nullptr,
          nullptr,
          nullptr,
          encrypt)) {
    // Cipher init failed
    return WebCryptoCipherStatus::FAILED;
  }

  if (mode == EVP_CIPH_GCM_MODE && !EVP_CIPHER_CTX_ctrl(
        ctx.get(),
        EVP_CTRL_AEAD_SET_IVLEN,
        params.iv.size(),
        nullptr)) {
    return WebCryptoCipherStatus::FAILED;
  }

  if (!EVP_CIPHER_CTX_set_key_length(
          ctx.get(),
          key_data->GetSymmetricKeySize()) ||
      !EVP_CipherInit_ex(
          ctx.get(),
          nullptr,
          nullptr,
          reinterpret_cast<const unsigned char*>(key_data->GetSymmetricKey()),
          params.iv.data<unsigned char>(),
          encrypt)) {
    return WebCryptoCipherStatus::FAILED;
  }

  size_t tag_len = 0;

  if (mode == EVP_CIPH_GCM_MODE) {
    switch (cipher_mode) {
      case kWebCryptoCipherDecrypt:
        // If in decrypt mode, the auth tag must be set in the params.tag.
        CHECK(params.tag);
        if (!EVP_CIPHER_CTX_ctrl(ctx.get(),
                                 EVP_CTRL_AEAD_SET_TAG,
                                 params.tag.size(),
                                 const_cast<char*>(params.tag.data<char>()))) {
          return WebCryptoCipherStatus::FAILED;
        }
        break;
      case kWebCryptoCipherEncrypt:
        // In decrypt mode, we grab the tag length here. We'll use it to
        // ensure that that allocated buffer has enough room for both the
        // final block and the auth tag. Unlike our other AES-GCM implementation
        // in CipherBase, in WebCrypto, the auth tag is concatenated to the end
        // of the generated ciphertext and returned in the same ArrayBuffer.
        tag_len = params.length;
        break;
      default:
        UNREACHABLE();
    }
  }

  size_t total = 0;
  int buf_len = in.size() + EVP_CIPHER_CTX_block_size(ctx.get()) + tag_len;
  int out_len;

  if (mode == EVP_CIPH_GCM_MODE &&
      params.additional_data.size() &&
      !EVP_CipherUpdate(
            ctx.get(),
            nullptr,
            &out_len,
            params.additional_data.data<unsigned char>(),
            params.additional_data.size())) {
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
  if (in.empty()) {
    out_len = 0;
  } else if (!EVP_CipherUpdate(ctx.get(),
                               buf.data<unsigned char>(),
                               &out_len,
                               in.data<unsigned char>(),
                               in.size())) {
    return WebCryptoCipherStatus::FAILED;
  }

  total += out_len;
  CHECK_LE(out_len, buf_len);
  out_len = EVP_CIPHER_CTX_block_size(ctx.get());
  if (!EVP_CipherFinal_ex(
          ctx.get(), buf.data<unsigned char>() + total, &out_len)) {
    return WebCryptoCipherStatus::FAILED;
  }
  total += out_len;

  // If using AES_GCM, grab the generated auth tag and append
  // it to the end of the ciphertext.
  if (cipher_mode == kWebCryptoCipherEncrypt && mode == EVP_CIPH_GCM_MODE) {
    if (!EVP_CIPHER_CTX_ctrl(ctx.get(),
                             EVP_CTRL_AEAD_GET_TAG,
                             tag_len,
                             buf.data<unsigned char>() + total))
      return WebCryptoCipherStatus::FAILED;
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
    return BignumPointer(BN_bin2bn(
        data + params.iv.size() - byte_length,
        byte_length,
        nullptr));
  }

  unsigned int byte_length =
      CeilDiv(params.length, static_cast<size_t>(CHAR_BIT));

  std::vector<unsigned char> counter(
      data + params.iv.size() - byte_length,
      data + params.iv.size());
  counter[0] &= ~(0xFF << remainder);

  return BignumPointer(BN_bin2bn(counter.data(), counter.size(), nullptr));
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

WebCryptoCipherStatus AES_CTR_Cipher2(
    KeyObjectData* key_data,
    WebCryptoCipherMode cipher_mode,
    const AESCipherConfig& params,
    const ByteSource& in,
    unsigned const char* counter,
    unsigned char* out) {
  CipherCtxPointer ctx(EVP_CIPHER_CTX_new());
  const bool encrypt = cipher_mode == kWebCryptoCipherEncrypt;

  if (!EVP_CipherInit_ex(
          ctx.get(),
          params.cipher,
          nullptr,
          reinterpret_cast<const unsigned char*>(key_data->GetSymmetricKey()),
          counter,
          encrypt)) {
    // Cipher init failed
    return WebCryptoCipherStatus::FAILED;
  }

  int out_len = 0;
  int final_len = 0;
  if (!EVP_CipherUpdate(
          ctx.get(),
          out,
          &out_len,
          in.data<unsigned char>(),
          in.size())) {
    return WebCryptoCipherStatus::FAILED;
  }

  if (!EVP_CipherFinal_ex(ctx.get(), out + out_len, &final_len))
    return WebCryptoCipherStatus::FAILED;

  out_len += final_len;
  if (static_cast<unsigned>(out_len) != in.size())
    return WebCryptoCipherStatus::FAILED;

  return WebCryptoCipherStatus::OK;
}

WebCryptoCipherStatus AES_CTR_Cipher(
    Environment* env,
    KeyObjectData* key_data,
    WebCryptoCipherMode cipher_mode,
    const AESCipherConfig& params,
    const ByteSource& in,
    ByteSource* out) {
  BignumPointer num_counters(BN_new());
  if (!BN_lshift(num_counters.get(), BN_value_one(), params.length))
    return WebCryptoCipherStatus::FAILED;

  BignumPointer current_counter = GetCounter(params);

  BignumPointer num_output(BN_new());

  if (!BN_set_word(num_output.get(), CeilDiv(in.size(), kAesBlockSize)))
    return WebCryptoCipherStatus::FAILED;

  // Just like in chromium's implementation, if the counter will
  // be incremented more than there are counter values, we fail.
  if (BN_cmp(num_output.get(), num_counters.get()) > 0)
    return WebCryptoCipherStatus::FAILED;

  BignumPointer remaining_until_reset(BN_new());
  if (!BN_sub(remaining_until_reset.get(),
              num_counters.get(),
              current_counter.get())) {
    return WebCryptoCipherStatus::FAILED;
  }

  // Output size is identical to the input size.
  ByteSource::Builder buf(in.size());

  // Also just like in chromium's implementation, if we can process
  // the input without wrapping the counter, we'll do it as a single
  // call here. If we can't, we'll fallback to the a two-step approach
  if (BN_cmp(remaining_until_reset.get(), num_output.get()) >= 0) {
    auto status = AES_CTR_Cipher2(key_data,
                                  cipher_mode,
                                  params,
                                  in,
                                  params.iv.data<unsigned char>(),
                                  buf.data<unsigned char>());
    if (status == WebCryptoCipherStatus::OK) *out = std::move(buf).release();
    return status;
  }

  BN_ULONG blocks_part1 = BN_get_word(remaining_until_reset.get());
  BN_ULONG input_size_part1 = blocks_part1 * kAesBlockSize;

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
  if (UNLIKELY(!iv.CheckSizeInt32())) {
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
      if (UNLIKELY(!tag_contents.CheckSizeInt32())) {
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
    if (UNLIKELY(!additional.CheckSizeInt32())) {
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

Maybe<bool> AESCipherTraits::AdditionalConfig(
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

  AESCipherMode cipher_op_mode;
  int cipher_nid;

#define V(name, _, mode, nid)                                                  \
  case kKeyVariantAES_##name: {                                                \
    cipher_op_mode = mode;                                                     \
    cipher_nid = nid;                                                          \
    break;                                                                     \
  }
  switch (params->variant) {
    VARIANTS(V)
    default:
      UNREACHABLE();
  }
#undef V

  if (cipher_op_mode != AESCipherMode::KW) {
    if (!ValidateIV(env, mode, args[offset + 1], params)) {
      return Nothing<bool>();
    }
    if (cipher_op_mode == AESCipherMode::CTR) {
      if (!ValidateCounter(env, args[offset + 2], params)) {
        return Nothing<bool>();
      }
    } else if (cipher_op_mode == AESCipherMode::GCM) {
      if (!ValidateAuthTag(env, mode, cipher_mode, args[offset + 2], params) ||
          !ValidateAdditionalData(env, mode, args[offset + 3], params)) {
        return Nothing<bool>();
      }
    }
  } else {
    UseDefaultIV(params);
  }

  params->cipher = EVP_get_cipherbynid(cipher_nid);
  if (params->cipher == nullptr) {
    THROW_ERR_CRYPTO_UNKNOWN_CIPHER(env);
    return Nothing<bool>();
  }

  if (params->iv.size() <
      static_cast<size_t>(EVP_CIPHER_iv_length(params->cipher))) {
    THROW_ERR_CRYPTO_INVALID_IV(env);
    return Nothing<bool>();
  }

  return Just(true);
}

WebCryptoCipherStatus AESCipherTraits::DoCipher(
    Environment* env,
    std::shared_ptr<KeyObjectData> key_data,
    WebCryptoCipherMode cipher_mode,
    const AESCipherConfig& params,
    const ByteSource& in,
    ByteSource* out) {
#define V(name, fn, _, __)                                                     \
  case kKeyVariantAES_##name:                                                  \
    return fn(env, key_data.get(), cipher_mode, params, in, out);
  switch (params.variant) {
    VARIANTS(V)
    default:
      UNREACHABLE();
  }
#undef V
}

void AES::Initialize(Environment* env, Local<Object> target) {
  AESCryptoJob::Initialize(env, target);

#define V(name, _, __, ___) NODE_DEFINE_CONSTANT(target, kKeyVariantAES_##name);
  VARIANTS(V)
#undef V
}

void AES::RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  AESCryptoJob::RegisterExternalReferences(registry);
}

}  // namespace crypto
}  // namespace node
