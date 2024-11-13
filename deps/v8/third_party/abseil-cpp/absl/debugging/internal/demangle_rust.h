// Copyright 2024 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef ABSL_DEBUGGING_INTERNAL_DEMANGLE_RUST_H_
#define ABSL_DEBUGGING_INTERNAL_DEMANGLE_RUST_H_

#include <cstddef>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {

// Demangle the Rust encoding `mangled`.  On success, return true and write the
// demangled symbol name to `out`.  Otherwise, return false, leaving unspecified
// contents in `out`.  For example, calling DemangleRustSymbolEncoding with
// `mangled = "_RNvC8my_crate7my_func"` will yield `my_crate::my_func` in `out`,
// provided `out_size` is large enough for that value and its trailing NUL.
//
// DemangleRustSymbolEncoding is async-signal-safe and runs in bounded C++
// call-stack space.  It is suitable for symbolizing stack traces in a signal
// handler.
bool DemangleRustSymbolEncoding(const char* mangled, char* out,
                                size_t out_size);

}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_DEBUGGING_INTERNAL_DEMANGLE_RUST_H_
