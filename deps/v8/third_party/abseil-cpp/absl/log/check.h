// Copyright 2022 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// -----------------------------------------------------------------------------
// File: log/check.h
// -----------------------------------------------------------------------------
//
// This header declares a family of `CHECK` macros.
//
// `CHECK` macros terminate the program with a fatal error if the specified
// condition is not true.
//
// Except for those whose names begin with `DCHECK`, these macros are not
// controlled by `NDEBUG` (cf. `assert`), so the check will be executed
// regardless of compilation mode. `CHECK` and friends are thus useful for
// confirming invariants in situations where continuing to run would be worse
// than terminating, e.g., due to risk of data corruption or security
// compromise.  It is also more robust and portable to deliberately terminate
// at a particular place with a useful message and backtrace than to assume some
// ultimately unspecified and unreliable crashing behavior (such as a
// "segmentation fault").

#ifndef ABSL_LOG_CHECK_H_
#define ABSL_LOG_CHECK_H_

#include "absl/log/internal/check_impl.h"
#include "absl/log/internal/check_op.h"     // IWYU pragma: export
#include "absl/log/internal/conditions.h"   // IWYU pragma: export
#include "absl/log/internal/log_message.h"  // IWYU pragma: export
#include "absl/log/internal/strip.h"        // IWYU pragma: export

// CHECK()
//
// `CHECK` terminates the program with a fatal error if `condition` is not true.
//
// The message may include additional information such as stack traces, when
// available.
//
// Example:
//
//   CHECK(!cheese.empty()) << "Out of Cheese";
//
// Might produce a message like:
//
//   Check failed: !cheese.empty() Out of Cheese
#define CHECK(condition) ABSL_LOG_INTERNAL_CHECK_IMPL((condition), #condition)

// QCHECK()
//
// `QCHECK` behaves like `CHECK` but does not print a full stack trace and does
// not run registered error handlers (as `QFATAL`).  It is useful when the
// problem is definitely unrelated to program flow, e.g. when validating user
// input.
#define QCHECK(condition) ABSL_LOG_INTERNAL_QCHECK_IMPL((condition), #condition)

// PCHECK()
//
// `PCHECK` behaves like `CHECK` but appends a description of the current state
// of `errno` to the failure message.
//
// Example:
//
//   int fd = open("/var/empty/missing", O_RDONLY);
//   PCHECK(fd != -1) << "posix is difficult";
//
// Might produce a message like:
//
//   Check failed: fd != -1 posix is difficult: No such file or directory [2]
#define PCHECK(condition) ABSL_LOG_INTERNAL_PCHECK_IMPL((condition), #condition)

// DCHECK()
//
// `DCHECK` behaves like `CHECK` in debug mode and does nothing otherwise (as
// `DLOG`).  Unlike with `CHECK` (but as with `assert`), it is not safe to rely
// on evaluation of `condition`: when `NDEBUG` is enabled, DCHECK does not
// evaluate the condition.
#define DCHECK(condition) ABSL_LOG_INTERNAL_DCHECK_IMPL((condition), #condition)

// `CHECK_EQ` and friends are syntactic sugar for `CHECK(x == y)` that
// automatically output the expression being tested and the evaluated values on
// either side.
//
// Example:
//
//   int x = 3, y = 5;
//   CHECK_EQ(2 * x, y) << "oops!";
//
// Might produce a message like:
//
//   Check failed: 2 * x == y (6 vs. 5) oops!
//
// The values must implement the appropriate comparison operator as well as
// `operator<<(std::ostream&, ...)`.  Care is taken to ensure that each
// argument is evaluated exactly once, and that anything which is legal to pass
// as a function argument is legal here.  In particular, the arguments may be
// temporary expressions which will end up being destroyed at the end of the
// statement,
//
// Example:
//
//   CHECK_EQ(std::string("abc")[1], 'b');
//
// WARNING: Passing `NULL` as an argument to `CHECK_EQ` and similar macros does
// not compile.  Use `nullptr` instead.
#define CHECK_EQ(val1, val2) \
  ABSL_LOG_INTERNAL_CHECK_EQ_IMPL((val1), #val1, (val2), #val2)
#define CHECK_NE(val1, val2) \
  ABSL_LOG_INTERNAL_CHECK_NE_IMPL((val1), #val1, (val2), #val2)
#define CHECK_LE(val1, val2) \
  ABSL_LOG_INTERNAL_CHECK_LE_IMPL((val1), #val1, (val2), #val2)
#define CHECK_LT(val1, val2) \
  ABSL_LOG_INTERNAL_CHECK_LT_IMPL((val1), #val1, (val2), #val2)
#define CHECK_GE(val1, val2) \
  ABSL_LOG_INTERNAL_CHECK_GE_IMPL((val1), #val1, (val2), #val2)
#define CHECK_GT(val1, val2) \
  ABSL_LOG_INTERNAL_CHECK_GT_IMPL((val1), #val1, (val2), #val2)
#define QCHECK_EQ(val1, val2) \
  ABSL_LOG_INTERNAL_QCHECK_EQ_IMPL((val1), #val1, (val2), #val2)
#define QCHECK_NE(val1, val2) \
  ABSL_LOG_INTERNAL_QCHECK_NE_IMPL((val1), #val1, (val2), #val2)
#define QCHECK_LE(val1, val2) \
  ABSL_LOG_INTERNAL_QCHECK_LE_IMPL((val1), #val1, (val2), #val2)
#define QCHECK_LT(val1, val2) \
  ABSL_LOG_INTERNAL_QCHECK_LT_IMPL((val1), #val1, (val2), #val2)
#define QCHECK_GE(val1, val2) \
  ABSL_LOG_INTERNAL_QCHECK_GE_IMPL((val1), #val1, (val2), #val2)
#define QCHECK_GT(val1, val2) \
  ABSL_LOG_INTERNAL_QCHECK_GT_IMPL((val1), #val1, (val2), #val2)
#define DCHECK_EQ(val1, val2) \
  ABSL_LOG_INTERNAL_DCHECK_EQ_IMPL((val1), #val1, (val2), #val2)
#define DCHECK_NE(val1, val2) \
  ABSL_LOG_INTERNAL_DCHECK_NE_IMPL((val1), #val1, (val2), #val2)
#define DCHECK_LE(val1, val2) \
  ABSL_LOG_INTERNAL_DCHECK_LE_IMPL((val1), #val1, (val2), #val2)
#define DCHECK_LT(val1, val2) \
  ABSL_LOG_INTERNAL_DCHECK_LT_IMPL((val1), #val1, (val2), #val2)
#define DCHECK_GE(val1, val2) \
  ABSL_LOG_INTERNAL_DCHECK_GE_IMPL((val1), #val1, (val2), #val2)
#define DCHECK_GT(val1, val2) \
  ABSL_LOG_INTERNAL_DCHECK_GT_IMPL((val1), #val1, (val2), #val2)

// `CHECK_OK` and friends validate that the provided `absl::Status` or
// `absl::StatusOr<T>` is OK.  If it isn't, they print a failure message that
// includes the actual status and terminate the program.
//
// As with all `DCHECK` variants, `DCHECK_OK` has no effect (not even
// evaluating its argument) if `NDEBUG` is enabled.
//
// Example:
//
//   CHECK_OK(FunctionReturnsStatus(x, y, z)) << "oops!";
//
// Might produce a message like:
//
//   Check failed: FunctionReturnsStatus(x, y, z) is OK (ABORTED: timeout) oops!
#define CHECK_OK(status) ABSL_LOG_INTERNAL_CHECK_OK_IMPL((status), #status)
#define QCHECK_OK(status) ABSL_LOG_INTERNAL_QCHECK_OK_IMPL((status), #status)
#define DCHECK_OK(status) ABSL_LOG_INTERNAL_DCHECK_OK_IMPL((status), #status)

// `CHECK_STREQ` and friends provide `CHECK_EQ` functionality for C strings,
// i.e., null-terminated char arrays.  The `CASE` versions are case-insensitive.
//
// Example:
//
//   CHECK_STREQ(argv[0], "./skynet");
//
// Note that both arguments may be temporary strings which are destroyed by the
// compiler at the end of the current full expression.
//
// Example:
//
//   CHECK_STREQ(Foo().c_str(), Bar().c_str());
#define CHECK_STREQ(s1, s2) \
  ABSL_LOG_INTERNAL_CHECK_STREQ_IMPL((s1), #s1, (s2), #s2)
#define CHECK_STRNE(s1, s2) \
  ABSL_LOG_INTERNAL_CHECK_STRNE_IMPL((s1), #s1, (s2), #s2)
#define CHECK_STRCASEEQ(s1, s2) \
  ABSL_LOG_INTERNAL_CHECK_STRCASEEQ_IMPL((s1), #s1, (s2), #s2)
#define CHECK_STRCASENE(s1, s2) \
  ABSL_LOG_INTERNAL_CHECK_STRCASENE_IMPL((s1), #s1, (s2), #s2)
#define QCHECK_STREQ(s1, s2) \
  ABSL_LOG_INTERNAL_QCHECK_STREQ_IMPL((s1), #s1, (s2), #s2)
#define QCHECK_STRNE(s1, s2) \
  ABSL_LOG_INTERNAL_QCHECK_STRNE_IMPL((s1), #s1, (s2), #s2)
#define QCHECK_STRCASEEQ(s1, s2) \
  ABSL_LOG_INTERNAL_QCHECK_STRCASEEQ_IMPL((s1), #s1, (s2), #s2)
#define QCHECK_STRCASENE(s1, s2) \
  ABSL_LOG_INTERNAL_QCHECK_STRCASENE_IMPL((s1), #s1, (s2), #s2)
#define DCHECK_STREQ(s1, s2) \
  ABSL_LOG_INTERNAL_DCHECK_STREQ_IMPL((s1), #s1, (s2), #s2)
#define DCHECK_STRNE(s1, s2) \
  ABSL_LOG_INTERNAL_DCHECK_STRNE_IMPL((s1), #s1, (s2), #s2)
#define DCHECK_STRCASEEQ(s1, s2) \
  ABSL_LOG_INTERNAL_DCHECK_STRCASEEQ_IMPL((s1), #s1, (s2), #s2)
#define DCHECK_STRCASENE(s1, s2) \
  ABSL_LOG_INTERNAL_DCHECK_STRCASENE_IMPL((s1), #s1, (s2), #s2)

#endif  // ABSL_LOG_CHECK_H_
