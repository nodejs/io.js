// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// FFT-based multiplication, due to Schönhage and Strassen.
// This implementation mostly follows the description given in:
// Christoph Lüders: Fast Multiplication of Large Integers,
// http://arxiv.org/abs/1503.04955

#include "src/bigint/bigint-internal.h"
#include "src/bigint/digit-arithmetic.h"
#include "src/bigint/util.h"

namespace v8 {
namespace bigint {

namespace {

////////////////////////////////////////////////////////////////////////////////
// Part 1: Functions for "mod F_n" arithmetic.
// F_n is of the shape 2^K + 1, and for convenience we use K to count the
// number of digits rather than the number of bits, so F_n (or K) are implicit
// and deduced from the length {len} of the digits array.

// Helper function for {ModFn} below.
void ModFn_Helper(digit_t* x, int len, signed_digit_t high) {
  if (high > 0) {
    digit_t borrow = high;
    x[len - 1] = 0;
    for (int i = 0; i < len; i++) {
      x[i] = digit_sub(x[i], borrow, &borrow);
      if (borrow == 0) break;
    }
  } else {
    digit_t carry = -high;
    x[len - 1] = 0;
    for (int i = 0; i < len; i++) {
      x[i] = digit_add2(x[i], carry, &carry);
      if (carry == 0) break;
    }
  }
}

// {x} := {x} mod F_n, assuming that {x} is "slightly" larger than F_n (e.g.
// after addition of two numbers that were mod-F_n-normalized before).
void ModFn(digit_t* x, int len) {
  int K = len - 1;
  signed_digit_t high = x[K];
  if (high == 0) return;
  ModFn_Helper(x, len, high);
  high = x[K];
  if (high == 0) return;
  DCHECK(high == 1 || high == -1);
  ModFn_Helper(x, len, high);
  high = x[K];
  if (high == -1) ModFn_Helper(x, len, high);
}

// {dest} := {src} mod F_n, assuming that {src} is about twice as long as F_n
// (e.g. after multiplication of two numbers that were mod-F_n-normalized
// before).
// {len} is length of {dest}; {src} is twice as long.
void ModFnDoubleWidth(digit_t* dest, const digit_t* src, int len) {
  int K = len - 1;
  digit_t borrow = 0;
  for (int i = 0; i < K; i++) {
    dest[i] = digit_sub2(src[i], src[i + K], borrow, &borrow);
  }
  dest[K] = digit_sub2(0, src[2 * K], borrow, &borrow);
  // {borrow} may be non-zero here, that's OK as {ModFn} will take care of it.
  ModFn(dest, len);
}

// Sets {sum} := {a} + {b} and {diff} := {a} - {b}, which is more efficient
// than computing sum and difference separately. Applies "mod F_n" normalization
// to both results.
void SumDiff(digit_t* sum, digit_t* diff, const digit_t* a, const digit_t* b,
             int len) {
  digit_t carry = 0;
  digit_t borrow = 0;
  for (int i = 0; i < len; i++) {
    // Read both values first, because inputs and outputs can overlap.
    digit_t ai = a[i];
    digit_t bi = b[i];
    sum[i] = digit_add3(ai, bi, carry, &carry);
    diff[i] = digit_sub2(ai, bi, borrow, &borrow);
  }
  ModFn(sum, len);
  ModFn(diff, len);
}

// {result} := ({input} << shift) mod F_n, where shift >= K.
void ShiftModFn_Large(digit_t* result, const digit_t* input, int digit_shift,
                      int bits_shift, int K) {
  // If {digit_shift} is greater than K, we use the following transformation
  // (where, since everything is mod 2^K + 1, we are allowed to add or
  // subtract any multiple of 2^K + 1 at any time):
  //      x * 2^{K+m}   mod 2^K + 1
  //   == x * 2^K * 2^m - (2^K + 1)*(x * 2^m)   mod 2^K + 1
  //   == x * 2^K * 2^m - x * 2^K * 2^m - x * 2^m   mod 2^K + 1
  //   == -x * 2^m   mod 2^K + 1
  // So the flow is the same as for m < K, but we invert the subtraction's
  // operands. In order to avoid underflow, we virtually initialize the
  // result to 2^K + 1:
  //   input  =  [ iK ][iK-1] ....  .... [ i1 ][ i0 ]
  //   result =  [   1][0000] ....  .... [0000][0001]
  //            +                  [ iK ] .... [ iX ]
  //            -      [iX-1] .... [ i0 ]
  DCHECK(digit_shift >= K);
  digit_shift -= K;
  digit_t borrow = 0;
  if (bits_shift == 0) {
    digit_t carry = 1;
    for (int i = 0; i < digit_shift; i++) {
      result[i] = digit_add2(input[i + K - digit_shift], carry, &carry);
    }
    result[digit_shift] = digit_sub(input[K] + carry, input[0], &borrow);
    for (int i = digit_shift + 1; i < K; i++) {
      digit_t d = input[i - digit_shift];
      result[i] = digit_sub2(0, d, borrow, &borrow);
    }
  } else {
    digit_t add_carry = 1;
    digit_t input_carry =
        input[K - digit_shift - 1] >> (kDigitBits - bits_shift);
    for (int i = 0; i < digit_shift; i++) {
      digit_t d = input[i + K - digit_shift];
      digit_t summand = (d << bits_shift) | input_carry;
      result[i] = digit_add2(summand, add_carry, &add_carry);
      input_carry = d >> (kDigitBits - bits_shift);
    }
    {
      // result[digit_shift] = (add_carry + iK_part) - i0_part
      digit_t d = input[K];
      digit_t iK_part = (d << bits_shift) | input_carry;
      digit_t iK_carry = d >> (kDigitBits - bits_shift);
      digit_t sum = digit_add2(add_carry, iK_part, &add_carry);
      // {iK_carry} is less than a full digit, so we can merge {add_carry}
      // into it without overflow.
      iK_carry += add_carry;
      d = input[0];
      digit_t i0_part = d << bits_shift;
      result[digit_shift] = digit_sub(sum, i0_part, &borrow);
      input_carry = d >> (kDigitBits - bits_shift);
      if (digit_shift + 1 < K) {
        d = input[1];
        digit_t subtrahend = (d << bits_shift) | input_carry;
        result[digit_shift + 1] =
            digit_sub2(iK_carry, subtrahend, borrow, &borrow);
        input_carry = d >> (kDigitBits - bits_shift);
      }
    }
    for (int i = digit_shift + 2; i < K; i++) {
      digit_t d = input[i - digit_shift];
      digit_t subtrahend = (d << bits_shift) | input_carry;
      result[i] = digit_sub2(0, subtrahend, borrow, &borrow);
      input_carry = d >> (kDigitBits - bits_shift);
    }
  }
  // The virtual 1 in result[K] should be eliminated by {borrow}. If there
  // is no borrow, then the virtual initialization was too much. Subtract
  // 2^K + 1.
  result[K] = 0;
  if (borrow != 1) {
    borrow = 1;
    for (int i = 0; i < K; i++) {
      result[i] = digit_sub(result[i], borrow, &borrow);
      if (borrow == 0) break;
    }
    if (borrow != 0) {
      // The result must be 2^K.
      for (int i = 0; i < K; i++) result[i] = 0;
      result[K] = 1;
    }
  }
}

// Sets {result} := {input} * 2^{power_of_two} mod 2^{K} + 1.
// This function is highly relevant for overall performance.
void ShiftModFn(digit_t* result, const digit_t* input, int power_of_two, int K,
                int zero_above = 0x7FFFFFFF) {
  // The modulo-reduction amounts to a subtraction, which we combine
  // with the shift as follows:
  //   input  =  [ iK ][iK-1] ....  .... [ i1 ][ i0 ]
  //   result =        [iX-1] .... [ i0 ] <---------- shift by {power_of_two}
  //            -                  [ iK ] .... [ iX ]
  // where "X" is the index "K - digit_shift".
  int digit_shift = power_of_two / kDigitBits;
  int bits_shift = power_of_two % kDigitBits;
  // By an analogous construction to the "digit_shift >= K" case,
  // it turns out that:
  //    x * 2^{2K+m} == x * 2^m   mod 2^K + 1.
  while (digit_shift >= 2 * K) digit_shift -= 2 * K;  // Faster than '%'!
  if (digit_shift >= K) {
    return ShiftModFn_Large(result, input, digit_shift, bits_shift, K);
  }
  digit_t borrow = 0;
  if (bits_shift == 0) {
    // We do a single pass over {input}, starting by copying digits [i1] to
    // [iX-1] to result indices digit_shift+1 to K-1.
    int i = 1;
    // Read input digits unless we know they are zero.
    int cap = std::min(K - digit_shift, zero_above);
    for (; i < cap; i++) {
      result[i + digit_shift] = input[i];
    }
    // Any remaining work can hard-code the knowledge that input[i] == 0.
    for (; i < K - digit_shift; i++) {
      DCHECK(input[i] == 0);
      result[i + digit_shift] = 0;
    }
    // Second phase: subtract input digits [iX] to [iK] from (virtually) zero-
    // initialized result indices 0 to digit_shift-1.
    cap = std::min(K, zero_above);
    for (; i < cap; i++) {
      digit_t d = input[i];
      result[i - K + digit_shift] = digit_sub2(0, d, borrow, &borrow);
    }
    // Any remaining work can hard-code the knowledge that input[i] == 0.
    for (; i < K; i++) {
      DCHECK(input[i] == 0);
      result[i - K + digit_shift] = digit_sub(0, borrow, &borrow);
    }
    // Last step: subtract [iK] from [i0] and store at result index digit_shift.
    result[digit_shift] = digit_sub2(input[0], input[K], borrow, &borrow);
  } else {
    // Same flow as before, but taking bits_shift != 0 into account.
    // First phase: result indices digit_shift+1 to K.
    digit_t carry = 0;
    int i = 0;
    // Read input digits unless we know they are zero.
    int cap = std::min(K - digit_shift, zero_above);
    for (; i < cap; i++) {
      digit_t d = input[i];
      result[i + digit_shift] = (d << bits_shift) | carry;
      carry = d >> (kDigitBits - bits_shift);
    }
    // Any remaining work can hard-code the knowledge that input[i] == 0.
    for (; i < K - digit_shift; i++) {
      DCHECK(input[i] == 0);
      result[i + digit_shift] = carry;
      carry = 0;
    }
    // Second phase: result indices 0 to digit_shift - 1.
    cap = std::min(K, zero_above);
    for (; i < cap; i++) {
      digit_t d = input[i];
      result[i - K + digit_shift] =
          digit_sub2(0, (d << bits_shift) | carry, borrow, &borrow);
      carry = d >> (kDigitBits - bits_shift);
    }
    // Any remaining work can hard-code the knowledge that input[i] == 0.
    if (i < K) {
      DCHECK(input[i] == 0);
      result[i - K + digit_shift] = digit_sub2(0, carry, borrow, &borrow);
      carry = 0;
      i++;
    }
    for (; i < K; i++) {
      DCHECK(input[i] == 0);
      result[i - K + digit_shift] = digit_sub(0, borrow, &borrow);
    }
    // Last step: compute result[digit_shift].
    digit_t d = input[K];
    result[digit_shift] = digit_sub2(
        result[digit_shift], (d << bits_shift) | carry, borrow, &borrow);
    // No carry left.
    DCHECK((d >> (kDigitBits - bits_shift)) == 0);
  }
  result[K] = 0;
  for (int i = digit_shift + 1; i <= K && borrow > 0; i++) {
    result[i] = digit_sub(result[i], borrow, &borrow);
  }
  if (borrow > 0) {
    // Underflow means we subtracted too much. Add 2^K + 1.
    digit_t carry = 1;
    for (int i = 0; i <= K; i++) {
      result[i] = digit_add2(result[i], carry, &carry);
      if (carry == 0) break;
    }
    result[K] = digit_add2(result[K], 1, &carry);
  }
}

////////////////////////////////////////////////////////////////////////////////
// Part 2: FFT-based multiplication is very sensitive to appropriate choice
// of parameters. The following functions choose the parameters that the
// subsequent actual computation will use. This is partially based on formal
// constraints and partially on experimentally-determined heuristics.

struct Parameters {
  // We never use the default values, but skipping zero-initialization
  // of these fields saddens and confuses MSan.
  int m{0};
  int K{0};
  int n{0};
  int s{0};
  int r{0};
};

// Computes parameters for the main calculation, given a bit length {N} and
// an {m}. See the paper for details.
void ComputeParameters(int N, int m, Parameters* params) {
  N *= kDigitBits;
  int n = 1 << m;  // 2^m
  int nhalf = n >> 1;
  int s = (N + n - 1) >> m;  // ceil(N/n)
  s = RoundUp(s, kDigitBits);
  int K = m + 2 * s + 1;  // K must be at least this big...
  K = RoundUp(K, nhalf);  // ...and a multiple of n/2.
  int r = K >> (m - 1);   // Which multiple?

  // We want recursive calls to make progress, so force K to be a multiple
  // of 8 if it's above the recursion threshold. Otherwise, K must be a
  // multiple of kDigitBits.
  const int threshold = (K + 1 >= kFftInnerThreshold * kDigitBits)
                            ? 3 + kLog2DigitBits
                            : kLog2DigitBits;
  int K_tz = CountTrailingZeros(K);
  while (K_tz < threshold) {
    K += (1 << K_tz);
    r = K >> (m - 1);
    K_tz = CountTrailingZeros(K);
  }

  DCHECK(K % kDigitBits == 0);
  DCHECK(s % kDigitBits == 0);
  params->K = K / kDigitBits;
  params->s = s / kDigitBits;
  params->n = n;
  params->r = r;
}

// Computes parameters for recursive invocations ("inner layer").
void ComputeParameters_Inner(int N, Parameters* params) {
  int max_m = CountTrailingZeros(N);
  int N_bits = BitLength(N);
  int m = N_bits - 4;  // Don't let s get too small.
  m = std::min(max_m, m);
  N *= kDigitBits;
  int n = 1 << m;  // 2^m
  // We can't round up s in the inner layer, because N = n*s is fixed.
  int s = N >> m;
  DCHECK(N == s * n);
  int K = m + 2 * s + 1;  // K must be at least this big...
  K = RoundUp(K, n);      // ...and a multiple of n and kDigitBits.
  K = RoundUp(K, kDigitBits);
  params->r = K >> m;           // Which multiple?
  DCHECK(K % kDigitBits == 0);
  DCHECK(s % kDigitBits == 0);
  params->K = K / kDigitBits;
  params->s = s / kDigitBits;
  params->n = n;
  params->m = m;
}

int PredictInnerK(int N) {
  Parameters params;
  ComputeParameters_Inner(N, &params);
  return params.K;
}

// Applies heuristics to decide whether {m} should be decremented, by looking
// at what would happen to {K} and {s} if {m} was decremented.
bool ShouldDecrementM(const Parameters& current, const Parameters& next,
                      const Parameters& after_next) {
  // K == 64 seems to work particularly well.
  if (current.K == 64 && next.K >= 112) return false;
  // Small values for s are never efficient.
  if (current.s < 6) return true;
  // The time is roughly determined by K * n. When we decrement m, then
  // n always halves, and K usually gets bigger, by up to 2x.
  // For not-quite-so-small s, look at how much bigger K would get: if
  // the K increase is small enough, making n smaller is worth it.
  // Empirically, it's most meaningful to look at the K *after* next.
  // The specific threshold values have been chosen by running many
  // benchmarks on inputs of many sizes, and manually selecting thresholds
  // that seemed to produce good results.
  double factor = static_cast<double>(after_next.K) / current.K;
  if ((current.s == 6 && factor < 3.85) ||  // --
      (current.s == 7 && factor < 3.73) ||  // --
      (current.s == 8 && factor < 3.55) ||  // --
      (current.s == 9 && factor < 3.50) ||  // --
      factor < 3.4) {
    return true;
  }
  // If K is just below the recursion threshold, make sure we do recurse,
  // unless doing so would be particularly inefficient (large inner_K).
  // If K is just above the recursion threshold, doubling it often makes
  // the inner call more efficient.
  if (current.K >= 160 && current.K < 250 && PredictInnerK(next.K) < 28) {
    return true;
  }
  // If we found no reason to decrement, keep m as large as possible.
  return false;
}

// Decides what parameters to use for a given input bit length {N}.
// Returns the chosen m.
int GetParameters(int N, Parameters* params) {
  int N_bits = BitLength(N);
  int max_m = N_bits - 3;                   // Larger m make s too small.
  max_m = std::max(kLog2DigitBits, max_m);  // Smaller m break the logic below.
  int m = max_m;
  Parameters current;
  ComputeParameters(N, m, &current);
  Parameters next;
  ComputeParameters(N, m - 1, &next);
  while (m > 2) {
    Parameters after_next;
    ComputeParameters(N, m - 2, &after_next);
    if (ShouldDecrementM(current, next, after_next)) {
      m--;
      current = next;
      next = after_next;
    } else {
      break;
    }
  }
  *params = current;
  return m;
}

////////////////////////////////////////////////////////////////////////////////
// Part 3: Fast Fourier Transformation.

class FFTContainer {
 public:
  // {n} is the number of chunks, whose length is {K}+1.
  // {K} determines F_n = 2^(K * kDigitBits) + 1.
  FFTContainer(int n, int K, ProcessorImpl* processor)
      : n_(n), K_(K), length_(K + 1), processor_(processor) {
    storage_ = new digit_t[length_ * n_];
    part_ = new digit_t*[n_];
    digit_t* ptr = storage_;
    for (int i = 0; i < n; i++, ptr += length_) {
      part_[i] = ptr;
    }
    temp_ = new digit_t[length_ * 2];
  }
  FFTContainer() = delete;
  FFTContainer(const FFTContainer&) = delete;
  FFTContainer& operator=(const FFTContainer&) = delete;

  ~FFTContainer() {
    delete[] storage_;
    delete[] part_;
    delete[] temp_;
  }

  void Start_Default(Digits X, int chunk_size, int theta, int omega);
  void Start(Digits X, int chunk_size, int theta, int omega);

  void NormalizeAndRecombine(int omega, int m, RWDigits Z, int chunk_size);
  void CounterWeightAndRecombine(int theta, int m, RWDigits Z, int chunk_size);

  void FFT_ReturnShuffledThreadsafe(int start, int len, int omega,
                                    digit_t* temp);
  void FFT_Recurse(int start, int half, int omega, digit_t* temp);

  void BackwardFFT(int start, int len, int omega);
  void BackwardFFT_Threadsafe(int start, int len, int omega, digit_t* temp);

  void PointwiseMultiply(const FFTContainer& other);
  void DoPointwiseMultiplication(const FFTContainer& other, int start, int end,
                                 digit_t* temp);

  int length() const { return length_; }

 private:
  const int n_;       // Number of parts.
  const int K_;       // Always length_ - 1.
  const int length_;  // Length of each part, in digits.
  ProcessorImpl* processor_;
  digit_t* storage_;  // Combined storage of all parts.
  digit_t** part_;    // Pointers to each part.
  digit_t* temp_;     // Temporary storage with size 2 * length_.
};

inline void CopyAndZeroExtend(digit_t* dst, const digit_t* src,
                              int digits_to_copy, size_t total_bytes) {
  size_t bytes_to_copy = digits_to_copy * sizeof(digit_t);
  memcpy(dst, static_cast<const void*>(src), bytes_to_copy);
  memset(dst + digits_to_copy, 0, total_bytes - bytes_to_copy);
}

// Reads {X} into the FFTContainer's internal storage, dividing it into chunks
// while doing so; then performs the forward FFT.
void FFTContainer::Start_Default(Digits X, int chunk_size, int theta,
                                 int omega) {
  int len = X.len();
  const digit_t* pointer = X.digits();
  const size_t part_length_in_bytes = length_ * sizeof(digit_t);
  int current_theta = 0;
  int i = 0;
  for (; i < n_ && len > 0; i++, current_theta += theta) {
    chunk_size = std::min(chunk_size, len);
    // For invocations via MultiplyFFT_Inner, X.len() == n_ * chunk_size + 1,
    // because the outer layer's "K" is passed as the inner layer's "N".
    // Since X is (mod Fn)-normalized on the outer layer, there is the rare
    // corner case where X[n_ * chunk_size] == 1. Detect that case, and handle
    // the extra bit as part of the last chunk; we always have the space.
    if (i == n_ - 1 && len == chunk_size + 1) {
      DCHECK(X[n_ * chunk_size] <= 1);
      DCHECK(length_ >= chunk_size + 1);
      chunk_size++;
    }
    if (current_theta != 0) {
      // Multiply with theta^i, and reduce modulo 2^K + 1.
      // We pass theta as a shift amount; it really means 2^theta.
      CopyAndZeroExtend(temp_, pointer, chunk_size, part_length_in_bytes);
      ShiftModFn(part_[i], temp_, current_theta, K_, chunk_size);
    } else {
      CopyAndZeroExtend(part_[i], pointer, chunk_size, part_length_in_bytes);
    }
    pointer += chunk_size;
    len -= chunk_size;
  }
  DCHECK(len == 0);
  for (; i < n_; i++) {
    memset(part_[i], 0, part_length_in_bytes);
  }
  FFT_ReturnShuffledThreadsafe(0, n_, omega, temp_);
}

// This version of Start is optimized for the case where ~half of the
// container will be filled with padding zeros.
void FFTContainer::Start(Digits X, int chunk_size, int theta, int omega) {
  int len = X.len();
  if (len > n_ * chunk_size / 2) {
    return Start_Default(X, chunk_size, theta, omega);
  }
  DCHECK(theta == 0);
  const digit_t* pointer = X.digits();
  const size_t part_length_in_bytes = length_ * sizeof(digit_t);
  int nhalf = n_ / 2;
  // Unrolled first iteration.
  CopyAndZeroExtend(part_[0], pointer, chunk_size, part_length_in_bytes);
  CopyAndZeroExtend(part_[nhalf], pointer, chunk_size, part_length_in_bytes);
  pointer += chunk_size;
  len -= chunk_size;
  int i = 1;
  for (; i < nhalf && len > 0; i++) {
    chunk_size = std::min(chunk_size, len);
    CopyAndZeroExtend(part_[i], pointer, chunk_size, part_length_in_bytes);
    int w = omega * i;
    ShiftModFn(part_[i + nhalf], part_[i], w, K_, chunk_size);
    pointer += chunk_size;
    len -= chunk_size;
  }
  for (; i < nhalf; i++) {
    memset(part_[i], 0, part_length_in_bytes);
    memset(part_[i + nhalf], 0, part_length_in_bytes);
  }
  FFT_Recurse(0, nhalf, omega, temp_);
}

// Forward transformation.
// We use the "DIF" aka "decimation in frequency" transform, because it
// leaves the result in "bit reversed" order, which is precisely what we
// need as input for the "DIT" aka "decimation in time" backwards transform.
void FFTContainer::FFT_ReturnShuffledThreadsafe(int start, int len, int omega,
                                                digit_t* temp) {
  DCHECK((len & 1) == 0);  // {len} must be even.
  int half = len / 2;
  SumDiff(part_[start], part_[start + half], part_[start], part_[start + half],
          length_);
  for (int k = 1; k < half; k++) {
    SumDiff(part_[start + k], temp, part_[start + k], part_[start + half + k],
            length_);
    int w = omega * k;
    ShiftModFn(part_[start + half + k], temp, w, K_);
  }
  FFT_Recurse(start, half, omega, temp);
}

// Recursive step of the above, factored out for additional callers.
void FFTContainer::FFT_Recurse(int start, int half, int omega, digit_t* temp) {
  if (half > 1) {
    FFT_ReturnShuffledThreadsafe(start, half, 2 * omega, temp);
    FFT_ReturnShuffledThreadsafe(start + half, half, 2 * omega, temp);
  }
}

// Backward transformation.
// We use the "DIT" aka "decimation in time" transform here, because it
// turns bit-reversed input into normally sorted output.
void FFTContainer::BackwardFFT(int start, int len, int omega) {
  BackwardFFT_Threadsafe(start, len, omega, temp_);
}

void FFTContainer::BackwardFFT_Threadsafe(int start, int len, int omega,
                                          digit_t* temp) {
  DCHECK((len & 1) == 0);  // {len} must be even.
  int half = len / 2;
  // Don't recurse for half == 2, as PointwiseMultiply already performed
  // the first level of the backwards FFT.
  if (half > 2) {
    BackwardFFT_Threadsafe(start, half, 2 * omega, temp);
    BackwardFFT_Threadsafe(start + half, half, 2 * omega, temp);
  }
  SumDiff(part_[start], part_[start + half], part_[start], part_[start + half],
          length_);
  for (int k = 1; k < half; k++) {
    int w = omega * (len - k);
    ShiftModFn(temp, part_[start + half + k], w, K_);
    SumDiff(part_[start + k], part_[start + half + k], part_[start + k], temp,
            length_);
  }
}

// Recombines the result's parts into {Z}, after backwards FFT.
void FFTContainer::NormalizeAndRecombine(int omega, int m, RWDigits Z,
                                         int chunk_size) {
  Z.Clear();
  int z_index = 0;
  const int shift = n_ * omega - m;
  for (int i = 0; i < n_; i++, z_index += chunk_size) {
    digit_t* part = part_[i];
    ShiftModFn(temp_, part, shift, K_);
    digit_t carry = 0;
    int zi = z_index;
    int j = 0;
    for (; j < length_ && zi < Z.len(); j++, zi++) {
      Z[zi] = digit_add3(Z[zi], temp_[j], carry, &carry);
    }
    for (; j < length_; j++) {
      DCHECK(temp_[j] == 0);
    }
    if (carry != 0) {
      DCHECK(zi < Z.len());
      Z[zi] = carry;
    }
  }
}

// Helper function for {CounterWeightAndRecombine} below.
bool ShouldBeNegative(const digit_t* x, int xlen, digit_t threshold, int s) {
  if (x[2 * s] >= threshold) return true;
  for (int i = 2 * s + 1; i < xlen; i++) {
    if (x[i] > 0) return true;
  }
  return false;
}

// Same as {NormalizeAndRecombine} above, but for the needs of the recursive
// invocation ("inner layer") of FFT multiplication, where an additional
// counter-weighting step is required.
void FFTContainer::CounterWeightAndRecombine(int theta, int m, RWDigits Z,
                                             int s) {
  Z.Clear();
  int z_index = 0;
  for (int k = 0; k < n_; k++, z_index += s) {
    int shift = -theta * k - m;
    if (shift < 0) shift += 2 * n_ * theta;
    DCHECK(shift >= 0);
    digit_t* input = part_[k];
    ShiftModFn(temp_, input, shift, K_);
    int remaining_z = Z.len() - z_index;
    if (ShouldBeNegative(temp_, length_, k + 1, s)) {
      // Subtract F_n from input before adding to result. We use the following
      // transformation (knowing that X < F_n):
      // Z + (X - F_n) == Z - (F_n - X)
      digit_t borrow_z = 0;
      digit_t borrow_Fn = 0;
      {
        // i == 0:
        digit_t d = digit_sub(1, temp_[0], &borrow_Fn);
        Z[z_index] = digit_sub(Z[z_index], d, &borrow_z);
      }
      int i = 1;
      for (; i < K_ && i < remaining_z; i++) {
        digit_t d = digit_sub2(0, temp_[i], borrow_Fn, &borrow_Fn);
        Z[z_index + i] = digit_sub2(Z[z_index + i], d, borrow_z, &borrow_z);
      }
      DCHECK(i == K_ && K_ == length_ - 1);
      for (; i < length_ && i < remaining_z; i++) {
        digit_t d = digit_sub2(1, temp_[i], borrow_Fn, &borrow_Fn);
        Z[z_index + i] = digit_sub2(Z[z_index + i], d, borrow_z, &borrow_z);
      }
      DCHECK(borrow_Fn == 0);
      for (; borrow_z > 0 && i < remaining_z; i++) {
        Z[z_index + i] = digit_sub(Z[z_index + i], borrow_z, &borrow_z);
      }
    } else {
      digit_t carry = 0;
      int i = 0;
      for (; i < length_ && i < remaining_z; i++) {
        Z[z_index + i] = digit_add3(Z[z_index + i], temp_[i], carry, &carry);
      }
      for (; i < length_; i++) {
        DCHECK(temp_[i] == 0);
      }
      for (; carry > 0 && i < remaining_z; i++) {
        Z[z_index + i] = digit_add2(Z[z_index + i], carry, &carry);
      }
      // {carry} might be != 0 here if Z was negative before. That's fine.
    }
  }
}

// Main FFT function for recursive invocations ("inner layer").
void MultiplyFFT_Inner(RWDigits Z, Digits X, Digits Y, const Parameters& params,
                       ProcessorImpl* processor) {
  int omega = 2 * params.r;  // really: 2^(2r)
  int theta = params.r;      // really: 2^r

  FFTContainer a(params.n, params.K, processor);
  a.Start_Default(X, params.s, theta, omega);
  FFTContainer b(params.n, params.K, processor);
  b.Start_Default(Y, params.s, theta, omega);

  a.PointwiseMultiply(b);
  if (processor->should_terminate()) return;

  FFTContainer& c = a;
  c.BackwardFFT(0, params.n, omega);

  c.CounterWeightAndRecombine(theta, params.m, Z, params.s);
}

// Actual implementation of pointwise multiplications.
void FFTContainer::DoPointwiseMultiplication(const FFTContainer& other,
                                             int start, int end,
                                             digit_t* temp) {
  // The (K_ & 3) != 0 condition makes sure that the inner FFT gets
  // to split the work into at least 4 chunks.
  bool use_fft = length_ >= kFftInnerThreshold && (K_ & 3) == 0;
  Parameters params;
  if (use_fft) ComputeParameters_Inner(K_, &params);
  RWDigits result(temp, 2 * length_);
  for (int i = start; i < end; i++) {
    Digits A(part_[i], length_);
    Digits B(other.part_[i], length_);
    if (use_fft) {
      MultiplyFFT_Inner(result, A, B, params, processor_);
    } else {
      processor_->Multiply(result, A, B);
    }
    if (processor_->should_terminate()) return;
    ModFnDoubleWidth(part_[i], result.digits(), length_);
    // To improve cache friendliness, we perform the first level of the
    // backwards FFT here.
    if ((i & 1) == 1) {
      SumDiff(part_[i - 1], part_[i], part_[i - 1], part_[i], length_);
    }
  }
}

// Convenient entry point for pointwise multiplications.
void FFTContainer::PointwiseMultiply(const FFTContainer& other) {
  DCHECK(n_ == other.n_);
  DoPointwiseMultiplication(other, 0, n_, temp_);
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// Part 4: Tying everything together into a multiplication algorithm.

// TODO(jkummerow): Consider doing a "Mersenne transform" and CRT reconstruction
// of the final result. Might yield a few percent of perf improvement.

// TODO(jkummerow): Consider implementing the "sqrt(2) trick".
// Gaudry/Kruppa/Zimmerman report that it saved them around 10%.

void ProcessorImpl::MultiplyFFT(RWDigits Z, Digits X, Digits Y) {
  Parameters params;
  int m = GetParameters(X.len() + Y.len(), &params);
  int omega = params.r;  // really: 2^r

  FFTContainer a(params.n, params.K, this);
  a.Start(X, params.s, 0, omega);
  if (X == Y) {
    // Squaring.
    a.PointwiseMultiply(a);
  } else {
    FFTContainer b(params.n, params.K, this);
    b.Start(Y, params.s, 0, omega);
    a.PointwiseMultiply(b);
  }
  if (should_terminate()) return;

  a.BackwardFFT(0, params.n, omega);
  a.NormalizeAndRecombine(omega, m, Z, params.s);
}

}  // namespace bigint
}  // namespace v8
