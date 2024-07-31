// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "testing/gmock/include/gmock/gmock.h"

int main(int argc, char** argv) {
  // Don't catch SEH exceptions and continue as the following tests might hang
  // in an broken environment on windows.
  GTEST_FLAG_SET(catch_exceptions, false);

  // Most unit-tests are multi-threaded, so enable thread-safe death-tests.
  GTEST_FLAG_SET(death_test_style, "threadsafe");

  testing::InitGoogleMock(&argc, argv);
  return RUN_ALL_TESTS();
}
