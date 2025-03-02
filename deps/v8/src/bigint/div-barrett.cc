// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Barrett division, finding the inverse with Newton's method.
// Reference: "Fast Division of Large Integers" by Karl Hasselström,
// found at https://treskal.com/s/masters-thesis.pdf

// Many thanks to Karl Wiberg, k@w5.se, for both writing up an
// understandable theoretical description of the algorithm and privately
// providing a demo implementation, on which the implementation in this file is
// based.

#include <algorithm>

#include "src/bigint/bigint-internal.h"
#include "src/bigint/digit-arithmetic.h"
#include "src/bigint/div-helpers.h"
#include "src/bigint/vector-arithmetic.h"

namespace v8 {
namespace bigint {

namespace {

void DcheckIntegerPartRange(Digits X, digit_t min, digit_t max) {
#if DEBUG
  digit_t integer_part = X.msd();
  DCHECK(integer_part >= min);
  DCHECK(integer_part <= max);
#else
  USE(X);
  USE(min);
  USE(max);
#endif
}

}  // namespace

// Z := (the fractional part of) 1/V, via naive division.
// See comments at {Invert} and {InvertNewton} below for details.
void ProcessorImpl::InvertBasecase(RWDigits Z, Digits V, RWDigits scratch) {
  DCHECK(Z.len() > V.len());
  DCHECK(V.len() > 0);
  DCHECK(scratch.len() >= 2 * V.len());
  int n = V.len();
  RWDigits X(scratch, 0, 2 * n);
  digit_t borrow = 0;
  int i = 0;
  for (; i < n; i++) X[i] = 0;
  for (; i < 2 * n; i++) X[i] = digit_sub2(0, V[i - n], borrow, &borrow);
  DCHECK(borrow == 1);
  RWDigits R(nullptr, 0);  // We don't need the remainder.
  if (n < kBurnikelThreshold) {
    DivideSchoolbook(Z, R, X, V);
  } else {
    DivideBurnikelZiegler(Z, R, X, V);
  }
}

// This is Algorithm 4.2 from the paper.
// Computes the inverse of V, shifted by kDigitBits * 2 * V.len, accurate to
// V.len+1 digits. The V.len low digits of the result digits will be written
// to Z, plus there is an implicit top digit with value 1.
// Needs InvertNewtonScratchSpace(V.len) of scratch space.
// The result is either correct or off by one (about half the time it is
// correct, half the time it is one too much, and in the corner case where V is
// minimal and the implicit top digit would have to be 2 it is one too little).
// Barrett's division algorithm can handle that, so we don't care.
void ProcessorImpl::InvertNewton(RWDigits Z, Digits V, RWDigits scratch) {
  const int vn = V.len();
  DCHECK(Z.len() >= vn);
  DCHECK(scratch.len() >= InvertNewtonScratchSpace(vn));
  const int kSOffset = 0;
  const int kWOffset = 0;  // S and W can share their scratch space.
  const int kUOffset = vn + kInvertNewtonExtraSpace;

  // The base case won't work otherwise.
  DCHECK(V.len() >= 3);

  constexpr int kBasecasePrecision = kNewtonInversionThreshold - 1;
  // V must have more digits than the basecase.
  DCHECK(V.len() > kBasecasePrecision);
  DCHECK(IsBitNormalized(V));

  // Step (1): Setup.
  // Calculate precision required at each step.
  // {k} is the number of fraction bits for the current iteration.
  int k = vn * kDigitBits;
  int target_fraction_bits[8 * sizeof(vn)];  // "k_i" in the paper.
  int iteration = -1;  // "i" in the paper, except inverted to run downwards.
  while (k > kBasecasePrecision * kDigitBits) {
    iteration++;
    target_fraction_bits[iteration] = k;
    k = DIV_CEIL(k, 2);
  }
  // At this point, k <= kBasecasePrecision*kDigitBits is the number of
  // fraction bits to use in the base case. {iteration} is the highest index
  // in use for f[].

  // Step (2): Initial approximation.
  int initial_digits = DIV_CEIL(k + 1, kDigitBits);
  Digits top_part_of_v(V, vn - initial_digits, initial_digits);
  InvertBasecase(Z, top_part_of_v, scratch);
  Z[initial_digits] = Z[initial_digits] + 1;  // Implicit top digit.
  // From now on, we'll keep Z.len updated to the part that's already computed.
  Z.set_len(initial_digits + 1);

  // Step (3): Precision doubling loop.
  while (true) {
    DcheckIntegerPartRange(Z, 1, 2);

    // (3b): S = Z^2
    RWDigits S(scratch, kSOffset, 2 * Z.len());
    Multiply(S, Z, Z);
    if (should_terminate()) return;
    S.TrimOne();  // Top digit of S is unused.
    DcheckIntegerPartRange(S, 1, 4);

    // (3c): T = V, truncated so that at least 2k+3 fraction bits remain.
    int fraction_digits = DIV_CEIL(2 * k + 3, kDigitBits);
    int t_len = std::min(V.len(), fraction_digits);
    Digits T(V, V.len() - t_len, t_len);

    // (3d): U = T * S, truncated so that at least 2k+1 fraction bits remain
    // (U has one integer digit, which might be zero).
    fraction_digits = DIV_CEIL(2 * k + 1, kDigitBits);
    RWDigits U(scratch, kUOffset, S.len() + T.len());
    DCHECK(U.len() > fraction_digits);
    Multiply(U, S, T);
    if (should_terminate()) return;
    U = U + (U.len() - (1 + fraction_digits));
    DcheckIntegerPartRange(U, 0, 3);

    // (3e): W = 2 * Z, padded with "0" fraction bits so that it has the
    // same number of fraction bits as U.
    DCHECK(U.len() >= Z.len());
    RWDigits W(scratch, kWOffset, U.len());
    int padding_digits = U.len() - Z.len();
    for (int i = 0; i < padding_digits; i++) W[i] = 0;
    LeftShift(W + padding_digits, Z, 1);
    DcheckIntegerPartRange(W, 2, 4);

    // (3f): Z = W - U.
    // This check is '<=' instead of '<' because U's top digit is its
    // integer part, and we want vn fraction digits.
    if (U.len() <= vn) {
      // Normal subtraction.
      // This is not the last iteration.
      DCHECK(iteration > 0);
      Z.set_len(U.len());
      digit_t borrow = SubtractAndReturnBorrow(Z, W, U);
      DCHECK(borrow == 0);
      USE(borrow);
      DcheckIntegerPartRange(Z, 1, 2);
    } else {
      // Truncate some least significant digits so that we get vn
      // fraction digits, and compute the integer digit separately.
      // This is the last iteration.
      DCHECK(iteration == 0);
      Z.set_len(vn);
      Digits W_part(W, W.len() - vn - 1, vn);
      Digits U_part(U, U.len() - vn - 1, vn);
      digit_t borrow = SubtractAndReturnBorrow(Z, W_part, U_part);
      digit_t integer_part = W.msd() - U.msd() - borrow;
      DCHECK(integer_part == 1 || integer_part == 2);
      if (integer_part == 2) {
        // This is the rare case where the correct result would be 2.0, but
        // since we can't express that by returning only the fractional part
        // with an implicit 1-digit, we have to return [1.]9999... instead.
        for (int i = 0; i < Z.len(); i++) Z[i] = ~digit_t{0};
      }
      break;
    }
    // (3g, 3h): Update local variables and loop.
    k = target_fraction_bits[iteration];
    iteration--;
  }
}

// Computes the inverse of V, shifted by kDigitBits * 2 * V.len, accurate to
// V.len+1 digits. The V.len low digits of the result digits will be written
// to Z, plus there is an implicit top digit with value 1.
// (Corner case: if V is minimal, the implicit digit should be 2; in that case
// we return one less than the correct answer. DivideBarrett can handle that.)
// Needs InvertScratchSpace(V.len) digits of scratch space.
void ProcessorImpl::Invert(RWDigits Z, Digits V, RWDigits scratch) {
  DCHECK(Z.len() > V.len());
  DCHECK(V.len() >= 1);
  DCHECK(IsBitNormalized(V));
  DCHECK(scratch.len() >= InvertScratchSpace(V.len()));

  int vn = V.len();
  if (vn >= kNewtonInversionThreshold) {
    return InvertNewton(Z, V, scratch);
  }
  if (vn == 1) {
    digit_t d = V[0];
    digit_t dummy_remainder;
    Z[0] = digit_div(~d, ~digit_t{0}, d, &dummy_remainder);
    Z[1] = 0;
  } else {
    InvertBasecase(Z, V, scratch);
    if (Z[vn] == 1) {
      for (int i = 0; i < vn; i++) Z[i] = ~digit_t{0};
      Z[vn] = 0;
    }
  }
}

// This is algorithm 3.5 from the paper.
// Computes Q(uotient) and R(emainder) for A/B using I, which is a
// precomputed approximation of 1/B (e.g. with Invert() above).
// Needs DivideBarrettScratchSpace(A.len) scratch space.
void ProcessorImpl::DivideBarrett(RWDigits Q, RWDigits R, Digits A, Digits B,
                                  Digits I, RWDigits scratch) {
  DCHECK(Q.len() > A.len() - B.len());
  DCHECK(R.len() >= B.len());
  DCHECK(A.len() > B.len());  // Careful: This is *not* '>=' !
  DCHECK(A.len() <= 2 * B.len());
  DCHECK(B.len() > 0);
  DCHECK(IsBitNormalized(B));
  DCHECK(I.len() == A.len() - B.len());
  DCHECK(scratch.len() >= DivideBarrettScratchSpace(A.len()));

  int orig_q_len = Q.len();

  // (1): A1 = A with B.len fewer digits.
  Digits A1 = A + B.len();
  DCHECK(A1.len() == I.len());

  // (2): Q = A1*I with I.len fewer digits.
  // {I} has an implicit high digit with value 1, so we add {A1} to the high
  // part of the multiplication result.
  RWDigits K(scratch, 0, 2 * I.len());
  Multiply(K, A1, I);
  if (should_terminate()) return;
  Q.set_len(I.len() + 1);
  Add(Q, K + I.len(), A1);
  // K is no longer used, can reuse {scratch} for P.

  // (3): R = A - B*Q (approximate remainder).
  RWDigits P(scratch, 0, A.len() + 1);
  Multiply(P, B, Q);
  if (should_terminate()) return;
  digit_t borrow = SubtractAndReturnBorrow(R, A, Digits(P, 0, B.len()));
  // R may be allocated wider than B, zero out any extra digits if so.
  for (int i = B.len(); i < R.len(); i++) R[i] = 0;
  digit_t r_high = A[B.len()] - P[B.len()] - borrow;

  // Adjust R and Q so that they become the correct remainder and quotient.
  // The number of iterations is guaranteed to be at most some very small
  // constant, unless the caller gave us a bad approximate quotient.
  if (r_high >> (kDigitBits - 1) == 1) {
    // (5b): R < 0, so R += B
    digit_t q_sub = 0;
    do {
      r_high += AddAndReturnCarry(R, R, B);
      q_sub++;
      DCHECK(q_sub <= 5);
    } while (r_high != 0);
    Subtract(Q, q_sub);
  } else {
    digit_t q_add = 0;
    while (r_high != 0 || GreaterThanOrEqual(R, B)) {
      // (5c): R >= B, so R -= B
      r_high -= SubtractAndReturnBorrow(R, R, B);
      q_add++;
      DCHECK(q_add <= 5);
    }
    Add(Q, q_add);
  }
  // (5a): Return.
  int final_q_len = Q.len();
  Q.set_len(orig_q_len);
  for (int i = final_q_len; i < orig_q_len; i++) Q[i] = 0;
}

// Computes Q(uotient) and R(emainder) for A/B, using Barrett division.
void ProcessorImpl::DivideBarrett(RWDigits Q, RWDigits R, Digits A, Digits B) {
  DCHECK(Q.len() > A.len() - B.len());
  DCHECK(R.len() >= B.len());
  DCHECK(A.len() > B.len());  // Careful: This is *not* '>=' !
  DCHECK(B.len() > 0);

  // Normalize B, and shift A by the same amount.
  ShiftedDigits b_normalized(B);
  ShiftedDigits a_normalized(A, b_normalized.shift());
  // Keep the code below more concise.
  B = b_normalized;
  A = a_normalized;

  // The core DivideBarrett function above only supports A having at most
  // twice as many digits as B. We generalize this to arbitrary inputs
  // similar to Burnikel-Ziegler division by performing a t-by-1 division
  // of B-sized chunks. It's easy to special-case the situation where we
  // don't need to bother.
  int barrett_dividend_length = A.len() <= 2 * B.len() ? A.len() : 2 * B.len();
  int i_len = barrett_dividend_length - B.len();
  ScratchDigits I(i_len + 1);  // +1 is for temporary use by Invert().
  int scratch_len =
      std::max(InvertScratchSpace(i_len),
               DivideBarrettScratchSpace(barrett_dividend_length));
  ScratchDigits scratch(scratch_len);
  Invert(I, Digits(B, B.len() - i_len, i_len), scratch);
  if (should_terminate()) return;
  I.TrimOne();
  DCHECK(I.len() == i_len);
  if (A.len() > 2 * B.len()) {
    // This follows the variable names and and algorithmic steps of
    // DivideBurnikelZiegler().
    int n = B.len();  // Chunk length.
    // (5): {t} is the number of B-sized chunks of A.
    int t = DIV_CEIL(A.len(), n);
    DCHECK(t >= 3);
    // (6)/(7): Z is used for the current 2-chunk block to be divided by B,
    // initialized to the two topmost chunks of A.
    int z_len = n * 2;
    ScratchDigits Z(z_len);
    PutAt(Z, A + n * (t - 2), z_len);
    // (8): For i from t-2 downto 0 do
    int qi_len = n + 1;
    ScratchDigits Qi(qi_len);
    ScratchDigits Ri(n);
    // First iteration unrolled and specialized.
    {
      int i = t - 2;
      DivideBarrett(Qi, Ri, Z, B, I, scratch);
      if (should_terminate()) return;
      RWDigits target = Q + n * i;
      // In the first iteration, all qi_len = n + 1 digits may be used.
      int to_copy = std::min(qi_len, target.len());
      for (int j = 0; j < to_copy; j++) target[j] = Qi[j];
      for (int j = to_copy; j < target.len(); j++) target[j] = 0;
#if DEBUG
      for (int j = to_copy; j < Qi.len(); j++) {
        DCHECK(Qi[j] == 0);
      }
#endif
    }
    // Now loop over any remaining iterations.
    for (int i = t - 3; i >= 0; i--) {
      // (8b): If i > 0, set Z_(i-1) = [Ri, A_(i-1)].
      // (De-duped with unrolled first iteration, hence reading A_(i).)
      PutAt(Z + n, Ri, n);
      PutAt(Z, A + n * i, n);
      // (8a): Compute Qi, Ri such that Zi = B*Qi + Ri.
      DivideBarrett(Qi, Ri, Z, B, I, scratch);
      DCHECK(Qi[qi_len - 1] == 0);
      if (should_terminate()) return;
      // (9): Return Q = [Q_(t-2), ..., Q_0]...
      PutAt(Q + n * i, Qi, n);
    }
    Ri.Normalize();
    DCHECK(Ri.len() <= R.len());
    // (9): ...and R = R_0 * 2^(-leading_zeros).
    RightShift(R, Ri, b_normalized.shift());
  } else {
    DivideBarrett(Q, R, A, B, I, scratch);
    if (should_terminate()) return;
    RightShift(R, R, b_normalized.shift());
  }
}

}  // namespace bigint
}  // namespace v8
