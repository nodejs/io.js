#
# Copyright 2020 The Abseil Authors.
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
    status
  HDRS
    "status.h"
  SRCS
    "internal/status_internal.h"
    "internal/status_internal.cc"
    "status.cc"
    "status_payload_printer.h"
    "status_payload_printer.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEFINES
    "$<$<PLATFORM_ID:AIX>:_LINUX_SOURCE_COMPAT>"
  DEPS
    absl::atomic_hook
    absl::config
    absl::cord
    absl::core_headers
    absl::function_ref
    absl::inlined_vector
    absl::memory
    absl::no_destructor
    absl::nullability
    absl::optional
    absl::raw_logging_internal
    absl::span
    absl::stacktrace
    absl::strerror
    absl::str_format
    absl::strings
    absl::symbolize
  PUBLIC
)

absl_cc_test(
  NAME
    status_test
  SRCS
   "status_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::status
    absl::str_format
    absl::strings
    GTest::gmock_main
)

absl_cc_library(
  NAME
    statusor
  HDRS
    "statusor.h"
  SRCS
    "statusor.cc"
    "internal/statusor_internal.h"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::base
    absl::config
    absl::core_headers
    absl::has_ostream_operator
    absl::nullability
    absl::raw_logging_internal
    absl::status
    absl::str_format
    absl::strings
    absl::type_traits
    absl::utility
    absl::variant
  PUBLIC
)

absl_cc_test(
  NAME
    statusor_test
  SRCS
   "statusor_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::status
    absl::statusor
    absl::strings
    GTest::gmock_main
)
