// Copyright 2024 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: Apache-2.0
// SPDX-License-Identifier: BSD-3-Clause

#include "hwy/abort.h"

#include <stdio.h>

#include <string>

#include "hwy/base.h"
#include "hwy/tests/hwy_gtest.h"
#include "hwy/tests/test_util-inl.h"  // HWY_ASSERT_EQ

namespace hwy {
namespace {

#ifdef GTEST_HAS_DEATH_TEST
std::string GetBaseName(std::string const& file_name) {
  auto last_slash = file_name.find_last_of("/\\");
  return file_name.substr(last_slash + 1);
}

TEST(AbortDeathTest, AbortDefault) {
  std::string expected = std::string("Abort at ") + GetBaseName(__FILE__) +
                         ":" + std::to_string(__LINE__ + 1) + ": Test Abort";
  ASSERT_DEATH(HWY_ABORT("Test %s", "Abort"), expected);
}

TEST(AbortDeathTest, AbortOverride) {
  const AbortFunc CustomAbortHandler = [](const char* file, int line,
                                          const char* formatted_err) -> void {
    fprintf(stderr, "%s from %02d of %s", formatted_err, line,
            GetBaseName(file).data());
  };

  SetAbortFunc(CustomAbortHandler);

  // googletest regex does not support `+` for digits on Windows?!
  // https://google.github.io/googletest/advanced.html#regular-expression-syntax
  // Hence we insert the expected line number manually.
  char buf[100];
  const std::string file = GetBaseName(__FILE__);
  const int line = __LINE__ + 2;  // from which HWY_ABORT is called
  snprintf(buf, sizeof(buf), "Test Abort from %02d of %s", line, file.c_str());
  ASSERT_DEATH({ HWY_ABORT("Test %s", "Abort"); }, buf);
}
#endif  // GTEST_HAS_DEATH_TEST

TEST(AbortTest, AbortOverrideChain) {
  AbortFunc FirstHandler = [](const char* file, int line,
                              const char* formatted_err) -> void {
    fprintf(stderr, "%s from %d of %s", formatted_err, line, file);
  };
  AbortFunc SecondHandler = [](const char* file, int line,
                               const char* formatted_err) -> void {
    fprintf(stderr, "%s from %d of %s", formatted_err, line, file);
  };

  // Do not check that the first SetAbortFunc returns nullptr, because it is
  // not guaranteed to be the first call - other TEST may come first.
  (void)SetAbortFunc(FirstHandler);
  HWY_ASSERT(GetAbortFunc() == FirstHandler);
  HWY_ASSERT(SetAbortFunc(SecondHandler) == FirstHandler);
  HWY_ASSERT(GetAbortFunc() == SecondHandler);
  HWY_ASSERT(SetAbortFunc(nullptr) == SecondHandler);
  HWY_ASSERT(GetAbortFunc() == nullptr);
}

}  // namespace
}  // namespace hwy

HWY_TEST_MAIN();
