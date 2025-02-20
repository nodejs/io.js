// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_TYPES_H_
#define V8_COMPILER_TURBOSHAFT_TYPES_H_

#include <cmath>
#include <limits>
#include <optional>

#include "src/base/container-utils.h"
#include "src/base/export-template.h"
#include "src/base/logging.h"
#include "src/base/small-vector.h"
#include "src/common/globals.h"
#include "src/compiler/turboshaft/fast-hash.h"
#include "src/numbers/conversions.h"
#include "src/objects/turboshaft-types.h"
#include "src/utils/ostreams.h"
#include "src/zone/zone-containers.h"

#ifdef DEBUG
#define TURBOSHAFT_TRACE_TYPING(...)                     \
  do {                                                   \
    if (V8_UNLIKELY(v8_flags.turboshaft_trace_typing)) { \
      PrintF(__VA_ARGS__);                               \
    }                                                    \
  } while (false)

#define TURBOSHAFT_TRACE_TYPING_WITH_COLOR(colorcode, str, ...)           \
  TURBOSHAFT_TRACE_TYPING(                                                \
      (v8_flags.log_colour ? ("\033[" colorcode "m" str "\033[m") : str), \
      __VA_ARGS__)
#define TURBOSHAFT_TRACE_TYPING_OK(str, ...) \
  TURBOSHAFT_TRACE_TYPING_WITH_COLOR("32", str, __VA_ARGS__)
#define TURBOSHAFT_TRACE_TYPING_FAIL(str, ...) \
  TURBOSHAFT_TRACE_TYPING_WITH_COLOR("31", str, __VA_ARGS__)
#else
#define TURBOSHAFT_TRACE_TYPING(...) ((void)0)
#define TURBOSHAFT_TRACE_TYPING_WITH_COLOR(colorcode, str, ...) ((void)0)
#define TURBOSHAFT_TRACE_TYPING_OK(str, ...) ((void)0)
#define TURBOSHAFT_TRACE_TYPING_FAIL(str, ...) ((void)0)
#endif  // DEBUG

namespace v8::internal {
class Factory;
}

namespace v8::internal::compiler::turboshaft {

namespace detail {

template <typename T>
inline bool is_unique_and_sorted(const T& container) {
  if (std::size(container) <= 1) return true;
  auto cur = std::begin(container);
  auto next = cur;
  for (++next; next != std::end(container); ++cur, ++next) {
    if (!(*cur < *next)) return false;
  }
  return true;
}

template <typename T>
inline bool is_minus_zero(T value) {
  return IsMinusZero(value);
}

template <typename T>
inline bool is_float_special_value(T value) {
  return std::isnan(value) || is_minus_zero(value);
}

template <size_t Bits>
struct TypeForBits;
template <>
struct TypeForBits<32> {
  using uint_type = uint32_t;
  using float_type = float;
  static constexpr float_type nan =
      std::numeric_limits<float_type>::quiet_NaN();
};
template <>
struct TypeForBits<64> {
  using uint_type = uint64_t;
  using float_type = double;
  static constexpr float_type nan =
      std::numeric_limits<float_type>::quiet_NaN();
};

// gcc versions < 9 may produce the following compilation error:
// > '<anonymous>' is used uninitialized in this function
// if Payload_Empty is initialized without any data, link to a relevant bug:
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=86465
// A workaround is to add a dummy value which is zero initialized by default.
// More information as well as a sample reproducible code can be found at the
// comment section of this CL crrev.com/c/4057111
// TODO(nicohartmann@): Remove dummy once all platforms are using gcc >= 9.
struct Payload_Empty {
  uint8_t dummy = 0;
};

template <typename T>
struct Payload_Range {
  T min;
  T max;
};

template <typename T>
struct Payload_InlineSet {
  T elements[2];
};

template <typename T>
struct Payload_OutlineSet {
  T* array;
};

}  // namespace detail

template <typename T>
T next_smaller(T v)
  requires std::is_floating_point<T>::value
{
  DCHECK(!std::isnan(v));
  DCHECK_LT(-std::numeric_limits<T>::infinity(), v);
  return std::nextafter(v, -std::numeric_limits<T>::infinity());
}

template <typename T>
T next_larger(T v)
  requires std::is_floating_point<T>::value
{
  DCHECK(!std::isnan(v));
  DCHECK_LT(v, std::numeric_limits<T>::infinity());
  return std::nextafter(v, std::numeric_limits<T>::infinity());
}

template <typename T>
T next_smaller(T v)
  requires std::is_integral<T>::value
{
  DCHECK_LT(std::numeric_limits<T>::min(), v);
  return v - 1;
}

template <typename T>
T next_larger(T v)
  requires std::is_integral<T>::value
{
  DCHECK_LT(v, std::numeric_limits<T>::max());
  return v + 1;
}

template <size_t Bits>
using uint_type = typename detail::TypeForBits<Bits>::uint_type;
template <size_t Bits>
using float_type = typename detail::TypeForBits<Bits>::float_type;
template <size_t Bits>
constexpr float_type<Bits> nan_v = detail::TypeForBits<Bits>::nan;

template <size_t Bits>
class WordType;
template <size_t Bits>
class FloatType;
class TupleType;

using Word32Type = WordType<32>;
using Word64Type = WordType<64>;
using Float32Type = FloatType<32>;
using Float64Type = FloatType<64>;

class V8_EXPORT_PRIVATE Type {
 public:
  enum class Kind : uint8_t {
    kInvalid,
    kNone,
    kWord32,
    kWord64,
    kFloat32,
    kFloat64,
    kTuple,
    kAny,
  };

  // Some operations cannot express the result precisely in a type, e.g. when an
  // intersection with a wrapping range may produce to disconnect subranges,
  // which cannot be represented. {ResolutionMode} allows to specify what the
  // operation should do when the result cannot be represented precisely.
  enum class ResolutionMode {
    // Return Type::Invalid().
    kPreciseOrInvalid,
    // Return a safe over approximation.
    kOverApproximate,
    // Return the greatest lower bound that can be represented.
    kGreatestLowerBound,
  };

  Type() : Type(Kind::kInvalid) {}

  // Type constructors
  static inline Type Invalid() { return Type(); }
  static inline Type None() { return Type(Kind::kNone); }
  static inline Type Any() { return Type(Kind::kAny); }

  // Checks and casts
  inline Kind kind() const { return kind_; }
  inline bool IsInvalid() const { return kind_ == Kind::kInvalid; }
  inline bool IsNone() const { return kind_ == Kind::kNone; }
  inline bool IsWord32() const { return kind_ == Kind::kWord32; }
  inline bool IsWord64() const { return kind_ == Kind::kWord64; }
  inline bool IsFloat32() const { return kind_ == Kind::kFloat32; }
  inline bool IsFloat64() const { return kind_ == Kind::kFloat64; }
  inline bool IsTuple() const { return kind_ == Kind::kTuple; }
  inline bool IsAny() const { return kind_ == Kind::kAny; }
  template <size_t B>
  inline bool IsWord() const {
    static_assert(B == 32 || B == 64);
    if constexpr (B == 32)
      return IsWord32();
    else
      return IsWord64();
  }
  template <size_t B>
  inline bool IsFloat() const {
    static_assert(B == 32 || B == 64);
    if constexpr (B == 32)
      return IsFloat32();
    else
      return IsFloat64();
  }

  // Casts
  inline const Word32Type& AsWord32() const;
  inline const Word64Type& AsWord64() const;
  inline const Float32Type& AsFloat32() const;
  inline const Float64Type& AsFloat64() const;
  inline const TupleType& AsTuple() const;
  template <size_t B>
  inline const auto& AsWord() const {
    static_assert(B == 32 || B == 64);
    if constexpr (B == 32)
      return AsWord32();
    else
      return AsWord64();
  }
  template <size_t B>
  inline const auto& AsFloat() const {
    static_assert(B == 32 || B == 64);
    if constexpr (B == 32)
      return AsFloat32();
    else
      return AsFloat64();
  }

  // Comparison
  bool Equals(const Type& other) const;
  bool IsSubtypeOf(const Type& other) const;

  // Printing
  void PrintTo(std::ostream& stream) const;
  void Print() const;
  std::string ToString() const {
    std::stringstream stream;
    PrintTo(stream);
    return stream.str();
  }

  // Other functions
  static Type LeastUpperBound(const Type& lhs, const Type& rhs, Zone* zone);
  static std::optional<Type> ParseFromString(const std::string_view& str,
                                             Zone* zone);
  Handle<TurboshaftType> AllocateOnHeap(Factory* factory) const;

 protected:
  template <typename Payload>
  Type(Kind kind, uint8_t sub_kind, uint8_t set_size, uint32_t bitfield,
       uint8_t reserved, const Payload& payload)
      : kind_(kind),
        sub_kind_(sub_kind),
        set_size_(set_size),
        reserved_(reserved),
        bitfield_(bitfield) {
    static_assert(sizeof(Payload) <= sizeof(payload_));
    memcpy(&payload_[0], &payload, sizeof(Payload));
    if constexpr (sizeof(Payload) < sizeof(payload_)) {
      memset(reinterpret_cast<uint8_t*>(&payload_[0]) + sizeof(Payload), 0x00,
             sizeof(payload_) - sizeof(Payload));
    }
  }

  template <typename Payload>
  const Payload& get_payload() const {
    static_assert(sizeof(Payload) <= sizeof(payload_));
    return *reinterpret_cast<const Payload*>(&payload_[0]);
  }

  union {
    struct {
      Kind kind_;
      uint8_t sub_kind_;
      uint8_t set_size_;
      uint8_t reserved_;
      uint32_t bitfield_;
    };
    // {header_} can be  used for faster hashing or comparison.
    uint64_t header_;
  };

 private:
  // Access through get_payload<>().
  uint64_t payload_[2];  // Type specific data

  friend struct fast_hash<Type>;
  explicit Type(Kind kind) : Type(kind, 0, 0, 0, 0, detail::Payload_Empty{}) {
    DCHECK(kind == Kind::kInvalid || kind == Kind::kNone || kind == Kind::kAny);
  }
};
static_assert(sizeof(Type) == 24);

template <size_t Bits>
class WordType : public Type {
  static_assert(Bits == 32 || Bits == 64);
  friend class Type;
  static constexpr int kMaxInlineSetSize = 2;

  enum class SubKind : uint8_t {
    kRange,
    kSet,
  };

 public:
  static constexpr int kMaxSetSize = 8;
  using word_t = uint_type<Bits>;
  using value_type = word_t;

  // Constructors
  static WordType Any() {
    return Range(0, std::numeric_limits<word_t>::max(), nullptr);
  }
  static WordType Range(word_t from, word_t to, Zone* zone) {
    // Normalize ranges smaller than {kMaxSetSize} to sets.
    if (to >= from) {
      // (to - from + 1) <= kMaxSetSize
      if (to - from <= kMaxSetSize - 1) {
        // Normalizing non-wrapping ranges to a Set.
        base::SmallVector<word_t, kMaxSetSize> elements;
        for (word_t i = from; i < to; ++i) elements.push_back(i);
        elements.push_back(to);
        return Set(elements, zone);
      }
    } else {
      // (max - from + 1) + (to + 1) <= kMaxSetSize
      if ((std::numeric_limits<word_t>::max() - from + to) <= kMaxSetSize - 2) {
        // Normalizing wrapping ranges to a Set.
        base::SmallVector<word_t, kMaxSetSize> elements;
        for (word_t i = from; i < std::numeric_limits<word_t>::max(); ++i) {
          elements.push_back(i);
        }
        elements.push_back(std::numeric_limits<word_t>::max());
        for (word_t i = 0; i < to; ++i) elements.push_back(i);
        elements.push_back(to);
        base::sort(elements);
        return Set(elements, zone);
      }
    }
    return WordType{SubKind::kRange, 0, Payload_Range{from, to}};
  }
  template <size_t N>
  static WordType Set(const base::SmallVector<word_t, N>& elements,
                      Zone* zone) {
    return Set(base::VectorOf(elements), zone);
  }
  static WordType Set(const std::vector<word_t>& elements, Zone* zone) {
    return Set(base::VectorOf(elements), zone);
  }
  static WordType Set(const std::initializer_list<word_t>& elements,
                      Zone* zone) {
    return Set(base::VectorOf(elements), zone);
  }
  static WordType Set(base::Vector<const word_t> elements, Zone* zone) {
    DCHECK(detail::is_unique_and_sorted(elements));
    DCHECK_IMPLIES(elements.size() > kMaxInlineSetSize, zone != nullptr);
    DCHECK_GT(elements.size(), 0);
    DCHECK_LE(elements.size(), kMaxSetSize);

    if (elements.size() <= kMaxInlineSetSize) {
      // Use inline storage.
      Payload_InlineSet p;
      DCHECK_LT(0, elements.size());
      p.elements[0] = elements[0];
      if (elements.size() > 1) p.elements[1] = elements[1];
      return WordType{SubKind::kSet, static_cast<uint8_t>(elements.size()), p};
    } else {
      // Allocate storage in the zone.
#if defined(__GNUC__) && !defined(__clang__)
      // Work around a spurious GCC-12 warning. The DCHECK above already
      // checks the right precondition.
      if (zone == nullptr) return WordType::Any();
#endif
      Payload_OutlineSet p;
      p.array = zone->AllocateArray<word_t>(elements.size());
      DCHECK_NOT_NULL(p.array);
      for (size_t i = 0; i < elements.size(); ++i) p.array[i] = elements[i];
      return WordType{SubKind::kSet, static_cast<uint8_t>(elements.size()), p};
    }
  }
  static WordType Constant(word_t constant) { return Set({constant}, nullptr); }

  // Checks
  bool is_range() const { return sub_kind() == SubKind::kRange; }
  bool is_set() const { return sub_kind() == SubKind::kSet; }
  bool is_any() const { return is_range() && range_to() + 1 == range_from(); }
  bool is_constant() const {
    DCHECK_EQ(set_size_ > 0, is_set());
    return set_size_ == 1;
  }
  bool is_wrapping() const { return is_range() && range_from() > range_to(); }

  // Accessors
  word_t range_from() const {
    DCHECK(is_range());
    return get_payload<Payload_Range>().min;
  }
  word_t range_to() const {
    DCHECK(is_range());
    return get_payload<Payload_Range>().max;
  }
  std::pair<word_t, word_t> range() const {
    DCHECK(is_range());
    return {range_from(), range_to()};
  }
  int set_size() const {
    DCHECK(is_set());
    return static_cast<int>(set_size_);
  }
  word_t set_element(int index) const {
    DCHECK(is_set());
    DCHECK_GE(index, 0);
    DCHECK_LT(index, set_size());
    return set_elements()[index];
  }
  base::Vector<const word_t> set_elements() const {
    DCHECK(is_set());
    if (set_size() <= kMaxInlineSetSize) {
      return base::Vector<const word_t>(
          get_payload<Payload_InlineSet>().elements, set_size());
    } else {
      return base::Vector<const word_t>(get_payload<Payload_OutlineSet>().array,
                                        set_size());
    }
  }
  std::optional<word_t> try_get_constant() const {
    if (!is_constant()) return std::nullopt;
    DCHECK(is_set());
    DCHECK_EQ(set_size(), 1);
    return set_element(0);
  }
  bool is_constant(word_t value) const {
    if (auto c = try_get_constant()) return *c == value;
    return false;
  }
  word_t unsigned_min() const {
    switch (sub_kind()) {
      case SubKind::kRange:
        return is_wrapping() ? word_t{0} : range_from();
      case SubKind::kSet:
        return set_element(0);
    }
  }
  word_t unsigned_max() const {
    switch (sub_kind()) {
      case SubKind::kRange:
        return is_wrapping() ? std::numeric_limits<word_t>::max() : range_to();
      case SubKind::kSet:
        DCHECK_GE(set_size(), 1);
        return set_element(set_size() - 1);
    }
  }

  // Misc
  bool Contains(word_t value) const;
  bool Equals(const WordType& other) const;
  bool IsSubtypeOf(const WordType& other) const;
  static WordType LeastUpperBound(const WordType& lhs, const WordType& rhs,
                                  Zone* zone);
  static Type Intersect(const WordType& lhs, const WordType& rhs,
                        ResolutionMode resolution_mode, Zone* zone);
  void PrintTo(std::ostream& stream) const;
  Handle<TurboshaftType> AllocateOnHeap(Factory* factory) const;

 private:
  static constexpr Kind KIND = Bits == 32 ? Kind::kWord32 : Kind::kWord64;
  using Payload_Range = detail::Payload_Range<word_t>;
  using Payload_InlineSet = detail::Payload_InlineSet<word_t>;
  using Payload_OutlineSet = detail::Payload_OutlineSet<word_t>;

  SubKind sub_kind() const { return static_cast<SubKind>(sub_kind_); }
  template <typename Payload>
  WordType(SubKind sub_kind, uint8_t set_size, const Payload& payload)
      : Type(KIND, static_cast<uint8_t>(sub_kind), set_size, 0, 0, payload) {}
};

extern template class EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE) WordType<32>;
extern template class EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE) WordType<64>;

template <size_t Bits>
class FloatType : public Type {
  static_assert(Bits == 32 || Bits == 64);
  friend class Type;
  static constexpr int kMaxInlineSetSize = 2;

  enum class SubKind : uint8_t {
    kRange,
    kSet,
    kOnlySpecialValues,
  };

 public:
  static constexpr int kMaxSetSize = 8;
  using float_t = float_type<Bits>;
  using value_type = float_t;

  enum Special : uint32_t {
    kNoSpecialValues = 0x0,
    kNaN = 0x1,
    kMinusZero = 0x2,
  };

  // Constructors
  static FloatType OnlySpecialValues(uint32_t special_values) {
    DCHECK_NE(0, special_values);
    return FloatType{SubKind::kOnlySpecialValues, 0, special_values,
                     Payload_OnlySpecial{}};
  }
  static FloatType NaN() {
    return FloatType{SubKind::kOnlySpecialValues, 0, Special::kNaN,
                     Payload_OnlySpecial{}};
  }
  static FloatType MinusZero() {
    return FloatType{SubKind::kOnlySpecialValues, 0, Special::kMinusZero,
                     Payload_OnlySpecial{}};
  }
  static FloatType Any(uint32_t special_values = Special::kNaN |
                                                 Special::kMinusZero) {
    return FloatType::Range(-std::numeric_limits<float_t>::infinity(),
                            std::numeric_limits<float_t>::infinity(),
                            special_values, nullptr);
  }
  static FloatType Range(float_t min, float_t max, Zone* zone) {
    return Range(min, max, Special::kNoSpecialValues, zone);
  }
  static FloatType Range(float_t min, float_t max, uint32_t special_values,
                         Zone* zone) {
    special_values |= IdentifyMinusZero(min);
    special_values |= IdentifyMinusZero(max);
    DCHECK(!detail::is_float_special_value(min));
    DCHECK(!detail::is_float_special_value(max));
    DCHECK_LE(min, max);
    if (min == max) return Set({min}, special_values, zone);
    return FloatType{SubKind::kRange, 0, special_values,
                     Payload_Range{min, max}};
  }
  template <size_t N>
  static FloatType Set(const base::SmallVector<const float_t, N>& elements,
                       Zone* zone) {
    return Set(elements, Special::kNoSpecialValues, zone);
  }
  template <size_t N>
  static FloatType Set(const base::SmallVector<float_t, N>& elements,
                       uint32_t special_values, Zone* zone) {
    return Set(base::VectorOf(elements), special_values, zone);
  }
  static FloatType Set(const std::initializer_list<float_t>& elements,
                       uint32_t special_values, Zone* zone) {
    return Set(base::VectorOf(elements), special_values, zone);
  }
  static FloatType Set(const std::vector<float_t>& elements, Zone* zone) {
    return Set(elements, Special::kNoSpecialValues, zone);
  }
  static FloatType Set(const std::vector<float_t>& elements,
                       uint32_t special_values, Zone* zone) {
    return Set(base::VectorOf(elements), special_values, zone);
  }
  static FloatType Set(base::Vector<const float_t> elements,
                       uint32_t special_values, Zone* zone) {
    DCHECK(detail::is_unique_and_sorted(elements));
    // NaN should be passed via {special_values} rather than {elements}.
    DCHECK(base::none_of(elements, [](float_t f) { return std::isnan(f); }));
    DCHECK_IMPLIES(elements.size() > kMaxInlineSetSize, zone != nullptr);
    DCHECK_GT(elements.size(), 0);
    DCHECK_LE(elements.size(), kMaxSetSize);

    if (elements.size() <= kMaxInlineSetSize) {
      // Use inline storage.
      Payload_InlineSet p;
      DCHECK_LT(0, elements.size());
      p.elements[0] = elements[0];
      special_values |= IdentifyMinusZero(p.elements[0]);
      if (elements.size() > 1) {
        p.elements[1] = elements[1];
        special_values |= IdentifyMinusZero(p.elements[1]);
      }
      return FloatType{SubKind::kSet, static_cast<uint8_t>(elements.size()),
                       special_values, p};
    } else {
      // Allocate storage in the zone.
      Payload_OutlineSet p;
      p.array = zone->AllocateArray<float_t>(elements.size());
      DCHECK_NOT_NULL(p.array);
      for (size_t i = 0; i < elements.size(); ++i) {
        p.array[i] = elements[i];
        special_values |= IdentifyMinusZero(p.array[i]);
      }
      return FloatType{SubKind::kSet, static_cast<uint8_t>(elements.size()),
                       special_values, p};
    }
  }
  static FloatType Constant(float_t constant) {
    return Set({constant}, 0, nullptr);
  }

  // Checks
  bool is_only_special_values() const {
    return sub_kind() == SubKind::kOnlySpecialValues;
  }
  bool is_only_nan() const {
    return is_only_special_values() && (special_values() == Special::kNaN);
  }
  bool is_only_minus_zero() const {
    return is_only_special_values() &&
           (special_values() == Special::kMinusZero);
  }
  bool is_range() const { return sub_kind() == SubKind::kRange; }
  bool is_set() const { return sub_kind() == SubKind::kSet; }
  bool is_any() const {
    return is_range() &&
           range_min() == -std::numeric_limits<float_t>::infinity() &&
           range_max() == std::numeric_limits<float_t>::infinity();
  }
  bool is_constant() const {
    DCHECK_EQ(set_size_ > 0, is_set());
    return set_size_ == 1 && !has_special_values();
  }
  uint32_t special_values() const { return bitfield_; }
  bool has_special_values() const { return special_values() != 0; }
  bool has_nan() const { return (special_values() & Special::kNaN) != 0; }
  bool has_minus_zero() const {
    return (special_values() & Special::kMinusZero) != 0;
  }

  // Accessors
  float_t range_min() const {
    DCHECK(is_range());
    return get_payload<Payload_Range>().min;
  }
  float_t range_max() const {
    DCHECK(is_range());
    return get_payload<Payload_Range>().max;
  }
  std::pair<float_t, float_t> range() const {
    DCHECK(is_range());
    return {range_min(), range_max()};
  }
  int set_size() const {
    DCHECK(is_set());
    return static_cast<int>(set_size_);
  }
  float_t set_element(int index) const {
    DCHECK(is_set());
    DCHECK_GE(index, 0);
    DCHECK_LT(index, set_size());
    return set_elements()[index];
  }
  base::Vector<const float_t> set_elements() const {
    DCHECK(is_set());
    if (set_size() <= kMaxInlineSetSize) {
      return base::Vector<const float_t>(
          get_payload<Payload_InlineSet>().elements, set_size());
    } else {
      return base::Vector<const float_t>(
          get_payload<Payload_OutlineSet>().array, set_size());
    }
  }
  float_t min() const {
    switch (sub_kind()) {
      case SubKind::kOnlySpecialValues:
        if (has_minus_zero()) return float_t{-0.0};
        DCHECK(is_only_nan());
        return nan_v<Bits>;
      case SubKind::kRange:
        if (has_minus_zero()) return std::min(float_t{-0.0}, range_min());
        return range_min();
      case SubKind::kSet:
        if (has_minus_zero()) return std::min(float_t{-0.0}, set_element(0));
        return set_element(0);
    }
  }
  float_t max() const {
    switch (sub_kind()) {
      case SubKind::kOnlySpecialValues:
        if (has_minus_zero()) return float_t{-0.0};
        DCHECK(is_only_nan());
        return nan_v<Bits>;
      case SubKind::kRange:
        if (has_minus_zero()) return std::max(float_t{-0.0}, range_max());
        return range_max();
      case SubKind::kSet:
        if (has_minus_zero()) {
          return std::max(float_t{-0.0}, set_element(set_size() - 1));
        }
        return set_element(set_size() - 1);
    }
  }
  std::pair<float_t, float_t> minmax() const { return {min(), max()}; }
  std::optional<float_t> try_get_constant() const {
    if (!is_constant()) return std::nullopt;
    DCHECK(is_set());
    DCHECK_EQ(set_size(), 1);
    return set_element(0);
  }
  bool is_constant(float_t value) const {
    if (V8_UNLIKELY(std::isnan(value))) return is_only_nan();
    if (V8_UNLIKELY(IsMinusZero(value))) return is_only_minus_zero();
    if (auto c = try_get_constant()) return *c == value;
    return false;
  }
  // Returns the minimium value of a range or set, ignoring any special values
  // (in contrast to min() above).
  float_t range_or_set_min() const {
    switch (sub_kind()) {
      case SubKind::kOnlySpecialValues:
        UNREACHABLE();
      case SubKind::kRange:
        return range_min();
      case SubKind::kSet:
        return set_element(0);
    }
  }
  // Returns the maximum value of a range or set, ignoring any special values
  // (in contrast to max() above).
  float_t range_or_set_max() const {
    switch (sub_kind()) {
      case SubKind::kOnlySpecialValues:
        UNREACHABLE();
      case SubKind::kRange:
        return range_max();
      case SubKind::kSet:
        return set_element(set_size() - 1);
    }
  }
  std::pair<float_t, float_t> range_or_set_minmax() const {
    return {range_or_set_min(), range_or_set_max()};
  }

  // Misc
  bool Contains(float_t value) const;
  bool Equals(const FloatType& other) const;
  bool IsSubtypeOf(const FloatType& other) const;
  static FloatType LeastUpperBound(const FloatType& lhs, const FloatType& rhs,
                                   Zone* zone);
  static Type Intersect(const FloatType& lhs, const FloatType& rhs, Zone* zone);
  void PrintTo(std::ostream& stream) const;
  Handle<TurboshaftType> AllocateOnHeap(Factory* factory) const;

 private:
  // This helper turns a -0 into a 0 in {value} and returns the
  // Special::kMinusZero flag in that case. Otherwise the {value} is unchanged
  // and Special::kNoSpecialValues is returned.
  static uint32_t IdentifyMinusZero(float_t& value) {
    if (V8_UNLIKELY(detail::is_minus_zero(value))) {
      value = float_t{0};
      return Special::kMinusZero;
    }
    return Special::kNoSpecialValues;
  }
  static Type ReplacedSpecialValues(const FloatType& t,
                                    uint32_t special_values) {
    if (special_values == 0 && t.is_only_special_values()) {
      return FloatType::None();
    }
    auto result = t;
    result.bitfield_ = special_values;
    DCHECK_EQ(result.bitfield_, result.special_values());
    return result;
  }

  static constexpr Kind KIND = Bits == 32 ? Kind::kFloat32 : Kind::kFloat64;
  SubKind sub_kind() const { return static_cast<SubKind>(sub_kind_); }
  using Payload_Range = detail::Payload_Range<float_t>;
  using Payload_InlineSet = detail::Payload_InlineSet<float_t>;
  using Payload_OutlineSet = detail::Payload_OutlineSet<float_t>;
  using Payload_OnlySpecial = detail::Payload_Empty;

  template <typename Payload>
  FloatType(SubKind sub_kind, uint8_t set_size, uint32_t special_values,
            const Payload& payload)
      : Type(KIND, static_cast<uint8_t>(sub_kind), set_size, special_values, 0,
             payload) {
    DCHECK_EQ(special_values & ~(Special::kNaN | Special::kMinusZero), 0);
  }
};

extern template class EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE) FloatType<32>;
extern template class EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE) FloatType<64>;

class TupleType : public Type {
 public:
  static constexpr int kMaxTupleSize = std::numeric_limits<uint8_t>::max();

  // Constructors
  static TupleType Tuple(const Type& element0, const Type& element1,
                         Zone* zone) {
    Payload p;
    p.array = zone->AllocateArray<Type>(2);
    DCHECK_NOT_NULL(p.array);
    p.array[0] = element0;
    p.array[1] = element1;
    return TupleType{2, p};
  }

  static TupleType Tuple(base::Vector<Type> elements, Zone* zone) {
    DCHECK_LE(elements.size(), kMaxTupleSize);
    Payload p;
    p.array = zone->AllocateArray<Type>(elements.size());
    DCHECK_NOT_NULL(p.array);
    for (size_t i = 0; i < elements.size(); ++i) {
      p.array[i] = elements[i];
    }
    return TupleType{static_cast<uint8_t>(elements.size()), p};
  }

  // Accessors
  int size() const { return static_cast<int>(set_size_); }
  const Type& element(int index) const {
    DCHECK_LE(0, index);
    DCHECK_LT(index, size());
    return get_payload<Payload>().array[index];
  }
  base::Vector<Type> elements() const {
    return {get_payload<Payload>().array, static_cast<size_t>(size())};
  }

  // Misc
  bool Equals(const TupleType& other) const;
  bool IsSubtypeOf(const TupleType& other) const;
  static Type LeastUpperBound(const TupleType& lhs, const TupleType& rhs,
                              Zone* zone);
  void PrintTo(std::ostream& stream) const;

 private:
  static constexpr Kind KIND = Kind::kTuple;
  using Payload = detail::Payload_OutlineSet<Type>;

  TupleType(uint8_t tuple_size, const Payload& payload)
      : Type(KIND, 0, tuple_size, 0, 0, payload) {}
};

const Word32Type& Type::AsWord32() const {
  DCHECK(IsWord32());
  return *static_cast<const Word32Type*>(this);
}

const Word64Type& Type::AsWord64() const {
  DCHECK(IsWord64());
  return *static_cast<const Word64Type*>(this);
}

const Float32Type& Type::AsFloat32() const {
  DCHECK(IsFloat32());
  return *static_cast<const Float32Type*>(this);
}

const Float64Type& Type::AsFloat64() const {
  DCHECK(IsFloat64());
  return *static_cast<const Float64Type*>(this);
}

const TupleType& Type::AsTuple() const {
  DCHECK(IsTuple());
  return *static_cast<const TupleType*>(this);
}

inline std::ostream& operator<<(std::ostream& stream, Type::Kind kind) {
  switch (kind) {
    case Type::Kind::kInvalid:
      return stream << "Invalid";
    case Type::Kind::kNone:
      return stream << "None";
    case Type::Kind::kWord32:
      return stream << "Word32";
    case Type::Kind::kWord64:
      return stream << "Word64";
    case Type::Kind::kFloat32:
      return stream << "Float32";
    case Type::Kind::kFloat64:
      return stream << "Float64";
    case Type::Kind::kTuple:
      return stream << "Tuple";
    case Type::Kind::kAny:
      return stream << "Any";
  }
}

inline std::ostream& operator<<(std::ostream& stream, const Type& type) {
  type.PrintTo(stream);
  return stream;
}

inline bool operator==(const Type& lhs, const Type& rhs) {
  return lhs.Equals(rhs);
}

inline bool operator!=(const Type& lhs, const Type& rhs) {
  return !lhs.Equals(rhs);
}

template <>
struct fast_hash<Type> {
  size_t operator()(const Type& v) const {
    // TODO(nicohartmann@): Fix fast_hash for outline payload once this is
    // required.
    UNREACHABLE();
    // return fast_hash_combine(v.header_, v.payload_[0], v.payload_[1]);
  }
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_TYPES_H_
