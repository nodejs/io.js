#include "ncrypto.h"
#include <openssl/asn1.h>
#include <openssl/bn.h>
#include <openssl/dh.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/pkcs12.h>
#include <openssl/rand.h>
#include <openssl/x509v3.h>
#include <algorithm>
#include <cstring>
#if OPENSSL_VERSION_MAJOR >= 3
#include <openssl/provider.h>
#endif
#ifdef OPENSSL_IS_BORINGSSL
#include "dh-primes.h"
#endif  // OPENSSL_IS_BORINGSSL

// EVP_PKEY_CTX_set_dsa_paramgen_q_bits was added in OpenSSL 1.1.1e.
#if OPENSSL_VERSION_NUMBER < 0x1010105fL
#define EVP_PKEY_CTX_set_dsa_paramgen_q_bits(ctx, qbits)                       \
  EVP_PKEY_CTX_ctrl((ctx),                                                     \
                    EVP_PKEY_DSA,                                              \
                    EVP_PKEY_OP_PARAMGEN,                                      \
                    EVP_PKEY_CTRL_DSA_PARAMGEN_Q_BITS,                         \
                    (qbits),                                                   \
                    nullptr)
#endif

namespace ncrypto {
namespace {
using BignumCtxPointer = DeleteFnPtr<BN_CTX, BN_CTX_free>;
using BignumGenCallbackPointer = DeleteFnPtr<BN_GENCB, BN_GENCB_free>;
using NetscapeSPKIPointer = DeleteFnPtr<NETSCAPE_SPKI, NETSCAPE_SPKI_free>;

static constexpr int kX509NameFlagsRFC2253WithinUtf8JSON =
    XN_FLAG_RFC2253 & ~ASN1_STRFLGS_ESC_MSB & ~ASN1_STRFLGS_ESC_CTRL;
}  // namespace

// ============================================================================

ClearErrorOnReturn::ClearErrorOnReturn(CryptoErrorList* errors)
    : errors_(errors) {
  ERR_clear_error();
}

ClearErrorOnReturn::~ClearErrorOnReturn() {
  if (errors_ != nullptr) errors_->capture();
  ERR_clear_error();
}

int ClearErrorOnReturn::peekError() {
  return ERR_peek_error();
}

MarkPopErrorOnReturn::MarkPopErrorOnReturn(CryptoErrorList* errors)
    : errors_(errors) {
  ERR_set_mark();
}

MarkPopErrorOnReturn::~MarkPopErrorOnReturn() {
  if (errors_ != nullptr) errors_->capture();
  ERR_pop_to_mark();
}

int MarkPopErrorOnReturn::peekError() {
  return ERR_peek_error();
}

CryptoErrorList::CryptoErrorList(CryptoErrorList::Option option) {
  if (option == Option::CAPTURE_ON_CONSTRUCT) capture();
}

void CryptoErrorList::capture() {
  errors_.clear();
  while (const auto err = ERR_get_error()) {
    char buf[256];
    ERR_error_string_n(err, buf, sizeof(buf));
    errors_.emplace_front(buf);
  }
}

void CryptoErrorList::add(std::string error) {
  errors_.push_back(error);
}

std::optional<std::string> CryptoErrorList::pop_back() {
  if (errors_.empty()) return std::nullopt;
  std::string error = errors_.back();
  errors_.pop_back();
  return error;
}

std::optional<std::string> CryptoErrorList::pop_front() {
  if (errors_.empty()) return std::nullopt;
  std::string error = errors_.front();
  errors_.pop_front();
  return error;
}

// ============================================================================
DataPointer DataPointer::Alloc(size_t len) {
#ifdef OPENSSL_IS_BORINGSSL
  // Boringssl does not implement OPENSSL_zalloc
  auto ptr = OPENSSL_malloc(len);
  if (ptr == nullptr) return {};
  memset(ptr, 0, len);
  return DataPointer(ptr, len);
#else
  return DataPointer(OPENSSL_zalloc(len), len);
#endif
}

DataPointer DataPointer::Copy(const Buffer<const void>& buffer) {
  return DataPointer(OPENSSL_memdup(buffer.data, buffer.len), buffer.len);
}

DataPointer::DataPointer(void* data, size_t length)
    : data_(data), len_(length) {}

DataPointer::DataPointer(const Buffer<void>& buffer)
    : data_(buffer.data), len_(buffer.len) {}

DataPointer::DataPointer(DataPointer&& other) noexcept
    : data_(other.data_), len_(other.len_) {
  other.data_ = nullptr;
  other.len_ = 0;
}

DataPointer& DataPointer::operator=(DataPointer&& other) noexcept {
  if (this == &other) return *this;
  this->~DataPointer();
  return *new (this) DataPointer(std::move(other));
}

DataPointer::~DataPointer() {
  reset();
}

void DataPointer::zero() {
  if (!data_) return;
  OPENSSL_cleanse(data_, len_);
}

void DataPointer::reset(void* data, size_t length) {
  if (data_ != nullptr) {
    OPENSSL_clear_free(data_, len_);
  }
  data_ = data;
  len_ = length;
}

void DataPointer::reset(const Buffer<void>& buffer) {
  reset(buffer.data, buffer.len);
}

Buffer<void> DataPointer::release() {
  Buffer<void> buf{
      .data = data_,
      .len = len_,
  };
  data_ = nullptr;
  len_ = 0;
  return buf;
}

DataPointer DataPointer::resize(size_t len) {
  size_t actual_len = std::min(len_, len);
  auto buf = release();
  if (actual_len == len_) return DataPointer(buf);
  buf.data = OPENSSL_realloc(buf.data, actual_len);
  buf.len = actual_len;
  return DataPointer(buf);
}

// ============================================================================
bool isFipsEnabled() {
#if OPENSSL_VERSION_MAJOR >= 3
  return EVP_default_properties_is_fips_enabled(nullptr) == 1;
#else
  return FIPS_mode() == 1;
#endif
}

bool setFipsEnabled(bool enable, CryptoErrorList* errors) {
  if (isFipsEnabled() == enable) return true;
  ClearErrorOnReturn clearErrorOnReturn(errors);
#if OPENSSL_VERSION_MAJOR >= 3
  return EVP_default_properties_enable_fips(nullptr, enable ? 1 : 0) == 1;
#else
  return FIPS_mode_set(enable ? 1 : 0) == 1;
#endif
}

bool testFipsEnabled() {
#if OPENSSL_VERSION_MAJOR >= 3
  OSSL_PROVIDER* fips_provider = nullptr;
  if (OSSL_PROVIDER_available(nullptr, "fips")) {
    fips_provider = OSSL_PROVIDER_load(nullptr, "fips");
  }
  const auto enabled = fips_provider == nullptr                 ? 0
                       : OSSL_PROVIDER_self_test(fips_provider) ? 1
                                                                : 0;
#else
#ifdef OPENSSL_FIPS
  const auto enabled = FIPS_selftest() ? 1 : 0;
#else  // OPENSSL_FIPS
  const auto enabled = 0;
#endif  // OPENSSL_FIPS
#endif

  return enabled;
}

// ============================================================================
// Bignum
BignumPointer::BignumPointer(BIGNUM* bignum) : bn_(bignum) {}

BignumPointer::BignumPointer(const unsigned char* data, size_t len)
    : BignumPointer(BN_bin2bn(data, len, nullptr)) {}

BignumPointer::BignumPointer(BignumPointer&& other) noexcept
    : bn_(other.release()) {}

BignumPointer BignumPointer::New() {
  return BignumPointer(BN_new());
}

BignumPointer BignumPointer::NewSecure() {
#ifdef OPENSSL_IS_BORINGSSL
  // Boringssl does not implement BN_secure_new.
  return New();
#else
  return BignumPointer(BN_secure_new());
#endif
}

BignumPointer& BignumPointer::operator=(BignumPointer&& other) noexcept {
  if (this == &other) return *this;
  this->~BignumPointer();
  return *new (this) BignumPointer(std::move(other));
}

BignumPointer::~BignumPointer() {
  reset();
}

void BignumPointer::reset(BIGNUM* bn) {
  bn_.reset(bn);
}

void BignumPointer::reset(const unsigned char* data, size_t len) {
  reset(BN_bin2bn(data, len, nullptr));
}

BIGNUM* BignumPointer::release() {
  return bn_.release();
}

size_t BignumPointer::byteLength() const {
  if (bn_ == nullptr) return 0;
  return BN_num_bytes(bn_.get());
}

DataPointer BignumPointer::encode() const {
  return EncodePadded(bn_.get(), byteLength());
}

DataPointer BignumPointer::encodePadded(size_t size) const {
  return EncodePadded(bn_.get(), size);
}

size_t BignumPointer::encodeInto(unsigned char* out) const {
  if (!bn_) return 0;
  return BN_bn2bin(bn_.get(), out);
}

size_t BignumPointer::encodePaddedInto(unsigned char* out, size_t size) const {
  if (!bn_) return 0;
  return BN_bn2binpad(bn_.get(), out, size);
}

DataPointer BignumPointer::Encode(const BIGNUM* bn) {
  return EncodePadded(bn, bn != nullptr ? BN_num_bytes(bn) : 0);
}

bool BignumPointer::setWord(unsigned long w) {  // NOLINT(runtime/int)
  if (!bn_) return false;
  return BN_set_word(bn_.get(), w) == 1;
}

unsigned long BignumPointer::GetWord(const BIGNUM* bn) {  // NOLINT(runtime/int)
  return BN_get_word(bn);
}

unsigned long BignumPointer::getWord() const {  // NOLINT(runtime/int)
  if (!bn_) return 0;
  return GetWord(bn_.get());
}

DataPointer BignumPointer::EncodePadded(const BIGNUM* bn, size_t s) {
  if (bn == nullptr) return DataPointer();
  size_t size = std::max(s, static_cast<size_t>(GetByteCount(bn)));
  auto buf = DataPointer::Alloc(size);
  BN_bn2binpad(bn, reinterpret_cast<unsigned char*>(buf.get()), size);
  return buf;
}
size_t BignumPointer::EncodePaddedInto(const BIGNUM* bn,
                                       unsigned char* out,
                                       size_t size) {
  if (bn == nullptr) return 0;
  return BN_bn2binpad(bn, out, size);
}

int BignumPointer::operator<=>(const BignumPointer& other) const noexcept {
  if (bn_ == nullptr && other.bn_ != nullptr) return -1;
  if (bn_ != nullptr && other.bn_ == nullptr) return 1;
  if (bn_ == nullptr && other.bn_ == nullptr) return 0;
  return BN_cmp(bn_.get(), other.bn_.get());
}

int BignumPointer::operator<=>(const BIGNUM* other) const noexcept {
  if (bn_ == nullptr && other != nullptr) return -1;
  if (bn_ != nullptr && other == nullptr) return 1;
  if (bn_ == nullptr && other == nullptr) return 0;
  return BN_cmp(bn_.get(), other);
}

DataPointer BignumPointer::toHex() const {
  if (!bn_) return {};
  char* hex = BN_bn2hex(bn_.get());
  if (!hex) return {};
  return DataPointer(hex, strlen(hex));
}

int BignumPointer::GetBitCount(const BIGNUM* bn) {
  return BN_num_bits(bn);
}

int BignumPointer::GetByteCount(const BIGNUM* bn) {
  return BN_num_bytes(bn);
}

bool BignumPointer::isZero() const {
  return bn_ && BN_is_zero(bn_.get());
}

bool BignumPointer::isOne() const {
  return bn_ && BN_is_one(bn_.get());
}

const BIGNUM* BignumPointer::One() {
  return BN_value_one();
}

BignumPointer BignumPointer::clone() {
  if (!bn_) return {};
  return BignumPointer(BN_dup(bn_.get()));
}

int BignumPointer::isPrime(int nchecks,
                           BignumPointer::PrimeCheckCallback innerCb) const {
  BignumCtxPointer ctx(BN_CTX_new());
  BignumGenCallbackPointer cb(nullptr);
  if (innerCb != nullptr) {
    cb = BignumGenCallbackPointer(BN_GENCB_new());
    if (!cb) [[unlikely]]
      return -1;
    BN_GENCB_set(
        cb.get(),
        // TODO(@jasnell): This could be refactored to allow inlining.
        // Not too important right now tho.
        [](int a, int b, BN_GENCB* ctx) mutable -> int {
          PrimeCheckCallback& ptr =
              *static_cast<PrimeCheckCallback*>(BN_GENCB_get_arg(ctx));
          return ptr(a, b) ? 1 : 0;
        },
        &innerCb);
  }
  return BN_is_prime_ex(get(), nchecks, ctx.get(), cb.get());
}

BignumPointer BignumPointer::NewPrime(const PrimeConfig& params,
                                      PrimeCheckCallback cb) {
  BignumPointer prime(BN_new());
  if (!prime || !prime.generate(params, std::move(cb))) {
    return {};
  }
  return prime;
}

bool BignumPointer::generate(const PrimeConfig& params,
                             PrimeCheckCallback innerCb) const {
  // BN_generate_prime_ex() calls RAND_bytes_ex() internally.
  // Make sure the CSPRNG is properly seeded.
  std::ignore = CSPRNG(nullptr, 0);
  BignumGenCallbackPointer cb(nullptr);
  if (innerCb != nullptr) {
    cb = BignumGenCallbackPointer(BN_GENCB_new());
    if (!cb) [[unlikely]]
      return -1;
    BN_GENCB_set(
        cb.get(),
        [](int a, int b, BN_GENCB* ctx) mutable -> int {
          PrimeCheckCallback& ptr =
              *static_cast<PrimeCheckCallback*>(BN_GENCB_get_arg(ctx));
          return ptr(a, b) ? 1 : 0;
        },
        &innerCb);
  }
  if (BN_generate_prime_ex(get(),
                           params.bits,
                           params.safe ? 1 : 0,
                           params.add.get(),
                           params.rem.get(),
                           cb.get()) == 0) {
    return false;
  }

  return true;
}

BignumPointer BignumPointer::NewSub(const BignumPointer& a,
                                    const BignumPointer& b) {
  BignumPointer res = New();
  if (!res) return {};
  if (!BN_sub(res.get(), a.get(), b.get())) {
    return {};
  }
  return res;
}

BignumPointer BignumPointer::NewLShift(size_t length) {
  BignumPointer res = New();
  if (!res) return {};
  if (!BN_lshift(res.get(), One(), length)) {
    return {};
  }
  return res;
}

// ============================================================================
// Utility methods

bool CSPRNG(void* buffer, size_t length) {
  auto buf = reinterpret_cast<unsigned char*>(buffer);
  do {
    if (1 == RAND_status()) {
#if OPENSSL_VERSION_MAJOR >= 3
      if (1 == RAND_bytes_ex(nullptr, buf, length, 0)) {
        return true;
      }
#else
      while (length > INT_MAX && 1 == RAND_bytes(buf, INT_MAX)) {
        buf += INT_MAX;
        length -= INT_MAX;
      }
      if (length <= INT_MAX && 1 == RAND_bytes(buf, static_cast<int>(length)))
        return true;
#endif
    }
#if OPENSSL_VERSION_MAJOR >= 3
    const auto code = ERR_peek_last_error();
    // A misconfigured OpenSSL 3 installation may report 1 from RAND_poll()
    // and RAND_status() but fail in RAND_bytes() if it cannot look up
    // a matching algorithm for the CSPRNG.
    if (ERR_GET_LIB(code) == ERR_LIB_RAND) {
      const auto reason = ERR_GET_REASON(code);
      if (reason == RAND_R_ERROR_INSTANTIATING_DRBG ||
          reason == RAND_R_UNABLE_TO_FETCH_DRBG ||
          reason == RAND_R_UNABLE_TO_CREATE_DRBG) {
        return false;
      }
    }
#endif
  } while (1 == RAND_poll());

  return false;
}

int NoPasswordCallback(char* buf, int size, int rwflag, void* u) {
  return 0;
}

int PasswordCallback(char* buf, int size, int rwflag, void* u) {
  auto passphrase = static_cast<const Buffer<char>*>(u);
  if (passphrase != nullptr) {
    size_t buflen = static_cast<size_t>(size);
    size_t len = passphrase->len;
    if (buflen < len) return -1;
    memcpy(buf, reinterpret_cast<const char*>(passphrase->data), len);
    return len;
  }

  return -1;
}

// Algorithm: http://howardhinnant.github.io/date_algorithms.html
constexpr int days_from_epoch(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);  // [0, 399]
  const unsigned doy =
      (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;          // [0, 365]
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;  // [0, 146096]
  return era * 146097 + static_cast<int>(doe) - 719468;
}

#ifndef OPENSSL_IS_BORINGSSL
// tm must be in UTC
// using time_t causes problems on 32-bit systems and windows x64.
int64_t PortableTimeGM(struct tm* t) {
  int year = t->tm_year + 1900;
  int month = t->tm_mon;
  if (month > 11) {
    year += month / 12;
    month %= 12;
  } else if (month < 0) {
    int years_diff = (11 - month) / 12;
    year -= years_diff;
    month += 12 * years_diff;
  }
  int days_since_epoch = days_from_epoch(year, month + 1, t->tm_mday);

  return 60 * (60 * (24LL * static_cast<int64_t>(days_since_epoch) +
                     t->tm_hour) +
               t->tm_min) +
         t->tm_sec;
}
#endif

// ============================================================================
// SPKAC

bool VerifySpkac(const char* input, size_t length) {
#ifdef OPENSSL_IS_BORINGSSL
  // OpenSSL uses EVP_DecodeBlock, which explicitly removes trailing characters,
  // while BoringSSL uses EVP_DecodedLength and EVP_DecodeBase64, which do not.
  // As such, we trim those characters here for compatibility.
  //
  // find_last_not_of can return npos, which is the maximum value of size_t.
  // The + 1 will force a roll-ver to 0, which is the correct value. in that
  // case.
  length = std::string_view(input, length).find_last_not_of(" \n\r\t") + 1;
#endif
  NetscapeSPKIPointer spki(NETSCAPE_SPKI_b64_decode(input, length));
  if (!spki) return false;

  EVPKeyPointer pkey(X509_PUBKEY_get(spki->spkac->pubkey));
  return pkey ? NETSCAPE_SPKI_verify(spki.get(), pkey.get()) > 0 : false;
}

BIOPointer ExportPublicKey(const char* input, size_t length) {
  BIOPointer bio(BIO_new(BIO_s_mem()));
  if (!bio) return {};

#ifdef OPENSSL_IS_BORINGSSL
  // OpenSSL uses EVP_DecodeBlock, which explicitly removes trailing characters,
  // while BoringSSL uses EVP_DecodedLength and EVP_DecodeBase64, which do not.
  // As such, we trim those characters here for compatibility.
  length = std::string_view(input, length).find_last_not_of(" \n\r\t") + 1;
#endif
  NetscapeSPKIPointer spki(NETSCAPE_SPKI_b64_decode(input, length));
  if (!spki) return {};

  EVPKeyPointer pkey(NETSCAPE_SPKI_get_pubkey(spki.get()));
  if (!pkey) return {};

  if (PEM_write_bio_PUBKEY(bio.get(), pkey.get()) <= 0) return {};

  return bio;
}

Buffer<char> ExportChallenge(const char* input, size_t length) {
#ifdef OPENSSL_IS_BORINGSSL
  // OpenSSL uses EVP_DecodeBlock, which explicitly removes trailing characters,
  // while BoringSSL uses EVP_DecodedLength and EVP_DecodeBase64, which do not.
  // As such, we trim those characters here for compatibility.
  length = std::string_view(input, length).find_last_not_of(" \n\r\t") + 1;
#endif
  NetscapeSPKIPointer sp(NETSCAPE_SPKI_b64_decode(input, length));
  if (!sp) return {};

  unsigned char* buf = nullptr;
  int buf_size = ASN1_STRING_to_UTF8(&buf, sp->spkac->challenge);
  if (buf_size >= 0) {
    return {
        .data = reinterpret_cast<char*>(buf),
        .len = static_cast<size_t>(buf_size),
    };
  }

  return {};
}

// ============================================================================
namespace {
enum class AltNameOption {
  NONE,
  UTF8,
};

bool IsSafeAltName(const char* name, size_t length, AltNameOption option) {
  for (size_t i = 0; i < length; i++) {
    char c = name[i];
    switch (c) {
      case '"':
      case '\\':
        // These mess with encoding rules.
        // Fall through.
      case ',':
        // Commas make it impossible to split the list of subject alternative
        // names unambiguously, which is why we have to escape.
        // Fall through.
      case '\'':
        // Single quotes are unlikely to appear in any legitimate values, but
        // they could be used to make a value look like it was escaped (i.e.,
        // enclosed in single/double quotes).
        return false;
      default:
        if (option == AltNameOption::UTF8) {
          // In UTF8 strings, we require escaping for any ASCII control
          // character, but NOT for non-ASCII characters. Note that all bytes of
          // any code point that consists of more than a single byte have their
          // MSB set.
          if (static_cast<unsigned char>(c) < ' ' || c == '\x7f') {
            return false;
          }
        } else {
          // Check if the char is a control character or non-ASCII character.
          // Note that char may or may not be a signed type. Regardless,
          // non-ASCII values will always be outside of this range.
          if (c < ' ' || c > '~') {
            return false;
          }
        }
    }
  }
  return true;
}

void PrintAltName(const BIOPointer& out,
                  const char* name,
                  size_t length,
                  AltNameOption option = AltNameOption::NONE,
                  const char* safe_prefix = nullptr) {
  if (IsSafeAltName(name, length, option)) {
    // For backward-compatibility, append "safe" names without any
    // modifications.
    if (safe_prefix != nullptr) {
      BIO_printf(out.get(), "%s:", safe_prefix);
    }
    BIO_write(out.get(), name, length);
  } else {
    // If a name is not "safe", we cannot embed it without special
    // encoding. This does not usually happen, but we don't want to hide
    // it from the user either. We use JSON compatible escaping here.
    BIO_write(out.get(), "\"", 1);
    if (safe_prefix != nullptr) {
      BIO_printf(out.get(), "%s:", safe_prefix);
    }
    for (size_t j = 0; j < length; j++) {
      char c = static_cast<char>(name[j]);
      if (c == '\\') {
        BIO_write(out.get(), "\\\\", 2);
      } else if (c == '"') {
        BIO_write(out.get(), "\\\"", 2);
      } else if ((c >= ' ' && c != ',' && c <= '~') ||
                 (option == AltNameOption::UTF8 && (c & 0x80))) {
        // Note that the above condition explicitly excludes commas, which means
        // that those are encoded as Unicode escape sequences in the "else"
        // block. That is not strictly necessary, and Node.js itself would parse
        // it correctly either way. We only do this to account for third-party
        // code that might be splitting the string at commas (as Node.js itself
        // used to do).
        BIO_write(out.get(), &c, 1);
      } else {
        // Control character or non-ASCII character. We treat everything as
        // Latin-1, which corresponds to the first 255 Unicode code points.
        const char hex[] = "0123456789abcdef";
        char u[] = {'\\', 'u', '0', '0', hex[(c & 0xf0) >> 4], hex[c & 0x0f]};
        BIO_write(out.get(), u, sizeof(u));
      }
    }
    BIO_write(out.get(), "\"", 1);
  }
}

// This function emulates the behavior of i2v_GENERAL_NAME in a safer and less
// ambiguous way. "othername:" entries use the GENERAL_NAME_print format.
bool PrintGeneralName(const BIOPointer& out, const GENERAL_NAME* gen) {
  if (gen->type == GEN_DNS) {
    ASN1_IA5STRING* name = gen->d.dNSName;
    BIO_write(out.get(), "DNS:", 4);
    // Note that the preferred name syntax (see RFCs 5280 and 1034) with
    // wildcards is a subset of what we consider "safe", so spec-compliant DNS
    // names will never need to be escaped.
    PrintAltName(out, reinterpret_cast<const char*>(name->data), name->length);
  } else if (gen->type == GEN_EMAIL) {
    ASN1_IA5STRING* name = gen->d.rfc822Name;
    BIO_write(out.get(), "email:", 6);
    PrintAltName(out, reinterpret_cast<const char*>(name->data), name->length);
  } else if (gen->type == GEN_URI) {
    ASN1_IA5STRING* name = gen->d.uniformResourceIdentifier;
    BIO_write(out.get(), "URI:", 4);
    // The set of "safe" names was designed to include just about any URI,
    // with a few exceptions, most notably URIs that contains commas (see
    // RFC 2396). In other words, most legitimate URIs will not require
    // escaping.
    PrintAltName(out, reinterpret_cast<const char*>(name->data), name->length);
  } else if (gen->type == GEN_DIRNAME) {
    // Earlier versions of Node.js used X509_NAME_oneline to print the X509_NAME
    // object. The format was non standard and should be avoided. The use of
    // X509_NAME_oneline is discouraged by OpenSSL but was required for backward
    // compatibility. Conveniently, X509_NAME_oneline produced ASCII and the
    // output was unlikely to contains commas or other characters that would
    // require escaping. However, it SHOULD NOT produce ASCII output since an
    // RFC5280 AttributeValue may be a UTF8String.
    // Newer versions of Node.js have since switched to X509_NAME_print_ex to
    // produce a better format at the cost of backward compatibility. The new
    // format may contain Unicode characters and it is likely to contain commas,
    // which require escaping. Fortunately, the recently safeguarded function
    // PrintAltName handles all of that safely.
    BIO_printf(out.get(), "DirName:");
    BIOPointer tmp(BIO_new(BIO_s_mem()));
    NCRYPTO_ASSERT_TRUE(tmp);
    if (X509_NAME_print_ex(
            tmp.get(), gen->d.dirn, 0, kX509NameFlagsRFC2253WithinUtf8JSON) <
        0) {
      return false;
    }
    char* oline = nullptr;
    long n_bytes = BIO_get_mem_data(tmp.get(), &oline);  // NOLINT(runtime/int)
    NCRYPTO_ASSERT_TRUE(n_bytes >= 0);
    PrintAltName(out,
                 oline,
                 static_cast<size_t>(n_bytes),
                 ncrypto::AltNameOption::UTF8,
                 nullptr);
  } else if (gen->type == GEN_IPADD) {
    BIO_printf(out.get(), "IP Address:");
    const ASN1_OCTET_STRING* ip = gen->d.ip;
    const unsigned char* b = ip->data;
    if (ip->length == 4) {
      BIO_printf(out.get(), "%d.%d.%d.%d", b[0], b[1], b[2], b[3]);
    } else if (ip->length == 16) {
      for (unsigned int j = 0; j < 8; j++) {
        uint16_t pair = (b[2 * j] << 8) | b[2 * j + 1];
        BIO_printf(out.get(), (j == 0) ? "%X" : ":%X", pair);
      }
    } else {
#if OPENSSL_VERSION_MAJOR >= 3
      BIO_printf(out.get(), "<invalid length=%d>", ip->length);
#else
      BIO_printf(out.get(), "<invalid>");
#endif
    }
  } else if (gen->type == GEN_RID) {
    // Unlike OpenSSL's default implementation, never print the OID as text and
    // instead always print its numeric representation.
    char oline[256];
    OBJ_obj2txt(oline, sizeof(oline), gen->d.rid, true);
    BIO_printf(out.get(), "Registered ID:%s", oline);
  } else if (gen->type == GEN_OTHERNAME) {
    // The format that is used here is based on OpenSSL's implementation of
    // GENERAL_NAME_print (as of OpenSSL 3.0.1). Earlier versions of Node.js
    // instead produced the same format as i2v_GENERAL_NAME, which was somewhat
    // awkward, especially when passed to translatePeerCertificate.
    bool unicode = true;
    const char* prefix = nullptr;
    // OpenSSL 1.1.1 does not support othername in GENERAL_NAME_print and may
    // not define these NIDs.
#if OPENSSL_VERSION_MAJOR >= 3
    int nid = OBJ_obj2nid(gen->d.otherName->type_id);
    switch (nid) {
      case NID_id_on_SmtpUTF8Mailbox:
        prefix = "SmtpUTF8Mailbox";
        break;
      case NID_XmppAddr:
        prefix = "XmppAddr";
        break;
      case NID_SRVName:
        prefix = "SRVName";
        unicode = false;
        break;
      case NID_ms_upn:
        prefix = "UPN";
        break;
      case NID_NAIRealm:
        prefix = "NAIRealm";
        break;
    }
#endif  // OPENSSL_VERSION_MAJOR >= 3
    int val_type = gen->d.otherName->value->type;
    if (prefix == nullptr || (unicode && val_type != V_ASN1_UTF8STRING) ||
        (!unicode && val_type != V_ASN1_IA5STRING)) {
      BIO_printf(out.get(), "othername:<unsupported>");
    } else {
      BIO_printf(out.get(), "othername:");
      if (unicode) {
        auto name = gen->d.otherName->value->value.utf8string;
        PrintAltName(out,
                     reinterpret_cast<const char*>(name->data),
                     name->length,
                     AltNameOption::UTF8,
                     prefix);
      } else {
        auto name = gen->d.otherName->value->value.ia5string;
        PrintAltName(out,
                     reinterpret_cast<const char*>(name->data),
                     name->length,
                     AltNameOption::NONE,
                     prefix);
      }
    }
  } else if (gen->type == GEN_X400) {
    // TODO(tniessen): this is what OpenSSL does, implement properly instead
    BIO_printf(out.get(), "X400Name:<unsupported>");
  } else if (gen->type == GEN_EDIPARTY) {
    // TODO(tniessen): this is what OpenSSL does, implement properly instead
    BIO_printf(out.get(), "EdiPartyName:<unsupported>");
  } else {
    // This is safe because X509V3_EXT_d2i would have returned nullptr in this
    // case already.
    unreachable();
  }

  return true;
}
}  // namespace

bool SafeX509SubjectAltNamePrint(const BIOPointer& out, X509_EXTENSION* ext) {
  auto ret = OBJ_obj2nid(X509_EXTENSION_get_object(ext));
  if (ret != NID_subject_alt_name) return false;

  GENERAL_NAMES* names = static_cast<GENERAL_NAMES*>(X509V3_EXT_d2i(ext));
  if (names == nullptr) return false;

  bool ok = true;

  for (OPENSSL_SIZE_T i = 0; i < sk_GENERAL_NAME_num(names); i++) {
    GENERAL_NAME* gen = sk_GENERAL_NAME_value(names, i);

    if (i != 0) BIO_write(out.get(), ", ", 2);

    if (!(ok = ncrypto::PrintGeneralName(out, gen))) {
      break;
    }
  }
  sk_GENERAL_NAME_pop_free(names, GENERAL_NAME_free);

  return ok;
}

bool SafeX509InfoAccessPrint(const BIOPointer& out, X509_EXTENSION* ext) {
  auto ret = OBJ_obj2nid(X509_EXTENSION_get_object(ext));
  if (ret != NID_info_access) return false;

  AUTHORITY_INFO_ACCESS* descs =
      static_cast<AUTHORITY_INFO_ACCESS*>(X509V3_EXT_d2i(ext));
  if (descs == nullptr) return false;

  bool ok = true;

  for (OPENSSL_SIZE_T i = 0; i < sk_ACCESS_DESCRIPTION_num(descs); i++) {
    ACCESS_DESCRIPTION* desc = sk_ACCESS_DESCRIPTION_value(descs, i);

    if (i != 0) BIO_write(out.get(), "\n", 1);

    char objtmp[80];
    i2t_ASN1_OBJECT(objtmp, sizeof(objtmp), desc->method);
    BIO_printf(out.get(), "%s - ", objtmp);
    if (!(ok = ncrypto::PrintGeneralName(out, desc->location))) {
      break;
    }
  }
  sk_ACCESS_DESCRIPTION_pop_free(descs, ACCESS_DESCRIPTION_free);

#if OPENSSL_VERSION_MAJOR < 3
  BIO_write(out.get(), "\n", 1);
#endif

  return ok;
}

// ============================================================================
// X509Pointer

X509Pointer::X509Pointer(X509* x509) : cert_(x509) {}

X509Pointer::X509Pointer(X509Pointer&& other) noexcept
    : cert_(other.release()) {}

X509Pointer& X509Pointer::operator=(X509Pointer&& other) noexcept {
  if (this == &other) return *this;
  this->~X509Pointer();
  return *new (this) X509Pointer(std::move(other));
}

X509Pointer::~X509Pointer() {
  reset();
}

void X509Pointer::reset(X509* x509) {
  cert_.reset(x509);
}

X509* X509Pointer::release() {
  return cert_.release();
}

X509View X509Pointer::view() const {
  return X509View(cert_.get());
}

BIOPointer X509View::toPEM() const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr) return {};
  BIOPointer bio(BIO_new(BIO_s_mem()));
  if (!bio) return {};
  if (PEM_write_bio_X509(bio.get(), const_cast<X509*>(cert_)) <= 0) return {};
  return bio;
}

BIOPointer X509View::toDER() const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr) return {};
  BIOPointer bio(BIO_new(BIO_s_mem()));
  if (!bio) return {};
  if (i2d_X509_bio(bio.get(), const_cast<X509*>(cert_)) <= 0) return {};
  return bio;
}

const X509Name X509View::getSubjectName() const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr) return {};
  return X509Name(X509_get_subject_name(cert_));
}

const X509Name X509View::getIssuerName() const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr) return {};
  return X509Name(X509_get_issuer_name(cert_));
}

BIOPointer X509View::getSubject() const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr) return {};
  BIOPointer bio(BIO_new(BIO_s_mem()));
  if (!bio) return {};
  if (X509_NAME_print_ex(bio.get(),
                         X509_get_subject_name(cert_),
                         0,
                         kX509NameFlagsMultiline) <= 0) {
    return {};
  }
  return bio;
}

BIOPointer X509View::getSubjectAltName() const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr) return {};
  BIOPointer bio(BIO_new(BIO_s_mem()));
  if (!bio) return {};
  int index = X509_get_ext_by_NID(cert_, NID_subject_alt_name, -1);
  if (index < 0 ||
      !SafeX509SubjectAltNamePrint(bio, X509_get_ext(cert_, index))) {
    return {};
  }
  return bio;
}

BIOPointer X509View::getIssuer() const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr) return {};
  BIOPointer bio(BIO_new(BIO_s_mem()));
  if (!bio) return {};
  if (X509_NAME_print_ex(
          bio.get(), X509_get_issuer_name(cert_), 0, kX509NameFlagsMultiline) <=
      0) {
    return {};
  }
  return bio;
}

BIOPointer X509View::getInfoAccess() const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr) return {};
  BIOPointer bio(BIO_new(BIO_s_mem()));
  if (!bio) return {};
  int index = X509_get_ext_by_NID(cert_, NID_info_access, -1);
  if (index < 0) return {};
  if (!SafeX509InfoAccessPrint(bio, X509_get_ext(cert_, index))) {
    return {};
  }
  return bio;
}

BIOPointer X509View::getValidFrom() const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr) return {};
  BIOPointer bio(BIO_new(BIO_s_mem()));
  if (!bio) return {};
  ASN1_TIME_print(bio.get(), X509_get_notBefore(cert_));
  return bio;
}

BIOPointer X509View::getValidTo() const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr) return {};
  BIOPointer bio(BIO_new(BIO_s_mem()));
  if (!bio) return {};
  ASN1_TIME_print(bio.get(), X509_get_notAfter(cert_));
  return bio;
}

int64_t X509View::getValidToTime() const {
#ifdef OPENSSL_IS_BORINGSSL
  // Boringssl does not implement ASN1_TIME_to_tm in a public way,
  // and only recently added ASN1_TIME_to_posix. Some boringssl
  // users on older version may still need to patch around this
  // or use a different implementation.
  int64_t tp;
  ASN1_TIME_to_posix(X509_get0_notAfter(cert_), &tp);
  return tp;
#else
  struct tm tp;
  ASN1_TIME_to_tm(X509_get0_notAfter(cert_), &tp);
  return PortableTimeGM(&tp);
#endif
}

int64_t X509View::getValidFromTime() const {
#ifdef OPENSSL_IS_BORINGSSL
  int64_t tp;
  ASN1_TIME_to_posix(X509_get0_notBefore(cert_), &tp);
  return tp;
#else
  struct tm tp;
  ASN1_TIME_to_tm(X509_get0_notBefore(cert_), &tp);
  return PortableTimeGM(&tp);
#endif
}

DataPointer X509View::getSerialNumber() const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr) return {};
  if (ASN1_INTEGER* serial_number =
          X509_get_serialNumber(const_cast<X509*>(cert_))) {
    if (auto bn = BignumPointer(ASN1_INTEGER_to_BN(serial_number, nullptr))) {
      return bn.toHex();
    }
  }
  return {};
}

Result<EVPKeyPointer, int> X509View::getPublicKey() const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr) return Result<EVPKeyPointer, int>(EVPKeyPointer{});
  auto pkey = EVPKeyPointer(X509_get_pubkey(const_cast<X509*>(cert_)));
  if (!pkey) return Result<EVPKeyPointer, int>(ERR_get_error());
  return pkey;
}

StackOfASN1 X509View::getKeyUsage() const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr) return {};
  return StackOfASN1(static_cast<STACK_OF(ASN1_OBJECT)*>(
      X509_get_ext_d2i(cert_, NID_ext_key_usage, nullptr, nullptr)));
}

bool X509View::isCA() const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr) return false;
  return X509_check_ca(const_cast<X509*>(cert_)) == 1;
}

bool X509View::isIssuedBy(const X509View& issuer) const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr || issuer.cert_ == nullptr) return false;
  return X509_check_issued(const_cast<X509*>(issuer.cert_),
                           const_cast<X509*>(cert_)) == X509_V_OK;
}

bool X509View::checkPrivateKey(const EVPKeyPointer& pkey) const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr || pkey == nullptr) return false;
  return X509_check_private_key(const_cast<X509*>(cert_), pkey.get()) == 1;
}

bool X509View::checkPublicKey(const EVPKeyPointer& pkey) const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr || pkey == nullptr) return false;
  return X509_verify(const_cast<X509*>(cert_), pkey.get()) == 1;
}

X509View::CheckMatch X509View::checkHost(const std::string_view host,
                                         int flags,
                                         DataPointer* peerName) const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr) return CheckMatch::NO_MATCH;
  char* peername;
  switch (X509_check_host(
      const_cast<X509*>(cert_), host.data(), host.size(), flags, &peername)) {
    case 0:
      return CheckMatch::NO_MATCH;
    case 1: {
      if (peername != nullptr) {
        DataPointer name(peername, strlen(peername));
        if (peerName != nullptr) *peerName = std::move(name);
      }
      return CheckMatch::MATCH;
    }
    case -2:
      return CheckMatch::INVALID_NAME;
    default:
      return CheckMatch::OPERATION_FAILED;
  }
}

X509View::CheckMatch X509View::checkEmail(const std::string_view email,
                                          int flags) const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr) return CheckMatch::NO_MATCH;
  switch (X509_check_email(
      const_cast<X509*>(cert_), email.data(), email.size(), flags)) {
    case 0:
      return CheckMatch::NO_MATCH;
    case 1:
      return CheckMatch::MATCH;
    case -2:
      return CheckMatch::INVALID_NAME;
    default:
      return CheckMatch::OPERATION_FAILED;
  }
}

X509View::CheckMatch X509View::checkIp(const std::string_view ip,
                                       int flags) const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr) return CheckMatch::NO_MATCH;
  switch (X509_check_ip_asc(const_cast<X509*>(cert_), ip.data(), flags)) {
    case 0:
      return CheckMatch::NO_MATCH;
    case 1:
      return CheckMatch::MATCH;
    case -2:
      return CheckMatch::INVALID_NAME;
    default:
      return CheckMatch::OPERATION_FAILED;
  }
}

X509View X509View::From(const SSLPointer& ssl) {
  ClearErrorOnReturn clear_error_on_return;
  if (!ssl) return {};
  return X509View(SSL_get_certificate(ssl.get()));
}

X509View X509View::From(const SSLCtxPointer& ctx) {
  ClearErrorOnReturn clear_error_on_return;
  if (!ctx) return {};
  return X509View(SSL_CTX_get0_certificate(ctx.get()));
}

std::optional<std::string> X509View::getFingerprint(
    const EVP_MD* method) const {
  unsigned int md_size;
  unsigned char md[EVP_MAX_MD_SIZE];
  static constexpr char hex[] = "0123456789ABCDEF";

  if (X509_digest(get(), method, md, &md_size)) {
    if (md_size == 0) return std::nullopt;
    std::string fingerprint((md_size * 3) - 1, 0);
    for (unsigned int i = 0; i < md_size; i++) {
      auto idx = 3 * i;
      fingerprint[idx] = hex[(md[i] & 0xf0) >> 4];
      fingerprint[idx + 1] = hex[(md[i] & 0x0f)];
      if (i == md_size - 1) break;
      fingerprint[idx + 2] = ':';
    }

    return fingerprint;
  }

  return std::nullopt;
}

X509Pointer X509View::clone() const {
  ClearErrorOnReturn clear_error_on_return;
  if (!cert_) return {};
  return X509Pointer(X509_dup(const_cast<X509*>(cert_)));
}

Result<X509Pointer, int> X509Pointer::Parse(
    Buffer<const unsigned char> buffer) {
  ClearErrorOnReturn clearErrorOnReturn;
  BIOPointer bio(BIO_new_mem_buf(buffer.data, buffer.len));
  if (!bio) return Result<X509Pointer, int>(ERR_get_error());

  X509Pointer pem(
      PEM_read_bio_X509_AUX(bio.get(), nullptr, NoPasswordCallback, nullptr));
  if (pem) return Result<X509Pointer, int>(std::move(pem));
  BIO_reset(bio.get());

  X509Pointer der(d2i_X509_bio(bio.get(), nullptr));
  if (der) return Result<X509Pointer, int>(std::move(der));

  return Result<X509Pointer, int>(ERR_get_error());
}

bool X509View::enumUsages(UsageCallback callback) const {
  if (cert_ == nullptr) return false;
  StackOfASN1 eku(static_cast<STACK_OF(ASN1_OBJECT)*>(
      X509_get_ext_d2i(cert_, NID_ext_key_usage, nullptr, nullptr)));
  if (!eku) return false;
  const int count = sk_ASN1_OBJECT_num(eku.get());
  char buf[256]{};

  for (int i = 0; i < count; i++) {
    if (OBJ_obj2txt(buf, sizeof(buf), sk_ASN1_OBJECT_value(eku.get(), i), 1) >=
        0) {
      callback(buf);
    }
  }
  return true;
}

bool X509View::ifRsa(KeyCallback<Rsa> callback) const {
  if (cert_ == nullptr) return true;
  OSSL3_CONST EVP_PKEY* pkey = X509_get0_pubkey(cert_);
  auto id = EVP_PKEY_id(pkey);
  if (id == EVP_PKEY_RSA || id == EVP_PKEY_RSA2 || id == EVP_PKEY_RSA_PSS) {
    Rsa rsa(EVP_PKEY_get0_RSA(pkey));
    if (!rsa) [[unlikely]]
      return true;
    return callback(rsa);
  }
  return true;
}

bool X509View::ifEc(KeyCallback<Ec> callback) const {
  if (cert_ == nullptr) return true;
  OSSL3_CONST EVP_PKEY* pkey = X509_get0_pubkey(cert_);
  auto id = EVP_PKEY_id(pkey);
  if (id == EVP_PKEY_EC) {
    Ec ec(EVP_PKEY_get0_EC_KEY(pkey));
    if (!ec) [[unlikely]]
      return true;
    return callback(ec);
  }
  return true;
}

X509Pointer X509Pointer::IssuerFrom(const SSLPointer& ssl,
                                    const X509View& view) {
  return IssuerFrom(SSL_get_SSL_CTX(ssl.get()), view);
}

X509Pointer X509Pointer::IssuerFrom(const SSL_CTX* ctx, const X509View& cert) {
  X509_STORE* store = SSL_CTX_get_cert_store(ctx);
  DeleteFnPtr<X509_STORE_CTX, X509_STORE_CTX_free> store_ctx(
      X509_STORE_CTX_new());
  X509Pointer result;
  X509* issuer;
  if (store_ctx.get() != nullptr &&
      X509_STORE_CTX_init(store_ctx.get(), store, nullptr, nullptr) == 1 &&
      X509_STORE_CTX_get1_issuer(&issuer, store_ctx.get(), cert.get()) == 1) {
    result.reset(issuer);
  }
  return result;
}

X509Pointer X509Pointer::PeerFrom(const SSLPointer& ssl) {
  return X509Pointer(SSL_get_peer_certificate(ssl.get()));
}

// When adding or removing errors below, please also update the list in the API
// documentation. See the "OpenSSL Error Codes" section of doc/api/errors.md
// Also *please* update the respective section in doc/api/tls.md as well
std::string_view X509Pointer::ErrorCode(int32_t err) {  // NOLINT(runtime/int)
#define CASE(CODE)                                                             \
  case X509_V_ERR_##CODE:                                                      \
    return #CODE;
  switch (err) {
    CASE(UNABLE_TO_GET_ISSUER_CERT)
    CASE(UNABLE_TO_GET_CRL)
    CASE(UNABLE_TO_DECRYPT_CERT_SIGNATURE)
    CASE(UNABLE_TO_DECRYPT_CRL_SIGNATURE)
    CASE(UNABLE_TO_DECODE_ISSUER_PUBLIC_KEY)
    CASE(CERT_SIGNATURE_FAILURE)
    CASE(CRL_SIGNATURE_FAILURE)
    CASE(CERT_NOT_YET_VALID)
    CASE(CERT_HAS_EXPIRED)
    CASE(CRL_NOT_YET_VALID)
    CASE(CRL_HAS_EXPIRED)
    CASE(ERROR_IN_CERT_NOT_BEFORE_FIELD)
    CASE(ERROR_IN_CERT_NOT_AFTER_FIELD)
    CASE(ERROR_IN_CRL_LAST_UPDATE_FIELD)
    CASE(ERROR_IN_CRL_NEXT_UPDATE_FIELD)
    CASE(OUT_OF_MEM)
    CASE(DEPTH_ZERO_SELF_SIGNED_CERT)
    CASE(SELF_SIGNED_CERT_IN_CHAIN)
    CASE(UNABLE_TO_GET_ISSUER_CERT_LOCALLY)
    CASE(UNABLE_TO_VERIFY_LEAF_SIGNATURE)
    CASE(CERT_CHAIN_TOO_LONG)
    CASE(CERT_REVOKED)
    CASE(INVALID_CA)
    CASE(PATH_LENGTH_EXCEEDED)
    CASE(INVALID_PURPOSE)
    CASE(CERT_UNTRUSTED)
    CASE(CERT_REJECTED)
    CASE(HOSTNAME_MISMATCH)
  }
#undef CASE
  return "UNSPECIFIED";
}

std::optional<std::string_view> X509Pointer::ErrorReason(int32_t err) {
  if (err == X509_V_OK) return std::nullopt;
  return X509_verify_cert_error_string(err);
}

// ============================================================================
// BIOPointer

BIOPointer::BIOPointer(BIO* bio) : bio_(bio) {}

BIOPointer::BIOPointer(BIOPointer&& other) noexcept : bio_(other.release()) {}

BIOPointer& BIOPointer::operator=(BIOPointer&& other) noexcept {
  if (this == &other) return *this;
  this->~BIOPointer();
  return *new (this) BIOPointer(std::move(other));
}

BIOPointer::~BIOPointer() {
  reset();
}

void BIOPointer::reset(BIO* bio) {
  bio_.reset(bio);
}

BIO* BIOPointer::release() {
  return bio_.release();
}

bool BIOPointer::resetBio() const {
  if (!bio_) return 0;
  return BIO_reset(bio_.get()) == 1;
}

BIOPointer BIOPointer::NewMem() {
  return BIOPointer(BIO_new(BIO_s_mem()));
}

BIOPointer BIOPointer::NewSecMem() {
#ifdef OPENSSL_IS_BORINGSSL
  // Boringssl does not implement the BIO_s_secmem API.
  return BIOPointer(BIO_new(BIO_s_mem()));
#else
  return BIOPointer(BIO_new(BIO_s_secmem()));
#endif
}

BIOPointer BIOPointer::New(const BIO_METHOD* method) {
  return BIOPointer(BIO_new(method));
}

BIOPointer BIOPointer::New(const void* data, size_t len) {
  return BIOPointer(BIO_new_mem_buf(data, len));
}

BIOPointer BIOPointer::NewFile(std::string_view filename,
                               std::string_view mode) {
  return BIOPointer(BIO_new_file(filename.data(), mode.data()));
}

BIOPointer BIOPointer::NewFp(FILE* fd, int close_flag) {
  return BIOPointer(BIO_new_fp(fd, close_flag));
}

BIOPointer BIOPointer::New(const BIGNUM* bn) {
  auto res = NewMem();
  if (!res || !BN_print(res.get(), bn)) return {};
  return res;
}

int BIOPointer::Write(BIOPointer* bio, std::string_view message) {
  if (bio == nullptr || !*bio) return 0;
  return BIO_write(bio->get(), message.data(), message.size());
}

// ============================================================================
// DHPointer

namespace {
bool EqualNoCase(const std::string_view a, const std::string_view b) {
  if (a.size() != b.size()) return false;
  return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char a, char b) {
    return std::tolower(a) == std::tolower(b);
  });
}
}  // namespace

DHPointer::DHPointer(DH* dh) : dh_(dh) {}

DHPointer::DHPointer(DHPointer&& other) noexcept : dh_(other.release()) {}

DHPointer& DHPointer::operator=(DHPointer&& other) noexcept {
  if (this == &other) return *this;
  this->~DHPointer();
  return *new (this) DHPointer(std::move(other));
}

DHPointer::~DHPointer() {
  reset();
}

void DHPointer::reset(DH* dh) {
  dh_.reset(dh);
}

DH* DHPointer::release() {
  return dh_.release();
}

BignumPointer DHPointer::FindGroup(const std::string_view name,
                                   FindGroupOption option) {
#define V(n, p)                                                                \
  if (EqualNoCase(name, n)) return BignumPointer(p(nullptr));
  if (option != FindGroupOption::NO_SMALL_PRIMES) {
#ifndef OPENSSL_IS_BORINGSSL
    // Boringssl does not support the 768 and 1024 small primes
    V("modp1", BN_get_rfc2409_prime_768);
    V("modp2", BN_get_rfc2409_prime_1024);
#endif
    V("modp5", BN_get_rfc3526_prime_1536);
  }
  V("modp14", BN_get_rfc3526_prime_2048);
  V("modp15", BN_get_rfc3526_prime_3072);
  V("modp16", BN_get_rfc3526_prime_4096);
  V("modp17", BN_get_rfc3526_prime_6144);
  V("modp18", BN_get_rfc3526_prime_8192);
#undef V
  return {};
}

BignumPointer DHPointer::GetStandardGenerator() {
  auto bn = BignumPointer::New();
  if (!bn) return {};
  if (!bn.setWord(DH_GENERATOR_2)) return {};
  return bn;
}

DHPointer DHPointer::FromGroup(const std::string_view name,
                               FindGroupOption option) {
  auto group = FindGroup(name, option);
  if (!group) return {};  // Unable to find the named group.

  auto generator = GetStandardGenerator();
  if (!generator) return {};  // Unable to create the generator.

  return New(std::move(group), std::move(generator));
}

DHPointer DHPointer::New(BignumPointer&& p, BignumPointer&& g) {
  if (!p || !g) return {};

  DHPointer dh(DH_new());
  if (!dh) return {};

  if (DH_set0_pqg(dh.get(), p.get(), nullptr, g.get()) != 1) return {};

  // If the call above is successful, the DH object takes ownership of the
  // BIGNUMs, so we must release them here. Unfortunately coverity does not
  // know that so we need to tell it not to complain.
  // coverity[resource_leak]
  p.release();
  // coverity[resource_leak]
  g.release();

  return dh;
}

DHPointer DHPointer::New(size_t bits, unsigned int generator) {
  DHPointer dh(DH_new());
  if (!dh) return {};

  if (DH_generate_parameters_ex(dh.get(), bits, generator, nullptr) != 1) {
    return {};
  }

  return dh;
}

DHPointer::CheckResult DHPointer::check() {
  ClearErrorOnReturn clearErrorOnReturn;
  if (!dh_) return DHPointer::CheckResult::NONE;
  int codes = 0;
  if (DH_check(dh_.get(), &codes) != 1)
    return DHPointer::CheckResult::CHECK_FAILED;
  return static_cast<CheckResult>(codes);
}

DHPointer::CheckPublicKeyResult DHPointer::checkPublicKey(
    const BignumPointer& pub_key) {
  ClearErrorOnReturn clearErrorOnReturn;
  if (!pub_key || !dh_) {
    return DHPointer::CheckPublicKeyResult::CHECK_FAILED;
  }
  int codes = 0;
  if (DH_check_pub_key(dh_.get(), pub_key.get(), &codes) != 1) {
    return DHPointer::CheckPublicKeyResult::CHECK_FAILED;
  }
#ifndef OPENSSL_IS_BORINGSSL
  // Boringssl does not define DH_CHECK_PUBKEY_TOO_SMALL or TOO_LARGE
  if (codes & DH_CHECK_PUBKEY_TOO_SMALL) {
    return DHPointer::CheckPublicKeyResult::TOO_SMALL;
  } else if (codes & DH_CHECK_PUBKEY_TOO_LARGE) {
    return DHPointer::CheckPublicKeyResult::TOO_LARGE;
  }
#endif
  if (codes != 0) {
    return DHPointer::CheckPublicKeyResult::INVALID;
  }
  return CheckPublicKeyResult::NONE;
}

DataPointer DHPointer::getPrime() const {
  if (!dh_) return {};
  const BIGNUM* p;
  DH_get0_pqg(dh_.get(), &p, nullptr, nullptr);
  return BignumPointer::Encode(p);
}

DataPointer DHPointer::getGenerator() const {
  if (!dh_) return {};
  const BIGNUM* g;
  DH_get0_pqg(dh_.get(), nullptr, nullptr, &g);
  return BignumPointer::Encode(g);
}

DataPointer DHPointer::getPublicKey() const {
  if (!dh_) return {};
  const BIGNUM* pub_key;
  DH_get0_key(dh_.get(), &pub_key, nullptr);
  return BignumPointer::Encode(pub_key);
}

DataPointer DHPointer::getPrivateKey() const {
  if (!dh_) return {};
  const BIGNUM* pvt_key;
  DH_get0_key(dh_.get(), nullptr, &pvt_key);
  return BignumPointer::Encode(pvt_key);
}

DataPointer DHPointer::generateKeys() const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (!dh_) return {};

  // Key generation failed
  if (!DH_generate_key(dh_.get())) return {};

  return getPublicKey();
}

size_t DHPointer::size() const {
  if (!dh_) return 0;
  int ret = DH_size(dh_.get());
  // DH_size can return a -1 on error but we just want to return a 0
  // in that case so we don't wrap around when returning the size_t.
  return ret >= 0 ? static_cast<size_t>(ret) : 0;
}

DataPointer DHPointer::computeSecret(const BignumPointer& peer) const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (!dh_ || !peer) return {};

  auto dp = DataPointer::Alloc(size());
  if (!dp) return {};

  int size =
      DH_compute_key(static_cast<uint8_t*>(dp.get()), peer.get(), dh_.get());
  if (size < 0) return {};

  // The size of the computed key can be smaller than the size of the DH key.
  // We want to make sure that the key is correctly padded.
  if (static_cast<size_t>(size) < dp.size()) {
    const size_t padding = dp.size() - size;
    uint8_t* data = static_cast<uint8_t*>(dp.get());
    memmove(data + padding, data, size);
    memset(data, 0, padding);
  }

  return dp;
}

bool DHPointer::setPublicKey(BignumPointer&& key) {
  if (!dh_) return false;
  if (DH_set0_key(dh_.get(), key.get(), nullptr) == 1) {
    // If DH_set0_key returns successfully, then dh_ takes ownership of the
    // BIGNUM, so we must release it here. Unfortunately coverity does not
    // know that so we need to tell it not to complain.
    // coverity[resource_leak]
    key.release();
    return true;
  }
  return false;
}

bool DHPointer::setPrivateKey(BignumPointer&& key) {
  if (!dh_) return false;
  if (DH_set0_key(dh_.get(), nullptr, key.get()) == 1) {
    // If DH_set0_key returns successfully, then dh_ takes ownership of the
    // BIGNUM, so we must release it here. Unfortunately coverity does not
    // know that so we need to tell it not to complain.
    // coverity[resource_leak]
    key.release();
    return true;
  }
  return false;
}

DataPointer DHPointer::stateless(const EVPKeyPointer& ourKey,
                                 const EVPKeyPointer& theirKey) {
  size_t out_size;
  if (!ourKey || !theirKey) return {};

  auto ctx = EVPKeyCtxPointer::New(ourKey);
  if (!ctx || EVP_PKEY_derive_init(ctx.get()) <= 0 ||
      EVP_PKEY_derive_set_peer(ctx.get(), theirKey.get()) <= 0 ||
      EVP_PKEY_derive(ctx.get(), nullptr, &out_size) <= 0) {
    return {};
  }

  if (out_size == 0) return {};

  auto out = DataPointer::Alloc(out_size);
  if (EVP_PKEY_derive(
          ctx.get(), reinterpret_cast<uint8_t*>(out.get()), &out_size) <= 0) {
    return {};
  }

  if (out_size < out.size()) {
    const size_t padding = out.size() - out_size;
    uint8_t* data = static_cast<uint8_t*>(out.get());
    memmove(data + padding, data, out_size);
    memset(data, 0, padding);
  }

  return out;
}

// ============================================================================
// KDF

const EVP_MD* getDigestByName(const std::string_view name) {
  // Historically, "dss1" and "DSS1" were DSA aliases for SHA-1
  // exposed through the public API.
  if (name == "dss1" || name == "DSS1") [[unlikely]] {
    return EVP_sha1();
  }
  return EVP_get_digestbyname(name.data());
}

const EVP_CIPHER* getCipherByName(const std::string_view name) {
  return EVP_get_cipherbyname(name.data());
}

bool checkHkdfLength(const EVP_MD* md, size_t length) {
  // HKDF-Expand computes up to 255 HMAC blocks, each having as many bits as
  // the output of the hash function. 255 is a hard limit because HKDF appends
  // an 8-bit counter to each HMAC'd message, starting at 1.
  static constexpr size_t kMaxDigestMultiplier = 255;
  size_t max_length = EVP_MD_size(md) * kMaxDigestMultiplier;
  if (length > max_length) return false;
  return true;
}

DataPointer hkdf(const EVP_MD* md,
                 const Buffer<const unsigned char>& key,
                 const Buffer<const unsigned char>& info,
                 const Buffer<const unsigned char>& salt,
                 size_t length) {
  ClearErrorOnReturn clearErrorOnReturn;

  if (!checkHkdfLength(md, length) || info.len > INT_MAX ||
      salt.len > INT_MAX) {
    return {};
  }

  auto ctx = EVPKeyCtxPointer::NewFromID(EVP_PKEY_HKDF);
  if (!ctx || !EVP_PKEY_derive_init(ctx.get()) ||
      !EVP_PKEY_CTX_set_hkdf_md(ctx.get(), md) ||
      !EVP_PKEY_CTX_add1_hkdf_info(ctx.get(), info.data, info.len)) {
    return {};
  }

  std::string_view actual_salt;
  static const char default_salt[EVP_MAX_MD_SIZE] = {0};
  if (salt.len > 0) {
    actual_salt = {reinterpret_cast<const char*>(salt.data), salt.len};
  } else {
    actual_salt = {default_salt, static_cast<unsigned>(EVP_MD_size(md))};
  }

  // We do not use EVP_PKEY_HKDF_MODE_EXTRACT_AND_EXPAND because and instead
  // implement the extraction step ourselves because EVP_PKEY_derive does not
  // handle zero-length keys, which are required for Web Crypto.
  // TODO(jasnell): Once OpenSSL 1.1.1 support is dropped completely, and once
  // BoringSSL is confirmed to support it, wen can hopefully drop this and use
  // EVP_KDF directly which does support zero length keys.
  unsigned char pseudorandom_key[EVP_MAX_MD_SIZE];
  unsigned pseudorandom_key_len = sizeof(pseudorandom_key);

  if (HMAC(md,
           actual_salt.data(),
           actual_salt.size(),
           key.data,
           key.len,
           pseudorandom_key,
           &pseudorandom_key_len) == nullptr) {
    return {};
  }
  if (!EVP_PKEY_CTX_hkdf_mode(ctx.get(), EVP_PKEY_HKDEF_MODE_EXPAND_ONLY) ||
      !EVP_PKEY_CTX_set1_hkdf_key(
          ctx.get(), pseudorandom_key, pseudorandom_key_len)) {
    return {};
  }

  auto buf = DataPointer::Alloc(length);
  if (!buf) return {};

  if (EVP_PKEY_derive(
          ctx.get(), static_cast<unsigned char*>(buf.get()), &length) <= 0) {
    return {};
  }

  return buf;
}

bool checkScryptParams(uint64_t N, uint64_t r, uint64_t p, uint64_t maxmem) {
  return EVP_PBE_scrypt(nullptr, 0, nullptr, 0, N, r, p, maxmem, nullptr, 0) ==
         1;
}

DataPointer scrypt(const Buffer<const char>& pass,
                   const Buffer<const unsigned char>& salt,
                   uint64_t N,
                   uint64_t r,
                   uint64_t p,
                   uint64_t maxmem,
                   size_t length) {
  ClearErrorOnReturn clearErrorOnReturn;

  if (pass.len > INT_MAX || salt.len > INT_MAX) {
    return {};
  }

  auto dp = DataPointer::Alloc(length);
  if (dp && EVP_PBE_scrypt(pass.data,
                           pass.len,
                           salt.data,
                           salt.len,
                           N,
                           r,
                           p,
                           maxmem,
                           reinterpret_cast<unsigned char*>(dp.get()),
                           length)) {
    return dp;
  }

  return {};
}

DataPointer pbkdf2(const EVP_MD* md,
                   const Buffer<const char>& pass,
                   const Buffer<const unsigned char>& salt,
                   uint32_t iterations,
                   size_t length) {
  ClearErrorOnReturn clearErrorOnReturn;

  if (pass.len > INT_MAX || salt.len > INT_MAX || length > INT_MAX) {
    return {};
  }

  auto dp = DataPointer::Alloc(length);
  if (dp && PKCS5_PBKDF2_HMAC(pass.data,
                              pass.len,
                              salt.data,
                              salt.len,
                              iterations,
                              md,
                              length,
                              reinterpret_cast<unsigned char*>(dp.get()))) {
    return dp;
  }

  return {};
}

// ============================================================================

EVPKeyPointer::PrivateKeyEncodingConfig::PrivateKeyEncodingConfig(
    const PrivateKeyEncodingConfig& other)
    : PrivateKeyEncodingConfig(
          other.output_key_object, other.format, other.type) {
  cipher = other.cipher;
  if (other.passphrase.has_value()) {
    auto& otherPassphrase = other.passphrase.value();
    auto newPassphrase = DataPointer::Alloc(otherPassphrase.size());
    memcpy(newPassphrase.get(), otherPassphrase.get(), otherPassphrase.size());
    passphrase = std::move(newPassphrase);
  }
}

EVPKeyPointer::AsymmetricKeyEncodingConfig::AsymmetricKeyEncodingConfig(
    bool output_key_object, PKFormatType format, PKEncodingType type)
    : output_key_object(output_key_object), format(format), type(type) {}

EVPKeyPointer::PrivateKeyEncodingConfig&
EVPKeyPointer::PrivateKeyEncodingConfig::operator=(
    const PrivateKeyEncodingConfig& other) {
  if (this == &other) return *this;
  this->~PrivateKeyEncodingConfig();
  return *new (this) PrivateKeyEncodingConfig(other);
}

EVPKeyPointer EVPKeyPointer::New() {
  return EVPKeyPointer(EVP_PKEY_new());
}

EVPKeyPointer EVPKeyPointer::NewRawPublic(
    int id, const Buffer<const unsigned char>& data) {
  if (id == 0) return {};
  return EVPKeyPointer(
      EVP_PKEY_new_raw_public_key(id, nullptr, data.data, data.len));
}

EVPKeyPointer EVPKeyPointer::NewRawPrivate(
    int id, const Buffer<const unsigned char>& data) {
  if (id == 0) return {};
  return EVPKeyPointer(
      EVP_PKEY_new_raw_private_key(id, nullptr, data.data, data.len));
}

EVPKeyPointer EVPKeyPointer::NewDH(DHPointer&& dh) {
  if (!dh) return {};
  auto key = New();
  if (!key) return {};
  if (EVP_PKEY_assign_DH(key.get(), dh.get())) {
    dh.release();
  }
  return key;
}

EVPKeyPointer EVPKeyPointer::NewRSA(RSAPointer&& rsa) {
  if (!rsa) return {};
  auto key = New();
  if (!key) return {};
  if (EVP_PKEY_assign_RSA(key.get(), rsa.get())) {
    rsa.release();
  }
  return key;
}

EVPKeyPointer::EVPKeyPointer(EVP_PKEY* pkey) : pkey_(pkey) {}

EVPKeyPointer::EVPKeyPointer(EVPKeyPointer&& other) noexcept
    : pkey_(other.release()) {}

EVPKeyPointer& EVPKeyPointer::operator=(EVPKeyPointer&& other) noexcept {
  if (this == &other) return *this;
  this->~EVPKeyPointer();
  return *new (this) EVPKeyPointer(std::move(other));
}

EVPKeyPointer::~EVPKeyPointer() {
  reset();
}

void EVPKeyPointer::reset(EVP_PKEY* pkey) {
  pkey_.reset(pkey);
}

EVP_PKEY* EVPKeyPointer::release() {
  return pkey_.release();
}

int EVPKeyPointer::id(const EVP_PKEY* key) {
  if (key == nullptr) return 0;
  return EVP_PKEY_id(key);
}

int EVPKeyPointer::base_id(const EVP_PKEY* key) {
  if (key == nullptr) return 0;
  return EVP_PKEY_base_id(key);
}

int EVPKeyPointer::id() const {
  return id(get());
}

int EVPKeyPointer::base_id() const {
  return base_id(get());
}

int EVPKeyPointer::bits() const {
  if (get() == nullptr) return 0;
  return EVP_PKEY_bits(get());
}

size_t EVPKeyPointer::size() const {
  if (get() == nullptr) return 0;
  return EVP_PKEY_size(get());
}

EVPKeyCtxPointer EVPKeyPointer::newCtx() const {
  if (!pkey_) return {};
  return EVPKeyCtxPointer::New(*this);
}

size_t EVPKeyPointer::rawPublicKeySize() const {
  if (!pkey_) return 0;
  size_t len = 0;
  if (EVP_PKEY_get_raw_public_key(get(), nullptr, &len) == 1) return len;
  return 0;
}

size_t EVPKeyPointer::rawPrivateKeySize() const {
  if (!pkey_) return 0;
  size_t len = 0;
  if (EVP_PKEY_get_raw_private_key(get(), nullptr, &len) == 1) return len;
  return 0;
}

DataPointer EVPKeyPointer::rawPublicKey() const {
  if (!pkey_) return {};
  if (auto data = DataPointer::Alloc(rawPublicKeySize())) {
    const Buffer<unsigned char> buf = data;
    size_t len = data.size();
    if (EVP_PKEY_get_raw_public_key(get(), buf.data, &len) != 1) return {};
    return data;
  }
  return {};
}

DataPointer EVPKeyPointer::rawPrivateKey() const {
  if (!pkey_) return {};
  if (auto data = DataPointer::Alloc(rawPrivateKeySize())) {
    const Buffer<unsigned char> buf = data;
    size_t len = data.size();
    if (EVP_PKEY_get_raw_private_key(get(), buf.data, &len) != 1) return {};
    return data;
  }
  return {};
}

BIOPointer EVPKeyPointer::derPublicKey() const {
  if (!pkey_) return {};
  auto bio = BIOPointer::NewMem();
  if (!bio) return {};
  if (!i2d_PUBKEY_bio(bio.get(), get())) return {};
  return bio;
}

bool EVPKeyPointer::assign(const ECKeyPointer& eckey) {
  if (!pkey_ || !eckey) return {};
  return EVP_PKEY_assign_EC_KEY(pkey_.get(), eckey.get());
}

bool EVPKeyPointer::set(const ECKeyPointer& eckey) {
  if (!pkey_ || !eckey) return false;
  return EVP_PKEY_set1_EC_KEY(pkey_.get(), eckey);
}

EVPKeyPointer::operator const EC_KEY*() const {
  if (!pkey_) return nullptr;
  return EVP_PKEY_get0_EC_KEY(pkey_.get());
}

namespace {
EVPKeyPointer::ParseKeyResult TryParsePublicKeyInner(const BIOPointer& bp,
                                                     const char* name,
                                                     auto&& parse) {
  if (!bp.resetBio()) {
    return EVPKeyPointer::ParseKeyResult(EVPKeyPointer::PKParseError::FAILED);
  }
  unsigned char* der_data;
  long der_len;  // NOLINT(runtime/int)

  // This skips surrounding data and decodes PEM to DER.
  {
    MarkPopErrorOnReturn mark_pop_error_on_return;
    if (PEM_bytes_read_bio(
            &der_data, &der_len, nullptr, name, bp.get(), nullptr, nullptr) !=
        1)
      return EVPKeyPointer::ParseKeyResult(
          EVPKeyPointer::PKParseError::NOT_RECOGNIZED);
  }
  DataPointer data(der_data, der_len);

  // OpenSSL might modify the pointer, so we need to make a copy before parsing.
  const unsigned char* p = der_data;
  EVPKeyPointer pkey(parse(&p, der_len));
  if (!pkey)
    return EVPKeyPointer::ParseKeyResult(EVPKeyPointer::PKParseError::FAILED);
  return EVPKeyPointer::ParseKeyResult(std::move(pkey));
}

constexpr bool IsASN1Sequence(const unsigned char* data,
                              size_t size,
                              size_t* data_offset,
                              size_t* data_size) {
  if (size < 2 || data[0] != 0x30) return false;

  if (data[1] & 0x80) {
    // Long form.
    size_t n_bytes = data[1] & ~0x80;
    if (n_bytes + 2 > size || n_bytes > sizeof(size_t)) return false;
    size_t length = 0;
    for (size_t i = 0; i < n_bytes; i++) length = (length << 8) | data[i + 2];
    *data_offset = 2 + n_bytes;
    *data_size = std::min(size - 2 - n_bytes, length);
  } else {
    // Short form.
    *data_offset = 2;
    *data_size = std::min<size_t>(size - 2, data[1]);
  }

  return true;
}

constexpr bool IsEncryptedPrivateKeyInfo(
    const Buffer<const unsigned char>& buffer) {
  // Both PrivateKeyInfo and EncryptedPrivateKeyInfo start with a SEQUENCE.
  if (buffer.len == 0 || buffer.data == nullptr) return false;
  size_t offset, len;
  if (!IsASN1Sequence(buffer.data, buffer.len, &offset, &len)) return false;

  // A PrivateKeyInfo sequence always starts with an integer whereas an
  // EncryptedPrivateKeyInfo starts with an AlgorithmIdentifier.
  return len >= 1 && buffer.data[offset] != 2;
}

}  // namespace

bool EVPKeyPointer::IsRSAPrivateKey(const Buffer<const unsigned char>& buffer) {
  // Both RSAPrivateKey and RSAPublicKey structures start with a SEQUENCE.
  size_t offset, len;
  if (!IsASN1Sequence(buffer.data, buffer.len, &offset, &len)) return false;

  // An RSAPrivateKey sequence always starts with a single-byte integer whose
  // value is either 0 or 1, whereas an RSAPublicKey starts with the modulus
  // (which is the product of two primes and therefore at least 4), so we can
  // decide the type of the structure based on the first three bytes of the
  // sequence.
  return len >= 3 && buffer.data[offset] == 2 && buffer.data[offset + 1] == 1 &&
         !(buffer.data[offset + 2] & 0xfe);
}

EVPKeyPointer::ParseKeyResult EVPKeyPointer::TryParsePublicKeyPEM(
    const Buffer<const unsigned char>& buffer) {
  auto bp = BIOPointer::New(buffer.data, buffer.len);
  if (!bp) return ParseKeyResult(PKParseError::FAILED);

  // Try parsing as SubjectPublicKeyInfo (SPKI) first.
  if (auto ret = TryParsePublicKeyInner(
          bp,
          "PUBLIC KEY",
          [](const unsigned char** p, long l) {  // NOLINT(runtime/int)
            return d2i_PUBKEY(nullptr, p, l);
          })) {
    return ret;
  }

  // Maybe it is PKCS#1.
  if (auto ret = TryParsePublicKeyInner(
          bp,
          "RSA PUBLIC KEY",
          [](const unsigned char** p, long l) {  // NOLINT(runtime/int)
            return d2i_PublicKey(EVP_PKEY_RSA, nullptr, p, l);
          })) {
    return ret;
  }

  // X.509 fallback.
  if (auto ret = TryParsePublicKeyInner(
          bp,
          "CERTIFICATE",
          [](const unsigned char** p, long l) {  // NOLINT(runtime/int)
            X509Pointer x509(d2i_X509(nullptr, p, l));
            return x509 ? X509_get_pubkey(x509.get()) : nullptr;
          })) {
    return ret;
  };

  return ParseKeyResult(PKParseError::NOT_RECOGNIZED);
}

EVPKeyPointer::ParseKeyResult EVPKeyPointer::TryParsePublicKey(
    const PublicKeyEncodingConfig& config,
    const Buffer<const unsigned char>& buffer) {
  if (config.format == PKFormatType::PEM) {
    return TryParsePublicKeyPEM(buffer);
  }

  if (config.format != PKFormatType::DER) {
    return ParseKeyResult(PKParseError::FAILED);
  }

  const unsigned char* start = buffer.data;

  EVP_PKEY* key = nullptr;

  if (config.type == PKEncodingType::PKCS1 &&
      (key = d2i_PublicKey(EVP_PKEY_RSA, nullptr, &start, buffer.len))) {
    return EVPKeyPointer::ParseKeyResult(EVPKeyPointer(key));
  }

  if (config.type == PKEncodingType::SPKI &&
      (key = d2i_PUBKEY(nullptr, &start, buffer.len))) {
    return EVPKeyPointer::ParseKeyResult(EVPKeyPointer(key));
  }

  return ParseKeyResult(PKParseError::FAILED);
}

namespace {
Buffer<char> GetPassphrase(
    const EVPKeyPointer::PrivateKeyEncodingConfig& config) {
  Buffer<char> pass{
      // OpenSSL will not actually dereference this pointer, so it can be any
      // non-null pointer. We cannot assert that directly, which is why we
      // intentionally use a pointer that will likely cause a segmentation fault
      // when dereferenced.
      .data = reinterpret_cast<char*>(-1),
      .len = 0,
  };
  if (config.passphrase.has_value()) {
    auto& passphrase = config.passphrase.value();
    // The pass.data can't be a nullptr, even if the len is zero or else
    // openssl will prompt for a password and we really don't want that.
    if (passphrase.get() != nullptr) {
      pass.data = static_cast<char*>(passphrase.get());
    }
    pass.len = passphrase.size();
  }
  return pass;
}
}  // namespace

EVPKeyPointer::ParseKeyResult EVPKeyPointer::TryParsePrivateKey(
    const PrivateKeyEncodingConfig& config,
    const Buffer<const unsigned char>& buffer) {
  static constexpr auto keyOrError = [](EVPKeyPointer pkey,
                                        bool had_passphrase = false) {
    if (int err = ERR_peek_error()) {
      if (ERR_GET_LIB(err) == ERR_LIB_PEM &&
          ERR_GET_REASON(err) == PEM_R_BAD_PASSWORD_READ && !had_passphrase) {
        return ParseKeyResult(PKParseError::NEED_PASSPHRASE);
      }
      return ParseKeyResult(PKParseError::FAILED, err);
    }
    if (!pkey) return ParseKeyResult(PKParseError::FAILED);
    return ParseKeyResult(std::move(pkey));
  };

  auto bio = BIOPointer::New(buffer);
  if (!bio) return ParseKeyResult(PKParseError::FAILED);

  auto passphrase = GetPassphrase(config);

  if (config.format == PKFormatType::PEM) {
    auto key = PEM_read_bio_PrivateKey(
        bio.get(),
        nullptr,
        PasswordCallback,
        config.passphrase.has_value() ? &passphrase : nullptr);
    return keyOrError(EVPKeyPointer(key), config.passphrase.has_value());
  }

  if (config.format != PKFormatType::DER) {
    return ParseKeyResult(PKParseError::FAILED);
  }

  switch (config.type) {
    case PKEncodingType::PKCS1: {
      auto key = d2i_PrivateKey_bio(bio.get(), nullptr);
      return keyOrError(EVPKeyPointer(key));
    }
    case PKEncodingType::PKCS8: {
      if (IsEncryptedPrivateKeyInfo(buffer)) {
        auto key = d2i_PKCS8PrivateKey_bio(
            bio.get(),
            nullptr,
            PasswordCallback,
            config.passphrase.has_value() ? &passphrase : nullptr);
        return keyOrError(EVPKeyPointer(key), config.passphrase.has_value());
      }

      PKCS8Pointer p8inf(d2i_PKCS8_PRIV_KEY_INFO_bio(bio.get(), nullptr));
      if (!p8inf) {
        return ParseKeyResult(PKParseError::FAILED, ERR_peek_error());
      }
      return keyOrError(EVPKeyPointer(EVP_PKCS82PKEY(p8inf.get())));
    }
    case PKEncodingType::SEC1: {
      auto key = d2i_PrivateKey_bio(bio.get(), nullptr);
      return keyOrError(EVPKeyPointer(key));
    }
    default: {
      return ParseKeyResult(PKParseError::FAILED, ERR_peek_error());
    }
  };
}

Result<BIOPointer, bool> EVPKeyPointer::writePrivateKey(
    const PrivateKeyEncodingConfig& config) const {
  if (config.format == PKFormatType::JWK) {
    return Result<BIOPointer, bool>(false);
  }

  auto bio = BIOPointer::NewMem();
  if (!bio) {
    return Result<BIOPointer, bool>(false);
  }

  auto passphrase = GetPassphrase(config);
  MarkPopErrorOnReturn mark_pop_error_on_return;
  bool err;

  switch (config.type) {
    case PKEncodingType::PKCS1: {
      // PKCS1 is only permitted for RSA keys.
      if (id() != EVP_PKEY_RSA) return Result<BIOPointer, bool>(false);

#if OPENSSL_VERSION_MAJOR >= 3
      const RSA* rsa = EVP_PKEY_get0_RSA(get());
#else
      RSA* rsa = EVP_PKEY_get0_RSA(get());
#endif
      switch (config.format) {
        case PKFormatType::PEM: {
          err = PEM_write_bio_RSAPrivateKey(
                    bio.get(),
                    rsa,
                    config.cipher,
                    reinterpret_cast<unsigned char*>(passphrase.data),
                    passphrase.len,
                    nullptr,
                    nullptr) != 1;
          break;
        }
        case PKFormatType::DER: {
          // Encoding PKCS1 as DER. This variation does not permit encryption.
          err = i2d_RSAPrivateKey_bio(bio.get(), rsa) != 1;
          break;
        }
        default: {
          // Should never get here.
          return Result<BIOPointer, bool>(false);
        }
      }
      break;
    }
    case PKEncodingType::PKCS8: {
      switch (config.format) {
        case PKFormatType::PEM: {
          // Encode PKCS#8 as PEM.
          err = PEM_write_bio_PKCS8PrivateKey(bio.get(),
                                              get(),
                                              config.cipher,
                                              passphrase.data,
                                              passphrase.len,
                                              nullptr,
                                              nullptr) != 1;
          break;
        }
        case PKFormatType::DER: {
          err = i2d_PKCS8PrivateKey_bio(bio.get(),
                                        get(),
                                        config.cipher,
                                        passphrase.data,
                                        passphrase.len,
                                        nullptr,
                                        nullptr) != 1;
          break;
        }
        default: {
          // Should never get here.
          return Result<BIOPointer, bool>(false);
        }
      }
      break;
    }
    case PKEncodingType::SEC1: {
      // SEC1 is only permitted for EC keys
      if (id() != EVP_PKEY_EC) return Result<BIOPointer, bool>(false);

#if OPENSSL_VERSION_MAJOR >= 3
      const EC_KEY* ec = EVP_PKEY_get0_EC_KEY(get());
#else
      EC_KEY* ec = EVP_PKEY_get0_EC_KEY(get());
#endif
      switch (config.format) {
        case PKFormatType::PEM: {
          err = PEM_write_bio_ECPrivateKey(
                    bio.get(),
                    ec,
                    config.cipher,
                    reinterpret_cast<unsigned char*>(passphrase.data),
                    passphrase.len,
                    nullptr,
                    nullptr) != 1;
          break;
        }
        case PKFormatType::DER: {
          // Encoding SEC1 as DER. This variation does not permit encryption.
          err = i2d_ECPrivateKey_bio(bio.get(), ec) != 1;
          break;
        }
        default: {
          // Should never get here.
          return Result<BIOPointer, bool>(false);
        }
      }
      break;
    }
    default: {
      // Not a valid private key encoding
      return Result<BIOPointer, bool>(false);
    }
  }

  if (err) {
    // Failed to encode the private key.
    return Result<BIOPointer, bool>(false,
                                    mark_pop_error_on_return.peekError());
  }

  return bio;
}

Result<BIOPointer, bool> EVPKeyPointer::writePublicKey(
    const ncrypto::EVPKeyPointer::PublicKeyEncodingConfig& config) const {
  auto bio = BIOPointer::NewMem();
  if (!bio) return Result<BIOPointer, bool>(false);

  MarkPopErrorOnReturn mark_pop_error_on_return;

  if (config.type == ncrypto::EVPKeyPointer::PKEncodingType::PKCS1) {
    // PKCS#1 is only valid for RSA keys.
#if OPENSSL_VERSION_MAJOR >= 3
    const RSA* rsa = EVP_PKEY_get0_RSA(get());
#else
    RSA* rsa = EVP_PKEY_get0_RSA(get());
#endif
    if (config.format == ncrypto::EVPKeyPointer::PKFormatType::PEM) {
      // Encode PKCS#1 as PEM.
      if (PEM_write_bio_RSAPublicKey(bio.get(), rsa) != 1) {
        return Result<BIOPointer, bool>(false,
                                        mark_pop_error_on_return.peekError());
      }
      return bio;
    }

    // Encode PKCS#1 as DER.
    if (i2d_RSAPublicKey_bio(bio.get(), rsa) != 1) {
      return Result<BIOPointer, bool>(false,
                                      mark_pop_error_on_return.peekError());
    }
    return bio;
  }

  if (config.format == ncrypto::EVPKeyPointer::PKFormatType::PEM) {
    // Encode SPKI as PEM.
    if (PEM_write_bio_PUBKEY(bio.get(), get()) != 1) {
      return Result<BIOPointer, bool>(false,
                                      mark_pop_error_on_return.peekError());
    }
    return bio;
  }

  // Encode SPKI as DER.
  if (i2d_PUBKEY_bio(bio.get(), get()) != 1) {
    return Result<BIOPointer, bool>(false,
                                    mark_pop_error_on_return.peekError());
  }
  return bio;
}

bool EVPKeyPointer::isRsaVariant() const {
  if (!pkey_) return false;
  int type = id();
  return type == EVP_PKEY_RSA || type == EVP_PKEY_RSA2 ||
         type == EVP_PKEY_RSA_PSS;
}

bool EVPKeyPointer::isOneShotVariant() const {
  if (!pkey_) return false;
  int type = id();
  return type == EVP_PKEY_ED25519 || type == EVP_PKEY_ED448;
}

bool EVPKeyPointer::isSigVariant() const {
  if (!pkey_) return false;
  int type = id();
  return type == EVP_PKEY_EC || type == EVP_PKEY_DSA;
}

int EVPKeyPointer::getDefaultSignPadding() const {
  return id() == EVP_PKEY_RSA_PSS ? RSA_PKCS1_PSS_PADDING : RSA_PKCS1_PADDING;
}

std::optional<uint32_t> EVPKeyPointer::getBytesOfRS() const {
  if (!pkey_) return std::nullopt;
  int bits, id = base_id();

  if (id == EVP_PKEY_DSA) {
    const DSA* dsa_key = EVP_PKEY_get0_DSA(get());
    // Both r and s are computed mod q, so their width is limited by that of q.
    bits = BignumPointer::GetBitCount(DSA_get0_q(dsa_key));
  } else if (id == EVP_PKEY_EC) {
    bits = EC_GROUP_order_bits(ECKeyPointer::GetGroup(*this));
  } else {
    return std::nullopt;
  }

  return (bits + 7) / 8;
}

EVPKeyPointer::operator Rsa() const {
  int type = id();
  if (type != EVP_PKEY_RSA && type != EVP_PKEY_RSA_PSS) return {};

  // TODO(tniessen): Remove the "else" branch once we drop support for OpenSSL
  // versions older than 1.1.1e via FIPS / dynamic linking.
  OSSL3_CONST RSA* rsa;
  if (OPENSSL_VERSION_NUMBER >= 0x1010105fL) {
    rsa = EVP_PKEY_get0_RSA(get());
  } else {
    rsa = static_cast<OSSL3_CONST RSA*>(EVP_PKEY_get0(get()));
  }
  if (rsa == nullptr) return {};
  return Rsa(rsa);
}

EVPKeyPointer::operator Dsa() const {
  int type = id();
  if (type != EVP_PKEY_DSA) return {};

  OSSL3_CONST DSA* dsa = EVP_PKEY_get0_DSA(get());
  if (dsa == nullptr) return {};
  return Dsa(dsa);
}

bool EVPKeyPointer::validateDsaParameters() const {
  if (!pkey_) return false;
    /* Validate DSA2 parameters from FIPS 186-4 */
#if OPENSSL_VERSION_MAJOR >= 3
  if (EVP_default_properties_is_fips_enabled(nullptr) && EVP_PKEY_DSA == id()) {
#else
  if (FIPS_mode() && EVP_PKEY_DSA == id()) {
#endif
    const DSA* dsa = EVP_PKEY_get0_DSA(pkey_.get());
    const BIGNUM* p;
    const BIGNUM* q;
    DSA_get0_pqg(dsa, &p, &q, nullptr);
    int L = BignumPointer::GetBitCount(p);
    int N = BignumPointer::GetBitCount(q);

    return (L == 1024 && N == 160) || (L == 2048 && N == 224) ||
           (L == 2048 && N == 256) || (L == 3072 && N == 256);
  }

  return true;
}

// ============================================================================

SSLPointer::SSLPointer(SSL* ssl) : ssl_(ssl) {}

SSLPointer::SSLPointer(SSLPointer&& other) noexcept : ssl_(other.release()) {}

SSLPointer& SSLPointer::operator=(SSLPointer&& other) noexcept {
  if (this == &other) return *this;
  this->~SSLPointer();
  return *new (this) SSLPointer(std::move(other));
}

SSLPointer::~SSLPointer() {
  reset();
}

void SSLPointer::reset(SSL* ssl) {
  ssl_.reset(ssl);
}

SSL* SSLPointer::release() {
  return ssl_.release();
}

SSLPointer SSLPointer::New(const SSLCtxPointer& ctx) {
  if (!ctx) return {};
  return SSLPointer(SSL_new(ctx.get()));
}

void SSLPointer::getCiphers(
    std::function<void(const std::string_view)> cb) const {
  if (!ssl_) return;
  STACK_OF(SSL_CIPHER)* ciphers = SSL_get_ciphers(get());

  // TLSv1.3 ciphers aren't listed by EVP. There are only 5, we could just
  // document them, but since there are only 5, easier to just add them manually
  // and not have to explain their absence in the API docs. They are lower-cased
  // because the docs say they will be.
  static constexpr const char* TLS13_CIPHERS[] = {
      "tls_aes_256_gcm_sha384",
      "tls_chacha20_poly1305_sha256",
      "tls_aes_128_gcm_sha256",
      "tls_aes_128_ccm_8_sha256",
      "tls_aes_128_ccm_sha256"};

  const int n = sk_SSL_CIPHER_num(ciphers);

  for (int i = 0; i < n; ++i) {
    const SSL_CIPHER* cipher = sk_SSL_CIPHER_value(ciphers, i);
    cb(SSL_CIPHER_get_name(cipher));
  }

  for (unsigned i = 0; i < 5; ++i) {
    cb(TLS13_CIPHERS[i]);
  }
}

bool SSLPointer::setSession(const SSLSessionPointer& session) {
  if (!session || !ssl_) return false;
  return SSL_set_session(get(), session.get()) == 1;
}

bool SSLPointer::setSniContext(const SSLCtxPointer& ctx) const {
  if (!ctx) return false;
  auto x509 = ncrypto::X509View::From(ctx);
  if (!x509) return false;
  EVP_PKEY* pkey = SSL_CTX_get0_privatekey(ctx.get());
  STACK_OF(X509) * chain;
  int err = SSL_CTX_get0_chain_certs(ctx.get(), &chain);
  if (err == 1) err = SSL_use_certificate(get(), x509);
  if (err == 1) err = SSL_use_PrivateKey(get(), pkey);
  if (err == 1 && chain != nullptr) err = SSL_set1_chain(get(), chain);
  return err == 1;
}

std::optional<uint32_t> SSLPointer::verifyPeerCertificate() const {
  if (!ssl_) return std::nullopt;
  if (X509Pointer::PeerFrom(*this)) {
    return SSL_get_verify_result(get());
  }

  const SSL_CIPHER* curr_cipher = SSL_get_current_cipher(get());
  const SSL_SESSION* sess = SSL_get_session(get());
  // Allow no-cert for PSK authentication in TLS1.2 and lower.
  // In TLS1.3 check that session was reused because TLS1.3 PSK
  // looks like session resumption.
  if (SSL_CIPHER_get_auth_nid(curr_cipher) == NID_auth_psk ||
      (SSL_SESSION_get_protocol_version(sess) == TLS1_3_VERSION &&
       SSL_session_reused(get()))) {
    return X509_V_OK;
  }

  return std::nullopt;
}

const std::string_view SSLPointer::getClientHelloAlpn() const {
  if (ssl_ == nullptr) return {};
#ifndef OPENSSL_IS_BORINGSSL
  const unsigned char* buf;
  size_t len;
  size_t rem;

  if (!SSL_client_hello_get0_ext(
          get(),
          TLSEXT_TYPE_application_layer_protocol_negotiation,
          &buf,
          &rem) ||
      rem < 2) {
    return {};
  }

  len = (buf[0] << 8) | buf[1];
  if (len + 2 != rem) return {};
  return reinterpret_cast<const char*>(buf + 3);
#else
  // Boringssl doesn't have a public API for this.
  return {};
#endif
}

const std::string_view SSLPointer::getClientHelloServerName() const {
  if (ssl_ == nullptr) return {};
#ifndef OPENSSL_IS_BORINGSSL
  const unsigned char* buf;
  size_t len;
  size_t rem;

  if (!SSL_client_hello_get0_ext(get(), TLSEXT_TYPE_server_name, &buf, &rem) ||
      rem <= 2) {
    return {};
  }

  len = (*buf << 8) | *(buf + 1);
  if (len + 2 != rem) return {};
  rem = len;

  if (rem == 0 || *(buf + 2) != TLSEXT_NAMETYPE_host_name) return {};
  rem--;
  if (rem <= 2) return {};
  len = (*(buf + 3) << 8) | *(buf + 4);
  if (len + 2 > rem) return {};
  return reinterpret_cast<const char*>(buf + 5);
#else
  // Boringssl doesn't have a public API for this.
  return {};
#endif
}

std::optional<const std::string_view> SSLPointer::GetServerName(
    const SSL* ssl) {
  if (ssl == nullptr) return std::nullopt;
  auto res = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  if (res == nullptr) return std::nullopt;
  return res;
}

std::optional<const std::string_view> SSLPointer::getServerName() const {
  if (!ssl_) return std::nullopt;
  return GetServerName(get());
}

X509View SSLPointer::getCertificate() const {
  if (!ssl_) return {};
  ClearErrorOnReturn clear_error_on_return;
  return ncrypto::X509View(SSL_get_certificate(get()));
}

const SSL_CIPHER* SSLPointer::getCipher() const {
  if (!ssl_) return nullptr;
  return SSL_get_current_cipher(get());
}

bool SSLPointer::isServer() const {
  return SSL_is_server(get()) != 0;
}

EVPKeyPointer SSLPointer::getPeerTempKey() const {
  if (!ssl_) return {};
  EVP_PKEY* raw_key = nullptr;
#ifndef OPENSSL_IS_BORINGSSL
  if (!SSL_get_peer_tmp_key(get(), &raw_key)) return {};
#else
  if (!SSL_get_server_tmp_key(get(), &raw_key)) return {};
#endif
  return EVPKeyPointer(raw_key);
}

std::optional<std::string_view> SSLPointer::getCipherName() const {
  auto cipher = getCipher();
  if (cipher == nullptr) return std::nullopt;
  return SSL_CIPHER_get_name(cipher);
}

std::optional<std::string_view> SSLPointer::getCipherStandardName() const {
  auto cipher = getCipher();
  if (cipher == nullptr) return std::nullopt;
  return SSL_CIPHER_standard_name(cipher);
}

std::optional<std::string_view> SSLPointer::getCipherVersion() const {
  auto cipher = getCipher();
  if (cipher == nullptr) return std::nullopt;
  return SSL_CIPHER_get_version(cipher);
}

SSLCtxPointer::SSLCtxPointer(SSL_CTX* ctx) : ctx_(ctx) {}

SSLCtxPointer::SSLCtxPointer(SSLCtxPointer&& other) noexcept
    : ctx_(other.release()) {}

SSLCtxPointer& SSLCtxPointer::operator=(SSLCtxPointer&& other) noexcept {
  if (this == &other) return *this;
  this->~SSLCtxPointer();
  return *new (this) SSLCtxPointer(std::move(other));
}

SSLCtxPointer::~SSLCtxPointer() {
  reset();
}

void SSLCtxPointer::reset(SSL_CTX* ctx) {
  ctx_.reset(ctx);
}

void SSLCtxPointer::reset(const SSL_METHOD* method) {
  ctx_.reset(SSL_CTX_new(method));
}

SSL_CTX* SSLCtxPointer::release() {
  return ctx_.release();
}

SSLCtxPointer SSLCtxPointer::NewServer() {
  return SSLCtxPointer(SSL_CTX_new(TLS_server_method()));
}

SSLCtxPointer SSLCtxPointer::NewClient() {
  return SSLCtxPointer(SSL_CTX_new(TLS_client_method()));
}

SSLCtxPointer SSLCtxPointer::New(const SSL_METHOD* method) {
  return SSLCtxPointer(SSL_CTX_new(method));
}

bool SSLCtxPointer::setGroups(const char* groups) {
  return SSL_CTX_set1_groups_list(get(), groups) == 1;
}

// ============================================================================

const Cipher Cipher::FromName(std::string_view name) {
  return Cipher(EVP_get_cipherbyname(name.data()));
}

const Cipher Cipher::FromNid(int nid) {
  return Cipher(EVP_get_cipherbynid(nid));
}

const Cipher Cipher::FromCtx(const CipherCtxPointer& ctx) {
  return Cipher(EVP_CIPHER_CTX_cipher(ctx.get()));
}

int Cipher::getMode() const {
  if (!cipher_) return 0;
  return EVP_CIPHER_mode(cipher_);
}

int Cipher::getIvLength() const {
  if (!cipher_) return 0;
  return EVP_CIPHER_iv_length(cipher_);
}

int Cipher::getKeyLength() const {
  if (!cipher_) return 0;
  return EVP_CIPHER_key_length(cipher_);
}

int Cipher::getBlockSize() const {
  if (!cipher_) return 0;
  return EVP_CIPHER_block_size(cipher_);
}

int Cipher::getNid() const {
  if (!cipher_) return 0;
  return EVP_CIPHER_nid(cipher_);
}

std::string_view Cipher::getModeLabel() const {
  if (!cipher_) return {};
  switch (getMode()) {
    case EVP_CIPH_CCM_MODE:
      return "ccm";
    case EVP_CIPH_CFB_MODE:
      return "cfb";
    case EVP_CIPH_CBC_MODE:
      return "cbc";
    case EVP_CIPH_CTR_MODE:
      return "ctr";
    case EVP_CIPH_ECB_MODE:
      return "ecb";
    case EVP_CIPH_GCM_MODE:
      return "gcm";
    case EVP_CIPH_OCB_MODE:
      return "ocb";
    case EVP_CIPH_OFB_MODE:
      return "ofb";
    case EVP_CIPH_WRAP_MODE:
      return "wrap";
    case EVP_CIPH_XTS_MODE:
      return "xts";
    case EVP_CIPH_STREAM_CIPHER:
      return "stream";
  }
  return "{unknown}";
}

std::string_view Cipher::getName() const {
  if (!cipher_) return {};
  // OBJ_nid2sn(EVP_CIPHER_nid(cipher)) is used here instead of
  // EVP_CIPHER_name(cipher) for compatibility with BoringSSL.
  return OBJ_nid2sn(getNid());
}

bool Cipher::isSupportedAuthenticatedMode() const {
  switch (getMode()) {
    case EVP_CIPH_CCM_MODE:
    case EVP_CIPH_GCM_MODE:
#ifndef OPENSSL_NO_OCB
    case EVP_CIPH_OCB_MODE:
#endif
      return true;
    case EVP_CIPH_STREAM_CIPHER:
      return getNid() == NID_chacha20_poly1305;
    default:
      return false;
  }
}

// ============================================================================

CipherCtxPointer CipherCtxPointer::New() {
  auto ret = CipherCtxPointer(EVP_CIPHER_CTX_new());
  if (!ret) return {};
  EVP_CIPHER_CTX_init(ret.get());
  return ret;
}

CipherCtxPointer::CipherCtxPointer(EVP_CIPHER_CTX* ctx) : ctx_(ctx) {}

CipherCtxPointer::CipherCtxPointer(CipherCtxPointer&& other) noexcept
    : ctx_(other.release()) {}

CipherCtxPointer& CipherCtxPointer::operator=(
    CipherCtxPointer&& other) noexcept {
  if (this == &other) return *this;
  this->~CipherCtxPointer();
  return *new (this) CipherCtxPointer(std::move(other));
}

CipherCtxPointer::~CipherCtxPointer() {
  reset();
}

void CipherCtxPointer::reset(EVP_CIPHER_CTX* ctx) {
  ctx_.reset(ctx);
}

EVP_CIPHER_CTX* CipherCtxPointer::release() {
  return ctx_.release();
}

void CipherCtxPointer::setFlags(int flags) {
  if (!ctx_) return;
  EVP_CIPHER_CTX_set_flags(ctx_.get(), flags);
}

bool CipherCtxPointer::setKeyLength(size_t length) {
  if (!ctx_) return false;
  return EVP_CIPHER_CTX_set_key_length(ctx_.get(), length);
}

bool CipherCtxPointer::setIvLength(size_t length) {
  if (!ctx_) return false;
  return EVP_CIPHER_CTX_ctrl(
      ctx_.get(), EVP_CTRL_AEAD_SET_IVLEN, length, nullptr);
}

bool CipherCtxPointer::setAeadTag(const Buffer<const char>& tag) {
  if (!ctx_) return false;
  return EVP_CIPHER_CTX_ctrl(
      ctx_.get(), EVP_CTRL_AEAD_SET_TAG, tag.len, const_cast<char*>(tag.data));
}

bool CipherCtxPointer::setAeadTagLength(size_t length) {
  if (!ctx_) return false;
  return EVP_CIPHER_CTX_ctrl(
      ctx_.get(), EVP_CTRL_AEAD_SET_TAG, length, nullptr);
}

bool CipherCtxPointer::setPadding(bool padding) {
  if (!ctx_) return false;
  return EVP_CIPHER_CTX_set_padding(ctx_.get(), padding);
}

int CipherCtxPointer::getBlockSize() const {
  if (!ctx_) return 0;
  return EVP_CIPHER_CTX_block_size(ctx_.get());
}

int CipherCtxPointer::getMode() const {
  if (!ctx_) return 0;
  return EVP_CIPHER_CTX_mode(ctx_.get());
}

int CipherCtxPointer::getNid() const {
  if (!ctx_) return 0;
  return EVP_CIPHER_CTX_nid(ctx_.get());
}

bool CipherCtxPointer::init(const Cipher& cipher,
                            bool encrypt,
                            const unsigned char* key,
                            const unsigned char* iv) {
  if (!ctx_) return false;
  return EVP_CipherInit_ex(
             ctx_.get(), cipher, nullptr, key, iv, encrypt ? 1 : 0) == 1;
}

bool CipherCtxPointer::update(const Buffer<const unsigned char>& in,
                              unsigned char* out,
                              int* out_len,
                              bool finalize) {
  if (!ctx_) return false;
  if (!finalize) {
    return EVP_CipherUpdate(ctx_.get(), out, out_len, in.data, in.len) == 1;
  }
  return EVP_CipherFinal_ex(ctx_.get(), out, out_len) == 1;
}

bool CipherCtxPointer::getAeadTag(size_t len, unsigned char* out) {
  if (!ctx_) return false;
  return EVP_CIPHER_CTX_ctrl(ctx_.get(), EVP_CTRL_AEAD_GET_TAG, len, out);
}

// ============================================================================

ECDSASigPointer::ECDSASigPointer() : sig_(nullptr) {}
ECDSASigPointer::ECDSASigPointer(ECDSA_SIG* sig) : sig_(sig) {
  if (sig_) {
    ECDSA_SIG_get0(sig_.get(), &pr_, &ps_);
  }
}
ECDSASigPointer::ECDSASigPointer(ECDSASigPointer&& other) noexcept
    : sig_(other.release()) {
  if (sig_) {
    ECDSA_SIG_get0(sig_.get(), &pr_, &ps_);
  }
}

ECDSASigPointer& ECDSASigPointer::operator=(ECDSASigPointer&& other) noexcept {
  sig_.reset(other.release());
  if (sig_) {
    ECDSA_SIG_get0(sig_.get(), &pr_, &ps_);
  }
  return *this;
}

ECDSASigPointer::~ECDSASigPointer() {
  reset();
}

void ECDSASigPointer::reset(ECDSA_SIG* sig) {
  sig_.reset();
  pr_ = nullptr;
  ps_ = nullptr;
}

ECDSA_SIG* ECDSASigPointer::release() {
  pr_ = nullptr;
  ps_ = nullptr;
  return sig_.release();
}

ECDSASigPointer ECDSASigPointer::New() {
  return ECDSASigPointer(ECDSA_SIG_new());
}

ECDSASigPointer ECDSASigPointer::Parse(const Buffer<const unsigned char>& sig) {
  const unsigned char* ptr = sig.data;
  return ECDSASigPointer(d2i_ECDSA_SIG(nullptr, &ptr, sig.len));
}

bool ECDSASigPointer::setParams(BignumPointer&& r, BignumPointer&& s) {
  if (!sig_) return false;
  return ECDSA_SIG_set0(sig_.get(), r.release(), s.release());
}

Buffer<unsigned char> ECDSASigPointer::encode() const {
  if (!sig_)
    return {
        .data = nullptr,
        .len = 0,
    };
  Buffer<unsigned char> buf;
  buf.len = i2d_ECDSA_SIG(sig_.get(), &buf.data);
  return buf;
}

// ============================================================================

ECGroupPointer::ECGroupPointer() : group_(nullptr) {}

ECGroupPointer::ECGroupPointer(EC_GROUP* group) : group_(group) {}

ECGroupPointer::ECGroupPointer(ECGroupPointer&& other) noexcept
    : group_(other.release()) {}

ECGroupPointer& ECGroupPointer::operator=(ECGroupPointer&& other) noexcept {
  group_.reset(other.release());
  return *this;
}

ECGroupPointer::~ECGroupPointer() {
  reset();
}

void ECGroupPointer::reset(EC_GROUP* group) {
  group_.reset();
}

EC_GROUP* ECGroupPointer::release() {
  return group_.release();
}

ECGroupPointer ECGroupPointer::NewByCurveName(int nid) {
  return ECGroupPointer(EC_GROUP_new_by_curve_name(nid));
}

// ============================================================================

ECPointPointer::ECPointPointer() : point_(nullptr) {}

ECPointPointer::ECPointPointer(EC_POINT* point) : point_(point) {}

ECPointPointer::ECPointPointer(ECPointPointer&& other) noexcept
    : point_(other.release()) {}

ECPointPointer& ECPointPointer::operator=(ECPointPointer&& other) noexcept {
  point_.reset(other.release());
  return *this;
}

ECPointPointer::~ECPointPointer() {
  reset();
}

void ECPointPointer::reset(EC_POINT* point) {
  point_.reset(point);
}

EC_POINT* ECPointPointer::release() {
  return point_.release();
}

ECPointPointer ECPointPointer::New(const EC_GROUP* group) {
  return ECPointPointer(EC_POINT_new(group));
}

bool ECPointPointer::setFromBuffer(const Buffer<const unsigned char>& buffer,
                                   const EC_GROUP* group) {
  if (!point_) return false;
  return EC_POINT_oct2point(
      group, point_.get(), buffer.data, buffer.len, nullptr);
}

bool ECPointPointer::mul(const EC_GROUP* group, const BIGNUM* priv_key) {
  if (!point_) return false;
  return EC_POINT_mul(group, point_.get(), priv_key, nullptr, nullptr, nullptr);
}

// ============================================================================

ECKeyPointer::ECKeyPointer() : key_(nullptr) {}

ECKeyPointer::ECKeyPointer(EC_KEY* key) : key_(key) {}

ECKeyPointer::ECKeyPointer(ECKeyPointer&& other) noexcept
    : key_(other.release()) {}

ECKeyPointer& ECKeyPointer::operator=(ECKeyPointer&& other) noexcept {
  key_.reset(other.release());
  return *this;
}

ECKeyPointer::~ECKeyPointer() {
  reset();
}

void ECKeyPointer::reset(EC_KEY* key) {
  key_.reset(key);
}

EC_KEY* ECKeyPointer::release() {
  return key_.release();
}

ECKeyPointer ECKeyPointer::clone() const {
  if (!key_) return {};
  return ECKeyPointer(EC_KEY_dup(key_.get()));
}

bool ECKeyPointer::generate() {
  if (!key_) return false;
  return EC_KEY_generate_key(key_.get());
}

bool ECKeyPointer::setPublicKey(const ECPointPointer& pub) {
  if (!key_) return false;
  return EC_KEY_set_public_key(key_.get(), pub.get()) == 1;
}

bool ECKeyPointer::setPublicKeyRaw(const BignumPointer& x,
                                   const BignumPointer& y) {
  if (!key_) return false;
  return EC_KEY_set_public_key_affine_coordinates(
             key_.get(), x.get(), y.get()) == 1;
}

bool ECKeyPointer::setPrivateKey(const BignumPointer& priv) {
  if (!key_) return false;
  return EC_KEY_set_private_key(key_.get(), priv.get()) == 1;
}

const BIGNUM* ECKeyPointer::getPrivateKey() const {
  if (!key_) return nullptr;
  return GetPrivateKey(key_.get());
}

const BIGNUM* ECKeyPointer::GetPrivateKey(const EC_KEY* key) {
  return EC_KEY_get0_private_key(key);
}

const EC_POINT* ECKeyPointer::getPublicKey() const {
  if (!key_) return nullptr;
  return GetPublicKey(key_.get());
}

const EC_POINT* ECKeyPointer::GetPublicKey(const EC_KEY* key) {
  return EC_KEY_get0_public_key(key);
}

const EC_GROUP* ECKeyPointer::getGroup() const {
  if (!key_) return nullptr;
  return GetGroup(key_.get());
}

const EC_GROUP* ECKeyPointer::GetGroup(const EC_KEY* key) {
  return EC_KEY_get0_group(key);
}

int ECKeyPointer::GetGroupName(const EC_KEY* key) {
  const EC_GROUP* group = GetGroup(key);
  return group ? EC_GROUP_get_curve_name(group) : 0;
}

bool ECKeyPointer::Check(const EC_KEY* key) {
  return EC_KEY_check_key(key) == 1;
}

bool ECKeyPointer::checkKey() const {
  if (!key_) return false;
  return Check(key_.get());
}

ECKeyPointer ECKeyPointer::NewByCurveName(int nid) {
  return ECKeyPointer(EC_KEY_new_by_curve_name(nid));
}

ECKeyPointer ECKeyPointer::New(const EC_GROUP* group) {
  auto ptr = ECKeyPointer(EC_KEY_new());
  if (!ptr) return {};
  if (!EC_KEY_set_group(ptr.get(), group)) return {};
  return ptr;
}

// ============================================================================

EVPKeyCtxPointer::EVPKeyCtxPointer() : ctx_(nullptr) {}

EVPKeyCtxPointer::EVPKeyCtxPointer(EVP_PKEY_CTX* ctx) : ctx_(ctx) {}

EVPKeyCtxPointer::EVPKeyCtxPointer(EVPKeyCtxPointer&& other) noexcept
    : ctx_(other.release()) {}

EVPKeyCtxPointer& EVPKeyCtxPointer::operator=(
    EVPKeyCtxPointer&& other) noexcept {
  ctx_.reset(other.release());
  return *this;
}

EVPKeyCtxPointer::~EVPKeyCtxPointer() {
  reset();
}

void EVPKeyCtxPointer::reset(EVP_PKEY_CTX* ctx) {
  ctx_.reset(ctx);
}

EVP_PKEY_CTX* EVPKeyCtxPointer::release() {
  return ctx_.release();
}

EVPKeyCtxPointer EVPKeyCtxPointer::New(const EVPKeyPointer& key) {
  if (!key) return {};
  return EVPKeyCtxPointer(EVP_PKEY_CTX_new(key.get(), nullptr));
}

EVPKeyCtxPointer EVPKeyCtxPointer::NewFromID(int id) {
  return EVPKeyCtxPointer(EVP_PKEY_CTX_new_id(id, nullptr));
}

bool EVPKeyCtxPointer::initForDerive(const EVPKeyPointer& peer) {
  if (!ctx_) return false;
  if (EVP_PKEY_derive_init(ctx_.get()) != 1) return false;
  return EVP_PKEY_derive_set_peer(ctx_.get(), peer.get()) == 1;
}

bool EVPKeyCtxPointer::initForKeygen() {
  if (!ctx_) return false;
  return EVP_PKEY_keygen_init(ctx_.get()) == 1;
}

bool EVPKeyCtxPointer::initForParamgen() {
  if (!ctx_) return false;
  return EVP_PKEY_paramgen_init(ctx_.get()) == 1;
}

int EVPKeyCtxPointer::initForVerify() {
  if (!ctx_) return 0;
  return EVP_PKEY_verify_init(ctx_.get());
}

int EVPKeyCtxPointer::initForSign() {
  if (!ctx_) return 0;
  return EVP_PKEY_sign_init(ctx_.get());
}

bool EVPKeyCtxPointer::setDhParameters(int prime_size, uint32_t generator) {
#ifndef OPENSSL_IS_BORINGSSL
  if (!ctx_) return false;
  return EVP_PKEY_CTX_set_dh_paramgen_prime_len(ctx_.get(), prime_size) == 1 &&
         EVP_PKEY_CTX_set_dh_paramgen_generator(ctx_.get(), generator) == 1;
#else
  // TODO(jasnell): Boringssl appears not to support this operation.
  // Is there an alternative approach that Boringssl does support?
  return false;
#endif
}

bool EVPKeyCtxPointer::setDsaParameters(uint32_t bits,
                                        std::optional<int> q_bits) {
  if (!ctx_) return false;
  if (EVP_PKEY_CTX_set_dsa_paramgen_bits(ctx_.get(), bits) != 1) {
    return false;
  }
  if (q_bits.has_value() &&
      EVP_PKEY_CTX_set_dsa_paramgen_q_bits(ctx_.get(), q_bits.value()) != 1) {
    return false;
  }
  return true;
}

bool EVPKeyCtxPointer::setEcParameters(int curve, int encoding) {
  if (!ctx_) return false;
  return EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx_.get(), curve) == 1 &&
         EVP_PKEY_CTX_set_ec_param_enc(ctx_.get(), encoding) == 1;
}

bool EVPKeyCtxPointer::setRsaOaepMd(const EVP_MD* md) {
  if (md == nullptr || !ctx_) return false;
  return EVP_PKEY_CTX_set_rsa_oaep_md(ctx_.get(), md) > 0;
}

bool EVPKeyCtxPointer::setRsaMgf1Md(const EVP_MD* md) {
  if (md == nullptr || !ctx_) return false;
  return EVP_PKEY_CTX_set_rsa_mgf1_md(ctx_.get(), md) > 0;
}

bool EVPKeyCtxPointer::setRsaPadding(int padding) {
  return setRsaPadding(ctx_.get(), padding, std::nullopt);
}

bool EVPKeyCtxPointer::setRsaPadding(EVP_PKEY_CTX* ctx,
                                     int padding,
                                     std::optional<int> salt_len) {
  if (ctx == nullptr) return false;
  if (EVP_PKEY_CTX_set_rsa_padding(ctx, padding) <= 0) {
    return false;
  }
  if (padding == RSA_PKCS1_PSS_PADDING && salt_len.has_value()) {
    return EVP_PKEY_CTX_set_rsa_pss_saltlen(ctx, salt_len.value()) > 0;
  }
  return true;
}

bool EVPKeyCtxPointer::setRsaKeygenBits(int bits) {
  if (!ctx_) return false;
  return EVP_PKEY_CTX_set_rsa_keygen_bits(ctx_.get(), bits) == 1;
}

bool EVPKeyCtxPointer::setRsaKeygenPubExp(BignumPointer&& e) {
  if (!ctx_) return false;
  if (EVP_PKEY_CTX_set_rsa_keygen_pubexp(ctx_.get(), e.get()) == 1) {
    // The ctx_ takes ownership of e on success.
    e.release();
    return true;
  }
  return false;
}

bool EVPKeyCtxPointer::setRsaPssKeygenMd(const EVP_MD* md) {
  if (md == nullptr || !ctx_) return false;
  return EVP_PKEY_CTX_set_rsa_pss_keygen_md(ctx_.get(), md) > 0;
}

bool EVPKeyCtxPointer::setRsaPssKeygenMgf1Md(const EVP_MD* md) {
  if (md == nullptr || !ctx_) return false;
  return EVP_PKEY_CTX_set_rsa_pss_keygen_mgf1_md(ctx_.get(), md) > 0;
}

bool EVPKeyCtxPointer::setRsaPssSaltlen(int salt_len) {
  if (!ctx_) return false;
  return EVP_PKEY_CTX_set_rsa_pss_keygen_saltlen(ctx_.get(), salt_len) > 0;
}

bool EVPKeyCtxPointer::setRsaImplicitRejection() {
#ifndef OPENSSL_IS_BORINGSSL
  if (!ctx_) return false;
  return EVP_PKEY_CTX_ctrl_str(
             ctx_.get(), "rsa_pkcs1_implicit_rejection", "1") > 0;
  // From the doc -2 means that the option is not supported.
  // The default for the option is enabled and if it has been
  // specifically disabled we want to respect that so we will
  // not throw an error if the option is supported regardless
  // of how it is set. The call to set the value
  // will not affect what is used since a different context is
  // used in the call if the option is supported
#else
  // TODO(jasnell): Boringssl appears not to support this operation.
  // Is there an alternative approach that Boringssl does support?
  return true;
#endif
}

bool EVPKeyCtxPointer::setRsaOaepLabel(DataPointer&& data) {
  if (!ctx_) return false;
  if (EVP_PKEY_CTX_set0_rsa_oaep_label(ctx_.get(),
                                       static_cast<unsigned char*>(data.get()),
                                       data.size()) > 0) {
    // The ctx_ takes ownership of data on success.
    data.release();
    return true;
  }
  return false;
}

bool EVPKeyCtxPointer::setSignatureMd(const EVPMDCtxPointer& md) {
  if (!ctx_) return false;
  return EVP_PKEY_CTX_set_signature_md(ctx_.get(), EVP_MD_CTX_md(md.get())) ==
         1;
}

bool EVPKeyCtxPointer::initForEncrypt() {
  if (!ctx_) return false;
  return EVP_PKEY_encrypt_init(ctx_.get()) == 1;
}

bool EVPKeyCtxPointer::initForDecrypt() {
  if (!ctx_) return false;
  return EVP_PKEY_decrypt_init(ctx_.get()) == 1;
}

DataPointer EVPKeyCtxPointer::derive() const {
  if (!ctx_) return {};
  size_t len = 0;
  if (EVP_PKEY_derive(ctx_.get(), nullptr, &len) != 1) return {};
  auto data = DataPointer::Alloc(len);
  if (!data) return {};
  if (EVP_PKEY_derive(
          ctx_.get(), static_cast<unsigned char*>(data.get()), &len) != 1) {
    return {};
  }
  return data;
}

EVPKeyPointer EVPKeyCtxPointer::paramgen() const {
  if (!ctx_) return {};
  EVP_PKEY* key = nullptr;
  if (EVP_PKEY_paramgen(ctx_.get(), &key) != 1) return {};
  return EVPKeyPointer(key);
}

bool EVPKeyCtxPointer::publicCheck() const {
  if (!ctx_) return false;
#ifndef OPENSSL_IS_BORINGSSL
  return EVP_PKEY_public_check(ctx_.get()) == 1;
#if OPENSSL_VERSION_MAJOR >= 3
  return EVP_PKEY_public_check_quick(ctx_.get()) == 1;
#else
  return EVP_PKEY_public_check(ctx_.get()) == 1;
#endif
#else  // OPENSSL_IS_BORINGSSL
  // Boringssl appears not to support this operation.
  // TODO(jasnell): Is there an alternative approach that Boringssl does
  // support?
  return true;
#endif
}

bool EVPKeyCtxPointer::privateCheck() const {
  if (!ctx_) return false;
#ifndef OPENSSL_IS_BORINGSSL
  return EVP_PKEY_check(ctx_.get()) == 1;
#else
  // Boringssl appears not to support this operation.
  // TODO(jasnell): Is there an alternative approach that Boringssl does
  // support?
  return true;
#endif
}

bool EVPKeyCtxPointer::verify(const Buffer<const unsigned char>& sig,
                              const Buffer<const unsigned char>& data) {
  if (!ctx_) return false;
  return EVP_PKEY_verify(ctx_.get(), sig.data, sig.len, data.data, data.len) ==
         1;
}

DataPointer EVPKeyCtxPointer::sign(const Buffer<const unsigned char>& data) {
  if (!ctx_) return {};
  size_t len = 0;
  if (EVP_PKEY_sign(ctx_.get(), nullptr, &len, data.data, data.len) != 1) {
    return {};
  }
  auto buf = DataPointer::Alloc(len);
  if (!buf) return {};
  if (EVP_PKEY_sign(ctx_.get(),
                    static_cast<unsigned char*>(buf.get()),
                    &len,
                    data.data,
                    data.len) != 1) {
    return {};
  }
  return buf.resize(len);
}

bool EVPKeyCtxPointer::signInto(const Buffer<const unsigned char>& data,
                                Buffer<unsigned char>* sig) {
  if (!ctx_) return false;
  size_t len = sig->len;
  if (EVP_PKEY_sign(ctx_.get(), sig->data, &len, data.data, data.len) != 1) {
    return false;
  }
  sig->len = len;
  return true;
}

// ============================================================================

namespace {

using EVP_PKEY_cipher_init_t = int(EVP_PKEY_CTX* ctx);
using EVP_PKEY_cipher_t = int(EVP_PKEY_CTX* ctx,
                              unsigned char* out,
                              size_t* outlen,
                              const unsigned char* in,
                              size_t inlen);

template <EVP_PKEY_cipher_init_t init, EVP_PKEY_cipher_t cipher>
DataPointer RSA_Cipher(const EVPKeyPointer& key,
                       const Rsa::CipherParams& params,
                       const Buffer<const void> in) {
  if (!key) return {};
  EVPKeyCtxPointer ctx = key.newCtx();

  if (!ctx || init(ctx.get()) <= 0 || !ctx.setRsaPadding(params.padding) ||
      (params.digest != nullptr && (!ctx.setRsaOaepMd(params.digest) ||
                                    !ctx.setRsaMgf1Md(params.digest)))) {
    return {};
  }

  if (params.label.len != 0 && params.label.data != nullptr &&
      !ctx.setRsaOaepLabel(DataPointer::Copy(params.label))) {
    return {};
  }

  size_t out_len = 0;
  if (cipher(ctx.get(),
             nullptr,
             &out_len,
             reinterpret_cast<const unsigned char*>(in.data),
             in.len) <= 0) {
    return {};
  }

  auto buf = DataPointer::Alloc(out_len);
  if (!buf) return {};

  if (cipher(ctx.get(),
             static_cast<unsigned char*>(buf.get()),
             &out_len,
             static_cast<const unsigned char*>(in.data),
             in.len) <= 0) {
    return {};
  }

  return buf.resize(out_len);
}

template <EVP_PKEY_cipher_init_t init, EVP_PKEY_cipher_t cipher>
DataPointer CipherImpl(const EVPKeyPointer& key,
                       const Rsa::CipherParams& params,
                       const Buffer<const void> in) {
  if (!key) return {};
  EVPKeyCtxPointer ctx = key.newCtx();
  if (!ctx || init(ctx.get()) <= 0 || !ctx.setRsaPadding(params.padding) ||
      (params.digest != nullptr && !ctx.setRsaOaepMd(params.digest))) {
    return {};
  }

  if (params.label.len != 0 && params.label.data != nullptr &&
      !ctx.setRsaOaepLabel(DataPointer::Copy(params.label))) {
    return {};
  }

  size_t out_len = 0;
  if (cipher(ctx.get(),
             nullptr,
             &out_len,
             static_cast<const unsigned char*>(in.data),
             in.len) <= 0) {
    return {};
  }

  auto buf = DataPointer::Alloc(out_len);
  if (!buf) return {};

  if (cipher(ctx.get(),
             static_cast<unsigned char*>(buf.get()),
             &out_len,
             static_cast<const unsigned char*>(in.data),
             in.len) <= 0) {
    return {};
  }

  return buf.resize(out_len);
}
}  // namespace

Rsa::Rsa() : rsa_(nullptr) {}

Rsa::Rsa(OSSL3_CONST RSA* ptr) : rsa_(ptr) {}

const Rsa::PublicKey Rsa::getPublicKey() const {
  if (rsa_ == nullptr) return {};
  PublicKey key;
  RSA_get0_key(rsa_, &key.n, &key.e, &key.d);
  return key;
}

const Rsa::PrivateKey Rsa::getPrivateKey() const {
  if (rsa_ == nullptr) return {};
  PrivateKey key;
  RSA_get0_factors(rsa_, &key.p, &key.q);
  RSA_get0_crt_params(rsa_, &key.dp, &key.dq, &key.qi);
  return key;
}

const std::optional<Rsa::PssParams> Rsa::getPssParams() const {
  if (rsa_ == nullptr) return std::nullopt;
  const RSA_PSS_PARAMS* params = RSA_get0_pss_params(rsa_);
  if (params == nullptr) return std::nullopt;
  Rsa::PssParams ret{
      .digest = OBJ_nid2ln(NID_sha1),
      .mgf1_digest = OBJ_nid2ln(NID_sha1),
      .salt_length = 20,
  };

  if (params->hashAlgorithm != nullptr) {
    const ASN1_OBJECT* hash_obj;
    X509_ALGOR_get0(&hash_obj, nullptr, nullptr, params->hashAlgorithm);
    ret.digest = OBJ_nid2ln(OBJ_obj2nid(hash_obj));
  }

  if (params->maskGenAlgorithm != nullptr) {
    const ASN1_OBJECT* mgf_obj;
    X509_ALGOR_get0(&mgf_obj, nullptr, nullptr, params->maskGenAlgorithm);
    int mgf_nid = OBJ_obj2nid(mgf_obj);
    if (mgf_nid == NID_mgf1) {
      const ASN1_OBJECT* mgf1_hash_obj;
      X509_ALGOR_get0(&mgf1_hash_obj, nullptr, nullptr, params->maskHash);
      ret.mgf1_digest = OBJ_nid2ln(OBJ_obj2nid(mgf1_hash_obj));
    }
  }

  if (params->saltLength != nullptr) {
    if (ASN1_INTEGER_get_int64(&ret.salt_length, params->saltLength) != 1) {
      return std::nullopt;
    }
  }
  return ret;
}

bool Rsa::setPublicKey(BignumPointer&& n, BignumPointer&& e) {
  if (!n || !e) return false;
  if (RSA_set0_key(const_cast<RSA*>(rsa_), n.get(), e.get(), nullptr) == 1) {
    n.release();
    e.release();
    return true;
  }
  return false;
}

bool Rsa::setPrivateKey(BignumPointer&& d,
                        BignumPointer&& q,
                        BignumPointer&& p,
                        BignumPointer&& dp,
                        BignumPointer&& dq,
                        BignumPointer&& qi) {
  if (!RSA_set0_key(const_cast<RSA*>(rsa_), nullptr, nullptr, d.get())) {
    return false;
  }
  d.release();

  if (!RSA_set0_factors(const_cast<RSA*>(rsa_), p.get(), q.get())) {
    return false;
  }
  p.release();
  q.release();

  if (!RSA_set0_crt_params(
          const_cast<RSA*>(rsa_), dp.get(), dq.get(), qi.get())) {
    return false;
  }
  dp.release();
  dq.release();
  qi.release();
  return true;
}

DataPointer Rsa::encrypt(const EVPKeyPointer& key,
                         const Rsa::CipherParams& params,
                         const Buffer<const void> in) {
  if (!key) return {};
  return RSA_Cipher<EVP_PKEY_encrypt_init, EVP_PKEY_encrypt>(key, params, in);
}

DataPointer Rsa::decrypt(const EVPKeyPointer& key,
                         const Rsa::CipherParams& params,
                         const Buffer<const void> in) {
  if (!key) return {};
  return RSA_Cipher<EVP_PKEY_decrypt_init, EVP_PKEY_decrypt>(key, params, in);
}

DataPointer Cipher::encrypt(const EVPKeyPointer& key,
                            const CipherParams& params,
                            const Buffer<const void> in) {
  // public operation
  return CipherImpl<EVP_PKEY_encrypt_init, EVP_PKEY_encrypt>(key, params, in);
}

DataPointer Cipher::decrypt(const EVPKeyPointer& key,
                            const CipherParams& params,
                            const Buffer<const void> in) {
  // private operation
  return CipherImpl<EVP_PKEY_decrypt_init, EVP_PKEY_decrypt>(key, params, in);
}

DataPointer Cipher::sign(const EVPKeyPointer& key,
                         const CipherParams& params,
                         const Buffer<const void> in) {
  // private operation
  return CipherImpl<EVP_PKEY_sign_init, EVP_PKEY_sign>(key, params, in);
}

DataPointer Cipher::recover(const EVPKeyPointer& key,
                            const CipherParams& params,
                            const Buffer<const void> in) {
  // public operation
  return CipherImpl<EVP_PKEY_verify_recover_init, EVP_PKEY_verify_recover>(
      key, params, in);
}

// ============================================================================

Ec::Ec() : ec_(nullptr) {}

Ec::Ec(OSSL3_CONST EC_KEY* key) : ec_(key) {}

const EC_GROUP* Ec::getGroup() const {
  return ECKeyPointer::GetGroup(ec_);
}

int Ec::getCurve() const {
  return EC_GROUP_get_curve_name(getGroup());
}

// ============================================================================

EVPMDCtxPointer::EVPMDCtxPointer() : ctx_(nullptr) {}

EVPMDCtxPointer::EVPMDCtxPointer(EVP_MD_CTX* ctx) : ctx_(ctx) {}

EVPMDCtxPointer::EVPMDCtxPointer(EVPMDCtxPointer&& other) noexcept
    : ctx_(other.release()) {}

EVPMDCtxPointer& EVPMDCtxPointer::operator=(EVPMDCtxPointer&& other) noexcept {
  ctx_.reset(other.release());
  return *this;
}

EVPMDCtxPointer::~EVPMDCtxPointer() {
  reset();
}

void EVPMDCtxPointer::reset(EVP_MD_CTX* ctx) {
  ctx_.reset(ctx);
}

EVP_MD_CTX* EVPMDCtxPointer::release() {
  return ctx_.release();
}

bool EVPMDCtxPointer::digestInit(const EVP_MD* digest) {
  if (!ctx_) return false;
  return EVP_DigestInit_ex(ctx_.get(), digest, nullptr) > 0;
}

bool EVPMDCtxPointer::digestUpdate(const Buffer<const void>& in) {
  if (!ctx_) return false;
  return EVP_DigestUpdate(ctx_.get(), in.data, in.len) > 0;
}

DataPointer EVPMDCtxPointer::digestFinal(size_t length) {
  if (!ctx_) return {};

  auto buf = DataPointer::Alloc(length);
  if (!buf) return {};

  Buffer<void> buffer = buf;

  if (!digestFinalInto(&buffer)) [[unlikely]] {
    return {};
  }

  return buf;
}

bool EVPMDCtxPointer::digestFinalInto(Buffer<void>* buf) {
  if (!ctx_) return false;

  auto ptr = static_cast<unsigned char*>(buf->data);

  int ret = (buf->len == getExpectedSize())
                ? EVP_DigestFinal_ex(ctx_.get(), ptr, nullptr)
                : EVP_DigestFinalXOF(ctx_.get(), ptr, buf->len);

  if (ret != 1) [[unlikely]]
    return false;

  return true;
}

size_t EVPMDCtxPointer::getExpectedSize() {
  if (!ctx_) return 0;
  return EVP_MD_CTX_size(ctx_.get());
}

size_t EVPMDCtxPointer::getDigestSize() const {
  return EVP_MD_size(getDigest());
}

const EVP_MD* EVPMDCtxPointer::getDigest() const {
  if (!ctx_) return nullptr;
  return EVP_MD_CTX_md(ctx_.get());
}

bool EVPMDCtxPointer::hasXofFlag() const {
  if (!ctx_) return false;
  return (EVP_MD_flags(getDigest()) & EVP_MD_FLAG_XOF) == EVP_MD_FLAG_XOF;
}

bool EVPMDCtxPointer::copyTo(const EVPMDCtxPointer& other) const {
  if (!ctx_ || !other) return {};
  if (EVP_MD_CTX_copy(other.get(), ctx_.get()) != 1) return false;
  return true;
}

std::optional<EVP_PKEY_CTX*> EVPMDCtxPointer::signInit(const EVPKeyPointer& key,
                                                       const EVP_MD* digest) {
  EVP_PKEY_CTX* ctx = nullptr;
  if (!EVP_DigestSignInit(ctx_.get(), &ctx, digest, nullptr, key.get())) {
    return std::nullopt;
  }
  return ctx;
}

std::optional<EVP_PKEY_CTX*> EVPMDCtxPointer::verifyInit(
    const EVPKeyPointer& key, const EVP_MD* digest) {
  EVP_PKEY_CTX* ctx = nullptr;
  if (!EVP_DigestVerifyInit(ctx_.get(), &ctx, digest, nullptr, key.get())) {
    return std::nullopt;
  }
  return ctx;
}

DataPointer EVPMDCtxPointer::signOneShot(
    const Buffer<const unsigned char>& buf) const {
  if (!ctx_) return {};
  size_t len;
  if (!EVP_DigestSign(ctx_.get(), nullptr, &len, buf.data, buf.len)) {
    return {};
  }
  auto data = DataPointer::Alloc(len);
  if (!data) [[unlikely]]
    return {};

  if (!EVP_DigestSign(ctx_.get(),
                      static_cast<unsigned char*>(data.get()),
                      &len,
                      buf.data,
                      buf.len)) {
    return {};
  }
  return data;
}

DataPointer EVPMDCtxPointer::sign(
    const Buffer<const unsigned char>& buf) const {
  if (!ctx_) [[unlikely]]
    return {};
  size_t len;
  if (!EVP_DigestSignUpdate(ctx_.get(), buf.data, buf.len) ||
      !EVP_DigestSignFinal(ctx_.get(), nullptr, &len)) {
    return {};
  }
  auto data = DataPointer::Alloc(len);
  if (!data) [[unlikely]]
    return {};
  if (!EVP_DigestSignFinal(
          ctx_.get(), static_cast<unsigned char*>(data.get()), &len)) {
    return {};
  }
  return data.resize(len);
}

bool EVPMDCtxPointer::verify(const Buffer<const unsigned char>& buf,
                             const Buffer<const unsigned char>& sig) const {
  if (!ctx_) return false;
  int ret = EVP_DigestVerify(ctx_.get(), sig.data, sig.len, buf.data, buf.len);
  return ret == 1;
}

EVPMDCtxPointer EVPMDCtxPointer::New() {
  return EVPMDCtxPointer(EVP_MD_CTX_new());
}

// ============================================================================

bool extractP1363(const Buffer<const unsigned char>& buf,
                  unsigned char* dest,
                  size_t n) {
  auto asn1_sig = ECDSASigPointer::Parse(buf);
  if (!asn1_sig) return false;

  return BignumPointer::EncodePaddedInto(asn1_sig.r(), dest, n) > 0 &&
         BignumPointer::EncodePaddedInto(asn1_sig.s(), dest + n, n) > 0;
}

// ============================================================================

HMACCtxPointer::HMACCtxPointer() : ctx_(nullptr) {}

HMACCtxPointer::HMACCtxPointer(HMAC_CTX* ctx) : ctx_(ctx) {}

HMACCtxPointer::HMACCtxPointer(HMACCtxPointer&& other) noexcept
    : ctx_(other.release()) {}

HMACCtxPointer& HMACCtxPointer::operator=(HMACCtxPointer&& other) noexcept {
  ctx_.reset(other.release());
  return *this;
}

HMACCtxPointer::~HMACCtxPointer() {
  reset();
}

void HMACCtxPointer::reset(HMAC_CTX* ctx) {
  ctx_.reset(ctx);
}

HMAC_CTX* HMACCtxPointer::release() {
  return ctx_.release();
}

bool HMACCtxPointer::init(const Buffer<const void>& buf, const EVP_MD* md) {
  if (!ctx_) return false;
  return HMAC_Init_ex(ctx_.get(), buf.data, buf.len, md, nullptr) == 1;
}

bool HMACCtxPointer::update(const Buffer<const void>& buf) {
  if (!ctx_) return false;
  return HMAC_Update(ctx_.get(),
                     static_cast<const unsigned char*>(buf.data),
                     buf.len) == 1;
}

DataPointer HMACCtxPointer::digest() {
  auto data = DataPointer::Alloc(EVP_MAX_MD_SIZE);
  if (!data) return {};
  Buffer<void> buf = data;
  if (!digestInto(&buf)) return {};
  return data.resize(buf.len);
}

bool HMACCtxPointer::digestInto(Buffer<void>* buf) {
  if (!ctx_) return false;

  unsigned int len = buf->len;
  if (!HMAC_Final(ctx_.get(), static_cast<unsigned char*>(buf->data), &len)) {
    return false;
  }
  buf->len = len;
  return true;
}

HMACCtxPointer HMACCtxPointer::New() {
  return HMACCtxPointer(HMAC_CTX_new());
}

DataPointer hashDigest(const Buffer<const unsigned char>& buf,
                       const EVP_MD* md) {
  if (md == nullptr) return {};
  size_t md_len = EVP_MD_size(md);
  unsigned int result_size;
  auto data = DataPointer::Alloc(md_len);
  if (!data) return {};

  if (!EVP_Digest(buf.data,
                  buf.len,
                  reinterpret_cast<unsigned char*>(data.get()),
                  &result_size,
                  md,
                  nullptr)) {
    return {};
  }

  return data.resize(result_size);
}

// ============================================================================

X509Name::X509Name() : name_(nullptr), total_(0) {}

X509Name::X509Name(const X509_NAME* name)
    : name_(name), total_(X509_NAME_entry_count(name)) {}

X509Name::Iterator::Iterator(const X509Name& name, int pos)
    : name_(name), loc_(pos) {}

X509Name::Iterator& X509Name::Iterator::operator++() {
  ++loc_;
  return *this;
}

X509Name::Iterator::operator bool() const {
  return loc_ < name_.total_;
}

bool X509Name::Iterator::operator==(const Iterator& other) const {
  return loc_ == other.loc_;
}

bool X509Name::Iterator::operator!=(const Iterator& other) const {
  return loc_ != other.loc_;
}

std::pair<std::string, std::string> X509Name::Iterator::operator*() const {
  if (loc_ == name_.total_) return {{}, {}};

  X509_NAME_ENTRY* entry = X509_NAME_get_entry(name_, loc_);
  if (entry == nullptr) [[unlikely]]
    return {{}, {}};

  ASN1_OBJECT* name = X509_NAME_ENTRY_get_object(entry);
  ASN1_STRING* value = X509_NAME_ENTRY_get_data(entry);

  if (name == nullptr || value == nullptr) [[unlikely]] {
    return {{}, {}};
  }

  int nid = OBJ_obj2nid(name);
  std::string name_str;
  if (nid != NID_undef) {
    name_str = std::string(OBJ_nid2sn(nid));
  } else {
    char buf[80];
    OBJ_obj2txt(buf, sizeof(buf), name, 0);
    name_str = std::string(buf);
  }

  unsigned char* value_str;
  int value_str_size = ASN1_STRING_to_UTF8(&value_str, value);

  return {
      std::move(name_str),
      std::string(reinterpret_cast<const char*>(value_str), value_str_size)};
}

// ============================================================================

Dsa::Dsa() : dsa_(nullptr) {}

Dsa::Dsa(OSSL3_CONST DSA* dsa) : dsa_(dsa) {}

const BIGNUM* Dsa::getP() const {
  if (dsa_ == nullptr) return nullptr;
  const BIGNUM* p;
  DSA_get0_pqg(dsa_, &p, nullptr, nullptr);
  return p;
}

const BIGNUM* Dsa::getQ() const {
  if (dsa_ == nullptr) return nullptr;
  const BIGNUM* q;
  DSA_get0_pqg(dsa_, nullptr, &q, nullptr);
  return q;
}

size_t Dsa::getModulusLength() const {
  if (dsa_ == nullptr) return 0;
  return BignumPointer::GetBitCount(getP());
}

size_t Dsa::getDivisorLength() const {
  if (dsa_ == nullptr) return 0;
  return BignumPointer::GetBitCount(getQ());
}

}  // namespace ncrypto
