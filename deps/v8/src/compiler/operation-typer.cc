// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/operation-typer.h"

#include "src/compiler/common-operator.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/type-cache.h"
#include "src/compiler/types.h"
#include "src/objects/oddball.h"

namespace v8 {
namespace internal {
namespace compiler {

OperationTyper::OperationTyper(JSHeapBroker* broker, Zone* zone)
    : zone_(zone), cache_(TypeCache::Get()) {
  infinity_ = Type::Constant(V8_INFINITY, zone);
  minus_infinity_ = Type::Constant(-V8_INFINITY, zone);
  Type truncating_to_zero = Type::MinusZeroOrNaN();
  DCHECK(!truncating_to_zero.Maybe(Type::Integral32()));

  singleton_empty_string_ =
      Type::Constant(broker, broker->empty_string(), zone);
  singleton_NaN_string_ = Type::Constant(broker, broker->NaN_string(), zone);
  singleton_zero_string_ = Type::Constant(broker, broker->zero_string(), zone);
  singleton_false_ = Type::Constant(broker, broker->false_value(), zone);
  singleton_true_ = Type::Constant(broker, broker->true_value(), zone);
  signed32ish_ = Type::Union(Type::Signed32(), truncating_to_zero, zone);
  unsigned32ish_ = Type::Union(Type::Unsigned32(), truncating_to_zero, zone);

  falsish_ = Type::Union(
      Type::Undetectable(),
      Type::Union(Type::Union(singleton_false_, cache_->kZeroish, zone),
                  singleton_empty_string_, zone),
      zone);
  truish_ = Type::Union(
      singleton_true_,
      Type::Union(Type::DetectableReceiver(), Type::Symbol(), zone), zone);
}

Type OperationTyper::Merge(Type left, Type right) {
  return Type::Union(left, right, zone());
}

Type OperationTyper::WeakenRange(Type previous_range, Type current_range) {
  static const double kWeakenMinLimits[] = {0.0,
                                            -1073741824.0,
                                            -2147483648.0,
                                            -4294967296.0,
                                            -8589934592.0,
                                            -17179869184.0,
                                            -34359738368.0,
                                            -68719476736.0,
                                            -137438953472.0,
                                            -274877906944.0,
                                            -549755813888.0,
                                            -1099511627776.0,
                                            -2199023255552.0,
                                            -4398046511104.0,
                                            -8796093022208.0,
                                            -17592186044416.0,
                                            -35184372088832.0,
                                            -70368744177664.0,
                                            -140737488355328.0,
                                            -281474976710656.0,
                                            -562949953421312.0};
  static const double kWeakenMaxLimits[] = {0.0,
                                            1073741823.0,
                                            2147483647.0,
                                            4294967295.0,
                                            8589934591.0,
                                            17179869183.0,
                                            34359738367.0,
                                            68719476735.0,
                                            137438953471.0,
                                            274877906943.0,
                                            549755813887.0,
                                            1099511627775.0,
                                            2199023255551.0,
                                            4398046511103.0,
                                            8796093022207.0,
                                            17592186044415.0,
                                            35184372088831.0,
                                            70368744177663.0,
                                            140737488355327.0,
                                            281474976710655.0,
                                            562949953421311.0};
  static_assert(arraysize(kWeakenMinLimits) == arraysize(kWeakenMaxLimits));

  double current_min = current_range.Min();
  double new_min = current_min;
  // Find the closest lower entry in the list of allowed
  // minima (or negative infinity if there is no such entry).
  if (current_min != previous_range.Min()) {
    new_min = -V8_INFINITY;
    for (double const min : kWeakenMinLimits) {
      if (min <= current_min) {
        new_min = min;
        break;
      }
    }
  }

  double current_max = current_range.Max();
  double new_max = current_max;
  // Find the closest greater entry in the list of allowed
  // maxima (or infinity if there is no such entry).
  if (current_max != previous_range.Max()) {
    new_max = V8_INFINITY;
    for (double const max : kWeakenMaxLimits) {
      if (max >= current_max) {
        new_max = max;
        break;
      }
    }
  }

  return Type::Range(new_min, new_max, zone());
}

Type OperationTyper::Rangify(Type type) {
  if (type.IsRange()) return type;  // Shortcut.
  if (!type.Is(cache_->kInteger)) {
    return type;  // Give up on non-integer types.
  }
  return Type::Range(type.Min(), type.Max(), zone());
}

namespace {

// Returns the array's least element, ignoring NaN.
// There must be at least one non-NaN element.
// Any -0 is converted to 0.
double array_min(double a[], size_t n) {
  DCHECK_NE(0, n);
  double x = +V8_INFINITY;
  for (size_t i = 0; i < n; ++i) {
    if (!std::isnan(a[i])) {
      x = std::min(a[i], x);
    }
  }
  DCHECK(!std::isnan(x));
  return x == 0 ? 0 : x;  // -0 -> 0
}

// Returns the array's greatest element, ignoring NaN.
// There must be at least one non-NaN element.
// Any -0 is converted to 0.
double array_max(double a[], size_t n) {
  DCHECK_NE(0, n);
  double x = -V8_INFINITY;
  for (size_t i = 0; i < n; ++i) {
    if (!std::isnan(a[i])) {
      x = std::max(a[i], x);
    }
  }
  DCHECK(!std::isnan(x));
  return x == 0 ? 0 : x;  // -0 -> 0
}

}  // namespace

Type OperationTyper::AddRanger(double lhs_min, double lhs_max, double rhs_min,
                               double rhs_max) {
  double results[4];
  results[0] = lhs_min + rhs_min;
  results[1] = lhs_min + rhs_max;
  results[2] = lhs_max + rhs_min;
  results[3] = lhs_max + rhs_max;
  // Since none of the inputs can be -0, the result cannot be -0 either.
  // However, it can be nan (the sum of two infinities of opposite sign).
  // On the other hand, if none of the "results" above is nan, then the
  // actual result cannot be nan either.
  int nans = 0;
  for (int i = 0; i < 4; ++i) {
    if (std::isnan(results[i])) ++nans;
  }
  if (nans == 4) return Type::NaN();
  Type type = Type::Range(array_min(results, 4), array_max(results, 4), zone());
  if (nans > 0) type = Type::Union(type, Type::NaN(), zone());
  // Examples:
  //   [-inf, -inf] + [+inf, +inf] = NaN
  //   [-inf, -inf] + [n, +inf] = [-inf, -inf] \/ NaN
  //   [-inf, +inf] + [n, +inf] = [-inf, +inf] \/ NaN
  //   [-inf, m] + [n, +inf] = [-inf, +inf] \/ NaN
  return type;
}

Type OperationTyper::SubtractRanger(double lhs_min, double lhs_max,
                                    double rhs_min, double rhs_max) {
  double results[4];
  results[0] = lhs_min - rhs_min;
  results[1] = lhs_min - rhs_max;
  results[2] = lhs_max - rhs_min;
  results[3] = lhs_max - rhs_max;
  // Since none of the inputs can be -0, the result cannot be -0.
  // However, it can be nan (the subtraction of two infinities of same sign).
  // On the other hand, if none of the "results" above is nan, then the actual
  // result cannot be nan either.
  int nans = 0;
  for (int i = 0; i < 4; ++i) {
    if (std::isnan(results[i])) ++nans;
  }
  if (nans == 4) return Type::NaN();  // [inf..inf] - [inf..inf] (all same sign)
  Type type = Type::Range(array_min(results, 4), array_max(results, 4), zone());
  return nans == 0 ? type : Type::Union(type, Type::NaN(), zone());
  // Examples:
  //   [-inf, +inf] - [-inf, +inf] = [-inf, +inf] \/ NaN
  //   [-inf, -inf] - [-inf, -inf] = NaN
  //   [-inf, -inf] - [n, +inf] = [-inf, -inf] \/ NaN
  //   [m, +inf] - [-inf, n] = [-inf, +inf] \/ NaN
}

Type OperationTyper::MultiplyRanger(double lhs_min, double lhs_max,
                                    double rhs_min, double rhs_max) {
  double results[4];
  results[0] = lhs_min * rhs_min;
  results[1] = lhs_min * rhs_max;
  results[2] = lhs_max * rhs_min;
  results[3] = lhs_max * rhs_max;
  // If the result may be nan, we give up on calculating a precise type,
  // because the discontinuity makes it too complicated.  Note that even if
  // none of the "results" above is nan, the actual result may still be, so we
  // have to do a different check:
  for (int i = 0; i < 4; ++i) {
    if (std::isnan(results[i])) {
      return cache_->kIntegerOrMinusZeroOrNaN;
    }
  }
  double min = array_min(results, 4);
  double max = array_max(results, 4);
  Type type = Type::Range(min, max, zone());
  if (min <= 0.0 && 0.0 <= max && (lhs_min < 0.0 || rhs_min < 0.0)) {
    type = Type::Union(type, Type::MinusZero(), zone());
  }
  // 0 * V8_INFINITY is NaN, regardless of sign
  if (((lhs_min == -V8_INFINITY || lhs_max == V8_INFINITY) &&
       (rhs_min <= 0.0 && 0.0 <= rhs_max)) ||
      ((rhs_min == -V8_INFINITY || rhs_max == V8_INFINITY) &&
       (lhs_min <= 0.0 && 0.0 <= lhs_max))) {
    type = Type::Union(type, Type::NaN(), zone());
  }
  return type;
}

Type OperationTyper::ConvertReceiver(Type type) {
  if (type.Is(Type::Receiver())) return type;
  bool const maybe_primitive = type.Maybe(Type::Primitive());
  type = Type::Intersect(type, Type::Receiver(), zone());
  if (maybe_primitive) {
    // ConvertReceiver maps null and undefined to the JSGlobalProxy of the
    // target function, and all other primitives are wrapped into a
    // JSPrimitiveWrapper.
    type = Type::Union(type, Type::StringWrapperOrOtherObject(), zone());
  }
  return type;
}

Type OperationTyper::ToNumber(Type type) {
  if (type.Is(Type::Number())) return type;

  // If {type} includes any receivers, we cannot tell what kind of
  // Number their callbacks might produce. Similarly in the case
  // where {type} includes String, it's not possible at this point
  // to tell which exact numbers are going to be produced.
  if (type.Maybe(Type::StringOrReceiver())) return Type::Number();

  // Both Symbol and BigInt primitives will cause exceptions
  // to be thrown from ToNumber conversions, so they don't
  // contribute to the resulting type anyways.
  type = Type::Intersect(type, Type::PlainPrimitive(), zone());

  // This leaves us with Number\/Oddball, so deal with the individual
  // Oddball primitives below.
  DCHECK(type.Is(Type::NumberOrOddball()));
  if (type.Maybe(Type::Null())) {
    // ToNumber(null) => +0
    type = Type::Union(type, cache_->kSingletonZero, zone());
  }
  if (type.Maybe(Type::Undefined())) {
    // ToNumber(undefined) => NaN
    type = Type::Union(type, Type::NaN(), zone());
  }
  if (type.Maybe(singleton_false_)) {
    // ToNumber(false) => +0
    type = Type::Union(type, cache_->kSingletonZero, zone());
  }
  if (type.Maybe(singleton_true_)) {
    // ToNumber(true) => +1
    type = Type::Union(type, cache_->kSingletonOne, zone());
  }
  return Type::Intersect(type, Type::Number(), zone());
}

Type OperationTyper::ToNumberConvertBigInt(Type type) {
  // If the {type} includes any receivers, then the callbacks
  // might actually produce BigInt primitive values here.
  bool maybe_bigint =
      type.Maybe(Type::BigInt()) || type.Maybe(Type::Receiver());
  type = ToNumber(Type::Intersect(type, Type::NonBigInt(), zone()));

  // Any BigInt is rounded to an integer Number in the range [-inf, inf].
  return maybe_bigint ? Type::Union(type, cache_->kInteger, zone()) : type;
}

Type OperationTyper::ToBigInt(Type type) {
  if (type.Is(Type::BigInt())) {
    return type;
  }

  return Type::BigInt();
}

Type OperationTyper::ToBigIntConvertNumber(Type type) {
  if (type.Is(Type::Unsigned32OrMinusZero())) {
    return Type::UnsignedBigInt63();
  } else if (type.Is(Type::Signed32OrMinusZero())) {
    return Type::SignedBigInt64();
  }

  bool maybe_number =
      type.Maybe(Type::Number()) || type.Maybe(Type::Receiver());
  type = ToBigInt(Type::Intersect(type, Type::NonNumber(), zone()));
  return maybe_number ? Type::Union(type, Type::BigInt(), zone()) : type;
}

Type OperationTyper::ToNumeric(Type type) {
  // If the {type} includes any receivers, then the callbacks
  // might actually produce BigInt primitive values here.
  if (type.Maybe(Type::Receiver())) {
    type = Type::Union(type, Type::BigInt(), zone());
  }
  return Type::Union(ToNumber(Type::Intersect(type, Type::NonBigInt(), zone())),
                     Type::Intersect(type, Type::BigInt(), zone()), zone());
}

Type OperationTyper::NumberAbs(Type type) {
  DCHECK(type.Is(Type::Number()));
  if (type.IsNone()) return type;

  bool const maybe_nan = type.Maybe(Type::NaN());
  bool const maybe_minuszero = type.Maybe(Type::MinusZero());

  type = Type::Intersect(type, Type::PlainNumber(), zone());
  if (!type.IsNone()) {
    double const max = type.Max();
    double const min = type.Min();
    if (min < 0) {
      if (type.Is(cache_->kInteger)) {
        type =
            Type::Range(0.0, std::max(std::fabs(min), std::fabs(max)), zone());
      } else {
        type = Type::PlainNumber();
      }
    }
  }

  if (maybe_minuszero) {
    type = Type::Union(type, cache_->kSingletonZero, zone());
  }
  if (maybe_nan) {
    type = Type::Union(type, Type::NaN(), zone());
  }
  return type;
}

Type OperationTyper::NumberAcos(Type type) {
  DCHECK(type.Is(Type::Number()));
  return Type::Number();
}

Type OperationTyper::NumberAcosh(Type type) {
  DCHECK(type.Is(Type::Number()));
  return Type::Number();
}

Type OperationTyper::NumberAsin(Type type) {
  DCHECK(type.Is(Type::Number()));
  return Type::Number();
}

Type OperationTyper::NumberAsinh(Type type) {
  DCHECK(type.Is(Type::Number()));
  return Type::Number();
}

Type OperationTyper::NumberAtan(Type type) {
  DCHECK(type.Is(Type::Number()));
  return Type::Number();
}

Type OperationTyper::NumberAtanh(Type type) {
  DCHECK(type.Is(Type::Number()));
  return Type::Number();
}

Type OperationTyper::NumberCbrt(Type type) {
  DCHECK(type.Is(Type::Number()));
  return Type::Number();
}

Type OperationTyper::NumberCeil(Type type) {
  DCHECK(type.Is(Type::Number()));
  if (type.Is(cache_->kIntegerOrMinusZeroOrNaN)) return type;
  type = Type::Intersect(type, Type::NaN(), zone());
  type = Type::Union(type, cache_->kIntegerOrMinusZero, zone());
  return type;
}

Type OperationTyper::NumberClz32(Type type) {
  DCHECK(type.Is(Type::Number()));
  return cache_->kZeroToThirtyTwo;
}

Type OperationTyper::NumberCos(Type type) {
  DCHECK(type.Is(Type::Number()));
  return Type::Number();
}

Type OperationTyper::NumberCosh(Type type) {
  DCHECK(type.Is(Type::Number()));
  return Type::Number();
}

Type OperationTyper::NumberExp(Type type) {
  DCHECK(type.Is(Type::Number()));
  return Type::Union(Type::PlainNumber(), Type::NaN(), zone());
}

Type OperationTyper::NumberExpm1(Type type) {
  DCHECK(type.Is(Type::Number()));
  return Type::Number();
}

Type OperationTyper::NumberFloor(Type type) {
  DCHECK(type.Is(Type::Number()));
  if (type.Is(cache_->kIntegerOrMinusZeroOrNaN)) return type;
  type = Type::Intersect(type, Type::MinusZeroOrNaN(), zone());
  type = Type::Union(type, cache_->kInteger, zone());
  return type;
}

Type OperationTyper::NumberFround(Type type) {
  DCHECK(type.Is(Type::Number()));
  return Type::Number();
}

Type OperationTyper::NumberLog(Type type) {
  DCHECK(type.Is(Type::Number()));
  return Type::Number();
}

Type OperationTyper::NumberLog1p(Type type) {
  DCHECK(type.Is(Type::Number()));
  return Type::Number();
}

Type OperationTyper::NumberLog2(Type type) {
  DCHECK(type.Is(Type::Number()));
  return Type::Number();
}

Type OperationTyper::NumberLog10(Type type) {
  DCHECK(type.Is(Type::Number()));
  return Type::Number();
}

Type OperationTyper::NumberRound(Type type) {
  DCHECK(type.Is(Type::Number()));
  if (type.Is(cache_->kIntegerOrMinusZeroOrNaN)) return type;
  type = Type::Intersect(type, Type::NaN(), zone());
  type = Type::Union(type, cache_->kIntegerOrMinusZero, zone());
  return type;
}

Type OperationTyper::NumberSign(Type type) {
  DCHECK(type.Is(Type::Number()));
  if (type.Is(cache_->kZeroish)) return type;
  bool maybe_minuszero = type.Maybe(Type::MinusZero());
  bool maybe_nan = type.Maybe(Type::NaN());
  type = Type::Intersect(type, Type::PlainNumber(), zone());
  if (type.IsNone()) {
    // Do nothing.
  } else if (type.Max() < 0.0) {
    type = cache_->kSingletonMinusOne;
  } else if (type.Max() <= 0.0) {
    type = cache_->kMinusOneOrZero;
  } else if (type.Min() > 0.0) {
    type = cache_->kSingletonOne;
  } else if (type.Min() >= 0.0) {
    type = cache_->kZeroOrOne;
  } else {
    type = Type::Range(-1.0, 1.0, zone());
  }
  if (maybe_minuszero) type = Type::Union(type, Type::MinusZero(), zone());
  if (maybe_nan) type = Type::Union(type, Type::NaN(), zone());
  DCHECK(!type.IsNone());
  return type;
}

Type OperationTyper::NumberSin(Type type) {
  DCHECK(type.Is(Type::Number()));
  return Type::Number();
}

Type OperationTyper::NumberSinh(Type type) {
  DCHECK(type.Is(Type::Number()));
  return Type::Number();
}

Type OperationTyper::NumberSqrt(Type type) {
  DCHECK(type.Is(Type::Number()));
  return Type::Number();
}

Type OperationTyper::NumberTan(Type type) {
  DCHECK(type.Is(Type::Number()));
  return Type::Number();
}

Type OperationTyper::NumberTanh(Type type) {
  DCHECK(type.Is(Type::Number()));
  return Type::Number();
}

Type OperationTyper::NumberTrunc(Type type) {
  DCHECK(type.Is(Type::Number()));
  if (type.Is(cache_->kIntegerOrMinusZeroOrNaN)) return type;
  type = Type::Intersect(type, Type::NaN(), zone());
  type = Type::Union(type, cache_->kIntegerOrMinusZero, zone());
  return type;
}

Type OperationTyper::NumberToBoolean(Type type) {
  DCHECK(type.Is(Type::Number()));
  if (type.IsNone()) return type;
  if (type.Is(cache_->kZeroish)) return singleton_false_;
  if (type.Is(Type::PlainNumber()) && (type.Max() < 0 || 0 < type.Min())) {
    return singleton_true_;  // Ruled out nan, -0 and +0.
  }
  return Type::Boolean();
}

Type OperationTyper::NumberToInt32(Type type) {
  DCHECK(type.Is(Type::Number()));

  if (type.Is(Type::Signed32())) return type;
  if (type.Is(cache_->kZeroish)) return cache_->kSingletonZero;
  if (type.Is(signed32ish_)) {
    return Type::Intersect(Type::Union(type, cache_->kSingletonZero, zone()),
                           Type::Signed32(), zone());
  }
  return Type::Signed32();
}

Type OperationTyper::NumberToString(Type type) {
  DCHECK(type.Is(Type::Number()));
  if (type.IsNone()) return type;
  if (type.Is(Type::NaN())) return singleton_NaN_string_;
  if (type.Is(cache_->kZeroOrMinusZero)) return singleton_zero_string_;
  return Type::String();
}

Type OperationTyper::NumberToUint32(Type type) {
  DCHECK(type.Is(Type::Number()));

  if (type.Is(Type::Unsigned32())) return type;
  if (type.Is(cache_->kZeroish)) return cache_->kSingletonZero;
  if (type.Is(unsigned32ish_)) {
    return Type::Intersect(Type::Union(type, cache_->kSingletonZero, zone()),
                           Type::Unsigned32(), zone());
  }
  return Type::Unsigned32();
}

Type OperationTyper::NumberToUint8Clamped(Type type) {
  DCHECK(type.Is(Type::Number()));

  if (type.Is(cache_->kUint8)) return type;
  return cache_->kUint8;
}

Type OperationTyper::Integral32OrMinusZeroToBigInt(Type type) {
  DCHECK(type.Is(Type::Number()));

  if (type.Is(Type::Unsigned32OrMinusZero())) {
    return Type::UnsignedBigInt63();
  }
  if (type.Is(Type::Signed32OrMinusZero())) {
    return Type::SignedBigInt64();
  }
  return Type::BigInt();
}

Type OperationTyper::NumberSilenceNaN(Type type) {
  DCHECK(type.Is(Type::Number()));
  // TODO(jarin): This is a terrible hack; we definitely need a dedicated type
  // for the hole (tagged and/or double). Otherwise if the input is the hole
  // NaN constant, we'd just eliminate this node in JSTypedLowering.
  if (type.Maybe(Type::NaN())) return Type::Number();
  return type;
}

Type OperationTyper::SpeculativeBigIntAsIntN(Type) {
  return Type::SignedBigInt64();
}

Type OperationTyper::SpeculativeBigIntAsUintN(Type) {
  return Type::UnsignedBigInt64();
}

Type OperationTyper::CheckBigInt(Type type) { return Type::BigInt(); }

Type OperationTyper::CheckedBigIntToBigInt64(Type type) {
  return Type::SignedBigInt64();
}

Type OperationTyper::NumberAdd(Type lhs, Type rhs) {
  DCHECK(lhs.Is(Type::Number()));
  DCHECK(rhs.Is(Type::Number()));

  if (lhs.IsNone() || rhs.IsNone()) return Type::None();

  // Addition can return NaN if either input can be NaN or we try to compute
  // the sum of two infinities of opposite sign.
  bool maybe_nan = lhs.Maybe(Type::NaN()) || rhs.Maybe(Type::NaN());

  // Addition can yield minus zero only if both inputs can be minus zero.
  bool maybe_minuszero = true;
  if (lhs.Maybe(Type::MinusZero())) {
    lhs = Type::Union(lhs, cache_->kSingletonZero, zone());
  } else {
    maybe_minuszero = false;
  }
  if (rhs.Maybe(Type::MinusZero())) {
    rhs = Type::Union(rhs, cache_->kSingletonZero, zone());
  } else {
    maybe_minuszero = false;
  }

  // We can give more precise types for integers.
  Type type = Type::None();
  lhs = Type::Intersect(lhs, Type::PlainNumber(), zone());
  rhs = Type::Intersect(rhs, Type::PlainNumber(), zone());
  if (!lhs.IsNone() && !rhs.IsNone()) {
    if (lhs.Is(cache_->kInteger) && rhs.Is(cache_->kInteger)) {
      type = AddRanger(lhs.Min(), lhs.Max(), rhs.Min(), rhs.Max());
    } else {
      if ((lhs.Maybe(minus_infinity_) && rhs.Maybe(infinity_)) ||
          (rhs.Maybe(minus_infinity_) && lhs.Maybe(infinity_))) {
        maybe_nan = true;
      }
      type = Type::PlainNumber();
    }
  }

  // Take into account the -0 and NaN information computed earlier.
  if (maybe_minuszero) type = Type::Union(type, Type::MinusZero(), zone());
  if (maybe_nan) type = Type::Union(type, Type::NaN(), zone());
  return type;
}

Type OperationTyper::NumberSubtract(Type lhs, Type rhs) {
  DCHECK(lhs.Is(Type::Number()));
  DCHECK(rhs.Is(Type::Number()));

  if (lhs.IsNone() || rhs.IsNone()) return Type::None();

  // Subtraction can return NaN if either input can be NaN or we try to
  // compute the sum of two infinities of opposite sign.
  bool maybe_nan = lhs.Maybe(Type::NaN()) || rhs.Maybe(Type::NaN());

  // Subtraction can yield minus zero if {lhs} can be minus zero and {rhs}
  // can be zero.
  bool maybe_minuszero = false;
  if (lhs.Maybe(Type::MinusZero())) {
    lhs = Type::Union(lhs, cache_->kSingletonZero, zone());
    maybe_minuszero = rhs.Maybe(cache_->kSingletonZero);
  }
  if (rhs.Maybe(Type::MinusZero())) {
    rhs = Type::Union(rhs, cache_->kSingletonZero, zone());
  }

  // We can give more precise types for integers.
  Type type = Type::None();
  lhs = Type::Intersect(lhs, Type::PlainNumber(), zone());
  rhs = Type::Intersect(rhs, Type::PlainNumber(), zone());
  if (!lhs.IsNone() && !rhs.IsNone()) {
    if (lhs.Is(cache_->kInteger) && rhs.Is(cache_->kInteger)) {
      type = SubtractRanger(lhs.Min(), lhs.Max(), rhs.Min(), rhs.Max());
    } else {
      if ((lhs.Maybe(infinity_) && rhs.Maybe(infinity_)) ||
          (rhs.Maybe(minus_infinity_) && lhs.Maybe(minus_infinity_))) {
        maybe_nan = true;
      }
      type = Type::PlainNumber();
    }
  }

  // Take into account the -0 and NaN information computed earlier.
  if (maybe_minuszero) type = Type::Union(type, Type::MinusZero(), zone());
  if (maybe_nan) type = Type::Union(type, Type::NaN(), zone());
  return type;
}

Type OperationTyper::SpeculativeSafeIntegerAdd(Type lhs, Type rhs) {
  Type result = SpeculativeNumberAdd(lhs, rhs);
  // If we have a Smi or Int32 feedback, the representation selection will
  // either truncate or it will check the inputs (i.e., deopt if not int32).
  // In either case the result will be in the safe integer range, so we
  // can bake in the type here. This needs to be in sync with
  // SimplifiedLowering::VisitSpeculativeAdditiveOp.
  return Type::Intersect(result, cache_->kSafeIntegerOrMinusZero, zone());
}

Type OperationTyper::SpeculativeSafeIntegerSubtract(Type lhs, Type rhs) {
  Type result = SpeculativeNumberSubtract(lhs, rhs);
  // If we have a Smi or Int32 feedback, the representation selection will
  // either truncate or it will check the inputs (i.e., deopt if not int32).
  // In either case the result will be in the safe integer range, so we
  // can bake in the type here. This needs to be in sync with
  // SimplifiedLowering::VisitSpeculativeAdditiveOp.
  return Type::Intersect(result, cache_->kSafeIntegerOrMinusZero, zone());
}

Type OperationTyper::NumberMultiply(Type lhs, Type rhs) {
  DCHECK(lhs.Is(Type::Number()));
  DCHECK(rhs.Is(Type::Number()));

  if (lhs.IsNone() || rhs.IsNone()) return Type::None();
  if (lhs.Is(Type::NaN()) || rhs.Is(Type::NaN())) return Type::NaN();

  // Multiplication propagates NaN:
  //   NaN * x = NaN         (regardless of sign of x)
  //   0 * Infinity = NaN    (regardless of signs)
  bool maybe_nan = lhs.Maybe(Type::NaN()) || rhs.Maybe(Type::NaN()) ||
                   (lhs.Maybe(cache_->kZeroish) &&
                    (rhs.Min() == -V8_INFINITY || rhs.Max() == V8_INFINITY)) ||
                   (rhs.Maybe(cache_->kZeroish) &&
                    (lhs.Min() == -V8_INFINITY || lhs.Max() == V8_INFINITY));
  lhs = Type::Intersect(lhs, Type::OrderedNumber(), zone());
  DCHECK(!lhs.IsNone());
  rhs = Type::Intersect(rhs, Type::OrderedNumber(), zone());
  DCHECK(!rhs.IsNone());

  // Try to rule out -0.
  bool maybe_minuszero = lhs.Maybe(Type::MinusZero()) ||
                         rhs.Maybe(Type::MinusZero()) ||
                         (lhs.Maybe(cache_->kZeroish) && rhs.Min() < 0.0) ||
                         (rhs.Maybe(cache_->kZeroish) && lhs.Min() < 0.0);
  if (lhs.Maybe(Type::MinusZero())) {
    lhs = Type::Union(lhs, cache_->kSingletonZero, zone());
    lhs = Type::Intersect(lhs, Type::PlainNumber(), zone());
  }
  if (rhs.Maybe(Type::MinusZero())) {
    rhs = Type::Union(rhs, cache_->kSingletonZero, zone());
    rhs = Type::Intersect(rhs, Type::PlainNumber(), zone());
  }

  // Compute the effective type, utilizing range information if possible.
  Type type = (lhs.Is(cache_->kInteger) && rhs.Is(cache_->kInteger))
                  ? MultiplyRanger(lhs.Min(), lhs.Max(), rhs.Min(), rhs.Max())
                  : Type::OrderedNumber();

  // Take into account the -0 and NaN information computed earlier.
  if (maybe_minuszero) type = Type::Union(type, Type::MinusZero(), zone());
  if (maybe_nan) type = Type::Union(type, Type::NaN(), zone());
  return type;
}

Type OperationTyper::NumberDivide(Type lhs, Type rhs) {
  DCHECK(lhs.Is(Type::Number()));
  DCHECK(rhs.Is(Type::Number()));

  if (lhs.IsNone() || rhs.IsNone()) return Type::None();
  if (lhs.Is(Type::NaN()) || rhs.Is(Type::NaN())) return Type::NaN();

  // Division is tricky, so all we do is try ruling out -0 and NaN.
  bool maybe_nan = lhs.Maybe(Type::NaN()) || rhs.Maybe(cache_->kZeroish) ||
                   ((lhs.Min() == -V8_INFINITY || lhs.Max() == +V8_INFINITY) &&
                    (rhs.Min() == -V8_INFINITY || rhs.Max() == +V8_INFINITY));
  lhs = Type::Intersect(lhs, Type::OrderedNumber(), zone());
  DCHECK(!lhs.IsNone());
  rhs = Type::Intersect(rhs, Type::OrderedNumber(), zone());
  DCHECK(!rhs.IsNone());

  // Try to rule out -0.
  bool maybe_minuszero =
      !lhs.Is(cache_->kInteger) ||
      (lhs.Maybe(cache_->kZeroish) && rhs.Min() < 0.0) ||
      (rhs.Min() == -V8_INFINITY || rhs.Max() == +V8_INFINITY);

  // Take into account the -0 and NaN information computed earlier.
  Type type = Type::PlainNumber();
  if (maybe_minuszero) type = Type::Union(type, Type::MinusZero(), zone());
  if (maybe_nan) type = Type::Union(type, Type::NaN(), zone());
  return type;
}

Type OperationTyper::NumberModulus(Type lhs, Type rhs) {
  DCHECK(lhs.Is(Type::Number()));
  DCHECK(rhs.Is(Type::Number()));

  if (lhs.IsNone() || rhs.IsNone()) return Type::None();

  // Modulus can yield NaN if either {lhs} or {rhs} are NaN, or
  // {lhs} is not finite, or the {rhs} is a zero value.
  bool maybe_nan = lhs.Maybe(Type::NaN()) || rhs.Maybe(cache_->kZeroish) ||
                   lhs.Min() == -V8_INFINITY || lhs.Max() == +V8_INFINITY;

  // Deal with -0 inputs, only the signbit of {lhs} matters for the result.
  bool maybe_minuszero = false;
  if (lhs.Maybe(Type::MinusZero())) {
    maybe_minuszero = true;
    lhs = Type::Union(lhs, cache_->kSingletonZero, zone());
  }
  if (rhs.Maybe(Type::MinusZero())) {
    rhs = Type::Union(rhs, cache_->kSingletonZero, zone());
  }

  // Rule out NaN and -0, and check what we can do with the remaining type info.
  Type type = Type::None();
  lhs = Type::Intersect(lhs, Type::PlainNumber(), zone());
  rhs = Type::Intersect(rhs, Type::PlainNumber(), zone());

  // We can only derive a meaningful type if both {lhs} and {rhs} are inhabited,
  // and the {rhs} is not 0, otherwise the result is NaN independent of {lhs}.
  if (!lhs.IsNone() && !rhs.Is(cache_->kSingletonZero)) {
    // Determine the bounds of {lhs} and {rhs}.
    double const lmin = lhs.Min();
    double const lmax = lhs.Max();
    double const rmin = rhs.Min();
    double const rmax = rhs.Max();

    // The sign of the result is the sign of the {lhs}.
    if (lmin < 0.0) maybe_minuszero = true;

    // For integer inputs {lhs} and {rhs} we can infer a precise type.
    if (lhs.Is(cache_->kInteger) && rhs.Is(cache_->kInteger)) {
      double labs = std::max(std::abs(lmin), std::abs(lmax));
      double rabs = std::max(std::abs(rmin), std::abs(rmax)) - 1;
      double abs = std::min(labs, rabs);
      double min = 0.0, max = 0.0;
      if (lmin >= 0.0) {
        // {lhs} positive.
        min = 0.0;
        max = abs;
      } else if (lmax <= 0.0) {
        // {lhs} negative.
        min = 0.0 - abs;
        max = 0.0;
      } else {
        // {lhs} positive or negative.
        min = 0.0 - abs;
        max = abs;
      }
      type = Type::Range(min, max, zone());
    } else {
      type = Type::PlainNumber();
    }
  }

  // Take into account the -0 and NaN information computed earlier.
  if (maybe_minuszero) type = Type::Union(type, Type::MinusZero(), zone());
  if (maybe_nan) type = Type::Union(type, Type::NaN(), zone());
  return type;
}

Type OperationTyper::NumberBitwiseOr(Type lhs, Type rhs) {
  DCHECK(lhs.Is(Type::Number()));
  DCHECK(rhs.Is(Type::Number()));

  lhs = NumberToInt32(lhs);
  rhs = NumberToInt32(rhs);

  if (lhs.IsNone() || rhs.IsNone()) return Type::None();

  double lmin = lhs.Min();
  double rmin = rhs.Min();
  double lmax = lhs.Max();
  double rmax = rhs.Max();
  // Or-ing any two values results in a value no smaller than their minimum.
  // Even no smaller than their maximum if both values are non-negative.
  double min =
      lmin >= 0 && rmin >= 0 ? std::max(lmin, rmin) : std::min(lmin, rmin);
  double max = kMaxInt;

  // Or-ing with 0 is essentially a conversion to int32.
  if (rmin == 0 && rmax == 0) {
    min = lmin;
    max = lmax;
  }
  if (lmin == 0 && lmax == 0) {
    min = rmin;
    max = rmax;
  }

  if (lmax < 0 || rmax < 0) {
    // Or-ing two values of which at least one is negative results in a negative
    // value.
    max = std::min(max, -1.0);
  }
  return Type::Range(min, max, zone());
}

Type OperationTyper::NumberBitwiseAnd(Type lhs, Type rhs) {
  DCHECK(lhs.Is(Type::Number()));
  DCHECK(rhs.Is(Type::Number()));

  lhs = NumberToInt32(lhs);
  rhs = NumberToInt32(rhs);

  if (lhs.IsNone() || rhs.IsNone()) return Type::None();

  double lmin = lhs.Min();
  double rmin = rhs.Min();
  double lmax = lhs.Max();
  double rmax = rhs.Max();
  double min = kMinInt;
  // And-ing any two values results in a value no larger than their maximum.
  // Even no larger than their minimum if both values are non-negative.
  double max =
      lmin >= 0 && rmin >= 0 ? std::min(lmax, rmax) : std::max(lmax, rmax);
  // And-ing with a non-negative value x causes the result to be between
  // zero and x.
  if (lmin >= 0) {
    min = 0;
    max = std::min(max, lmax);
  }
  if (rmin >= 0) {
    min = 0;
    max = std::min(max, rmax);
  }
  return Type::Range(min, max, zone());
}

Type OperationTyper::NumberBitwiseXor(Type lhs, Type rhs) {
  DCHECK(lhs.Is(Type::Number()));
  DCHECK(rhs.Is(Type::Number()));

  lhs = NumberToInt32(lhs);
  rhs = NumberToInt32(rhs);

  if (lhs.IsNone() || rhs.IsNone()) return Type::None();

  double lmin = lhs.Min();
  double rmin = rhs.Min();
  double lmax = lhs.Max();
  double rmax = rhs.Max();
  if ((lmin >= 0 && rmin >= 0) || (lmax < 0 && rmax < 0)) {
    // Xor-ing negative or non-negative values results in a non-negative value.
    return Type::Unsigned31();
  }
  if ((lmax < 0 && rmin >= 0) || (lmin >= 0 && rmax < 0)) {
    // Xor-ing a negative and a non-negative value results in a negative value.
    // TODO(jarin) Use a range here.
    return Type::Negative32();
  }
  return Type::Signed32();
}

Type OperationTyper::NumberShiftLeft(Type lhs, Type rhs) {
  DCHECK(lhs.Is(Type::Number()));
  DCHECK(rhs.Is(Type::Number()));

  lhs = NumberToInt32(lhs);
  rhs = NumberToUint32(rhs);

  if (lhs.IsNone() || rhs.IsNone()) return Type::None();

  int32_t min_lhs = lhs.Min();
  int32_t max_lhs = lhs.Max();
  uint32_t min_rhs = rhs.Min();
  uint32_t max_rhs = rhs.Max();
  if (max_rhs > 31) {
    // rhs can be larger than the bitmask
    max_rhs = 31;
    min_rhs = 0;
  }

  if (max_lhs > (kMaxInt >> max_rhs) || min_lhs < (kMinInt >> max_rhs)) {
    // overflow possible
    return Type::Signed32();
  }

  double min =
      std::min(static_cast<int32_t>(static_cast<uint32_t>(min_lhs) << min_rhs),
               static_cast<int32_t>(static_cast<uint32_t>(min_lhs) << max_rhs));
  double max =
      std::max(static_cast<int32_t>(static_cast<uint32_t>(max_lhs) << min_rhs),
               static_cast<int32_t>(static_cast<uint32_t>(max_lhs) << max_rhs));

  if (max == kMaxInt && min == kMinInt) return Type::Signed32();
  return Type::Range(min, max, zone());
}

Type OperationTyper::NumberShiftRight(Type lhs, Type rhs) {
  DCHECK(lhs.Is(Type::Number()));
  DCHECK(rhs.Is(Type::Number()));

  lhs = NumberToInt32(lhs);
  rhs = NumberToUint32(rhs);

  if (lhs.IsNone() || rhs.IsNone()) return Type::None();

  int32_t min_lhs = lhs.Min();
  int32_t max_lhs = lhs.Max();
  uint32_t min_rhs = rhs.Min();
  uint32_t max_rhs = rhs.Max();
  if (max_rhs > 31) {
    // rhs can be larger than the bitmask
    max_rhs = 31;
    min_rhs = 0;
  }
  double min = std::min(min_lhs >> min_rhs, min_lhs >> max_rhs);
  double max = std::max(max_lhs >> min_rhs, max_lhs >> max_rhs);

  if (max == kMaxInt && min == kMinInt) return Type::Signed32();
  return Type::Range(min, max, zone());
}

Type OperationTyper::NumberShiftRightLogical(Type lhs, Type rhs) {
  DCHECK(lhs.Is(Type::Number()));
  DCHECK(rhs.Is(Type::Number()));

  lhs = NumberToUint32(lhs);
  rhs = NumberToUint32(rhs);

  if (lhs.IsNone() || rhs.IsNone()) return Type::None();

  uint32_t min_lhs = lhs.Min();
  uint32_t max_lhs = lhs.Max();
  uint32_t min_rhs = rhs.Min();
  uint32_t max_rhs = rhs.Max();
  if (max_rhs > 31) {
    // rhs can be larger than the bitmask
    max_rhs = 31;
    min_rhs = 0;
  }

  double min = min_lhs >> max_rhs;
  double max = max_lhs >> min_rhs;
  DCHECK_LE(0, min);
  DCHECK_LE(max, kMaxUInt32);

  if (min == 0 && max == kMaxInt) return Type::Unsigned31();
  if (min == 0 && max == kMaxUInt32) return Type::Unsigned32();
  return Type::Range(min, max, zone());
}

Type OperationTyper::NumberAtan2(Type lhs, Type rhs) {
  DCHECK(lhs.Is(Type::Number()));
  DCHECK(rhs.Is(Type::Number()));
  return Type::Number();
}

Type OperationTyper::NumberImul(Type lhs, Type rhs) {
  DCHECK(lhs.Is(Type::Number()));
  DCHECK(rhs.Is(Type::Number()));
  // TODO(turbofan): We should be able to do better here.
  return Type::Signed32();
}

Type OperationTyper::NumberMax(Type lhs, Type rhs) {
  DCHECK(lhs.Is(Type::Number()));
  DCHECK(rhs.Is(Type::Number()));

  if (lhs.IsNone() || rhs.IsNone()) return Type::None();
  if (lhs.Is(Type::NaN()) || rhs.Is(Type::NaN())) return Type::NaN();

  Type type = Type::None();
  if (lhs.Maybe(Type::NaN()) || rhs.Maybe(Type::NaN())) {
    type = Type::Union(type, Type::NaN(), zone());
  }
  if (lhs.Maybe(Type::MinusZero()) || rhs.Maybe(Type::MinusZero())) {
    type = Type::Union(type, Type::MinusZero(), zone());
    // In order to ensure monotonicity of the computation below, we additionally
    // pretend +0 is present (for simplicity on both sides).
    lhs = Type::Union(lhs, cache_->kSingletonZero, zone());
    rhs = Type::Union(rhs, cache_->kSingletonZero, zone());
  }
  if (!lhs.Is(cache_->kIntegerOrMinusZeroOrNaN) ||
      !rhs.Is(cache_->kIntegerOrMinusZeroOrNaN)) {
    return Type::Union(type, Type::Union(lhs, rhs, zone()), zone());
  }

  lhs = Type::Intersect(lhs, cache_->kInteger, zone());
  rhs = Type::Intersect(rhs, cache_->kInteger, zone());
  DCHECK(!lhs.IsNone());
  DCHECK(!rhs.IsNone());

  double min = std::max(lhs.Min(), rhs.Min());
  double max = std::max(lhs.Max(), rhs.Max());
  type = Type::Union(type, Type::Range(min, max, zone()), zone());

  return type;
}

Type OperationTyper::NumberMin(Type lhs, Type rhs) {
  DCHECK(lhs.Is(Type::Number()));
  DCHECK(rhs.Is(Type::Number()));

  if (lhs.IsNone() || rhs.IsNone()) return Type::None();
  if (lhs.Is(Type::NaN()) || rhs.Is(Type::NaN())) return Type::NaN();

  Type type = Type::None();
  if (lhs.Maybe(Type::NaN()) || rhs.Maybe(Type::NaN())) {
    type = Type::Union(type, Type::NaN(), zone());
  }
  if (lhs.Maybe(Type::MinusZero()) || rhs.Maybe(Type::MinusZero())) {
    type = Type::Union(type, Type::MinusZero(), zone());
    // In order to ensure monotonicity of the computation below, we additionally
    // pretend +0 is present (for simplicity on both sides).
    lhs = Type::Union(lhs, cache_->kSingletonZero, zone());
    rhs = Type::Union(rhs, cache_->kSingletonZero, zone());
  }
  if (!lhs.Is(cache_->kIntegerOrMinusZeroOrNaN) ||
      !rhs.Is(cache_->kIntegerOrMinusZeroOrNaN)) {
    return Type::Union(type, Type::Union(lhs, rhs, zone()), zone());
  }

  lhs = Type::Intersect(lhs, cache_->kInteger, zone());
  rhs = Type::Intersect(rhs, cache_->kInteger, zone());
  DCHECK(!lhs.IsNone());
  DCHECK(!rhs.IsNone());

  double min = std::min(lhs.Min(), rhs.Min());
  double max = std::min(lhs.Max(), rhs.Max());
  type = Type::Union(type, Type::Range(min, max, zone()), zone());

  return type;
}

Type OperationTyper::NumberPow(Type lhs, Type rhs) {
  DCHECK(lhs.Is(Type::Number()));
  DCHECK(rhs.Is(Type::Number()));
  // TODO(turbofan): We should be able to do better here.
  return Type::Number();
}

#define SPECULATIVE_NUMBER_BINOP(Name)                         \
  Type OperationTyper::Speculative##Name(Type lhs, Type rhs) { \
    lhs = SpeculativeToNumber(lhs);                            \
    rhs = SpeculativeToNumber(rhs);                            \
    return Name(lhs, rhs);                                     \
  }
SPECULATIVE_NUMBER_BINOP(NumberAdd)
SPECULATIVE_NUMBER_BINOP(NumberSubtract)
SPECULATIVE_NUMBER_BINOP(NumberMultiply)
SPECULATIVE_NUMBER_BINOP(NumberPow)
SPECULATIVE_NUMBER_BINOP(NumberDivide)
SPECULATIVE_NUMBER_BINOP(NumberModulus)
SPECULATIVE_NUMBER_BINOP(NumberBitwiseOr)
SPECULATIVE_NUMBER_BINOP(NumberBitwiseAnd)
SPECULATIVE_NUMBER_BINOP(NumberBitwiseXor)
SPECULATIVE_NUMBER_BINOP(NumberShiftLeft)
SPECULATIVE_NUMBER_BINOP(NumberShiftRight)
SPECULATIVE_NUMBER_BINOP(NumberShiftRightLogical)
#undef SPECULATIVE_NUMBER_BINOP

#define MACHINE_BINOP(Name) \
  Type OperationTyper::Name(Type, Type) { return Type::Machine(); }
TYPER_SUPPORTED_MACHINE_BINOP_LIST(MACHINE_BINOP)
#undef MACHINE_BINOP

Type OperationTyper::ChangeUint32ToUint64(Type input) {
  return Type::Machine();
}

#define BIGINT_BINOP(Name)                                 \
  Type OperationTyper::Name(Type lhs, Type rhs) {          \
    DCHECK(lhs.Is(Type::BigInt()));                        \
    DCHECK(rhs.Is(Type::BigInt()));                        \
    if (lhs.IsNone() || rhs.IsNone()) return Type::None(); \
    return Type::BigInt();                                 \
  }
SIMPLIFIED_BIGINT_BINOP_LIST(BIGINT_BINOP)
#undef BIGINT_BINOP

Type OperationTyper::BigIntNegate(Type type) {
  DCHECK(type.Is(Type::BigInt()));

  if (type.IsNone()) return type;
  return Type::BigInt();
}

#define SPECULATIVE_BIGINT_BINOP(Name)                     \
  Type OperationTyper::Name(Type lhs, Type rhs) {          \
    if (lhs.IsNone() || rhs.IsNone()) return Type::None(); \
    return Type::BigInt();                                 \
  }
SIMPLIFIED_SPECULATIVE_BIGINT_BINOP_LIST(SPECULATIVE_BIGINT_BINOP)
#undef SPECULATIVE_BIGINT_BINOP

Type OperationTyper::SpeculativeBigIntNegate(Type type) {
  if (type.IsNone()) return type;
  return Type::BigInt();
}

Type OperationTyper::SpeculativeToBigInt(Type type) {
  return ToBigInt(Type::Intersect(type, Type::BigInt(), zone()));
}

Type OperationTyper::SpeculativeToNumber(Type type) {
  return ToNumber(Type::Intersect(type, Type::NumberOrOddball(), zone()));
}

Type OperationTyper::ToPrimitive(Type type) {
  if (type.Is(Type::Primitive())) {
    return type;
  }
  return Type::Primitive();
}

Type OperationTyper::Invert(Type type) {
  DCHECK(type.Is(Type::Boolean()));
  CHECK(!type.IsNone());
  if (type.Is(singleton_false())) return singleton_true();
  if (type.Is(singleton_true())) return singleton_false();
  return type;
}

OperationTyper::ComparisonOutcome OperationTyper::Invert(
    ComparisonOutcome outcome) {
  ComparisonOutcome result(0);
  if ((outcome & kComparisonUndefined) != 0) result |= kComparisonUndefined;
  if ((outcome & kComparisonTrue) != 0) result |= kComparisonFalse;
  if ((outcome & kComparisonFalse) != 0) result |= kComparisonTrue;
  return result;
}

Type OperationTyper::FalsifyUndefined(ComparisonOutcome outcome) {
  if ((outcome & kComparisonFalse) != 0 ||
      (outcome & kComparisonUndefined) != 0) {
    return (outcome & kComparisonTrue) != 0 ? Type::Boolean()
                                            : singleton_false();
  }
  // Type should be non empty, so we know it should be true.
  DCHECK_NE(0, outcome & kComparisonTrue);
  return singleton_true();
}

namespace {

Type JSType(Type type) {
  if (type.Is(Type::Boolean())) return Type::Boolean();
  if (type.Is(Type::String())) return Type::String();
  if (type.Is(Type::Number())) return Type::Number();
  if (type.Is(Type::BigInt())) return Type::BigInt();
  if (type.Is(Type::Undefined())) return Type::Undefined();
  if (type.Is(Type::Null())) return Type::Null();
  if (type.Is(Type::Symbol())) return Type::Symbol();
  if (type.Is(Type::Receiver())) return Type::Receiver();  // JS "Object"
  return Type::Any();
}

}  // namespace

Type OperationTyper::SameValue(Type lhs, Type rhs) {
  if (!JSType(lhs).Maybe(JSType(rhs))) return singleton_false();
  if (lhs.Is(Type::NaN())) {
    if (rhs.Is(Type::NaN())) return singleton_true();
    if (!rhs.Maybe(Type::NaN())) return singleton_false();
  } else if (rhs.Is(Type::NaN())) {
    if (!lhs.Maybe(Type::NaN())) return singleton_false();
  }
  if (lhs.Is(Type::MinusZero())) {
    if (rhs.Is(Type::MinusZero())) return singleton_true();
    if (!rhs.Maybe(Type::MinusZero())) return singleton_false();
  } else if (rhs.Is(Type::MinusZero())) {
    if (!lhs.Maybe(Type::MinusZero())) return singleton_false();
  }
  if (lhs.Is(Type::OrderedNumber()) && rhs.Is(Type::OrderedNumber()) &&
      (lhs.Max() < rhs.Min() || lhs.Min() > rhs.Max())) {
    return singleton_false();
  }
  return Type::Boolean();
}

Type OperationTyper::SameValueNumbersOnly(Type lhs, Type rhs) {
  // SameValue and SamevalueNumbersOnly only differ in treatment of
  // strings and biginits. Since the SameValue typer does not do anything
  // special about strings or bigints, we can just use it here.
  return SameValue(lhs, rhs);
}

Type OperationTyper::StrictEqual(Type lhs, Type rhs) {
  CHECK(!lhs.IsNone());
  CHECK(!rhs.IsNone());
  if (!JSType(lhs).Maybe(JSType(rhs))) return singleton_false();
  if (lhs.Is(Type::NaN()) || rhs.Is(Type::NaN())) return singleton_false();
  if (lhs.Is(Type::Number()) && rhs.Is(Type::Number()) &&
      (lhs.Max() < rhs.Min() || lhs.Min() > rhs.Max())) {
    return singleton_false();
  }
  if (lhs.IsSingleton() && rhs.Is(lhs)) {
    // Types are equal and are inhabited only by a single semantic value,
    // which is not nan due to the earlier check.
    DCHECK(lhs.Is(rhs));
    return singleton_true();
  }
  if ((lhs.Is(Type::Unique()) || rhs.Is(Type::Unique())) && !lhs.Maybe(rhs)) {
    // One of the inputs has a canonical representation but types don't overlap.
    return singleton_false();
  }
  return Type::Boolean();
}

Type OperationTyper::CheckBounds(Type index, Type length) {
  DCHECK(length.Is(cache_->kPositiveSafeInteger));
  if (length.Is(cache_->kSingletonZero)) return Type::None();
  Type const upper_bound = Type::Range(0.0, length.Max() - 1, zone());
  if (index.Maybe(Type::String())) return upper_bound;
  if (index.Maybe(Type::MinusZero())) {
    index = Type::Union(index, cache_->kSingletonZero, zone());
  }
  return Type::Intersect(index, upper_bound, zone());
}

Type OperationTyper::CheckFloat64Hole(Type type) {
  if (type.Maybe(Type::Hole())) {
    // Turn a "hole" into undefined.
    type = Type::Intersect(type, Type::Number(), zone());
    type = Type::Union(type, Type::Undefined(), zone());
  }
  return type;
}

Type OperationTyper::CheckNumber(Type type) {
  return Type::Intersect(type, Type::Number(), zone());
}

Type OperationTyper::TypeTypeGuard(const Operator* sigma_op, Type input) {
  return Type::Intersect(input, TypeGuardTypeOf(sigma_op), zone());
}

Type OperationTyper::ConvertTaggedHoleToUndefined(Type input) {
  if (input.Maybe(Type::Hole())) {
    // Turn a "hole" into undefined.
    Type type = Type::Intersect(input, Type::NonInternal(), zone());
    return Type::Union(type, Type::Undefined(), zone());
  }
  return input;
}

Type OperationTyper::ToBoolean(Type type) {
  if (type.Is(Type::Boolean())) return type;
  if (type.Is(falsish_)) return singleton_false_;
  if (type.Is(truish_)) return singleton_true_;
  if (type.Is(Type::Number())) {
    return NumberToBoolean(type);
  }
  return Type::Boolean();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
