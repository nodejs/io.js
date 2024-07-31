#
# Copyright 2018 The Abseil Authors.
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
    hash
  HDRS
    "hash.h"
  SRCS
    "internal/hash.cc"
    "internal/hash.h"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::bits
    absl::city
    absl::config
    absl::core_headers
    absl::endian
    absl::fixed_array
    absl::function_ref
    absl::meta
    absl::int128
    absl::strings
    absl::optional
    absl::variant
    absl::utility
    absl::low_level_hash
  PUBLIC
)

absl_cc_library(
  NAME
    hash_testing
  HDRS
    "hash_testing.h"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::spy_hash_state
    absl::meta
    absl::strings
    absl::variant
    GTest::gmock
  TESTONLY
  PUBLIC
)

absl_cc_test(
  NAME
    hash_test
  SRCS
    "hash_test.cc"
    "internal/hash_test.h"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::btree
    absl::cord_test_helpers
    absl::core_headers
    absl::flat_hash_map
    absl::flat_hash_set
    absl::hash
    absl::hash_testing
    absl::int128
    absl::memory
    absl::meta
    absl::node_hash_map
    absl::node_hash_set
    absl::optional
    absl::spy_hash_state
    absl::string_view
    absl::variant
    GTest::gmock_main
)

absl_cc_test(
  NAME
    hash_instantiated_test
  SRCS
    "hash_instantiated_test.cc"
    "internal/hash_test.h"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::hash
    absl::hash_testing
    absl::config
    absl::btree
    absl::flat_hash_map
    absl::flat_hash_set
    absl::node_hash_map
    absl::node_hash_set
    GTest::gmock_main
)

# Internal-only target, do not depend on directly.
#
# Note: Even though external code should not depend on this target
# directly, it must be marked PUBLIC since it is a dependency of
# hash_testing.
absl_cc_library(
  NAME
    spy_hash_state
  HDRS
    "internal/spy_hash_state.h"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::hash
    absl::strings
    absl::str_format
  TESTONLY
  PUBLIC
)

# Internal-only target, do not depend on directly.
absl_cc_library(
  NAME
    city
  HDRS
    "internal/city.h"
  SRCS
    "internal/city.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::config
    absl::core_headers
    absl::endian
)

absl_cc_test(
  NAME
    city_test
  SRCS
    "internal/city_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::city
    GTest::gmock_main
)

# Internal-only target, do not depend on directly.
absl_cc_library(
  NAME
    low_level_hash
  HDRS
    "internal/low_level_hash.h"
  SRCS
    "internal/low_level_hash.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::config
    absl::endian
    absl::int128
    absl::prefetch
)

absl_cc_test(
  NAME
    low_level_hash_test
  SRCS
    "internal/low_level_hash_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::low_level_hash
    absl::strings
    GTest::gmock_main
)
