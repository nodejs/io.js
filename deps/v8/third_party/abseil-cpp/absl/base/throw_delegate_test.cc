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

#include "absl/base/internal/throw_delegate.h"

#include <functional>
#include <new>
#include <stdexcept>

#include "absl/base/config.h"
#include "gtest/gtest.h"

namespace {

using absl::base_internal::ThrowStdLogicError;
using absl::base_internal::ThrowStdInvalidArgument;
using absl::base_internal::ThrowStdDomainError;
using absl::base_internal::ThrowStdLengthError;
using absl::base_internal::ThrowStdOutOfRange;
using absl::base_internal::ThrowStdRuntimeError;
using absl::base_internal::ThrowStdRangeError;
using absl::base_internal::ThrowStdOverflowError;
using absl::base_internal::ThrowStdUnderflowError;
using absl::base_internal::ThrowStdBadFunctionCall;
using absl::base_internal::ThrowStdBadAlloc;

constexpr const char* what_arg = "The quick brown fox jumps over the lazy dog";

template <typename E>
void ExpectThrowChar(void (*f)(const char*)) {
#ifdef ABSL_HAVE_EXCEPTIONS
  try {
    f(what_arg);
    FAIL() << "Didn't throw";
  } catch (const E& e) {
    EXPECT_STREQ(e.what(), what_arg);
  }
#else
  EXPECT_DEATH_IF_SUPPORTED(f(what_arg), what_arg);
#endif
}

template <typename E>
void ExpectThrowString(void (*f)(const std::string&)) {
#ifdef ABSL_HAVE_EXCEPTIONS
  try {
    f(what_arg);
    FAIL() << "Didn't throw";
  } catch (const E& e) {
    EXPECT_STREQ(e.what(), what_arg);
  }
#else
  EXPECT_DEATH_IF_SUPPORTED(f(what_arg), what_arg);
#endif
}

template <typename E>
void ExpectThrowNoWhat(void (*f)()) {
#ifdef ABSL_HAVE_EXCEPTIONS
  try {
    f();
    FAIL() << "Didn't throw";
  } catch (const E& e) {
  }
#else
  EXPECT_DEATH_IF_SUPPORTED(f(), "");
#endif
}

TEST(ThrowDelegate, ThrowStdLogicErrorChar) {
  ExpectThrowChar<std::logic_error>(ThrowStdLogicError);
}

TEST(ThrowDelegate, ThrowStdInvalidArgumentChar) {
  ExpectThrowChar<std::invalid_argument>(ThrowStdInvalidArgument);
}

TEST(ThrowDelegate, ThrowStdDomainErrorChar) {
  ExpectThrowChar<std::domain_error>(ThrowStdDomainError);
}

TEST(ThrowDelegate, ThrowStdLengthErrorChar) {
  ExpectThrowChar<std::length_error>(ThrowStdLengthError);
}

TEST(ThrowDelegate, ThrowStdOutOfRangeChar) {
  ExpectThrowChar<std::out_of_range>(ThrowStdOutOfRange);
}

TEST(ThrowDelegate, ThrowStdRuntimeErrorChar) {
  ExpectThrowChar<std::runtime_error>(ThrowStdRuntimeError);
}

TEST(ThrowDelegate, ThrowStdRangeErrorChar) {
  ExpectThrowChar<std::range_error>(ThrowStdRangeError);
}

TEST(ThrowDelegate, ThrowStdOverflowErrorChar) {
  ExpectThrowChar<std::overflow_error>(ThrowStdOverflowError);
}

TEST(ThrowDelegate, ThrowStdUnderflowErrorChar) {
  ExpectThrowChar<std::underflow_error>(ThrowStdUnderflowError);
}

TEST(ThrowDelegate, ThrowStdLogicErrorString) {
  ExpectThrowString<std::logic_error>(ThrowStdLogicError);
}

TEST(ThrowDelegate, ThrowStdInvalidArgumentString) {
  ExpectThrowString<std::invalid_argument>(ThrowStdInvalidArgument);
}

TEST(ThrowDelegate, ThrowStdDomainErrorString) {
  ExpectThrowString<std::domain_error>(ThrowStdDomainError);
}

TEST(ThrowDelegate, ThrowStdLengthErrorString) {
  ExpectThrowString<std::length_error>(ThrowStdLengthError);
}

TEST(ThrowDelegate, ThrowStdOutOfRangeString) {
  ExpectThrowString<std::out_of_range>(ThrowStdOutOfRange);
}

TEST(ThrowDelegate, ThrowStdRuntimeErrorString) {
  ExpectThrowString<std::runtime_error>(ThrowStdRuntimeError);
}

TEST(ThrowDelegate, ThrowStdRangeErrorString) {
  ExpectThrowString<std::range_error>(ThrowStdRangeError);
}

TEST(ThrowDelegate, ThrowStdOverflowErrorString) {
  ExpectThrowString<std::overflow_error>(ThrowStdOverflowError);
}

TEST(ThrowDelegate, ThrowStdUnderflowErrorString) {
  ExpectThrowString<std::underflow_error>(ThrowStdUnderflowError);
}

TEST(ThrowDelegate, ThrowStdBadFunctionCallNoWhat) {
#ifdef ABSL_HAVE_EXCEPTIONS
  try {
    ThrowStdBadFunctionCall();
    FAIL() << "Didn't throw";
  } catch (const std::bad_function_call&) {
  }
#ifdef _LIBCPP_VERSION
  catch (const std::exception&) {
    // https://reviews.llvm.org/D92397 causes issues with the vtable for
    // std::bad_function_call when using libc++ as a shared library.
  }
#endif
#else
  EXPECT_DEATH_IF_SUPPORTED(ThrowStdBadFunctionCall(), "");
#endif
}

TEST(ThrowDelegate, ThrowStdBadAllocNoWhat) {
  ExpectThrowNoWhat<std::bad_alloc>(ThrowStdBadAlloc);
}

}  // namespace
