# Integrates googletest at configure time.  Based on the instructions at
# https://github.com/google/googletest/tree/master/googletest#incorporating-into-an-existing-cmake-project

# Set up the external googletest project, downloading the latest from Github
# master if requested.
configure_file(
  ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt.in
  ${CMAKE_BINARY_DIR}/googletest-external/CMakeLists.txt
)

set(ABSL_SAVE_CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
set(ABSL_SAVE_CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
if (BUILD_SHARED_LIBS)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DGTEST_CREATE_SHARED_LIBRARY=1")
endif()

# Configure and build the googletest source.
execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
  RESULT_VARIABLE result
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/googletest-external )
if(result)
  message(FATAL_ERROR "CMake step for googletest failed: ${result}")
endif()

execute_process(COMMAND ${CMAKE_COMMAND} --build .
  RESULT_VARIABLE result
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/googletest-external)
if(result)
  message(FATAL_ERROR "Build step for googletest failed: ${result}")
endif()

set(CMAKE_CXX_FLAGS ${ABSL_SAVE_CMAKE_CXX_FLAGS})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${ABSL_SAVE_CMAKE_RUNTIME_OUTPUT_DIRECTORY})

# Prevent overriding the parent project's compiler/linker settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Add googletest directly to our build. This defines the gtest and gtest_main
# targets.
add_subdirectory(${absl_gtest_src_dir} ${absl_gtest_build_dir} EXCLUDE_FROM_ALL)
