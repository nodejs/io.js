// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_NUMBERS_INTEGER_LITERAL_H_
#define V8_NUMBERS_INTEGER_LITERAL_H_

#include <optional>

#include "src/common/globals.h"

namespace v8 {
namespace internal {

class IntegerLiteral {
 public:
  IntegerLiteral(bool negative, uint64_t absolute_value)
      : negative_(negative), absolute_value_(absolute_value) {
    if (absolute_value == 0) negative_ = false;
  }

  template <typename T>
  explicit IntegerLiteral(T value) : IntegerLiteral(value, true) {}

  bool is_negative() const { return negative_; }
  uint64_t absolute_value() const { return absolute_value_; }

  template <typename T>
  bool IsRepresentableAs() const {
    static_assert(std::is_integral<T>::value, "Integral type required");
    static_assert(sizeof(T) <= sizeof(uint64_t),
                  "Types with more than 64 bits are not supported");
    return Compare(IntegerLiteral(std::numeric_limits<T>::min(), false)) >= 0 &&
           Compare(IntegerLiteral(std::numeric_limits<T>::max(), false)) <= 0;
  }

  template <typename T>
  T To() const {
    static_assert(std::is_integral<T>::value, "Integral type required");
    DCHECK(IsRepresentableAs<T>());
    uint64_t v = absolute_value_;
    if (negative_) v = ~v + 1;
    return static_cast<T>(v);
  }

  template <typename T>
  std::optional<T> TryTo() const {
    static_assert(std::is_integral<T>::value, "Integral type required");
    if (!IsRepresentableAs<T>()) return std::nullopt;
    return To<T>();
  }

  int Compare(const IntegerLiteral& other) const {
    if (absolute_value_ == other.absolute_value_) {
      if (absolute_value_ == 0 || negative_ == other.negative_) return 0;
      return negative_ ? -1 : 1;
    } else if (absolute_value_ < other.absolute_value_) {
      return other.negative_ ? 1 : -1;
    } else {
      return negative_ ? -1 : 1;
    }
  }

  std::string ToString() const;

 private:
  template <typename T>
  explicit IntegerLiteral(T value, bool perform_dcheck) : negative_(false) {
    static_assert(std::is_integral<T>::value, "Integral type required");
    absolute_value_ = static_cast<uint64_t>(value);
    if (value < T(0)) {
      negative_ = true;
      absolute_value_ = ~absolute_value_ + 1;
    }
    if (perform_dcheck) DCHECK_EQ(To<T>(), value);
  }

  bool negative_;
  uint64_t absolute_value_;
};

inline bool operator==(const IntegerLiteral& x, const IntegerLiteral& y) {
  return x.Compare(y) == 0;
}

inline bool operator!=(const IntegerLiteral& x, const IntegerLiteral& y) {
  return x.Compare(y) != 0;
}

inline std::ostream& operator<<(std::ostream& stream,
                                const IntegerLiteral& literal) {
  return stream << literal.ToString();
}

inline IntegerLiteral operator|(const IntegerLiteral& x,
                                const IntegerLiteral& y) {
  DCHECK(!x.is_negative());
  DCHECK(!y.is_negative());
  return IntegerLiteral(false, x.absolute_value() | y.absolute_value());
}

IntegerLiteral operator<<(const IntegerLiteral& x, const IntegerLiteral& y);
IntegerLiteral operator+(const IntegerLiteral& x, const IntegerLiteral& y);

}  // namespace internal
}  // namespace v8
#endif  // V8_NUMBERS_INTEGER_LITERAL_H_
