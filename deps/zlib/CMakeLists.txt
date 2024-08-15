cmake_minimum_required(VERSION 2.4.4...3.15.0)
set(CMAKE_ALLOW_LOOSE_LOOP_CONSTRUCTS ON)

project(zlib C)

set(VERSION "1.3.0.1")

set(INSTALL_BIN_DIR "${CMAKE_INSTALL_PREFIX}/bin" CACHE PATH "Installation directory for executables")
set(INSTALL_LIB_DIR "${CMAKE_INSTALL_PREFIX}/lib" CACHE PATH "Installation directory for libraries")
set(INSTALL_INC_DIR "${CMAKE_INSTALL_PREFIX}/include" CACHE PATH "Installation directory for headers")
set(INSTALL_MAN_DIR "${CMAKE_INSTALL_PREFIX}/share/man" CACHE PATH "Installation directory for manual pages")
set(INSTALL_PKGCONFIG_DIR "${CMAKE_INSTALL_PREFIX}/share/pkgconfig" CACHE PATH "Installation directory for pkgconfig (.pc) files")

include(CheckTypeSize)
include(CheckFunctionExists)
include(CheckIncludeFile)
include(CheckCSourceCompiles)
enable_testing()

check_include_file(sys/types.h HAVE_SYS_TYPES_H)
check_include_file(stdint.h    HAVE_STDINT_H)
check_include_file(stddef.h    HAVE_STDDEF_H)

option(ENABLE_SIMD_OPTIMIZATIONS "Enable all SIMD optimizations" OFF)
option(ENABLE_SIMD_AVX512 "Enable SIMD AXV512 optimizations" OFF)
option(USE_ZLIB_RABIN_KARP_HASH "Enable bitstream compatibility with canonical zlib" OFF)
option(ENABLE_INTEL_QAT_COMPRESSION "Enable Intel Quick Assist Technology use for compression" OFF)
option(BUILD_UNITTESTS "Enable standalone unit tests build" OFF)
option(BUILD_MINIZIP_BIN "Enable building minzip_bin tool" OFF)
option(BUILD_ZPIPE "Enable building zpipe tool" OFF)
option(BUILD_MINIGZIP "Enable building minigzip tool" OFF)

if (USE_ZLIB_RABIN_KARP_HASH)
   add_definitions(-DUSE_ZLIB_RABIN_KARP_ROLLING_HASH)
endif()

# TODO(cavalcantii): add support for other OSes (e.g. Android, Fuchsia, etc)
# and architectures (e.g. RISCV).
if (ENABLE_SIMD_OPTIMIZATIONS)
  # Apparently some environments (e.g. CentOS) require to explicitly link
  # with pthread and that is required by the CPU features detection code.
  find_package (Threads REQUIRED)
  SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

  if (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    add_definitions(-DINFLATE_CHUNK_SIMD_SSE2)
    add_definitions(-DADLER32_SIMD_SSSE3)
    add_definitions(-DINFLATE_CHUNK_READ_64LE)
    add_definitions(-DCRC32_SIMD_SSE42_PCLMUL)
    if (ENABLE_SIMD_AVX512)
      add_definitions(-DCRC32_SIMD_AVX512_PCLMUL)
      add_compile_options(-mvpclmulqdq -msse2 -mavx512f -mpclmul)
    else()
      add_compile_options(-msse4.2 -mpclmul)
    endif()
    add_definitions(-DDEFLATE_SLIDE_HASH_SSE2)
    # Required by CPU features detection code.
    add_definitions(-DX86_NOT_WINDOWS)
  endif()

  if ((CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64") OR
      (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64"))
    add_definitions(-DINFLATE_CHUNK_SIMD_NEON)
    add_definitions(-DADLER32_SIMD_NEON)
    add_definitions(-DINFLATE_CHUNK_READ_64LE)
    add_definitions(-DCRC32_ARMV8_CRC32)
    add_definitions(-DDEFLATE_SLIDE_HASH_NEON)
    # Required by CPU features detection code.
    if (APPLE)
      add_definitions(-DARMV8_OS_MACOS)
    endif()

    if (UNIX AND NOT APPLE)
      add_definitions(-DARMV8_OS_LINUX)
    endif()

    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv8-a+crc+crypto")
  endif()

  if (CMAKE_SYSTEM_PROCESSOR STREQUAL "riscv64")
    add_definitions(-DRISCV_RVV)
    add_definitions(-DDEFLATE_SLIDE_HASH_RVV)
    add_definitions(-DADLER32_SIMD_RVV)

    # TODO(cavalcantii): add remaining flags as we port optimizations to RVV.
    # chunk_copy is required for READ64 and unconditional decode of literals.
    add_definitions(-DINFLATE_CHUNK_GENERIC)
    add_definitions(-DINFLATE_CHUNK_READ_64LE)

    # Tested with clang-17, unaligned loads are required by read64 & chunk_copy.
    # TODO(cavalcantii): replace internal clang flags for -munaligned-access
    # when we have a newer compiler available.
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --target=riscv64-unknown-linux-gnu -march=rv64gcv -Xclang -target-feature -Xclang +unaligned-scalar-mem")
  endif()

endif()

#
# Check to see if we have large file support
#
set(CMAKE_REQUIRED_DEFINITIONS -D_LARGEFILE64_SOURCE=1)
# We add these other definitions here because CheckTypeSize.cmake
# in CMake 2.4.x does not automatically do so and we want
# compatibility with CMake 2.4.x.
if(HAVE_SYS_TYPES_H)
    list(APPEND CMAKE_REQUIRED_DEFINITIONS -DHAVE_SYS_TYPES_H)
endif()
if(HAVE_STDINT_H)
    list(APPEND CMAKE_REQUIRED_DEFINITIONS -DHAVE_STDINT_H)
endif()
if(HAVE_STDDEF_H)
    list(APPEND CMAKE_REQUIRED_DEFINITIONS -DHAVE_STDDEF_H)
endif()
check_type_size(off64_t OFF64_T)
if(HAVE_OFF64_T)
   add_definitions(-D_LARGEFILE64_SOURCE=1)
endif()
set(CMAKE_REQUIRED_DEFINITIONS) # clear variable

#
# Check for fseeko
#
check_function_exists(fseeko HAVE_FSEEKO)
if(NOT HAVE_FSEEKO)
    add_definitions(-DNO_FSEEKO)
endif()

#
# Check for unistd.h
#
check_include_file(unistd.h Z_HAVE_UNISTD_H)

if(MSVC)
    set(CMAKE_DEBUG_POSTFIX "d")
    add_definitions(-D_CRT_SECURE_NO_DEPRECATE)
    add_definitions(-D_CRT_NONSTDC_NO_DEPRECATE)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR})
endif()

if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_CURRENT_BINARY_DIR)
    # If we're doing an out of source build and the user has a zconf.h
    # in their source tree...
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/zconf.h)
        message(STATUS "Renaming")
        message(STATUS "    ${CMAKE_CURRENT_SOURCE_DIR}/zconf.h")
        message(STATUS "to 'zconf.h.included' because this file is included with zlib")
        message(STATUS "but CMake generates it automatically in the build directory.")
        file(RENAME ${CMAKE_CURRENT_SOURCE_DIR}/zconf.h ${CMAKE_CURRENT_SOURCE_DIR}/zconf.h.included)
  endif()
endif()

set(ZLIB_PC ${CMAKE_CURRENT_BINARY_DIR}/zlib.pc)
configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/zlib.pc.cmakein
		${ZLIB_PC} @ONLY)
configure_file(	${CMAKE_CURRENT_SOURCE_DIR}/zconf.h.cmakein
		${CMAKE_CURRENT_BINARY_DIR}/zconf.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_SOURCE_DIR})


#============================================================================
# zlib
#============================================================================

set(ZLIB_PUBLIC_HDRS
    ${CMAKE_CURRENT_BINARY_DIR}/zconf.h
    zlib.h
)
set(ZLIB_PRIVATE_HDRS
    crc32.h
    deflate.h
    gzguts.h
    inffast.h
    inffixed.h
    inflate.h
    inftrees.h
    trees.h
    zutil.h
)
set(ZLIB_SRCS
    adler32.c
    compress.c
    crc32.c
    deflate.c
    gzclose.c
    gzlib.c
    gzread.c
    gzwrite.c
    inflate.c
    infback.c
    inftrees.c
    inffast.c
    trees.c
    uncompr.c
    zutil.c
)


#============================================================================
# Update list of source files if optimizations were enabled
#============================================================================
if (ENABLE_SIMD_OPTIMIZATIONS)
  if (CMAKE_SYSTEM_PROCESSOR STREQUAL "riscv64")
    message("RISCVV: Add optimizations.")
    list(REMOVE_ITEM ZLIB_SRCS inflate.c)
    list(APPEND ZLIB_PRIVATE_HDRS ${CMAKE_CURRENT_SOURCE_DIR}/adler32_simd.h)
    list(APPEND ZLIB_PRIVATE_HDRS ${CMAKE_CURRENT_SOURCE_DIR}/contrib/optimizations/chunkcopy.h)
    list(APPEND ZLIB_PRIVATE_HDRS ${CMAKE_CURRENT_SOURCE_DIR}/cpu_features.h)

    list(APPEND ZLIB_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/adler32_simd.c)
    list(APPEND ZLIB_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/contrib/optimizations/inffast_chunk.c)
    list(APPEND ZLIB_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/contrib/optimizations/inflate.c)
    list(APPEND ZLIB_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/cpu_features.c)
  else()
    list(REMOVE_ITEM ZLIB_SRCS inflate.c)

    list(APPEND ZLIB_PRIVATE_HDRS ${CMAKE_CURRENT_SOURCE_DIR}/adler32_simd.h)
    list(APPEND ZLIB_PRIVATE_HDRS ${CMAKE_CURRENT_SOURCE_DIR}/contrib/optimizations/chunkcopy.h)
    list(APPEND ZLIB_PRIVATE_HDRS ${CMAKE_CURRENT_SOURCE_DIR}/contrib/optimizations/inffast_chunk.h)
    list(APPEND ZLIB_PRIVATE_HDRS ${CMAKE_CURRENT_SOURCE_DIR}/cpu_features.h)
    list(APPEND ZLIB_PRIVATE_HDRS ${CMAKE_CURRENT_SOURCE_DIR}/crc32_simd.h)

    list(APPEND ZLIB_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/adler32_simd.c)
    list(APPEND ZLIB_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/contrib/optimizations/inffast_chunk.c)
    list(APPEND ZLIB_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/contrib/optimizations/inflate.c)
    list(APPEND ZLIB_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/cpu_features.c)
    list(APPEND ZLIB_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/crc32_simd.c)
    list(APPEND ZLIB_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/crc_folding.c)
  endif()
endif()

if (ENABLE_INTEL_QAT_COMPRESSION)
    list(APPEND ZLIB_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/contrib/qat/deflate_qat.cpp)
    list(APPEND ZLIB_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/contrib/qat/qatzpp/io_buffers.cpp)
    list(APPEND ZLIB_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/contrib/qat/qatzpp/memory.cpp)
    list(APPEND ZLIB_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/contrib/qat/qatzpp/qat_buffer_list.cpp)
    list(APPEND ZLIB_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/contrib/qat/qatzpp/qat.cpp)
    list(APPEND ZLIB_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/contrib/qat/qatzpp/qat_instance.cpp)
    list(APPEND ZLIB_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/contrib/qat/qatzpp/session.cpp)
    list(APPEND ZLIB_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/contrib/qat/qatzpp/qat_task.cpp)

    # TODO(gustavoa): Find a way to include the qatzpp headers without having the
    # presubmit check throw errors.
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/contrib/qat/qatzpp)
    add_compile_definitions(QAT_COMPRESSION_ENABLED)
endif()

# parse the full version number from zlib.h and include in ZLIB_FULL_VERSION
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/zlib.h _zlib_h_contents)
string(REGEX REPLACE ".*#define[ \t]+ZLIB_VERSION[ \t]+\"([-0-9A-Za-z.]+)\".*"
    "\\1" ZLIB_FULL_VERSION ${_zlib_h_contents})

if(MINGW)
    # This gets us DLL resource information when compiling on MinGW.
    if(NOT CMAKE_RC_COMPILER)
        set(CMAKE_RC_COMPILER windres.exe)
    endif()

    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/zlib1rc.obj
                       COMMAND ${CMAKE_RC_COMPILER}
                            -D GCC_WINDRES
                            -I ${CMAKE_CURRENT_SOURCE_DIR}
                            -I ${CMAKE_CURRENT_BINARY_DIR}
                            -o ${CMAKE_CURRENT_BINARY_DIR}/zlib1rc.obj
                            -i ${CMAKE_CURRENT_SOURCE_DIR}/win32/zlib1.rc)
    set(ZLIB_DLL_SRCS ${CMAKE_CURRENT_BINARY_DIR}/zlib1rc.obj)
endif(MINGW)

add_library(zlib SHARED ${ZLIB_SRCS} ${ZLIB_DLL_SRCS} ${ZLIB_PUBLIC_HDRS} ${ZLIB_PRIVATE_HDRS})
add_library(zlibstatic STATIC ${ZLIB_SRCS} ${ZLIB_PUBLIC_HDRS} ${ZLIB_PRIVATE_HDRS})
set_target_properties(zlib PROPERTIES DEFINE_SYMBOL ZLIB_DLL)
set_target_properties(zlib PROPERTIES SOVERSION 1)

if (ENABLE_INTEL_QAT_COMPRESSION)
    target_include_directories(zlib PUBLIC ${QATZPP_INCLUDE_DIRS})
    target_link_libraries(zlib ${QATZPP_LIBRARY})
    target_link_libraries(zlib qat)
    target_include_directories(zlibstatic PUBLIC ${QATZPP_INCLUDE_DIRS})
    target_link_libraries(zlibstatic ${QATZPP_LIBRARY})
    target_link_libraries(zlibstatic qat)
endif()

if(NOT CYGWIN)
    # This property causes shared libraries on Linux to have the full version
    # encoded into their final filename.  We disable this on Cygwin because
    # it causes cygz-${ZLIB_FULL_VERSION}.dll to be created when cygz.dll
    # seems to be the default.
    #
    # This has no effect with MSVC, on that platform the version info for
    # the DLL comes from the resource file win32/zlib1.rc
    set_target_properties(zlib PROPERTIES VERSION ${ZLIB_FULL_VERSION})
endif()

if(UNIX)
    # On unix-like platforms the library is almost always called libz
   set_target_properties(zlib zlibstatic PROPERTIES OUTPUT_NAME z)
   if(NOT APPLE)
     set_target_properties(zlib PROPERTIES LINK_FLAGS "-Wl,--version-script,\"${CMAKE_CURRENT_SOURCE_DIR}/zlib.map\"")
   endif()
elseif(BUILD_SHARED_LIBS AND WIN32)
    # Creates zlib1.dll when building shared library version
    set_target_properties(zlib PROPERTIES SUFFIX "1.dll")
endif()

if(NOT SKIP_INSTALL_LIBRARIES AND NOT SKIP_INSTALL_ALL )
    install(TARGETS zlib zlibstatic
        RUNTIME DESTINATION "${INSTALL_BIN_DIR}"
        ARCHIVE DESTINATION "${INSTALL_LIB_DIR}"
        LIBRARY DESTINATION "${INSTALL_LIB_DIR}" )
endif()
if(NOT SKIP_INSTALL_HEADERS AND NOT SKIP_INSTALL_ALL )
    install(FILES ${ZLIB_PUBLIC_HDRS} DESTINATION "${INSTALL_INC_DIR}")
endif()
if(NOT SKIP_INSTALL_FILES AND NOT SKIP_INSTALL_ALL )
    install(FILES zlib.3 DESTINATION "${INSTALL_MAN_DIR}/man3")
endif()
if(NOT SKIP_INSTALL_FILES AND NOT SKIP_INSTALL_ALL )
    install(FILES ${ZLIB_PC} DESTINATION "${INSTALL_PKGCONFIG_DIR}")
endif()

#============================================================================
# Benchmarker
#============================================================================
enable_language(CXX)
set(CMAKE_CXX_STANDARD 14) # workaround for older compilers (e.g. g++ 5.4).
add_executable(zlib_bench contrib/bench/zlib_bench.cc)
target_link_libraries(zlib_bench zlib)

#============================================================================
# Unit Tests
#============================================================================
if (BUILD_UNITTESTS)
    include (ExternalProject)
    set_directory_properties(PROPERTIES EP_PREFIX ${CMAKE_BINARY_DIR}/third_party)
    ExternalProject_add(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG d1467f5813f4d363cfd11aba99c4e9fe47a85e99
        UPDATE_COMMAND ""
        INSTALL_COMMAND ""
        LOG_DOWNLOAD ON
        LOG_CONFIGURE ON
        LOG_BUILD ON
    )

    # gtest includedir
    ExternalProject_Get_Property(googletest source_dir)
    set(GTEST_INCLUDE_DIRS
        ${source_dir}/googletest/include
        ${source_dir}/googletest/include/gtest
    )

    # gtest library
    ExternalProject_Get_Property(googletest binary_dir)
    set(GTEST_LIBRARY_PATH ${binary_dir}/lib/${CMAKE_FIND_LIBRARY_PREFIXES}gtest.a)
    set(GTEST_LIBRARY gtest)
    add_library(${GTEST_LIBRARY} UNKNOWN IMPORTED)
    set_property(TARGET ${GTEST_LIBRARY} PROPERTY IMPORTED_LOCATION ${GTEST_LIBRARY_PATH})
    add_dependencies(${GTEST_LIBRARY} googletest)

    set(UTEST_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/contrib/tests/infcover.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/contrib/tests/infcover.h
        ${CMAKE_CURRENT_SOURCE_DIR}/contrib/tests/utils_unittest.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/contrib/tests/standalone_test_runner.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/google/compression_utils_portable.cc
    )

    add_compile_definitions(CMAKE_STANDALONE_UNITTESTS)

    add_executable(zlib_unittests ${UTEST_SRC})
    target_include_directories(zlib_unittests PUBLIC ${GTEST_INCLUDE_DIRS})
    target_include_directories(zlib_unittests PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/google)

    target_link_libraries(zlib_unittests ${GTEST_LIBRARY})
    target_link_libraries(zlib_unittests zlib)
    # Needed by gtest
    target_link_libraries(zlib_unittests pthread)
endif()

#============================================================================
# Minizip tool
#============================================================================
# TODO(cavalcantii): get it working on Windows.
if (BUILD_MINIZIP_BIN)
  add_executable(minizip_bin contrib/minizip/minizip.c contrib/minizip/ioapi.c
    contrib/minizip/ioapi.h contrib/minizip/unzip.c
    contrib/minizip/unzip.h contrib/minizip/zip.c contrib/minizip/zip.h
    )
  target_link_libraries(minizip_bin zlib)
endif()

#============================================================================
# zpipe tool
#============================================================================
if (BUILD_ZPIPE)
  add_executable(zpipe examples/zpipe.c)
  target_link_libraries(zpipe zlib)
endif()
#============================================================================
# MiniGzip tool
#============================================================================
if (BUILD_MINIGZIP)
  add_executable(minigzip_bin test/minigzip.c)
  target_link_libraries(minigzip_bin zlib)
endif()
