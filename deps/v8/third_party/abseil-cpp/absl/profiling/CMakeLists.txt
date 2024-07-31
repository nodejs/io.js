# Copyright 2021 The Abseil Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

absl_cc_library(
  NAME
    sample_recorder
  HDRS
    "internal/sample_recorder.h"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::base
    absl::synchronization
)

absl_cc_test(
  NAME
    sample_recorder_test
  SRCS
    "internal/sample_recorder_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::sample_recorder
    absl::time
    GTest::gmock_main
)

absl_cc_library(
  NAME
    exponential_biased
  SRCS
    "internal/exponential_biased.cc"
  HDRS
    "internal/exponential_biased.h"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::config
    absl::core_headers
)

absl_cc_test(
  NAME
    exponential_biased_test
  SRCS
    "internal/exponential_biased_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::exponential_biased
    absl::strings
    GTest::gmock_main
)

absl_cc_library(
  NAME
    periodic_sampler
  SRCS
    "internal/periodic_sampler.cc"
  HDRS
    "internal/periodic_sampler.h"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::core_headers
    absl::exponential_biased
)

absl_cc_test(
  NAME
    periodic_sampler_test
  SRCS
    "internal/periodic_sampler_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::core_headers
    absl::periodic_sampler
    GTest::gmock_main
)

