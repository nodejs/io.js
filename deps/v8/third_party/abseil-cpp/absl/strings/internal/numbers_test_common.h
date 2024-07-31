// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// This file contains common things needed by numbers_test.cc,
// numbers_legacy_test.cc and numbers_benchmark.cc.

#ifndef ABSL_STRINGS_INTERNAL_NUMBERS_TEST_COMMON_H_
#define ABSL_STRINGS_INTERNAL_NUMBERS_TEST_COMMON_H_

#include <array>
#include <cstdint>
#include <limits>
#include <string>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace strings_internal {

template <typename IntType>
inline bool Itoa(IntType value, int base, std::string* destination) {
  destination->clear();
  if (base <= 1 || base > 36) {
    return false;
  }

  if (value == 0) {
    destination->push_back('0');
    return true;
  }

  bool negative = value < 0;
  while (value != 0) {
    const IntType next_value = value / base;
    // Can't use std::abs here because of problems when IntType is unsigned.
    int remainder =
        static_cast<int>(value > next_value * base ? value - next_value * base
                                                   : next_value * base - value);
    char c = remainder < 10 ? '0' + remainder : 'A' + remainder - 10;
    destination->insert(0, 1, c);
    value = next_value;
  }

  if (negative) {
    destination->insert(0, 1, '-');
  }
  return true;
}

struct uint32_test_case {
  const char* str;
  bool expect_ok;
  int base;  // base to pass to the conversion function
  uint32_t expected;
};

inline const std::array<uint32_test_case, 27>& strtouint32_test_cases() {
  static const std::array<uint32_test_case, 27> test_cases{{
      {"0xffffffff", true, 16, (std::numeric_limits<uint32_t>::max)()},
      {"0x34234324", true, 16, 0x34234324},
      {"34234324", true, 16, 0x34234324},
      {"0", true, 16, 0},
      {" \t\n 0xffffffff", true, 16, (std::numeric_limits<uint32_t>::max)()},
      {" \f\v 46", true, 10, 46},  // must accept weird whitespace
      {" \t\n 72717222", true, 8, 072717222},
      {" \t\n 072717222", true, 8, 072717222},
      {" \t\n 072717228", false, 8, 07271722},
      {"0", true, 0, 0},

      // Base-10 version.
      {"34234324", true, 0, 34234324},
      {"4294967295", true, 0, (std::numeric_limits<uint32_t>::max)()},
      {"34234324 \n\t", true, 10, 34234324},

      // Unusual base
      {"0", true, 3, 0},
      {"2", true, 3, 2},
      {"11", true, 3, 4},

      // Invalid uints.
      {"", false, 0, 0},
      {"  ", false, 0, 0},
      {"abc", false, 0, 0},  // would be valid hex, but prefix is missing
      {"34234324a", false, 0, 34234324},
      {"34234.3", false, 0, 34234},
      {"-1", false, 0, 0},
      {"   -123", false, 0, 0},
      {" \t\n -123", false, 0, 0},

      // Out of bounds.
      {"4294967296", false, 0, (std::numeric_limits<uint32_t>::max)()},
      {"0x100000000", false, 0, (std::numeric_limits<uint32_t>::max)()},
      {nullptr, false, 0, 0},
  }};
  return test_cases;
}

struct uint64_test_case {
  const char* str;
  bool expect_ok;
  int base;
  uint64_t expected;
};

inline const std::array<uint64_test_case, 34>& strtouint64_test_cases() {
  static const std::array<uint64_test_case, 34> test_cases{{
      {"0x3423432448783446", true, 16, int64_t{0x3423432448783446}},
      {"3423432448783446", true, 16, int64_t{0x3423432448783446}},

      {"0", true, 16, 0},
      {"000", true, 0, 0},
      {"0", true, 0, 0},
      {" \t\n 0xffffffffffffffff", true, 16,
       (std::numeric_limits<uint64_t>::max)()},

      {"012345670123456701234", true, 8, int64_t{012345670123456701234}},
      {"12345670123456701234", true, 8, int64_t{012345670123456701234}},

      {"12845670123456701234", false, 8, 0},

      // Base-10 version.
      {"34234324487834466", true, 0, int64_t{34234324487834466}},

      {" \t\n 18446744073709551615", true, 0,
       (std::numeric_limits<uint64_t>::max)()},

      {"34234324487834466 \n\t ", true, 0, int64_t{34234324487834466}},

      {" \f\v 46", true, 10, 46},  // must accept weird whitespace

      // Unusual base
      {"0", true, 3, 0},
      {"2", true, 3, 2},
      {"11", true, 3, 4},

      {"0", true, 0, 0},

      // Invalid uints.
      {"", false, 0, 0},
      {"  ", false, 0, 0},
      {"abc", false, 0, 0},
      {"34234324487834466a", false, 0, 0},
      {"34234487834466.3", false, 0, 0},
      {"-1", false, 0, 0},
      {"   -123", false, 0, 0},
      {" \t\n -123", false, 0, 0},

      // Out of bounds.
      {"18446744073709551616", false, 10, 0},
      {"18446744073709551616", false, 0, 0},
      {"0x10000000000000000", false, 16,
       (std::numeric_limits<uint64_t>::max)()},
      {"0X10000000000000000", false, 16,
       (std::numeric_limits<uint64_t>::max)()},  // 0X versus 0x.
      {"0x10000000000000000", false, 0, (std::numeric_limits<uint64_t>::max)()},
      {"0X10000000000000000", false, 0,
       (std::numeric_limits<uint64_t>::max)()},  // 0X versus 0x.

      {"0x1234", true, 16, 0x1234},

      // Base-10 string version.
      {"1234", true, 0, 1234},
      {nullptr, false, 0, 0},
  }};
  return test_cases;
}

}  // namespace strings_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_NUMBERS_TEST_COMMON_H_
