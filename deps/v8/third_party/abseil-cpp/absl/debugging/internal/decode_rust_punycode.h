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

#ifndef ABSL_DEBUGGING_INTERNAL_DECODE_RUST_PUNYCODE_H_
#define ABSL_DEBUGGING_INTERNAL_DECODE_RUST_PUNYCODE_H_

#include "absl/base/config.h"
#include "absl/base/nullability.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {

struct DecodeRustPunycodeOptions {
  const char* punycode_begin;
  const char* punycode_end;
  char* out_begin;
  char* out_end;
};

// Given Rust Punycode in `punycode_begin .. punycode_end`, writes the
// corresponding UTF-8 plaintext into `out_begin .. out_end`, followed by a NUL
// character, and returns a pointer to that final NUL on success.  On failure
// returns a null pointer, and the contents of `out_begin .. out_end` are
// unspecified.
//
// Failure occurs in precisely these cases:
//   - Any input byte does not match [0-9a-zA-Z_].
//   - The first input byte is an underscore, but no other underscore appears in
//     the input.
//   - The delta sequence does not represent a valid sequence of code-point
//     insertions.
//   - The plaintext would contain more than 256 code points.
//
// DecodeRustPunycode is async-signal-safe with bounded runtime and a small
// stack footprint, making it suitable for use in demangling Rust symbol names
// from a signal handler.
absl::Nullable<char*> DecodeRustPunycode(DecodeRustPunycodeOptions options);

}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_DEBUGGING_INTERNAL_DECODE_RUST_PUNYCODE_H_
