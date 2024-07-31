// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Demo fuzz tests used to probe-test the FuzzTest and Centipede integration
// with Clusterfuzz.

#include "test/unittests/fuzztest.h"

#include <iostream>
#include <string>
#include <vector>

#include "src/base/logging.h"

namespace v8::internal {

#ifdef V8_ENABLE_FUZZTEST

static void ManyConditions(std::vector<int> input, int failure) {
  int i = 0;

  if (input.size() > 4) {
    if (input[0] >= 1500 && input[0] <= 2000) {
      i++;
    }
    if (input[1] >= 250 && input[1] <= 500) {
      i++;
    }
    if (input[2] >= 200 && input[2] <= 250) {
      i++;
    }
    if (input[3] >= 150 && input[3] <= 200) {
      i++;
    }
    if (input[4] >= 3 && input[4] <= 20) {
      i++;
    }
  }

  if (i >= 4) {
    switch (failure) {
      case 0:
        ASSERT_LT(failure, 0);
        break;
      case 1:
        CHECK_WITH_MSG(false, "Fake fuzz-test check failure");
        break;
      case 2:
        DCHECK_WITH_MSG(false, "Fake fuzz-test dcheck failure");
        break;
      case 3:
        // Fake Heap-buffer-overflow.
        int* adr = new int(3);
        std::cout << *(adr + 3);
        break;
    }
  }
}

V8_FUZZ_TEST(SmokeTest, ManyConditions)
    .WithDomains(fuzztest::VectorOf(fuzztest::InRange(1, 2000)),
                 fuzztest::InRange(0, 3));

static void SingleString(std::string input) { ASSERT_NE(input, "V8"); }

V8_FUZZ_TEST(SmokeTest, SingleString);

#endif  // V8_ENABLE_FUZZTEST
}  // namespace v8::internal
