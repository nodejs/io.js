// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
  Macros to define fuzz tests in V8 (https://github.com/google/fuzztest/).
  The macros are no-ops with unsupported configurations.

  // Example without test fixture:
  static void FuzzTest(...) {...}
  V8_FUZZ_TEST(FuzzSuite, FuzzTest).WithDomains(...);

  // Example with test fixture:
  class ExistingTestFixture {
    void MyParameterizedTestP(...);
  }

  V8_FUZZ_SUITE(NewFuzzTest, ExistingTestFixture);

  void ExistingTestFixture::MyParameterizedTestP(...) {
    // Old test behavior parameterized.
    ...
  }

  // Old test.
  TEST_F(ExistingTestFixture, OldTest) {
    TRACED_FOREACH(...) { MyParameterizedTestP(...); }
  }

  // New fuzz test.
  V8_FUZZ_TEST_F(NewFuzzTest, MyParameterizedTestP).WithDomains(...);
*/

#ifndef V8_UNITTESTS_FUZZTEST_H_
#define V8_UNITTESTS_FUZZTEST_H_

#ifdef V8_ENABLE_FUZZTEST
#include "test/unittests/fuzztest-adapter.h"

#define V8_FUZZ_SUITE(fuzz_fixture, test_fixture) \
  class fuzz_fixture                              \
      : public fuzztest::PerFuzzTestFixtureAdapter<test_fixture> {}

#define V8_FUZZ_TEST(cls, name) FUZZ_TEST(cls, name)

#define V8_FUZZ_TEST_F(cls, name) FUZZ_TEST_F(cls, name)

#else  // V8_ENABLE_FUZZTEST
#define V8_FUZZ_SUITE(fuzz_fixture, test_fixture) \
  class fuzz_fixture {}

struct _NoFuzz {
  _NoFuzz WithDomains() { return *this; }
};

#define WithDomains(...) WithDomains()

#define _NO_FUZZ(cls, name) \
  [[maybe_unused]] static _NoFuzz cls##_##name = _NoFuzz()

#define V8_FUZZ_TEST(cls, name) _NO_FUZZ(cls, name)
#define V8_FUZZ_TEST_F(cls, name) _NO_FUZZ(cls, name)

#endif  // V8_ENABLE_FUZZTEST
#endif  // V8_UNITTESTS_FUZZTEST_H_
