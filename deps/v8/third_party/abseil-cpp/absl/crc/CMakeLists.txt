# Copyright 2022 The Abseil Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Internal-only target, do not depend on directly.
absl_cc_library(
  NAME
    crc_cpu_detect
  HDRS
    "internal/cpu_detect.h"
  SRCS
    "internal/cpu_detect.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::base
    absl::config
)

# Internal-only target, do not depend on directly.
absl_cc_library(
  NAME
    crc_internal
  HDRS
    "internal/crc.h"
    "internal/crc32_x86_arm_combined_simd.h"
  SRCS
    "internal/crc.cc"
    "internal/crc_internal.h"
    "internal/crc_x86_arm_combined.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::crc_cpu_detect
    absl::config
    absl::core_headers
    absl::endian
    absl::prefetch
    absl::raw_logging_internal
    absl::memory
    absl::bits
)

absl_cc_library(
  NAME
    crc32c
  HDRS
    "crc32c.h"
    "internal/crc32c.h"
    "internal/crc_memcpy.h"
  SRCS
    "crc32c.cc"
    "internal/crc32c_inline.h"
    "internal/crc_memcpy_fallback.cc"
    "internal/crc_memcpy_x86_arm_combined.cc"
    "internal/crc_non_temporal_memcpy.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::crc_cpu_detect
    absl::crc_internal
    absl::non_temporal_memcpy
    absl::config
    absl::core_headers
    absl::endian
    absl::prefetch
    absl::str_format
    absl::strings
)

absl_cc_test(
  NAME
    crc32c_test
  SRCS
    "crc32c_test.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::crc32c
    absl::strings
    absl::str_format
    GTest::gtest_main
)

# Internal-only target, do not depend on directly.
absl_cc_library(
  NAME
    non_temporal_arm_intrinsics
  HDRS
    "internal/non_temporal_arm_intrinsics.h"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::config
)

# Internal-only target, do not depend on directly.
absl_cc_library(
  NAME
    non_temporal_memcpy
  HDRS
    "internal/non_temporal_memcpy.h"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::non_temporal_arm_intrinsics
    absl::config
    absl::core_headers
)

absl_cc_test(
  NAME
    crc_memcpy_test
  SRCS
    "internal/crc_memcpy_test.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::crc32c
    absl::memory
    absl::random_random
    absl::random_distributions
    absl::strings
    GTest::gtest_main
)

absl_cc_test(
  NAME
    non_temporal_memcpy_test
  SRCS
    "internal/non_temporal_memcpy_test.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::non_temporal_memcpy
    GTest::gtest_main
)

absl_cc_library(
  NAME
    crc_cord_state
  HDRS
    "internal/crc_cord_state.h"
  SRCS
    "internal/crc_cord_state.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::crc32c
    absl::config
    absl::strings
    absl::no_destructor
)

absl_cc_test(
  NAME
    crc_cord_state_test
  SRCS
    "internal/crc_cord_state_test.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::crc_cord_state
    absl::crc32c
    GTest::gtest_main
)
