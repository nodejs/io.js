//
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
//

#ifndef ABSL_BASE_INTERNAL_THROW_DELEGATE_H_
#define ABSL_BASE_INTERNAL_THROW_DELEGATE_H_

#include <string>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {

// Helper functions that allow throwing exceptions consistently from anywhere.
// The main use case is for header-based libraries (eg templates), as they will
// be built by many different targets with their own compiler options.
// In particular, this will allow a safe way to throw exceptions even if the
// caller is compiled with -fno-exceptions.  This is intended for implementing
// things like map<>::at(), which the standard documents as throwing an
// exception on error.
//
// Using other techniques like #if tricks could lead to ODR violations.
//
// You shouldn't use it unless you're writing code that you know will be built
// both with and without exceptions and you need to conform to an interface
// that uses exceptions.

[[noreturn]] void ThrowStdLogicError(const std::string& what_arg);
[[noreturn]] void ThrowStdLogicError(const char* what_arg);
[[noreturn]] void ThrowStdInvalidArgument(const std::string& what_arg);
[[noreturn]] void ThrowStdInvalidArgument(const char* what_arg);
[[noreturn]] void ThrowStdDomainError(const std::string& what_arg);
[[noreturn]] void ThrowStdDomainError(const char* what_arg);
[[noreturn]] void ThrowStdLengthError(const std::string& what_arg);
[[noreturn]] void ThrowStdLengthError(const char* what_arg);
[[noreturn]] void ThrowStdOutOfRange(const std::string& what_arg);
[[noreturn]] void ThrowStdOutOfRange(const char* what_arg);
[[noreturn]] void ThrowStdRuntimeError(const std::string& what_arg);
[[noreturn]] void ThrowStdRuntimeError(const char* what_arg);
[[noreturn]] void ThrowStdRangeError(const std::string& what_arg);
[[noreturn]] void ThrowStdRangeError(const char* what_arg);
[[noreturn]] void ThrowStdOverflowError(const std::string& what_arg);
[[noreturn]] void ThrowStdOverflowError(const char* what_arg);
[[noreturn]] void ThrowStdUnderflowError(const std::string& what_arg);
[[noreturn]] void ThrowStdUnderflowError(const char* what_arg);

[[noreturn]] void ThrowStdBadFunctionCall();
[[noreturn]] void ThrowStdBadAlloc();

// ThrowStdBadArrayNewLength() cannot be consistently supported because
// std::bad_array_new_length is missing in libstdc++ until 4.9.0.
// https://gcc.gnu.org/onlinedocs/gcc-4.8.3/libstdc++/api/a01379_source.html
// https://gcc.gnu.org/onlinedocs/gcc-4.9.0/libstdc++/api/a01327_source.html
// libcxx (as of 3.2) and msvc (as of 2015) both have it.
// [[noreturn]] void ThrowStdBadArrayNewLength();

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_BASE_INTERNAL_THROW_DELEGATE_H_
