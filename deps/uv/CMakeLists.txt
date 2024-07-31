cmake_minimum_required(VERSION 3.9)

if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW) # Enable MSVC_RUNTIME_LIBRARY setting
endif()
if(POLICY CMP0092)
  cmake_policy(SET CMP0092 NEW) # disable /W3 warning, if possible
endif()

project(libuv LANGUAGES C)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(CMakePackageConfigHelpers)
include(CMakeDependentOption)
include(CheckCCompilerFlag)
include(GNUInstallDirs)
include(CTest)

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_C_STANDARD 90)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LIBUV_BUILD_SHARED "Build shared lib" ON)

cmake_dependent_option(LIBUV_BUILD_TESTS
  "Build the unit tests when BUILD_TESTING is enabled and we are the root project" ON
  "BUILD_TESTING;LIBUV_BUILD_SHARED;CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR" OFF)
cmake_dependent_option(LIBUV_BUILD_BENCH
  "Build the benchmarks when building unit tests and we are the root project" ON
  "LIBUV_BUILD_TESTS" OFF)

# Qemu Build
option(QEMU "build for qemu" OFF)
if(QEMU)
  list(APPEND uv_defines __QEMU__=1)
endif()

# Note: these are mutually exclusive.
option(ASAN "Enable AddressSanitizer (ASan)" OFF)
option(MSAN "Enable MemorySanitizer (MSan)" OFF)
option(TSAN "Enable ThreadSanitizer (TSan)" OFF)
option(UBSAN "Enable UndefinedBehaviorSanitizer (UBSan)" OFF)

if(MSAN AND NOT CMAKE_C_COMPILER_ID MATCHES "AppleClang|Clang")
  message(SEND_ERROR "MemorySanitizer requires clang. Try again with -DCMAKE_C_COMPILER=clang")
endif()

if(ASAN)
  list(APPEND uv_defines __ASAN__=1)
  if(CMAKE_C_COMPILER_ID MATCHES "AppleClang|GNU|Clang")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-omit-frame-pointer -fsanitize=address")
    set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fno-omit-frame-pointer -fsanitize=address")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fno-omit-frame-pointer -fsanitize=address")
  elseif(MSVC)
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /fsanitize=address")
  else()
    message(SEND_ERROR "AddressSanitizer support requires clang, gcc, or msvc. Try again with -DCMAKE_C_COMPILER.")
  endif()
endif()

if(MSAN)
  list(APPEND uv_defines __MSAN__=1)
  set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-omit-frame-pointer -fsanitize=memory")
  set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fno-omit-frame-pointer -fsanitize=memory")
  set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fno-omit-frame-pointer -fsanitize=memory")
endif()

if(TSAN)
  list(APPEND uv_defines __TSAN__=1)
  if(CMAKE_C_COMPILER_ID MATCHES "AppleClang|GNU|Clang")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-omit-frame-pointer -fsanitize=thread")
    set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fno-omit-frame-pointer -fsanitize=thread")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fno-omit-frame-pointer -fsanitize=thread")
  else()
    message(SEND_ERROR "ThreadSanitizer support requires clang or gcc. Try again with -DCMAKE_C_COMPILER.")
  endif()
endif()

if(UBSAN)
  list(APPEND uv_defines __UBSAN__=1)
  if(CMAKE_C_COMPILER_ID MATCHES "AppleClang|GNU|Clang")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-omit-frame-pointer -fsanitize=undefined")
    set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fno-omit-frame-pointer -fsanitize=undefined")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fno-omit-frame-pointer -fsanitize=undefined")
  elseif(MSVC)
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /fsanitize=undefined")
  else()
    message(SEND_ERROR "UndefinedBehaviorSanitizer support requires clang, gcc, or msvc. Try again with -DCMAKE_C_COMPILER.")
  endif()
endif()

# Compiler check
string(CONCAT is-msvc $<OR:
  $<C_COMPILER_ID:MSVC>,
  $<STREQUAL:${CMAKE_C_COMPILER_FRONTEND_VARIANT},MSVC>
>)

check_c_compiler_flag(/W4 UV_LINT_W4)
check_c_compiler_flag(/wd4100 UV_LINT_NO_UNUSED_PARAMETER_MSVC)
check_c_compiler_flag(/wd4127 UV_LINT_NO_CONDITIONAL_CONSTANT_MSVC)
check_c_compiler_flag(/wd4201 UV_LINT_NO_NONSTANDARD_MSVC)
check_c_compiler_flag(/wd4206 UV_LINT_NO_NONSTANDARD_EMPTY_TU_MSVC)
check_c_compiler_flag(/wd4210 UV_LINT_NO_NONSTANDARD_FILE_SCOPE_MSVC)
check_c_compiler_flag(/wd4232 UV_LINT_NO_NONSTANDARD_NONSTATIC_DLIMPORT_MSVC)
check_c_compiler_flag(/wd4456 UV_LINT_NO_HIDES_LOCAL)
check_c_compiler_flag(/wd4457 UV_LINT_NO_HIDES_PARAM)
check_c_compiler_flag(/wd4459 UV_LINT_NO_HIDES_GLOBAL)
check_c_compiler_flag(/wd4706 UV_LINT_NO_CONDITIONAL_ASSIGNMENT_MSVC)
check_c_compiler_flag(/wd4996 UV_LINT_NO_UNSAFE_MSVC)

check_c_compiler_flag(-Wall UV_LINT_WALL) # DO NOT use this under MSVC

# TODO: Place these into its own function
check_c_compiler_flag(-Wno-unused-parameter UV_LINT_NO_UNUSED_PARAMETER)
check_c_compiler_flag(-Wstrict-prototypes UV_LINT_STRICT_PROTOTYPES)
check_c_compiler_flag(-Wextra UV_LINT_EXTRA)

check_c_compiler_flag(/utf-8 UV_LINT_UTF8_MSVC)

set(lint-no-unused-parameter $<$<BOOL:${UV_LINT_NO_UNUSED_PARAMETER}>:-Wno-unused-parameter>)
set(lint-strict-prototypes $<$<BOOL:${UV_LINT_STRICT_PROTOTYPES}>:-Wstrict-prototypes>)
set(lint-extra $<$<BOOL:${UV_LINT_EXTRA}>:-Wextra>)
set(lint-w4 $<$<BOOL:${UV_LINT_W4}>:/W4>)
set(lint-no-unused-parameter-msvc $<$<BOOL:${UV_LINT_NO_UNUSED_PARAMETER_MSVC}>:/wd4100>)
set(lint-no-conditional-constant-msvc $<$<BOOL:${UV_LINT_NO_CONDITIONAL_CONSTANT_MSVC}>:/wd4127>)
set(lint-no-nonstandard-msvc $<$<BOOL:${UV_LINT_NO_NONSTANDARD_MSVC}>:/wd4201>)
set(lint-no-nonstandard-empty-tu-msvc $<$<BOOL:${UV_LINT_NO_NONSTANDARD_EMPTY_TU_MSVC}>:/wd4206>)
set(lint-no-nonstandard-file-scope-msvc $<$<BOOL:${UV_LINT_NO_NONSTANDARD_FILE_SCOPE_MSVC}>:/wd4210>)
set(lint-no-nonstandard-nonstatic-dlimport-msvc $<$<BOOL:${UV_LINT_NO_NONSTANDARD_NONSTATIC_DLIMPORT_MSVC}>:/wd4232>)
set(lint-no-hides-local-msvc $<$<BOOL:${UV_LINT_NO_HIDES_LOCAL}>:/wd4456>)
set(lint-no-hides-param-msvc $<$<BOOL:${UV_LINT_NO_HIDES_PARAM}>:/wd4457>)
set(lint-no-hides-global-msvc $<$<BOOL:${UV_LINT_NO_HIDES_GLOBAL}>:/wd4459>)
set(lint-no-conditional-assignment-msvc $<$<BOOL:${UV_LINT_NO_CONDITIONAL_ASSIGNMENT_MSVC}>:/wd4706>)
set(lint-no-unsafe-msvc $<$<BOOL:${UV_LINT_NO_UNSAFE_MSVC}>:/wd4996>)
# Unfortunately, this one is complicated because MSVC and clang-cl support -Wall
# but using it is like calling -Weverything
string(CONCAT lint-default $<
  $<AND:$<BOOL:${UV_LINT_WALL}>,$<NOT:${is-msvc}>>:-Wall
>)
set(lint-utf8-msvc $<$<BOOL:${UV_LINT_UTF8_MSVC}>:/utf-8>)

list(APPEND uv_cflags ${lint-strict-prototypes} ${lint-extra} ${lint-default} ${lint-w4})
list(APPEND uv_cflags ${lint-no-unused-parameter})
list(APPEND uv_cflags ${lint-no-unused-parameter-msvc})
list(APPEND uv_cflags ${lint-no-conditional-constant-msvc})
list(APPEND uv_cflags ${lint-no-nonstandard-msvc})
list(APPEND uv_cflags ${lint-no-nonstandard-empty-tu-msvc})
list(APPEND uv_cflags ${lint-no-nonstandard-file-scope-msvc})
list(APPEND uv_cflags ${lint-no-nonstandard-nonstatic-dlimport-msvc})
list(APPEND uv_cflags ${lint-no-hides-local-msvc})
list(APPEND uv_cflags ${lint-no-hides-param-msvc})
list(APPEND uv_cflags ${lint-no-hides-global-msvc})
list(APPEND uv_cflags ${lint-no-conditional-assignment-msvc})
list(APPEND uv_cflags ${lint-no-unsafe-msvc})
list(APPEND uv_cflags ${lint-utf8-msvc} )

check_c_compiler_flag(-fno-strict-aliasing UV_F_STRICT_ALIASING)
list(APPEND uv_cflags $<$<BOOL:${UV_F_STRICT_ALIASING}>:-fno-strict-aliasing>)

if (MSVC)
  # Error on calling undeclared functions.
  list(APPEND uv_cflags "/we4013")
endif()

set(uv_sources
    src/fs-poll.c
    src/idna.c
    src/inet.c
    src/random.c
    src/strscpy.c
    src/strtok.c
    src/thread-common.c
    src/threadpool.c
    src/timer.c
    src/uv-common.c
    src/uv-data-getter-setters.c
    src/version.c)

if(WIN32)
  list(APPEND uv_defines WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0602 _CRT_DECLARE_NONSTDC_NAMES=0)
  list(APPEND uv_libraries
       psapi
       user32
       advapi32
       iphlpapi
       userenv
       ws2_32
       dbghelp
       ole32
       shell32)
  list(APPEND uv_sources
       src/win/async.c
       src/win/core.c
       src/win/detect-wakeup.c
       src/win/dl.c
       src/win/error.c
       src/win/fs.c
       src/win/fs-event.c
       src/win/getaddrinfo.c
       src/win/getnameinfo.c
       src/win/handle.c
       src/win/loop-watcher.c
       src/win/pipe.c
       src/win/thread.c
       src/win/poll.c
       src/win/process.c
       src/win/process-stdio.c
       src/win/signal.c
       src/win/snprintf.c
       src/win/stream.c
       src/win/tcp.c
       src/win/tty.c
       src/win/udp.c
       src/win/util.c
       src/win/winapi.c
       src/win/winsock.c)
  list(APPEND uv_test_libraries ws2_32)
  list(APPEND uv_test_sources src/win/snprintf.c test/runner-win.c)
else()
  list(APPEND uv_defines _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE)
  if(NOT CMAKE_SYSTEM_NAME MATCHES "Android|OS390|QNX")
    # TODO: This should be replaced with find_package(Threads) if possible
    # Android has pthread as part of its c library, not as a separate
    # libpthread.so.
    list(APPEND uv_libraries pthread)
  endif()
  list(APPEND uv_sources
       src/unix/async.c
       src/unix/core.c
       src/unix/dl.c
       src/unix/fs.c
       src/unix/getaddrinfo.c
       src/unix/getnameinfo.c
       src/unix/loop-watcher.c
       src/unix/loop.c
       src/unix/pipe.c
       src/unix/poll.c
       src/unix/process.c
       src/unix/random-devurandom.c
       src/unix/signal.c
       src/unix/stream.c
       src/unix/tcp.c
       src/unix/thread.c
       src/unix/tty.c
       src/unix/udp.c)
  list(APPEND uv_test_sources test/runner-unix.c)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "AIX")
  list(APPEND uv_defines
       _ALL_SOURCE
       _LINUX_SOURCE_COMPAT
       _THREAD_SAFE
       _XOPEN_SOURCE=500
       HAVE_SYS_AHAFS_EVPRODS_H)
  list(APPEND uv_libraries perfstat)
  list(APPEND uv_sources
       src/unix/aix.c
       src/unix/aix-common.c)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Android")
  list(APPEND uv_defines _GNU_SOURCE)
  list(APPEND uv_libraries dl)
  list(APPEND uv_sources
       src/unix/linux.c
       src/unix/procfs-exepath.c
       src/unix/random-getentropy.c
       src/unix/random-getrandom.c
       src/unix/random-sysctl-linux.c)
endif()

if(APPLE OR CMAKE_SYSTEM_NAME MATCHES "Android|Linux")
  list(APPEND uv_sources src/unix/proctitle.c)
endif()

if(CMAKE_SYSTEM_NAME MATCHES "DragonFly|FreeBSD")
  list(APPEND uv_sources src/unix/freebsd.c)
endif()

if(CMAKE_SYSTEM_NAME MATCHES "DragonFly|FreeBSD|NetBSD|OpenBSD")
  list(APPEND uv_sources src/unix/posix-hrtime.c src/unix/bsd-proctitle.c)
endif()

if(APPLE OR CMAKE_SYSTEM_NAME MATCHES "DragonFly|FreeBSD|NetBSD|OpenBSD")
  list(APPEND uv_sources src/unix/bsd-ifaddrs.c src/unix/kqueue.c)
endif()

if(CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
  list(APPEND uv_sources src/unix/random-getrandom.c)
endif()

if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
  list(APPEND uv_sources src/unix/random-getentropy.c)
endif()

if(APPLE)
  list(APPEND uv_defines _DARWIN_UNLIMITED_SELECT=1 _DARWIN_USE_64_BIT_INODE=1)
  list(APPEND uv_sources
       src/unix/darwin-proctitle.c
       src/unix/darwin.c
       src/unix/fsevents.c)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "GNU")
  list(APPEND uv_libraries dl)
  list(APPEND uv_sources
       src/unix/bsd-ifaddrs.c
       src/unix/no-fsevents.c
       src/unix/no-proctitle.c
       src/unix/posix-hrtime.c
       src/unix/posix-poll.c
       src/unix/hurd.c)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND uv_defines _GNU_SOURCE _POSIX_C_SOURCE=200112)
  list(APPEND uv_libraries dl rt)
  list(APPEND uv_sources
       src/unix/linux.c
       src/unix/procfs-exepath.c
       src/unix/random-getrandom.c
       src/unix/random-sysctl-linux.c)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "NetBSD")
  list(APPEND uv_sources src/unix/netbsd.c)
  list(APPEND uv_libraries kvm)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
  list(APPEND uv_sources src/unix/openbsd.c)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "OS390")
  enable_language(CXX)
  list(APPEND uv_defines PATH_MAX=1024)
  list(APPEND uv_defines _AE_BIMODAL)
  list(APPEND uv_defines _ALL_SOURCE)
  list(APPEND uv_defines _ENHANCED_ASCII_EXT=0xFFFFFFFF)
  list(APPEND uv_defines _ISOC99_SOURCE)
  list(APPEND uv_defines _LARGE_TIME_API)
  list(APPEND uv_defines _OPEN_MSGQ_EXT)
  list(APPEND uv_defines _OPEN_SYS_FILE_EXT)
  list(APPEND uv_defines _OPEN_SYS_IF_EXT)
  list(APPEND uv_defines _OPEN_SYS_SOCK_EXT3)
  list(APPEND uv_defines _OPEN_SYS_SOCK_IPV6)
  list(APPEND uv_defines _UNIX03_SOURCE)
  list(APPEND uv_defines _UNIX03_THREADS)
  list(APPEND uv_defines _UNIX03_WITHDRAWN)
  list(APPEND uv_defines _XOPEN_SOURCE=600)
  list(APPEND uv_defines _XOPEN_SOURCE_EXTENDED)
  list(APPEND uv_sources
       src/unix/os390.c
       src/unix/os390-syscalls.c
       src/unix/os390-proctitle.c)
  list(APPEND uv_cflags
       -q64
       -qascii
       -qexportall
       -qgonumber
       -qlongname
       -qlibansi
       -qfloat=IEEE
       -qtune=10
       -qarch=10
       -qasm
       -qasmlib=sys1.maclib:sys1.modgen)
  find_library(ZOSLIB
    NAMES zoslib
    PATHS ${ZOSLIB_DIR}
    PATH_SUFFIXES lib
  )
  list(APPEND uv_libraries ${ZOSLIB})
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "OS400")
  list(APPEND uv_defines
       _ALL_SOURCE
       _LINUX_SOURCE_COMPAT
       _THREAD_SAFE
       _XOPEN_SOURCE=500)
  list(APPEND uv_sources
    src/unix/aix-common.c
    src/unix/ibmi.c
    src/unix/no-fsevents.c
    src/unix/posix-poll.c)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "SunOS")
  if(CMAKE_SYSTEM_VERSION STREQUAL "5.10")
    list(APPEND uv_defines SUNOS_NO_IFADDRS)
    list(APPEND uv_libraries rt)
  endif()
  list(APPEND uv_defines __EXTENSIONS__ _XOPEN_SOURCE=500 _REENTRANT)
  list(APPEND uv_libraries kstat nsl sendfile socket)
  list(APPEND uv_sources
       src/unix/no-proctitle.c
       src/unix/sunos.c)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
  list(APPEND uv_defines _BSD_SOURCE)
  list(APPEND uv_libraries bsd network)
  list(APPEND uv_sources
	  src/unix/haiku.c
	  src/unix/bsd-ifaddrs.c
	  src/unix/no-fsevents.c
	  src/unix/no-proctitle.c
	  src/unix/posix-hrtime.c
	  src/unix/posix-poll.c)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "QNX")
  list(APPEND uv_sources
    src/unix/posix-hrtime.c
    src/unix/posix-poll.c
    src/unix/qnx.c
    src/unix/bsd-ifaddrs.c
    src/unix/no-proctitle.c
    src/unix/no-fsevents.c)
  list(APPEND uv_libraries socket)
endif()

if(APPLE OR CMAKE_SYSTEM_NAME MATCHES "DragonFly|FreeBSD|Linux|NetBSD|OpenBSD")
  list(APPEND uv_test_libraries util)
endif()

if(CYGWIN OR MSYS)
  list(APPEND uv_defines _GNU_SOURCE)
  list(APPEND uv_sources
       src/unix/cygwin.c
       src/unix/bsd-ifaddrs.c
       src/unix/no-fsevents.c
       src/unix/no-proctitle.c
       src/unix/posix-hrtime.c
       src/unix/posix-poll.c
       src/unix/procfs-exepath.c
       src/unix/sysinfo-loadavg.c
       src/unix/sysinfo-memory.c)
endif()

if(LIBUV_BUILD_SHARED)
  add_library(uv SHARED ${uv_sources})
  target_compile_definitions(uv
    INTERFACE
      USING_UV_SHARED=1
    PRIVATE
      BUILDING_UV_SHARED=1
      ${uv_defines})
  target_compile_options(uv PRIVATE ${uv_cflags})
  target_include_directories(uv
    PUBLIC
      $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
      $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>)
  if(CMAKE_SYSTEM_NAME STREQUAL "OS390")
    target_include_directories(uv PUBLIC $<BUILD_INTERFACE:${ZOSLIB_DIR}/include>)
    set_target_properties(uv PROPERTIES LINKER_LANGUAGE CXX)
  endif()
  target_link_libraries(uv ${uv_libraries})
  set_target_properties(uv PROPERTIES OUTPUT_NAME "uv")
endif()

add_library(uv_a STATIC ${uv_sources})
target_compile_definitions(uv_a PRIVATE ${uv_defines})
target_compile_options(uv_a PRIVATE ${uv_cflags})
target_include_directories(uv_a
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>)
if(CMAKE_SYSTEM_NAME STREQUAL "OS390")
  target_include_directories(uv_a PUBLIC $<BUILD_INTERFACE:${ZOSLIB_DIR}/include>)
  set_target_properties(uv_a PROPERTIES LINKER_LANGUAGE CXX)
endif()
target_link_libraries(uv_a ${uv_libraries})
set_target_properties(uv_a PROPERTIES OUTPUT_NAME "uv")
if(WIN32)
  set_target_properties(uv_a PROPERTIES PREFIX "lib")
endif()

if(LIBUV_BUILD_TESTS)
  # Small hack: use ${uv_test_sources} now to get the runner skeleton,
  # before the actual tests are added.
  add_executable(
    uv_run_benchmarks_a
    ${uv_test_sources}
    test/benchmark-async-pummel.c
    test/benchmark-async.c
    test/benchmark-fs-stat.c
    test/benchmark-getaddrinfo.c
    test/benchmark-loop-count.c
    test/benchmark-queue-work.c
    test/benchmark-million-async.c
    test/benchmark-million-timers.c
    test/benchmark-multi-accept.c
    test/benchmark-ping-pongs.c
    test/benchmark-ping-udp.c
    test/benchmark-pound.c
    test/benchmark-pump.c
    test/benchmark-sizes.c
    test/benchmark-spawn.c
    test/benchmark-tcp-write-batch.c
    test/benchmark-thread.c
    test/benchmark-udp-pummel.c
    test/blackhole-server.c
    test/echo-server.c
    test/run-benchmarks.c
    test/runner.c)
  target_compile_definitions(uv_run_benchmarks_a PRIVATE ${uv_defines})
  target_compile_options(uv_run_benchmarks_a PRIVATE ${uv_cflags})
  target_link_libraries(uv_run_benchmarks_a uv_a ${uv_test_libraries})

  list(APPEND uv_test_sources
       test/blackhole-server.c
       test/echo-server.c
       test/run-tests.c
       test/runner.c
       test/test-active.c
       test/test-async-null-cb.c
       test/test-async.c
       test/test-barrier.c
       test/test-callback-stack.c
       test/test-close-fd.c
       test/test-close-order.c
       test/test-condvar.c
       test/test-connect-unspecified.c
       test/test-connection-fail.c
       test/test-cwd-and-chdir.c
       test/test-default-loop-close.c
       test/test-delayed-accept.c
       test/test-dlerror.c
       test/test-eintr-handling.c
       test/test-embed.c
       test/test-emfile.c
       test/test-env-vars.c
       test/test-error.c
       test/test-fail-always.c
       test/test-fork.c
       test/test-fs-copyfile.c
       test/test-fs-event.c
       test/test-fs-poll.c
       test/test-fs.c
       test/test-fs-readdir.c
       test/test-fs-fd-hash.c
       test/test-fs-open-flags.c
       test/test-get-currentexe.c
       test/test-get-loadavg.c
       test/test-get-memory.c
       test/test-get-passwd.c
       test/test-getaddrinfo.c
       test/test-gethostname.c
       test/test-getnameinfo.c
       test/test-getsockname.c
       test/test-getters-setters.c
       test/test-gettimeofday.c
       test/test-handle-fileno.c
       test/test-homedir.c
       test/test-hrtime.c
       test/test-idle.c
       test/test-idna.c
       test/test-ip4-addr.c
       test/test-ip6-addr.c
       test/test-ip-name.c
       test/test-ipc-heavy-traffic-deadlock-bug.c
       test/test-ipc-send-recv.c
       test/test-ipc.c
       test/test-loop-alive.c
       test/test-loop-close.c
       test/test-loop-configure.c
       test/test-loop-handles.c
       test/test-loop-stop.c
       test/test-loop-time.c
       test/test-metrics.c
       test/test-multiple-listen.c
       test/test-mutexes.c
       test/test-not-readable-nor-writable-on-read-error.c
       test/test-not-writable-after-shutdown.c
       test/test-osx-select.c
       test/test-pass-always.c
       test/test-ping-pong.c
       test/test-pipe-bind-error.c
       test/test-pipe-close-stdout-read-stdin.c
       test/test-pipe-connect-error.c
       test/test-pipe-connect-multiple.c
       test/test-pipe-connect-prepare.c
       test/test-pipe-getsockname.c
       test/test-pipe-pending-instances.c
       test/test-pipe-sendmsg.c
       test/test-pipe-server-close.c
       test/test-pipe-set-fchmod.c
       test/test-pipe-set-non-blocking.c
       test/test-platform-output.c
       test/test-poll-close-doesnt-corrupt-stack.c
       test/test-poll-close.c
       test/test-poll-closesocket.c
       test/test-poll-multiple-handles.c
       test/test-poll-oob.c
       test/test-poll.c
       test/test-process-priority.c
       test/test-process-title-threadsafe.c
       test/test-process-title.c
       test/test-queue-foreach-delete.c
       test/test-random.c
       test/test-readable-on-eof.c
       test/test-ref.c
       test/test-run-nowait.c
       test/test-run-once.c
       test/test-semaphore.c
       test/test-shutdown-close.c
       test/test-shutdown-eof.c
       test/test-shutdown-simultaneous.c
       test/test-shutdown-twice.c
       test/test-signal-multiple-loops.c
       test/test-signal-pending-on-close.c
       test/test-signal.c
       test/test-socket-buffer-size.c
       test/test-spawn.c
       test/test-stdio-over-pipes.c
       test/test-strscpy.c
       test/test-strtok.c
       test/test-tcp-alloc-cb-fail.c
       test/test-tcp-bind-error.c
       test/test-tcp-bind6-error.c
       test/test-tcp-close-accept.c
       test/test-tcp-close-after-read-timeout.c
       test/test-tcp-close-while-connecting.c
       test/test-tcp-close.c
       test/test-tcp-close-reset.c
       test/test-tcp-connect-error-after-write.c
       test/test-tcp-connect-error.c
       test/test-tcp-connect-timeout.c
       test/test-tcp-connect6-error.c
       test/test-tcp-create-socket-early.c
       test/test-tcp-flags.c
       test/test-tcp-oob.c
       test/test-tcp-open.c
       test/test-tcp-read-stop.c
       test/test-tcp-read-stop-start.c
       test/test-tcp-rst.c
       test/test-tcp-shutdown-after-write.c
       test/test-tcp-try-write.c
       test/test-tcp-write-in-a-row.c
       test/test-tcp-try-write-error.c
       test/test-tcp-unexpected-read.c
       test/test-tcp-write-after-connect.c
       test/test-tcp-write-fail.c
       test/test-tcp-write-queue-order.c
       test/test-tcp-write-to-half-open-connection.c
       test/test-tcp-writealot.c
       test/test-test-macros.c
       test/test-thread-affinity.c
       test/test-thread-equal.c
       test/test-thread.c
       test/test-thread-priority.c
       test/test-threadpool-cancel.c
       test/test-threadpool.c
       test/test-timer-again.c
       test/test-timer-from-check.c
       test/test-timer.c
       test/test-tmpdir.c
       test/test-tty-duplicate-key.c
       test/test-tty-escape-sequence-processing.c
       test/test-tty.c
       test/test-udp-alloc-cb-fail.c
       test/test-udp-bind.c
       test/test-udp-connect.c
       test/test-udp-connect6.c
       test/test-udp-create-socket-early.c
       test/test-udp-dgram-too-big.c
       test/test-udp-ipv6.c
       test/test-udp-mmsg.c
       test/test-udp-multicast-interface.c
       test/test-udp-multicast-interface6.c
       test/test-udp-multicast-join.c
       test/test-udp-multicast-join6.c
       test/test-udp-multicast-ttl.c
       test/test-udp-open.c
       test/test-udp-options.c
       test/test-udp-send-and-recv.c
       test/test-udp-send-hang-loop.c
       test/test-udp-send-immediate.c
       test/test-udp-sendmmsg-error.c
       test/test-udp-send-unreachable.c
       test/test-udp-try-send.c
       test/test-udp-recv-in-a-row.c
       test/test-uname.c
       test/test-walk-handles.c
       test/test-watcher-cross-stop.c)

  add_executable(uv_run_tests ${uv_test_sources} uv_win_longpath.manifest)
  target_compile_definitions(uv_run_tests
                             PRIVATE ${uv_defines} USING_UV_SHARED=1)
  target_compile_options(uv_run_tests PRIVATE ${uv_cflags})
  target_link_libraries(uv_run_tests uv ${uv_test_libraries})
  add_test(NAME uv_test
           COMMAND uv_run_tests
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  if(CMAKE_SYSTEM_NAME STREQUAL "OS390")
    set_tests_properties(uv_test PROPERTIES ENVIRONMENT
                         "LIBPATH=${CMAKE_BINARY_DIR}:$ENV{LIBPATH}")
  endif()
  if(WIN32)
    add_custom_command(TARGET uv_run_tests POST_BUILD
                       COMMAND "${CMAKE_COMMAND}" -E copy
                       "$<TARGET_FILE:uv_run_tests>"
                       "$<TARGET_FILE_DIR:uv_run_tests>/uv_run_tests_no_ext")
  endif()
  add_executable(uv_run_tests_a ${uv_test_sources} uv_win_longpath.manifest)
  target_compile_definitions(uv_run_tests_a PRIVATE ${uv_defines})
  target_compile_options(uv_run_tests_a PRIVATE ${uv_cflags})
  if(QEMU)
    target_link_libraries(uv_run_tests_a uv_a ${uv_test_libraries} -static)
  else()
    target_link_libraries(uv_run_tests_a uv_a ${uv_test_libraries})
  endif()
  add_test(NAME uv_test_a
           COMMAND uv_run_tests_a
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  if(CMAKE_SYSTEM_NAME STREQUAL "OS390")
    set_target_properties(uv_run_benchmarks_a PROPERTIES LINKER_LANGUAGE CXX)
    set_target_properties(uv_run_tests PROPERTIES LINKER_LANGUAGE CXX)
    set_target_properties(uv_run_tests_a PROPERTIES LINKER_LANGUAGE CXX)
  endif()
  if(WIN32)
    add_custom_command(TARGET uv_run_tests_a POST_BUILD
                       COMMAND "${CMAKE_COMMAND}" -E copy
                       "$<TARGET_FILE:uv_run_tests_a>"
                       "$<TARGET_FILE_DIR:uv_run_tests_a>/uv_run_tests_a_no_ext")
  endif()
endif()

# Now for some gibbering horrors from beyond the stars...
foreach(lib IN LISTS uv_libraries)
  list(APPEND LIBS "-l${lib}")
endforeach()
string(REPLACE ";" " " LIBS "${LIBS}")
# Consider setting project version via project() call?
file(STRINGS configure.ac configure_ac REGEX ^AC_INIT)
string(REGEX MATCH "([0-9]+)[.][0-9]+[.][0-9]+" PACKAGE_VERSION "${configure_ac}")
set(UV_VERSION_MAJOR "${CMAKE_MATCH_1}")

set(includedir ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR})
set(libdir ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR})
set(prefix ${CMAKE_INSTALL_PREFIX})
configure_file(libuv-static.pc.in libuv-static.pc @ONLY)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES LICENSE-extra DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES ${PROJECT_BINARY_DIR}/libuv-static.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
install(TARGETS uv_a EXPORT libuvConfig
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(EXPORT libuvConfig
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libuv
	NAMESPACE libuv::)

if(LIBUV_BUILD_SHARED)
  # The version in the filename is mirroring the behaviour of autotools.
  set_target_properties(uv PROPERTIES
          VERSION ${UV_VERSION_MAJOR}.0.0
          SOVERSION ${UV_VERSION_MAJOR})
  configure_file(libuv.pc.in libuv.pc @ONLY)
  install(FILES ${PROJECT_BINARY_DIR}/libuv.pc
          DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
  install(TARGETS uv EXPORT libuvConfig
          RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

if(MSVC)
  set(CMAKE_DEBUG_POSTFIX d)
  get_filename_component(CMAKE_C_COMPILER_DIR ${CMAKE_C_COMPILER} DIRECTORY)
  if(ASAN)
    file(INSTALL "${CMAKE_C_COMPILER_DIR}/llvm-symbolizer.exe" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
    file(INSTALL "${CMAKE_C_COMPILER_DIR}/clang_rt.asan_dynamic-x86_64.dll" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
    file(INSTALL "${CMAKE_C_COMPILER_DIR}/clang_rt.asan_dbg_dynamic-x86_64.dll" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
  endif()
endif()

message(STATUS "summary of build options:
    Install prefix:  ${CMAKE_INSTALL_PREFIX}
    Target system:   ${CMAKE_SYSTEM_NAME}
    Compiler:
      C compiler:    ${CMAKE_C_COMPILER} (${CMAKE_C_COMPILER_ID})
      CFLAGS:        ${CMAKE_C_FLAGS_${_build_type}} ${CMAKE_C_FLAGS}
")
