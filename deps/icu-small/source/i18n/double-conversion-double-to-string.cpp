// © 2018 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
//
// From the double-conversion library. Original license:
//
// Copyright 2010 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// ICU PATCH: ifdef around UCONFIG_NO_FORMATTING
#include "unicode/utypes.h"
#if !UCONFIG_NO_FORMATTING

#include <algorithm>
#include <climits>
#include <cmath>

// ICU PATCH: Customize header file paths for ICU.
// The file fixed-dtoa.h is not needed.

#include "double-conversion-double-to-string.h"

#include "double-conversion-bignum-dtoa.h"
#include "double-conversion-fast-dtoa.h"
#include "double-conversion-ieee.h"
#include "double-conversion-utils.h"

// ICU PATCH: Wrap in ICU namespace
U_NAMESPACE_BEGIN

namespace double_conversion {

#if 0  // not needed for ICU
const DoubleToStringConverter& DoubleToStringConverter::EcmaScriptConverter() {
  int flags = UNIQUE_ZERO | EMIT_POSITIVE_EXPONENT_SIGN;
  static DoubleToStringConverter converter(flags,
                                           "Infinity",
                                           "NaN",
                                           'e',
                                           -6, 21,
                                           6, 0);
  return converter;
}


bool DoubleToStringConverter::HandleSpecialValues(
    double value,
    StringBuilder* result_builder) const {
  Double double_inspect(value);
  if (double_inspect.IsInfinite()) {
    if (infinity_symbol_ == DOUBLE_CONVERSION_NULLPTR) return false;
    if (value < 0) {
      result_builder->AddCharacter('-');
    }
    result_builder->AddString(infinity_symbol_);
    return true;
  }
  if (double_inspect.IsNan()) {
    if (nan_symbol_ == DOUBLE_CONVERSION_NULLPTR) return false;
    result_builder->AddString(nan_symbol_);
    return true;
  }
  return false;
}


void DoubleToStringConverter::CreateExponentialRepresentation(
    const char* decimal_digits,
    int length,
    int exponent,
    StringBuilder* result_builder) const {
  DOUBLE_CONVERSION_ASSERT(length != 0);
  result_builder->AddCharacter(decimal_digits[0]);
  if (length == 1) {
    if ((flags_ & EMIT_TRAILING_DECIMAL_POINT_IN_EXPONENTIAL) != 0) {
      result_builder->AddCharacter('.');
      if ((flags_ & EMIT_TRAILING_ZERO_AFTER_POINT_IN_EXPONENTIAL) != 0) {
          result_builder->AddCharacter('0');
      }
    }
  } else {
    result_builder->AddCharacter('.');
    result_builder->AddSubstring(&decimal_digits[1], length-1);
  }
  result_builder->AddCharacter(exponent_character_);
  if (exponent < 0) {
    result_builder->AddCharacter('-');
    exponent = -exponent;
  } else {
    if ((flags_ & EMIT_POSITIVE_EXPONENT_SIGN) != 0) {
      result_builder->AddCharacter('+');
    }
  }
  DOUBLE_CONVERSION_ASSERT(exponent < 1e4);
  // Changing this constant requires updating the comment of DoubleToStringConverter constructor
  const int kMaxExponentLength = 5;
  char buffer[kMaxExponentLength + 1];
  buffer[kMaxExponentLength] = '\0';
  int first_char_pos = kMaxExponentLength;
  if (exponent == 0) {
    buffer[--first_char_pos] = '0';
  } else {
    while (exponent > 0) {
      buffer[--first_char_pos] = '0' + (exponent % 10);
      exponent /= 10;
    }
  }
  // Add prefix '0' to make exponent width >= min(min_exponent_with_, kMaxExponentLength)
  // For example: convert 1e+9 -> 1e+09, if min_exponent_with_ is set to 2
  while(kMaxExponentLength - first_char_pos < std::min(min_exponent_width_, kMaxExponentLength)) {
    buffer[--first_char_pos] = '0';
  }
  result_builder->AddSubstring(&buffer[first_char_pos],
                               kMaxExponentLength - first_char_pos);
}


void DoubleToStringConverter::CreateDecimalRepresentation(
    const char* decimal_digits,
    int length,
    int decimal_point,
    int digits_after_point,
    StringBuilder* result_builder) const {
  // Create a representation that is padded with zeros if needed.
  if (decimal_point <= 0) {
      // "0.00000decimal_rep" or "0.000decimal_rep00".
    result_builder->AddCharacter('0');
    if (digits_after_point > 0) {
      result_builder->AddCharacter('.');
      result_builder->AddPadding('0', -decimal_point);
      DOUBLE_CONVERSION_ASSERT(length <= digits_after_point - (-decimal_point));
      result_builder->AddSubstring(decimal_digits, length);
      int remaining_digits = digits_after_point - (-decimal_point) - length;
      result_builder->AddPadding('0', remaining_digits);
    }
  } else if (decimal_point >= length) {
    // "decimal_rep0000.00000" or "decimal_rep.0000".
    result_builder->AddSubstring(decimal_digits, length);
    result_builder->AddPadding('0', decimal_point - length);
    if (digits_after_point > 0) {
      result_builder->AddCharacter('.');
      result_builder->AddPadding('0', digits_after_point);
    }
  } else {
    // "decima.l_rep000".
    DOUBLE_CONVERSION_ASSERT(digits_after_point > 0);
    result_builder->AddSubstring(decimal_digits, decimal_point);
    result_builder->AddCharacter('.');
    DOUBLE_CONVERSION_ASSERT(length - decimal_point <= digits_after_point);
    result_builder->AddSubstring(&decimal_digits[decimal_point],
                                 length - decimal_point);
    int remaining_digits = digits_after_point - (length - decimal_point);
    result_builder->AddPadding('0', remaining_digits);
  }
  if (digits_after_point == 0) {
    if ((flags_ & EMIT_TRAILING_DECIMAL_POINT) != 0) {
      result_builder->AddCharacter('.');
    }
    if ((flags_ & EMIT_TRAILING_ZERO_AFTER_POINT) != 0) {
      result_builder->AddCharacter('0');
    }
  }
}


bool DoubleToStringConverter::ToShortestIeeeNumber(
    double value,
    StringBuilder* result_builder,
    DoubleToStringConverter::DtoaMode mode) const {
  DOUBLE_CONVERSION_ASSERT(mode == SHORTEST || mode == SHORTEST_SINGLE);
  if (Double(value).IsSpecial()) {
    return HandleSpecialValues(value, result_builder);
  }

  int decimal_point;
  bool sign;
  const int kDecimalRepCapacity = kBase10MaximalLength + 1;
  char decimal_rep[kDecimalRepCapacity];
  int decimal_rep_length;

  DoubleToAscii(value, mode, 0, decimal_rep, kDecimalRepCapacity,
                &sign, &decimal_rep_length, &decimal_point);

  bool unique_zero = (flags_ & UNIQUE_ZERO) != 0;
  if (sign && (value != 0.0 || !unique_zero)) {
    result_builder->AddCharacter('-');
  }

  int exponent = decimal_point - 1;
  if ((decimal_in_shortest_low_ <= exponent) &&
      (exponent < decimal_in_shortest_high_)) {
    CreateDecimalRepresentation(decimal_rep, decimal_rep_length,
                                decimal_point,
                                (std::max)(0, decimal_rep_length - decimal_point),
                                result_builder);
  } else {
    CreateExponentialRepresentation(decimal_rep, decimal_rep_length, exponent,
                                    result_builder);
  }
  return true;
}


bool DoubleToStringConverter::ToFixed(double value,
                                      int requested_digits,
                                      StringBuilder* result_builder) const {
  DOUBLE_CONVERSION_ASSERT(kMaxFixedDigitsBeforePoint == 60);
  const double kFirstNonFixed = 1e60;

  if (Double(value).IsSpecial()) {
    return HandleSpecialValues(value, result_builder);
  }

  if (requested_digits > kMaxFixedDigitsAfterPoint) return false;
  if (value >= kFirstNonFixed || value <= -kFirstNonFixed) return false;

  // Find a sufficiently precise decimal representation of n.
  int decimal_point;
  bool sign;
  // Add space for the '\0' byte.
  const int kDecimalRepCapacity =
      kMaxFixedDigitsBeforePoint + kMaxFixedDigitsAfterPoint + 1;
  char decimal_rep[kDecimalRepCapacity];
  int decimal_rep_length;
  DoubleToAscii(value, FIXED, requested_digits,
                decimal_rep, kDecimalRepCapacity,
                &sign, &decimal_rep_length, &decimal_point);

  bool unique_zero = ((flags_ & UNIQUE_ZERO) != 0);
  if (sign && (value != 0.0 || !unique_zero)) {
    result_builder->AddCharacter('-');
  }

  CreateDecimalRepresentation(decimal_rep, decimal_rep_length, decimal_point,
                              requested_digits, result_builder);
  return true;
}


bool DoubleToStringConverter::ToExponential(
    double value,
    int requested_digits,
    StringBuilder* result_builder) const {
  if (Double(value).IsSpecial()) {
    return HandleSpecialValues(value, result_builder);
  }

  if (requested_digits < -1) return false;
  if (requested_digits > kMaxExponentialDigits) return false;

  int decimal_point;
  bool sign;
  // Add space for digit before the decimal point and the '\0' character.
  const int kDecimalRepCapacity = kMaxExponentialDigits + 2;
  DOUBLE_CONVERSION_ASSERT(kDecimalRepCapacity > kBase10MaximalLength);
  char decimal_rep[kDecimalRepCapacity];
#ifndef NDEBUG
  // Problem: there is an assert in StringBuilder::AddSubstring() that
  // will pass this buffer to strlen(), and this buffer is not generally
  // null-terminated.
  memset(decimal_rep, 0, sizeof(decimal_rep));
#endif
  int decimal_rep_length;

  if (requested_digits == -1) {
    DoubleToAscii(value, SHORTEST, 0,
                  decimal_rep, kDecimalRepCapacity,
                  &sign, &decimal_rep_length, &decimal_point);
  } else {
    DoubleToAscii(value, PRECISION, requested_digits + 1,
                  decimal_rep, kDecimalRepCapacity,
                  &sign, &decimal_rep_length, &decimal_point);
    DOUBLE_CONVERSION_ASSERT(decimal_rep_length <= requested_digits + 1);

    for (int i = decimal_rep_length; i < requested_digits + 1; ++i) {
      decimal_rep[i] = '0';
    }
    decimal_rep_length = requested_digits + 1;
  }

  bool unique_zero = ((flags_ & UNIQUE_ZERO) != 0);
  if (sign && (value != 0.0 || !unique_zero)) {
    result_builder->AddCharacter('-');
  }

  int exponent = decimal_point - 1;
  CreateExponentialRepresentation(decimal_rep,
                                  decimal_rep_length,
                                  exponent,
                                  result_builder);
  return true;
}


bool DoubleToStringConverter::ToPrecision(double value,
                                          int precision,
                                          StringBuilder* result_builder) const {
  if (Double(value).IsSpecial()) {
    return HandleSpecialValues(value, result_builder);
  }

  if (precision < kMinPrecisionDigits || precision > kMaxPrecisionDigits) {
    return false;
  }

  // Find a sufficiently precise decimal representation of n.
  int decimal_point;
  bool sign;
  // Add one for the terminating null character.
  const int kDecimalRepCapacity = kMaxPrecisionDigits + 1;
  char decimal_rep[kDecimalRepCapacity];
  int decimal_rep_length;

  DoubleToAscii(value, PRECISION, precision,
                decimal_rep, kDecimalRepCapacity,
                &sign, &decimal_rep_length, &decimal_point);
  DOUBLE_CONVERSION_ASSERT(decimal_rep_length <= precision);

  bool unique_zero = ((flags_ & UNIQUE_ZERO) != 0);
  if (sign && (value != 0.0 || !unique_zero)) {
    result_builder->AddCharacter('-');
  }

  // The exponent if we print the number as x.xxeyyy. That is with the
  // decimal point after the first digit.
  int exponent = decimal_point - 1;

  int extra_zero = ((flags_ & EMIT_TRAILING_ZERO_AFTER_POINT) != 0) ? 1 : 0;
  bool as_exponential =
      (-decimal_point + 1 > max_leading_padding_zeroes_in_precision_mode_) ||
      (decimal_point - precision + extra_zero >
       max_trailing_padding_zeroes_in_precision_mode_);
  if ((flags_ & NO_TRAILING_ZERO) != 0) {
    // Truncate trailing zeros that occur after the decimal point (if exponential,
    // that is everything after the first digit).
    int stop = as_exponential ? 1 : std::max(1, decimal_point);
    while (decimal_rep_length > stop && decimal_rep[decimal_rep_length - 1] == '0') {
      --decimal_rep_length;
    }
    // Clamp precision to avoid the code below re-adding the zeros.
    precision = std::min(precision, decimal_rep_length);
  }
  if (as_exponential) {
    // Fill buffer to contain 'precision' digits.
    // Usually the buffer is already at the correct length, but 'DoubleToAscii'
    // is allowed to return less characters.
    for (int i = decimal_rep_length; i < precision; ++i) {
      decimal_rep[i] = '0';
    }

    CreateExponentialRepresentation(decimal_rep,
                                    precision,
                                    exponent,
                                    result_builder);
  } else {
    CreateDecimalRepresentation(decimal_rep, decimal_rep_length, decimal_point,
                                (std::max)(0, precision - decimal_point),
                                result_builder);
  }
  return true;
}
#endif // not needed for ICU


static BignumDtoaMode DtoaToBignumDtoaMode(
    DoubleToStringConverter::DtoaMode dtoa_mode) {
  switch (dtoa_mode) {
    case DoubleToStringConverter::SHORTEST:  return BIGNUM_DTOA_SHORTEST;
    case DoubleToStringConverter::SHORTEST_SINGLE:
        return BIGNUM_DTOA_SHORTEST_SINGLE;
    case DoubleToStringConverter::FIXED:     return BIGNUM_DTOA_FIXED;
    case DoubleToStringConverter::PRECISION: return BIGNUM_DTOA_PRECISION;
    default:
      DOUBLE_CONVERSION_UNREACHABLE();
  }
}


void DoubleToStringConverter::DoubleToAscii(double v,
                                            DtoaMode mode,
                                            int requested_digits,
                                            char* buffer,
                                            int buffer_length,
                                            bool* sign,
                                            int* length,
                                            int* point) {
  Vector<char> vector(buffer, buffer_length);
  DOUBLE_CONVERSION_ASSERT(!Double(v).IsSpecial());
  DOUBLE_CONVERSION_ASSERT(mode == SHORTEST || mode == SHORTEST_SINGLE || requested_digits >= 0);

  if (Double(v).Sign() < 0) {
    *sign = true;
    v = -v;
  } else {
    *sign = false;
  }

  if (mode == PRECISION && requested_digits == 0) {
    vector[0] = '\0';
    *length = 0;
    return;
  }

  if (v == 0) {
    vector[0] = '0';
    vector[1] = '\0';
    *length = 1;
    *point = 1;
    return;
  }

  bool fast_worked;
  switch (mode) {
    case SHORTEST:
      fast_worked = FastDtoa(v, FAST_DTOA_SHORTEST, 0, vector, length, point);
      break;
#if 0 // not needed for ICU
    case SHORTEST_SINGLE:
      fast_worked = FastDtoa(v, FAST_DTOA_SHORTEST_SINGLE, 0,
                             vector, length, point);
      break;
    case FIXED:
      fast_worked = FastFixedDtoa(v, requested_digits, vector, length, point);
      break;
    case PRECISION:
      fast_worked = FastDtoa(v, FAST_DTOA_PRECISION, requested_digits,
                             vector, length, point);
      break;
#endif // not needed for ICU
    default:
      fast_worked = false;
      DOUBLE_CONVERSION_UNREACHABLE();
  }
  if (fast_worked) return;

  // If the fast dtoa didn't succeed use the slower bignum version.
  BignumDtoaMode bignum_mode = DtoaToBignumDtoaMode(mode);
  BignumDtoa(v, bignum_mode, requested_digits, vector, length, point);
  vector[*length] = '\0';
}

}  // namespace double_conversion

// ICU PATCH: Close ICU namespace
U_NAMESPACE_END
#endif // ICU PATCH: close #if !UCONFIG_NO_FORMATTING
