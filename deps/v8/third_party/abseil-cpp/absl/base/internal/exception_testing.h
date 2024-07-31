// Copyright 2017 The Abseil Authors.
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

// Testing utilities for ABSL types which throw exceptions.

#ifndef ABSL_BASE_INTERNAL_EXCEPTION_TESTING_H_
#define ABSL_BASE_INTERNAL_EXCEPTION_TESTING_H_

#include "gtest/gtest.h"
#include "absl/base/config.h"

// ABSL_BASE_INTERNAL_EXPECT_FAIL tests either for a specified thrown exception
// if exceptions are enabled, or for death with a specified text in the error
// message
#ifdef ABSL_HAVE_EXCEPTIONS

#define ABSL_BASE_INTERNAL_EXPECT_FAIL(expr, exception_t, text) \
  EXPECT_THROW(expr, exception_t)

#elif defined(__ANDROID__)
// Android asserts do not log anywhere that gtest can currently inspect.
// So we expect exit, but cannot match the message.
#define ABSL_BASE_INTERNAL_EXPECT_FAIL(expr, exception_t, text) \
  EXPECT_DEATH(expr, ".*")
#else
#define ABSL_BASE_INTERNAL_EXPECT_FAIL(expr, exception_t, text) \
  EXPECT_DEATH_IF_SUPPORTED(expr, text)

#endif

#endif  // ABSL_BASE_INTERNAL_EXCEPTION_TESTING_H_
