# Copyright 2021 The Abseil Authors.
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
    cleanup_internal
  HDRS
    "internal/cleanup.h"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::base_internal
    absl::core_headers
    absl::utility
  PUBLIC
)

absl_cc_library(
  NAME
    cleanup
  HDRS
    "cleanup.h"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::cleanup_internal
    absl::config
    absl::core_headers
  PUBLIC
)

absl_cc_test(
  NAME
    cleanup_test
  SRCS
    "cleanup_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::cleanup
    absl::config
    absl::utility
    GTest::gmock_main
)
