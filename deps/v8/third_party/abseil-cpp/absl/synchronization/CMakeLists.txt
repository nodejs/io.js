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

# Internal-only target, do not depend on directly.
absl_cc_library(
  NAME
    graphcycles_internal
  HDRS
    "internal/graphcycles.h"
  SRCS
    "internal/graphcycles.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::base
    absl::base_internal
    absl::config
    absl::core_headers
    absl::malloc_internal
    absl::raw_logging_internal
)

# Internal-only target, do not depend on directly.
absl_cc_library(
  NAME
    kernel_timeout_internal
  HDRS
    "internal/kernel_timeout.h"
  SRCS
    "internal/kernel_timeout.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::base
    absl::config
    absl::core_headers
    absl::raw_logging_internal
    absl::time
)

absl_cc_test(
  NAME
    kernel_timeout_internal_test
  SRCS
    "internal/kernel_timeout_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::kernel_timeout_internal
    absl::config
    absl::random_random
    absl::time
    GTest::gmock_main
)

absl_cc_library(
  NAME
    synchronization
  HDRS
    "barrier.h"
    "blocking_counter.h"
    "internal/create_thread_identity.h"
    "internal/futex.h"
    "internal/futex_waiter.h"
    "internal/per_thread_sem.h"
    "internal/pthread_waiter.h"
    "internal/sem_waiter.h"
    "internal/stdcpp_waiter.h"
    "internal/waiter.h"
    "internal/waiter_base.h"
    "internal/win32_waiter.h"
    "mutex.h"
    "notification.h"
  SRCS
    "barrier.cc"
    "blocking_counter.cc"
    "internal/create_thread_identity.cc"
    "internal/futex_waiter.cc"
    "internal/per_thread_sem.cc"
    "internal/pthread_waiter.cc"
    "internal/sem_waiter.cc"
    "internal/stdcpp_waiter.cc"
    "internal/waiter_base.cc"
    "internal/win32_waiter.cc"
    "notification.cc"
    "mutex.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::graphcycles_internal
    absl::kernel_timeout_internal
    absl::atomic_hook
    absl::base
    absl::base_internal
    absl::config
    absl::core_headers
    absl::dynamic_annotations
    absl::malloc_internal
    absl::raw_logging_internal
    absl::stacktrace
    absl::symbolize
    absl::time
    Threads::Threads
  PUBLIC
)

absl_cc_test(
  NAME
    barrier_test
  SRCS
    "barrier_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::synchronization
    absl::time
    GTest::gmock_main
)

absl_cc_test(
  NAME
    blocking_counter_test
  SRCS
    "blocking_counter_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::synchronization
    absl::time
    GTest::gmock_main
)

absl_cc_test(
  NAME
    graphcycles_test
  SRCS
    "internal/graphcycles_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::check
    absl::core_headers
    absl::graphcycles_internal
    absl::log
    GTest::gmock_main
)

# Internal-only target, do not depend on directly.
absl_cc_library(
  NAME
    thread_pool
  HDRS
    "internal/thread_pool.h"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::any_invocable
    absl::core_headers
    absl::synchronization
  TESTONLY
)

absl_cc_test(
  NAME
    mutex_test
  SRCS
    "mutex_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::synchronization
    absl::thread_pool
    absl::base
    absl::check
    absl::config
    absl::core_headers
    absl::log
    absl::memory
    absl::time
    GTest::gmock_main
)

absl_cc_test(
  NAME
    mutex_method_pointer_test
  SRCS
    "mutex_method_pointer_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::base
    absl::config
    absl::synchronization
    GTest::gmock_main
)

absl_cc_test(
  NAME
    notification_test
  SRCS
    "notification_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::synchronization
    absl::time
    GTest::gmock_main
)

# Internal-only target, do not depend on directly.
absl_cc_library(
  NAME
    per_thread_sem_test_common
  SRCS
    "internal/per_thread_sem_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::synchronization
    absl::base
    absl::config
    absl::strings
    absl::time
    GTest::gmock
  TESTONLY
)

absl_cc_test(
  NAME
    per_thread_sem_test
  SRCS
    "internal/per_thread_sem_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::per_thread_sem_test_common
    absl::synchronization
    absl::strings
    absl::time
    GTest::gmock_main
)

absl_cc_test(
  NAME
    waiter_test
  SRCS
    "internal/waiter_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::config
    absl::kernel_timeout_internal
    absl::random_random
    absl::synchronization
    absl::thread_pool
    absl::time
    GTest::gmock_main
)

absl_cc_test(
  NAME
    lifetime_test
  SRCS
    "lifetime_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::synchronization
    absl::core_headers
    absl::check
)
