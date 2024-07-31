/* Copyright (C) The c-ares project and its contributors
 * SPDX-License-Identifier: MIT
 */

/* Generated from ares_config.h.cmake */

/* Define if building universal (internal helper macro) */
#undef AC_APPLE_UNIVERSAL_BUILD

/* Defined for build with symbol hiding. */
#cmakedefine CARES_SYMBOL_HIDING

/* Use resolver library to configure cares */
#cmakedefine CARES_USE_LIBRESOLV

/* if a /etc/inet dir is being used */
#undef ETC_INET

/* Define to the type of arg 2 for gethostname. */
#define GETHOSTNAME_TYPE_ARG2 @GETHOSTNAME_TYPE_ARG2@

/* Define to the type qualifier of arg 1 for getnameinfo. */
#define GETNAMEINFO_QUAL_ARG1 @GETNAMEINFO_QUAL_ARG1@

/* Define to the type of arg 1 for getnameinfo. */
#define GETNAMEINFO_TYPE_ARG1 @GETNAMEINFO_TYPE_ARG1@

/* Define to the type of arg 2 for getnameinfo. */
#define GETNAMEINFO_TYPE_ARG2 @GETNAMEINFO_TYPE_ARG2@

/* Define to the type of args 4 and 6 for getnameinfo. */
#define GETNAMEINFO_TYPE_ARG46 @GETNAMEINFO_TYPE_ARG46@

/* Define to the type of arg 7 for getnameinfo. */
#define GETNAMEINFO_TYPE_ARG7 @GETNAMEINFO_TYPE_ARG7@

/* Specifies the number of arguments to getservbyport_r */
#define GETSERVBYPORT_R_ARGS @GETSERVBYPORT_R_ARGS@

/* Specifies the number of arguments to getservbyname_r */
#define GETSERVBYNAME_R_ARGS @GETSERVBYNAME_R_ARGS@

/* Define to 1 if you have AF_INET6. */
#cmakedefine HAVE_AF_INET6 1

/* Define to 1 if you have the <arpa/inet.h> header file. */
#cmakedefine HAVE_ARPA_INET_H 1

/* Define to 1 if you have the <arpa/nameser_compat.h> header file. */
#cmakedefine HAVE_ARPA_NAMESER_COMPAT_H 1

/* Define to 1 if you have the <arpa/nameser.h> header file. */
#cmakedefine HAVE_ARPA_NAMESER_H 1

/* Define to 1 if you have the <assert.h> header file. */
#cmakedefine HAVE_ASSERT_H 1

/* Define to 1 if you have the clock_gettime function and monotonic timer. */
#cmakedefine HAVE_CLOCK_GETTIME_MONOTONIC 1

/* Define to 1 if you have the closesocket function. */
#cmakedefine HAVE_CLOSESOCKET 1

/* Define to 1 if you have the CloseSocket camel case function. */
#cmakedefine HAVE_CLOSESOCKET_CAMEL 1

/* Define to 1 if you have the connect function. */
#cmakedefine HAVE_CONNECT 1

/* define if the compiler supports basic C++11 syntax */
#cmakedefine HAVE_CXX11 1

/* Define to 1 if you have the <dlfcn.h> header file. */
#cmakedefine HAVE_DLFCN_H 1

/* Define to 1 if you have the <errno.h> header file. */
#cmakedefine HAVE_ERRNO_H 1

/* Define to 1 if you have the <poll.h> header file. */
#cmakedefine HAVE_POLL_H 1

/* Define to 1 if you have the poll function. */
#cmakedefine HAVE_POLL 1

/* Define to 1 if you have the pipe function. */
#cmakedefine HAVE_PIPE 1

/* Define to 1 if you have the pipe2 function. */
#cmakedefine HAVE_PIPE2 1

/* Define to 1 if you have the kqueue function. */
#cmakedefine HAVE_KQUEUE 1

/* Define to 1 if you have the epoll{_create,ctl,wait} functions. */
#cmakedefine HAVE_EPOLL 1

/* Define to 1 if you have the fcntl function. */
#cmakedefine HAVE_FCNTL 1

/* Define to 1 if you have the <fcntl.h> header file. */
#cmakedefine HAVE_FCNTL_H 1

/* Define to 1 if you have a working fcntl O_NONBLOCK function. */
#cmakedefine HAVE_FCNTL_O_NONBLOCK 1

/* Define to 1 if you have the freeaddrinfo function. */
#cmakedefine HAVE_FREEADDRINFO 1

/* Define to 1 if you have a working getaddrinfo function. */
#cmakedefine HAVE_GETADDRINFO 1

/* Define to 1 if the getaddrinfo function is threadsafe. */
#cmakedefine HAVE_GETADDRINFO_THREADSAFE 1

/* Define to 1 if you have the getenv function. */
#cmakedefine HAVE_GETENV 1

/* Define to 1 if you have the gethostname function. */
#cmakedefine HAVE_GETHOSTNAME 1

/* Define to 1 if you have the getnameinfo function. */
#cmakedefine HAVE_GETNAMEINFO 1

/* Define to 1 if you have the getrandom function. */
#cmakedefine HAVE_GETRANDOM 1

/* Define to 1 if you have the getservbyport_r function. */
#cmakedefine HAVE_GETSERVBYPORT_R 1

/* Define to 1 if you have the getservbyname_r function. */
#cmakedefine HAVE_GETSERVBYNAME_R 1

/* Define to 1 if you have the `gettimeofday' function. */
#cmakedefine HAVE_GETTIMEOFDAY 1

/* Define to 1 if you have the `if_indextoname' function. */
#cmakedefine HAVE_IF_INDEXTONAME 1

/* Define to 1 if you have the `if_nametoindex' function. */
#cmakedefine HAVE_IF_NAMETOINDEX 1

/* Define to 1 if you have the `ConvertInterfaceIndexToLuid' function. */
#cmakedefine HAVE_CONVERTINTERFACEINDEXTOLUID 1

/* Define to 1 if you have the `ConvertInterfaceLuidToNameA' function. */
#cmakedefine HAVE_CONVERTINTERFACELUIDTONAMEA 1

/* Define to 1 if you have a IPv6 capable working inet_net_pton function. */
#cmakedefine HAVE_INET_NET_PTON 1

/* Define to 1 if you have a IPv6 capable working inet_ntop function. */
#cmakedefine HAVE_INET_NTOP 1

/* Define to 1 if you have a IPv6 capable working inet_pton function. */
#cmakedefine HAVE_INET_PTON 1

/* Define to 1 if you have the <inttypes.h> header file. */
#cmakedefine HAVE_INTTYPES_H 1

/* Define to 1 if you have the ioctl function. */
#cmakedefine HAVE_IOCTL 1

/* Define to 1 if you have the ioctlsocket function. */
#cmakedefine HAVE_IOCTLSOCKET 1

/* Define to 1 if you have the IoctlSocket camel case function. */
#cmakedefine HAVE_IOCTLSOCKET_CAMEL 1

/* Define to 1 if you have a working IoctlSocket camel case FIONBIO function.
   */
#cmakedefine HAVE_IOCTLSOCKET_CAMEL_FIONBIO 1

/* Define to 1 if you have a working ioctlsocket FIONBIO function. */
#cmakedefine HAVE_IOCTLSOCKET_FIONBIO 1

/* Define to 1 if you have a working ioctl FIONBIO function. */
#cmakedefine HAVE_IOCTL_FIONBIO 1

/* Define to 1 if you have a working ioctl SIOCGIFADDR function. */
#cmakedefine HAVE_IOCTL_SIOCGIFADDR 1

/* Define to 1 if you have the `resolve' library (-lresolve). */
#cmakedefine HAVE_LIBRESOLV 1

/* Define to 1 if you have iphlpapi.h */
#cmakedefine HAVE_IPHLPAPI_H 1

/* Define to 1 if you have netioapi.h */
#cmakedefine HAVE_NETIOAPI_H 1

/* Define to 1 if you have the <limits.h> header file. */
#cmakedefine HAVE_LIMITS_H 1

/* Define to 1 if the compiler supports the 'long long' data type. */
#cmakedefine HAVE_LONGLONG 1

/* Define to 1 if you have the malloc.h header file. */
#cmakedefine HAVE_MALLOC_H 1

/* Define to 1 if you have the memory.h header file. */
#cmakedefine HAVE_MEMORY_H 1

/* Define to 1 if you have the AvailabilityMacros.h header file. */
#cmakedefine HAVE_AVAILABILITYMACROS_H 1

/* Define to 1 if you have the MSG_NOSIGNAL flag. */
#cmakedefine HAVE_MSG_NOSIGNAL 1

/* Define to 1 if you have the <netdb.h> header file. */
#cmakedefine HAVE_NETDB_H 1

/* Define to 1 if you have the <netinet/in.h> header file. */
#cmakedefine HAVE_NETINET_IN_H 1

/* Define to 1 if you have the <netinet6/in6.h> header file. */
#cmakedefine HAVE_NETINET6_IN6_H 1

/* Define to 1 if you have the <netinet/tcp.h> header file. */
#cmakedefine HAVE_NETINET_TCP_H 1

/* Define to 1 if you have the <net/if.h> header file. */
#cmakedefine HAVE_NET_IF_H 1

/* Define to 1 if you have PF_INET6. */
#cmakedefine HAVE_PF_INET6 1

/* Define to 1 if you have the recv function. */
#cmakedefine HAVE_RECV 1

/* Define to 1 if you have the recvfrom function. */
#cmakedefine HAVE_RECVFROM 1

/* Define to 1 if you have the send function. */
#cmakedefine HAVE_SEND 1

/* Define to 1 if you have the setsockopt function. */
#cmakedefine HAVE_SETSOCKOPT 1

/* Define to 1 if you have a working setsockopt SO_NONBLOCK function. */
#cmakedefine HAVE_SETSOCKOPT_SO_NONBLOCK 1

/* Define to 1 if you have the <signal.h> header file. */
#cmakedefine HAVE_SIGNAL_H 1

/* Define to 1 if your struct sockaddr_in6 has sin6_scope_id. */
#cmakedefine HAVE_STRUCT_SOCKADDR_IN6_SIN6_SCOPE_ID 1

/* Define to 1 if you have the socket function. */
#cmakedefine HAVE_SOCKET 1

/* Define to 1 if you have the <socket.h> header file. */
#cmakedefine HAVE_SOCKET_H 1

/* Define to 1 if you have the <stdbool.h> header file. */
#cmakedefine HAVE_STDBOOL_H 1

/* Define to 1 if you have the <stdint.h> header file. */
#cmakedefine HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#cmakedefine HAVE_STDLIB_H 1

/* Define to 1 if you have the strcasecmp function. */
#cmakedefine HAVE_STRCASECMP 1

/* Define to 1 if you have the strcmpi function. */
#cmakedefine HAVE_STRCMPI 1

/* Define to 1 if you have the strdup function. */
#cmakedefine HAVE_STRDUP 1

/* Define to 1 if you have the stricmp function. */
#cmakedefine HAVE_STRICMP 1

/* Define to 1 if you have the <strings.h> header file. */
#cmakedefine HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#cmakedefine HAVE_STRING_H 1

/* Define to 1 if you have the strncasecmp function. */
#cmakedefine HAVE_STRNCASECMP 1

/* Define to 1 if you have the strncmpi function. */
#cmakedefine HAVE_STRNCMPI 1

/* Define to 1 if you have the strnicmp function. */
#cmakedefine HAVE_STRNICMP 1

/* Define to 1 if you have the <stropts.h> header file. */
#cmakedefine HAVE_STROPTS_H 1

/* Define to 1 if you have struct addrinfo. */
#cmakedefine HAVE_STRUCT_ADDRINFO 1

/* Define to 1 if you have struct in6_addr. */
#cmakedefine HAVE_STRUCT_IN6_ADDR 1

/* Define to 1 if you have struct sockaddr_in6. */
#cmakedefine HAVE_STRUCT_SOCKADDR_IN6 1

/* if struct sockaddr_storage is defined */
#cmakedefine HAVE_STRUCT_SOCKADDR_STORAGE 1

/* Define to 1 if you have the timeval struct. */
#cmakedefine HAVE_STRUCT_TIMEVAL 1

/* Define to 1 if you have the <sys/ioctl.h> header file. */
#cmakedefine HAVE_SYS_IOCTL_H 1

/* Define to 1 if you have the <sys/param.h> header file. */
#cmakedefine HAVE_SYS_PARAM_H 1

/* Define to 1 if you have the <sys/random.h> header file. */
#cmakedefine HAVE_SYS_RANDOM_H 1

/* Define to 1 if you have the <sys/event.h> header file. */
#cmakedefine HAVE_SYS_EVENT_H 1

/* Define to 1 if you have the <sys/epoll.h> header file. */
#cmakedefine HAVE_SYS_EPOLL_H 1

/* Define to 1 if you have the <sys/select.h> header file. */
#cmakedefine HAVE_SYS_SELECT_H 1

/* Define to 1 if you have the <sys/socket.h> header file. */
#cmakedefine HAVE_SYS_SOCKET_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#cmakedefine HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/time.h> header file. */
#cmakedefine HAVE_SYS_TIME_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#cmakedefine HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <sys/uio.h> header file. */
#cmakedefine HAVE_SYS_UIO_H 1

/* Define to 1 if you have the <time.h> header file. */
#cmakedefine HAVE_TIME_H 1

/* Define to 1 if you have the <ifaddrs.h> header file. */
#cmakedefine HAVE_IFADDRS_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#cmakedefine HAVE_UNISTD_H 1

/* Define to 1 if you have the windows.h header file. */
#cmakedefine HAVE_WINDOWS_H 1

/* Define to 1 if you have the winsock2.h header file. */
#cmakedefine HAVE_WINSOCK2_H 1

/* Define to 1 if you have the winsock.h header file. */
#cmakedefine HAVE_WINSOCK_H 1

/* Define to 1 if you have the mswsock.h header file. */
#cmakedefine HAVE_MSWSOCK_H 1

/* Define to 1 if you have the winternl.h header file. */
#cmakedefine HAVE_WINTERNL_H 1

/* Define to 1 if you have the ntstatus.h header file. */
#cmakedefine HAVE_NTSTATUS_H 1

/* Define to 1 if you have the ntdef.h header file. */
#cmakedefine HAVE_NTDEF_H 1

/* Define to 1 if you have the writev function. */
#cmakedefine HAVE_WRITEV 1

/* Define to 1 if you have the ws2tcpip.h header file. */
#cmakedefine HAVE_WS2TCPIP_H 1

/* Define to 1 if you have the __system_property_get function */
#cmakedefine HAVE___SYSTEM_PROPERTY_GET 1

/* Define if have arc4random_buf() */
#cmakedefine HAVE_ARC4RANDOM_BUF 1

/* Define if have getifaddrs() */
#cmakedefine HAVE_GETIFADDRS 1

/* Define if have stat() */
#cmakedefine HAVE_STAT 1

/* a suitable file/device to read random data from */
#cmakedefine CARES_RANDOM_FILE "@CARES_RANDOM_FILE@"

/* Define to the type qualifier pointed by arg 5 for recvfrom. */
#define RECVFROM_QUAL_ARG5 @RECVFROM_QUAL_ARG5@

/* Define to the type of arg 1 for recvfrom. */
#define RECVFROM_TYPE_ARG1 @RECVFROM_TYPE_ARG1@

/* Define to the type pointed by arg 2 for recvfrom. */
#define RECVFROM_TYPE_ARG2 @RECVFROM_TYPE_ARG2@

/* Define to 1 if the type pointed by arg 2 for recvfrom is void. */
#cmakedefine01 RECVFROM_TYPE_ARG2_IS_VOID

/* Define to the type of arg 3 for recvfrom. */
#define RECVFROM_TYPE_ARG3 @RECVFROM_TYPE_ARG3@

/* Define to the type of arg 4 for recvfrom. */
#define RECVFROM_TYPE_ARG4 @RECVFROM_TYPE_ARG4@

/* Define to the type pointed by arg 5 for recvfrom. */
#define RECVFROM_TYPE_ARG5 @RECVFROM_TYPE_ARG5@

/* Define to 1 if the type pointed by arg 5 for recvfrom is void. */
#cmakedefine01 RECVFROM_TYPE_ARG5_IS_VOID

/* Define to the type pointed by arg 6 for recvfrom. */
#define RECVFROM_TYPE_ARG6 @RECVFROM_TYPE_ARG6@

/* Define to 1 if the type pointed by arg 6 for recvfrom is void. */
#cmakedefine01 RECVFROM_TYPE_ARG6_IS_VOID

/* Define to the function return type for recvfrom. */
#define RECVFROM_TYPE_RETV @RECVFROM_TYPE_RETV@

/* Define to the type of arg 1 for recv. */
#define RECV_TYPE_ARG1 @RECV_TYPE_ARG1@

/* Define to the type of arg 2 for recv. */
#define RECV_TYPE_ARG2 @RECV_TYPE_ARG2@

/* Define to the type of arg 3 for recv. */
#define RECV_TYPE_ARG3 @RECV_TYPE_ARG3@

/* Define to the type of arg 4 for recv. */
#define RECV_TYPE_ARG4 @RECV_TYPE_ARG4@

/* Define to the function return type for recv. */
#define RECV_TYPE_RETV @RECV_TYPE_RETV@

/* Define to the type of arg 1 for send. */
#define SEND_TYPE_ARG1 @SEND_TYPE_ARG1@

/* Define to the type of arg 2 for send. */
#define SEND_TYPE_ARG2 @SEND_TYPE_ARG2@

/* Define to the type of arg 3 for send. */
#define SEND_TYPE_ARG3 @SEND_TYPE_ARG3@

/* Define to the type of arg 4 for send. */
#define SEND_TYPE_ARG4 @SEND_TYPE_ARG4@

/* Define to the function return type for send. */
#define SEND_TYPE_RETV @SEND_TYPE_RETV@

/* Define to disable non-blocking sockets. */
#undef USE_BLOCKING_SOCKETS

/* Define to avoid automatic inclusion of winsock.h */
#undef WIN32_LEAN_AND_MEAN

/* Define to 1 if you have the pthread.h header file. */
#cmakedefine HAVE_PTHREAD_H 1

/* Define to 1 if you have the pthread_np.h header file. */
#cmakedefine HAVE_PTHREAD_NP_H 1

/* Define to 1 if threads are enabled */
#cmakedefine CARES_THREADS 1

/* Define to 1 if pthread_init() exists */
#cmakedefine HAVE_PTHREAD_INIT 1

