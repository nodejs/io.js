# Copyright (C) The c-ares project and its contributors
# SPDX-License-Identifier: MIT
CMAKE_MINIMUM_REQUIRED (VERSION 3.5.0...3.10.0)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/")

INCLUDE (CheckIncludeFiles)
INCLUDE (CheckTypeSize)
INCLUDE (CheckFunctionExists)
INCLUDE (CheckSymbolExists)
INCLUDE (CheckCSourceCompiles)
INCLUDE (CheckStructHasMember)
INCLUDE (CheckLibraryExists)

PROJECT (c-ares LANGUAGES C VERSION "1.34.4" )

# Set this version before release
SET (CARES_VERSION "${PROJECT_VERSION}")

INCLUDE (GNUInstallDirs) # include this *AFTER* PROJECT(), otherwise paths are wrong.

# This is for libtool compatibility, and specified in a form that is easily
# translatable from libtool (even if the actual form doesn't make sense).
# For instance, in an autotools project, in Makefile.am there is a line that
# contains something like:
#     -version-info 4:0:2
# This breaks down into sections of current:revision:age
# This then generates a version of  "(current-age).age.revision"  with an
# interface version of "(current-age)"
# For example, a version of 4:0:2 would generate output such as:
#    libname.so   -> libname.so.2
#    libname.so.2 -> libname.so.2.2.0
SET (CARES_LIB_VERSIONINFO "21:3:19")


OPTION (CARES_STATIC        "Build as a static library"                                             OFF)
OPTION (CARES_SHARED        "Build as a shared library"                                             ON)
OPTION (CARES_INSTALL       "Create installation targets (chain builders may want to disable this)" ON)
OPTION (CARES_STATIC_PIC    "Build the static library as PIC (position independent)"                OFF)
OPTION (CARES_BUILD_TESTS   "Build and run tests"                                                   OFF)
OPTION (CARES_BUILD_CONTAINER_TESTS "Build and run container tests (implies CARES_BUILD_TESTS, Linux only)" OFF)
OPTION (CARES_BUILD_TOOLS   "Build tools"                                                           ON)
OPTION (CARES_SYMBOL_HIDING "Hide private symbols in shared libraries"                              OFF)
OPTION (CARES_THREADS       "Build with thread-safety support"                                      ON)
OPTION (CARES_COVERAGE      "Build for code coverage"                                               OFF)
SET    (CARES_RANDOM_FILE "/dev/urandom" CACHE STRING "Suitable File / Device Path for entropy, such as /dev/urandom")


# Tests require a C++14 compiler
IF (CARES_BUILD_TESTS OR CARES_BUILD_CONTAINER_TESTS)
	set(CMAKE_CXX_STANDARD 14)
	set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
	set(CMAKE_CXX_EXTENSIONS FALSE)
	enable_language(CXX)
ENDIF ()

# Tests require static to be enabled on Windows to be able to access otherwise hidden symbols
IF ((CARES_BUILD_TESTS OR CARES_BUILD_CONTAINER_TESTS) AND (NOT CARES_STATIC) AND WIN32)
	SET (CARES_STATIC ON)
	SET (CARES_STATIC_PIC ON)
	MESSAGE (WARNING "Static building was requested be disabled, but re-enabled to support tests")
ENDIF ()

INCLUDE (EnableWarnings)

IF (MSVC)
	# allow linking against the static runtime library in msvc
	OPTION (CARES_MSVC_STATIC_RUNTIME "Link against the static runtime library" OFF)
	IF (CARES_MSVC_STATIC_RUNTIME)
		# CMAKE_CONFIGURATION_TYPES is empty on non-IDE generators (Ninja, NMake)
		# and that's why we also use CMAKE_BUILD_TYPE to cover for those generators.
		# For IDE generators, CMAKE_BUILD_TYPE is usually empty
		FOREACH (config_type ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
			STRING (TOUPPER ${config_type} upper_config_type)
			SET (flag_var "CMAKE_C_FLAGS_${upper_config_type}")
			IF (${flag_var} MATCHES "/MD")
				STRING (REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
			ENDIF ()
		ENDFOREACH ()

		# clean up
		SET (upper_config_type)
		SET (config_type)
		SET (flag_var)
	ENDIF ()
ENDIF ()

IF (CARES_SYMBOL_HIDING)
	IF (CMAKE_VERSION VERSION_LESS 3.12)
		MESSAGE (FATAL_ERROR "Hiding symbols requires CMake 3.12")
	ENDIF ()
	CMAKE_POLICY (SET CMP0063 NEW)
ENDIF ()

# Keep build organized, but only if it is the top-level project.
# CMake 3.21 or later has PROJECT_IS_TOP_LEVEL, but we aren't yet depending on
# that version.
IF (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	SET (CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}")
	SET (CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}")
	SET (CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}")
	SET (CMAKE_COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
ENDIF ()

# Destinations for installing different kinds of targets (pass to install command).
SET (TARGETS_INST_DEST
	RUNTIME DESTINATION  ${CMAKE_INSTALL_BINDIR}
	BUNDLE DESTINATION   ${CMAKE_INSTALL_BINDIR}
	LIBRARY DESTINATION  ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION  ${CMAKE_INSTALL_LIBDIR}
)

# Function in Library
# CHECK_LIBRARY_EXISTS can't be used as it will return true if the function
# is found in a different required/dependent library.
MACRO (CARES_FUNCTION_IN_LIBRARY func lib var)
	SET (_ORIG_FIL_CMAKE_REQUIRED_LIBRARIES "${CMAKE_REQUIRED_LIBRARIES}")
	SET (CMAKE_REQUIRED_LIBRARIES )
	CHECK_FUNCTION_EXISTS ("${func}" "_CARES_FUNC_IN_LIB_GLOBAL_${func}")
	SET (CMAKE_REQUIRED_LIBRARIES "${_ORIG_FIL_CMAKE_REQUIRED_LIBRARIES}")

	IF ("${_CARES_FUNC_IN_LIB_GLOBAL_${func}}")
		SET (${var} FALSE)
	ELSE ()
		CHECK_LIBRARY_EXISTS ("${lib}" "${func}" "" ${var})
	ENDIF ()
ENDMACRO ()

# Look for dependent/required libraries
CARES_FUNCTION_IN_LIBRARY (res_servicename resolv HAVE_RES_SERVICENAME_IN_LIBRESOLV)
IF (HAVE_RES_SERVICENAME_IN_LIBRESOLV)
	SET (HAVE_LIBRESOLV 1)
ENDIF ()

IF (APPLE)
	CHECK_C_SOURCE_COMPILES ("
	#include <stdio.h>
	#include <TargetConditionals.h>
	int main() {
#if TARGET_OS_IPHONE == 0
#error Not an iPhone target
#endif
return 0;
	}
	"
	IOS)

	CHECK_C_SOURCE_COMPILES ("
#include <stdio.h>
#include <TargetConditionals.h>
	int main() {
#if TARGET_OS_IPHONE == 0 || __IPHONE_OS_VERSION_MIN_REQUIRED < 100000
#  error Not iOS v10
#endif
return 0;
	}
	"
	IOS_V10)

	CHECK_C_SOURCE_COMPILES ("
#include <stdio.h>
#include <AvailabilityMacros.h>
#ifndef MAC_OS_X_VERSION_10_12
#  define MAC_OS_X_VERSION_10_12 101200
#endif
	int main() {
#if MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_12
#  error Not MacOSX 10.12 or higher
#endif
return 0;
	}
	"
	MACOS_V1012)
ENDIF ()

IF (ZOS AND HAVE_LIBRESOLV)
	SET (CARES_USE_LIBRESOLV 1)
ENDIF()


CARES_FUNCTION_IN_LIBRARY (gethostbyname nsl HAVE_LIBNSL)
CARES_FUNCTION_IN_LIBRARY (gethostbyname socket HAVE_GHBN_LIBSOCKET)
CARES_FUNCTION_IN_LIBRARY (gethostbyname network HAVE_LIBNETWORK)
CARES_FUNCTION_IN_LIBRARY (socket socket HAVE_SOCKET_LIBSOCKET)
IF (HAVE_GHBN_LIBSOCKET OR HAVE_SOCKET_LIBSOCKET)
	SET(HAVE_LIBSOCKET TRUE)
ENDIF ()
CARES_FUNCTION_IN_LIBRARY (socket network HAVE_LIBNETWORK)
CARES_FUNCTION_IN_LIBRARY (clock_gettime rt HAVE_LIBRT)


# Look for necessary includes
CHECK_INCLUDE_FILES (AvailabilityMacros.h  HAVE_AVAILABILITYMACROS_H)
CHECK_INCLUDE_FILES (sys/types.h           HAVE_SYS_TYPES_H)
CHECK_INCLUDE_FILES (sys/random.h          HAVE_SYS_RANDOM_H)
CHECK_INCLUDE_FILES (sys/socket.h          HAVE_SYS_SOCKET_H)
CHECK_INCLUDE_FILES (sys/sockio.h          HAVE_SYS_SOCKIO_H)
CHECK_INCLUDE_FILES (arpa/inet.h           HAVE_ARPA_INET_H)
CHECK_INCLUDE_FILES (arpa/nameser_compat.h HAVE_ARPA_NAMESER_COMPAT_H)
CHECK_INCLUDE_FILES (arpa/nameser.h        HAVE_ARPA_NAMESER_H)
CHECK_INCLUDE_FILES (assert.h              HAVE_ASSERT_H)
CHECK_INCLUDE_FILES (errno.h               HAVE_ERRNO_H)
CHECK_INCLUDE_FILES (fcntl.h               HAVE_FCNTL_H)
CHECK_INCLUDE_FILES (inttypes.h            HAVE_INTTYPES_H)
CHECK_INCLUDE_FILES (limits.h              HAVE_LIMITS_H)
CHECK_INCLUDE_FILES (malloc.h              HAVE_MALLOC_H)
CHECK_INCLUDE_FILES (memory.h              HAVE_MEMORY_H)
CHECK_INCLUDE_FILES (netdb.h               HAVE_NETDB_H)
CHECK_INCLUDE_FILES (netinet/in.h          HAVE_NETINET_IN_H)
CHECK_INCLUDE_FILES (netinet6/in6.h        HAVE_NETINET6_IN6_H)
# On old MacOS SDK versions, you must include sys/socket.h before net/if.h
IF (HAVE_SYS_SOCKET_H)
  CHECK_INCLUDE_FILES ("sys/socket.h;net/if.h"  HAVE_NET_IF_H)
ELSE ()
  CHECK_INCLUDE_FILES (net/if.h                 HAVE_NET_IF_H)
ENDIF ()
CHECK_INCLUDE_FILES (signal.h              HAVE_SIGNAL_H)
CHECK_INCLUDE_FILES (socket.h              HAVE_SOCKET_H)
CHECK_INCLUDE_FILES (stdbool.h             HAVE_STDBOOL_H)
CHECK_INCLUDE_FILES (stdint.h              HAVE_STDINT_H)
CHECK_INCLUDE_FILES (stdlib.h              HAVE_STDLIB_H)
CHECK_INCLUDE_FILES (strings.h             HAVE_STRINGS_H)
CHECK_INCLUDE_FILES (string.h              HAVE_STRING_H)
CHECK_INCLUDE_FILES (stropts.h             HAVE_STROPTS_H)
CHECK_INCLUDE_FILES (sys/ioctl.h           HAVE_SYS_IOCTL_H)
CHECK_INCLUDE_FILES (sys/param.h           HAVE_SYS_PARAM_H)
CHECK_INCLUDE_FILES (sys/select.h          HAVE_SYS_SELECT_H)
CHECK_INCLUDE_FILES (sys/stat.h            HAVE_SYS_STAT_H)
CHECK_INCLUDE_FILES (sys/time.h            HAVE_SYS_TIME_H)
CHECK_INCLUDE_FILES (sys/uio.h             HAVE_SYS_UIO_H)
CHECK_INCLUDE_FILES (sys/event.h           HAVE_SYS_EVENT_H)
CHECK_INCLUDE_FILES (sys/epoll.h           HAVE_SYS_EPOLL_H)
CHECK_INCLUDE_FILES (ifaddrs.h             HAVE_IFADDRS_H)
CHECK_INCLUDE_FILES (time.h                HAVE_TIME_H)
CHECK_INCLUDE_FILES (poll.h                HAVE_POLL_H)
CHECK_INCLUDE_FILES (dlfcn.h               HAVE_DLFCN_H)
CHECK_INCLUDE_FILES (unistd.h              HAVE_UNISTD_H)
# On OpenBSD, you must include sys/types.h before netinet/tcp.h
IF (HAVE_SYS_TYPES_H)
  CHECK_INCLUDE_FILES ("sys/types.h;netinet/tcp.h"         HAVE_NETINET_TCP_H)
ELSE ()
  CHECK_INCLUDE_FILES (netinet/tcp.h                       HAVE_NETINET_TCP_H)
ENDIF ()

# Include order matters for these windows files.
# As cygwin environment has both socket.h and winsock2.h
# headers check WIN32 not to include the later one here
IF (WIN32)
CHECK_INCLUDE_FILES ("winsock2.h;windows.h"            HAVE_WINSOCK2_H)
CHECK_INCLUDE_FILES ("winsock2.h;ws2tcpip.h;windows.h" HAVE_WS2TCPIP_H)
CHECK_INCLUDE_FILES ("winsock2.h;iphlpapi.h;windows.h" HAVE_IPHLPAPI_H)
CHECK_INCLUDE_FILES ("winsock2.h;netioapi.h;windows.h" HAVE_NETIOAPI_H)
CHECK_INCLUDE_FILES ("winsock2.h;mswsock.h;windows.h"  HAVE_MSWSOCK_H)
CHECK_INCLUDE_FILES ("winsock.h;windows.h"             HAVE_WINSOCK_H)
CHECK_INCLUDE_FILES (windows.h                         HAVE_WINDOWS_H)
CHECK_INCLUDE_FILES ("windows.h;winternl.h"            HAVE_WINTERNL_H)
CHECK_INCLUDE_FILES ("windows.h;ntdef.h"               HAVE_NTDEF_H)
CHECK_INCLUDE_FILES ("windows.h;ntdef.h;ntstatus.h"    HAVE_NTSTATUS_H)


ENDIF ()

# Set system-specific compiler flags
IF (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	LIST (APPEND SYSFLAGS -D_DARWIN_C_SOURCE)
ELSEIF (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Android")
	LIST (APPEND SYSFLAGS -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700)
ELSEIF (CMAKE_SYSTEM_NAME STREQUAL "SunOS")
	LIST (APPEND SYSFLAGS -D__EXTENSIONS__ -D_REENTRANT -D_XOPEN_SOURCE=600)
ELSEIF (CMAKE_SYSTEM_NAME STREQUAL "AIX")
	LIST (APPEND SYSFLAGS -D_ALL_SOURCE -D_XOPEN_SOURCE=700 -D_USE_IRS)
ELSEIF (CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
	# Don't define _XOPEN_SOURCE on FreeBSD, it actually reduces visibility instead of increasing it
ELSEIF (CMAKE_SYSTEM_NAME STREQUAL "QNX")
	LIST (APPEND SYSFLAGS -D_QNX_SOURCE)
ELSEIF (WIN32)
	LIST (APPEND SYSFLAGS -DWIN32_LEAN_AND_MEAN -D_CRT_SECURE_NO_DEPRECATE -D_CRT_NONSTDC_NO_DEPRECATE -D_WIN32_WINNT=0x0602)
ENDIF ()
ADD_DEFINITIONS(${SYSFLAGS})



# Tell C-Ares about libraries to depend on
IF (HAVE_LIBRESOLV)
	LIST (APPEND CARES_DEPENDENT_LIBS resolv)
ENDIF ()
IF (HAVE_LIBNSL)
	LIST (APPEND CARES_DEPENDENT_LIBS nsl)
ENDIF ()
IF (HAVE_LIBSOCKET)
	LIST (APPEND CARES_DEPENDENT_LIBS socket)
ENDIF ()
IF (HAVE_LIBNETWORK)
	LIST (APPEND CARES_DEPENDENT_LIBS network)
ENDIF ()
IF (HAVE_LIBRT)
	LIST (APPEND CARES_DEPENDENT_LIBS rt)
ENDIF ()
IF (WIN32)
	LIST (APPEND CARES_DEPENDENT_LIBS ws2_32 advapi32 iphlpapi)
ENDIF ()


# When checking for symbols, we need to make sure we set the proper
# headers, libraries, and definitions for the detection to work properly
# CMAKE_REQUIRED_DEFINITIONS, CMAKE_REQUIRED_LIBRARIES, and
# CMAKE_EXTRA_INCLUDE_FILES.  When we're done with the detection, we'll
# restore them to their original values (otherwise a parent project
# that tries to set these won't be maintained, see Issue #729)
SET (ORIG_CMAKE_REQUIRED_DEFINITIONS ${CMAKE_REQUIRED_DEEFINITIONS})
SET (ORIG_CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES})
SET (ORIG_CMAKE_EXTRA_INCLUDE_FILES ${CMAKE_EXTRA_INCLUDE_FILES})

SET (CMAKE_REQUIRED_DEFINITIONS ${SYSFLAGS})
LIST (APPEND CMAKE_REQUIRED_LIBRARIES ${CARES_DEPENDENT_LIBS})

MACRO (CARES_EXTRAINCLUDE_IFSET var include)
	IF (${var})
		LIST (APPEND CMAKE_EXTRA_INCLUDE_FILES ${include})
	ENDIF ()
ENDMACRO ()

CARES_EXTRAINCLUDE_IFSET (HAVE_AVAILABILITYMACROS_H AvailabilityMacros.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_STDBOOL_H      stdbool.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_SYS_TYPES_H    sys/types.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_ARPA_INET_H    arpa/inet.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_ARPA_NAMESER_H arpa/nameser.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_NETDB_H        netdb.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_NET_IF_H       net/if.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_IFADDRS_H      ifaddrs.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_NETINET_IN_H   netinet/in.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_NETINET6_IN6_H netinet6/in6.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_NETINET_TCP_H  netinet/tcp.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_SIGNAL_H       signal.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_STDLIB_H       stdlib.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_STRING_H       string.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_STRINGS_H      strings.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_SYS_IOCTL_H    sys/ioctl.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_SYS_RANDOM_H   sys/random.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_SYS_SELECT_H   sys/select.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_SYS_SOCKET_H   sys/socket.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_SYS_SOCKIO_H	sys/sockio.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_SYS_TIME_H     sys/time.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_SYS_STAT_H     sys/stat.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_SYS_UIO_H      sys/uio.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_SYS_EVENT_H    sys/event.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_SYS_EPOLL_H    sys/epoll.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_TIME_H         time.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_POLL_H         poll.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_FCNTL_H        fcntl.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_UNISTD_H       unistd.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_WINSOCK2_H     winsock2.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_WS2TCPIP_H     ws2tcpip.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_IPHLPAPI_H     iphlpapi.h)
CARES_EXTRAINCLUDE_IFSET (HAVE_WINDOWS_H      windows.h)

# Check Types
# CHECK_TYPE_SIZE can't be used to see if a type exists because on Apple when
# building multi-arch, it will throw an error.  So we need to wrap
# CHECK_C_SOURCE_COMPILES for our tests.
MACRO (CARES_TYPE_EXISTS type var)
	SET(_CARES_C_SOURCE "
		#include <stdio.h>
		#include <stdlib.h>
	")
	FOREACH(_C_HEADER ${CMAKE_EXTRA_INCLUDE_FILES})
		SET(_CARES_C_SOURCE "${_CARES_C_SOURCE}
		#include <${_C_HEADER}>")
	ENDFOREACH(_C_HEADER)

	SET(_CARES_C_SOURCE "${_CARES_C_SOURCE}
		int main() {
			${type} var_exists;
			(void)var_exists;
			return 0;
		}
	")
	CHECK_C_SOURCE_COMPILES ("${_CARES_C_SOURCE}" ${var})
ENDMACRO ()

CARES_TYPE_EXISTS (socklen_t                 HAVE_SOCKLEN_T)
CARES_TYPE_EXISTS (SOCKET                    HAVE_TYPE_SOCKET)
CARES_TYPE_EXISTS (ssize_t                   HAVE_SSIZE_T)
CARES_TYPE_EXISTS ("long long"               HAVE_LONGLONG)
CARES_TYPE_EXISTS ("struct addrinfo"         HAVE_STRUCT_ADDRINFO)
CARES_TYPE_EXISTS ("struct in6_addr"         HAVE_STRUCT_IN6_ADDR)
CARES_TYPE_EXISTS ("struct sockaddr_in6"     HAVE_STRUCT_SOCKADDR_IN6)
CARES_TYPE_EXISTS ("struct sockaddr_storage" HAVE_STRUCT_SOCKADDR_STORAGE)
CARES_TYPE_EXISTS ("struct timeval"          HAVE_STRUCT_TIMEVAL)


# Check for preprocessor defines
CHECK_SYMBOL_EXISTS (AF_INET6        "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_AF_INET6)
CHECK_SYMBOL_EXISTS (O_NONBLOCK      "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_O_NONBLOCK)
CHECK_SYMBOL_EXISTS (FIONBIO         "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_FIONBIO)
CHECK_SYMBOL_EXISTS (SIOCGIFADDR     "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_IOCTL_SIOCGIFADDR)
CHECK_SYMBOL_EXISTS (MSG_NOSIGNAL    "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_MSG_NOSIGNAL)
CHECK_SYMBOL_EXISTS (PF_INET6        "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_PF_INET6)
CHECK_SYMBOL_EXISTS (SO_NONBLOCK     "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_SO_NONBLOCK)

# XCode v8 bug: iOS when targeting less than v10, or MacOS when targeting less than v10.12 will
# say clock_gettime exists, it is a weak symbol that only exists in iOS10/MacOS10.12 and will
# cause a crash at runtime when running on older versions.  Skip finding CLOCK_MONOTONIC on older
# OS's.
IF ((NOT APPLE) OR IOS_V10 OR MACOS_V1012)
	CHECK_SYMBOL_EXISTS (CLOCK_MONOTONIC "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_CLOCK_GETTIME_MONOTONIC)
ENDIF ()

CHECK_STRUCT_HAS_MEMBER("struct sockaddr_in6" sin6_scope_id "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_STRUCT_SOCKADDR_IN6_SIN6_SCOPE_ID LANGUAGE C)

CHECK_SYMBOL_EXISTS (strnlen         "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_STRNLEN)
CHECK_SYMBOL_EXISTS (memmem          "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_MEMMEM)
CHECK_SYMBOL_EXISTS (closesocket     "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_CLOSESOCKET)
CHECK_SYMBOL_EXISTS (CloseSocket     "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_CLOSESOCKET_CAMEL)
CHECK_SYMBOL_EXISTS (connect         "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_CONNECT)
CHECK_SYMBOL_EXISTS (connectx        "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_CONNECTX)
CHECK_SYMBOL_EXISTS (fcntl           "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_FCNTL)
CHECK_SYMBOL_EXISTS (freeaddrinfo    "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_FREEADDRINFO)
CHECK_SYMBOL_EXISTS (getaddrinfo     "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_GETADDRINFO)
CHECK_SYMBOL_EXISTS (getenv          "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_GETENV)
CHECK_SYMBOL_EXISTS (gethostname     "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_GETHOSTNAME)
CHECK_SYMBOL_EXISTS (getnameinfo     "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_GETNAMEINFO)
CHECK_SYMBOL_EXISTS (getrandom       "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_GETRANDOM)
CHECK_SYMBOL_EXISTS (getservbyport_r "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_GETSERVBYPORT_R)
CHECK_SYMBOL_EXISTS (getservbyname_r "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_GETSERVBYNAME_R)
CHECK_SYMBOL_EXISTS (gettimeofday    "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_GETTIMEOFDAY)
CHECK_SYMBOL_EXISTS (if_indextoname  "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_IF_INDEXTONAME)
CHECK_SYMBOL_EXISTS (if_nametoindex  "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_IF_NAMETOINDEX)
CHECK_SYMBOL_EXISTS (ConvertInterfaceIndexToLuid  "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_CONVERTINTERFACEINDEXTOLUID)
CHECK_SYMBOL_EXISTS (ConvertInterfaceLuidToNameA  "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_CONVERTINTERFACELUIDTONAMEA)
CHECK_SYMBOL_EXISTS (NotifyIpInterfaceChange      "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_NOTIFYIPINTERFACECHANGE)
CHECK_SYMBOL_EXISTS (RegisterWaitForSingleObject  "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_REGISTERWAITFORSINGLEOBJECT)


CHECK_SYMBOL_EXISTS (inet_net_pton   "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_INET_NET_PTON)
IF (NOT WIN32)
	# Disabled on Windows, because these functions are only really supported on Windows
	# Vista or newer (_WIN32_WINNT >= 0x0600). Older versions of Windows may provide
	# them as experimental non-working features, so we have to disable them manually.
	CHECK_SYMBOL_EXISTS (inet_ntop       "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_INET_NTOP)
	CHECK_SYMBOL_EXISTS (inet_pton       "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_INET_PTON)
ENDIF ()
CHECK_SYMBOL_EXISTS (ioctl           "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_IOCTL)
CHECK_SYMBOL_EXISTS (ioctlsocket     "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_IOCTLSOCKET)
CHECK_SYMBOL_EXISTS (IoctlSocket     "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_IOCTLSOCKET_CAMEL)
CHECK_SYMBOL_EXISTS (recv            "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_RECV)
CHECK_SYMBOL_EXISTS (recvfrom        "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_RECVFROM)
CHECK_SYMBOL_EXISTS (send            "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_SEND)
CHECK_SYMBOL_EXISTS (sendto          "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_SENDTO)
CHECK_SYMBOL_EXISTS (setsockopt      "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_SETSOCKOPT)
CHECK_SYMBOL_EXISTS (socket          "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_SOCKET)
CHECK_SYMBOL_EXISTS (strcasecmp      "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_STRCASECMP)
CHECK_SYMBOL_EXISTS (strcmpi         "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_STRCMPI)
CHECK_SYMBOL_EXISTS (strdup          "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_STRDUP)
CHECK_SYMBOL_EXISTS (stricmp         "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_STRICMP)
CHECK_SYMBOL_EXISTS (strncasecmp     "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_STRNCASECMP)
CHECK_SYMBOL_EXISTS (strncmpi        "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_STRNCMPI)
CHECK_SYMBOL_EXISTS (strnicmp        "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_STRNICMP)
CHECK_SYMBOL_EXISTS (writev          "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_WRITEV)
CHECK_SYMBOL_EXISTS (arc4random_buf  "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_ARC4RANDOM_BUF)
CHECK_SYMBOL_EXISTS (stat            "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_STAT)
CHECK_SYMBOL_EXISTS (getifaddrs      "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_GETIFADDRS)
CHECK_SYMBOL_EXISTS (poll            "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_POLL)
CHECK_SYMBOL_EXISTS (pipe            "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_PIPE)
CHECK_SYMBOL_EXISTS (pipe2           "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_PIPE2)
CHECK_SYMBOL_EXISTS (kqueue          "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_KQUEUE)
CHECK_SYMBOL_EXISTS (epoll_create1   "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_EPOLL)


# On Android, the system headers may define __system_property_get(), but excluded
# from libc.  We need to perform a link test instead of a header/symbol test.
CHECK_FUNCTION_EXISTS (__system_property_get HAVE___SYSTEM_PROPERTY_GET)

# Restore original values (as per Issue #729)
SET (CMAKE_REQUIRED_DEFINITIONS ${ORIG_CMAKE_REQUIRED_DEEFINITIONS})
SET (CMAKE_REQUIRED_LIBRARIES ${ORIG_CMAKE_REQUIRED_LIBRARIES})
SET (CMAKE_EXTRA_INCLUDE_FILES ${ORIG_CMAKE_EXTRA_INCLUDE_FILES})


################################################################################
# Threading Support
#
IF (CARES_THREADS)
	IF (WIN32)
		# Do nothing, always has threads
	ELSE ()
		# Need to prefer pthreads on platforms that may have more threading choices
		# (e.g. Solaris)
		SET (CMAKE_THREAD_PREFER_PTHREAD TRUE)
		FIND_PACKAGE (Threads)

		IF (Threads_FOUND)
			# Fix solaris9 bug due to libc having pthread_create() stubs that always fail.  CMake
			# doesn't realize that the real pthread functions aren't in libc, so sets the pthread
			# library CAKE_THREAD_LIBS_INIT variable to blank instead of to the correct "-lpthread".
			IF (CMAKE_SYSTEM_NAME STREQUAL "SunOS" AND NOT CMAKE_THREAD_LIBS_INIT)
				SET (CMAKE_THREAD_LIBS_INIT "-lpthread")
			ENDIF ()

			# PThread functions.
			CHECK_INCLUDE_FILES (pthread.h                                 HAVE_PTHREAD_H)
			CHECK_INCLUDE_FILES (pthread_np.h                              HAVE_PTHREAD_NP_H)
			CARES_EXTRAINCLUDE_IFSET (HAVE_PTHREAD_H                       pthread.h)
			CARES_EXTRAINCLUDE_IFSET (HAVE_PTHREAD_NP_H                    pthread_np.h)
			CHECK_SYMBOL_EXISTS (pthread_init  "${CMAKE_EXTRA_INCLUDE_FILES}" HAVE_PTHREAD_INIT)
			# Make sure libcares.pc.cmake knows about thread libraries on static builds
			# The variable set by FIND_PACKAGE(Threads) has a -l prefix on it, we need
			# to strip that first since CARES_DEPENDENT_LIBS doesn't expect that.
			STRING (REPLACE "-l" "" CARES_THREAD_LIBRARY "${CMAKE_THREAD_LIBS_INIT}")
			LIST (APPEND CARES_DEPENDENT_LIBS ${CARES_THREAD_LIBRARY})
		ELSE ()
			MESSAGE (WARNING "Threading support not found, disabling...")
			SET (CARES_THREADS OFF)
		ENDIF ()
	ENDIF ()
ENDIF ()



################################################################################
# recv, recvfrom, send, getnameinfo, gethostname
# ARGUMENTS AND RETURN VALUES
#
# The AutoTools build tries to be really thorough here.  So much so that it
# takes forever.  We really don't want to do that.  Lets make some educated
# guesses based on datatypes we have available, and for others, use some 'sane'
# defaults.  This should be much quicker and nearly as accurate ... and even
# if not, it probably won't matter in the least.

IF (HAVE_SSIZE_T AND HAVE_SOCKLEN_T AND NOT WIN32)
	# If we have ssize_t and socklen_t, the API is usually sane and uses ssize_t and size_t for lengths
	SET (RECVFROM_TYPE_RETV ssize_t)
	SET (RECVFROM_TYPE_ARG3 size_t)
ELSE ()
	SET (RECVFROM_TYPE_RETV int)
	SET (RECVFROM_TYPE_ARG3 int)
ENDIF ()

IF (HAVE_TYPE_SOCKET)
	# If the SOCKET type is defined, it uses socket ... should be windows only
	SET (RECVFROM_TYPE_ARG1 SOCKET)
ELSE ()
	SET (RECVFROM_TYPE_ARG1 int)
ENDIF()

IF (HAVE_SOCKLEN_T)
	# If we have socklen_t the APIs pretty much always actually use it
	SET (RECVFROM_TYPE_ARG6 "socklen_t *")
	SET (GETNAMEINFO_TYPE_ARG2 socklen_t)
	SET (GETNAMEINFO_TYPE_ARG46 socklen_t)
ELSE ()
	SET (RECVFROM_TYPE_ARG6 "int *")
	SET (GETNAMEINFO_TYPE_ARG2 int)
	SET (GETNAMEINFO_TYPE_ARG46 int)
ENDIF ()

IF (WIN32)
	SET (RECV_TYPE_ARG2 "char *")
ELSE ()
	SET (RECV_TYPE_ARG2 "void *")
ENDIF ()

# Functions are typically consistent so the equivalent fields map ... equivalently
SET (RECV_TYPE_RETV ${RECVFROM_TYPE_RETV})
SET (SEND_TYPE_RETV ${RECVFROM_TYPE_RETV})
SET (RECV_TYPE_ARG1 ${RECVFROM_TYPE_ARG1})
SET (RECVFROM_TYPE_ARG2 ${RECV_TYPE_ARG2})
SET (SEND_TYPE_ARG1 ${RECVFROM_TYPE_ARG1})
SET (RECV_TYPE_ARG3 ${RECVFROM_TYPE_ARG3})
SET (SEND_TYPE_ARG3 ${RECVFROM_TYPE_ARG3})
SET (GETHOSTNAME_TYPE_ARG2 ${RECVFROM_TYPE_ARG3})

# These should always be "sane" values to use always
SET (RECVFROM_QUAL_ARG5 )
SET (RECVFROM_TYPE_ARG4 int)
SET (RECVFROM_TYPE_ARG5 "struct sockaddr *")
SET (RECV_TYPE_ARG4 int)
SET (GETNAMEINFO_TYPE_ARG1 "struct sockaddr *")
SET (GETNAMEINFO_TYPE_ARG7 int)
SET (SEND_TYPE_ARG2 "const void *")
SET (SEND_TYPE_ARG4 int)
################################################################################


# HAVE_CXX11  ??
# HAVE_SIG_ATOMIC_T_VOLATILE  ??


# Set a few variables by hand that C-Ares wants, logically, based on detection
# data.

IF (HAVE_SOCKLEN_T)
	Set (CARES_TYPEOF_ARES_SOCKLEN_T "socklen_t")
ELSE ()
	Set (CARES_TYPEOF_ARES_SOCKLEN_T "int")
ENDIF ()

IF (HAVE_SSIZE_T)
	Set (CARES_TYPEOF_ARES_SSIZE_T "ssize_t")
ELSE ()
	IF (WIN32)
		IF ("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
			Set (CARES_TYPEOF_ARES_SSIZE_T "__int64")
		ELSE ()
			Set (CARES_TYPEOF_ARES_SSIZE_T "int")
		ENDIF ()
	ELSE ()
		Set (CARES_TYPEOF_ARES_SSIZE_T "long")
	ENDIF ()
ENDIF ()

IF (HAVE_FCNTL AND HAVE_O_NONBLOCK)
	SET (HAVE_FCNTL_O_NONBLOCK 1)
ENDIF ()

IF (HAVE_IOCTL AND HAVE_FIONBIO)
	SET (HAVE_IOCTL_FIONBIO 1)
ENDIF ()

IF (HAVE_IOCTLSOCKET AND HAVE_FIONBIO)
	SET (HAVE_IOCTLSOCKET_FIONBIO 1)
ENDIF ()

IF (HAVE_IOCTLSOCKET_CAMEL AND HAVE_FIONBIO)
	SET (HAVE_IOCTLSOCKET_CAMEL_FIONBIO 1)
ENDIF ()

IF (HAVE_GETADDRINFO)
	IF (CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR
		CMAKE_SYSTEM_NAME STREQUAL "HPUX" OR
		CMAKE_SYSTEM_NAME STREQUAL "NetBSD" OR
		CMAKE_SYSTEM_NAME STREQUAL "SunOS" OR
		CMAKE_SYSTEM_NAME STREQUAL "FreeBSD" OR
		CMAKE_SYSTEM_NAME STREQUAL "AIX" OR
		WIN32)
			SET (HAVE_GETADDRINFO_THREADSAFE 1)
	ENDIF ()
ENDIF ()

IF (HAVE_TIME_H AND HAVE_SYS_TIME_H)
	SET (TIME_WITH_SYS_TIME 1)
ENDIF ()

IF (HAVE_GETSERVBYPORT_R)
	# TODO : Should probably autodetect
	IF (CMAKE_SYSTEM_NAME STREQUAL "SunOS")
		SET (GETSERVBYPORT_R_ARGS 5)
	ELSEIF (CMAKE_SYSTEM_NAME STREQUAL "AIX" OR
		CMAKE_SYSTEM_NAME STREQUAL "OpenBSD" OR
		HAIKU)
		SET (GETSERVBYPORT_R_ARGS 4)
	ELSE ()
		# Probably linux
		SET (GETSERVBYPORT_R_ARGS 6)
	ENDIF ()
ENDIF ()

IF (HAVE_GETSERVBYNAME_R)
	# TODO : Should probably autodetect
	IF (CMAKE_SYSTEM_NAME STREQUAL "SunOS")
		SET (GETSERVBYNAME_R_ARGS 5)
	ELSEIF (CMAKE_SYSTEM_NAME STREQUAL "AIX" OR
		CMAKE_SYSTEM_NAME STREQUAL "OpenBSD" OR
		HAIKU)
		SET (GETSERVBYNAME_R_ARGS 4)
	ELSE ()
		# Probably linux
		SET (GETSERVBYNAME_R_ARGS 6)
	ENDIF ()
ENDIF ()

# Set some aliases used for ares_build.h
IF (HAVE_SYS_TYPES_H)
	SET (CARES_HAVE_SYS_TYPES_H 1)
ENDIF ()
IF (HAVE_SYS_SOCKET_H)
	SET (CARES_HAVE_SYS_SOCKET_H 1)
ENDIF()
IF (HAVE_SYS_SELECT_H)
	SET (CARES_HAVE_SYS_SELECT_H 1)
ENDIF()
IF (HAVE_WS2TCPIP_H)
	SET (CARES_HAVE_WS2TCPIP_H 1)
ENDIF()
IF (HAVE_WINSOCK2_H)
	SET (CARES_HAVE_WINSOCK2_H 1)
ENDIF()
IF (HAVE_WINDOWS_H)
	SET (CARES_HAVE_WINDOWS_H 1)
ENDIF()
IF (HAVE_ARPA_NAMESER_H)
	SET (CARES_HAVE_ARPA_NAMESER_H 1)
ENDIF()
IF (HAVE_ARPA_NAMESER_COMPAT_H)
	SET (CARES_HAVE_ARPA_NAMESER_COMPAT_H 1)
ENDIF()

# Coverage
IF (CARES_COVERAGE)
	# set compiler flags
	SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ftest-coverage -fprofile-arcs")
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ftest-coverage -fprofile-arcs")

	# find required tools
	FIND_PROGRAM(LCOV lcov REQUIRED)
	FIND_PROGRAM(GENHTML genhtml REQUIRED)

	# add coverage target
	ADD_CUSTOM_TARGET(coverage
		# gather data
		COMMAND ${LCOV} --directory . --capture --output-file coverage.info
		# generate report
		COMMAND ${GENHTML} --demangle-cpp -o coverage coverage.info
		WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
	)
ENDIF()

# TRANSFORM_MAKEFILE_INC
#
# This function consumes the "Makefile.inc" autotools file, and converts it into
#  "Makefile.inc.cmake", a cmake include file; transforming this:
#
# CSOURCES = ares__close_sockets.c	\
#   ares__get_hostent.c			\
#   ares__read_line.c			\
#   ...
#
#   into this:
#
# SET (CSOURCES
# 	ares__close_sockets.c
# 	ares__get_hostent.c
# 	ares__read_line.c
#	...
function(TRANSFORM_MAKEFILE_INC INPUT_FILE OUTPUT_FILE)
	file(READ ${INPUT_FILE} MAKEFILE_INC_TEXT)
	string(REPLACE "$(top_srcdir)"   "\${PROJECT_SOURCE_DIR}" MAKEFILE_INC_TEXT ${MAKEFILE_INC_TEXT})
	string(REPLACE "$(top_builddir)" "\${PROJECT_BINARY_DIR}" MAKEFILE_INC_TEXT ${MAKEFILE_INC_TEXT})

	string(REGEX REPLACE "\\\\\n" "ß!ß" MAKEFILE_INC_TEXT ${MAKEFILE_INC_TEXT})
	string(REGEX REPLACE "([a-zA-Z_][a-zA-Z0-9_]*)[\t ]*=[\t ]*([^\n]*)" "SET(\\1 \\2)" MAKEFILE_INC_TEXT ${MAKEFILE_INC_TEXT})
	string(REPLACE "ß!ß" "\n" MAKEFILE_INC_TEXT ${MAKEFILE_INC_TEXT})

	string(REGEX REPLACE "\\$\\(([a-zA-Z_][a-zA-Z0-9_]*)\\)" "\${\\1}" MAKEFILE_INC_TEXT ${MAKEFILE_INC_TEXT})    # Replace $() with ${}
	string(REGEX REPLACE "@([a-zA-Z_][a-zA-Z0-9_]*)@" "\${\\1}" MAKEFILE_INC_TEXT ${MAKEFILE_INC_TEXT})    # Replace @@ with ${}, even if that may not be read by CMake scripts.
	file(WRITE ${OUTPUT_FILE} ${MAKEFILE_INC_TEXT})
endfunction()

# Directory for includes
ADD_SUBDIRECTORY (include)

# Directory for lib and tools
ADD_SUBDIRECTORY (src)

# Docs
ADD_SUBDIRECTORY (docs)

# Tests
IF (CARES_BUILD_TESTS OR CARES_BUILD_CONTAINER_TESTS)
	ENABLE_TESTING ()
	ADD_SUBDIRECTORY (test)
ENDIF ()

# Export targets
IF (CARES_INSTALL)
	SET (CMAKECONFIG_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}")
	INCLUDE (CMakePackageConfigHelpers)
	CONFIGURE_PACKAGE_CONFIG_FILE (${PROJECT_NAME}-config.cmake.in ${PROJECT_NAME}-config.cmake
		INSTALL_DESTINATION ${CMAKECONFIG_INSTALL_DIR}
		PATH_VARS CMAKE_INSTALL_INCLUDEDIR
		NO_CHECK_REQUIRED_COMPONENTS_MACRO
	)

	WRITE_BASIC_PACKAGE_VERSION_FILE(${PROJECT_NAME}-config-version.cmake VERSION "${CARES_VERSION}" COMPATIBILITY SameMajorVersion)
	INSTALL (EXPORT ${PROJECT_NAME}-targets COMPONENT Devel DESTINATION ${CMAKECONFIG_INSTALL_DIR} NAMESPACE ${PROJECT_NAME}::)
	INSTALL (FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake" "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake" COMPONENT Devel DESTINATION ${CMAKECONFIG_INSTALL_DIR})

	# pkgconfig support for static builds
	FOREACH (LIB ${CARES_DEPENDENT_LIBS})
		SET (CARES_PRIVATE_LIBS "${CARES_PRIVATE_LIBS} -l${LIB}")
	ENDFOREACH ()

	CONFIGURE_FILE("libcares.pc.cmake" "libcares.pc" @ONLY)
	INSTALL (FILES "${CMAKE_CURRENT_BINARY_DIR}/libcares.pc" COMPONENT Devel DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
ENDIF ()

# Legacy chain-building variables (provided for compatibility with old code).
# Don't use these, external code should be updated to refer to the aliases directly (e.g., Cares::cares).
SET (CARES_FOUND 1 CACHE INTERNAL "CARES LIBRARY FOUND")
SET (CARES_LIBRARIES ${PROJECT_NAME}::cares CACHE INTERNAL "CARES LIBRARIES")


IF (CARES_INSTALL)
    # Package creation
    set( CPACK_PACKAGE_NAME ${PROJECT_NAME} )
    set( CPACK_PACKAGE_VENDOR "Daniel Stenberg" ) # Github project owner
    set( CPACK_PACKAGE_DESCRIPTION_SUMMARY "A C library for asynchronous DNS requests" )
    set( CPACK_PACKAGE_HOMEPAGE_URL "https://c-ares.org/" )
    set( CPACK_PACKAGE_CONTACT      "https://c-ares.org/" )
    set( CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR} )
    set( CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR} )
    set( CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH} )
    set( CPACK_PACKAGE_VERSION ${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH} )
    set( CPACK_PACKAGE_INSTALL_DIRECTORY ${PROJECT_NAME} )
    set( CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md" )
    set( CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md" )

    set( CPACK_COMPONENT_Library_DISPLAY_NAME "c-ares Library" )
    set( CPACK_COMPONENT_Library_DESCRIPTION "The c-ares binary library." )
    set( CPACK_COMPONENT_Library_REQUIRED 1 )
    set( CPACK_COMPONENT_Devel_DISPLAY_NAME "c-ares Development Files" )
    set( CPACK_COMPONENT_Devel_DESCRIPTION "Development files for compiling against c-ares." )
    set( CPACK_COMPONENT_Devel_REQUIRED 0 )
    IF (CARES_BUILD_TOOLS)
        set( CPACK_COMPONENT_Tools_DISPLAY_NAME "c-ares Tools" )
        set( CPACK_COMPONENT_Tools_DESCRIPTION "Tools for using c-ares." )
        set( CPACK_COMPONENT_Tools_REQUIRED 0 )
    ENDIF ()

    if( ${CMAKE_SYSTEM_NAME} STREQUAL "Linux" )

        if ( "${CPACK_PACKAGE_ARCHITECTURE}" STREQUAL "" )
            set( CPACK_PACKAGE_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}" )
        endif()
        if ( "${CPACK_PACKAGE_ARCHITECTURE}" STREQUAL "" )
            if ( "${CMAKE_HOST_SYSTEM_NAME}" STREQUAL "Windows" )
                message( FATAL_ERROR "Failed to determine CPACK_PACKAGE_ARCHITECTURE. Is CMAKE_SYSTEM_PROCESSOR set?" )
            endif()
            # Note: the architecture should default to the local architecture, but it
            # in fact comes up empty.  We call `uname -m` to ask the kernel instead.
            EXECUTE_PROCESS( COMMAND uname -m COMMAND tr -d '\n' OUTPUT_VARIABLE CPACK_PACKAGE_ARCHITECTURE )
        endif()

        set( CPACK_INCLUDE_TOPLEVEL_DIRECTORY 1 )
        set( CPACK_PACKAGE_RELEASE 1 )


        # RPM - https://cmake.org/cmake/help/latest/cpack_gen/rpm.html
        set( CPACK_RPM_PACKAGE_RELEASE ${CPACK_PACKAGE_RELEASE} )
        set( CPACK_RPM_PACKAGE_ARCHITECTURE ${CPACK_PACKAGE_ARCHITECTURE} )
        set( CPACK_RPM_PACKAGE_DESCRIPTION ${CPACK_PACKAGE_DESCRIPTION_SUMMARY} )
        set( CPACK_RPM_PACKAGE_URL ${CPACK_PACKAGE_HOMEPAGE_URL} )
        set( CPACK_RPM_PACKAGE_LICENSE "MIT" )
        set( CPACK_RPM_COMPONENT_INSTALL 1 )
        set( CPACK_RPM_COMPRESSION_TYPE "xz" )
        set( CPACK_RPM_PACKAGE_AUTOPROV 1 )

        set( CPACK_RPM_Library_PACKAGE_SUMMARY ${CPACK_COMPONENT_Library_DESCRIPTION} )
        set( CPACK_RPM_Library_PACKAGE_ARCHITECTURE ${CPACK_RPM_PACKAGE_ARCHITECTURE} )
        set( CPACK_RPM_Library_PACKAGE_NAME "libcares${CARES_LIB_VERSION_MAJOR}" )
        set( CPACK_RPM_Library_FILE_NAME "RPM-DEFAULT" )

        set( CPACK_RPM_Devel_PACKAGE_REQUIRES "cmake >= ${CMAKE_MINIMUM_REQUIRED_VERSION}, ${CPACK_RPM_Library_PACKAGE_NAME} >= ${CPACK_PACKAGE_VERSION}" )
        set( CPACK_RPM_Devel_PACKAGE_SUMMARY ${CPACK_COMPONENT_Devel_DESCRIPTION} )
        set( CPACK_RPM_Devel_PACKAGE_ARCHITECTURE ${CPACK_RPM_PACKAGE_ARCHITECTURE} )
        set( CPACK_RPM_Devel_PACKAGE_NAME "${CPACK_PACKAGE_NAME}-devel" )
        set( CPACK_RPM_Devel_FILE_NAME "RPM-DEFAULT" )

        IF (CARES_BUILD_TOOLS)
            set( CPACK_RPM_Tools_PACKAGE_REQUIRES "${CPACK_RPM_Library_PACKAGE_NAME} >= ${CPACK_PACKAGE_VERSION}" )
            set( CPACK_RPM_Tools_PACKAGE_SUMMARY ${CPACK_COMPONENT_Tools_DESCRIPTION} )
            set( CPACK_RPM_Tools_PACKAGE_ARCHITECTURE ${CPACK_RPM_PACKAGE_ARCHITECTURE} )
            set( CPACK_RPM_Tools_PACKAGE_NAME "${CPACK_PACKAGE_NAME}-tools" )
            set( CPACK_RPM_Tools_FILE_NAME "RPM-DEFAULT" )
        ENDIF ()


        # DEB - https://cmake.org/cmake/help/latest/cpack_gen/deb.html
        set( CPACK_DEBIAN_PACKAGE_RELEASE ${CPACK_PACKAGE_RELEASE} )
        set( CPACK_DEBIAN_PACKAGE_HOMEPAGE ${CPACK_PACKAGE_HOMEPAGE_URL} )
        set( CPACK_DEB_COMPONENT_INSTALL 1 )
        set( CPACK_DEBIAN_COMPRESSION_TYPE "xz")
        set( CPACK_DEBIAN_PACKAGE_SHLIBDEPS 1 )

        if ( ${CPACK_PACKAGE_ARCHITECTURE} STREQUAL "x86_64" )
            set( CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64" )  # DEB doesn't always use the kernel's arch name
        else()
            set( CPACK_DEBIAN_PACKAGE_ARCHITECTURE ${CPACK_PACKAGE_ARCHITECTURE} )
        endif()

        set( CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT" ) # Use default naming scheme

        set( CPACK_DEBIAN_LIBRARY_PACKAGE_NAME ${CPACK_RPM_Library_PACKAGE_NAME} )

        set( CPACK_DEBIAN_DEVEL_PACKAGE_DEPENDS "cmake (>= ${CMAKE_MINIMUM_REQUIRED_VERSION}), ${CPACK_DEBIAN_LIBRARY_PACKAGE_NAME} (>= ${CPACK_PACKAGE_VERSION})" )
        set( CPACK_DEBIAN_DEVEL_PACKAGE_NAME "${CPACK_PACKAGE_NAME}-dev" )

        IF (CARES_BUILD_TOOLS)
            set( CPACK_DEBIAN_TOOLS_PACKAGE_NAME "${CPACK_PACKAGE_NAME}-tools" )
            set( CPACK_DEBIAN_TOOLS_PACKAGE_SHLIBDEPS OFF ) # dpkg-shlibdeps can't find the libs we built
            set( CPACK_DEBIAN_TOOLS_PACKAGE_DEPENDS "${CPACK_DEBIAN_LIBRARY_PACKAGE_NAME} (>= ${CPACK_PACKAGE_VERSION})" )
        ENDIF ()

    elseif( ${CMAKE_HOST_WIN32} )
        set( CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON )
        set( CPACK_NSIS_DISPLAY_NAME ${PROJECT_NAME} )
        set( CPACK_NSIS_PACKAGE_NAME ${PROJECT_NAME} )
        set( CPACK_NSIS_URL_INFO_ABOUT ${CPACK_PACKAGE_HOMEPAGE_URL} )
    endif()

    # This must always be last!
    include( CPack )
ENDIF ()
