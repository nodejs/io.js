// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "env-inl.h"
#include "node_constants.h"
#include "node_internals.h"
#include "util-inl.h"

#include "zlib.h"

#if !defined(_MSC_VER)
#include <unistd.h>
#endif

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>


#if HAVE_OPENSSL
#include <openssl/ec.h>
#include <openssl/ssl.h>
#ifndef OPENSSL_NO_ENGINE
#include <openssl/engine.h>
#endif  // !OPENSSL_NO_ENGINE
#endif  // HAVE_OPENSSL

#if defined(__POSIX__)
#include <dlfcn.h>
#endif

#if defined(_WIN32)
#include <io.h>  // _S_IREAD _S_IWRITE
#ifndef S_IRUSR
#define S_IRUSR _S_IREAD
#endif  // S_IRUSR
#ifndef S_IWUSR
#define S_IWUSR _S_IWRITE
#endif  // S_IWUSR
#endif

#include <cerrno>
#include <csignal>
#include <limits>

namespace node {

using v8::Context;
using v8::Isolate;
using v8::Local;
using v8::Null;
using v8::Object;
using v8::Value;

namespace constants {

void DefineErrnoConstants(Local<Object> target) {
#ifdef E2BIG
  NODE_DEFINE_CONSTANT(target, E2BIG);
#endif

#ifdef EACCES
  NODE_DEFINE_CONSTANT(target, EACCES);
#endif

#ifdef EADDRINUSE
  NODE_DEFINE_CONSTANT(target, EADDRINUSE);
#endif

#ifdef EADDRNOTAVAIL
  NODE_DEFINE_CONSTANT(target, EADDRNOTAVAIL);
#endif

#ifdef EAFNOSUPPORT
  NODE_DEFINE_CONSTANT(target, EAFNOSUPPORT);
#endif

#ifdef EAGAIN
  NODE_DEFINE_CONSTANT(target, EAGAIN);
#endif

#ifdef EALREADY
  NODE_DEFINE_CONSTANT(target, EALREADY);
#endif

#ifdef EBADF
  NODE_DEFINE_CONSTANT(target, EBADF);
#endif

#ifdef EBADMSG
  NODE_DEFINE_CONSTANT(target, EBADMSG);
#endif

#ifdef EBUSY
  NODE_DEFINE_CONSTANT(target, EBUSY);
#endif

#ifdef ECANCELED
  NODE_DEFINE_CONSTANT(target, ECANCELED);
#endif

#ifdef ECHILD
  NODE_DEFINE_CONSTANT(target, ECHILD);
#endif

#ifdef ECONNABORTED
  NODE_DEFINE_CONSTANT(target, ECONNABORTED);
#endif

#ifdef ECONNREFUSED
  NODE_DEFINE_CONSTANT(target, ECONNREFUSED);
#endif

#ifdef ECONNRESET
  NODE_DEFINE_CONSTANT(target, ECONNRESET);
#endif

#ifdef EDEADLK
  NODE_DEFINE_CONSTANT(target, EDEADLK);
#endif

#ifdef EDESTADDRREQ
  NODE_DEFINE_CONSTANT(target, EDESTADDRREQ);
#endif

#ifdef EDOM
  NODE_DEFINE_CONSTANT(target, EDOM);
#endif

#ifdef EDQUOT
  NODE_DEFINE_CONSTANT(target, EDQUOT);
#endif

#ifdef EEXIST
  NODE_DEFINE_CONSTANT(target, EEXIST);
#endif

#ifdef EFAULT
  NODE_DEFINE_CONSTANT(target, EFAULT);
#endif

#ifdef EFBIG
  NODE_DEFINE_CONSTANT(target, EFBIG);
#endif

#ifdef EHOSTUNREACH
  NODE_DEFINE_CONSTANT(target, EHOSTUNREACH);
#endif

#ifdef EIDRM
  NODE_DEFINE_CONSTANT(target, EIDRM);
#endif

#ifdef EILSEQ
  NODE_DEFINE_CONSTANT(target, EILSEQ);
#endif

#ifdef EINPROGRESS
  NODE_DEFINE_CONSTANT(target, EINPROGRESS);
#endif

#ifdef EINTR
  NODE_DEFINE_CONSTANT(target, EINTR);
#endif

#ifdef EINVAL
  NODE_DEFINE_CONSTANT(target, EINVAL);
#endif

#ifdef EIO
  NODE_DEFINE_CONSTANT(target, EIO);
#endif

#ifdef EISCONN
  NODE_DEFINE_CONSTANT(target, EISCONN);
#endif

#ifdef EISDIR
  NODE_DEFINE_CONSTANT(target, EISDIR);
#endif

#ifdef ELOOP
  NODE_DEFINE_CONSTANT(target, ELOOP);
#endif

#ifdef EMFILE
  NODE_DEFINE_CONSTANT(target, EMFILE);
#endif

#ifdef EMLINK
  NODE_DEFINE_CONSTANT(target, EMLINK);
#endif

#ifdef EMSGSIZE
  NODE_DEFINE_CONSTANT(target, EMSGSIZE);
#endif

#ifdef EMULTIHOP
  NODE_DEFINE_CONSTANT(target, EMULTIHOP);
#endif

#ifdef ENAMETOOLONG
  NODE_DEFINE_CONSTANT(target, ENAMETOOLONG);
#endif

#ifdef ENETDOWN
  NODE_DEFINE_CONSTANT(target, ENETDOWN);
#endif

#ifdef ENETRESET
  NODE_DEFINE_CONSTANT(target, ENETRESET);
#endif

#ifdef ENETUNREACH
  NODE_DEFINE_CONSTANT(target, ENETUNREACH);
#endif

#ifdef ENFILE
  NODE_DEFINE_CONSTANT(target, ENFILE);
#endif

#ifdef ENOBUFS
  NODE_DEFINE_CONSTANT(target, ENOBUFS);
#endif

#ifdef ENODATA
  NODE_DEFINE_CONSTANT(target, ENODATA);
#endif

#ifdef ENODEV
  NODE_DEFINE_CONSTANT(target, ENODEV);
#endif

#ifdef ENOENT
  NODE_DEFINE_CONSTANT(target, ENOENT);
#endif

#ifdef ENOEXEC
  NODE_DEFINE_CONSTANT(target, ENOEXEC);
#endif

#ifdef ENOLCK
  NODE_DEFINE_CONSTANT(target, ENOLCK);
#endif

#ifdef ENOLINK
  NODE_DEFINE_CONSTANT(target, ENOLINK);
#endif

#ifdef ENOMEM
  NODE_DEFINE_CONSTANT(target, ENOMEM);
#endif

#ifdef ENOMSG
  NODE_DEFINE_CONSTANT(target, ENOMSG);
#endif

#ifdef ENOPROTOOPT
  NODE_DEFINE_CONSTANT(target, ENOPROTOOPT);
#endif

#ifdef ENOSPC
  NODE_DEFINE_CONSTANT(target, ENOSPC);
#endif

#ifdef ENOSR
  NODE_DEFINE_CONSTANT(target, ENOSR);
#endif

#ifdef ENOSTR
  NODE_DEFINE_CONSTANT(target, ENOSTR);
#endif

#ifdef ENOSYS
  NODE_DEFINE_CONSTANT(target, ENOSYS);
#endif

#ifdef ENOTCONN
  NODE_DEFINE_CONSTANT(target, ENOTCONN);
#endif

#ifdef ENOTDIR
  NODE_DEFINE_CONSTANT(target, ENOTDIR);
#endif

#ifdef ENOTEMPTY
  NODE_DEFINE_CONSTANT(target, ENOTEMPTY);
#endif

#ifdef ENOTSOCK
  NODE_DEFINE_CONSTANT(target, ENOTSOCK);
#endif

#ifdef ENOTSUP
  NODE_DEFINE_CONSTANT(target, ENOTSUP);
#endif

#ifdef ENOTTY
  NODE_DEFINE_CONSTANT(target, ENOTTY);
#endif

#ifdef ENXIO
  NODE_DEFINE_CONSTANT(target, ENXIO);
#endif

#ifdef EOPNOTSUPP
  NODE_DEFINE_CONSTANT(target, EOPNOTSUPP);
#endif

#ifdef EOVERFLOW
  NODE_DEFINE_CONSTANT(target, EOVERFLOW);
#endif

#ifdef EPERM
  NODE_DEFINE_CONSTANT(target, EPERM);
#endif

#ifdef EPIPE
  NODE_DEFINE_CONSTANT(target, EPIPE);
#endif

#ifdef EPROTO
  NODE_DEFINE_CONSTANT(target, EPROTO);
#endif

#ifdef EPROTONOSUPPORT
  NODE_DEFINE_CONSTANT(target, EPROTONOSUPPORT);
#endif

#ifdef EPROTOTYPE
  NODE_DEFINE_CONSTANT(target, EPROTOTYPE);
#endif

#ifdef ERANGE
  NODE_DEFINE_CONSTANT(target, ERANGE);
#endif

#ifdef EROFS
  NODE_DEFINE_CONSTANT(target, EROFS);
#endif

#ifdef ESPIPE
  NODE_DEFINE_CONSTANT(target, ESPIPE);
#endif

#ifdef ESRCH
  NODE_DEFINE_CONSTANT(target, ESRCH);
#endif

#ifdef ESTALE
  NODE_DEFINE_CONSTANT(target, ESTALE);
#endif

#ifdef ETIME
  NODE_DEFINE_CONSTANT(target, ETIME);
#endif

#ifdef ETIMEDOUT
  NODE_DEFINE_CONSTANT(target, ETIMEDOUT);
#endif

#ifdef ETXTBSY
  NODE_DEFINE_CONSTANT(target, ETXTBSY);
#endif

#ifdef EWOULDBLOCK
  NODE_DEFINE_CONSTANT(target, EWOULDBLOCK);
#endif

#ifdef EXDEV
  NODE_DEFINE_CONSTANT(target, EXDEV);
#endif
}

void DefineWindowsErrorConstants(Local<Object> target) {
#ifdef WSAEINTR
  NODE_DEFINE_CONSTANT(target, WSAEINTR);
#endif

#ifdef WSAEBADF
  NODE_DEFINE_CONSTANT(target, WSAEBADF);
#endif

#ifdef WSAEACCES
  NODE_DEFINE_CONSTANT(target, WSAEACCES);
#endif

#ifdef WSAEFAULT
  NODE_DEFINE_CONSTANT(target, WSAEFAULT);
#endif

#ifdef WSAEINVAL
  NODE_DEFINE_CONSTANT(target, WSAEINVAL);
#endif

#ifdef WSAEMFILE
  NODE_DEFINE_CONSTANT(target, WSAEMFILE);
#endif

#ifdef WSAEWOULDBLOCK
  NODE_DEFINE_CONSTANT(target, WSAEWOULDBLOCK);
#endif

#ifdef WSAEINPROGRESS
  NODE_DEFINE_CONSTANT(target, WSAEINPROGRESS);
#endif

#ifdef WSAEALREADY
  NODE_DEFINE_CONSTANT(target, WSAEALREADY);
#endif

#ifdef WSAENOTSOCK
  NODE_DEFINE_CONSTANT(target, WSAENOTSOCK);
#endif

#ifdef WSAEDESTADDRREQ
  NODE_DEFINE_CONSTANT(target, WSAEDESTADDRREQ);
#endif

#ifdef WSAEMSGSIZE
  NODE_DEFINE_CONSTANT(target, WSAEMSGSIZE);
#endif

#ifdef WSAEPROTOTYPE
  NODE_DEFINE_CONSTANT(target, WSAEPROTOTYPE);
#endif

#ifdef WSAENOPROTOOPT
  NODE_DEFINE_CONSTANT(target, WSAENOPROTOOPT);
#endif

#ifdef WSAEPROTONOSUPPORT
  NODE_DEFINE_CONSTANT(target, WSAEPROTONOSUPPORT);
#endif

#ifdef WSAESOCKTNOSUPPORT
  NODE_DEFINE_CONSTANT(target, WSAESOCKTNOSUPPORT);
#endif

#ifdef WSAEOPNOTSUPP
  NODE_DEFINE_CONSTANT(target, WSAEOPNOTSUPP);
#endif

#ifdef WSAEPFNOSUPPORT
  NODE_DEFINE_CONSTANT(target, WSAEPFNOSUPPORT);
#endif

#ifdef WSAEAFNOSUPPORT
  NODE_DEFINE_CONSTANT(target, WSAEAFNOSUPPORT);
#endif

#ifdef WSAEADDRINUSE
  NODE_DEFINE_CONSTANT(target, WSAEADDRINUSE);
#endif

#ifdef WSAEADDRNOTAVAIL
  NODE_DEFINE_CONSTANT(target, WSAEADDRNOTAVAIL);
#endif

#ifdef WSAENETDOWN
  NODE_DEFINE_CONSTANT(target, WSAENETDOWN);
#endif

#ifdef WSAENETUNREACH
  NODE_DEFINE_CONSTANT(target, WSAENETUNREACH);
#endif

#ifdef WSAENETRESET
  NODE_DEFINE_CONSTANT(target, WSAENETRESET);
#endif

#ifdef WSAECONNABORTED
  NODE_DEFINE_CONSTANT(target, WSAECONNABORTED);
#endif

#ifdef WSAECONNRESET
  NODE_DEFINE_CONSTANT(target, WSAECONNRESET);
#endif

#ifdef WSAENOBUFS
  NODE_DEFINE_CONSTANT(target, WSAENOBUFS);
#endif

#ifdef WSAEISCONN
  NODE_DEFINE_CONSTANT(target, WSAEISCONN);
#endif

#ifdef WSAENOTCONN
  NODE_DEFINE_CONSTANT(target, WSAENOTCONN);
#endif

#ifdef WSAESHUTDOWN
  NODE_DEFINE_CONSTANT(target, WSAESHUTDOWN);
#endif

#ifdef WSAETOOMANYREFS
  NODE_DEFINE_CONSTANT(target, WSAETOOMANYREFS);
#endif

#ifdef WSAETIMEDOUT
  NODE_DEFINE_CONSTANT(target, WSAETIMEDOUT);
#endif

#ifdef WSAECONNREFUSED
  NODE_DEFINE_CONSTANT(target, WSAECONNREFUSED);
#endif

#ifdef WSAELOOP
  NODE_DEFINE_CONSTANT(target, WSAELOOP);
#endif

#ifdef WSAENAMETOOLONG
  NODE_DEFINE_CONSTANT(target, WSAENAMETOOLONG);
#endif

#ifdef WSAEHOSTDOWN
  NODE_DEFINE_CONSTANT(target, WSAEHOSTDOWN);
#endif

#ifdef WSAEHOSTUNREACH
  NODE_DEFINE_CONSTANT(target, WSAEHOSTUNREACH);
#endif

#ifdef WSAENOTEMPTY
  NODE_DEFINE_CONSTANT(target, WSAENOTEMPTY);
#endif

#ifdef WSAEPROCLIM
  NODE_DEFINE_CONSTANT(target, WSAEPROCLIM);
#endif

#ifdef WSAEUSERS
  NODE_DEFINE_CONSTANT(target, WSAEUSERS);
#endif

#ifdef WSAEDQUOT
  NODE_DEFINE_CONSTANT(target, WSAEDQUOT);
#endif

#ifdef WSAESTALE
  NODE_DEFINE_CONSTANT(target, WSAESTALE);
#endif

#ifdef WSAEREMOTE
  NODE_DEFINE_CONSTANT(target, WSAEREMOTE);
#endif

#ifdef WSASYSNOTREADY
  NODE_DEFINE_CONSTANT(target, WSASYSNOTREADY);
#endif

#ifdef WSAVERNOTSUPPORTED
  NODE_DEFINE_CONSTANT(target, WSAVERNOTSUPPORTED);
#endif

#ifdef WSANOTINITIALISED
  NODE_DEFINE_CONSTANT(target, WSANOTINITIALISED);
#endif

#ifdef WSAEDISCON
  NODE_DEFINE_CONSTANT(target, WSAEDISCON);
#endif

#ifdef WSAENOMORE
  NODE_DEFINE_CONSTANT(target, WSAENOMORE);
#endif

#ifdef WSAECANCELLED
  NODE_DEFINE_CONSTANT(target, WSAECANCELLED);
#endif

#ifdef WSAEINVALIDPROCTABLE
  NODE_DEFINE_CONSTANT(target, WSAEINVALIDPROCTABLE);
#endif

#ifdef WSAEINVALIDPROVIDER
  NODE_DEFINE_CONSTANT(target, WSAEINVALIDPROVIDER);
#endif

#ifdef WSAEPROVIDERFAILEDINIT
  NODE_DEFINE_CONSTANT(target, WSAEPROVIDERFAILEDINIT);
#endif

#ifdef WSASYSCALLFAILURE
  NODE_DEFINE_CONSTANT(target, WSASYSCALLFAILURE);
#endif

#ifdef WSASERVICE_NOT_FOUND
  NODE_DEFINE_CONSTANT(target, WSASERVICE_NOT_FOUND);
#endif

#ifdef WSATYPE_NOT_FOUND
  NODE_DEFINE_CONSTANT(target, WSATYPE_NOT_FOUND);
#endif

#ifdef WSA_E_NO_MORE
  NODE_DEFINE_CONSTANT(target, WSA_E_NO_MORE);
#endif

#ifdef WSA_E_CANCELLED
  NODE_DEFINE_CONSTANT(target, WSA_E_CANCELLED);
#endif

#ifdef WSAEREFUSED
  NODE_DEFINE_CONSTANT(target, WSAEREFUSED);
#endif
}

void DefineSignalConstants(Local<Object> target) {
#ifdef SIGHUP
  NODE_DEFINE_CONSTANT(target, SIGHUP);
#endif

#ifdef SIGINT
  NODE_DEFINE_CONSTANT(target, SIGINT);
#endif

#ifdef SIGQUIT
  NODE_DEFINE_CONSTANT(target, SIGQUIT);
#endif

#ifdef SIGILL
  NODE_DEFINE_CONSTANT(target, SIGILL);
#endif

#ifdef SIGTRAP
  NODE_DEFINE_CONSTANT(target, SIGTRAP);
#endif

#ifdef SIGABRT
  NODE_DEFINE_CONSTANT(target, SIGABRT);
#endif

#ifdef SIGIOT
  NODE_DEFINE_CONSTANT(target, SIGIOT);
#endif

#ifdef SIGBUS
  NODE_DEFINE_CONSTANT(target, SIGBUS);
#endif

#ifdef SIGFPE
  NODE_DEFINE_CONSTANT(target, SIGFPE);
#endif

#ifdef SIGKILL
  NODE_DEFINE_CONSTANT(target, SIGKILL);
#endif

#ifdef SIGUSR1
  NODE_DEFINE_CONSTANT(target, SIGUSR1);
#endif

#ifdef SIGSEGV
  NODE_DEFINE_CONSTANT(target, SIGSEGV);
#endif

#ifdef SIGUSR2
  NODE_DEFINE_CONSTANT(target, SIGUSR2);
#endif

#ifdef SIGPIPE
  NODE_DEFINE_CONSTANT(target, SIGPIPE);
#endif

#ifdef SIGALRM
  NODE_DEFINE_CONSTANT(target, SIGALRM);
#endif

  NODE_DEFINE_CONSTANT(target, SIGTERM);

#ifdef SIGCHLD
  NODE_DEFINE_CONSTANT(target, SIGCHLD);
#endif

#ifdef SIGSTKFLT
  NODE_DEFINE_CONSTANT(target, SIGSTKFLT);
#endif


#ifdef SIGCONT
  NODE_DEFINE_CONSTANT(target, SIGCONT);
#endif

#ifdef SIGSTOP
  NODE_DEFINE_CONSTANT(target, SIGSTOP);
#endif

#ifdef SIGTSTP
  NODE_DEFINE_CONSTANT(target, SIGTSTP);
#endif

#ifdef SIGBREAK
  NODE_DEFINE_CONSTANT(target, SIGBREAK);
#endif

#ifdef SIGTTIN
  NODE_DEFINE_CONSTANT(target, SIGTTIN);
#endif

#ifdef SIGTTOU
  NODE_DEFINE_CONSTANT(target, SIGTTOU);
#endif

#ifdef SIGURG
  NODE_DEFINE_CONSTANT(target, SIGURG);
#endif

#ifdef SIGXCPU
  NODE_DEFINE_CONSTANT(target, SIGXCPU);
#endif

#ifdef SIGXFSZ
  NODE_DEFINE_CONSTANT(target, SIGXFSZ);
#endif

#ifdef SIGVTALRM
  NODE_DEFINE_CONSTANT(target, SIGVTALRM);
#endif

#ifdef SIGPROF
  NODE_DEFINE_CONSTANT(target, SIGPROF);
#endif

#ifdef SIGWINCH
  NODE_DEFINE_CONSTANT(target, SIGWINCH);
#endif

#ifdef SIGIO
  NODE_DEFINE_CONSTANT(target, SIGIO);
#endif

#ifdef SIGPOLL
  NODE_DEFINE_CONSTANT(target, SIGPOLL);
#endif

#ifdef SIGLOST
  NODE_DEFINE_CONSTANT(target, SIGLOST);
#endif

#ifdef SIGPWR
  NODE_DEFINE_CONSTANT(target, SIGPWR);
#endif

#ifdef SIGINFO
  NODE_DEFINE_CONSTANT(target, SIGINFO);
#endif

#ifdef SIGSYS
  NODE_DEFINE_CONSTANT(target, SIGSYS);
#endif

#ifdef SIGUNUSED
  NODE_DEFINE_CONSTANT(target, SIGUNUSED);
#endif
}

void DefinePriorityConstants(Local<Object> target) {
#ifdef UV_PRIORITY_LOW
# define PRIORITY_LOW UV_PRIORITY_LOW
  NODE_DEFINE_CONSTANT(target, PRIORITY_LOW);
# undef PRIORITY_LOW
#endif

#ifdef UV_PRIORITY_BELOW_NORMAL
# define PRIORITY_BELOW_NORMAL UV_PRIORITY_BELOW_NORMAL
  NODE_DEFINE_CONSTANT(target, PRIORITY_BELOW_NORMAL);
# undef PRIORITY_BELOW_NORMAL
#endif

#ifdef UV_PRIORITY_NORMAL
# define PRIORITY_NORMAL UV_PRIORITY_NORMAL
  NODE_DEFINE_CONSTANT(target, PRIORITY_NORMAL);
# undef PRIORITY_NORMAL
#endif

#ifdef UV_PRIORITY_ABOVE_NORMAL
# define PRIORITY_ABOVE_NORMAL UV_PRIORITY_ABOVE_NORMAL
  NODE_DEFINE_CONSTANT(target, PRIORITY_ABOVE_NORMAL);
# undef PRIORITY_ABOVE_NORMAL
#endif

#ifdef UV_PRIORITY_HIGH
# define PRIORITY_HIGH UV_PRIORITY_HIGH
  NODE_DEFINE_CONSTANT(target, PRIORITY_HIGH);
# undef PRIORITY_HIGH
#endif

#ifdef UV_PRIORITY_HIGHEST
# define PRIORITY_HIGHEST UV_PRIORITY_HIGHEST
  NODE_DEFINE_CONSTANT(target, PRIORITY_HIGHEST);
# undef PRIORITY_HIGHEST
#endif
}

void DefineCryptoConstants(Local<Object> target) {
#ifdef OPENSSL_VERSION_NUMBER
    NODE_DEFINE_CONSTANT(target, OPENSSL_VERSION_NUMBER);
#endif

#ifdef SSL_OP_ALL
    NODE_DEFINE_CONSTANT(target, SSL_OP_ALL);
#endif

#ifdef SSL_OP_ALLOW_NO_DHE_KEX
    NODE_DEFINE_CONSTANT(target, SSL_OP_ALLOW_NO_DHE_KEX);
#endif

#ifdef SSL_OP_ALLOW_UNSAFE_LEGACY_RENEGOTIATION
    NODE_DEFINE_CONSTANT(target, SSL_OP_ALLOW_UNSAFE_LEGACY_RENEGOTIATION);
#endif

#ifdef SSL_OP_CIPHER_SERVER_PREFERENCE
    NODE_DEFINE_CONSTANT(target, SSL_OP_CIPHER_SERVER_PREFERENCE);
#endif

#ifdef SSL_OP_CISCO_ANYCONNECT
    NODE_DEFINE_CONSTANT(target, SSL_OP_CISCO_ANYCONNECT);
#endif

#ifdef SSL_OP_COOKIE_EXCHANGE
    NODE_DEFINE_CONSTANT(target, SSL_OP_COOKIE_EXCHANGE);
#endif

#ifdef SSL_OP_CRYPTOPRO_TLSEXT_BUG
    NODE_DEFINE_CONSTANT(target, SSL_OP_CRYPTOPRO_TLSEXT_BUG);
#endif

#ifdef SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS
    NODE_DEFINE_CONSTANT(target, SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS);
#endif

#ifdef SSL_OP_LEGACY_SERVER_CONNECT
    NODE_DEFINE_CONSTANT(target, SSL_OP_LEGACY_SERVER_CONNECT);
#endif

#ifdef SSL_OP_NO_COMPRESSION
    NODE_DEFINE_CONSTANT(target, SSL_OP_NO_COMPRESSION);
#endif

#ifdef SSL_OP_NO_ENCRYPT_THEN_MAC
    NODE_DEFINE_CONSTANT(target, SSL_OP_NO_ENCRYPT_THEN_MAC);
#endif

#ifdef SSL_OP_NO_QUERY_MTU
    NODE_DEFINE_CONSTANT(target, SSL_OP_NO_QUERY_MTU);
#endif

#ifdef SSL_OP_NO_RENEGOTIATION
    NODE_DEFINE_CONSTANT(target, SSL_OP_NO_RENEGOTIATION);
#endif

#ifdef SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION
    NODE_DEFINE_CONSTANT(target, SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION);
#endif

#ifdef SSL_OP_NO_SSLv2
    NODE_DEFINE_CONSTANT(target, SSL_OP_NO_SSLv2);
#endif

#ifdef SSL_OP_NO_SSLv3
    NODE_DEFINE_CONSTANT(target, SSL_OP_NO_SSLv3);
#endif

#ifdef SSL_OP_NO_TICKET
    NODE_DEFINE_CONSTANT(target, SSL_OP_NO_TICKET);
#endif

#ifdef SSL_OP_NO_TLSv1
    NODE_DEFINE_CONSTANT(target, SSL_OP_NO_TLSv1);
#endif

#ifdef SSL_OP_NO_TLSv1_1
    NODE_DEFINE_CONSTANT(target, SSL_OP_NO_TLSv1_1);
#endif

#ifdef SSL_OP_NO_TLSv1_2
    NODE_DEFINE_CONSTANT(target, SSL_OP_NO_TLSv1_2);
#endif

#ifdef SSL_OP_NO_TLSv1_3
    NODE_DEFINE_CONSTANT(target, SSL_OP_NO_TLSv1_3);
#endif

#ifdef SSL_OP_PRIORITIZE_CHACHA
    NODE_DEFINE_CONSTANT(target, SSL_OP_PRIORITIZE_CHACHA);
#endif

#ifdef SSL_OP_TLS_ROLLBACK_BUG
    NODE_DEFINE_CONSTANT(target, SSL_OP_TLS_ROLLBACK_BUG);
#endif

# ifndef OPENSSL_NO_ENGINE

# ifdef ENGINE_METHOD_RSA
    NODE_DEFINE_CONSTANT(target, ENGINE_METHOD_RSA);
# endif

# ifdef ENGINE_METHOD_DSA
    NODE_DEFINE_CONSTANT(target, ENGINE_METHOD_DSA);
# endif

# ifdef ENGINE_METHOD_DH
    NODE_DEFINE_CONSTANT(target, ENGINE_METHOD_DH);
# endif

# ifdef ENGINE_METHOD_RAND
    NODE_DEFINE_CONSTANT(target, ENGINE_METHOD_RAND);
# endif

# ifdef ENGINE_METHOD_EC
    NODE_DEFINE_CONSTANT(target, ENGINE_METHOD_EC);
# endif

# ifdef ENGINE_METHOD_CIPHERS
    NODE_DEFINE_CONSTANT(target, ENGINE_METHOD_CIPHERS);
# endif

# ifdef ENGINE_METHOD_DIGESTS
    NODE_DEFINE_CONSTANT(target, ENGINE_METHOD_DIGESTS);
# endif

# ifdef ENGINE_METHOD_PKEY_METHS
    NODE_DEFINE_CONSTANT(target, ENGINE_METHOD_PKEY_METHS);
# endif

# ifdef ENGINE_METHOD_PKEY_ASN1_METHS
    NODE_DEFINE_CONSTANT(target, ENGINE_METHOD_PKEY_ASN1_METHS);
# endif

# ifdef ENGINE_METHOD_ALL
    NODE_DEFINE_CONSTANT(target, ENGINE_METHOD_ALL);
# endif

# ifdef ENGINE_METHOD_NONE
    NODE_DEFINE_CONSTANT(target, ENGINE_METHOD_NONE);
# endif

# endif  // !OPENSSL_NO_ENGINE

#ifdef DH_CHECK_P_NOT_SAFE_PRIME
    NODE_DEFINE_CONSTANT(target, DH_CHECK_P_NOT_SAFE_PRIME);
#endif

#ifdef DH_CHECK_P_NOT_PRIME
    NODE_DEFINE_CONSTANT(target, DH_CHECK_P_NOT_PRIME);
#endif

#ifdef DH_UNABLE_TO_CHECK_GENERATOR
    NODE_DEFINE_CONSTANT(target, DH_UNABLE_TO_CHECK_GENERATOR);
#endif

#ifdef DH_NOT_SUITABLE_GENERATOR
    NODE_DEFINE_CONSTANT(target, DH_NOT_SUITABLE_GENERATOR);
#endif

#ifdef RSA_PKCS1_PADDING
    NODE_DEFINE_CONSTANT(target, RSA_PKCS1_PADDING);
#endif

#ifdef RSA_SSLV23_PADDING
    NODE_DEFINE_CONSTANT(target, RSA_SSLV23_PADDING);
#endif

#ifdef RSA_NO_PADDING
    NODE_DEFINE_CONSTANT(target, RSA_NO_PADDING);
#endif

#ifdef RSA_PKCS1_OAEP_PADDING
    NODE_DEFINE_CONSTANT(target, RSA_PKCS1_OAEP_PADDING);
#endif

#ifdef RSA_X931_PADDING
    NODE_DEFINE_CONSTANT(target, RSA_X931_PADDING);
#endif

#ifdef RSA_PKCS1_PSS_PADDING
    NODE_DEFINE_CONSTANT(target, RSA_PKCS1_PSS_PADDING);
#endif

#ifdef RSA_PSS_SALTLEN_DIGEST
    NODE_DEFINE_CONSTANT(target, RSA_PSS_SALTLEN_DIGEST);
#endif

#ifdef RSA_PSS_SALTLEN_MAX_SIGN
    NODE_DEFINE_CONSTANT(target, RSA_PSS_SALTLEN_MAX_SIGN);
#endif

#ifdef RSA_PSS_SALTLEN_AUTO
    NODE_DEFINE_CONSTANT(target, RSA_PSS_SALTLEN_AUTO);
#endif

#ifdef DEFAULT_CIPHER_LIST_CORE
  NODE_DEFINE_STRING_CONSTANT(target,
                              "defaultCoreCipherList",
                              DEFAULT_CIPHER_LIST_CORE);
#endif

#ifdef TLS1_VERSION
  NODE_DEFINE_CONSTANT(target, TLS1_VERSION);
#endif

#ifdef TLS1_1_VERSION
  NODE_DEFINE_CONSTANT(target, TLS1_1_VERSION);
#endif

#ifdef TLS1_2_VERSION
  NODE_DEFINE_CONSTANT(target, TLS1_2_VERSION);
#endif

#ifdef TLS1_3_VERSION
  NODE_DEFINE_CONSTANT(target, TLS1_3_VERSION);
#endif

#if HAVE_OPENSSL
  // NOTE: These are not defines
  NODE_DEFINE_CONSTANT(target, POINT_CONVERSION_COMPRESSED);

  NODE_DEFINE_CONSTANT(target, POINT_CONVERSION_UNCOMPRESSED);

  NODE_DEFINE_CONSTANT(target, POINT_CONVERSION_HYBRID);
#endif
}

void DefineSystemConstants(Local<Object> target) {
  NODE_DEFINE_CONSTANT(target, UV_FS_SYMLINK_DIR);
  NODE_DEFINE_CONSTANT(target, UV_FS_SYMLINK_JUNCTION);
  // file access modes
  NODE_DEFINE_CONSTANT(target, O_RDONLY);
  NODE_DEFINE_CONSTANT(target, O_WRONLY);
  NODE_DEFINE_CONSTANT(target, O_RDWR);

  // file types from readdir
  NODE_DEFINE_CONSTANT(target, UV_DIRENT_UNKNOWN);
  NODE_DEFINE_CONSTANT(target, UV_DIRENT_FILE);
  NODE_DEFINE_CONSTANT(target, UV_DIRENT_DIR);
  NODE_DEFINE_CONSTANT(target, UV_DIRENT_LINK);
  NODE_DEFINE_CONSTANT(target, UV_DIRENT_FIFO);
  NODE_DEFINE_CONSTANT(target, UV_DIRENT_SOCKET);
  NODE_DEFINE_CONSTANT(target, UV_DIRENT_CHAR);
  NODE_DEFINE_CONSTANT(target, UV_DIRENT_BLOCK);

  // Define module specific constants
  NODE_DEFINE_CONSTANT(target, EXTENSIONLESS_FORMAT_JAVASCRIPT);
  NODE_DEFINE_CONSTANT(target, EXTENSIONLESS_FORMAT_WASM);

  NODE_DEFINE_CONSTANT(target, S_IFMT);
  NODE_DEFINE_CONSTANT(target, S_IFREG);
  NODE_DEFINE_CONSTANT(target, S_IFDIR);
  NODE_DEFINE_CONSTANT(target, S_IFCHR);
#ifdef S_IFBLK
  NODE_DEFINE_CONSTANT(target, S_IFBLK);
#endif

#ifdef S_IFIFO
  NODE_DEFINE_CONSTANT(target, S_IFIFO);
#endif

#ifdef S_IFLNK
  NODE_DEFINE_CONSTANT(target, S_IFLNK);
#endif

#ifdef S_IFSOCK
  NODE_DEFINE_CONSTANT(target, S_IFSOCK);
#endif

#ifdef O_CREAT
  NODE_DEFINE_CONSTANT(target, O_CREAT);
#endif

#ifdef O_EXCL
  NODE_DEFINE_CONSTANT(target, O_EXCL);
#endif

NODE_DEFINE_CONSTANT(target, UV_FS_O_FILEMAP);

#ifdef O_NOCTTY
  NODE_DEFINE_CONSTANT(target, O_NOCTTY);
#endif

#ifdef O_TRUNC
  NODE_DEFINE_CONSTANT(target, O_TRUNC);
#endif

#ifdef O_APPEND
  NODE_DEFINE_CONSTANT(target, O_APPEND);
#endif

#ifdef O_DIRECTORY
  NODE_DEFINE_CONSTANT(target, O_DIRECTORY);
#endif

#ifdef O_EXCL
  NODE_DEFINE_CONSTANT(target, O_EXCL);
#endif

#ifdef O_NOATIME
  NODE_DEFINE_CONSTANT(target, O_NOATIME);
#endif

#ifdef O_NOFOLLOW
  NODE_DEFINE_CONSTANT(target, O_NOFOLLOW);
#endif

#ifdef O_SYNC
  NODE_DEFINE_CONSTANT(target, O_SYNC);
#endif

#ifdef O_DSYNC
  NODE_DEFINE_CONSTANT(target, O_DSYNC);
#endif


#ifdef O_SYMLINK
  NODE_DEFINE_CONSTANT(target, O_SYMLINK);
#endif

#ifdef O_DIRECT
  NODE_DEFINE_CONSTANT(target, O_DIRECT);
#endif

#ifdef O_NONBLOCK
  NODE_DEFINE_CONSTANT(target, O_NONBLOCK);
#endif

#ifdef S_IRWXU
  NODE_DEFINE_CONSTANT(target, S_IRWXU);
#endif

#ifdef S_IRUSR
  NODE_DEFINE_CONSTANT(target, S_IRUSR);
#endif

#ifdef S_IWUSR
  NODE_DEFINE_CONSTANT(target, S_IWUSR);
#endif

#ifdef S_IXUSR
  NODE_DEFINE_CONSTANT(target, S_IXUSR);
#endif

#ifdef S_IRWXG
  NODE_DEFINE_CONSTANT(target, S_IRWXG);
#endif

#ifdef S_IRGRP
  NODE_DEFINE_CONSTANT(target, S_IRGRP);
#endif

#ifdef S_IWGRP
  NODE_DEFINE_CONSTANT(target, S_IWGRP);
#endif

#ifdef S_IXGRP
  NODE_DEFINE_CONSTANT(target, S_IXGRP);
#endif

#ifdef S_IRWXO
  NODE_DEFINE_CONSTANT(target, S_IRWXO);
#endif

#ifdef S_IROTH
  NODE_DEFINE_CONSTANT(target, S_IROTH);
#endif

#ifdef S_IWOTH
  NODE_DEFINE_CONSTANT(target, S_IWOTH);
#endif

#ifdef S_IXOTH
  NODE_DEFINE_CONSTANT(target, S_IXOTH);
#endif

#ifdef F_OK
  NODE_DEFINE_CONSTANT(target, F_OK);
#endif

#ifdef R_OK
  NODE_DEFINE_CONSTANT(target, R_OK);
#endif

#ifdef W_OK
  NODE_DEFINE_CONSTANT(target, W_OK);
#endif

#ifdef X_OK
  NODE_DEFINE_CONSTANT(target, X_OK);
#endif

#ifdef UV_FS_COPYFILE_EXCL
# define COPYFILE_EXCL UV_FS_COPYFILE_EXCL
  NODE_DEFINE_CONSTANT(target, UV_FS_COPYFILE_EXCL);
  NODE_DEFINE_CONSTANT(target, COPYFILE_EXCL);
# undef COPYFILE_EXCL
#endif

#ifdef UV_FS_COPYFILE_FICLONE
# define COPYFILE_FICLONE UV_FS_COPYFILE_FICLONE
  NODE_DEFINE_CONSTANT(target, UV_FS_COPYFILE_FICLONE);
  NODE_DEFINE_CONSTANT(target, COPYFILE_FICLONE);
# undef COPYFILE_FICLONE
#endif

#ifdef UV_FS_COPYFILE_FICLONE_FORCE
# define COPYFILE_FICLONE_FORCE UV_FS_COPYFILE_FICLONE_FORCE
  NODE_DEFINE_CONSTANT(target, UV_FS_COPYFILE_FICLONE_FORCE);
  NODE_DEFINE_CONSTANT(target, COPYFILE_FICLONE_FORCE);
# undef COPYFILE_FICLONE_FORCE
#endif
}

void DefineDLOpenConstants(Local<Object> target) {
#ifdef RTLD_LAZY
  NODE_DEFINE_CONSTANT(target, RTLD_LAZY);
#endif

#ifdef RTLD_NOW
  NODE_DEFINE_CONSTANT(target, RTLD_NOW);
#endif

#ifdef RTLD_GLOBAL
  NODE_DEFINE_CONSTANT(target, RTLD_GLOBAL);
#endif

#ifdef RTLD_LOCAL
  NODE_DEFINE_CONSTANT(target, RTLD_LOCAL);
#endif

#ifdef RTLD_DEEPBIND
  NODE_DEFINE_CONSTANT(target, RTLD_DEEPBIND);
#endif
}

void DefineTraceConstants(Local<Object> target) {
  NODE_DEFINE_CONSTANT(target, TRACE_EVENT_PHASE_BEGIN);
  NODE_DEFINE_CONSTANT(target, TRACE_EVENT_PHASE_END);
  NODE_DEFINE_CONSTANT(target, TRACE_EVENT_PHASE_COMPLETE);
  NODE_DEFINE_CONSTANT(target, TRACE_EVENT_PHASE_INSTANT);
  NODE_DEFINE_CONSTANT(target, TRACE_EVENT_PHASE_ASYNC_BEGIN);
  NODE_DEFINE_CONSTANT(target, TRACE_EVENT_PHASE_ASYNC_STEP_INTO);
  NODE_DEFINE_CONSTANT(target, TRACE_EVENT_PHASE_ASYNC_STEP_PAST);
  NODE_DEFINE_CONSTANT(target, TRACE_EVENT_PHASE_ASYNC_END);
  NODE_DEFINE_CONSTANT(target, TRACE_EVENT_PHASE_NESTABLE_ASYNC_BEGIN);
  NODE_DEFINE_CONSTANT(target, TRACE_EVENT_PHASE_NESTABLE_ASYNC_END);
  NODE_DEFINE_CONSTANT(target, TRACE_EVENT_PHASE_NESTABLE_ASYNC_INSTANT);
  NODE_DEFINE_CONSTANT(target, TRACE_EVENT_PHASE_FLOW_BEGIN);
  NODE_DEFINE_CONSTANT(target, TRACE_EVENT_PHASE_FLOW_STEP);
  NODE_DEFINE_CONSTANT(target, TRACE_EVENT_PHASE_FLOW_END);
  NODE_DEFINE_CONSTANT(target, TRACE_EVENT_PHASE_METADATA);
  NODE_DEFINE_CONSTANT(target, TRACE_EVENT_PHASE_COUNTER);
  NODE_DEFINE_CONSTANT(target, TRACE_EVENT_PHASE_SAMPLE);
  NODE_DEFINE_CONSTANT(target, TRACE_EVENT_PHASE_CREATE_OBJECT);
  NODE_DEFINE_CONSTANT(target, TRACE_EVENT_PHASE_SNAPSHOT_OBJECT);
  NODE_DEFINE_CONSTANT(target, TRACE_EVENT_PHASE_DELETE_OBJECT);
  NODE_DEFINE_CONSTANT(target, TRACE_EVENT_PHASE_MEMORY_DUMP);
  NODE_DEFINE_CONSTANT(target, TRACE_EVENT_PHASE_MARK);
  NODE_DEFINE_CONSTANT(target, TRACE_EVENT_PHASE_CLOCK_SYNC);
  NODE_DEFINE_CONSTANT(target, TRACE_EVENT_PHASE_ENTER_CONTEXT);
  NODE_DEFINE_CONSTANT(target, TRACE_EVENT_PHASE_LEAVE_CONTEXT);
  NODE_DEFINE_CONSTANT(target, TRACE_EVENT_PHASE_LINK_IDS);
}

void CreatePerContextProperties(Local<Object> target,
                                Local<Value> unused,
                                Local<Context> context,
                                void* priv) {
  Isolate* isolate = context->GetIsolate();
  Environment* env = Environment::GetCurrent(context);

  CHECK(
      target->SetPrototypeV2(env->context(), Null(env->isolate())).FromJust());

  Local<Object> os_constants = Object::New(isolate);
  CHECK(os_constants->SetPrototypeV2(env->context(), Null(env->isolate()))
            .FromJust());

  Local<Object> err_constants = Object::New(isolate);
  CHECK(err_constants->SetPrototypeV2(env->context(), Null(env->isolate()))
            .FromJust());

  Local<Object> sig_constants = Object::New(isolate);
  CHECK(sig_constants->SetPrototypeV2(env->context(), Null(env->isolate()))
            .FromJust());

  Local<Object> priority_constants = Object::New(isolate);
  CHECK(priority_constants->SetPrototypeV2(env->context(), Null(env->isolate()))
            .FromJust());

  Local<Object> fs_constants = Object::New(isolate);
  CHECK(fs_constants->SetPrototypeV2(env->context(), Null(env->isolate()))
            .FromJust());

  Local<Object> crypto_constants = Object::New(isolate);
  CHECK(crypto_constants->SetPrototypeV2(env->context(), Null(env->isolate()))
            .FromJust());

  Local<Object> zlib_constants = Object::New(isolate);
  CHECK(zlib_constants->SetPrototypeV2(env->context(), Null(env->isolate()))
            .FromJust());

  Local<Object> dlopen_constants = Object::New(isolate);
  CHECK(dlopen_constants->SetPrototypeV2(env->context(), Null(env->isolate()))
            .FromJust());

  Local<Object> trace_constants = Object::New(isolate);
  CHECK(trace_constants->SetPrototypeV2(env->context(), Null(env->isolate()))
            .FromJust());

  DefineErrnoConstants(err_constants);
  DefineWindowsErrorConstants(err_constants);
  DefineSignalConstants(sig_constants);
  DefinePriorityConstants(priority_constants);
  DefineSystemConstants(fs_constants);
  DefineCryptoConstants(crypto_constants);
  DefineZlibConstants(zlib_constants);
  DefineDLOpenConstants(dlopen_constants);
  DefineTraceConstants(trace_constants);

  // Define libuv constants.
  NODE_DEFINE_CONSTANT(os_constants, UV_UDP_REUSEADDR);

  os_constants
      ->Set(env->context(),
            FIXED_ONE_BYTE_STRING(isolate, "dlopen"),
            dlopen_constants)
      .Check();
  os_constants
      ->Set(env->context(),
            FIXED_ONE_BYTE_STRING(isolate, "errno"),
            err_constants)
      .Check();
  os_constants
      ->Set(env->context(),
            FIXED_ONE_BYTE_STRING(isolate, "signals"),
            sig_constants)
      .Check();
  os_constants
      ->Set(env->context(),
            FIXED_ONE_BYTE_STRING(isolate, "priority"),
            priority_constants)
      .Check();
  target
      ->Set(env->context(), FIXED_ONE_BYTE_STRING(isolate, "os"), os_constants)
      .Check();
  target
      ->Set(env->context(), FIXED_ONE_BYTE_STRING(isolate, "fs"), fs_constants)
      .Check();
  target
      ->Set(env->context(),
            FIXED_ONE_BYTE_STRING(isolate, "crypto"),
            crypto_constants)
      .Check();
  target
      ->Set(env->context(),
            FIXED_ONE_BYTE_STRING(isolate, "zlib"),
            zlib_constants)
      .Check();
  target
      ->Set(env->context(),
            FIXED_ONE_BYTE_STRING(isolate, "trace"),
            trace_constants)
      .Check();
}

}  // namespace constants
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(constants,
                                    node::constants::CreatePerContextProperties)
