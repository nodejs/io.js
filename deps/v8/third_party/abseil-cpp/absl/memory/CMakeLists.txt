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
    memory
  HDRS
    "memory.h"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::core_headers
    absl::meta
  PUBLIC
)

absl_cc_test(
  NAME
    memory_test
  SRCS
    "memory_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::memory
    absl::core_headers
    GTest::gmock_main
)
