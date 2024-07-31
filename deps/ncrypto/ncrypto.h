#pragma once

#include <list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "openssl/bn.h"
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
using X509Pointer = DeleteFnPtr<X509, X509_free>;

class BignumPointer final {
 public:
  BignumPointer() = default;
  explicit BignumPointer(BIGNUM* bignum);
  BignumPointer(BignumPointer&& other) noexcept;
  BignumPointer& operator=(BignumPointer&& other) noexcept;
  NCRYPTO_DISALLOW_COPY(BignumPointer)
  ~BignumPointer();

  bool operator==(const BignumPointer& other) noexcept;
  bool operator==(const BIGNUM* other) noexcept;
  inline bool operator==(std::nullptr_t) noexcept { return bn_ == nullptr; }
  inline operator bool() const { return bn_ != nullptr; }
  inline BIGNUM* get() const noexcept { return bn_.get(); }
  void reset(BIGNUM* bn = nullptr);
  BIGNUM* release();

  size_t byteLength();

  std::vector<uint8_t> encode();
  std::vector<uint8_t> encodePadded(size_t size);

  static std::vector<uint8_t> encode(const BIGNUM* bn);
  static std::vector<uint8_t> encodePadded(const BIGNUM* bn, size_t size);

 private:
  DeleteFnPtr<BIGNUM, BN_clear_free> bn_;
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

struct Buffer {
  const void* data;
  size_t len;
};

bool CSPRNG(void* buffer, size_t length) NCRYPTO_MUST_USE_RESULT;

// This callback is used to avoid the default passphrase callback in OpenSSL
// which will typically prompt for the passphrase. The prompting is designed
// for the OpenSSL CLI, but works poorly for some environments like Node.js
// because it involves synchronous interaction with the controlling terminal,
// something we never want, and use this function to avoid it.
int NoPasswordCallback(char* buf, int size, int rwflag, void* u);

int PasswordCallback(char* buf, int size, int rwflag, void* u);

// ============================================================================
// Version metadata
#define NCRYPTO_VERSION "0.0.1"

enum {
  NCRYPTO_VERSION_MAJOR = 0,
  NCRYPTO_VERSION_MINOR = 0,
  NCRYPTO_VERSION_REVISION = 1,
};

}  // namespace ncrypto
