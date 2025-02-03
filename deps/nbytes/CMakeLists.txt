cmake_minimum_required(VERSION 3.28)
project(nbytes)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
if (NOT CMAKE_BUILD_TYPE)
  message(STATUS "No build type selected, default to Release")
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

option(NBYTES_DEVELOPMENT_CHECKS "Enable development checks" OFF)

include(GNUInstallDirs)
include(FetchContent)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_subdirectory(src)
enable_testing()
add_subdirectory(tests)

install(
  FILES include/nbytes.h
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  COMPONENT nbytes_development
)

install(
  TARGETS nbytes
  EXPORT nbytes_targets
  RUNTIME COMPONENT nbytes_runtime
  LIBRARY COMPONENT nbytes_runtime
  NAMELINK_COMPONENT nbytes_development
  ARCHIVE COMPONENT nbytes_development
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
