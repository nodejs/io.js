// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bigint/bigint-internal.h"
#include "src/bigint/vector-arithmetic.h"

namespace v8 {
namespace bigint {

// The classic algorithm: for every part, multiply the accumulator with
// the appropriate multiplier, and add the part. O(n²) overall.
void ProcessorImpl::FromStringClassic(RWDigits Z,
                                      FromStringAccumulator* accumulator) {
  // We always have at least one part to process.
  DCHECK(accumulator->stack_parts_used_ > 0);
  Z[0] = accumulator->stack_parts_[0];
  RWDigits already_set(Z, 0, 1);
  for (int i = 1; i < Z.len(); i++) Z[i] = 0;

  // The {FromStringAccumulator} uses stack-allocated storage for the first
  // few parts; if heap storage is used at all then all parts are copied there.
  int num_stack_parts = accumulator->stack_parts_used_;
  if (num_stack_parts == 1) return;
  const std::vector<digit_t>& heap_parts = accumulator->heap_parts_;
  int num_heap_parts = static_cast<int>(heap_parts.size());
  // All multipliers are the same, except possibly for the last.
  const digit_t max_multiplier = accumulator->max_multiplier_;

  if (num_heap_parts == 0) {
    for (int i = 1; i < num_stack_parts - 1; i++) {
      MultiplySingle(Z, already_set, max_multiplier);
      Add(Z, accumulator->stack_parts_[i]);
      already_set.set_len(already_set.len() + 1);
    }
    MultiplySingle(Z, already_set, accumulator->last_multiplier_);
    Add(Z, accumulator->stack_parts_[num_stack_parts - 1]);
    return;
  }
  // Parts are stored on the heap.
  for (int i = 1; i < num_heap_parts - 1; i++) {
    MultiplySingle(Z, already_set, max_multiplier);
    Add(Z, accumulator->heap_parts_[i]);
    already_set.set_len(already_set.len() + 1);
  }
  MultiplySingle(Z, already_set, accumulator->last_multiplier_);
  Add(Z, accumulator->heap_parts_.back());
}

// The fast algorithm: combine parts in a balanced-binary-tree like order:
// Multiply-and-add neighboring pairs of parts, then loop, until only one
// part is left. The benefit is that the multiplications will have inputs of
// similar sizes, which makes them amenable to fast multiplication algorithms.
// We have to do more multiplications than the classic algorithm though,
// because we also have to multiply the multipliers.
// Optimizations:
// - We can skip the multiplier for the first part, because we never need it.
// - Most multipliers are the same; we can avoid repeated multiplications and
//   just copy the previous result. (In theory we could even de-dupe them, but
//   as the parts/multipliers grow, we'll need most of the memory anyway.)
//   Copied results are marked with a * below.
// - We can re-use memory using a system of three buffers whose usage rotates:
//   - one is considered empty, and is overwritten with the new parts,
//   - one holds the multipliers (and will be "empty" in the next round), and
//   - one initially holds the parts and is overwritten with the new multipliers
//   Parts and multipliers both grow in each iteration, and get fewer, so we
//   use the space of two adjacent old chunks for one new chunk.
//   Since the {heap_parts_} vectors has the right size, and so does the
//   result {Z}, we can use that memory, and only need to allocate one scratch
//   vector. If the final result ends up in the wrong bucket, we have to copy it
//   to the correct one.
// - We don't have to keep track of the positions and sizes of the chunks,
//   because we can deduce their precise placement from the iteration index.
//
// Example, assuming digit_t is 4 bits, fitting one decimal digit:
// Initial state:
// parts_:        1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
// multipliers_: 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10
// After the first iteration of the outer loop:
// parts:         12    34    56    78    90    12    34    5
// multipliers:        100  *100  *100  *100  *100  *100   10
// After the second iteration:
// parts:         1234        5678        9012        345
// multipliers:              10000      *10000       1000
// After the third iteration:
// parts:         12345678                9012345
// multipliers:                          10000000
// And then there's an obvious last iteration.
void ProcessorImpl::FromStringLarge(RWDigits Z,
                                    FromStringAccumulator* accumulator) {
  int num_parts = static_cast<int>(accumulator->heap_parts_.size());
  DCHECK(num_parts >= 2);
  DCHECK(Z.len() >= num_parts);
  RWDigits parts(accumulator->heap_parts_.data(), num_parts);
  Storage multipliers_storage(num_parts);
  RWDigits multipliers(multipliers_storage.get(), num_parts);
  RWDigits temp(Z, 0, num_parts);
  // Unrolled and specialized first iteration: part_len == 1, so instead of
  // Digits sub-vectors we have individual digit_t values, and the multipliers
  // are known up front.
  {
    digit_t max_multiplier = accumulator->max_multiplier_;
    digit_t last_multiplier = accumulator->last_multiplier_;
    RWDigits new_parts = temp;
    RWDigits new_multipliers = parts;
    int i = 0;
    for (; i + 1 < num_parts; i += 2) {
      digit_t p_in = parts[i];
      digit_t p_in2 = parts[i + 1];
      digit_t m_in = max_multiplier;
      digit_t m_in2 = i == num_parts - 2 ? last_multiplier : max_multiplier;
      // p[j] = p[i] * m[i+1] + p[i+1]
      digit_t p_high;
      digit_t p_low = digit_mul(p_in, m_in2, &p_high);
      digit_t carry;
      new_parts[i] = digit_add2(p_low, p_in2, &carry);
      new_parts[i + 1] = p_high + carry;
      // m[j] = m[i] * m[i+1]
      if (i > 0) {
        if (i > 2 && m_in2 != last_multiplier) {
          new_multipliers[i] = new_multipliers[i - 2];
          new_multipliers[i + 1] = new_multipliers[i - 1];
        } else {
          digit_t m_high;
          new_multipliers[i] = digit_mul(m_in, m_in2, &m_high);
          new_multipliers[i + 1] = m_high;
        }
      }
    }
    // Trailing last part (if {num_parts} was odd).
    if (i < num_parts) {
      new_parts[i] = parts[i];
      new_multipliers[i] = last_multiplier;
      i += 2;
    }
    num_parts = i >> 1;
    RWDigits new_temp = multipliers;
    parts = new_parts;
    multipliers = new_multipliers;
    temp = new_temp;
    AddWorkEstimate(num_parts);
  }
  int part_len = 2;

  // Remaining iterations.
  while (num_parts > 1) {
    RWDigits new_parts = temp;
    RWDigits new_multipliers = parts;
    int new_part_len = part_len * 2;
    int i = 0;
    for (; i + 1 < num_parts; i += 2) {
      int start = i * part_len;
      Digits p_in(parts, start, part_len);
      Digits p_in2(parts, start + part_len, part_len);
      Digits m_in(multipliers, start, part_len);
      Digits m_in2(multipliers, start + part_len, part_len);
      RWDigits p_out(new_parts, start, new_part_len);
      RWDigits m_out(new_multipliers, start, new_part_len);
      // p[j] = p[i] * m[i+1] + p[i+1]
      Multiply(p_out, p_in, m_in2);
      if (should_terminate()) return;
      digit_t overflow = AddAndReturnOverflow(p_out, p_in2);
      DCHECK(overflow == 0);
      USE(overflow);
      // m[j] = m[i] * m[i+1]
      if (i > 0) {
        bool copied = false;
        if (i > 2) {
          int prev_start = (i - 2) * part_len;
          Digits m_in_prev(multipliers, prev_start, part_len);
          Digits m_in2_prev(multipliers, prev_start + part_len, part_len);
          if (Compare(m_in, m_in_prev) == 0 &&
              Compare(m_in2, m_in2_prev) == 0) {
            copied = true;
            Digits m_out_prev(new_multipliers, prev_start, new_part_len);
            for (int k = 0; k < new_part_len; k++) m_out[k] = m_out_prev[k];
          }
        }
        if (!copied) {
          Multiply(m_out, m_in, m_in2);
          if (should_terminate()) return;
        }
      }
    }
    // Trailing last part (if {num_parts} was odd).
    if (i < num_parts) {
      Digits p_in(parts, i * part_len, part_len);
      Digits m_in(multipliers, i * part_len, part_len);
      RWDigits p_out(new_parts, i * part_len, new_part_len);
      RWDigits m_out(new_multipliers, i * part_len, new_part_len);
      int k = 0;
      for (; k < p_in.len(); k++) p_out[k] = p_in[k];
      for (; k < p_out.len(); k++) p_out[k] = 0;
      k = 0;
      for (; k < m_in.len(); k++) m_out[k] = m_in[k];
      for (; k < m_out.len(); k++) m_out[k] = 0;
      i += 2;
    }
    num_parts = i >> 1;
    part_len = new_part_len;
    RWDigits new_temp = multipliers;
    parts = new_parts;
    multipliers = new_multipliers;
    temp = new_temp;
  }
  // Copy the result to Z, if it doesn't happen to be there already.
  if (parts.digits() != Z.digits()) {
    int i = 0;
    for (; i < parts.len(); i++) Z[i] = parts[i];
    // Z might be bigger than we requested; be robust towards that.
    for (; i < Z.len(); i++) Z[i] = 0;
  }
}

// Specialized algorithms for power-of-two radixes. Designed to work with
// {ParsePowerTwo}: {max_multiplier_} isn't saved, but {radix_} is, and
// {last_multiplier_} has special meaning, namely the number of unpopulated bits
// in the last part.
// For these radixes, {parts} already is a list of correct bit sequences, we
// just have to put them together in the right way:
// - The parts are currently in reversed order. The highest-index parts[i]
//   will go into Z[0].
// - All parts, possibly except for the last, are maximally populated.
// - A maximally populated part stores a non-fractional number of characters,
//   i.e. the largest fitting multiple of {char_bits} of it is populated.
// - The populated bits in a part are at the low end.
// - The number of unused bits in the last part is stored in
//   {accumulator->last_multiplier_}.
//
// Example: Given the following parts vector, where letters are used to
// label bits, bit order is big endian (i.e. [00000101] encodes "5"),
// 'x' means "unpopulated", kDigitBits == 8, radix == 8, and char_bits == 3:
//
//     parts[0] -> [xxABCDEF][xxGHIJKL][xxMNOPQR][xxxxxSTU] <- parts[3]
//
// We have to assemble the following result:
//
//         Z[0] -> [NOPQRSTU][FGHIJKLM][xxxABCDE] <- Z[2]
//
void ProcessorImpl::FromStringBasePowerOfTwo(
    RWDigits Z, FromStringAccumulator* accumulator) {
  const int num_parts = accumulator->ResultLength();
  DCHECK(num_parts >= 1);
  DCHECK(Z.len() >= num_parts);
  Digits parts(accumulator->heap_parts_.empty()
                   ? accumulator->stack_parts_
                   : accumulator->heap_parts_.data(),
               num_parts);
  uint8_t radix = accumulator->radix_;
  DCHECK(radix == 2 || radix == 4 || radix == 8 || radix == 16 || radix == 32);
  const int char_bits = BitLength(radix - 1);
  const int unused_last_part_bits =
      static_cast<int>(accumulator->last_multiplier_);
  const int unused_part_bits = kDigitBits % char_bits;
  const int max_part_bits = kDigitBits - unused_part_bits;
  int z_index = 0;
  int part_index = num_parts - 1;

  // If the last part is fully populated, then all parts must be, and we can
  // simply copy them (in reversed order).
  if (unused_last_part_bits == 0) {
    DCHECK(kDigitBits % char_bits == 0);
    while (part_index >= 0) {
      Z[z_index++] = parts[part_index--];
    }
    for (; z_index < Z.len(); z_index++) Z[z_index] = 0;
    return;
  }

  // Otherwise we have to shift parts contents around as needed.
  // Holds the next Z digit that we want to store...
  digit_t digit = parts[part_index--];
  // ...and the number of bits (at the right end) we already know.
  int digit_bits = kDigitBits - unused_last_part_bits;
  while (part_index >= 0) {
    // Holds the last part that we read from {parts}...
    digit_t part;
    // ...and the number of bits (at the right end) that we haven't used yet.
    int part_bits;
    while (digit_bits < kDigitBits) {
      part = parts[part_index--];
      part_bits = max_part_bits;
      digit |= part << digit_bits;
      int part_shift = kDigitBits - digit_bits;
      if (part_shift > part_bits) {
        digit_bits += part_bits;
        part = 0;
        part_bits = 0;
        if (part_index < 0) break;
      } else {
        digit_bits = kDigitBits;
        part >>= part_shift;
        part_bits -= part_shift;
      }
    }
    Z[z_index++] = digit;
    digit = part;
    digit_bits = part_bits;
  }
  if (digit_bits > 0) {
    Z[z_index++] = digit;
  }
  for (; z_index < Z.len(); z_index++) Z[z_index] = 0;
}

void ProcessorImpl::FromString(RWDigits Z, FromStringAccumulator* accumulator) {
  if (accumulator->inline_everything_) {
    int i = 0;
    for (; i < accumulator->stack_parts_used_; i++) {
      Z[i] = accumulator->stack_parts_[i];
    }
    for (; i < Z.len(); i++) Z[i] = 0;
  } else if (accumulator->stack_parts_used_ == 0) {
    for (int i = 0; i < Z.len(); i++) Z[i] = 0;
  } else if (IsPowerOfTwo(accumulator->radix_)) {
    FromStringBasePowerOfTwo(Z, accumulator);
  } else if (accumulator->ResultLength() < kFromStringLargeThreshold) {
    FromStringClassic(Z, accumulator);
  } else {
    FromStringLarge(Z, accumulator);
  }
}

Status Processor::FromString(RWDigits Z, FromStringAccumulator* accumulator) {
  ProcessorImpl* impl = static_cast<ProcessorImpl*>(this);
  impl->FromString(Z, accumulator);
  return impl->get_and_clear_status();
}

}  // namespace bigint
}  // namespace v8
