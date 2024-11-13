#
# Copyright 2017 The Abseil Authors.
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
#

absl_cc_library(
  NAME
    bits
  HDRS
    "bits.h"
    "internal/bits.h"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::core_headers
  PUBLIC
)

absl_cc_test(
  NAME
    bits_test
  SRCS
    "bits_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::bits
    absl::core_headers
    absl::random_random
    GTest::gmock_main
)

absl_cc_library(
  NAME
    int128
  HDRS
    "int128.h"
  SRCS
    "int128.cc"
    "int128_have_intrinsic.inc"
    "int128_no_intrinsic.inc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::compare
    absl::config
    absl::core_headers
    absl::bits
  PUBLIC
)

absl_cc_test(
  NAME
    int128_test
  SRCS
    "int128_stream_test.cc"
    "int128_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::int128
    absl::base
    absl::compare
    absl::hash_testing
    absl::type_traits
    absl::strings
    GTest::gmock_main
)

# component target
absl_cc_library(
  NAME
    numeric
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::int128
  PUBLIC
)

absl_cc_library(
  NAME
    numeric_representation
  HDRS
    "internal/representation.h"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::config
  PUBLIC
)
