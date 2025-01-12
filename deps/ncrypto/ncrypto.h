#pragma once

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/dh.h>
#include <openssl/dsa.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <cstddef>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#ifndef OPENSSL_NO_ENGINE
#include <openssl/engine.h>
#endif  // !OPENSSL_NO_ENGINE
// The FIPS-related functions are only available
// when the OpenSSL itself was compiled with FIPS support.
#if defined(OPENSSL_FIPS) && OPENSSL_VERSION_MAJOR < 3
#include <openssl/fips.h>
#endif  // OPENSSL_FIPS

#if OPENSSL_VERSION_MAJOR >= 3
#define OSSL3_CONST const
#else
#define OSSL3_CONST
#endif

#ifdef __GNUC__
#define NCRYPTO_MUST_USE_RESULT __attribute__((warn_unused_result))
#else
#define NCRYPTO_MUST_USE_RESULT
#endif

namespace ncrypto {

// ============================================================================
// Utility macros

#if NCRYPTO_DEVELOPMENT_CHECKS
#define NCRYPTO_STR(x) #x
#define NCRYPTO_REQUIRE(EXPR)                                                  \
  {                                                                            \
    if (!(EXPR) { abort(); }) }

#define NCRYPTO_FAIL(MESSAGE)                                                  \
  do {                                                                         \
    std::cerr << "FAIL: " << (MESSAGE) << std::endl;                           \
    abort();                                                                   \
  } while (0);
#define NCRYPTO_ASSERT_EQUAL(LHS, RHS, MESSAGE)                                \
  do {                                                                         \
    if (LHS != RHS) {                                                          \
      std::cerr << "Mismatch: '" << LHS << "' - '" << RHS << "'" << std::endl; \
      NCRYPTO_FAIL(MESSAGE);                                                   \
    }                                                                          \
  } while (0);
#define NCRYPTO_ASSERT_TRUE(COND)                                              \
  do {                                                                         \
    if (!(COND)) {                                                             \
      std::cerr << "Assert at line " << __LINE__ << " of file " << __FILE__    \
                << std::endl;                                                  \
      NCRYPTO_FAIL(NCRYPTO_STR(COND));                                         \
    }                                                                          \
  } while (0);
#else
#define NCRYPTO_FAIL(MESSAGE)
#define NCRYPTO_ASSERT_EQUAL(LHS, RHS, MESSAGE)
#define NCRYPTO_ASSERT_TRUE(COND)
#endif

#define NCRYPTO_DISALLOW_COPY(Name)                                            \
  Name(const Name&) = delete;                                                  \
  Name& operator=(const Name&) = delete;
#define NCRYPTO_DISALLOW_MOVE(Name)                                            \
  Name(Name&&) = delete;                                                       \
  Name& operator=(Name&&) = delete;
#define NCRYPTO_DISALLOW_COPY_AND_MOVE(Name)                                   \
  NCRYPTO_DISALLOW_COPY(Name)                                                  \
  NCRYPTO_DISALLOW_MOVE(Name)
#define NCRYPTO_DISALLOW_NEW_DELETE()                                          \
  void* operator new(size_t) = delete;                                         \
  void operator delete(void*) = delete;

[[noreturn]] inline void unreachable() {
#ifdef __GNUC__
  __builtin_unreachable();
#elif defined(_MSC_VER)
  __assume(false);
#else
#endif
}

static constexpr int kX509NameFlagsMultiline =
    ASN1_STRFLGS_ESC_2253 | ASN1_STRFLGS_ESC_CTRL | ASN1_STRFLGS_UTF8_CONVERT |
    XN_FLAG_SEP_MULTILINE | XN_FLAG_FN_SN;

// ============================================================================
// Error handling utilities

// Capture the current OpenSSL Error Stack. The stack will be ordered such
// that the error currently at the top of the stack is at the end of the
// list and the error at the bottom of the stack is at the beginning.
class CryptoErrorList final {
 public:
  enum class Option { NONE, CAPTURE_ON_CONSTRUCT };
  CryptoErrorList(Option option = Option::CAPTURE_ON_CONSTRUCT);

  void capture();

  // Add an error message to the end of the stack.
  void add(std::string message);

  inline const std::string& peek_back() const { return errors_.back(); }
  inline size_t size() const { return errors_.size(); }
  inline bool empty() const { return errors_.empty(); }

  inline auto begin() const noexcept { return errors_.begin(); }
  inline auto end() const noexcept { return errors_.end(); }
  inline auto rbegin() const noexcept { return errors_.rbegin(); }
  inline auto rend() const noexcept { return errors_.rend(); }

  std::optional<std::string> pop_back();
  std::optional<std::string> pop_front();

 private:
  std::list<std::string> errors_;
};

// Forcibly clears the error stack on destruction. This stops stale errors
// from popping up later in the lifecycle of crypto operations where they
// would cause spurious failures. It is a rather blunt method, though, and
// ERR_clear_error() isn't necessarily cheap.
//
// If created with a pointer to a CryptoErrorList, the current OpenSSL error
// stack will be captured before clearing the error.
class ClearErrorOnReturn final {
 public:
  ClearErrorOnReturn(CryptoErrorList* errors = nullptr);
  ~ClearErrorOnReturn();
  NCRYPTO_DISALLOW_COPY_AND_MOVE(ClearErrorOnReturn)
  NCRYPTO_DISALLOW_NEW_DELETE()

  int peekError();

 private:
  CryptoErrorList* errors_;
};

// Pop errors from OpenSSL's error stack that were added between when this
// was constructed and destructed.
//
// If created with a pointer to a CryptoErrorList, the current OpenSSL error
// stack will be captured before resetting the error to the mark.
class MarkPopErrorOnReturn final {
 public:
  MarkPopErrorOnReturn(CryptoErrorList* errors = nullptr);
  ~MarkPopErrorOnReturn();
  NCRYPTO_DISALLOW_COPY_AND_MOVE(MarkPopErrorOnReturn)
  NCRYPTO_DISALLOW_NEW_DELETE()

  int peekError();

 private:
  CryptoErrorList* errors_;
};

// TODO(@jasnell): Eventually replace with std::expected when we are able to
// bump up to c++23.
template <typename T, typename E>
struct Result final {
  const bool has_value;
  T value;
  std::optional<E> error = std::nullopt;
  std::optional<int> openssl_error = std::nullopt;
  Result(T&& value) : has_value(true), value(std::move(value)) {}
  Result(E&& error, std::optional<int> openssl_error = std::nullopt)
      : has_value(false),
        error(std::move(error)),
        openssl_error(std::move(openssl_error)) {}
  inline operator bool() const { return has_value; }
};

// ============================================================================
// Various smart pointer aliases for OpenSSL types.

template <typename T, void (*function)(T*)>
struct FunctionDeleter {
  void operator()(T* pointer) const { function(pointer); }
  typedef std::unique_ptr<T, FunctionDeleter> Pointer;
};

template <typename T, void (*function)(T*)>
using DeleteFnPtr = typename FunctionDeleter<T, function>::Pointer;

using BignumCtxPointer = DeleteFnPtr<BN_CTX, BN_CTX_free>;
using BignumGenCallbackPointer = DeleteFnPtr<BN_GENCB, BN_GENCB_free>;
using EVPKeyCtxPointer = DeleteFnPtr<EVP_PKEY_CTX, EVP_PKEY_CTX_free>;
using EVPMDCtxPointer = DeleteFnPtr<EVP_MD_CTX, EVP_MD_CTX_free>;
using HMACCtxPointer = DeleteFnPtr<HMAC_CTX, HMAC_CTX_free>;
using NetscapeSPKIPointer = DeleteFnPtr<NETSCAPE_SPKI, NETSCAPE_SPKI_free>;
using PKCS8Pointer = DeleteFnPtr<PKCS8_PRIV_KEY_INFO, PKCS8_PRIV_KEY_INFO_free>;
using RSAPointer = DeleteFnPtr<RSA, RSA_free>;
using SSLSessionPointer = DeleteFnPtr<SSL_SESSION, SSL_SESSION_free>;

class CipherCtxPointer;
class ECKeyPointer;

struct StackOfXASN1Deleter {
  void operator()(STACK_OF(ASN1_OBJECT) * p) const {
    sk_ASN1_OBJECT_pop_free(p, ASN1_OBJECT_free);
  }
};
using StackOfASN1 = std::unique_ptr<STACK_OF(ASN1_OBJECT), StackOfXASN1Deleter>;

// An unowned, unmanaged pointer to a buffer of data.
template <typename T>
struct Buffer {
  T* data = nullptr;
  size_t len = 0;
};

class Cipher final {
 public:
  Cipher() = default;
  Cipher(const EVP_CIPHER* cipher) : cipher_(cipher) {}
  Cipher(const Cipher&) = default;
  Cipher& operator=(const Cipher&) = default;
  inline Cipher& operator=(const EVP_CIPHER* cipher) {
    cipher_ = cipher;
    return *this;
  }
  NCRYPTO_DISALLOW_MOVE(Cipher)

  inline const EVP_CIPHER* get() const { return cipher_; }
  inline operator const EVP_CIPHER*() const { return cipher_; }
  inline operator bool() const { return cipher_ != nullptr; }

  int getNid() const;
  int getMode() const;
  int getIvLength() const;
  int getKeyLength() const;
  int getBlockSize() const;
  std::string_view getModeLabel() const;
  std::string_view getName() const;

  bool isSupportedAuthenticatedMode() const;

  static const Cipher FromName(const char* name);
  static const Cipher FromNid(int nid);
  static const Cipher FromCtx(const CipherCtxPointer& ctx);

 private:
  const EVP_CIPHER* cipher_ = nullptr;
};

// A managed pointer to a buffer of data. When destroyed the underlying
// buffer will be freed.
class DataPointer final {
 public:
  static DataPointer Alloc(size_t len);

  DataPointer() = default;
  explicit DataPointer(void* data, size_t len);
  explicit DataPointer(const Buffer<void>& buffer);
  DataPointer(DataPointer&& other) noexcept;
  DataPointer& operator=(DataPointer&& other) noexcept;
  NCRYPTO_DISALLOW_COPY(DataPointer)
  ~DataPointer();

  inline bool operator==(std::nullptr_t) noexcept { return data_ == nullptr; }
  inline operator bool() const { return data_ != nullptr; }
  inline void* get() const noexcept { return data_; }
  inline size_t size() const noexcept { return len_; }
  void reset(void* data = nullptr, size_t len = 0);
  void reset(const Buffer<void>& buffer);

  // Releases ownership of the underlying data buffer. It is the caller's
  // responsibility to ensure the buffer is appropriately freed.
  Buffer<void> release();

  // Returns a Buffer struct that is a view of the underlying data.
  template <typename T = void>
  inline operator const Buffer<T>() const {
    return {
        .data = static_cast<T*>(data_),
        .len = len_,
    };
  }

 private:
  void* data_ = nullptr;
  size_t len_ = 0;
};

class BIOPointer final {
 public:
  static BIOPointer NewMem();
  static BIOPointer NewSecMem();
  static BIOPointer New(const BIO_METHOD* method);
  static BIOPointer New(const void* data, size_t len);
  static BIOPointer New(const BIGNUM* bn);
  static BIOPointer NewFile(std::string_view filename, std::string_view mode);
  static BIOPointer NewFp(FILE* fd, int flags);

  template <typename T>
  static BIOPointer New(const Buffer<T>& buf) {
    return New(buf.data, buf.len);
  }

  BIOPointer() = default;
  BIOPointer(std::nullptr_t) : bio_(nullptr) {}
  explicit BIOPointer(BIO* bio);
  BIOPointer(BIOPointer&& other) noexcept;
  BIOPointer& operator=(BIOPointer&& other) noexcept;
  NCRYPTO_DISALLOW_COPY(BIOPointer)
  ~BIOPointer();

  inline bool operator==(std::nullptr_t) noexcept { return bio_ == nullptr; }
  inline operator bool() const { return bio_ != nullptr; }
  inline BIO* get() const noexcept { return bio_.get(); }

  inline operator BUF_MEM*() const {
    BUF_MEM* mem = nullptr;
    if (!bio_) return mem;
    BIO_get_mem_ptr(bio_.get(), &mem);
    return mem;
  }

  inline operator BIO*() const { return bio_.get(); }

  void reset(BIO* bio = nullptr);
  BIO* release();

  bool resetBio() const;

  static int Write(BIOPointer* bio, std::string_view message);

  template <typename... Args>
  static void Printf(BIOPointer* bio, const char* format, Args... args) {
    if (bio == nullptr || !*bio) return;
    BIO_printf(bio->get(), format, std::forward<Args...>(args...));
  }

 private:
  mutable DeleteFnPtr<BIO, BIO_free_all> bio_;
};

class BignumPointer final {
 public:
  BignumPointer() = default;
  explicit BignumPointer(BIGNUM* bignum);
  explicit BignumPointer(const unsigned char* data, size_t len);
  BignumPointer(BignumPointer&& other) noexcept;
  BignumPointer& operator=(BignumPointer&& other) noexcept;
  NCRYPTO_DISALLOW_COPY(BignumPointer)
  ~BignumPointer();

  int operator<=>(const BignumPointer& other) const noexcept;
  int operator<=>(const BIGNUM* other) const noexcept;
  inline operator bool() const { return bn_ != nullptr; }
  inline BIGNUM* get() const noexcept { return bn_.get(); }
  void reset(BIGNUM* bn = nullptr);
  void reset(const unsigned char* data, size_t len);
  BIGNUM* release();

  bool isZero() const;
  bool isOne() const;

  bool setWord(unsigned long w);  // NOLINT(runtime/int)
  unsigned long getWord() const;  // NOLINT(runtime/int)

  size_t byteLength() const;

  DataPointer toHex() const;
  DataPointer encode() const;
  DataPointer encodePadded(size_t size) const;
  size_t encodeInto(unsigned char* out) const;
  size_t encodePaddedInto(unsigned char* out, size_t size) const;

  using PrimeCheckCallback = std::function<bool(int, int)>;
  int isPrime(int checks,
              PrimeCheckCallback cb = defaultPrimeCheckCallback) const;
  struct PrimeConfig {
    int bits;
    bool safe = false;
    const BignumPointer& add;
    const BignumPointer& rem;
  };

  static BignumPointer NewPrime(
      const PrimeConfig& params,
      PrimeCheckCallback cb = defaultPrimeCheckCallback);

  bool generate(const PrimeConfig& params,
                PrimeCheckCallback cb = defaultPrimeCheckCallback) const;

  static BignumPointer New();
  static BignumPointer NewSecure();
  static BignumPointer NewSub(const BignumPointer& a, const BignumPointer& b);
  static BignumPointer NewLShift(size_t length);

  static DataPointer Encode(const BIGNUM* bn);
  static DataPointer EncodePadded(const BIGNUM* bn, size_t size);
  static size_t EncodePaddedInto(const BIGNUM* bn,
                                 unsigned char* out,
                                 size_t size);
  static int GetBitCount(const BIGNUM* bn);
  static int GetByteCount(const BIGNUM* bn);
  static unsigned long GetWord(const BIGNUM* bn);  // NOLINT(runtime/int)
  static const BIGNUM* One();

  BignumPointer clone();

 private:
  DeleteFnPtr<BIGNUM, BN_clear_free> bn_;

  static bool defaultPrimeCheckCallback(int, int) { return 1; }
};

class CipherCtxPointer final {
 public:
  static CipherCtxPointer New();

  CipherCtxPointer() = default;
  explicit CipherCtxPointer(EVP_CIPHER_CTX* ctx);
  CipherCtxPointer(CipherCtxPointer&& other) noexcept;
  CipherCtxPointer& operator=(CipherCtxPointer&& other) noexcept;
  NCRYPTO_DISALLOW_COPY(CipherCtxPointer)
  ~CipherCtxPointer();

  inline bool operator==(std::nullptr_t) const noexcept {
    return ctx_ == nullptr;
  }
  inline operator bool() const { return ctx_ != nullptr; }
  inline EVP_CIPHER_CTX* get() const { return ctx_.get(); }
  inline operator EVP_CIPHER_CTX*() const { return ctx_.get(); }
  void reset(EVP_CIPHER_CTX* ctx = nullptr);
  EVP_CIPHER_CTX* release();

  void setFlags(int flags);
  bool setKeyLength(size_t length);
  bool setIvLength(size_t length);
  bool setAeadTag(const Buffer<const char>& tag);
  bool setAeadTagLength(size_t length);
  bool setPadding(bool padding);
  bool init(const Cipher& cipher,
            bool encrypt,
            const unsigned char* key = nullptr,
            const unsigned char* iv = nullptr);

  int getBlockSize() const;
  int getMode() const;
  int getNid() const;

  bool update(const Buffer<const unsigned char>& in,
              unsigned char* out,
              int* out_len,
              bool finalize = false);
  bool getAeadTag(size_t len, unsigned char* out);

 private:
  DeleteFnPtr<EVP_CIPHER_CTX, EVP_CIPHER_CTX_free> ctx_;
};

class EVPKeyPointer final {
 public:
  static EVPKeyPointer New();
  static EVPKeyPointer NewRawPublic(int id,
                                    const Buffer<const unsigned char>& data);
  static EVPKeyPointer NewRawPrivate(int id,
                                     const Buffer<const unsigned char>& data);

  enum class PKEncodingType {
    // RSAPublicKey / RSAPrivateKey according to PKCS#1.
    PKCS1,
    // PrivateKeyInfo or EncryptedPrivateKeyInfo according to PKCS#8.
    PKCS8,
    // SubjectPublicKeyInfo according to X.509.
    SPKI,
    // ECPrivateKey according to SEC1.
    SEC1,
  };

  enum class PKFormatType {
    DER,
    PEM,
    JWK,
  };

  enum class PKParseError { NOT_RECOGNIZED, NEED_PASSPHRASE, FAILED };
  using ParseKeyResult = Result<EVPKeyPointer, PKParseError>;

  struct AsymmetricKeyEncodingConfig {
    bool output_key_object = false;
    PKFormatType format = PKFormatType::DER;
    PKEncodingType type = PKEncodingType::PKCS8;
    AsymmetricKeyEncodingConfig() = default;
    AsymmetricKeyEncodingConfig(bool output_key_object,
                                PKFormatType format,
                                PKEncodingType type);
    AsymmetricKeyEncodingConfig(const AsymmetricKeyEncodingConfig&) = default;
    AsymmetricKeyEncodingConfig& operator=(const AsymmetricKeyEncodingConfig&) =
        default;
  };
  using PublicKeyEncodingConfig = AsymmetricKeyEncodingConfig;

  struct PrivateKeyEncodingConfig : public AsymmetricKeyEncodingConfig {
    const EVP_CIPHER* cipher = nullptr;
    std::optional<DataPointer> passphrase = std::nullopt;
    PrivateKeyEncodingConfig() = default;
    PrivateKeyEncodingConfig(bool output_key_object,
                             PKFormatType format,
                             PKEncodingType type)
        : AsymmetricKeyEncodingConfig(output_key_object, format, type) {}
    PrivateKeyEncodingConfig(const PrivateKeyEncodingConfig&);
    PrivateKeyEncodingConfig& operator=(const PrivateKeyEncodingConfig&);
  };

  static ParseKeyResult TryParsePublicKey(
      const PublicKeyEncodingConfig& config,
      const Buffer<const unsigned char>& buffer);

  static ParseKeyResult TryParsePublicKeyPEM(
      const Buffer<const unsigned char>& buffer);

  static ParseKeyResult TryParsePrivateKey(
      const PrivateKeyEncodingConfig& config,
      const Buffer<const unsigned char>& buffer);

  EVPKeyPointer() = default;
  explicit EVPKeyPointer(EVP_PKEY* pkey);
  EVPKeyPointer(EVPKeyPointer&& other) noexcept;
  EVPKeyPointer& operator=(EVPKeyPointer&& other) noexcept;
  NCRYPTO_DISALLOW_COPY(EVPKeyPointer)
  ~EVPKeyPointer();

  bool assign(const ECKeyPointer& eckey);
  bool set(const ECKeyPointer& eckey);
  operator const EC_KEY*() const;

  inline bool operator==(std::nullptr_t) const noexcept {
    return pkey_ == nullptr;
  }
  inline operator bool() const { return pkey_ != nullptr; }
  inline EVP_PKEY* get() const { return pkey_.get(); }
  void reset(EVP_PKEY* pkey = nullptr);
  EVP_PKEY* release();

  static int id(const EVP_PKEY* key);
  static int base_id(const EVP_PKEY* key);

  int id() const;
  int base_id() const;
  int bits() const;
  size_t size() const;

  size_t rawPublicKeySize() const;
  size_t rawPrivateKeySize() const;
  DataPointer rawPublicKey() const;
  DataPointer rawPrivateKey() const;
  BIOPointer derPublicKey() const;

  Result<BIOPointer, bool> writePrivateKey(
      const PrivateKeyEncodingConfig& config) const;
  Result<BIOPointer, bool> writePublicKey(
      const PublicKeyEncodingConfig& config) const;

  EVPKeyCtxPointer newCtx() const;

  static bool IsRSAPrivateKey(const Buffer<const unsigned char>& buffer);

 private:
  DeleteFnPtr<EVP_PKEY, EVP_PKEY_free> pkey_;
};

class DHPointer final {
 public:
  enum class FindGroupOption {
    NONE,
    // There are known and documented security issues with prime groups smaller
    // than 2048 bits. When the NO_SMALL_PRIMES option is set, these small prime
    // groups will not be supported.
    NO_SMALL_PRIMES,
  };

  static BignumPointer GetStandardGenerator();

  static BignumPointer FindGroup(
      const std::string_view name,
      FindGroupOption option = FindGroupOption::NONE);
  static DHPointer FromGroup(const std::string_view name,
                             FindGroupOption option = FindGroupOption::NONE);

  static DHPointer New(BignumPointer&& p, BignumPointer&& g);
  static DHPointer New(size_t bits, unsigned int generator);

  DHPointer() = default;
  explicit DHPointer(DH* dh);
  DHPointer(DHPointer&& other) noexcept;
  DHPointer& operator=(DHPointer&& other) noexcept;
  NCRYPTO_DISALLOW_COPY(DHPointer)
  ~DHPointer();

  inline bool operator==(std::nullptr_t) noexcept { return dh_ == nullptr; }
  inline operator bool() const { return dh_ != nullptr; }
  inline DH* get() const { return dh_.get(); }
  void reset(DH* dh = nullptr);
  DH* release();

  enum class CheckResult {
    NONE,
    P_NOT_PRIME = DH_CHECK_P_NOT_PRIME,
    P_NOT_SAFE_PRIME = DH_CHECK_P_NOT_SAFE_PRIME,
    UNABLE_TO_CHECK_GENERATOR = DH_UNABLE_TO_CHECK_GENERATOR,
    NOT_SUITABLE_GENERATOR = DH_NOT_SUITABLE_GENERATOR,
    Q_NOT_PRIME = DH_CHECK_Q_NOT_PRIME,
    INVALID_Q = DH_CHECK_INVALID_Q_VALUE,
    INVALID_J = DH_CHECK_INVALID_J_VALUE,
    CHECK_FAILED = 512,
  };
  CheckResult check();

  enum class CheckPublicKeyResult {
    NONE,
    TOO_SMALL = DH_R_CHECK_PUBKEY_TOO_SMALL,
    TOO_LARGE = DH_R_CHECK_PUBKEY_TOO_LARGE,
    INVALID = DH_R_CHECK_PUBKEY_INVALID,
    CHECK_FAILED = 512,
  };
  // Check to see if the given public key is suitable for this DH instance.
  CheckPublicKeyResult checkPublicKey(const BignumPointer& pub_key);

  DataPointer getPrime() const;
  DataPointer getGenerator() const;
  DataPointer getPublicKey() const;
  DataPointer getPrivateKey() const;
  DataPointer generateKeys() const;
  DataPointer computeSecret(const BignumPointer& peer) const;

  bool setPublicKey(BignumPointer&& key);
  bool setPrivateKey(BignumPointer&& key);

  size_t size() const;

  static DataPointer stateless(const EVPKeyPointer& ourKey,
                               const EVPKeyPointer& theirKey);

 private:
  DeleteFnPtr<DH, DH_free> dh_;
};

struct StackOfX509Deleter {
  void operator()(STACK_OF(X509) * p) const { sk_X509_pop_free(p, X509_free); }
};
using StackOfX509 = std::unique_ptr<STACK_OF(X509), StackOfX509Deleter>;

class X509Pointer;
class X509View;

class SSLCtxPointer final {
 public:
  SSLCtxPointer() = default;
  explicit SSLCtxPointer(SSL_CTX* ctx);
  SSLCtxPointer(SSLCtxPointer&& other) noexcept;
  SSLCtxPointer& operator=(SSLCtxPointer&& other) noexcept;
  NCRYPTO_DISALLOW_COPY(SSLCtxPointer)
  ~SSLCtxPointer();

  inline bool operator==(std::nullptr_t) const noexcept {
    return ctx_ == nullptr;
  }
  inline operator bool() const { return ctx_ != nullptr; }
  inline SSL_CTX* get() const { return ctx_.get(); }
  void reset(SSL_CTX* ctx = nullptr);
  void reset(const SSL_METHOD* method);
  SSL_CTX* release();

  bool setGroups(const char* groups);
  void setStatusCallback(auto callback) {
    if (!ctx_) return;
    SSL_CTX_set_tlsext_status_cb(get(), callback);
    SSL_CTX_set_tlsext_status_arg(get(), nullptr);
  }

  static SSLCtxPointer NewServer();
  static SSLCtxPointer NewClient();
  static SSLCtxPointer New(const SSL_METHOD* method = TLS_method());

 private:
  DeleteFnPtr<SSL_CTX, SSL_CTX_free> ctx_;
};

class SSLPointer final {
 public:
  SSLPointer() = default;
  explicit SSLPointer(SSL* ssl);
  SSLPointer(SSLPointer&& other) noexcept;
  SSLPointer& operator=(SSLPointer&& other) noexcept;
  NCRYPTO_DISALLOW_COPY(SSLPointer)
  ~SSLPointer();

  inline bool operator==(std::nullptr_t) noexcept { return ssl_ == nullptr; }
  inline operator bool() const { return ssl_ != nullptr; }
  inline SSL* get() const { return ssl_.get(); }
  inline operator SSL*() const { return ssl_.get(); }
  void reset(SSL* ssl = nullptr);
  SSL* release();

  bool setSession(const SSLSessionPointer& session);
  bool setSniContext(const SSLCtxPointer& ctx) const;

  const std::string_view getClientHelloAlpn() const;
  const std::string_view getClientHelloServerName() const;

  std::optional<const std::string_view> getServerName() const;
  X509View getCertificate() const;
  EVPKeyPointer getPeerTempKey() const;
  const SSL_CIPHER* getCipher() const;
  bool isServer() const;

  std::optional<uint32_t> verifyPeerCertificate() const;

  void getCiphers(std::function<void(const std::string_view)> cb) const;

  static SSLPointer New(const SSLCtxPointer& ctx);
  static std::optional<const std::string_view> GetServerName(const SSL* ssl);

 private:
  DeleteFnPtr<SSL, SSL_free> ssl_;
};

class X509View final {
 public:
  static X509View From(const SSLPointer& ssl);
  static X509View From(const SSLCtxPointer& ctx);

  X509View() = default;
  inline explicit X509View(const X509* cert) : cert_(cert) {}
  X509View(const X509View& other) = default;
  X509View& operator=(const X509View& other) = default;
  NCRYPTO_DISALLOW_MOVE(X509View)

  inline X509* get() const { return const_cast<X509*>(cert_); }
  inline operator X509*() const { return const_cast<X509*>(cert_); }
  inline operator const X509*() const { return cert_; }

  inline bool operator==(std::nullptr_t) noexcept { return cert_ == nullptr; }
  inline operator bool() const { return cert_ != nullptr; }

  BIOPointer toPEM() const;
  BIOPointer toDER() const;

  BIOPointer getSubject() const;
  BIOPointer getSubjectAltName() const;
  BIOPointer getIssuer() const;
  BIOPointer getInfoAccess() const;
  BIOPointer getValidFrom() const;
  BIOPointer getValidTo() const;
  int64_t getValidFromTime() const;
  int64_t getValidToTime() const;
  DataPointer getSerialNumber() const;
  Result<EVPKeyPointer, int> getPublicKey() const;
  StackOfASN1 getKeyUsage() const;

  bool isCA() const;
  bool isIssuedBy(const X509View& other) const;
  bool checkPrivateKey(const EVPKeyPointer& pkey) const;
  bool checkPublicKey(const EVPKeyPointer& pkey) const;

  std::optional<std::string> getFingerprint(const EVP_MD* method) const;

  X509Pointer clone() const;

  enum class CheckMatch {
    NO_MATCH,
    MATCH,
    INVALID_NAME,
    OPERATION_FAILED,
  };
  CheckMatch checkHost(const std::string_view host,
                       int flags,
                       DataPointer* peerName = nullptr) const;
  CheckMatch checkEmail(const std::string_view email, int flags) const;
  CheckMatch checkIp(const std::string_view ip, int flags) const;

 private:
  const X509* cert_ = nullptr;
};

class X509Pointer final {
 public:
  static Result<X509Pointer, int> Parse(Buffer<const unsigned char> buffer);
  static X509Pointer IssuerFrom(const SSLPointer& ssl, const X509View& view);
  static X509Pointer IssuerFrom(const SSL_CTX* ctx, const X509View& view);
  static X509Pointer PeerFrom(const SSLPointer& ssl);

  X509Pointer() = default;
  explicit X509Pointer(X509* cert);
  X509Pointer(X509Pointer&& other) noexcept;
  X509Pointer& operator=(X509Pointer&& other) noexcept;
  NCRYPTO_DISALLOW_COPY(X509Pointer)
  ~X509Pointer();

  inline bool operator==(std::nullptr_t) noexcept { return cert_ == nullptr; }
  inline operator bool() const { return cert_ != nullptr; }
  inline X509* get() const { return cert_.get(); }
  inline operator X509*() const { return cert_.get(); }
  inline operator const X509*() const { return cert_.get(); }
  void reset(X509* cert = nullptr);
  X509* release();

  X509View view() const;
  operator X509View() const { return view(); }

  static std::string_view ErrorCode(int32_t err);
  static std::optional<std::string_view> ErrorReason(int32_t err);

 private:
  DeleteFnPtr<X509, X509_free> cert_;
};

class ECDSASigPointer final {
 public:
  explicit ECDSASigPointer();
  explicit ECDSASigPointer(ECDSA_SIG* sig);
  ECDSASigPointer(ECDSASigPointer&& other) noexcept;
  ECDSASigPointer& operator=(ECDSASigPointer&& other) noexcept;
  NCRYPTO_DISALLOW_COPY(ECDSASigPointer)
  ~ECDSASigPointer();

  inline bool operator==(std::nullptr_t) noexcept { return sig_ == nullptr; }
  inline operator bool() const { return sig_ != nullptr; }
  inline ECDSA_SIG* get() const { return sig_.get(); }
  inline operator ECDSA_SIG*() const { return sig_.get(); }
  void reset(ECDSA_SIG* sig = nullptr);
  ECDSA_SIG* release();

  static ECDSASigPointer New();
  static ECDSASigPointer Parse(const Buffer<const unsigned char>& buffer);

  inline const BIGNUM* r() const { return pr_; }
  inline const BIGNUM* s() const { return ps_; }

  bool setParams(BignumPointer&& r, BignumPointer&& s);

  Buffer<unsigned char> encode() const;

 private:
  DeleteFnPtr<ECDSA_SIG, ECDSA_SIG_free> sig_;
  const BIGNUM* pr_ = nullptr;
  const BIGNUM* ps_ = nullptr;
};

class ECGroupPointer final {
 public:
  explicit ECGroupPointer();
  explicit ECGroupPointer(EC_GROUP* group);
  ECGroupPointer(ECGroupPointer&& other) noexcept;
  ECGroupPointer& operator=(ECGroupPointer&& other) noexcept;
  NCRYPTO_DISALLOW_COPY(ECGroupPointer)
  ~ECGroupPointer();

  inline bool operator==(std::nullptr_t) noexcept { return group_ == nullptr; }
  inline operator bool() const { return group_ != nullptr; }
  inline EC_GROUP* get() const { return group_.get(); }
  inline operator EC_GROUP*() const { return group_.get(); }
  void reset(EC_GROUP* group = nullptr);
  EC_GROUP* release();

  static ECGroupPointer NewByCurveName(int nid);

 private:
  DeleteFnPtr<EC_GROUP, EC_GROUP_free> group_;
};

class ECPointPointer final {
 public:
  ECPointPointer();
  explicit ECPointPointer(EC_POINT* point);
  ECPointPointer(ECPointPointer&& other) noexcept;
  ECPointPointer& operator=(ECPointPointer&& other) noexcept;
  NCRYPTO_DISALLOW_COPY(ECPointPointer)
  ~ECPointPointer();

  inline bool operator==(std::nullptr_t) noexcept { return point_ == nullptr; }
  inline operator bool() const { return point_ != nullptr; }
  inline EC_POINT* get() const { return point_.get(); }
  inline operator EC_POINT*() const { return point_.get(); }
  void reset(EC_POINT* point = nullptr);
  EC_POINT* release();

  bool setFromBuffer(const Buffer<const unsigned char>& buffer,
                     const EC_GROUP* group);
  bool mul(const EC_GROUP* group, const BIGNUM* priv_key);

  static ECPointPointer New(const EC_GROUP* group);

 private:
  DeleteFnPtr<EC_POINT, EC_POINT_free> point_;
};

class ECKeyPointer final {
 public:
  ECKeyPointer();
  explicit ECKeyPointer(EC_KEY* key);
  ECKeyPointer(ECKeyPointer&& other) noexcept;
  ECKeyPointer& operator=(ECKeyPointer&& other) noexcept;
  NCRYPTO_DISALLOW_COPY(ECKeyPointer)
  ~ECKeyPointer();

  inline bool operator==(std::nullptr_t) noexcept { return key_ == nullptr; }
  inline operator bool() const { return key_ != nullptr; }
  inline EC_KEY* get() const { return key_.get(); }
  inline operator EC_KEY*() const { return key_.get(); }
  void reset(EC_KEY* key = nullptr);
  EC_KEY* release();

  ECKeyPointer clone() const;
  bool setPrivateKey(const BignumPointer& priv);
  bool setPublicKey(const ECPointPointer& pub);
  bool setPublicKeyRaw(const BignumPointer& x, const BignumPointer& y);
  bool generate();
  bool checkKey() const;

  const EC_GROUP* getGroup() const;
  const BIGNUM* getPrivateKey() const;
  const EC_POINT* getPublicKey() const;

  static ECKeyPointer New(const EC_GROUP* group);
  static ECKeyPointer NewByCurveName(int nid);

  static const EC_POINT* GetPublicKey(const EC_KEY* key);
  static const BIGNUM* GetPrivateKey(const EC_KEY* key);
  static const EC_GROUP* GetGroup(const EC_KEY* key);
  static int GetGroupName(const EC_KEY* key);
  static bool Check(const EC_KEY* key);

 private:
  DeleteFnPtr<EC_KEY, EC_KEY_free> key_;
};

#ifndef OPENSSL_NO_ENGINE
class EnginePointer final {
 public:
  EnginePointer() = default;

  explicit EnginePointer(ENGINE* engine_, bool finish_on_exit = false);
  EnginePointer(EnginePointer&& other) noexcept;
  EnginePointer& operator=(EnginePointer&& other) noexcept;
  NCRYPTO_DISALLOW_COPY(EnginePointer)
  ~EnginePointer();

  inline operator bool() const { return engine != nullptr; }
  inline ENGINE* get() { return engine; }
  inline void setFinishOnExit() { finish_on_exit = true; }

  void reset(ENGINE* engine_ = nullptr, bool finish_on_exit_ = false);

  bool setAsDefault(uint32_t flags, CryptoErrorList* errors = nullptr);
  bool init(bool finish_on_exit = false);
  EVPKeyPointer loadPrivateKey(const std::string_view key_name);

  // Release ownership of the ENGINE* pointer.
  ENGINE* release();

  // Retrieve an OpenSSL Engine instance by name. If the name does not
  // identify a valid named engine, the returned EnginePointer will be
  // empty.
  static EnginePointer getEngineByName(const std::string_view name,
                                       CryptoErrorList* errors = nullptr);

  // Call once when initializing OpenSSL at startup for the process.
  static void initEnginesOnce();

 private:
  ENGINE* engine = nullptr;
  bool finish_on_exit = false;
};
#endif  // !OPENSSL_NO_ENGINE

// ============================================================================
// FIPS
bool isFipsEnabled();

bool setFipsEnabled(bool enabled, CryptoErrorList* errors);

bool testFipsEnabled();

// ============================================================================
// Various utilities

bool CSPRNG(void* buffer, size_t length) NCRYPTO_MUST_USE_RESULT;

// This callback is used to avoid the default passphrase callback in OpenSSL
// which will typically prompt for the passphrase. The prompting is designed
// for the OpenSSL CLI, but works poorly for some environments like Node.js
// because it involves synchronous interaction with the controlling terminal,
// something we never want, and use this function to avoid it.
int NoPasswordCallback(char* buf, int size, int rwflag, void* u);

int PasswordCallback(char* buf, int size, int rwflag, void* u);

bool SafeX509SubjectAltNamePrint(const BIOPointer& out, X509_EXTENSION* ext);
bool SafeX509InfoAccessPrint(const BIOPointer& out, X509_EXTENSION* ext);

// ============================================================================
// SPKAC

bool VerifySpkac(const char* input, size_t length);
BIOPointer ExportPublicKey(const char* input, size_t length);

// The caller takes ownership of the returned Buffer<char>
Buffer<char> ExportChallenge(const char* input, size_t length);

// ============================================================================
// KDF

const EVP_MD* getDigestByName(const std::string_view name);

// Verify that the specified HKDF output length is valid for the given digest.
// The maximum length for HKDF output for a given digest is 255 times the
// hash size for the given digest algorithm.
bool checkHkdfLength(const EVP_MD* md, size_t length);

DataPointer hkdf(const EVP_MD* md,
                 const Buffer<const unsigned char>& key,
                 const Buffer<const unsigned char>& info,
                 const Buffer<const unsigned char>& salt,
                 size_t length);

bool checkScryptParams(uint64_t N, uint64_t r, uint64_t p, uint64_t maxmem);

DataPointer scrypt(const Buffer<const char>& pass,
                   const Buffer<const unsigned char>& salt,
                   uint64_t N,
                   uint64_t r,
                   uint64_t p,
                   uint64_t maxmem,
                   size_t length);

DataPointer pbkdf2(const EVP_MD* md,
                   const Buffer<const char>& pass,
                   const Buffer<const unsigned char>& salt,
                   uint32_t iterations,
                   size_t length);

// ============================================================================
// Version metadata
#define NCRYPTO_VERSION "0.0.1"

enum {
  NCRYPTO_VERSION_MAJOR = 0,
  NCRYPTO_VERSION_MINOR = 0,
  NCRYPTO_VERSION_REVISION = 1,
};

}  // namespace ncrypto
