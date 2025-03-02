// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Toom-Cook multiplication.
// Reference: https://en.wikipedia.org/wiki/Toom%E2%80%93Cook_multiplication

#include <algorithm>

#include "src/bigint/bigint-internal.h"
#include "src/bigint/digit-arithmetic.h"
#include "src/bigint/vector-arithmetic.h"

namespace v8 {
namespace bigint {

namespace {

void TimesTwo(RWDigits X) {
  digit_t carry = 0;
  for (int i = 0; i < X.len(); i++) {
    digit_t d = X[i];
    X[i] = (d << 1) | carry;
    carry = d >> (kDigitBits - 1);
  }
}

void DivideByTwo(RWDigits X) {
  digit_t carry = 0;
  for (int i = X.len() - 1; i >= 0; i--) {
    digit_t d = X[i];
    X[i] = (d >> 1) | carry;
    carry = d << (kDigitBits - 1);
  }
}

void DivideByThree(RWDigits X) {
  digit_t remainder = 0;
  for (int i = X.len() - 1; i >= 0; i--) {
    digit_t d = X[i];
    digit_t upper = (remainder << kHalfDigitBits) | (d >> kHalfDigitBits);
    digit_t u_result = upper / 3;
    remainder = upper - 3 * u_result;
    digit_t lower = (remainder << kHalfDigitBits) | (d & kHalfDigitMask);
    digit_t l_result = lower / 3;
    remainder = lower - 3 * l_result;
    X[i] = (u_result << kHalfDigitBits) | l_result;
  }
}

}  // namespace

#if DEBUG
// Set {len_} to 1 rather than 0 so that attempts to access the first digit
// will crash.
#define MARK_INVALID(D) D = RWDigits(nullptr, 1)
#else
#define MARK_INVALID(D) (void(0))
#endif

void ProcessorImpl::Toom3Main(RWDigits Z, Digits X, Digits Y) {
  DCHECK(Z.len() >= X.len() + Y.len());
  // Phase 1: Splitting.
  int i = DIV_CEIL(std::max(X.len(), Y.len()), 3);
  Digits X0(X, 0, i);
  Digits X1(X, i, i);
  Digits X2(X, 2 * i, i);
  Digits Y0(Y, 0, i);
  Digits Y1(Y, i, i);
  Digits Y2(Y, 2 * i, i);

  // Temporary storage.
  int p_len = i + 1;      // For all px, qx below.
  int r_len = 2 * p_len;  // For all r_x, Rx below.
  Storage temp_storage(4 * r_len);
  // We will use the same variable names as the Wikipedia article, as much as
  // C++ lets us: our "p_m1" is their "p(-1)" etc. For consistency with other
  // algorithms, we use X and Y where Wikipedia uses m and n.
  // We will use and reuse the temporary storage as follows:
  //
  //   chunk                  | -------- time ----------->
  //   [0 .. i]               |( po )( p_m1 ) ( r_m2  )
  //   [i+1 .. rlen-1]        |( qo )( q_m1 ) ( r_m2  )
  //   [rlen .. rlen+i]       | (p_1 ) ( p_m2 ) (r_inf)
  //   [rlen+i+1 .. 2*rlen-1] | (q_1 ) ( q_m2 ) (r_inf)
  //   [2*rlen .. 3*rlen-1]   |      (   r_1          )
  //   [3*rlen .. 4*rlen-1]   |             (  r_m1   )
  //
  // This requires interleaving phases 2 and 3 a bit: after computing
  // r_1 = p_1 * q_1, we can reuse p_1's storage for p_m2, and so on.
  digit_t* t = temp_storage.get();
  RWDigits po(t, p_len);
  RWDigits qo(t + p_len, p_len);
  RWDigits p_1(t + r_len, p_len);
  RWDigits q_1(t + r_len + p_len, p_len);
  RWDigits r_1(t + 2 * r_len, r_len);
  RWDigits r_m1(t + 3 * r_len, r_len);

  // We can also share the  backing stores of Z, r_0, R0.
  DCHECK(Z.len() >= r_len);
  RWDigits r_0(Z, 0, r_len);

  // Phase 2a: Evaluation, steps 0, 1, m1.
  // po = X0 + X2
  Add(po, X0, X2);
  // p_0 = X0
  // p_1 = po + X1
  Add(p_1, po, X1);
  // p_m1 = po - X1
  RWDigits p_m1 = po;
  bool p_m1_sign = SubtractSigned(p_m1, po, false, X1, false);
  MARK_INVALID(po);

  // qo = Y0 + Y2
  Add(qo, Y0, Y2);
  // q_0 = Y0
  // q_1 = qo + Y1
  Add(q_1, qo, Y1);
  // q_m1 = qo - Y1
  RWDigits q_m1 = qo;
  bool q_m1_sign = SubtractSigned(q_m1, qo, false, Y1, false);
  MARK_INVALID(qo);

  // Phase 3a: Pointwise multiplication, steps 0, 1, m1.
  Multiply(r_0, X0, Y0);
  Multiply(r_1, p_1, q_1);
  Multiply(r_m1, p_m1, q_m1);
  bool r_m1_sign = p_m1_sign != q_m1_sign;

  // Phase 2b: Evaluation, steps m2 and inf.
  // p_m2 = (p_m1 + X2) * 2 - X0
  RWDigits p_m2 = p_1;
  MARK_INVALID(p_1);
  bool p_m2_sign = AddSigned(p_m2, p_m1, p_m1_sign, X2, false);
  TimesTwo(p_m2);
  p_m2_sign = SubtractSigned(p_m2, p_m2, p_m2_sign, X0, false);
  // p_inf = X2

  // q_m2 = (q_m1 + Y2) * 2 - Y0
  RWDigits q_m2 = q_1;
  MARK_INVALID(q_1);
  bool q_m2_sign = AddSigned(q_m2, q_m1, q_m1_sign, Y2, false);
  TimesTwo(q_m2);
  q_m2_sign = SubtractSigned(q_m2, q_m2, q_m2_sign, Y0, false);
  // q_inf = Y2

  // Phase 3b: Pointwise multiplication, steps m2 and inf.
  RWDigits r_m2(t, r_len);
  MARK_INVALID(p_m1);
  MARK_INVALID(q_m1);
  Multiply(r_m2, p_m2, q_m2);
  bool r_m2_sign = p_m2_sign != q_m2_sign;

  RWDigits r_inf(t + r_len, r_len);
  MARK_INVALID(p_m2);
  MARK_INVALID(q_m2);
  Multiply(r_inf, X2, Y2);

  // Phase 4: Interpolation.
  Digits R0 = r_0;
  Digits R4 = r_inf;
  // R3 <- (r_m2 - r_1) / 3
  RWDigits R3 = r_m2;
  bool R3_sign = SubtractSigned(R3, r_m2, r_m2_sign, r_1, false);
  DivideByThree(R3);
  // R1 <- (r_1 - r_m1) / 2
  RWDigits R1 = r_1;
  bool R1_sign = SubtractSigned(R1, r_1, false, r_m1, r_m1_sign);
  DivideByTwo(R1);
  // R2 <- r_m1 - r_0
  RWDigits R2 = r_m1;
  bool R2_sign = SubtractSigned(R2, r_m1, r_m1_sign, R0, false);
  // R3 <- (R2 - R3) / 2 + 2 * r_inf
  R3_sign = SubtractSigned(R3, R2, R2_sign, R3, R3_sign);
  DivideByTwo(R3);
  // TODO(jkummerow): Would it be a measurable improvement to write an
  // "AddTwice" helper?
  R3_sign = AddSigned(R3, R3, R3_sign, r_inf, false);
  R3_sign = AddSigned(R3, R3, R3_sign, r_inf, false);
  // R2 <- R2 + R1 - R4
  R2_sign = AddSigned(R2, R2, R2_sign, R1, R1_sign);
  R2_sign = SubtractSigned(R2, R2, R2_sign, R4, false);
  // R1 <- R1 - R3
  R1_sign = SubtractSigned(R1, R1, R1_sign, R3, R3_sign);

#if DEBUG
  R1.Normalize();
  R2.Normalize();
  R3.Normalize();
  DCHECK(R1_sign == false || R1.len() == 0);
  DCHECK(R2_sign == false || R2.len() == 0);
  DCHECK(R3_sign == false || R3.len() == 0);
#endif

  // Phase 5: Recomposition. R0 is already in place. Overflow can't happen.
  for (int j = R0.len(); j < Z.len(); j++) Z[j] = 0;
  AddAndReturnOverflow(Z + i, R1);
  AddAndReturnOverflow(Z + 2 * i, R2);
  AddAndReturnOverflow(Z + 3 * i, R3);
  AddAndReturnOverflow(Z + 4 * i, R4);
}

void ProcessorImpl::MultiplyToomCook(RWDigits Z, Digits X, Digits Y) {
  DCHECK(X.len() >= Y.len());
  int k = Y.len();
  // TODO(jkummerow): Would it be a measurable improvement to share the
  // scratch memory for several invocations?
  Digits X0(X, 0, k);
  Toom3Main(Z, X0, Y);
  if (X.len() > Y.len()) {
    ScratchDigits T(2 * k);
    for (int i = k; i < X.len(); i += k) {
      Digits Xi(X, i, k);
      // TODO(jkummerow): would it be a measurable improvement to craft a
      // "ToomChunk" method in the style of {KaratsubaChunk}?
      Toom3Main(T, Xi, Y);
      AddAndReturnOverflow(Z + i, T);  // Can't overflow.
    }
  }
}

}  // namespace bigint
}  // namespace v8
