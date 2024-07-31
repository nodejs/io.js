include(GoogleTest)
include(CTest)
add_executable(basic basic.cpp)
target_link_libraries(
  basic
  GTest::gtest_main
)
target_link_libraries(basic nbytes)
add_test(basic_test basic)
gtest_discover_tests(basic)
