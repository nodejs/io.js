#if HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC

#include "tokens.h"
#include <crypto/crypto_util.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <node_sockaddr-inl.h>
#include <string_bytes.h>
#include <util-inl.h>
#include <algorithm>
#include "nbytes.h"
#include "ncrypto.h"

namespace node::quic {

// ============================================================================
// TokenSecret

TokenSecret::TokenSecret() : buf_() {
  // As a performance optimization later, we could consider creating an entropy
  // cache here similar to what we use for random CIDs so that we do not have
  // to engage CSPRNG on every call. That, however, is suboptimal for secrets.
  // If someone manages to get visibility into that cache then they would know
  // the secrets for a larger number of tokens, which could be bad. For now,
  // generating on each call is safer, even if less performant.
  CHECK(ncrypto::CSPRNG(buf_, QUIC_TOKENSECRET_LEN));
}

TokenSecret::TokenSecret(const uint8_t* secret) : buf_() {
  CHECK_NOT_NULL(secret);
  memcpy(buf_, secret, QUIC_TOKENSECRET_LEN);
}

TokenSecret::~TokenSecret() {
  memset(buf_, 0, QUIC_TOKENSECRET_LEN);
}

TokenSecret::operator const uint8_t*() const {
  return buf_;
}

uint8_t TokenSecret::operator[](int pos) const {
  CHECK_GE(pos, 0);
  CHECK_LT(pos, QUIC_TOKENSECRET_LEN);
  return buf_[pos];
}

TokenSecret::operator const char*() const {
  return reinterpret_cast<const char*>(buf_);
}

std::string TokenSecret::ToString() const {
  char dest[QUIC_TOKENSECRET_LEN * 2];
  size_t written =
      nbytes::HexEncode(*this, QUIC_TOKENSECRET_LEN, dest, arraysize(dest));
  DCHECK_EQ(written, arraysize(dest));
  return std::string(dest, written);
}

// ============================================================================
// StatelessResetToken

StatelessResetToken::StatelessResetToken() : ptr_(nullptr), buf_() {}

StatelessResetToken::StatelessResetToken(const uint8_t* token) : ptr_(token) {}

StatelessResetToken::StatelessResetToken(const TokenSecret& secret,
                                         const CID& cid)
    : ptr_(buf_) {
  CHECK_EQ(ngtcp2_crypto_generate_stateless_reset_token(
               buf_, secret, kStatelessTokenLen, cid),
           0);
}

StatelessResetToken::StatelessResetToken(uint8_t* token,
                                         const TokenSecret& secret,
                                         const CID& cid)
    : ptr_(token) {
  CHECK_EQ(ngtcp2_crypto_generate_stateless_reset_token(
               token, secret, kStatelessTokenLen, cid),
           0);
}

StatelessResetToken::StatelessResetToken(const StatelessResetToken& other)
    : ptr_(buf_) {
  if (other) {
    memcpy(buf_, other.ptr_, kStatelessTokenLen);
  } else {
    ptr_ = nullptr;
  }
}

StatelessResetToken::operator const uint8_t*() const {
  return ptr_ != nullptr ? ptr_ : buf_;
}

StatelessResetToken::operator const char*() const {
  return reinterpret_cast<const char*>(ptr_ != nullptr ? ptr_ : buf_);
}

StatelessResetToken::operator bool() const {
  return ptr_ != nullptr;
}

bool StatelessResetToken::operator==(const StatelessResetToken& other) const {
  if (ptr_ == other.ptr_) return true;
  if ((ptr_ == nullptr && other.ptr_ != nullptr) ||
      (ptr_ != nullptr && other.ptr_ == nullptr)) {
    return false;
  }
  return memcmp(ptr_, other.ptr_, kStatelessTokenLen) == 0;
}

bool StatelessResetToken::operator!=(const StatelessResetToken& other) const {
  return !(*this == other);
}

std::string StatelessResetToken::ToString() const {
  if (ptr_ == nullptr) return std::string();
  char dest[kStatelessTokenLen * 2];
  size_t written =
      nbytes::HexEncode(*this, kStatelessTokenLen, dest, arraysize(dest));
  DCHECK_EQ(written, arraysize(dest));
  return std::string(dest, written);
}

size_t StatelessResetToken::Hash::operator()(
    const StatelessResetToken& token) const {
  size_t hash = 0;
  if (token.ptr_ == nullptr) return hash;
  for (size_t n = 0; n < kStatelessTokenLen; n++)
    hash ^= std::hash<uint8_t>{}(token.ptr_[n]) + 0x9e3779b9 + (hash << 6) +
            (hash >> 2);
  return hash;
}

StatelessResetToken StatelessResetToken::kInvalid;

// ============================================================================
// RetryToken and RegularToken
namespace {
ngtcp2_vec GenerateRetryToken(uint8_t* buffer,
                              uint32_t version,
                              const SocketAddress& address,
                              const CID& retry_cid,
                              const CID& odcid,
                              const TokenSecret& token_secret) {
  ssize_t ret =
      ngtcp2_crypto_generate_retry_token(buffer,
                                         token_secret,
                                         TokenSecret::QUIC_TOKENSECRET_LEN,
                                         version,
                                         address.data(),
                                         address.length(),
                                         retry_cid,
                                         odcid,
                                         uv_hrtime());
  DCHECK_GE(ret, 0);
  DCHECK_LE(ret, RetryToken::kRetryTokenLen);
  DCHECK_EQ(buffer[0], RetryToken::kTokenMagic);
  // This shouldn't be possible but we handle it anyway just to be safe.
  if (ret == 0) return {nullptr, 0};
  return {buffer, static_cast<size_t>(ret)};
}

ngtcp2_vec GenerateRegularToken(uint8_t* buffer,
                                uint32_t version,
                                const SocketAddress& address,
                                const TokenSecret& token_secret) {
  ssize_t ret =
      ngtcp2_crypto_generate_regular_token(buffer,
                                           token_secret,
                                           TokenSecret::QUIC_TOKENSECRET_LEN,
                                           address.data(),
                                           address.length(),
                                           uv_hrtime());
  DCHECK_GE(ret, 0);
  DCHECK_LE(ret, RegularToken::kRegularTokenLen);
  DCHECK_EQ(buffer[0], RegularToken::kTokenMagic);
  // This shouldn't be possible but we handle it anyway just to be safe.
  if (ret == 0) return {nullptr, 0};
  return {buffer, static_cast<size_t>(ret)};
}
}  // namespace

RetryToken::RetryToken(uint32_t version,
                       const SocketAddress& address,
                       const CID& retry_cid,
                       const CID& odcid,
                       const TokenSecret& token_secret)
    : buf_(),
      ptr_(GenerateRetryToken(
          buf_, version, address, retry_cid, odcid, token_secret)) {}

RetryToken::RetryToken(const uint8_t* token, size_t size)
    : ptr_(ngtcp2_vec{const_cast<uint8_t*>(token), size}) {
  DCHECK_LE(size, RetryToken::kRetryTokenLen);
  DCHECK_IMPLIES(token == nullptr, size = 0);
}

std::optional<CID> RetryToken::Validate(uint32_t version,
                                        const SocketAddress& addr,
                                        const CID& dcid,
                                        const TokenSecret& token_secret,
                                        uint64_t verification_expiration) {
  if (ptr_.base == nullptr || ptr_.len == 0) return std::nullopt;
  ngtcp2_cid ocid;
  int ret = ngtcp2_crypto_verify_retry_token(
      &ocid,
      ptr_.base,
      ptr_.len,
      token_secret,
      TokenSecret::QUIC_TOKENSECRET_LEN,
      version,
      addr.data(),
      addr.length(),
      dcid,
      std::min(verification_expiration, QUIC_MIN_RETRYTOKEN_EXPIRATION),
      uv_hrtime());
  if (ret != 0) return std::nullopt;
  return std::optional<CID>(ocid);
}

RetryToken::operator const ngtcp2_vec&() const {
  return ptr_;
}
RetryToken::operator const ngtcp2_vec*() const {
  return &ptr_;
}

std::string RetryToken::ToString() const {
  if (ptr_.base == nullptr) return std::string();
  MaybeStackBuffer<char, 32> dest(ptr_.len * 2);
  size_t written =
      nbytes::HexEncode(*this, ptr_.len, dest.out(), dest.length());
  DCHECK_EQ(written, dest.length());
  return std::string(dest.out(), written);
}

RetryToken::operator const char*() const {
  return reinterpret_cast<const char*>(ptr_.base);
}

RetryToken::operator bool() const {
  return ptr_.base != nullptr && ptr_.len > 0;
}

RegularToken::RegularToken() : buf_(), ptr_(ngtcp2_vec{nullptr, 0}) {}

RegularToken::RegularToken(uint32_t version,
                           const SocketAddress& address,
                           const TokenSecret& token_secret)
    : buf_(),
      ptr_(GenerateRegularToken(buf_, version, address, token_secret)) {}

RegularToken::RegularToken(const uint8_t* token, size_t size)
    : ptr_(ngtcp2_vec{const_cast<uint8_t*>(token), size}) {
  DCHECK_LE(size, RegularToken::kRegularTokenLen);
  DCHECK_IMPLIES(token == nullptr, size = 0);
}

RegularToken::operator bool() const {
  return ptr_.base != nullptr && ptr_.len > 0;
}

bool RegularToken::Validate(uint32_t version,
                            const SocketAddress& addr,
                            const TokenSecret& token_secret,
                            uint64_t verification_expiration) {
  if (ptr_.base == nullptr || ptr_.len == 0) return false;
  return ngtcp2_crypto_verify_regular_token(
             ptr_.base,
             ptr_.len,
             token_secret,
             TokenSecret::QUIC_TOKENSECRET_LEN,
             addr.data(),
             addr.length(),
             std::min(verification_expiration,
                      QUIC_MIN_REGULARTOKEN_EXPIRATION),
             uv_hrtime()) == 0;
}

RegularToken::operator const ngtcp2_vec&() const {
  return ptr_;
}
RegularToken::operator const ngtcp2_vec*() const {
  return &ptr_;
}

std::string RegularToken::ToString() const {
  if (ptr_.base == nullptr) return std::string();
  MaybeStackBuffer<char, 32> dest(ptr_.len * 2);
  size_t written =
      nbytes::HexEncode(*this, ptr_.len, dest.out(), dest.length());
  DCHECK_EQ(written, dest.length());
  return std::string(dest.out(), written);
}

RegularToken::operator const char*() const {
  return reinterpret_cast<const char*>(ptr_.base);
}

}  // namespace node::quic

#endif  // HAVE_OPENSSL && NODE_OPENSSL_HAS_QUIC
