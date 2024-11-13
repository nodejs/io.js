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
    time
  HDRS
    "civil_time.h"
    "clock.h"
    "time.h"
  SRCS
    "civil_time.cc"
    "clock.cc"
    "duration.cc"
    "format.cc"
    "internal/get_current_time_chrono.inc"
    "internal/get_current_time_posix.inc"
    "time.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::base
    absl::civil_time
    absl::core_headers
    absl::int128
    absl::raw_logging_internal
    absl::strings
    absl::time_zone
  PUBLIC
)

absl_cc_library(
  NAME
    civil_time
  HDRS
    "internal/cctz/include/cctz/civil_time.h"
    "internal/cctz/include/cctz/civil_time_detail.h"
  SRCS
  "internal/cctz/src/civil_time_detail.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
)

absl_cc_library(
  NAME
    time_zone
  HDRS
    "internal/cctz/include/cctz/time_zone.h"
    "internal/cctz/include/cctz/zone_info_source.h"
  SRCS
    "internal/cctz/src/time_zone_fixed.cc"
    "internal/cctz/src/time_zone_fixed.h"
    "internal/cctz/src/time_zone_format.cc"
    "internal/cctz/src/time_zone_if.cc"
    "internal/cctz/src/time_zone_if.h"
    "internal/cctz/src/time_zone_impl.cc"
    "internal/cctz/src/time_zone_impl.h"
    "internal/cctz/src/time_zone_info.cc"
    "internal/cctz/src/time_zone_info.h"
    "internal/cctz/src/time_zone_libc.cc"
    "internal/cctz/src/time_zone_libc.h"
    "internal/cctz/src/time_zone_lookup.cc"
    "internal/cctz/src/time_zone_posix.cc"
    "internal/cctz/src/time_zone_posix.h"
    "internal/cctz/src/tzfile.h"
    "internal/cctz/src/zone_info_source.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    Threads::Threads
    # TODO(#1495): Use $<LINK_LIBRARY:FRAMEWORK,CoreFoundation> once our
    # minimum CMake version >= 3.24
    $<$<PLATFORM_ID:Darwin,iOS,tvOS,visionOS,watchOS>:-Wl,-framework,CoreFoundation>
)

# Internal-only target, do not depend on directly.
absl_cc_library(
  NAME
    time_internal_test_util
  HDRS
    "internal/test_util.h"
  SRCS
    "internal/test_util.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::time
    absl::config
    absl::raw_logging_internal
    absl::time_zone
  TESTONLY
)

absl_cc_test(
  NAME
    time_test
  SRCS
    "civil_time_test.cc"
    "clock_test.cc"
    "duration_test.cc"
    "format_test.cc"
    "time_test.cc"
    "time_zone_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::time_internal_test_util
    absl::time
    absl::config
    absl::core_headers
    absl::strings
    absl::str_format
    absl::time_zone
    GTest::gmock_main
)

absl_cc_test(
  NAME
    flag_test
  SRCS
    "flag_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::flags
    absl::flags_reflection
    GTest::gmock_main
)
