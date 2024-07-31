#
# Copyright 2019 The Abseil Authors.
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
    any_invocable
  SRCS
    "internal/any_invocable.h"
  HDRS
    "any_invocable.h"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::base_internal
    absl::config
    absl::core_headers
    absl::type_traits
    absl::utility
  PUBLIC
)

absl_cc_test(
  NAME
    any_invocable_test
  SRCS
    "any_invocable_test.cc"
    "internal/any_invocable.h"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::any_invocable
    absl::base_internal
    absl::config
    absl::core_headers
    absl::type_traits
    absl::utility
    GTest::gmock_main
)

absl_cc_library(
  NAME
    bind_front
  SRCS
    "internal/front_binder.h"
  HDRS
    "bind_front.h"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::base_internal
    absl::compressed_tuple
  PUBLIC
)

absl_cc_test(
  NAME
    bind_front_test
  SRCS
    "bind_front_test.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::bind_front
    absl::memory
    GTest::gmock_main
)

absl_cc_library(
  NAME
    function_ref
  SRCS
    "internal/function_ref.h"
  HDRS
    "function_ref.h"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::base_internal
    absl::core_headers
    absl::any_invocable
    absl::meta
  PUBLIC
)

absl_cc_test(
  NAME
    function_ref_test
  SRCS
    "function_ref_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::function_ref
    absl::memory
    absl::test_instance_tracker
    GTest::gmock_main
)

absl_cc_library(
  NAME
    overload
  HDRS
    "overload.h"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::meta
  PUBLIC
)

absl_cc_test(
  NAME
    overload_test
  SRCS
    "overload_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::strings
    GTest::gmock_main
)
