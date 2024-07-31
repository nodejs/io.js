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
// File: log/absl_check.h
// -----------------------------------------------------------------------------
//
// This header declares a family of `ABSL_CHECK` macros as alternative spellings
// for `CHECK` macros in `check.h`.
//
// Except for those whose names begin with `ABSL_DCHECK`, these macros are not
// controlled by `NDEBUG` (cf. `assert`), so the check will be executed
// regardless of compilation mode. `ABSL_CHECK` and friends are thus useful for
// confirming invariants in situations where continuing to run would be worse
// than terminating, e.g., due to risk of data corruption or security
// compromise.  It is also more robust and portable to deliberately terminate
// at a particular place with a useful message and backtrace than to assume some
// ultimately unspecified and unreliable crashing behavior (such as a
// "segmentation fault").
//
// For full documentation of each macro, see comments in `check.h`, which has an
// identical set of macros without the ABSL_* prefix.

#ifndef ABSL_LOG_ABSL_CHECK_H_
#define ABSL_LOG_ABSL_CHECK_H_

#include "absl/log/internal/check_impl.h"

#define ABSL_CHECK(condition) \
  ABSL_LOG_INTERNAL_CHECK_IMPL((condition), #condition)
#define ABSL_QCHECK(condition) \
  ABSL_LOG_INTERNAL_QCHECK_IMPL((condition), #condition)
#define ABSL_PCHECK(condition) \
  ABSL_LOG_INTERNAL_PCHECK_IMPL((condition), #condition)
#define ABSL_DCHECK(condition) \
  ABSL_LOG_INTERNAL_DCHECK_IMPL((condition), #condition)

#define ABSL_CHECK_EQ(val1, val2) \
  ABSL_LOG_INTERNAL_CHECK_EQ_IMPL((val1), #val1, (val2), #val2)
#define ABSL_CHECK_NE(val1, val2) \
  ABSL_LOG_INTERNAL_CHECK_NE_IMPL((val1), #val1, (val2), #val2)
#define ABSL_CHECK_LE(val1, val2) \
  ABSL_LOG_INTERNAL_CHECK_LE_IMPL((val1), #val1, (val2), #val2)
#define ABSL_CHECK_LT(val1, val2) \
  ABSL_LOG_INTERNAL_CHECK_LT_IMPL((val1), #val1, (val2), #val2)
#define ABSL_CHECK_GE(val1, val2) \
  ABSL_LOG_INTERNAL_CHECK_GE_IMPL((val1), #val1, (val2), #val2)
#define ABSL_CHECK_GT(val1, val2) \
  ABSL_LOG_INTERNAL_CHECK_GT_IMPL((val1), #val1, (val2), #val2)
#define ABSL_QCHECK_EQ(val1, val2) \
  ABSL_LOG_INTERNAL_QCHECK_EQ_IMPL((val1), #val1, (val2), #val2)
#define ABSL_QCHECK_NE(val1, val2) \
  ABSL_LOG_INTERNAL_QCHECK_NE_IMPL((val1), #val1, (val2), #val2)
#define ABSL_QCHECK_LE(val1, val2) \
  ABSL_LOG_INTERNAL_QCHECK_LE_IMPL((val1), #val1, (val2), #val2)
#define ABSL_QCHECK_LT(val1, val2) \
  ABSL_LOG_INTERNAL_QCHECK_LT_IMPL((val1), #val1, (val2), #val2)
#define ABSL_QCHECK_GE(val1, val2) \
  ABSL_LOG_INTERNAL_QCHECK_GE_IMPL((val1), #val1, (val2), #val2)
#define ABSL_QCHECK_GT(val1, val2) \
  ABSL_LOG_INTERNAL_QCHECK_GT_IMPL((val1), #val1, (val2), #val2)
#define ABSL_DCHECK_EQ(val1, val2) \
  ABSL_LOG_INTERNAL_DCHECK_EQ_IMPL((val1), #val1, (val2), #val2)
#define ABSL_DCHECK_NE(val1, val2) \
  ABSL_LOG_INTERNAL_DCHECK_NE_IMPL((val1), #val1, (val2), #val2)
#define ABSL_DCHECK_LE(val1, val2) \
  ABSL_LOG_INTERNAL_DCHECK_LE_IMPL((val1), #val1, (val2), #val2)
#define ABSL_DCHECK_LT(val1, val2) \
  ABSL_LOG_INTERNAL_DCHECK_LT_IMPL((val1), #val1, (val2), #val2)
#define ABSL_DCHECK_GE(val1, val2) \
  ABSL_LOG_INTERNAL_DCHECK_GE_IMPL((val1), #val1, (val2), #val2)
#define ABSL_DCHECK_GT(val1, val2) \
  ABSL_LOG_INTERNAL_DCHECK_GT_IMPL((val1), #val1, (val2), #val2)

#define ABSL_CHECK_OK(status) ABSL_LOG_INTERNAL_CHECK_OK_IMPL((status), #status)
#define ABSL_QCHECK_OK(status) \
  ABSL_LOG_INTERNAL_QCHECK_OK_IMPL((status), #status)
#define ABSL_DCHECK_OK(status) \
  ABSL_LOG_INTERNAL_DCHECK_OK_IMPL((status), #status)

#define ABSL_CHECK_STREQ(s1, s2) \
  ABSL_LOG_INTERNAL_CHECK_STREQ_IMPL((s1), #s1, (s2), #s2)
#define ABSL_CHECK_STRNE(s1, s2) \
  ABSL_LOG_INTERNAL_CHECK_STRNE_IMPL((s1), #s1, (s2), #s2)
#define ABSL_CHECK_STRCASEEQ(s1, s2) \
  ABSL_LOG_INTERNAL_CHECK_STRCASEEQ_IMPL((s1), #s1, (s2), #s2)
#define ABSL_CHECK_STRCASENE(s1, s2) \
  ABSL_LOG_INTERNAL_CHECK_STRCASENE_IMPL((s1), #s1, (s2), #s2)
#define ABSL_QCHECK_STREQ(s1, s2) \
  ABSL_LOG_INTERNAL_QCHECK_STREQ_IMPL((s1), #s1, (s2), #s2)
#define ABSL_QCHECK_STRNE(s1, s2) \
  ABSL_LOG_INTERNAL_QCHECK_STRNE_IMPL((s1), #s1, (s2), #s2)
#define ABSL_QCHECK_STRCASEEQ(s1, s2) \
  ABSL_LOG_INTERNAL_QCHECK_STRCASEEQ_IMPL((s1), #s1, (s2), #s2)
#define ABSL_QCHECK_STRCASENE(s1, s2) \
  ABSL_LOG_INTERNAL_QCHECK_STRCASENE_IMPL((s1), #s1, (s2), #s2)
#define ABSL_DCHECK_STREQ(s1, s2) \
  ABSL_LOG_INTERNAL_DCHECK_STREQ_IMPL((s1), #s1, (s2), #s2)
#define ABSL_DCHECK_STRNE(s1, s2) \
  ABSL_LOG_INTERNAL_DCHECK_STRNE_IMPL((s1), #s1, (s2), #s2)
#define ABSL_DCHECK_STRCASEEQ(s1, s2) \
  ABSL_LOG_INTERNAL_DCHECK_STRCASEEQ_IMPL((s1), #s1, (s2), #s2)
#define ABSL_DCHECK_STRCASENE(s1, s2) \
  ABSL_LOG_INTERNAL_DCHECK_STRCASENE_IMPL((s1), #s1, (s2), #s2)

#endif  // ABSL_LOG_ABSL_CHECK_H_
