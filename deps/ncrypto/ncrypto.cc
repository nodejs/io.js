#include "ncrypto.h"
#include <algorithm>
#include <cstring>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/pkcs12.h>
#include <openssl/x509v3.h>
#if OPENSSL_VERSION_MAJOR >= 3
#include <openssl/provider.h>
#endif

namespace ncrypto {
namespace {
static constexpr int kX509NameFlagsRFC2253WithinUtf8JSON =
    XN_FLAG_RFC2253 &
    ~ASN1_STRFLGS_ESC_MSB &
    ~ASN1_STRFLGS_ESC_CTRL;
}  // namespace

// ============================================================================

ClearErrorOnReturn::ClearErrorOnReturn(CryptoErrorList* errors) : errors_(errors) {
  ERR_clear_error();
}

ClearErrorOnReturn::~ClearErrorOnReturn() {
  if (errors_ != nullptr) errors_->capture();
  ERR_clear_error();
}

int ClearErrorOnReturn::peeKError() { return ERR_peek_error(); }

MarkPopErrorOnReturn::MarkPopErrorOnReturn(CryptoErrorList* errors) : errors_(errors) {
  ERR_set_mark();
}

MarkPopErrorOnReturn::~MarkPopErrorOnReturn() {
  if (errors_ != nullptr) errors_->capture();
  ERR_pop_to_mark();
}

int MarkPopErrorOnReturn::peekError() { return ERR_peek_error(); }

CryptoErrorList::CryptoErrorList(CryptoErrorList::Option option) {
  if (option == Option::CAPTURE_ON_CONSTRUCT) capture();
}

void CryptoErrorList::capture() {
  errors_.clear();
  while(const auto err = ERR_get_error()) {
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
  return DataPointer(OPENSSL_malloc(len), len);
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

DataPointer::~DataPointer() { reset(); }

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
  Buffer<void> buf {
    .data = data_,
    .len = len_,
  };
  data_ = nullptr;
  len_ = 0;
  return buf;
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
  const auto enabled = fips_provider == nullptr ? 0 :
      OSSL_PROVIDER_self_test(fips_provider) ? 1 : 0;
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
  return BignumPointer(BN_secure_new());
}

BignumPointer& BignumPointer::operator=(BignumPointer&& other) noexcept {
  if (this == &other) return *this;
  this->~BignumPointer();
  return *new (this) BignumPointer(std::move(other));
}

BignumPointer::~BignumPointer() { reset(); }

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

bool BignumPointer::setWord(unsigned long w) {
  if (!bn_) return false;
  return BN_set_word(bn_.get(), w) == 1;
}

unsigned long BignumPointer::GetWord(const BIGNUM* bn) {
  return BN_get_word(bn);
}

unsigned long BignumPointer::getWord() const {
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
size_t BignumPointer::EncodePaddedInto(const BIGNUM* bn, unsigned char* out, size_t size) {
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

int BignumPointer::GetByteCount(const BIGNUM *bn) {
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
    if (buflen < len)
      return -1;
    memcpy(buf, reinterpret_cast<const char*>(passphrase->data), len);
    return len;
  }

  return -1;
}

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
  NetscapeSPKIPointer spki(
      NETSCAPE_SPKI_b64_decode(input, length));
  if (!spki)
    return false;

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
  NetscapeSPKIPointer spki(
      NETSCAPE_SPKI_b64_decode(input, length));
  if (!spki) return {};

  EVPKeyPointer pkey(NETSCAPE_SPKI_get_pubkey(spki.get()));
  if (!pkey) return {};

  if (PEM_write_bio_PUBKEY(bio.get(), pkey.get()) <= 0) return { };

  return bio;
}

Buffer<char> ExportChallenge(const char* input, size_t length) {
#ifdef OPENSSL_IS_BORINGSSL
  // OpenSSL uses EVP_DecodeBlock, which explicitly removes trailing characters,
  // while BoringSSL uses EVP_DecodedLength and EVP_DecodeBase64, which do not.
  // As such, we trim those characters here for compatibility.
  length = std::string_view(input, length).find_last_not_of(" \n\r\t") + 1;
#endif
  NetscapeSPKIPointer sp(
      NETSCAPE_SPKI_b64_decode(input, length));
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
      // Single quotes are unlikely to appear in any legitimate values, but they
      // could be used to make a value look like it was escaped (i.e., enclosed
      // in single/double quotes).
      return false;
    default:
      if (option == AltNameOption::UTF8) {
        // In UTF8 strings, we require escaping for any ASCII control character,
        // but NOT for non-ASCII characters. Note that all bytes of any code
        // point that consists of more than a single byte have their MSB set.
        if (static_cast<unsigned char>(c) < ' ' || c == '\x7f') {
          return false;
        }
      } else {
        // Check if the char is a control character or non-ASCII character. Note
        // that char may or may not be a signed type. Regardless, non-ASCII
        // values will always be outside of this range.
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
        char u[] = { '\\', 'u', '0', '0', hex[(c & 0xf0) >> 4], hex[c & 0x0f] };
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
    if (X509_NAME_print_ex(tmp.get(),
                           gen->d.dirn,
                           0,
                           kX509NameFlagsRFC2253WithinUtf8JSON) < 0) {
      return false;
    }
    char* oline = nullptr;
    long n_bytes = BIO_get_mem_data(tmp.get(), &oline);  // NOLINT(runtime/int)
    NCRYPTO_ASSERT_TRUE(n_bytes >= 0);
    PrintAltName(out, oline, static_cast<size_t>(n_bytes),
        ncrypto::AltNameOption::UTF8, nullptr);
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
    if (prefix == nullptr ||
        (unicode && val_type != V_ASN1_UTF8STRING) ||
        (!unicode && val_type != V_ASN1_IA5STRING)) {
      BIO_printf(out.get(), "othername:<unsupported>");
    } else {
      BIO_printf(out.get(), "othername:");
      if (unicode) {
        auto name = gen->d.otherName->value->value.utf8string;
        PrintAltName(out,
            reinterpret_cast<const char*>(name->data), name->length,
            AltNameOption::UTF8, prefix);
      } else {
        auto name = gen->d.otherName->value->value.ia5string;
        PrintAltName(out,
            reinterpret_cast<const char*>(name->data), name->length,
            AltNameOption::NONE, prefix);
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
  NCRYPTO_ASSERT_EQUAL(ret, NID_subject_alt_name, "unexpected extension type");

  GENERAL_NAMES* names = static_cast<GENERAL_NAMES*>(X509V3_EXT_d2i(ext));
  if (names == nullptr)
    return false;

  bool ok = true;

  for (int i = 0; i < sk_GENERAL_NAME_num(names); i++) {
    GENERAL_NAME* gen = sk_GENERAL_NAME_value(names, i);

    if (i != 0)
      BIO_write(out.get(), ", ", 2);

    if (!(ok = ncrypto::PrintGeneralName(out, gen))) {
      break;
    }
  }
  sk_GENERAL_NAME_pop_free(names, GENERAL_NAME_free);

  return ok;
}

bool SafeX509InfoAccessPrint(const BIOPointer& out, X509_EXTENSION* ext) {
  auto ret = OBJ_obj2nid(X509_EXTENSION_get_object(ext));
  NCRYPTO_ASSERT_EQUAL(ret, NID_info_access, "unexpected extension type");

  AUTHORITY_INFO_ACCESS* descs =
      static_cast<AUTHORITY_INFO_ACCESS*>(X509V3_EXT_d2i(ext));
  if (descs == nullptr)
    return false;

  bool ok = true;

  for (int i = 0; i < sk_ACCESS_DESCRIPTION_num(descs); i++) {
    ACCESS_DESCRIPTION* desc = sk_ACCESS_DESCRIPTION_value(descs, i);

    if (i != 0)
      BIO_write(out.get(), "\n", 1);

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

X509Pointer::~X509Pointer() { reset(); }

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

BIOPointer X509View::getSubject() const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr) return {};
  BIOPointer bio(BIO_new(BIO_s_mem()));
  if (!bio) return {};
  if (X509_NAME_print_ex(bio.get(), X509_get_subject_name(cert_),
                         0, kX509NameFlagsMultiline) <= 0) {
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
  if (index < 0 || !SafeX509SubjectAltNamePrint(bio, X509_get_ext(cert_, index))) {
    return {};
  }
  return bio;
}

BIOPointer X509View::getIssuer() const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr) return {};
  BIOPointer bio(BIO_new(BIO_s_mem()));
  if (!bio) return {};
  if (X509_NAME_print_ex(bio.get(), X509_get_issuer_name(cert_), 0,
                         kX509NameFlagsMultiline) <= 0) {
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

DataPointer X509View::getSerialNumber() const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr) return {};
  if (ASN1_INTEGER* serial_number = X509_get_serialNumber(const_cast<X509*>(cert_))) {
    if (auto bn = BignumPointer(ASN1_INTEGER_to_BN(serial_number, nullptr))) {
      return bn.toHex();
    }
  }
  return {};
}

Result<EVPKeyPointer, int> X509View::getPublicKey() const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr) return Result<EVPKeyPointer, int>(EVPKeyPointer {});
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

X509View::CheckMatch X509View::checkHost(const std::string_view host, int flags,
                                         DataPointer* peerName) const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr) return CheckMatch::NO_MATCH;
  char* peername;
  switch (X509_check_host(const_cast<X509*>(cert_), host.data(), host.size(), flags, &peername)) {
    case 0: return CheckMatch::NO_MATCH;
    case 1: {
      if (peername != nullptr) {
        DataPointer name(peername, strlen(peername));
        if (peerName != nullptr) *peerName = std::move(name);
      }
      return CheckMatch::MATCH;
    }
    case -2: return CheckMatch::INVALID_NAME;
    default: return CheckMatch::OPERATION_FAILED;
  }
}

X509View::CheckMatch X509View::checkEmail(const std::string_view email, int flags) const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr) return CheckMatch::NO_MATCH;
  switch (X509_check_email(const_cast<X509*>(cert_), email.data(), email.size(), flags)) {
    case 0: return CheckMatch::NO_MATCH;
    case 1: return CheckMatch::MATCH;
    case -2: return CheckMatch::INVALID_NAME;
    default: return CheckMatch::OPERATION_FAILED;
  }
}

X509View::CheckMatch X509View::checkIp(const std::string_view ip, int flags) const {
  ClearErrorOnReturn clearErrorOnReturn;
  if (cert_ == nullptr) return CheckMatch::NO_MATCH;
  switch (X509_check_ip_asc(const_cast<X509*>(cert_), ip.data(), flags)) {
    case 0: return CheckMatch::NO_MATCH;
    case 1: return CheckMatch::MATCH;
    case -2: return CheckMatch::INVALID_NAME;
    default: return CheckMatch::OPERATION_FAILED;
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

X509Pointer X509View::clone() const {
  ClearErrorOnReturn clear_error_on_return;
  if (!cert_) return {};
  return X509Pointer(X509_dup(const_cast<X509*>(cert_)));
}

Result<X509Pointer, int> X509Pointer::Parse(Buffer<const unsigned char> buffer) {
  ClearErrorOnReturn clearErrorOnReturn;
  BIOPointer bio(BIO_new_mem_buf(buffer.data, buffer.len));
  if (!bio) return Result<X509Pointer, int>(ERR_get_error());

  X509Pointer pem(PEM_read_bio_X509_AUX(bio.get(), nullptr, NoPasswordCallback, nullptr));
  if (pem) return Result<X509Pointer, int>(std::move(pem));
  BIO_reset(bio.get());

  X509Pointer der(d2i_X509_bio(bio.get(), nullptr));
  if (der) return Result<X509Pointer, int>(std::move(der));

  return Result<X509Pointer, int>(ERR_get_error());
}


X509Pointer X509Pointer::IssuerFrom(const SSLPointer& ssl, const X509View& view) {
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
}  // namespace ncrypto
