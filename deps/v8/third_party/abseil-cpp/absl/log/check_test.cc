//
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

#include "absl/log/check.h"

#define ABSL_TEST_CHECK CHECK
#define ABSL_TEST_CHECK_OK CHECK_OK
#define ABSL_TEST_CHECK_EQ CHECK_EQ
#define ABSL_TEST_CHECK_NE CHECK_NE
#define ABSL_TEST_CHECK_GE CHECK_GE
#define ABSL_TEST_CHECK_LE CHECK_LE
#define ABSL_TEST_CHECK_GT CHECK_GT
#define ABSL_TEST_CHECK_LT CHECK_LT
#define ABSL_TEST_CHECK_STREQ CHECK_STREQ
#define ABSL_TEST_CHECK_STRNE CHECK_STRNE
#define ABSL_TEST_CHECK_STRCASEEQ CHECK_STRCASEEQ
#define ABSL_TEST_CHECK_STRCASENE CHECK_STRCASENE

#define ABSL_TEST_DCHECK DCHECK
#define ABSL_TEST_DCHECK_OK DCHECK_OK
#define ABSL_TEST_DCHECK_EQ DCHECK_EQ
#define ABSL_TEST_DCHECK_NE DCHECK_NE
#define ABSL_TEST_DCHECK_GE DCHECK_GE
#define ABSL_TEST_DCHECK_LE DCHECK_LE
#define ABSL_TEST_DCHECK_GT DCHECK_GT
#define ABSL_TEST_DCHECK_LT DCHECK_LT
#define ABSL_TEST_DCHECK_STREQ DCHECK_STREQ
#define ABSL_TEST_DCHECK_STRNE DCHECK_STRNE
#define ABSL_TEST_DCHECK_STRCASEEQ DCHECK_STRCASEEQ
#define ABSL_TEST_DCHECK_STRCASENE DCHECK_STRCASENE

#define ABSL_TEST_QCHECK QCHECK
#define ABSL_TEST_QCHECK_OK QCHECK_OK
#define ABSL_TEST_QCHECK_EQ QCHECK_EQ
#define ABSL_TEST_QCHECK_NE QCHECK_NE
#define ABSL_TEST_QCHECK_GE QCHECK_GE
#define ABSL_TEST_QCHECK_LE QCHECK_LE
#define ABSL_TEST_QCHECK_GT QCHECK_GT
#define ABSL_TEST_QCHECK_LT QCHECK_LT
#define ABSL_TEST_QCHECK_STREQ QCHECK_STREQ
#define ABSL_TEST_QCHECK_STRNE QCHECK_STRNE
#define ABSL_TEST_QCHECK_STRCASEEQ QCHECK_STRCASEEQ
#define ABSL_TEST_QCHECK_STRCASENE QCHECK_STRCASENE

#include "gtest/gtest.h"
#include "absl/log/check_test_impl.inc"
