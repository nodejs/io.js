// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Slightly adapted for inclusion in V8.
// Copyright 2014 the V8 project authors. All rights reserved.
// List of adaptations:
// - include guard names
// - wrap in v8 namespace
// - formatting (git cl format)
// - include paths

#ifndef V8_BASE_SAFE_CONVERSIONS_H_
#define V8_BASE_SAFE_CONVERSIONS_H_

#include <stddef.h>

#include <cmath>
#include <concepts>
#include <limits>
#include <type_traits>

#include "src/base/safe_conversions_impl.h"

#if defined(__ARMEL__) && !defined(__native_client__)
#include "src/base/safe_conversions_arm_impl.h"
#define BASE_HAS_OPTIMIZED_SAFE_CONVERSIONS (1)
#else
#define BASE_HAS_OPTIMIZED_SAFE_CONVERSIONS (0)
#endif

namespace v8::base {
namespace internal {

#if !BASE_HAS_OPTIMIZED_SAFE_CONVERSIONS
template <typename Dst, typename Src>
struct SaturateFastAsmOp {
  static constexpr bool is_supported = false;
  static constexpr Dst Do(Src) {
    // Force a compile failure if instantiated.
    return CheckOnFailure::template HandleFailure<Dst>();
  }
};
#endif  // BASE_HAS_OPTIMIZED_SAFE_CONVERSIONS
#undef BASE_HAS_OPTIMIZED_SAFE_CONVERSIONS

// The following special case a few specific integer conversions where we can
// eke out better performance than range checking.
template <typename Dst, typename Src>
struct IsValueInRangeFastOp {
  static constexpr bool is_supported = false;
  static constexpr bool Do(Src value) {
    // Force a compile failure if instantiated.
    return CheckOnFailure::template HandleFailure<bool>();
  }
};

// Signed to signed range comparison.
template <typename Dst, typename Src>
  requires(std::signed_integral<Dst> && std::signed_integral<Src> &&
           !IsTypeInRangeForNumericType<Dst, Src>::value)
struct IsValueInRangeFastOp<Dst, Src> {
  static constexpr bool is_supported = true;

  static constexpr bool Do(Src value) {
    // Just downcast to the smaller type, sign extend it back to the original
    // type, and then see if it matches the original value.
    return value == static_cast<Dst>(value);
  }
};

// Signed to unsigned range comparison.
template <typename Dst, typename Src>
  requires(std::unsigned_integral<Dst> && std::signed_integral<Src> &&
           !IsTypeInRangeForNumericType<Dst, Src>::value)
struct IsValueInRangeFastOp<Dst, Src> {
  static constexpr bool is_supported = true;

  static constexpr bool Do(Src value) {
    // We cast a signed as unsigned to overflow negative values to the top,
    // then compare against whichever maximum is smaller, as our upper bound.
    return as_unsigned(value) <= as_unsigned(CommonMax<Src, Dst>());
  }
};

// Convenience function that returns true if the supplied value is in range
// for the destination type.
template <typename Dst, typename Src>
constexpr bool IsValueInRangeForNumericType(Src value) {
  using SrcType = typename internal::UnderlyingType<Src>::type;
  return internal::IsValueInRangeFastOp<Dst, SrcType>::is_supported
             ? internal::IsValueInRangeFastOp<Dst, SrcType>::Do(
                   static_cast<SrcType>(value))
             : internal::DstRangeRelationToSrcRange<Dst>(
                   static_cast<SrcType>(value))
                   .IsValid();
}

// checked_cast<> is analogous to static_cast<> for numeric types,
// except that it CHECKs that the specified numeric conversion will not
// overflow or underflow. NaN source will always trigger a CHECK.
template <typename Dst, class CheckHandler = internal::CheckOnFailure,
          typename Src>
constexpr Dst checked_cast(Src value) {
  // This throws a compile-time error on evaluating the constexpr if it can be
  // determined at compile-time as failing, otherwise it will CHECK at runtime.
  using SrcType = typename internal::UnderlyingType<Src>::type;
  if (IsValueInRangeForNumericType<Dst>(value)) [[likely]] {
    return static_cast<Dst>(static_cast<SrcType>(value));
  }
  return CheckHandler::template HandleFailure<Dst>();
}

// Default boundaries for integral/float: max/infinity, lowest/-infinity, 0/NaN.
// You may provide your own limits (e.g. to saturated_cast) so long as you
// implement all of the static constexpr member functions in the class below.
template <typename T>
struct SaturationDefaultLimits : public std::numeric_limits<T> {
  static constexpr T NaN() {
    if constexpr (std::numeric_limits<T>::has_quiet_NaN) {
      return std::numeric_limits<T>::quiet_NaN();
    } else {
      return T();
    }
  }
  using std::numeric_limits<T>::max;
  static constexpr T Overflow() {
    if constexpr (std::numeric_limits<T>::has_infinity) {
      return std::numeric_limits<T>::infinity();
    } else {
      return std::numeric_limits<T>::max();
    }
  }
  using std::numeric_limits<T>::lowest;
  static constexpr T Underflow() {
    if constexpr (std::numeric_limits<T>::has_infinity) {
      return std::numeric_limits<T>::infinity() * -1;
    } else {
      return std::numeric_limits<T>::lowest();
    }
  }
};

template <typename Dst, template <typename> class S, typename Src>
constexpr Dst saturated_cast_impl(Src value, RangeCheck constraint) {
  // For some reason clang generates much better code when the branch is
  // structured exactly this way, rather than a sequence of checks.
  return !constraint.IsOverflowFlagSet()
             ? (!constraint.IsUnderflowFlagSet() ? static_cast<Dst>(value)
                                                 : S<Dst>::Underflow())
             // Skip this check for integral Src, which cannot be NaN.
             : (std::is_integral_v<Src> || !constraint.IsUnderflowFlagSet()
                    ? S<Dst>::Overflow()
                    : S<Dst>::NaN());
}

// We can reduce the number of conditions and get slightly better performance
// for normal signed and unsigned integer ranges. And in the specific case of
// Arm, we can use the optimized saturation instructions.
template <typename Dst, typename Src>
struct SaturateFastOp {
  static constexpr bool is_supported = false;
  static constexpr Dst Do(Src value) {
    // Force a compile failure if instantiated.
    return CheckOnFailure::template HandleFailure<Dst>();
  }
};

template <typename Dst, typename Src>
  requires(std::integral<Src> && std::integral<Dst> &&
           SaturateFastAsmOp<Dst, Src>::is_supported)
struct SaturateFastOp<Dst, Src> {
  static constexpr bool is_supported = true;
  static constexpr Dst Do(Src value) {
    return SaturateFastAsmOp<Dst, Src>::Do(value);
  }
};

template <typename Dst, typename Src>
  requires(std::integral<Src> && std::integral<Dst> &&
           !SaturateFastAsmOp<Dst, Src>::is_supported)
struct SaturateFastOp<Dst, Src> {
  static constexpr bool is_supported = true;
  static constexpr Dst Do(Src value) {
    // The exact order of the following is structured to hit the correct
    // optimization heuristics across compilers. Do not change without
    // checking the emitted code.
    const Dst saturated = CommonMaxOrMin<Dst, Src>(
        IsMaxInRangeForNumericType<Dst, Src>() ||
        (!IsMinInRangeForNumericType<Dst, Src>() && IsValueNegative(value)));
    if (IsValueInRangeForNumericType<Dst>(value)) [[likely]] {
      return static_cast<Dst>(value);
    }
    return saturated;
  }
};

// saturated_cast<> is analogous to static_cast<> for numeric types, except
// that the specified numeric conversion will saturate by default rather than
// overflow or underflow, and NaN assignment to an integral will return 0.
// All boundary condition behaviors can be overridden with a custom handler.
template <typename Dst,
          template <typename> class SaturationHandler = SaturationDefaultLimits,
          typename Src>
constexpr Dst saturated_cast(Src value) {
  using SrcType = typename UnderlyingType<Src>::type;
  return !IsConstantEvaluated() && SaturateFastOp<Dst, SrcType>::is_supported &&
                 std::is_same_v<SaturationHandler<Dst>,
                                SaturationDefaultLimits<Dst>>
             ? SaturateFastOp<Dst, SrcType>::Do(static_cast<SrcType>(value))
             : saturated_cast_impl<Dst, SaturationHandler, SrcType>(
                   static_cast<SrcType>(value),
                   DstRangeRelationToSrcRange<Dst, SaturationHandler, SrcType>(
                       static_cast<SrcType>(value)));
}

// strict_cast<> is analogous to static_cast<> for numeric types, except that
// it will cause a compile failure if the destination type is not large enough
// to contain any value in the source type. It performs no runtime checking.
template <typename Dst, typename Src>
constexpr Dst strict_cast(Src value) {
  using SrcType = typename UnderlyingType<Src>::type;
  static_assert(UnderlyingType<Src>::is_numeric, "Argument must be numeric.");
  static_assert(std::is_arithmetic_v<Dst>, "Result must be numeric.");

  // If you got here from a compiler error, it's because you tried to assign
  // from a source type to a destination type that has insufficient range.
  // The solution may be to change the destination type you're assigning to,
  // and use one large enough to represent the source.
  // Alternatively, you may be better served with the checked_cast<> or
  // saturated_cast<> template functions for your particular use case.
  static_assert(StaticDstRangeRelationToSrcRange<Dst, SrcType>::value ==
                    NUMERIC_RANGE_CONTAINED,
                "The source type is out of range for the destination type. "
                "Please see strict_cast<> comments for more information.");

  return static_cast<Dst>(static_cast<SrcType>(value));
}

// Some wrappers to statically check that a type is in range.
template <typename Dst, typename Src>
struct IsNumericRangeContained {
  static constexpr bool value = false;
};

template <typename Dst, typename Src>
  requires(ArithmeticOrUnderlyingEnum<Dst>::value &&
           ArithmeticOrUnderlyingEnum<Src>::value)
struct IsNumericRangeContained<Dst, Src> {
  static constexpr bool value =
      StaticDstRangeRelationToSrcRange<Dst, Src>::value ==
      NUMERIC_RANGE_CONTAINED;
};

// StrictNumeric implements compile time range checking between numeric types by
// wrapping assignment operations in a strict_cast. This class is intended to be
// used for function arguments and return types, to ensure the destination type
// can always contain the source type. This is essentially the same as enforcing
// -Wconversion in gcc and C4302 warnings on MSVC, but it can be applied
// incrementally at API boundaries, making it easier to convert code so that it
// compiles cleanly with truncation warnings enabled.
// This template should introduce no runtime overhead, but it also provides no
// runtime checking of any of the associated mathematical operations. Use
// CheckedNumeric for runtime range checks of the actual value being assigned.
template <typename T>
class StrictNumeric {
 public:
  using type = T;

  constexpr StrictNumeric() : value_(0) {}

  // Copy constructor.
  template <typename Src>
  constexpr StrictNumeric(const StrictNumeric<Src>& rhs)
      : value_(strict_cast<T>(rhs.value_)) {}

  // Strictly speaking, this is not necessary, but declaring this allows class
  // template argument deduction to be used so that it is possible to simply
  // write `StrictNumeric(777)` instead of `StrictNumeric<int>(777)`.
  // NOLINTNEXTLINE(runtime/explicit)
  constexpr StrictNumeric(T value) : value_(value) {}

  // This is not an explicit constructor because we implicitly upgrade regular
  // numerics to StrictNumerics to make them easier to use.
  template <typename Src>
  // NOLINTNEXTLINE(runtime/explicit)
  constexpr StrictNumeric(Src value) : value_(strict_cast<T>(value)) {}

  // If you got here from a compiler error, it's because you tried to assign
  // from a source type to a destination type that has insufficient range.
  // The solution may be to change the destination type you're assigning to,
  // and use one large enough to represent the source.
  // If you're assigning from a CheckedNumeric<> class, you may be able to use
  // the AssignIfValid() member function, specify a narrower destination type to
  // the member value functions (e.g. val.template ValueOrDie<Dst>()), use one
  // of the value helper functions (e.g. ValueOrDieForType<Dst>(val)).
  // If you've encountered an _ambiguous overload_ you can use a static_cast<>
  // to explicitly cast the result to the destination type.
  // If none of that works, you may be better served with the checked_cast<> or
  // saturated_cast<> template functions for your particular use case.
  template <typename Dst>
    requires(IsNumericRangeContained<Dst, T>::value)
  constexpr operator Dst() const {
    return static_cast<typename ArithmeticOrUnderlyingEnum<Dst>::type>(value_);
  }

 private:
  template <typename>
  friend class StrictNumeric;

  T value_;
};

// Convenience wrapper returns a StrictNumeric from the provided arithmetic
// type.
template <typename T>
constexpr StrictNumeric<typename UnderlyingType<T>::type> MakeStrictNum(
    const T value) {
  return value;
}

#define BASE_NUMERIC_COMPARISON_OPERATORS(CLASS, NAME, OP)          \
  template <typename L, typename R>                                 \
    requires(internal::Is##CLASS##Op<L, R>::value)                  \
  constexpr bool operator OP(const L lhs, const R rhs) {            \
    return SafeCompare<NAME, typename UnderlyingType<L>::type,      \
                       typename UnderlyingType<R>::type>(lhs, rhs); \
  }

BASE_NUMERIC_COMPARISON_OPERATORS(Strict, IsLess, <)
BASE_NUMERIC_COMPARISON_OPERATORS(Strict, IsLessOrEqual, <=)
BASE_NUMERIC_COMPARISON_OPERATORS(Strict, IsGreater, >)
BASE_NUMERIC_COMPARISON_OPERATORS(Strict, IsGreaterOrEqual, >=)
BASE_NUMERIC_COMPARISON_OPERATORS(Strict, IsEqual, ==)
BASE_NUMERIC_COMPARISON_OPERATORS(Strict, IsNotEqual, !=)

}  // namespace internal

using internal::as_signed;
using internal::as_unsigned;
using internal::checked_cast;
using internal::IsTypeInRangeForNumericType;
using internal::IsValueInRangeForNumericType;
using internal::IsValueNegative;
using internal::MakeStrictNum;
using internal::SafeUnsignedAbs;
using internal::saturated_cast;
using internal::strict_cast;
using internal::StrictNumeric;

// Explicitly make a shorter size_t alias for convenience.
using SizeT = StrictNumeric<size_t>;

// floating -> integral conversions that saturate and thus can actually return
// an integral type.
//
// Generally, what you want is saturated_cast<Dst>(std::nearbyint(x)), which
// rounds correctly according to IEEE-754 (round to nearest, ties go to nearest
// even number; this avoids bias). If your code is performance-critical
// and you are sure that you will never overflow, you can use std::lrint()
// or std::llrint(), which return a long or long long directly.
//
// Below are convenience functions around similar patterns, except that
// they round in nonstandard directions and will generally be slower.

// Rounds towards negative infinity (i.e., down).
template <typename Dst = int, typename Src>
  requires(std::integral<Dst> && std::floating_point<Src>)
Dst ClampFloor(Src value) {
  return saturated_cast<Dst>(std::floor(value));
}

// Rounds towards positive infinity (i.e., up).
template <typename Dst = int, typename Src>
  requires(std::integral<Dst> && std::floating_point<Src>)
Dst ClampCeil(Src value) {
  return saturated_cast<Dst>(std::ceil(value));
}

// Rounds towards nearest integer, with ties away from zero.
// This means that 0.5 will be rounded to 1 and 1.5 will be rounded to 2.
// Similarly, -0.5 will be rounded to -1 and -1.5 will be rounded to -2.
//
// This is normally not what you want accuracy-wise (it introduces a small bias
// away from zero), and it is not the fastest option, but it is frequently what
// existing code expects. Compare with saturated_cast<Dst>(std::nearbyint(x))
// or std::lrint(x), which would round 0.5 and -0.5 to 0 but 1.5 to 2 and
// -1.5 to -2.
template <typename Dst = int, typename Src>
  requires(std::integral<Dst> && std::floating_point<Src>)
Dst ClampRound(Src value) {
  const Src rounded = std::round(value);
  return saturated_cast<Dst>(rounded);
}

}  // namespace v8::base

#endif  // V8_BASE_SAFE_CONVERSIONS_H_
