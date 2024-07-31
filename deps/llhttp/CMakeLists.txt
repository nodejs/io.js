cmake_minimum_required(VERSION 3.5.1)
cmake_policy(SET CMP0069 NEW)

project(llhttp VERSION 9.2.1)
include(GNUInstallDirs)

set(CMAKE_C_STANDARD 99)

# By default build in relwithdebinfo type, supports both lowercase and uppercase
if(NOT CMAKE_CONFIGURATION_TYPES)
  set(allowableBuildTypes DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "${allowableBuildTypes}")
  if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RELWITHDEBINFO CACHE STRING "" FORCE)
  else()
    string(TOUPPER ${CMAKE_BUILD_TYPE} CMAKE_BUILD_TYPE)
    if(NOT CMAKE_BUILD_TYPE IN_LIST allowableBuildTypes)
      message(FATAL_ERROR "Invalid build type: ${CMAKE_BUILD_TYPE}")
    endif()
  endif()
endif()

#
# Options
#
# Generic option
option(BUILD_SHARED_LIBS "Build shared libraries (.dll/.so)" ON)
option(BUILD_STATIC_LIBS "Build static libraries (.lib/.a)" OFF)

# Source code
set(LLHTTP_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/llhttp.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/http.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/api.c
)

set(LLHTTP_HEADERS
  ${CMAKE_CURRENT_SOURCE_DIR}/include/llhttp.h
)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/libllhttp.pc.in
  ${CMAKE_CURRENT_SOURCE_DIR}/libllhttp.pc
  @ONLY
)

function(config_library target)
  target_sources(${target} PRIVATE ${LLHTTP_SOURCES} ${LLHTTP_HEADERS})

  target_include_directories(${target} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  )

  set_target_properties(${target} PROPERTIES
    OUTPUT_NAME llhttp
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    PUBLIC_HEADER ${LLHTTP_HEADERS}
  )

  install(TARGETS ${target}
    EXPORT llhttp
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

  install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/libllhttp.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
  )

  # This is required to work with FetchContent
  install(EXPORT llhttp
    FILE llhttp-config.cmake
    NAMESPACE llhttp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/llhttp
  )
endfunction(config_library target)

if(BUILD_SHARED_LIBS)
  add_library(llhttp_shared SHARED
      ${llhttp_src}
  )
  add_library(llhttp::llhttp ALIAS llhttp_shared)
  config_library(llhttp_shared)
endif()

if(BUILD_STATIC_LIBS)
  add_library(llhttp_static STATIC
      ${llhttp_src}
  )
  if(BUILD_SHARED_LIBS)
    add_library(llhttp::llhttp ALIAS llhttp_shared)
  else()
    add_library(llhttp::llhttp ALIAS llhttp_static)
  endif()
  config_library(llhttp_static)
endif()

# On windows with Visual Studio, add a debug postfix so that release
# and debug libraries can coexist.
if(MSVC)
  set(CMAKE_DEBUG_POSTFIX "d")
endif()

# Print project configure summary
message(STATUS "")
message(STATUS "")
message(STATUS "Project configure summary:")
message(STATUS "")
message(STATUS "  CMake build type .................: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Install prefix ...................: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Build shared library .............: ${BUILD_SHARED_LIBS}")
message(STATUS "  Build static library .............: ${BUILD_STATIC_LIBS}")
message(STATUS "")
