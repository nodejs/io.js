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

find_library(EXECINFO_LIBRARY execinfo)

absl_cc_library(
  NAME
    stacktrace
  HDRS
    "stacktrace.h"
    "internal/stacktrace_aarch64-inl.inc"
    "internal/stacktrace_arm-inl.inc"
    "internal/stacktrace_config.h"
    "internal/stacktrace_emscripten-inl.inc"
    "internal/stacktrace_generic-inl.inc"
    "internal/stacktrace_powerpc-inl.inc"
    "internal/stacktrace_riscv-inl.inc"
    "internal/stacktrace_unimplemented-inl.inc"
    "internal/stacktrace_win32-inl.inc"
    "internal/stacktrace_x86-inl.inc"
  SRCS
    "stacktrace.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  LINKOPTS
    $<$<BOOL:${EXECINFO_LIBRARY}>:${EXECINFO_LIBRARY}>
  DEPS
    absl::debugging_internal
    absl::config
    absl::core_headers
    absl::dynamic_annotations
    absl::raw_logging_internal
  PUBLIC
)

absl_cc_test(
  NAME
    stacktrace_test
  SRCS
    "stacktrace_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::stacktrace
    absl::core_headers
    GTest::gmock_main
)

absl_cc_library(
  NAME
    symbolize
  HDRS
    "symbolize.h"
    "internal/symbolize.h"
  SRCS
    "symbolize.cc"
    "symbolize_darwin.inc"
    "symbolize_elf.inc"
    "symbolize_emscripten.inc"
    "symbolize_unimplemented.inc"
    "symbolize_win32.inc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  LINKOPTS
    ${ABSL_DEFAULT_LINKOPTS}
    $<$<BOOL:${MINGW}>:-ldbghelp>
  DEPS
    absl::debugging_internal
    absl::demangle_internal
    absl::base
    absl::config
    absl::core_headers
    absl::dynamic_annotations
    absl::malloc_internal
    absl::raw_logging_internal
    absl::strings
  PUBLIC
)

absl_cc_test(
  NAME
    symbolize_test
  SRCS
    "symbolize_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
    $<$<BOOL:${MSVC}>:-Z7>
  LINKOPTS
    $<$<BOOL:${MSVC}>:-DEBUG>
  DEPS
    absl::base
    absl::check
    absl::config
    absl::core_headers
    absl::log
    absl::memory
    absl::stack_consumption
    absl::strings
    absl::symbolize
    GTest::gmock
)

# Internal-only target, do not depend on directly.
absl_cc_library(
  NAME
    examine_stack
  HDRS
    "internal/examine_stack.h"
  SRCS
    "internal/examine_stack.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::stacktrace
    absl::symbolize
    absl::config
    absl::core_headers
    absl::raw_logging_internal
)

absl_cc_library(
  NAME
    failure_signal_handler
  HDRS
    "failure_signal_handler.h"
  SRCS
    "failure_signal_handler.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::examine_stack
    absl::stacktrace
    absl::base
    absl::config
    absl::core_headers
    absl::raw_logging_internal
  PUBLIC
)

absl_cc_test(
  NAME
    failure_signal_handler_test
  SRCS
    "failure_signal_handler_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::check
    absl::failure_signal_handler
    absl::stacktrace
    absl::symbolize
    absl::strings
    absl::raw_logging_internal
    Threads::Threads
    GTest::gmock
)

# Internal-only target, do not depend on directly.
absl_cc_library(
  NAME
    debugging_internal
  HDRS
    "internal/address_is_readable.h"
    "internal/elf_mem_image.h"
    "internal/vdso_support.h"
  SRCS
    "internal/address_is_readable.cc"
    "internal/elf_mem_image.cc"
    "internal/vdso_support.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::core_headers
    absl::config
    absl::dynamic_annotations
    absl::errno_saver
    absl::raw_logging_internal
)

# Internal-only target, do not depend on directly.
absl_cc_library(
  NAME
    demangle_internal
  HDRS
    "internal/demangle.h"
  SRCS
    "internal/demangle.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::base
    absl::core_headers
  PUBLIC
)

absl_cc_test(
  NAME
    demangle_test
  SRCS
    "internal/demangle_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::demangle_internal
    absl::stack_consumption
    absl::config
    absl::core_headers
    absl::log
    absl::memory
    GTest::gmock_main
)

absl_cc_library(
  NAME
    leak_check
  HDRS
    "leak_check.h"
  SRCS
    "leak_check.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::config
    absl::core_headers
  PUBLIC
)

absl_cc_test(
  NAME
    leak_check_test
  SRCS
    "leak_check_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  LINKOPTS
    ${ABSL_DEFAULT_LINKOPTS}
  DEPS
    absl::leak_check
    absl::base
    absl::log
    GTest::gmock_main
)

# Internal-only target, do not depend on directly.
absl_cc_library(
  NAME
    stack_consumption
  HDRS
    "internal/stack_consumption.h"
  SRCS
    "internal/stack_consumption.cc"
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::config
    absl::core_headers
    absl::raw_logging_internal
  TESTONLY
)

absl_cc_test(
  NAME
    stack_consumption_test
  SRCS
    "internal/stack_consumption_test.cc"
  COPTS
    ${ABSL_TEST_COPTS}
  DEPS
    absl::stack_consumption
    absl::core_headers
    absl::log
    GTest::gmock_main
)

# component target
absl_cc_library(
  NAME
    debugging
  COPTS
    ${ABSL_DEFAULT_COPTS}
  DEPS
    absl::stacktrace
    absl::leak_check
  PUBLIC
)
