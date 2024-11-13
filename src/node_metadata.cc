#include "node_metadata.h"
#include "acorn_version.h"
#include "ada.h"
#include "amaro_version.h"
#include "ares.h"
#include "brotli/encode.h"
#include "cjs_module_lexer_version.h"
#include "llhttp.h"
#include "nbytes.h"
#include "nghttp2/nghttp2ver.h"
#include "node.h"
#include "simdjson.h"
#include "simdutf.h"
#include "sqlite3.h"
#include "undici_version.h"
#include "util.h"
#include "uv.h"
#include "uvwasi.h"
#include "v8.h"

#ifdef NODE_BUNDLED_ZLIB
#include "zlib_version.h"
#else
#include <zlib.h>
#endif  // NODE_BUNDLED_ZLIB

#if HAVE_OPENSSL
#include <openssl/crypto.h>
#include "ncrypto.h"
#if NODE_OPENSSL_HAS_QUIC
#include <openssl/quic.h>
#endif
#endif  // HAVE_OPENSSL

#ifdef NODE_OPENSSL_HAS_QUIC
#include <ngtcp2/version.h>
#include <nghttp3/version.h>
#endif

#ifdef NODE_HAVE_I18N_SUPPORT
#include <unicode/timezone.h>
#include <unicode/ulocdata.h>
#include <unicode/uvernum.h>
#include <unicode/uversion.h>
#endif  // NODE_HAVE_I18N_SUPPORT

namespace node {

namespace per_process {
Metadata metadata;
}

#if HAVE_OPENSSL
static constexpr size_t search(const char* s, char c, size_t n = 0) {
  return *s == '\0' ? n : (*s == c ? n : search(s + 1, c, n + 1));
}

static inline std::string GetOpenSSLVersion() {
  // sample openssl version string format
  // for reference: "OpenSSL 1.1.0i 14 Aug 2018"
  const char* version = OpenSSL_version(OPENSSL_VERSION);
  const size_t first_space = search(version, ' ');

  // When Node.js is linked to an alternative library implementing the
  // OpenSSL API e.g. BoringSSL, the version string may not match the
  //  expected pattern. In this case just return “0.0.0” as placeholder.
  if (version[first_space] == '\0') {
    return "0.0.0";
  }

  const size_t start = first_space + 1;
  const size_t len = search(&version[start], ' ');
  return std::string(version, start, len);
}
#endif  // HAVE_OPENSSL

#ifdef NODE_HAVE_I18N_SUPPORT
void Metadata::Versions::InitializeIntlVersions() {
  UErrorCode status = U_ZERO_ERROR;

  const char* tz_version = icu::TimeZone::getTZDataVersion(status);
  if (U_SUCCESS(status)) {
    tz = tz_version;
  }

  char buf[U_MAX_VERSION_STRING_LENGTH];
  UVersionInfo versionArray;
  ulocdata_getCLDRVersion(versionArray, &status);
  if (U_SUCCESS(status)) {
    u_versionToString(versionArray, buf);
    cldr = buf;
  }
}
#endif  // NODE_HAVE_I18N_SUPPORT

Metadata::Versions::Versions() {
  node = NODE_VERSION_STRING;
  v8 = v8::V8::GetVersion();
  uv = uv_version_string();
#ifdef NODE_BUNDLED_ZLIB
  zlib = ZLIB_VERSION;
#else
  zlib = zlibVersion();
#endif  // NODE_BUNDLED_ZLIB
  ares = ARES_VERSION_STR;
  modules = NODE_STRINGIFY(NODE_MODULE_VERSION);
  nghttp2 = NGHTTP2_VERSION;
  napi = NODE_STRINGIFY(NODE_API_SUPPORTED_VERSION_MAX);
  llhttp =
      NODE_STRINGIFY(LLHTTP_VERSION_MAJOR)
      "."
      NODE_STRINGIFY(LLHTTP_VERSION_MINOR)
      "."
      NODE_STRINGIFY(LLHTTP_VERSION_PATCH);

  brotli =
    std::to_string(BrotliEncoderVersion() >> 24) +
    "." +
    std::to_string((BrotliEncoderVersion() & 0xFFF000) >> 12) +
    "." +
    std::to_string(BrotliEncoderVersion() & 0xFFF);
#ifndef NODE_SHARED_BUILTIN_UNDICI_UNDICI_PATH
  undici = UNDICI_VERSION;
#endif

  acorn = ACORN_VERSION;
  cjs_module_lexer = CJS_MODULE_LEXER_VERSION;
  uvwasi = UVWASI_VERSION_STRING;

#ifndef NODE_SHARED_BUILTIN_AMARO_DIST_INDEX_PATH
#if HAVE_AMARO
  amaro = AMARO_VERSION;
#endif
#endif

#if HAVE_OPENSSL
  openssl = GetOpenSSLVersion();
  ncrypto = NCRYPTO_VERSION;
#endif

#ifdef NODE_HAVE_I18N_SUPPORT
  icu = U_ICU_VERSION;
  unicode = U_UNICODE_VERSION;
#endif  // NODE_HAVE_I18N_SUPPORT

#ifdef NODE_OPENSSL_HAS_QUIC
  ngtcp2 = NGTCP2_VERSION;
  nghttp3 = NGHTTP3_VERSION;
#endif

  simdjson = SIMDJSON_VERSION;
  simdutf = SIMDUTF_VERSION;
  sqlite = SQLITE_VERSION;
  ada = ADA_VERSION;
  nbytes = NBYTES_VERSION;
}

Metadata::Release::Release() : name(NODE_RELEASE) {
#if NODE_VERSION_IS_LTS
  lts = NODE_VERSION_LTS_CODENAME;
#endif  // NODE_VERSION_IS_LTS

#ifdef NODE_HAS_RELEASE_URLS
#define NODE_RELEASE_URLPFX NODE_RELEASE_URLBASE "v" NODE_VERSION_STRING "/"
#define NODE_RELEASE_URLFPFX NODE_RELEASE_URLPFX "node-v" NODE_VERSION_STRING

  source_url = NODE_RELEASE_URLFPFX ".tar.gz";
  headers_url = NODE_RELEASE_URLFPFX "-headers.tar.gz";
#ifdef _WIN32
  lib_url = NODE_RELEASE_URLPFX "win-" NODE_ARCH "/node.lib";
#endif  // _WIN32

#endif  // NODE_HAS_RELEASE_URLS
}

Metadata::Metadata() : arch(NODE_ARCH), platform(NODE_PLATFORM) {}

}  // namespace node
