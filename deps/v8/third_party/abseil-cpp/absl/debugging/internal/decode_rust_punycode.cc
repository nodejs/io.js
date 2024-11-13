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

#include "absl/debugging/internal/decode_rust_punycode.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "absl/base/config.h"
#include "absl/base/nullability.h"
#include "absl/debugging/internal/bounded_utf8_length_sequence.h"
#include "absl/debugging/internal/utf8_for_code_point.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {

namespace {

// Decoding Punycode requires repeated random-access insertion into a stream of
// variable-length UTF-8 code-point encodings.  We need this to be tolerably
// fast (no N^2 slowdown for unfortunate inputs), and we can't allocate any data
// structures on the heap (async-signal-safety).
//
// It is pragmatic to impose a moderately low limit on the identifier length and
// bail out if we ever hit it.  Then BoundedUtf8LengthSequence efficiently
// determines where to insert the next code point, and memmove efficiently makes
// room for it.
//
// The chosen limit is a round number several times larger than identifiers
// expected in practice, yet still small enough that a memmove of this many
// UTF-8 characters is not much more expensive than the division and modulus
// operations that Punycode decoding requires.
constexpr uint32_t kMaxChars = 256;

// Constants from RFC 3492 section 5.
constexpr uint32_t kBase = 36, kTMin = 1, kTMax = 26, kSkew = 38, kDamp = 700;

constexpr uint32_t kMaxCodePoint = 0x10ffff;

// Overflow threshold in DecodeRustPunycode's inner loop; see comments there.
constexpr uint32_t kMaxI = 1 << 30;

// If punycode_begin .. punycode_end begins with a prefix matching the regular
// expression [0-9a-zA-Z_]+_, removes that prefix, copies all but the final
// underscore into out_begin .. out_end, sets num_ascii_chars to the number of
// bytes copied, and returns true.  (A prefix of this sort represents the
// nonempty subsequence of ASCII characters in the corresponding plaintext.)
//
// If punycode_begin .. punycode_end does not contain an underscore, sets
// num_ascii_chars to zero and returns true.  (The encoding of a plaintext
// without any ASCII characters does not carry such a prefix.)
//
// Returns false and zeroes num_ascii_chars on failure (either parse error or
// not enough space in the output buffer).
bool ConsumeOptionalAsciiPrefix(const char*& punycode_begin,
                                const char* const punycode_end,
                                char* const out_begin,
                                char* const out_end,
                                uint32_t& num_ascii_chars) {
  num_ascii_chars = 0;

  // Remember the last underscore if any.  Also use the same string scan to
  // reject any ASCII bytes that do not belong in an identifier, including NUL,
  // as well as non-ASCII bytes, which should have been delta-encoded instead.
  int last_underscore = -1;
  for (int i = 0; i < punycode_end - punycode_begin; ++i) {
    const char c = punycode_begin[i];
    if (c == '_') {
      last_underscore = i;
      continue;
    }
    // We write out the meaning of absl::ascii_isalnum rather than call that
    // function because its documentation does not promise it will remain
    // async-signal-safe under future development.
    if ('a' <= c && c <= 'z') continue;
    if ('A' <= c && c <= 'Z') continue;
    if ('0' <= c && c <= '9') continue;
    return false;
  }

  // If there was no underscore, that means there were no ASCII characters in
  // the plaintext, so there is no prefix to consume.  Our work is done.
  if (last_underscore < 0) return true;

  // Otherwise there will be an underscore delimiter somewhere.  It can't be
  // initial because then there would be no ASCII characters to its left, and no
  // delimiter would have been added in that case.
  if (last_underscore == 0) return false;

  // Any other position is reasonable.  Make sure there's room in the buffer.
  if (last_underscore + 1 > out_end - out_begin) return false;

  // Consume and write out the ASCII characters.
  num_ascii_chars = static_cast<uint32_t>(last_underscore);
  std::memcpy(out_begin, punycode_begin, num_ascii_chars);
  out_begin[num_ascii_chars] = '\0';
  punycode_begin += num_ascii_chars + 1;
  return true;
}

// Returns the value of `c` as a base-36 digit according to RFC 3492 section 5,
// or -1 if `c` is not such a digit.
int DigitValue(char c) {
  if ('0' <= c && c <= '9') return c - '0' + 26;
  if ('a' <= c && c <= 'z') return c - 'a';
  if ('A' <= c && c <= 'Z') return c - 'A';
  return -1;
}

// Consumes the next delta encoding from punycode_begin .. punycode_end,
// updating i accordingly.  Returns true on success.  Returns false on parse
// failure or arithmetic overflow.
bool ScanNextDelta(const char*& punycode_begin, const char* const punycode_end,
                   uint32_t bias, uint32_t& i) {
  uint64_t w = 1;  // 64 bits to prevent overflow in w *= kBase - t

  // "for k = base to infinity in steps of base do begin ... end" in RFC 3492
  // section 6.2.  Each loop iteration scans one digit of the delta.
  for (uint32_t k = kBase; punycode_begin != punycode_end; k += kBase) {
    const int digit_value = DigitValue(*punycode_begin++);
    if (digit_value < 0) return false;

    // Compute this in 64-bit arithmetic so we can check for overflow afterward.
    const uint64_t new_i = i + static_cast<uint64_t>(digit_value) * w;

    // Valid deltas are bounded by (#chars already emitted) * kMaxCodePoint, but
    // invalid input could encode an arbitrarily large delta.  Nip that in the
    // bud here.
    static_assert(
        kMaxI >= kMaxChars * kMaxCodePoint,
        "kMaxI is too small to prevent spurious failures on good input");
    if (new_i > kMaxI) return false;

    static_assert(
        kMaxI < (uint64_t{1} << 32),
        "Make kMaxI smaller or i 64 bits wide to prevent silent wraparound");
    i = static_cast<uint32_t>(new_i);

    // Compute the threshold that determines whether this is the last digit and
    // (if not) what the next digit's place value will be.  This logic from RFC
    // 3492 section 6.2 is explained in section 3.3.
    uint32_t t;
    if (k <= bias + kTMin) {
      t = kTMin;
    } else if (k >= bias + kTMax) {
      t = kTMax;
    } else {
      t = k - bias;
    }
    if (static_cast<uint32_t>(digit_value) < t) return true;

    // If this gets too large, the range check on new_i in the next iteration
    // will catch it.  We know this multiplication will not overwrap because w
    // is 64 bits wide.
    w *= kBase - t;
  }
  return false;
}

}  // namespace

absl::Nullable<char*> DecodeRustPunycode(DecodeRustPunycodeOptions options) {
  const char* punycode_begin = options.punycode_begin;
  const char* const punycode_end = options.punycode_end;
  char* const out_begin = options.out_begin;
  char* const out_end = options.out_end;

  // Write a NUL terminator first.  Later memcpy calls will keep bumping it
  // along to its new right place.
  const size_t out_size = static_cast<size_t>(out_end - out_begin);
  if (out_size == 0) return nullptr;
  *out_begin = '\0';

  // RFC 3492 section 6.2 begins here.  We retain the names of integer variables
  // appearing in that text.
  uint32_t n = 128, i = 0, bias = 72, num_chars = 0;

  // If there are any ASCII characters, consume them and their trailing
  // underscore delimiter.
  if (!ConsumeOptionalAsciiPrefix(punycode_begin, punycode_end,
                                  out_begin, out_end, num_chars)) {
    return nullptr;
  }
  uint32_t total_utf8_bytes = num_chars;

  BoundedUtf8LengthSequence<kMaxChars> utf8_lengths;

  // "while the input is not exhausted do begin ... end"
  while (punycode_begin != punycode_end) {
    if (num_chars >= kMaxChars) return nullptr;

    const uint32_t old_i = i;

    if (!ScanNextDelta(punycode_begin, punycode_end, bias, i)) return nullptr;

    // Update bias as in RFC 3492 section 6.1.  (We have inlined adapt.)
    uint32_t delta = i - old_i;
    delta /= (old_i == 0 ? kDamp : 2);
    delta += delta/(num_chars + 1);
    bias = 0;
    while (delta > ((kBase - kTMin) * kTMax)/2) {
      delta /= kBase - kTMin;
      bias += kBase;
    }
    bias += ((kBase - kTMin + 1) * delta)/(delta + kSkew);

    // Back in section 6.2, compute the new code point and insertion index.
    static_assert(
        kMaxI + kMaxCodePoint < (uint64_t{1} << 32),
        "Make kMaxI smaller or n 64 bits wide to prevent silent wraparound");
    n += i/(num_chars + 1);
    i %= num_chars + 1;

    // To actually insert, we need to convert the code point n to UTF-8 and the
    // character index i to an index into the byte stream emitted so far.  First
    // prepare the UTF-8 encoding for n, rejecting surrogates, overlarge values,
    // and anything that won't fit into the remaining output storage.
    Utf8ForCodePoint utf8_for_code_point(n);
    if (!utf8_for_code_point.ok()) return nullptr;
    if (total_utf8_bytes + utf8_for_code_point.length + 1 > out_size) {
      return nullptr;
    }

    // Now insert the new character into both our length map and the output.
    uint32_t n_index =
        utf8_lengths.InsertAndReturnSumOfPredecessors(
            i, utf8_for_code_point.length);
    std::memmove(
        out_begin + n_index + utf8_for_code_point.length, out_begin + n_index,
        total_utf8_bytes + 1 - n_index);
    std::memcpy(out_begin + n_index, utf8_for_code_point.bytes,
                utf8_for_code_point.length);
    total_utf8_bytes += utf8_for_code_point.length;
    ++num_chars;

    // Finally, advance to the next state before continuing.
    ++i;
  }

  return out_begin + total_utf8_bytes;
}

}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl
