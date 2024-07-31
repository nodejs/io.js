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

#include <cstdlib>
#include <functional>
#include <new>
#include <stdexcept>

#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {

// NOTE: The exception types, like `std::logic_error`, do not exist on all
// platforms. (For example, the Android NDK does not have them.)
// Therefore, their use must be guarded by `#ifdef` or equivalent.

void ThrowStdLogicError(const std::string& what_arg) {
#ifdef ABSL_HAVE_EXCEPTIONS
  throw std::logic_error(what_arg);
#else
  ABSL_RAW_LOG(FATAL, "%s", what_arg.c_str());
  std::abort();
#endif
}
void ThrowStdLogicError(const char* what_arg) {
#ifdef ABSL_HAVE_EXCEPTIONS
  throw std::logic_error(what_arg);
#else
  ABSL_RAW_LOG(FATAL, "%s", what_arg);
  std::abort();
#endif
}
void ThrowStdInvalidArgument(const std::string& what_arg) {
#ifdef ABSL_HAVE_EXCEPTIONS
  throw std::invalid_argument(what_arg);
#else
  ABSL_RAW_LOG(FATAL, "%s", what_arg.c_str());
  std::abort();
#endif
}
void ThrowStdInvalidArgument(const char* what_arg) {
#ifdef ABSL_HAVE_EXCEPTIONS
  throw std::invalid_argument(what_arg);
#else
  ABSL_RAW_LOG(FATAL, "%s", what_arg);
  std::abort();
#endif
}

void ThrowStdDomainError(const std::string& what_arg) {
#ifdef ABSL_HAVE_EXCEPTIONS
  throw std::domain_error(what_arg);
#else
  ABSL_RAW_LOG(FATAL, "%s", what_arg.c_str());
  std::abort();
#endif
}
void ThrowStdDomainError(const char* what_arg) {
#ifdef ABSL_HAVE_EXCEPTIONS
  throw std::domain_error(what_arg);
#else
  ABSL_RAW_LOG(FATAL, "%s", what_arg);
  std::abort();
#endif
}

void ThrowStdLengthError(const std::string& what_arg) {
#ifdef ABSL_HAVE_EXCEPTIONS
  throw std::length_error(what_arg);
#else
  ABSL_RAW_LOG(FATAL, "%s", what_arg.c_str());
  std::abort();
#endif
}
void ThrowStdLengthError(const char* what_arg) {
#ifdef ABSL_HAVE_EXCEPTIONS
  throw std::length_error(what_arg);
#else
  ABSL_RAW_LOG(FATAL, "%s", what_arg);
  std::abort();
#endif
}

void ThrowStdOutOfRange(const std::string& what_arg) {
#ifdef ABSL_HAVE_EXCEPTIONS
  throw std::out_of_range(what_arg);
#else
  ABSL_RAW_LOG(FATAL, "%s", what_arg.c_str());
  std::abort();
#endif
}
void ThrowStdOutOfRange(const char* what_arg) {
#ifdef ABSL_HAVE_EXCEPTIONS
  throw std::out_of_range(what_arg);
#else
  ABSL_RAW_LOG(FATAL, "%s", what_arg);
  std::abort();
#endif
}

void ThrowStdRuntimeError(const std::string& what_arg) {
#ifdef ABSL_HAVE_EXCEPTIONS
  throw std::runtime_error(what_arg);
#else
  ABSL_RAW_LOG(FATAL, "%s", what_arg.c_str());
  std::abort();
#endif
}
void ThrowStdRuntimeError(const char* what_arg) {
#ifdef ABSL_HAVE_EXCEPTIONS
  throw std::runtime_error(what_arg);
#else
  ABSL_RAW_LOG(FATAL, "%s", what_arg);
  std::abort();
#endif
}

void ThrowStdRangeError(const std::string& what_arg) {
#ifdef ABSL_HAVE_EXCEPTIONS
  throw std::range_error(what_arg);
#else
  ABSL_RAW_LOG(FATAL, "%s", what_arg.c_str());
  std::abort();
#endif
}
void ThrowStdRangeError(const char* what_arg) {
#ifdef ABSL_HAVE_EXCEPTIONS
  throw std::range_error(what_arg);
#else
  ABSL_RAW_LOG(FATAL, "%s", what_arg);
  std::abort();
#endif
}

void ThrowStdOverflowError(const std::string& what_arg) {
#ifdef ABSL_HAVE_EXCEPTIONS
  throw std::overflow_error(what_arg);
#else
  ABSL_RAW_LOG(FATAL, "%s", what_arg.c_str());
  std::abort();
#endif
}
void ThrowStdOverflowError(const char* what_arg) {
#ifdef ABSL_HAVE_EXCEPTIONS
  throw std::overflow_error(what_arg);
#else
  ABSL_RAW_LOG(FATAL, "%s", what_arg);
  std::abort();
#endif
}

void ThrowStdUnderflowError(const std::string& what_arg) {
#ifdef ABSL_HAVE_EXCEPTIONS
  throw std::underflow_error(what_arg);
#else
  ABSL_RAW_LOG(FATAL, "%s", what_arg.c_str());
  std::abort();
#endif
}
void ThrowStdUnderflowError(const char* what_arg) {
#ifdef ABSL_HAVE_EXCEPTIONS
  throw std::underflow_error(what_arg);
#else
  ABSL_RAW_LOG(FATAL, "%s", what_arg);
  std::abort();
#endif
}

void ThrowStdBadFunctionCall() {
#ifdef ABSL_HAVE_EXCEPTIONS
  throw std::bad_function_call();
#else
  std::abort();
#endif
}

void ThrowStdBadAlloc() {
#ifdef ABSL_HAVE_EXCEPTIONS
  throw std::bad_alloc();
#else
  std::abort();
#endif
}

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl
