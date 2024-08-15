#pragma once

#include <cstddef>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <openssl/bn.h>
#include <openssl/x509.h>
#include <openssl/dh.h>
#include <openssl/dsa.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#ifndef OPENSSL_NO_ENGINE
#  include <openssl/engine.h>
#endif  // !OPENSSL_NO_ENGINE
// The FIPS-related functions are only available
// when the OpenSSL itself was compiled with FIPS support.
#if defined(OPENSSL_FIPS) && OPENSSL_VERSION_MAJOR < 3
#  include <openssl/fips.h>
#endif  // OPENSSL_FIPS

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
    ASN1_STRFLGS_ESC_2253 |
    ASN1_STRFLGS_ESC_CTRL |
    ASN1_STRFLGS_UTF8_CONVERT |
    XN_FLAG_SEP_MULTILINE |
    XN_FLAG_FN_SN;

// ============================================================================
// Error handling utilities

// Capture the current OpenSSL Error Stack. The stack will be ordered such
// that the error currently at the top of the stack is at the end of the
// list and the error at the bottom of the stack is at the beginning.
class CryptoErrorList final {
public:
  enum class Option {
    NONE,
    CAPTURE_ON_CONSTRUCT
  };
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

  int peeKError();

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

template <typename T, typename E>
struct Result final {
  T value;
  std::optional<E> error;
  Result(T&& value) : value(std::move(value)) {}
  Result(E&& error) : error(std::move(error)) {}
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
using BIOPointer = DeleteFnPtr<BIO, BIO_free_all>;
using CipherCtxPointer = DeleteFnPtr<EVP_CIPHER_CTX, EVP_CIPHER_CTX_free>;
using DHPointer = DeleteFnPtr<DH, DH_free>;
using DSAPointer = DeleteFnPtr<DSA, DSA_free>;
using DSASigPointer = DeleteFnPtr<DSA_SIG, DSA_SIG_free>;
using ECDSASigPointer = DeleteFnPtr<ECDSA_SIG, ECDSA_SIG_free>;
using ECPointer = DeleteFnPtr<EC_KEY, EC_KEY_free>;
using ECGroupPointer = DeleteFnPtr<EC_GROUP, EC_GROUP_free>;
using ECKeyPointer = DeleteFnPtr<EC_KEY, EC_KEY_free>;
using ECPointPointer = DeleteFnPtr<EC_POINT, EC_POINT_free>;
using EVPKeyCtxPointer = DeleteFnPtr<EVP_PKEY_CTX, EVP_PKEY_CTX_free>;
using EVPKeyPointer = DeleteFnPtr<EVP_PKEY, EVP_PKEY_free>;
using EVPMDCtxPointer = DeleteFnPtr<EVP_MD_CTX, EVP_MD_CTX_free>;
using HMACCtxPointer = DeleteFnPtr<HMAC_CTX, HMAC_CTX_free>;
using NetscapeSPKIPointer = DeleteFnPtr<NETSCAPE_SPKI, NETSCAPE_SPKI_free>;
using PKCS8Pointer = DeleteFnPtr<PKCS8_PRIV_KEY_INFO, PKCS8_PRIV_KEY_INFO_free>;
using RSAPointer = DeleteFnPtr<RSA, RSA_free>;
using SSLCtxPointer = DeleteFnPtr<SSL_CTX, SSL_CTX_free>;
using SSLPointer = DeleteFnPtr<SSL, SSL_free>;
using SSLSessionPointer = DeleteFnPtr<SSL_SESSION, SSL_SESSION_free>;

struct StackOfXASN1Deleter {
  void operator()(STACK_OF(ASN1_OBJECT)* p) const {
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
  inline operator const Buffer<void>() const {
    return {
      .data = data_,
      .len = len_,
    };
  }

 private:
  void* data_ = nullptr;
  size_t len_ = 0;
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

  bool setWord(unsigned long w);
  unsigned long getWord() const;

  size_t byteLength() const;

  DataPointer toHex() const;
  DataPointer encode() const;
  DataPointer encodePadded(size_t size) const;
  size_t encodeInto(unsigned char* out) const;
  size_t encodePaddedInto(unsigned char* out, size_t size) const;

  static BignumPointer New();
  static BignumPointer NewSecure();
  static DataPointer Encode(const BIGNUM* bn);
  static DataPointer EncodePadded(const BIGNUM* bn, size_t size);
  static size_t EncodePaddedInto(const BIGNUM* bn, unsigned char* out, size_t size);
  static int GetBitCount(const BIGNUM* bn);
  static int GetByteCount(const BIGNUM* bn);
  static unsigned long GetWord(const BIGNUM* bn);
  static const BIGNUM* One();

 private:
  DeleteFnPtr<BIGNUM, BN_clear_free> bn_;
};

class X509Pointer;

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
  DataPointer getSerialNumber() const;
  Result<EVPKeyPointer, int> getPublicKey() const;
  StackOfASN1 getKeyUsage() const;

  bool isCA() const;
  bool isIssuedBy(const X509View& other) const;
  bool checkPrivateKey(const EVPKeyPointer& pkey) const;
  bool checkPublicKey(const EVPKeyPointer& pkey) const;

  X509Pointer clone() const;

  enum class CheckMatch {
    NO_MATCH,
    MATCH,
    INVALID_NAME,
    OPERATION_FAILED,
  };
  CheckMatch checkHost(const std::string_view host, int flags,
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
  void reset(X509* cert = nullptr);
  X509* release();

  X509View view() const;
  operator X509View() const { return view(); }

 private:
  DeleteFnPtr<X509, X509_free> cert_;
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
// Version metadata
#define NCRYPTO_VERSION "0.0.1"

enum {
  NCRYPTO_VERSION_MAJOR = 0,
  NCRYPTO_VERSION_MINOR = 0,
  NCRYPTO_VERSION_REVISION = 1,
};

}  // namespace ncrypto
