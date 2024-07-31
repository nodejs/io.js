// Copyright (c) 2017 Facebook Inc.
// Copyright (c) 2017 Georgia Institute of Technology
// Copyright 2019 Google LLC
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_V8_CODEGEN_FP16_INL_H_
#define THIRD_PARTY_V8_CODEGEN_FP16_INL_H_

#include "src/codegen/code-stub-assembler-inl.h"
#include "src/codegen/code-stub-assembler.h"
#include "src/codegen/tnode.h"

// The following code-stub-assembler implementation corresponds with
// third_party/fp16/includes/fp16.h to use software-implemented
// floating point conversion between float16 and float32.

namespace v8 {
namespace internal {
// fp16_ieee_to_fp32_value()
TNode<Float32T> CodeStubAssembler::ChangeFloat16ToFloat32(
    TNode<Float16T> value) {
  /*
   * Extend the half-precision floating-point number to 32 bits and shift to the
   * upper part of the 32-bit word:
   *      +---+-----+------------+-------------------+
   *      | S |EEEEE|MM MMMM MMMM|0000 0000 0000 0000|
   *      +---+-----+------------+-------------------+
   * Bits  31  26-30    16-25            0-15
   *
   * S - sign bit, E - bits of the biased exponent, M - bits of the mantissa, 0
   * - zero bits.
   */
  TNode<Uint32T> w = ReinterpretCast<Uint32T>(
      Word32Shl(ReinterpretCast<Uint32T>(value), Uint32Constant(16)));
  /*
   * Extract the sign of the input number into the high bit of the 32-bit word:
   *
   *      +---+----------------------------------+
   *      | S |0000000 00000000 00000000 00000000|
   *      +---+----------------------------------+
   * Bits  31                 0-31
   */
  TNode<Word32T> sign = Word32And(w, Uint32Constant(0x80000000U));
  /*
   * Extract mantissa and biased exponent of the input number into the high bits
   * of the 32-bit word:
   *
   *      +-----+------------+---------------------+
   *      |EEEEE|MM MMMM MMMM|0 0000 0000 0000 0000|
   *      +-----+------------+---------------------+
   * Bits  27-31    17-26            0-16
   */
  TNode<Uint32T> two_w = Uint32Add(w, w);

  /*
   * Shift mantissa and exponent into bits 23-28 and bits 13-22 so they become
   * mantissa and exponent of a single-precision floating-point number:
   *
   *       S|Exponent |          Mantissa
   *      +-+---+-----+------------+----------------+
   *      |0|000|EEEEE|MM MMMM MMMM|0 0000 0000 0000|
   *      +-+---+-----+------------+----------------+
   * Bits   | 23-31   |           0-22
   *
   * Next, there are some adjustments to the exponent:
   * - The exponent needs to be corrected by the difference in exponent bias
   * between single-precision and half-precision formats (0x7F - 0xF = 0x70)
   * - Inf and NaN values in the inputs should become Inf and NaN values after
   * conversion to the single-precision number. Therefore, if the biased
   * exponent of the half-precision input was 0x1F (max possible value), the
   * biased exponent of the single-precision output must be 0xFF (max possible
   * value). We do this correction in two steps:
   *   - First, we adjust the exponent by (0xFF - 0x1F) = 0xE0 (see exp_offset
   * below) rather than by 0x70 suggested by the difference in the exponent bias
   * (see above).
   *   - Then we multiply the single-precision result of exponent adjustment by
   * 2**(-112) to reverse the effect of exponent adjustment by 0xE0 less the
   * necessary exponent adjustment by 0x70 due to difference in exponent bias.
   *     The floating-point multiplication hardware would ensure than Inf and
   * NaN would retain their value on at least partially IEEE754-compliant
   * implementations.
   *
   * Note that the above operations do not handle denormal inputs (where biased
   * exponent == 0). However, they also do not operate on denormal inputs, and
   * do not produce denormal results.
   */
  TNode<Uint32T> exp_offset = Uint32Constant(0x70000000U /* 0xE0U << 23 */);

  TNode<Float32T> exp_scale = Float32Constant(0x1.0p-112f);

  TNode<Float32T> normalized_value =
      Float32Mul(BitcastInt32ToFloat32(Uint32Add(
                     Word32Shr(two_w, Uint32Constant(4)), exp_offset)),
                 exp_scale);

  /*
   * Convert denormalized half-precision inputs into single-precision results
   * (always normalized). Zero inputs are also handled here.
   *
   * In a denormalized number the biased exponent is zero, and mantissa has
   * on-zero bits. First, we shift mantissa into bits 0-9 of the 32-bit word.
   *
   *                  zeros           |  mantissa
   *      +---------------------------+------------+
   *      |0000 0000 0000 0000 0000 00|MM MMMM MMMM|
   *      +---------------------------+------------+
   * Bits             10-31                0-9
   *
   * Now, remember that denormalized half-precision numbers are represented as:
   *    FP16 = mantissa * 2**(-24).
   * The trick is to construct a normalized single-precision number with the
   * same mantissa and thehalf-precision input and with an exponent which would
   * scale the corresponding mantissa bits to 2**(-24). A normalized
   * single-precision floating-point number is represented as: FP32 = (1 +
   * mantissa * 2**(-23)) * 2**(exponent - 127) Therefore, when the biased
   * exponent is 126, a unit change in the mantissa of the input denormalized
   * half-precision number causes a change of the constructud single-precision
   * number by 2**(-24), i.e. the same ammount.
   *
   * The last step is to adjust the bias of the constructed single-precision
   * number. When the input half-precision number is zero, the constructed
   * single-precision number has the value of FP32 = 1 * 2**(126 - 127) =
   * 2**(-1) = 0.5 Therefore, we need to subtract 0.5 from the constructed
   * single-precision number to get the numerical equivalent of the input
   * half-precision number.
   */

  TNode<Uint32T> magic_mask = ReinterpretCast<Uint32T>(
      Word32Shl(Uint32Constant(126), Uint32Constant(23)));
  TNode<Float32T> magic_bias = Float32Constant(0.5);

  TNode<Float32T> denormalized_value = Float32Sub(
      BitcastInt32ToFloat32(
          ReinterpretCast<Uint32T>(Word32Or(Word32Shr(two_w, 17), magic_mask))),
      magic_bias);

  /*
   * - Choose either results of conversion of input as a normalized number, or
   * as a denormalized number, depending on the input exponent. The variable
   * two_w contains input exponent in bits 27-31, therefore if its smaller than
   * 2**27, the input is either a denormal number, or zero.
   * - Combine the result of conversion of exponent and mantissa with the sign
   * of the input number.
   */

  TNode<Uint32T> denormalized_cutoff = Uint32Constant(0x8000000);

  TVARIABLE(Uint32T, var_result);

  Label is_normalized(this), is_denormalized(this), done(this);

  Branch(Uint32LessThan(two_w, denormalized_cutoff), &is_denormalized,
         &is_normalized);

  BIND(&is_denormalized);
  {
    var_result = BitcastFloat32ToInt32(denormalized_value);
    Goto(&done);
  }

  BIND(&is_normalized);
  {
    var_result = BitcastFloat32ToInt32(normalized_value);
    Goto(&done);
  }

  BIND(&done);

  return BitcastInt32ToFloat32(Word32Or(sign, var_result.value()));
}

// fp16_ieee_from_fp32_value()
TNode<Float16T> CodeStubAssembler::TruncateFloat32ToFloat16(
    TNode<Float32T> value) {
  TVARIABLE(Float32T, base);

  TVARIABLE(Uint32T, bias);
  TVARIABLE(Uint16T, result);
  Label if_bias(this), is_nan(this), is_not_nan(this), bias_done(this),
      done(this);

  TNode<Float32T> scale_to_inf = Float32Constant(0x1.0p+112f);
  TNode<Float32T> scale_to_zero = Float32Constant(0x1.0p-110f);

  base = Float32Abs(Float32Mul(Float32Mul(value, scale_to_inf), scale_to_zero));

  TNode<Uint32T> w = BitcastFloat32ToInt32(value);
  TNode<Uint32T> shl1_w = Uint32Add(w, w);
  TNode<Uint32T> sign = Word32And(w, Uint32Constant(0x80000000U));
  bias = Word32And(shl1_w, Uint32Constant(0XFF000000U));

  GotoIf(Uint32LessThan(bias.value(), Uint32Constant(0x71000000U)), &if_bias);
  Goto(&bias_done);

  BIND(&if_bias);
  bias = Uint32Constant(0x71000000U);
  Goto(&bias_done);

  BIND(&bias_done);
  base = Float32Add(BitcastInt32ToFloat32(Uint32Add(
                        ReinterpretCast<Uint32T>(Word32Shr(bias.value(), 1)),
                        Uint32Constant(0x07800000U))),
                    base.value());

  TNode<Uint32T> bits = BitcastFloat32ToInt32(base.value());
  TNode<Uint32T> exp_bits = ReinterpretCast<Uint32T>(
      Word32And(Word32Shr(bits, 13), Uint32Constant(0x00007C00U)));
  TNode<Uint32T> mantissa_bits =
      ReinterpretCast<Uint32T>(Word32And(bits, Uint32Constant(0x00000FFFU)));

  Branch(Uint32GreaterThan(shl1_w, Uint32Constant(0xFF000000U)), &is_nan,
         &is_not_nan);

  BIND(&is_nan);
  {
    result = Uint16Constant(0x7E00);
    Goto(&done);
  };
  BIND(&is_not_nan);
  {
    result = ReinterpretCast<Uint16T>(Uint32Add(exp_bits, mantissa_bits));
    Goto(&done);
  };

  BIND(&done);
  return ReinterpretCast<Float16T>(
      Word32Or(Word32Shr(sign, 16), result.value()));
}

}  // namespace internal
}  // namespace v8

#endif  // THIRD_PARTY_V8_CODEGEN_FP16_INL_H_