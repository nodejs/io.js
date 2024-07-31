/* src/lib/ares_config.h.  Generated from ares_config.h.in by configure.  */
/* src/lib/ares_config.h.in.  Generated from configure.ac by autoheader.  */

/* a suitable file/device to read random data from */
#define CARES_RANDOM_FILE "/dev/urandom"

/* Set to 1 if non-pubilc shared library symbols are hidden */
#define CARES_SYMBOL_HIDING 1 

/* Threading enabled */
#define CARES_THREADS 1 

/* the signed version of size_t */
#define CARES_TYPEOF_ARES_SSIZE_T ssize_t

/* Use resolver library to configure cares */
/* #undef CARES_USE_LIBRESOLV */

/* if a /etc/inet dir is being used */
/* #undef ETC_INET */

/* gethostname() arg2 type */
#define GETHOSTNAME_TYPE_ARG2 size_t 

/* getnameinfo() arg1 type */
#define GETNAMEINFO_TYPE_ARG1 struct sockaddr * 

/* getnameinfo() arg2 type */
#define GETNAMEINFO_TYPE_ARG2 socklen_t 

/* getnameinfo() arg4 and 6 type */
#define GETNAMEINFO_TYPE_ARG46 socklen_t 

/* getnameinfo() arg7 type */
#define GETNAMEINFO_TYPE_ARG7 int 

/* number of arguments for getservbyname_r() */
/* #undef GETSERVBYNAME_R_ARGS */

/* number of arguments for getservbyport_r() */
#define GETSERVBYPORT_R_ARGS 6 

/* Define to 1 if you have AF_INET6 */
#define HAVE_AF_INET6 1

/* Define to 1 if you have `arc4random_buf` */
#define HAVE_ARC4RANDOM_BUF 1

/* Define to 1 if you have the <arpa/inet.h> header file. */
#define HAVE_ARPA_INET_H 1

/* Define to 1 if you have the <arpa/nameser_compat.h> header file. */
#define HAVE_ARPA_NAMESER_COMPAT_H 1

/* Define to 1 if you have the <arpa/nameser.h> header file. */
#define HAVE_ARPA_NAMESER_H 1

/* Define to 1 if you have the <assert.h> header file. */
#define HAVE_ASSERT_H 1

/* Define to 1 if you have the <AvailabilityMacros.h> header file. */
/* #undef HAVE_AVAILABILITYMACROS_H */

/* Define to 1 if you have `clock_gettime` */
#define HAVE_CLOCK_GETTIME 1

/* clock_gettime() with CLOCK_MONOTONIC support */
#define HAVE_CLOCK_GETTIME_MONOTONIC 1 

/* Define to 1 if you have `closesocket` */
/* #undef HAVE_CLOSESOCKET */

/* Define to 1 if you have `CloseSocket` */
/* #undef HAVE_CLOSESOCKET_CAMEL */

/* Define to 1 if you have `connect` */
#define HAVE_CONNECT 1

/* Define to 1 if you have `ConvertInterfaceIndexToLuid` */
/* #undef HAVE_CONVERTINTERFACEINDEXTOLUID */

/* Define to 1 if you have `ConvertInterfaceLuidToNameA` */
/* #undef HAVE_CONVERTINTERFACELUIDTONAMEA */

/* define if the compiler supports basic C++14 syntax */
#define HAVE_CXX14 1

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define to 1 if you have `epoll_{create1,ctl,wait}` */
/* #undef HAVE_EPOLL */

/* Define to 1 if you have the <errno.h> header file. */
#define HAVE_ERRNO_H 1

/* Define to 1 if you have `fcntl` */
#define HAVE_FCNTL 1

/* Define to 1 if you have the <fcntl.h> header file. */
#define HAVE_FCNTL_H 1

/* fcntl() with O_NONBLOCK support */
#define HAVE_FCNTL_O_NONBLOCK 1 

/* Define to 1 if you have `getenv` */
#define HAVE_GETENV 1

/* Define to 1 if you have `gethostname` */
#define HAVE_GETHOSTNAME 1

/* Define to 1 if you have `getifaddrs` */
#define HAVE_GETIFADDRS 1

/* Define to 1 if you have `getnameinfo` */
#define HAVE_GETNAMEINFO 1

/* Define to 1 if you have `getrandom` */
#define HAVE_GETRANDOM 1

/* Define to 1 if you have `getservbyport_r` */
#define HAVE_GETSERVBYPORT_R 1

/* Define to 1 if you have `gettimeofday` */
#define HAVE_GETTIMEOFDAY 1

/* Define to 1 if you have the <ifaddrs.h> header file. */
#define HAVE_IFADDRS_H 1

/* Define to 1 if you have `if_indextoname` */
#define HAVE_IF_INDEXTONAME 1

/* Define to 1 if you have `if_nametoindex` */
#define HAVE_IF_NAMETOINDEX 1

/* Define to 1 if you have `inet_net_pton` */
#define HAVE_INET_NET_PTON 1

/* Define to 1 if you have `inet_ntop` */
#define HAVE_INET_NTOP 1

/* Define to 1 if you have `inet_pton` */
#define HAVE_INET_PTON 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have `ioctl` */
#define HAVE_IOCTL 1

/* Define to 1 if you have `ioctlsocket` */
/* #undef HAVE_IOCTLSOCKET */

/* Define to 1 if you have `IoctlSocket` */
/* #undef HAVE_IOCTLSOCKET_CAMEL */

/* ioctlsocket() with FIONBIO support */
/* #undef HAVE_IOCTLSOCKET_FIONBIO */

/* ioctl() with FIONBIO support */
#define HAVE_IOCTL_FIONBIO 1 

/* Define to 1 if you have the <iphlpapi.h> header file. */
/* #undef HAVE_IPHLPAPI_H */

/* Define to 1 if you have `kqueue` */
#define HAVE_KQUEUE 1

/* Define to 1 if you have the <limits.h> header file. */
#define HAVE_LIMITS_H 1

/* Define to 1 if the compiler supports the 'long long' data type. */
#define HAVE_LONGLONG 1

/* Define to 1 if you have the <malloc.h> header file. */
#define HAVE_MALLOC_H 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the <minix/config.h> header file. */
/* #undef HAVE_MINIX_CONFIG_H */

/* Define to 1 if you have the <mswsock.h> header file. */
/* #undef HAVE_MSWSOCK_H */

/* Define to 1 if you have the <netdb.h> header file. */
#define HAVE_NETDB_H 1

/* Define to 1 if you have the <netinet/in.h> header file. */
#define HAVE_NETINET_IN_H 1

/* Define to 1 if you have the <netinet/tcp.h> header file. */
#define HAVE_NETINET_TCP_H 1

/* Define to 1 if you have the <netioapi.h> header file. */
/* #undef HAVE_NETIOAPI_H */

/* Define to 1 if you have the <net/if.h> header file. */
#define HAVE_NET_IF_H 1

/* Define to 1 if you have the <ntdef.h> header file. */
/* #undef HAVE_NTDEF_H */

/* Define to 1 if you have the <ntstatus.h> header file. */
/* #undef HAVE_NTSTATUS_H */

/* Define to 1 if you have PF_INET6 */
#define HAVE_PF_INET6 1

/* Define to 1 if you have `pipe` */
#define HAVE_PIPE 1

/* Define to 1 if you have `pipe2` */
#define HAVE_PIPE2 1

/* Define to 1 if you have `poll` */
#define HAVE_POLL 1

/* Define to 1 if you have the <poll.h> header file. */
#define HAVE_POLL_H 1

/* Define to 1 if you have the <pthread.h> header file. */
#define HAVE_PTHREAD_H 1

/* Define to 1 if you have the <pthread_np.h> header file. */
#define HAVE_PTHREAD_NP_H 1

/* Have PTHREAD_PRIO_INHERIT. */
#define HAVE_PTHREAD_PRIO_INHERIT 1

/* Define to 1 if you have `recv` */
#define HAVE_RECV 1

/* Define to 1 if you have `recvfrom` */
#define HAVE_RECVFROM 1

/* Define to 1 if you have `send` */
#define HAVE_SEND 1

/* Define to 1 if you have `setsockopt` */
#define HAVE_SETSOCKOPT 1

/* setsockopt() with SO_NONBLOCK support */
/* #undef HAVE_SETSOCKOPT_SO_NONBLOCK */

/* Define to 1 if you have `socket` */
#define HAVE_SOCKET 1

/* Define to 1 if you have the <socket.h> header file. */
/* #undef HAVE_SOCKET_H */

/* socklen_t */
#define HAVE_SOCKLEN_T /**/

/* Define to 1 if you have `stat` */
#define HAVE_STAT 1

/* Define to 1 if you have the <stdbool.h> header file. */
#define HAVE_STDBOOL_H 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdio.h> header file. */
#define HAVE_STDIO_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have `strcasecmp` */
#define HAVE_STRCASECMP 1

/* Define to 1 if you have `strdup` */
#define HAVE_STRDUP 1

/* Define to 1 if you have `stricmp` */
/* #undef HAVE_STRICMP */

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have `strncasecmp` */
#define HAVE_STRNCASECMP 1

/* Define to 1 if you have `strncmpi` */
/* #undef HAVE_STRNCMPI */

/* Define to 1 if you have `strnicmp` */
/* #undef HAVE_STRNICMP */

/* Define to 1 if the system has the type `struct addrinfo'. */
#define HAVE_STRUCT_ADDRINFO 1

/* Define to 1 if `ai_flags' is a member of `struct addrinfo'. */
#define HAVE_STRUCT_ADDRINFO_AI_FLAGS 1

/* Define to 1 if the system has the type `struct in6_addr'. */
#define HAVE_STRUCT_IN6_ADDR 1

/* Define to 1 if the system has the type `struct sockaddr_in6'. */
#define HAVE_STRUCT_SOCKADDR_IN6 1

/* Define to 1 if `sin6_scope_id' is a member of `struct sockaddr_in6'. */
#define HAVE_STRUCT_SOCKADDR_IN6_SIN6_SCOPE_ID 1

/* Define to 1 if the system has the type `struct sockaddr_storage'. */
#define HAVE_STRUCT_SOCKADDR_STORAGE 1

/* Define to 1 if the system has the type `struct timeval'. */
#define HAVE_STRUCT_TIMEVAL 1

/* Define to 1 if you have the <sys/epoll.h> header file. */
/* #undef HAVE_SYS_EPOLL_H */

/* Define to 1 if you have the <sys/event.h> header file. */
#define HAVE_SYS_EVENT_H 1

/* Define to 1 if you have the <sys/filio.h> header file. */
#define HAVE_SYS_FILIO_H 1

/* Define to 1 if you have the <sys/ioctl.h> header file. */
#define HAVE_SYS_IOCTL_H 1

/* Define to 1 if you have the <sys/param.h> header file. */
#define HAVE_SYS_PARAM_H 1

/* Define to 1 if you have the <sys/random.h> header file. */
#define HAVE_SYS_RANDOM_H 1

/* Define to 1 if you have the <sys/select.h> header file. */
#define HAVE_SYS_SELECT_H 1

/* Define to 1 if you have the <sys/socket.h> header file. */
#define HAVE_SYS_SOCKET_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/time.h> header file. */
#define HAVE_SYS_TIME_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <sys/uio.h> header file. */
#define HAVE_SYS_UIO_H 1

/* Define to 1 if you have the <time.h> header file. */
#define HAVE_TIME_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Whether user namespaces are available */
/* #undef HAVE_USER_NAMESPACE */

/* Whether UTS namespaces are available */
/* #undef HAVE_UTS_NAMESPACE */

/* Define to 1 if you have the <wchar.h> header file. */
#define HAVE_WCHAR_H 1

/* Define to 1 if you have the <windows.h> header file. */
/* #undef HAVE_WINDOWS_H */

/* Define to 1 if you have the <winsock2.h> header file. */
/* #undef HAVE_WINSOCK2_H */

/* Define to 1 if you have the <winternl.h> header file. */
/* #undef HAVE_WINTERNL_H */

/* Define to 1 if you have `writev` */
#define HAVE_WRITEV 1

/* Define to 1 if you have the <ws2ipdef.h> header file. */
/* #undef HAVE_WS2IPDEF_H */

/* Define to 1 if you have the <ws2tcpip.h> header file. */
/* #undef HAVE_WS2TCPIP_H */

/* Define to 1 if you have `__system_property_get` */
/* #undef HAVE___SYSTEM_PROPERTY_GET */

/* Define to the sub-directory where libtool stores uninstalled libraries. */
#define LT_OBJDIR ".libs/"

/* Name of package */
#define PACKAGE "c-ares"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "c-ares mailing list: http://lists.haxx.se/listinfo/c-ares"

/* Define to the full name of this package. */
#define PACKAGE_NAME "c-ares"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "c-ares 1.26.0"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "c-ares"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "1.26.0"

/* Define to necessary symbol if this constant uses a non-standard name on
   your system. */
/* #undef PTHREAD_CREATE_JOINABLE */

/* recvfrom() arg5 qualifier */
#define RECVFROM_QUAL_ARG5 

/* recvfrom() arg1 type */
#define RECVFROM_TYPE_ARG1 int 

/* recvfrom() arg2 type */
#define RECVFROM_TYPE_ARG2 void * 

/* recvfrom() arg3 type */
#define RECVFROM_TYPE_ARG3 size_t 

/* recvfrom() arg4 type */
#define RECVFROM_TYPE_ARG4 int 

/* recvfrom() arg5 type */
#define RECVFROM_TYPE_ARG5 struct sockaddr * 

/* recvfrom() return value */
#define RECVFROM_TYPE_RETV ssize_t 

/* recv() arg1 type */
#define RECV_TYPE_ARG1 int 

/* recv() arg2 type */
#define RECV_TYPE_ARG2 void * 

/* recv() arg3 type */
#define RECV_TYPE_ARG3 size_t 

/* recv() arg4 type */
#define RECV_TYPE_ARG4 int 

/* recv() return value */
#define RECV_TYPE_RETV ssize_t 

/* send() arg2 qualifier */
#define SEND_QUAL_ARG2 

/* send() arg1 type */
#define SEND_TYPE_ARG1 int 

/* send() arg2 type */
#define SEND_TYPE_ARG2 void * 

/* send() arg3 type */
#define SEND_TYPE_ARG3 size_t 

/* send() arg4 type */
#define SEND_TYPE_ARG4 int 

/* send() return value */
#define SEND_TYPE_RETV ssize_t 

/* Define to 1 if all of the C90 standard headers exist (not just the ones
   required in a freestanding environment). This macro is provided for
   backward compatibility; new code need not use it. */
#define STDC_HEADERS 1

/* Enable extensions on AIX 3, Interix.  */
#ifndef _ALL_SOURCE
# define _ALL_SOURCE 1
#endif
/* Enable general extensions on macOS.  */
#ifndef _DARWIN_C_SOURCE
# define _DARWIN_C_SOURCE 1
#endif
/* Enable general extensions on Solaris.  */
#ifndef __EXTENSIONS__
# define __EXTENSIONS__ 1
#endif
/* Enable GNU extensions on systems that have them.  */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif
/* Enable X/Open compliant socket functions that do not require linking
   with -lxnet on HP-UX 11.11.  */
#ifndef _HPUX_ALT_XOPEN_SOCKET_API
# define _HPUX_ALT_XOPEN_SOCKET_API 1
#endif
/* Identify the host operating system as Minix.
   This macro does not affect the system headers' behavior.
   A future release of Autoconf may stop defining this macro.  */
#ifndef _MINIX
/* # undef _MINIX */
#endif
/* Enable general extensions on NetBSD.
   Enable NetBSD compatibility extensions on Minix.  */
#ifndef _NETBSD_SOURCE
# define _NETBSD_SOURCE 1
#endif
/* Enable OpenBSD compatibility extensions on NetBSD.
   Oddly enough, this does nothing on OpenBSD.  */
#ifndef _OPENBSD_SOURCE
# define _OPENBSD_SOURCE 1
#endif
/* Define to 1 if needed for POSIX-compatible behavior.  */
#ifndef _POSIX_SOURCE
/* # undef _POSIX_SOURCE */
#endif
/* Define to 2 if needed for POSIX-compatible behavior.  */
#ifndef _POSIX_1_SOURCE
/* # undef _POSIX_1_SOURCE */
#endif
/* Enable POSIX-compatible threading on Solaris.  */
#ifndef _POSIX_PTHREAD_SEMANTICS
# define _POSIX_PTHREAD_SEMANTICS 1
#endif
/* Enable extensions specified by ISO/IEC TS 18661-5:2014.  */
#ifndef __STDC_WANT_IEC_60559_ATTRIBS_EXT__
# define __STDC_WANT_IEC_60559_ATTRIBS_EXT__ 1
#endif
/* Enable extensions specified by ISO/IEC TS 18661-1:2014.  */
#ifndef __STDC_WANT_IEC_60559_BFP_EXT__
# define __STDC_WANT_IEC_60559_BFP_EXT__ 1
#endif
/* Enable extensions specified by ISO/IEC TS 18661-2:2015.  */
#ifndef __STDC_WANT_IEC_60559_DFP_EXT__
# define __STDC_WANT_IEC_60559_DFP_EXT__ 1
#endif
/* Enable extensions specified by ISO/IEC TS 18661-4:2015.  */
#ifndef __STDC_WANT_IEC_60559_FUNCS_EXT__
# define __STDC_WANT_IEC_60559_FUNCS_EXT__ 1
#endif
/* Enable extensions specified by ISO/IEC TS 18661-3:2015.  */
#ifndef __STDC_WANT_IEC_60559_TYPES_EXT__
# define __STDC_WANT_IEC_60559_TYPES_EXT__ 1
#endif
/* Enable extensions specified by ISO/IEC TR 24731-2:2010.  */
#ifndef __STDC_WANT_LIB_EXT2__
# define __STDC_WANT_LIB_EXT2__ 1
#endif
/* Enable extensions specified by ISO/IEC 24747:2009.  */
#ifndef __STDC_WANT_MATH_SPEC_FUNCS__
# define __STDC_WANT_MATH_SPEC_FUNCS__ 1
#endif
/* Enable extensions on HP NonStop.  */
#ifndef _TANDEM_SOURCE
# define _TANDEM_SOURCE 1
#endif
/* Enable X/Open extensions.  Define to 500 only if necessary
   to make mbstate_t available.  */
#ifndef _XOPEN_SOURCE
/* # undef _XOPEN_SOURCE */
#endif


/* Version number of package */
#define VERSION "1.26.0"

/* Number of bits in a file offset, on hosts where this is settable. */
/* #undef _FILE_OFFSET_BITS */

/* Define for large files, on AIX-style hosts. */
/* #undef _LARGE_FILES */

/* Define to `unsigned int' if <sys/types.h> does not define. */
/* #undef size_t */