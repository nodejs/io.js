// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_TYPER_H_
#define V8_COMPILER_TURBOSHAFT_TYPER_H_

#include <limits>

#include "src/base/logging.h"
#include "src/base/vector.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/turboshaft/types.h"

namespace v8::internal::compiler::turboshaft {

// Returns the array's least element, ignoring NaN.
// There must be at least one non-NaN element.
// Any -0 is converted to 0.
template <typename T, size_t N>
T array_min(const std::array<T, N>& a) {
  DCHECK_NE(0, N);
  T x = +std::numeric_limits<T>::infinity();
  for (size_t i = 0; i < N; ++i) {
    if (!std::isnan(a[i])) {
      x = std::min(a[i], x);
    }
  }
  DCHECK(!std::isnan(x));
  return x == T{0} ? T{0} : x;  // -0 -> 0
}

// Returns the array's greatest element, ignoring NaN.
// There must be at least one non-NaN element.
// Any -0 is converted to 0.
template <typename T, size_t N>
T array_max(const std::array<T, N>& a) {
  DCHECK_NE(0, N);
  T x = -std::numeric_limits<T>::infinity();
  for (size_t i = 0; i < N; ++i) {
    if (!std::isnan(a[i])) {
      x = std::max(a[i], x);
    }
  }
  DCHECK(!std::isnan(x));
  return x == T{0} ? T{0} : x;  // -0 -> 0
}

template <size_t Bits>
struct WordOperationTyper {
  static_assert(Bits == 32 || Bits == 64);
  using word_t = uint_type<Bits>;
  using type_t = WordType<Bits>;
  using ElementsVector = base::SmallVector<word_t, type_t::kMaxSetSize * 2>;
  static constexpr word_t max = std::numeric_limits<word_t>::max();

  static type_t FromElements(ElementsVector elements, Zone* zone) {
    base::sort(elements);
    auto it = std::unique(elements.begin(), elements.end());
    elements.pop_back(std::distance(it, elements.end()));
    DCHECK(!elements.empty());
    if (elements.size() <= type_t::kMaxSetSize) {
      return type_t::Set(elements, zone);
    }

    auto range =
        MakeRange(base::Vector<const word_t>{elements.data(), elements.size()});
    auto result = type_t::Range(range.first, range.second, zone);
    DCHECK(
        base::all_of(elements, [&](word_t e) { return result.Contains(e); }));
    return result;
  }

  static std::pair<word_t, word_t> MakeRange(const type_t& t) {
    if (t.is_range()) return t.range();
    DCHECK(t.is_set());
    return MakeRange(t.set_elements());
  }

  // This function tries to find a somewhat reasonable range for a given set of
  // values. If the elements span no more than half of the range, we just
  // construct the range from min(elements) to max(elements) Otherwise, we
  // consider a wrapping range because it is likely that there is a larger gap
  // in the middle of the elements. For that, we start with a wrapping range
  // from max(elements) to min(elements) and then incrementally add another
  // element either by increasing the 'to' or decreasing the 'from' of the
  // range, whichever leads to a smaller range.
  static std::pair<word_t, word_t> MakeRange(
      base::Vector<const word_t> elements) {
    DCHECK(!elements.empty());
    DCHECK(detail::is_unique_and_sorted(elements));
    if (elements[elements.size() - 1] - elements[0] <= max / 2) {
      // Construct a non-wrapping range.
      return {elements[0], elements[elements.size() - 1]};
    }
    // Construct a wrapping range.
    size_t from_index = elements.size() - 1;
    size_t to_index = 0;
    while (to_index + 1 < from_index) {
      if ((elements[to_index + 1] - elements[to_index]) <
          (elements[from_index] - elements[from_index - 1])) {
        ++to_index;
      } else {
        --from_index;
      }
    }
    return {elements[from_index], elements[to_index]};
  }

  static word_t distance(const std::pair<word_t, word_t>& range) {
    return distance(range.first, range.second);
  }
  static word_t distance(word_t from, word_t to) {
    return is_wrapping(from, to) ? (max - from + to) : to - from;
  }

  static bool is_wrapping(const std::pair<word_t, word_t>& range) {
    return is_wrapping(range.first, range.second);
  }
  static bool is_wrapping(word_t from, word_t to) { return from > to; }

  static type_t Add(const type_t& lhs, const type_t& rhs, Zone* zone) {
    if (lhs.is_any() || rhs.is_any()) return type_t::Any();

    // If both sides are decently small sets, we produce the product set (which
    // we convert to a range if it exceeds the set limit).
    if (lhs.is_set() && rhs.is_set()) {
      ElementsVector result_elements;
      for (int i = 0; i < lhs.set_size(); ++i) {
        for (int j = 0; j < rhs.set_size(); ++j) {
          result_elements.push_back(lhs.set_element(i) + rhs.set_element(j));
        }
      }
      return FromElements(std::move(result_elements), zone);
    }

    // Otherwise just construct a range.
    std::pair<word_t, word_t> x = MakeRange(lhs);
    std::pair<word_t, word_t> y = MakeRange(rhs);

    // If the result would not be a complete range, we compute it.
    // Check: (lhs.to - lhs.from + 1) + rhs.to - rhs.from < max
    // =====> (lhs.to - lhs.from + 1) < max - rhs.to + rhs.from
    // =====> (lhs.to - lhs.from + 1) < max - (rhs.to - rhs.from)
    if (distance(x) + 1 < max - distance(y)) {
      return type_t::Range(x.first + y.first, x.second + y.second, zone);
    }

    return type_t::Any();
  }

  static type_t Subtract(const type_t& lhs, const type_t& rhs, Zone* zone) {
    if (lhs.is_any() || rhs.is_any()) return type_t::Any();

    // If both sides are decently small sets, we produce the product set (which
    // we convert to a range if it exceeds the set limit).
    if (lhs.is_set() && rhs.is_set()) {
      ElementsVector result_elements;
      for (int i = 0; i < lhs.set_size(); ++i) {
        for (int j = 0; j < rhs.set_size(); ++j) {
          result_elements.push_back(lhs.set_element(i) - rhs.set_element(j));
        }
      }
      return FromElements(std::move(result_elements), zone);
    }

    // Otherwise just construct a range.
    std::pair<word_t, word_t> x = MakeRange(lhs);
    std::pair<word_t, word_t> y = MakeRange(rhs);

    if (!is_wrapping(x) && !is_wrapping(y)) {
      // If the result would not be a complete range, we compute it.
      // Check: (lhs.to - lhs.from + 1) + rhs.to - rhs.from < max
      // =====> (lhs.to - lhs.from + 1) < max - rhs.to + rhs.from
      // =====> (lhs.to - lhs.from + 1) < max - (rhs.to - rhs.from)
      if (distance(x) + 1 < max - distance(y)) {
        return type_t::Range(x.first - y.second, x.second - y.first, zone);
      }
    }

    // TODO(nicohartmann@): Improve the wrapping cases.
    return type_t::Any();
  }

  static Word32Type UnsignedLessThan(const type_t& lhs, const type_t& rhs,
                                     Zone* zone) {
    bool can_be_true = lhs.unsigned_min() < rhs.unsigned_max();
    bool can_be_false = lhs.unsigned_max() >= rhs.unsigned_min();

    if (!can_be_true) return Word32Type::Constant(0);
    if (!can_be_false) return Word32Type::Constant(1);
    return Word32Type::Set({0, 1}, zone);
  }

  static Word32Type UnsignedLessThanOrEqual(const type_t& lhs,
                                            const type_t& rhs, Zone* zone) {
    bool can_be_true = lhs.unsigned_min() <= rhs.unsigned_max();
    bool can_be_false = lhs.unsigned_max() > rhs.unsigned_min();

    if (!can_be_true) return Word32Type::Constant(0);
    if (!can_be_false) return Word32Type::Constant(1);
    return Word32Type::Set({0, 1}, zone);
  }

  // Computes the ranges to which the sides of the unsigned comparison (lhs <
  // rhs) can be restricted when the comparison is true. When the comparison is
  // true, we learn: lhs cannot be >= rhs.max and rhs cannot be <= lhs.min.
  static std::pair<Type, Type> RestrictionForUnsignedLessThan_True(
      const type_t& lhs, const type_t& rhs, Zone* zone) {
    Type restrict_lhs;
    if (rhs.unsigned_max() == 0) {
      // There is no value for lhs that could make (lhs < 0) true.
      restrict_lhs = Type::None();
    } else {
      restrict_lhs = type_t::Range(0, next_smaller(rhs.unsigned_max()), zone);
    }

    Type restrict_rhs;
    if (lhs.unsigned_min() == max) {
      // There is no value for rhs that could make (max < rhs) true.
      restrict_rhs = Type::None();
    } else {
      restrict_rhs = type_t::Range(next_larger(lhs.unsigned_min()), max, zone);
    }

    return {restrict_lhs, restrict_rhs};
  }

  // Computes the ranges to which the sides of the unsigned comparison (lhs <
  // rhs) can be restricted when the comparison is false. When the comparison is
  // false, we learn: lhs cannot be < rhs.min and rhs cannot be > lhs.max.
  static std::pair<Type, Type> RestrictionForUnsignedLessThan_False(
      const type_t& lhs, const type_t& rhs, Zone* zone) {
    return {type_t::Range(rhs.unsigned_min(), max, zone),
            type_t::Range(0, lhs.unsigned_max(), zone)};
  }

  // Computes the ranges to which the sides of the unsigned comparison (lhs <=
  // rhs) can be restricted when the comparison is true. When the comparison is
  // true, we learn: lhs cannot be > rhs.max and rhs cannot be < lhs.min.
  static std::pair<Type, Type> RestrictionForUnsignedLessThanOrEqual_True(
      const type_t& lhs, const type_t& rhs, Zone* zone) {
    return {type_t::Range(0, rhs.unsigned_max(), zone),
            type_t::Range(lhs.unsigned_min(), max, zone)};
  }

  // Computes the ranges to which the sides of the unsigned comparison (lhs <=
  // rhs) can be restricted when the comparison is false. When the comparison is
  // false, we learn: lhs cannot be <= rhs.min and rhs cannot be >= lhs.max.
  static std::pair<Type, Type> RestrictionForUnsignedLessThanOrEqual_False(
      const type_t& lhs, const type_t& rhs, Zone* zone) {
    Type restrict_lhs;
    if (rhs.unsigned_min() == max) {
      // There is no value for lhs that could make (lhs <= max) false.
      restrict_lhs = Type::None();
    } else {
      restrict_lhs = type_t::Range(next_larger(rhs.unsigned_min()), max, zone);
    }

    Type restrict_rhs;
    if (lhs.unsigned_max() == 0) {
      // There is no value for rhs that could make (0 <= rhs) false.
      restrict_rhs = Type::None();
    } else {
      restrict_rhs = type_t::Range(0, next_smaller(lhs.unsigned_max()), zone);
    }

    return {restrict_lhs, restrict_rhs};
  }

  // WidenMaximal widens one of the boundary to the extreme immediately.
  static type_t WidenMaximal(const type_t& old_type, const type_t& new_type,
                             Zone* zone) {
    if (new_type.is_any()) return new_type;
    if (old_type.is_wrapping() || new_type.is_wrapping()) return type_t::Any();

    word_t result_from = new_type.unsigned_min();
    if (result_from < old_type.unsigned_min()) result_from = 0;
    word_t result_to = new_type.unsigned_max();
    if (result_to > old_type.unsigned_max()) {
      result_to = std::numeric_limits<word_t>::max();
    }
    return type_t::Range(result_from, result_to, zone);
  }

  // Performs exponential widening, which means that the number of values
  // described by the resulting type is at least doubled with respect to the
  // {old_type}. If {new_type} is already twice the size of {old_type},
  // {new_type} may be returned directly.
  static type_t WidenExponential(const type_t& old_type, type_t new_type,
                                 Zone* zone) {
    if (new_type.is_any()) return new_type;
    word_t old_from, old_to, new_from, new_to;
    if (old_type.is_set()) {
      const word_t old_size = old_type.set_size();
      if (new_type.is_set()) {
        const word_t new_size = new_type.set_size();
        if (new_size >= 2 * old_size) return new_type;
        std::tie(new_from, new_to) = MakeRange(new_type);
      } else {
        DCHECK(new_type.is_range());
        std::tie(new_from, new_to) = new_type.range();
      }
      if (distance(new_from, new_to) >= 2 * old_size) {
        return type_t::Range(new_from, new_to, zone);
      }
      std::tie(old_from, old_to) = MakeRange(old_type);
    } else {
      DCHECK(old_type.is_range());
      std::tie(old_from, old_to) = old_type.range();
      if (new_type.is_set()) {
        std::tie(new_from, new_to) = MakeRange(new_type);
      } else {
        DCHECK(new_type.is_range());
        std::tie(new_from, new_to) = new_type.range();
      }
    }

    // If the old type is already quite large, we go to full range.
    if (distance(old_from, old_to) >= std::numeric_limits<word_t>::max() / 4) {
      return type_t::Any();
    }

    const word_t min_size = 2 * (distance(old_from, old_to) + 1);
    if (distance(new_from, new_to) >= min_size) {
      return type_t::Range(new_from, new_to, zone);
    }

    // If old is wrapping (and so is new).
    if (is_wrapping(old_from, old_to)) {
      DCHECK(is_wrapping(new_from, new_to));
      if (new_from < old_from) {
        DCHECK_LE(old_to, new_to);
        // We widen the `from` (although `to` might have grown, too).
        DCHECK_LT(new_to, min_size);
        word_t result_from =
            std::numeric_limits<word_t>::max() - (min_size - new_to);
        DCHECK_LT(result_from, new_from);
        DCHECK_LE(min_size, distance(result_from, new_to));
        return type_t::Range(result_from, new_to, zone);
      } else {
        DCHECK_EQ(old_from, new_from);
        // We widen the `to`.
        DCHECK_LT(std::numeric_limits<word_t>::max() - new_from, min_size);
        word_t result_to =
            min_size - (std::numeric_limits<word_t>::max() - new_from);
        DCHECK_GT(result_to, new_to);
        DCHECK_LE(min_size, distance(new_from, result_to));
        return type_t::Range(new_from, result_to, zone);
      }
    }

    // If old is not wrapping, but new is.
    if (is_wrapping(new_from, new_to)) {
      if (new_to < old_to) {
        // If wrapping was caused by to growing over max, grow `to` further
        // (although `from` might have grown, too).
        DCHECK_LT(std::numeric_limits<word_t>::max() - new_from, min_size);
        word_t result_to =
            min_size - (std::numeric_limits<word_t>::max() - new_from);
        DCHECK_LT(new_to, result_to);
        return type_t::Range(new_from, result_to, zone);
      } else {
        DCHECK_LT(old_from, new_from);
        // If wrapping was caused by `from` growing below 0, grow `from`
        // further.
        DCHECK_LT(new_to, min_size);
        word_t result_from =
            std::numeric_limits<word_t>::max() - (min_size - new_to);
        DCHECK_LT(result_from, new_from);
        return type_t::Range(result_from, new_to, zone);
      }
    }

    // Neither old nor new is wrapping.
    if (new_from < old_from) {
      DCHECK_LE(old_to, new_to);
      // Check if we can widen the `from`.
      if (new_to >= min_size) {
        // We can decrease `from` without going below 0.
        word_t result_from = new_to - min_size;
        DCHECK_LT(result_from, new_from);
        return type_t::Range(result_from, new_to, zone);
      } else {
        // We cannot grow `from` enough, so we also have to grow `to`.
        return type_t::Range(0, min_size, zone);
      }
    } else {
      DCHECK_EQ(old_from, new_from);
      // Check if we can widen the `to`.
      if (new_from <= std::numeric_limits<word_t>::max() - min_size) {
        // We can increase `to` without going above max.
        word_t result_to = new_from + min_size;
        DCHECK_GT(result_to, new_to);
        return type_t::Range(new_from, result_to, zone);
      } else {
        // We cannot grow `to` enough, so we also have to grow `from`.
        return type_t::Range(std::numeric_limits<word_t>::max() - min_size,
                             std::numeric_limits<word_t>::max(), zone);
      }
    }
  }
};

template <size_t Bits>
struct FloatOperationTyper {
  static_assert(Bits == 32 || Bits == 64);
  using float_t = std::conditional_t<Bits == 32, float, double>;
  using type_t = FloatType<Bits>;
  static constexpr float_t inf = std::numeric_limits<float_t>::infinity();
  static constexpr int kSetThreshold = type_t::kMaxSetSize;

  static type_t Range(float_t min, float_t max, uint32_t special_values,
                      Zone* zone) {
    DCHECK_LE(min, max);
    DCHECK_IMPLIES(detail::is_minus_zero(min),
                   (special_values & type_t::kMinusZero));
    DCHECK_IMPLIES(detail::is_minus_zero(max),
                   (special_values & type_t::kMinusZero));
    if (min == max) return Set({min + float_t{0}}, special_values, zone);
    return type_t::Range(min, max, special_values, zone);
  }

  static type_t Set(std::vector<float_t> elements, uint32_t special_values,
                    Zone* zone) {
    base::sort(elements);
    elements.erase(std::unique(elements.begin(), elements.end()),
                   elements.end());
    if (base::erase_if(elements, [](float_t v) { return std::isnan(v); }) > 0) {
      special_values |= type_t::kNaN;
    }
    if (base::erase_if(elements, [](float_t v) { return IsMinusZero(v); }) >
        0) {
      special_values |= type_t::kMinusZero;
    }
    if (elements.empty()) {
      DCHECK_NE(0, special_values);
      return type_t::OnlySpecialValues(special_values);
    }
    return type_t::Set(elements, special_values, zone);
  }

  // Check if the elements in the set are all integers. This ignores special
  // values (NaN, -0)!
  static bool IsIntegerSet(const type_t& t) {
    if (!t.is_set()) return false;
    int size = t.set_size();
    DCHECK_LT(0, size);

    float_t unused_ipart;
    float_t min = t.set_element(0);
    if (std::modf(min, &unused_ipart) != 0.0) return false;
    if (min == -inf) return false;
    float_t max = t.set_element(size - 1);
    if (std::modf(max, &unused_ipart) != 0.0) return false;
    if (max == inf) return false;

    for (int i = 1; i < size - 1; ++i) {
      if (std::modf(t.set_element(i), &unused_ipart) != 0.0) return false;
    }
    return true;
  }

  static bool IsZeroish(const type_t& l) {
    return l.has_nan() || l.has_minus_zero() || l.Contains(0);
  }

  // Tries to construct the product of two sets where values are generated using
  // {combine}. Returns Type::Invalid() if a set cannot be constructed (e.g.
  // because the result exceeds the maximal number of set elements).
  static Type ProductSet(const type_t& l, const type_t& r,
                         uint32_t special_values, Zone* zone,
                         std::function<float_t(float_t, float_t)> combine) {
    DCHECK(l.is_set());
    DCHECK(r.is_set());

    std::vector<float_t> results;
    auto CombineWithLeft = [&](float_t left) {
      for (int j = 0; j < r.set_size(); ++j) {
        results.push_back(combine(left, r.set_element(j)));
      }
      if (r.has_minus_zero()) results.push_back(combine(left, -0.0));
      if (r.has_nan()) results.push_back(combine(left, nan_v<Bits>));
    };

    for (int i = 0; i < l.set_size(); ++i) {
      CombineWithLeft(l.set_element(i));
    }
    if (l.has_minus_zero()) CombineWithLeft(-0.0);
    if (l.has_nan()) CombineWithLeft(nan_v<Bits>);

    if (base::erase_if(results, [](float_t v) { return std::isnan(v); }) > 0) {
      special_values |= type_t::kNaN;
    }
    if (base::erase_if(results, [](float_t v) { return IsMinusZero(v); }) > 0) {
      special_values |= type_t::kMinusZero;
    }
    base::sort(results);
    auto it = std::unique(results.begin(), results.end());
    if (std::distance(results.begin(), it) > kSetThreshold)
      return Type::Invalid();
    results.erase(it, results.end());
    if (results.empty()) return type_t::OnlySpecialValues(special_values);
    return Set(std::move(results), special_values, zone);
  }

  static Type Add(type_t l, type_t r, Zone* zone) {
    // Addition can return NaN if either input can be NaN or we try to compute
    // the sum of two infinities of opposite sign.
    if (l.is_only_nan() || r.is_only_nan()) return type_t::NaN();
    bool maybe_nan = l.has_nan() || r.has_nan();

    // Addition can yield minus zero only if both inputs can be minus zero.
    bool maybe_minuszero = true;
    if (l.has_minus_zero()) {
      l = type_t::LeastUpperBound(l, type_t::Constant(0), zone);
    } else {
      maybe_minuszero = false;
    }
    if (r.has_minus_zero()) {
      r = type_t::LeastUpperBound(r, type_t::Constant(0), zone);
    } else {
      maybe_minuszero = false;
    }

    uint32_t special_values = (maybe_nan ? type_t::kNaN : 0) |
                              (maybe_minuszero ? type_t::kMinusZero : 0);
    // If both sides are decently small sets, we produce the product set.
    auto combine = [](float_t a, float_t b) { return a + b; };
    if (l.is_set() && r.is_set()) {
      auto result = ProductSet(l, r, special_values, zone, combine);
      if (!result.IsInvalid()) return result;
    }

    // Otherwise just construct a range.
    auto [l_min, l_max] = l.minmax();
    auto [r_min, r_max] = r.minmax();

    std::array<float_t, 4> results;
    results[0] = l_min + r_min;
    results[1] = l_min + r_max;
    results[2] = l_max + r_min;
    results[3] = l_max + r_max;

    int nans = 0;
    for (int i = 0; i < 4; ++i) {
      if (std::isnan(results[i])) ++nans;
    }
    if (nans > 0) {
      special_values |= type_t::kNaN;
      if (nans >= 4) {
        // All combinations of inputs produce NaN.
        return type_t::OnlySpecialValues(special_values);
      }
    }
    const float_t result_min = array_min(results);
    const float_t result_max = array_max(results);
    return Range(result_min, result_max, special_values, zone);
  }

  static Type Subtract(type_t l, type_t r, Zone* zone) {
    // Subtraction can return NaN if either input can be NaN or we try to
    // compute the sum of two infinities of opposite sign.
    if (l.is_only_nan() || r.is_only_nan()) return type_t::NaN();
    bool maybe_nan = l.has_nan() || r.has_nan();

    // Subtraction can yield minus zero if {lhs} can be minus zero and {rhs}
    // can be zero.
    bool maybe_minuszero = false;
    if (l.has_minus_zero()) {
      l = type_t::LeastUpperBound(l, type_t::Constant(0), zone);
      maybe_minuszero = r.Contains(0);
    }
    if (r.has_minus_zero()) {
      r = type_t::LeastUpperBound(r, type_t::Constant(0), zone);
    }

    uint32_t special_values = (maybe_nan ? type_t::kNaN : 0) |
                              (maybe_minuszero ? type_t::kMinusZero : 0);
    // If both sides are decently small sets, we produce the product set.
    auto combine = [](float_t a, float_t b) { return a - b; };
    if (l.is_set() && r.is_set()) {
      auto result = ProductSet(l, r, special_values, zone, combine);
      if (!result.IsInvalid()) return result;
    }

    // Otherwise just construct a range.
    auto [l_min, l_max] = l.minmax();
    auto [r_min, r_max] = r.minmax();

    std::array<float_t, 4> results;
    results[0] = l_min - r_min;
    results[1] = l_min - r_max;
    results[2] = l_max - r_min;
    results[3] = l_max - r_max;

    int nans = 0;
    for (int i = 0; i < 4; ++i) {
      if (std::isnan(results[i])) ++nans;
    }
    if (nans > 0) {
      special_values |= type_t::kNaN;
      if (nans >= 4) {
        // All combinations of inputs produce NaN.
        return type_t::NaN();
      }
    }
    const float_t result_min = array_min(results);
    const float_t result_max = array_max(results);
    return Range(result_min, result_max, special_values, zone);
  }

  static Type Multiply(type_t l, type_t r, Zone* zone) {
    // Multiplication propagates NaN:
    //   NaN * x = NaN         (regardless of sign of x)
    //   0 * Infinity = NaN    (regardless of signs)
    if (l.is_only_nan() || r.is_only_nan()) return type_t::NaN();
    bool maybe_nan = l.has_nan() || r.has_nan() ||
                     (IsZeroish(l) && (r.min() == -inf || r.max() == inf)) ||
                     (IsZeroish(r) && (l.min() == -inf || r.max() == inf));

    // Try to rule out -0.
    bool maybe_minuszero = l.has_minus_zero() || r.has_minus_zero() ||
                           (IsZeroish(l) && r.min() < 0.0) ||
                           (IsZeroish(r) && l.min() < 0.0);
    if (l.has_minus_zero()) {
      l = type_t::LeastUpperBound(l, type_t::Constant(0), zone);
    }
    if (r.has_minus_zero()) {
      r = type_t::LeastUpperBound(r, type_t::Constant(0), zone);
    }

    uint32_t special_values = (maybe_nan ? type_t::kNaN : 0) |
                              (maybe_minuszero ? type_t::kMinusZero : 0);
    // If both sides are decently small sets, we produce the product set.
    auto combine = [](float_t a, float_t b) { return a * b; };
    if (l.is_set() && r.is_set()) {
      auto result = ProductSet(l, r, special_values, zone, combine);
      if (!result.IsInvalid()) return result;
    }

    // Otherwise just construct a range.
    auto [l_min, l_max] = l.minmax();
    auto [r_min, r_max] = r.minmax();

    std::array<float_t, 4> results;
    results[0] = l_min * r_min;
    results[1] = l_min * r_max;
    results[2] = l_max * r_min;
    results[3] = l_max * r_max;

    for (int i = 0; i < 4; ++i) {
      if (std::isnan(results[i])) {
        return type_t::Any();
      }
    }

    float_t result_min = array_min(results);
    float_t result_max = array_max(results);
    if (result_min <= 0.0 && 0.0 <= result_max &&
        (l_min < 0.0 || r_min < 0.0)) {
      special_values |= type_t::kMinusZero;
      // Remove -0.
      result_min += 0.0;
      result_max += 0.0;
    }
    // 0 * V8_INFINITY is NaN, regardless of sign
    if (((l_min == -inf || l_max == inf) && (r_min <= 0.0 && 0.0 <= r_max)) ||
        ((r_min == -inf || r_max == inf) && (l_min <= 0.0 && 0.0 <= l_max))) {
      special_values |= type_t::kNaN;
    }

    type_t type = Range(result_min, result_max, special_values, zone);
    return type;
  }

  static Type Divide(const type_t& l, const type_t& r, Zone* zone) {
    // Division is tricky, so all we do is try ruling out -0 and NaN.
    if (l.is_only_nan() || r.is_only_nan()) return type_t::NaN();

    // If both sides are decently small sets, we produce the product set.
    auto combine = [](float_t a, float_t b) {
      if V8_UNLIKELY (!std::isfinite(a) && !std::isfinite(b)) {
        return nan_v<Bits>;
      }
      if V8_UNLIKELY (IsMinusZero(b)) {
        // +-0 / -0 ==> NaN
        if (a == 0 || std::isnan(a)) return nan_v<Bits>;
        return a > 0 ? -inf : inf;
      }
      if V8_UNLIKELY (b == 0) {
        // +-0 / 0 ==> NaN
        if (a == 0 || std::isnan(a)) return nan_v<Bits>;
        return a > 0 ? inf : -inf;
      }
      return a / b;
    };
    if (l.is_set() && r.is_set()) {
      auto result = ProductSet(l, r, 0, zone, combine);
      if (!result.IsInvalid()) return result;
    }

    auto [l_min, l_max] = l.minmax();
    auto [r_min, r_max] = r.minmax();

    bool maybe_nan =
        l.has_nan() || IsZeroish(r) ||
        ((l_min == -inf || l_max == inf) && (r_min == -inf || r_max == inf));

    // Try to rule out -0.
    bool maybe_minuszero =
        // -0 / r (r > 0)
        (l.has_minus_zero() && r_max > 0)
        // 0 / r (r < 0)
        || (l.Contains(0) && r_min < 0)
        // -0.0..01 / r (r > 1)
        || (l.Contains(0) && l_min < 0 && r_max > 1)
        // 0.0..01 / r (r < -1)
        || (l.Contains(0) && l_max >= 0 && r_min < -1)
        // l / large (l < 0)
        || (l_max < 0 && detail::is_minus_zero(l_max / r_max))
        // l / -large (l > 0)
        || (l_min > 0 && detail::is_minus_zero(l_min / r_min));

    uint32_t special_values = (maybe_nan ? type_t::kNaN : 0) |
                              (maybe_minuszero ? type_t::kMinusZero : 0);

    const bool r_all_positive = r_min >= 0 && !r.has_minus_zero();
    const bool r_all_negative = r_max < 0;

    // If r doesn't span 0, we can try to compute a more precise type.
    if (r_all_positive || r_all_negative) {
      // If r does not contain 0 or -0, we can compute a range.
      if (r_min > 0 && !r.has_minus_zero()) {
        std::array<float_t, 4> results;
        results[0] = l_min / r_min;
        results[1] = l_min / r_max;
        results[2] = l_max / r_min;
        results[3] = l_max / r_max;

        for (float_t r : results) {
          if (std::isnan(r)) return type_t::Any();
        }

        const float_t result_min = array_min(results);
        const float_t result_max = array_max(results);
        return Range(result_min, result_max, special_values, zone);
      }

      // Otherwise we try to check for the sign of the result.
      if (l_max < 0) {
        if (r_all_positive) {
          // All values are negative.
          return Range(-inf, next_smaller(float_t{0}), special_values, zone);
        } else {
          DCHECK(r_all_negative);
          // All values are positive.
          return Range(0, inf, special_values, zone);
        }
      } else if (l_min >= 0 && !l.has_minus_zero()) {
        if (r_all_positive) {
          // All values are positive.
          DCHECK_EQ(special_values & type_t::kMinusZero, 0);
          return Range(0, inf, special_values, zone);
        } else {
          DCHECK(r_all_negative);
          // All values are negative.
          return Range(-inf, next_smaller(float_t{0}), special_values, zone);
        }
      }
    }

    // Otherwise we give up on a precise type.
    return type_t::Any(special_values);
  }

  static Type Modulus(type_t l, type_t r, Zone* zone) {
    // Modulus can yield NaN if either {lhs} or {rhs} are NaN, or
    // {lhs} is not finite, or the {rhs} is a zero value.
    if (l.is_only_nan() || r.is_only_nan()) return type_t::NaN();
    bool maybe_nan =
        l.has_nan() || IsZeroish(r) || l.min() == -inf || l.max() == inf;

    // Deal with -0 inputs, only the signbit of {lhs} matters for the result.
    bool maybe_minuszero = l.min() < 0;
    if (l.has_minus_zero()) {
      maybe_minuszero = true;
      l = type_t::LeastUpperBound(l, type_t::Constant(0), zone);
    }
    if (r.has_minus_zero()) {
      r = type_t::LeastUpperBound(r, type_t::Constant(0), zone);
    }

    uint32_t special_values = (maybe_nan ? type_t::kNaN : 0) |
                              (maybe_minuszero ? type_t::kMinusZero : 0);
    // For integer inputs {l} and {r} we can infer a precise type.
    if (IsIntegerSet(l) && IsIntegerSet(r)) {
      auto [l_min, l_max] = l.minmax();
      auto [r_min, r_max] = r.minmax();
      // l % r is:
      // - never greater than abs(l)
      // - never greater than abs(r) - 1
      auto l_abs = std::max(std::abs(l_min), std::abs(l_max));
      auto r_abs = std::max(std::abs(r_min), std::abs(r_max));
      // If rhs is 0, we can only produce NaN.
      if (r_abs == 0) return type_t::NaN();
      r_abs -= 1;
      auto abs = std::min(l_abs, r_abs);
      float_t min = 0.0, max = 0.0;
      if (l_min >= 0.0) {
        // {l} positive.
        max = abs;
      } else if (l_max <= 0.0) {
        // {l} negative.
        min = 0.0 - abs;
      } else {
        // {l} positive or negative.
        min = 0.0 - abs;
        max = abs;
      }
      if (min == max) return Set({min}, special_values, zone);
      return Range(min, max, special_values, zone);
    }

    // Otherwise, we give up.
    return type_t::Any(special_values);
  }

  static Type Min(type_t l, type_t r, Zone* zone) {
    if (l.is_only_nan() || r.is_only_nan()) return type_t::NaN();
    bool maybe_nan = l.has_nan() || r.has_nan();

    // In order to ensure monotonicity of the computation below, we additionally
    // pretend +0 is present (for simplicity on both sides).
    bool maybe_minuszero = false;
    if (l.has_minus_zero() && !(r.max() < 0.0)) {
      maybe_minuszero = true;
      l = type_t::LeastUpperBound(l, type_t::Constant(0), zone);
    }
    if (r.has_minus_zero() && !(l.max() < 0.0)) {
      maybe_minuszero = true;
      r = type_t::LeastUpperBound(r, type_t::Constant(0), zone);
    }

    uint32_t special_values = (maybe_nan ? type_t::kNaN : 0) |
                              (maybe_minuszero ? type_t::kMinusZero : 0);
    // If both sides are decently small sets, we produce the product set.
    auto combine = [](float_t a, float_t b) { return std::min(a, b); };
    if (l.is_set() && r.is_set()) {
      // TODO(nicohartmann@): There is a faster way to compute this set.
      auto result = ProductSet(l, r, special_values, zone, combine);
      if (!result.IsInvalid()) return result;
    }

    // Otherwise just construct a range.
    auto [l_min, l_max] = l.minmax();
    auto [r_min, r_max] = r.minmax();

    auto min = std::min(l_min, r_min);
    auto max = std::min(l_max, r_max);
    return Range(min, max, special_values, zone);
  }

  static Type Max(type_t l, type_t r, Zone* zone) {
    if (l.is_only_nan() || r.is_only_nan()) return type_t::NaN();
    bool maybe_nan = l.has_nan() || r.has_nan();

    // In order to ensure monotonicity of the computation below, we additionally
    // pretend +0 is present (for simplicity on both sides).
    bool maybe_minuszero = false;
    if (l.has_minus_zero() && !(r.min() > 0.0)) {
      maybe_minuszero = true;
      l = type_t::LeastUpperBound(l, type_t::Constant(0), zone);
    }
    if (r.has_minus_zero() && !(l.min() > 0.0)) {
      maybe_minuszero = true;
      r = type_t::LeastUpperBound(r, type_t::Constant(0), zone);
    }

    uint32_t special_values = (maybe_nan ? type_t::kNaN : 0) |
                              (maybe_minuszero ? type_t::kMinusZero : 0);
    // If both sides are decently small sets, we produce the product set.
    auto combine = [](float_t a, float_t b) { return std::max(a, b); };
    if (l.is_set() && r.is_set()) {
      // TODO(nicohartmann@): There is a faster way to compute this set.
      auto result = ProductSet(l, r, special_values, zone, combine);
      if (!result.IsInvalid()) return result;
    }

    // Otherwise just construct a range.
    auto [l_min, l_max] = l.minmax();
    auto [r_min, r_max] = r.minmax();

    auto min = std::max(l_min, r_min);
    auto max = std::max(l_max, r_max);
    return Range(min, max, special_values, zone);
  }

  static Type Power(const type_t& l, const type_t& r, Zone* zone) {
    // x ** NaN => Nan.
    if (r.is_only_nan()) return type_t::NaN();
    // x ** +-0 => 1.
    if (r.is_constant(0) || r.is_only_minus_zero()) return type_t::Constant(1);
    if (l.is_only_nan()) {
      // NaN ** 0 => 1.
      if (r.Contains(0) || r.has_minus_zero()) {
        return type_t::Set({1}, type_t::kNaN, zone);
      }
      // NaN ** x => NaN (x != +-0).
      return type_t::NaN();
    }
    bool maybe_nan = l.has_nan() || r.has_nan();
    // +-1 ** +-Infinity => NaN.
    if (r.Contains(-inf) || r.Contains(inf)) {
      if (l.Contains(1) || l.Contains(-1)) maybe_nan = true;
    }

    // a ** b produces NaN if a < 0 && b is fraction.
    if (l.min() < 0.0 && !IsIntegerSet(r)) maybe_nan = true;

    // Precise checks for when the result can be -0 is difficult, because of
    // large (negative) exponents. To be safe we add -0 whenever the left hand
    // side can be negative. We might refine this when necessary.
    bool maybe_minus_zero = l.min() < 0.0 || l.has_minus_zero();
    uint32_t special_values = (maybe_nan ? type_t::kNaN : 0) |
                              (maybe_minus_zero ? type_t::kMinusZero : 0) |
                              l.special_values();

    // If both sides are decently small sets, we produce the product set.
    auto combine = [](float_t a, float_t b) { return std::pow(a, b); };
    if (l.is_set() && r.is_set()) {
      auto result = ProductSet(l, r, special_values, zone, combine);
      if (!result.IsInvalid()) return result;
    }

    // TODO(nicohartmann@): Maybe we can produce a more precise range here.
    return type_t::Any(special_values);
  }

  static Type Atan2(const type_t& l, const type_t& r, Zone* zone) {
    // TODO(nicohartmann@): Maybe we can produce a more precise range here.
    return type_t::Any();
  }

  static Type LessThan(const type_t& lhs, const type_t& rhs, Zone* zone) {
    bool can_be_true = false;
    bool can_be_false = false;
    if (lhs.is_only_special_values()) {
      if (lhs.has_minus_zero()) {
        can_be_true = !rhs.is_only_special_values() && rhs.max() > 0.0;
        can_be_false = rhs.min() <= 0.0;
      } else {
        DCHECK(lhs.is_only_nan());
      }
    } else if (rhs.is_only_special_values()) {
      if (rhs.has_minus_zero()) {
        can_be_true = lhs.min() < 0.0;
        can_be_false = lhs.max() >= 0.0;
      } else {
        DCHECK(rhs.is_only_nan());
      }
    } else {
      // Both sides have at least one non-special value. We don't have to treat
      // special values here, because nan has been taken care of already and
      // -0.0 is included in min/max.
      can_be_true = lhs.min() < rhs.max();
      can_be_false = lhs.max() >= rhs.min();
    }

    // Consider NaN.
    can_be_false = can_be_false || lhs.has_nan() || rhs.has_nan();

    if (!can_be_true) return Word32Type::Constant(0);
    if (!can_be_false) return Word32Type::Constant(1);
    return Word32Type::Set({0, 1}, zone);
  }

  static Type LessThanOrEqual(const type_t& lhs, const type_t& rhs,
                              Zone* zone) {
    bool can_be_true = false;
    bool can_be_false = false;
    if (lhs.is_only_special_values()) {
      if (lhs.has_minus_zero()) {
        can_be_true = (!rhs.is_only_special_values() && rhs.max() >= 0.0) ||
                      rhs.has_minus_zero();
        can_be_false = rhs.min() < 0.0;
      } else {
        DCHECK(lhs.is_only_nan());
      }
    } else if (rhs.is_only_special_values()) {
      if (rhs.has_minus_zero()) {
        can_be_true = (!lhs.is_only_special_values() && lhs.min() <= 0.0) ||
                      lhs.has_minus_zero();
        can_be_false = lhs.max() > 0.0;
      } else {
        DCHECK(rhs.is_only_nan());
      }
    } else {
      // Both sides have at least one non-special value. We don't have to treat
      // special values here, because nan has been taken care of already and
      // -0.0 is included in min/max.
      can_be_true = can_be_true || lhs.min() <= rhs.max();
      can_be_false = can_be_false || lhs.max() > rhs.min();
    }

    // Consider NaN.
    can_be_false = can_be_false || lhs.has_nan() || rhs.has_nan();

    if (!can_be_true) return Word32Type::Constant(0);
    if (!can_be_false) return Word32Type::Constant(1);
    return Word32Type::Set({0, 1}, zone);
  }

  static Word32Type UnsignedLessThanOrEqual(const type_t& lhs,
                                            const type_t& rhs, Zone* zone) {
    bool can_be_true = lhs.unsigned_min() <= rhs.unsigned_max();
    bool can_be_false = lhs.unsigned_max() > rhs.unsigned_min();

    if (!can_be_true) return Word32Type::Constant(0);
    if (!can_be_false) return Word32Type::Constant(1);
    return Word32Type::Set({0, 1}, zone);
  }

  // Computes the ranges to which the sides of the comparison (lhs < rhs) can be
  // restricted when the comparison is true. When the comparison is true, we
  // learn: lhs cannot be >= rhs.max and rhs cannot be <= lhs.min and neither
  // can be NaN.
  static std::pair<Type, Type> RestrictionForLessThan_True(const type_t& lhs,
                                                           const type_t& rhs,
                                                           Zone* zone) {
    // If either side is only NaN, this comparison can never be true.
    if (lhs.is_only_nan() || rhs.is_only_nan()) {
      return {Type::None(), Type::None()};
    }

    Type restrict_lhs;
    if (rhs.max() == -inf) {
      // There is no value for lhs that could make (lhs < -inf) true.
      restrict_lhs = Type::None();
    } else {
      const auto max = next_smaller(rhs.max());
      uint32_t sv = max >= 0 ? type_t::kMinusZero : type_t::kNoSpecialValues;
      restrict_lhs = type_t::Range(-inf, max, sv, zone);
    }

    Type restrict_rhs;
    if (lhs.min() == inf) {
      // There is no value for rhs that could make (inf < rhs) true.
      restrict_rhs = Type::None();
    } else {
      const auto min = next_larger(lhs.min());
      uint32_t sv = min <= 0 ? type_t::kMinusZero : type_t::kNoSpecialValues;
      restrict_rhs = type_t::Range(min, inf, sv, zone);
    }

    return {restrict_lhs, restrict_rhs};
  }

  // Computes the ranges to which the sides of the comparison (lhs < rhs) can be
  // restricted when the comparison is false. When the comparison is false, we
  // learn: lhs cannot be < rhs.min and rhs cannot be > lhs.max.
  static std::pair<Type, Type> RestrictionForLessThan_False(const type_t& lhs,
                                                            const type_t& rhs,
                                                            Zone* zone) {
    Type restrict_lhs;
    if (rhs.has_nan()) {
      restrict_lhs = type_t::Any();
    } else {
      uint32_t lhs_sv =
          type_t::kNaN |
          (rhs.min() <= 0 ? type_t::kMinusZero : type_t::kNoSpecialValues);
      restrict_lhs = type_t::Range(rhs.min(), inf, lhs_sv, zone);
    }

    Type restrict_rhs;
    if (lhs.has_nan()) {
      restrict_rhs = type_t::Any();
    } else {
      uint32_t rhs_sv =
          type_t::kNaN |
          (lhs.max() >= 0 ? type_t::kMinusZero : type_t::kNoSpecialValues);
      restrict_rhs = type_t::Range(-inf, lhs.max(), rhs_sv, zone);
    }

    return {restrict_lhs, restrict_rhs};
  }

  // Computes the ranges to which the sides of the comparison (lhs <= rhs) can
  // be restricted when the comparison is true. When the comparison is true, we
  // learn: lhs cannot be > rhs.max and rhs cannot be < lhs.min and neither can
  // be NaN.
  static std::pair<Type, Type> RestrictionForLessThanOrEqual_True(
      const type_t& lhs, const type_t& rhs, Zone* zone) {
    // If either side is only NaN, this comparison can never be true.
    if (lhs.is_only_nan() || rhs.is_only_nan()) {
      return {Type::None(), Type::None()};
    }

    uint32_t lhs_sv =
        rhs.max() >= 0 ? type_t::kMinusZero : type_t::kNoSpecialValues;
    uint32_t rhs_sv =
        lhs.min() <= 0 ? type_t::kMinusZero : type_t::kNoSpecialValues;
    return {type_t::Range(-inf, rhs.max(), lhs_sv, zone),
            type_t::Range(lhs.min(), inf, rhs_sv, zone)};
  }

  // Computes the ranges to which the sides of the comparison (lhs <= rhs) can
  // be restricted when the comparison is false. When the comparison is false,
  // we learn: lhs cannot be <= rhs.min and rhs cannot be >= lhs.max.
  static std::pair<Type, Type> RestrictionForLessThanOrEqual_False(
      const type_t& lhs, const type_t& rhs, Zone* zone) {
    Type restrict_lhs;
    if (rhs.has_nan()) {
      restrict_lhs = type_t::Any();
    } else if (rhs.min() == inf) {
      // The only value for lhs that could make (lhs <= inf) false is NaN.
      restrict_lhs = type_t::NaN();
    } else {
      const auto min = next_larger(rhs.min());
      uint32_t sv = type_t::kNaN |
                    (min <= 0 ? type_t::kMinusZero : type_t::kNoSpecialValues);
      restrict_lhs = type_t::Range(min, inf, sv, zone);
    }

    Type restrict_rhs;
    if (lhs.has_nan()) {
      restrict_rhs = type_t::Any();
    } else if (lhs.max() == -inf) {
      // The only value for rhs that could make (-inf <= rhs) false is NaN.
      restrict_rhs = type_t::NaN();
    } else {
      const auto max = next_smaller(lhs.max());
      uint32_t sv = type_t::kNaN |
                    (max >= 0 ? type_t::kMinusZero : type_t::kNoSpecialValues);
      restrict_rhs = type_t::Range(-inf, max, sv, zone);
    }

    return {restrict_lhs, restrict_rhs};
  }
};

class Typer {
 public:
  static Type TypeForRepresentation(RegisterRepresentation rep) {
    switch (rep.value()) {
      case RegisterRepresentation::Word32():
        return Word32Type::Any();
      case RegisterRepresentation::Word64():
        return Word64Type::Any();
      case RegisterRepresentation::Float32():
        return Float32Type::Any();
      case RegisterRepresentation::Float64():
        return Float64Type::Any();

      case RegisterRepresentation::Tagged():
      case RegisterRepresentation::Compressed():
      case RegisterRepresentation::Simd128():
      case RegisterRepresentation::Simd256():
        // TODO(nicohartmann@): Support these representations.
        return Type::Any();
    }
  }

  static Type TypeForRepresentation(
      base::Vector<const RegisterRepresentation> reps, Zone* zone) {
    DCHECK_LT(0, reps.size());
    if (reps.size() == 1) return TypeForRepresentation(reps[0]);
    base::SmallVector<Type, 4> tuple_types;
    for (auto rep : reps) tuple_types.push_back(TypeForRepresentation(rep));
    return TupleType::Tuple(base::VectorOf(tuple_types), zone);
  }

  static Type TypeConstant(ConstantOp::Kind kind, ConstantOp::Storage value) {
    switch (kind) {
      case ConstantOp::Kind::kFloat32:
        if (value.float32.is_nan()) return Float32Type::NaN();
        if (IsMinusZero(value.float32.get_scalar()))
          return Float32Type::MinusZero();
        return Float32Type::Constant(value.float32.get_scalar());
      case ConstantOp::Kind::kFloat64:
        if (value.float64.is_nan()) return Float64Type::NaN();
        if (IsMinusZero(value.float64.get_scalar()))
          return Float64Type::MinusZero();
        return Float64Type::Constant(value.float64.get_scalar());
      case ConstantOp::Kind::kWord32:
        return Word32Type::Constant(static_cast<uint32_t>(value.integral));
      case ConstantOp::Kind::kWord64:
        return Word64Type::Constant(static_cast<uint64_t>(value.integral));
      default:
        // TODO(nicohartmann@): Support remaining {kind}s.
        return Type::Any();
    }
  }

  static Type TypeProjection(const Type& input, uint16_t idx) {
    if (input.IsNone()) return Type::None();
    if (!input.IsTuple()) return Type::Any();
    const TupleType& tuple = input.AsTuple();
    DCHECK_LT(idx, tuple.size());
    return tuple.element(idx);
  }

  static Type TypeWordBinop(Type left_type, Type right_type,
                            WordBinopOp::Kind kind, WordRepresentation rep,
                            Zone* zone) {
    DCHECK(!left_type.IsInvalid());
    DCHECK(!right_type.IsInvalid());

    if (rep == WordRepresentation::Word32()) {
      switch (kind) {
        case WordBinopOp::Kind::kAdd:
          return TypeWord32Add(left_type, right_type, zone);
        case WordBinopOp::Kind::kSub:
          return TypeWord32Sub(left_type, right_type, zone);
        default:
          // TODO(nicohartmann@): Support remaining {kind}s.
          return Word32Type::Any();
      }
    } else {
      DCHECK_EQ(rep, WordRepresentation::Word64());
      switch (kind) {
        case WordBinopOp::Kind::kAdd:
          return TypeWord64Add(left_type, right_type, zone);
        case WordBinopOp::Kind::kSub:
          return TypeWord64Sub(left_type, right_type, zone);
        default:
          // TODO(nicohartmann@): Support remaining {kind}s.
          return Word64Type::Any();
      }
    }
  }

  static Type TypeWord32Add(const Type& lhs, const Type& rhs, Zone* zone) {
    if (lhs.IsNone() || rhs.IsNone()) return Type::None();
    auto l = TruncateWord32Input(lhs, true, zone);
    auto r = TruncateWord32Input(rhs, true, zone);
    return WordOperationTyper<32>::Add(l, r, zone);
  }

  static Type TypeWord32Sub(const Type& lhs, const Type& rhs, Zone* zone) {
    if (lhs.IsNone() || rhs.IsNone()) return Type::None();
    auto l = TruncateWord32Input(lhs, true, zone);
    auto r = TruncateWord32Input(rhs, true, zone);
    return WordOperationTyper<32>::Subtract(l, r, zone);
  }

  static Type TypeWord64Add(const Type& lhs, const Type& rhs, Zone* zone) {
    if (lhs.IsNone() || rhs.IsNone()) return Type::None();
    if (!InputIs(lhs, Type::Kind::kWord64) ||
        !InputIs(rhs, Type::Kind::kWord64)) {
      return Word64Type::Any();
    }
    const auto& l = lhs.AsWord64();
    const auto& r = rhs.AsWord64();

    return WordOperationTyper<64>::Add(l, r, zone);
  }

  static Type TypeWord64Sub(const Type& lhs, const Type& rhs, Zone* zone) {
    if (lhs.IsNone() || rhs.IsNone()) return Type::None();
    if (!InputIs(lhs, Type::Kind::kWord64) ||
        !InputIs(rhs, Type::Kind::kWord64)) {
      return Word64Type::Any();
    }

    const auto& l = lhs.AsWord64();
    const auto& r = rhs.AsWord64();

    return WordOperationTyper<64>::Subtract(l, r, zone);
  }

  static Type TypeFloatBinop(Type left_type, Type right_type,
                             FloatBinopOp::Kind kind, FloatRepresentation rep,
                             Zone* zone) {
    DCHECK(!left_type.IsInvalid());
    DCHECK(!right_type.IsInvalid());

#define FLOAT_BINOP(op, bits)     \
  case FloatBinopOp::Kind::k##op: \
    return TypeFloat##bits##op(left_type, right_type, zone);

    if (rep == FloatRepresentation::Float32()) {
      switch (kind) {
        FLOAT_BINOP(Add, 32)
        FLOAT_BINOP(Sub, 32)
        FLOAT_BINOP(Mul, 32)
        FLOAT_BINOP(Div, 32)
        FLOAT_BINOP(Mod, 32)
        FLOAT_BINOP(Min, 32)
        FLOAT_BINOP(Max, 32)
        FLOAT_BINOP(Power, 32)
        FLOAT_BINOP(Atan2, 32)
      }
    } else {
      DCHECK_EQ(rep, FloatRepresentation::Float64());
      switch (kind) {
        FLOAT_BINOP(Add, 64)
        FLOAT_BINOP(Sub, 64)
        FLOAT_BINOP(Mul, 64)
        FLOAT_BINOP(Div, 64)
        FLOAT_BINOP(Mod, 64)
        FLOAT_BINOP(Min, 64)
        FLOAT_BINOP(Max, 64)
        FLOAT_BINOP(Power, 64)
        FLOAT_BINOP(Atan2, 64)
      }
    }

#undef FLOAT_BINOP
  }

#define FLOAT_BINOP(op, bits, float_typer_handler)                     \
  static Type TypeFloat##bits##op(const Type& lhs, const Type& rhs,    \
                                  Zone* zone) {                        \
    if (lhs.IsNone() || rhs.IsNone()) return Type::None();             \
    if (!InputIs(lhs, Type::Kind::kFloat##bits) ||                     \
        !InputIs(rhs, Type::Kind::kFloat##bits)) {                     \
      return Float##bits##Type::Any();                                 \
    }                                                                  \
    const auto& l = lhs.AsFloat##bits();                               \
    const auto& r = rhs.AsFloat##bits();                               \
    return FloatOperationTyper<bits>::float_typer_handler(l, r, zone); \
  }

  // Float32 operations
  FLOAT_BINOP(Add, 32, Add)
  FLOAT_BINOP(Sub, 32, Subtract)
  FLOAT_BINOP(Mul, 32, Multiply)
  FLOAT_BINOP(Div, 32, Divide)
  FLOAT_BINOP(Mod, 32, Modulus)
  FLOAT_BINOP(Min, 32, Min)
  FLOAT_BINOP(Max, 32, Max)
  FLOAT_BINOP(Power, 32, Power)
  FLOAT_BINOP(Atan2, 32, Atan2)
  // Float64 operations
  FLOAT_BINOP(Add, 64, Add)
  FLOAT_BINOP(Sub, 64, Subtract)
  FLOAT_BINOP(Mul, 64, Multiply)
  FLOAT_BINOP(Div, 64, Divide)
  FLOAT_BINOP(Mod, 64, Modulus)
  FLOAT_BINOP(Min, 64, Min)
  FLOAT_BINOP(Max, 64, Max)
  FLOAT_BINOP(Power, 64, Power)
  FLOAT_BINOP(Atan2, 64, Atan2)
#undef FLOAT_BINOP

  static Type TypeOverflowCheckedBinop(const Type& left_type,
                                       const Type& right_type,
                                       OverflowCheckedBinopOp::Kind kind,
                                       WordRepresentation rep, Zone* zone) {
    DCHECK(!left_type.IsInvalid());
    DCHECK(!right_type.IsInvalid());

    if (rep == WordRepresentation::Word32()) {
      switch (kind) {
        case OverflowCheckedBinopOp::Kind::kSignedAdd:
          return TypeWord32OverflowCheckedAdd(left_type, right_type, zone);
        case OverflowCheckedBinopOp::Kind::kSignedSub:
        case OverflowCheckedBinopOp::Kind::kSignedMul:
          // TODO(nicohartmann@): Support these.
          return TupleType::Tuple(Word32Type::Any(),
                                  Word32Type::Set({0, 1}, zone), zone);
      }
    } else {
      DCHECK_EQ(rep, WordRepresentation::Word64());
      switch (kind) {
        case OverflowCheckedBinopOp::Kind::kSignedAdd:
        case OverflowCheckedBinopOp::Kind::kSignedSub:
        case OverflowCheckedBinopOp::Kind::kSignedMul:
          // TODO(nicohartmann@): Support these.
          return TupleType::Tuple(Word64Type::Any(),
                                  Word32Type::Set({0, 1}, zone), zone);
      }
    }
  }

  static Type TypeWord32OverflowCheckedAdd(const Type& lhs, const Type& rhs,
                                           Zone* zone) {
    if (lhs.IsNone() || rhs.IsNone()) return Type::None();
    auto l = TruncateWord32Input(lhs, true, zone);
    auto r = TruncateWord32Input(rhs, true, zone);

    auto value = WordOperationTyper<32>::Add(l, r, zone);
    // We check for signed overflow and if the topmost bits of both opperands
    // are 0, we know that the result cannot overflow.
    if ((0xC0000000 & l.unsigned_max()) == 0 &&
        (0xC0000000 & r.unsigned_max()) == 0) {
      // Cannot overflow.
      return TupleType::Tuple(value, Word32Type::Constant(0), zone);
    }
    // Special case for two constant inputs to figure out the overflow.
    if (l.is_constant() && r.is_constant()) {
      constexpr uint32_t msb_mask = 0x80000000;
      DCHECK(value.is_constant());
      uint32_t l_msb = (*l.try_get_constant()) & msb_mask;
      uint32_t r_msb = (*r.try_get_constant()) & msb_mask;
      if (l_msb != r_msb) {
        // Different sign bits can never lead to an overflow.
        return TupleType::Tuple(value, Word32Type::Constant(0), zone);
      }
      uint32_t value_msb = (*value.try_get_constant()) & msb_mask;
      const uint32_t overflow = value_msb == l_msb ? 0 : 1;
      return TupleType::Tuple(value, Word32Type::Constant(overflow), zone);
    }
    // Otherwise we accept some imprecision.
    return TupleType::Tuple(value, Word32Type::Set({0, 1}, zone), zone);
  }

  static Type TypeComparison(const Type& lhs, const Type& rhs,
                             RegisterRepresentation rep,
                             ComparisonOp::Kind kind, Zone* zone) {
    switch (rep.value()) {
      case RegisterRepresentation::Word32():
        return TypeWord32Comparison(lhs, rhs, kind, zone);
      case RegisterRepresentation::Word64():
        return TypeWord64Comparison(lhs, rhs, kind, zone);
      case RegisterRepresentation::Float32():
        return TypeFloat32Comparison(lhs, rhs, kind, zone);
      case RegisterRepresentation::Float64():
        return TypeFloat64Comparison(lhs, rhs, kind, zone);
      case RegisterRepresentation::Tagged():
      case RegisterRepresentation::Compressed():
      case RegisterRepresentation::Simd128():
      case RegisterRepresentation::Simd256():
        if (lhs.IsNone() || rhs.IsNone()) return Type::None();
        // TODO(nicohartmann@): Support those cases.
        return Word32Type::Set({0, 1}, zone);
    }
  }

  static Type TypeWord32Comparison(const Type& lhs, const Type& rhs,
                                   ComparisonOp::Kind kind, Zone* zone) {
    if (lhs.IsNone() || rhs.IsNone()) return Type::None();
    auto l = TruncateWord32Input(lhs, true, zone);
    auto r = TruncateWord32Input(rhs, true, zone);
    switch (kind) {
      case ComparisonOp::Kind::kEqual:
      case ComparisonOp::Kind::kSignedLessThan:
      case ComparisonOp::Kind::kSignedLessThanOrEqual:
        // TODO(nicohartmann@): Support this.
        return Word32Type::Set({0, 1}, zone);
      case ComparisonOp::Kind::kUnsignedLessThan:
        return WordOperationTyper<32>::UnsignedLessThan(l, r, zone);
      case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
        return WordOperationTyper<32>::UnsignedLessThanOrEqual(l, r, zone);
    }
    UNREACHABLE();
  }

  static Type TypeWord64Comparison(const Type& lhs, const Type& rhs,
                                   ComparisonOp::Kind kind, Zone* zone) {
    if (lhs.IsNone() || rhs.IsNone()) return Type::None();
    switch (kind) {
      case ComparisonOp::Kind::kEqual:
      case ComparisonOp::Kind::kSignedLessThan:
      case ComparisonOp::Kind::kSignedLessThanOrEqual:
        // TODO(nicohartmann@): Support this.
        return Word32Type::Set({0, 1}, zone);
      case ComparisonOp::Kind::kUnsignedLessThan:
        return WordOperationTyper<64>::UnsignedLessThan(lhs.AsWord64(),
                                                        rhs.AsWord64(), zone);
      case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
        return WordOperationTyper<64>::UnsignedLessThanOrEqual(
            lhs.AsWord64(), rhs.AsWord64(), zone);
    }
    UNREACHABLE();
  }

  static Type TypeFloat32Comparison(const Type& lhs, const Type& rhs,
                                    ComparisonOp::Kind kind, Zone* zone) {
    if (lhs.IsNone() || rhs.IsNone()) return Type::None();
    switch (kind) {
      case ComparisonOp::Kind::kEqual:
        // TODO(nicohartmann@): Support this.
        return Word32Type::Set({0, 1}, zone);
      case ComparisonOp::Kind::kSignedLessThan:
        return FloatOperationTyper<32>::LessThan(lhs.AsFloat32(),
                                                 rhs.AsFloat32(), zone);
      case ComparisonOp::Kind::kSignedLessThanOrEqual:
        return FloatOperationTyper<32>::LessThanOrEqual(lhs.AsFloat32(),
                                                        rhs.AsFloat32(), zone);
      case ComparisonOp::Kind::kUnsignedLessThan:
      case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
        UNREACHABLE();
    }
  }

  static Type TypeFloat64Comparison(const Type& lhs, const Type& rhs,
                                    ComparisonOp::Kind kind, Zone* zone) {
    if (lhs.IsNone() || rhs.IsNone()) return Type::None();
    switch (kind) {
      case ComparisonOp::Kind::kEqual:
        // TODO(nicohartmann@): Support this.
        return Word32Type::Set({0, 1}, zone);
      case ComparisonOp::Kind::kSignedLessThan:
        return FloatOperationTyper<64>::LessThan(lhs.AsFloat64(),
                                                 rhs.AsFloat64(), zone);
      case ComparisonOp::Kind::kSignedLessThanOrEqual:
        return FloatOperationTyper<64>::LessThanOrEqual(lhs.AsFloat64(),
                                                        rhs.AsFloat64(), zone);
      case ComparisonOp::Kind::kUnsignedLessThan:
      case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
        UNREACHABLE();
    }
  }

  static Word64Type ExtendWord32ToWord64(const Word32Type& t, Zone* zone) {
    // We cannot infer much, but the lower bound of the word32 is also the lower
    // bound of the word64 type.
    if (t.is_wrapping()) return Word64Type::Any();
    return Word64Type::Range(static_cast<uint64_t>(t.unsigned_min()),
                             std::numeric_limits<uint64_t>::max(), zone);
  }

  static Word32Type TruncateWord32Input(const Type& input,
                                        bool implicit_word64_narrowing,
                                        Zone* zone) {
    DCHECK(!input.IsInvalid());
    DCHECK(!input.IsNone());

    if (input.IsAny()) {
      if (allow_invalid_inputs()) return Word32Type::Any();
    } else if (input.IsWord32()) {
      return input.AsWord32();
    } else if (input.IsWord64() && implicit_word64_narrowing) {
      // The input is implicitly converted to word32.
      const auto& w64 = input.AsWord64();
      if (w64.is_set()) {
        WordOperationTyper<32>::ElementsVector elements;
        for (uint64_t e : w64.set_elements()) {
          elements.push_back(static_cast<uint32_t>(e));
        }
        return WordOperationTyper<32>::FromElements(std::move(elements), zone);
      }

      if (w64.is_any() || w64.is_wrapping()) return Word32Type::Any();

      if (w64.range_to() <= std::numeric_limits<uint32_t>::max()) {
        DCHECK_LE(w64.range_from(), std::numeric_limits<uint32_t>::max());
        return Word32Type::Range(static_cast<uint32_t>(w64.range_from()),
                                 static_cast<uint32_t>(w64.range_to()), zone);
      }

      // TODO(nicohartmann@): Might compute a more precise range here.
      return Word32Type::Any();
    }

    FATAL("Missing proper type for TruncateWord32Input. Type is: %s",
          input.ToString().c_str());
  }

  class BranchRefinements {
   public:
    // type_getter_t has to provide the type for a given input index.
    using type_getter_t = std::function<Type(OpIndex)>;
    // type_refiner_t is called with those arguments:
    //  - OpIndex: index of the operation whose type is refined by the branch.
    //  - Type: the refined type of the operation (after refinement, guaranteed
    //  to be a subtype of the original type).
    using type_refiner_t = std::function<void(OpIndex, const Type&)>;

    BranchRefinements(type_getter_t type_getter, type_refiner_t type_refiner)
        : type_getter_(type_getter), type_refiner_(type_refiner) {
      DCHECK(type_getter_);
      DCHECK(type_refiner_);
    }

    void RefineTypes(const Operation& condition, bool then_branch, Zone* zone);

   private:
    template <bool allow_implicit_word64_truncation>
    Type RefineWord32Type(const Type& type, const Type& refinement,
                          Zone* zone) {
      // If refinement is Type::None(), the operation/branch is unreachable.
      if (refinement.IsNone()) return Type::None();
      DCHECK(refinement.IsWord32());
      if constexpr (allow_implicit_word64_truncation) {
        // Turboshaft allows implicit trunction of Word64 values to Word32. When
        // an operation on Word32 representation computes a refinement type,
        // this is going to be a Type::Word32() even if the actual {type} was
        // Word64 before truncation. To correctly refine this type, we need to
        // extend the {refinement} to Word64 such that it reflects the
        // corresponding values in the original type (before truncation) before
        // we intersect.
        if (type.IsWord64()) {
          return Word64Type::Intersect(
              type.AsWord64(),
              Typer::ExtendWord32ToWord64(refinement.AsWord32(), zone),
              Type::ResolutionMode::kOverApproximate, zone);
        }
      }
      // We limit the values of {type} to those in {refinement}.
      return Word32Type::Intersect(type.AsWord32(), refinement.AsWord32(),
                                   Type::ResolutionMode::kOverApproximate,
                                   zone);
    }

    type_getter_t type_getter_;
    type_refiner_t type_refiner_;
  };

  static bool InputIs(const Type& input, Type::Kind expected) {
    if (input.IsInvalid()) {
      if (allow_invalid_inputs()) return false;
    } else if (input.kind() == expected) {
      return true;
    } else if (input.IsAny()) {
      if (allow_invalid_inputs()) return false;
    }

    std::stringstream s;
    s << expected;
    FATAL("Missing proper type (%s). Type is: %s", s.str().c_str(),
          input.ToString().c_str());
  }

  // For now we allow invalid inputs (which will then just lead to very generic
  // typing). Once all operations are implemented, we are going to disable this.
  static bool allow_invalid_inputs() { return true; }
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_TYPER_H_
