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

#ifndef ABSL_DEBUGGING_INTERNAL_BOUNDED_UTF8_LENGTH_SEQUENCE_H_
#define ABSL_DEBUGGING_INTERNAL_BOUNDED_UTF8_LENGTH_SEQUENCE_H_

#include <cstdint>

#include "absl/base/config.h"
#include "absl/numeric/bits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {

// A sequence of up to max_elements integers between 1 and 4 inclusive, whose
// insertion operation computes the sum of all the elements before the insertion
// point.  This is useful in decoding Punycode, where one needs to know where in
// a UTF-8 byte stream the n-th code point begins.
//
// BoundedUtf8LengthSequence is async-signal-safe and suitable for use in
// symbolizing stack traces in a signal handler, provided max_elements is not
// improvidently large.  For inputs of lengths accepted by the Rust demangler,
// up to a couple hundred code points, InsertAndReturnSumOfPredecessors should
// run in a few dozen clock cycles, on par with the other arithmetic required
// for Punycode decoding.
template <uint32_t max_elements>
class BoundedUtf8LengthSequence {
 public:
  // Constructs an empty sequence.
  BoundedUtf8LengthSequence() = default;

  // Inserts `utf_length` at position `index`, shifting any existing elements at
  // or beyond `index` one position to the right.  If the sequence is already
  // full, the rightmost element is discarded.
  //
  // Returns the sum of the elements at positions 0 to `index - 1` inclusive.
  // If `index` is greater than the number of elements already inserted, the
  // excess positions in the range count 1 apiece.
  //
  // REQUIRES: index < max_elements and 1 <= utf8_length <= 4.
  uint32_t InsertAndReturnSumOfPredecessors(
      uint32_t index, uint32_t utf8_length) {
    // The caller shouldn't pass out-of-bounds inputs, but if it does happen,
    // clamp the values and try to continue.  If we're being called from a
    // signal handler, the last thing we want to do is crash.  Emitting
    // malformed UTF-8 is a lesser evil.
    if (index >= max_elements) index = max_elements - 1;
    if (utf8_length == 0 || utf8_length > 4) utf8_length = 1;

    const uint32_t word_index = index/32;
    const uint32_t bit_index = 2 * (index % 32);
    const uint64_t ones_bit = uint64_t{1} << bit_index;

    // Compute the sum of predecessors.
    //   - Each value from 1 to 4 is represented by a bit field with value from
    //     0 to 3, so the desired sum is index plus the sum of the
    //     representations actually stored.
    //   - For each bit field, a set low bit should contribute 1 to the sum, and
    //     a set high bit should contribute 2.
    //   - Another way to say the same thing is that each set bit contributes 1,
    //     and each set high bit contributes an additional 1.
    //   - So the sum we want is index + popcount(everything) + popcount(bits in
    //     odd positions).
    const uint64_t odd_bits_mask = 0xaaaaaaaaaaaaaaaa;
    const uint64_t lower_seminibbles_mask = ones_bit - 1;
    const uint64_t higher_seminibbles_mask = ~lower_seminibbles_mask;
    const uint64_t same_word_bits_below_insertion =
        rep_[word_index] & lower_seminibbles_mask;
    int full_popcount = absl::popcount(same_word_bits_below_insertion);
    int odd_popcount =
        absl::popcount(same_word_bits_below_insertion & odd_bits_mask);
    for (uint32_t j = word_index; j > 0; --j) {
      const uint64_t word_below_insertion = rep_[j - 1];
      full_popcount += absl::popcount(word_below_insertion);
      odd_popcount += absl::popcount(word_below_insertion & odd_bits_mask);
    }
    const uint32_t sum_of_predecessors =
        index + static_cast<uint32_t>(full_popcount + odd_popcount);

    // Now insert utf8_length's representation, shifting successors up one
    // place.
    for (uint32_t j = max_elements/32 - 1; j > word_index; --j) {
      rep_[j] = (rep_[j] << 2) | (rep_[j - 1] >> 62);
    }
    rep_[word_index] =
        (rep_[word_index] & lower_seminibbles_mask) |
        (uint64_t{utf8_length - 1} << bit_index) |
        ((rep_[word_index] & higher_seminibbles_mask) << 2);

    return sum_of_predecessors;
  }

 private:
  // If the (32 * i + j)-th element of the represented sequence has the value k
  // (0 <= j < 32, 1 <= k <= 4), then bits 2 * j and 2 * j + 1 of rep_[i]
  // contain the seminibble (k - 1).
  //
  // In particular, the zero-initialization of rep_ makes positions not holding
  // any inserted element count as 1 in InsertAndReturnSumOfPredecessors.
  //
  // Example: rep_ = {0xb1, ... the rest zeroes ...} represents the sequence
  // (2, 1, 4, 3, ... the rest 1's ...).  Constructing the sequence of Unicode
  // code points "Àa🂻中" = {U+00C0, U+0061, U+1F0BB, U+4E2D} (among many
  // other examples) would yield this value of rep_.
  static_assert(max_elements > 0 && max_elements % 32 == 0,
                "max_elements must be a positive multiple of 32");
  uint64_t rep_[max_elements/32] = {};
};

}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_DEBUGGING_INTERNAL_BOUNDED_UTF8_LENGTH_SEQUENCE_H_
