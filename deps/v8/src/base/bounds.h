// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_BOUNDS_H_
#define V8_BASE_BOUNDS_H_

#include "include/v8config.h"
#include "src/base/macros.h"

namespace v8 {
namespace base {

// Checks if value is in range [lower_limit, higher_limit] using a single
// branch.
template <typename T, typename U>
  requires((std::is_integral_v<T> || std::is_enum_v<T>) &&
           (std::is_integral_v<U> || std::is_enum_v<U>)) &&
          (sizeof(U) <= sizeof(T))
inline constexpr bool IsInRange(T value, U lower_limit, U higher_limit) {
  DCHECK_LE(lower_limit, higher_limit);
  using unsigned_T = typename std::make_unsigned<T>::type;
  // Use static_cast to support enum classes.
  return static_cast<unsigned_T>(static_cast<unsigned_T>(value) -
                                 static_cast<unsigned_T>(lower_limit)) <=
         static_cast<unsigned_T>(static_cast<unsigned_T>(higher_limit) -
                                 static_cast<unsigned_T>(lower_limit));
}

// Like IsInRange but for the half-open range [lower_limit, higher_limit).
template <typename T, typename U>
  requires((std::is_integral_v<T> || std::is_enum_v<T>) &&
           (std::is_integral_v<U> || std::is_enum_v<U>)) &&
          (sizeof(U) <= sizeof(T))
inline constexpr bool IsInHalfOpenRange(T value, U lower_limit,
                                        U higher_limit) {
  DCHECK_LE(lower_limit, higher_limit);
  using unsigned_T = typename std::make_unsigned<T>::type;
  // Use static_cast to support enum classes.
  return static_cast<unsigned_T>(static_cast<unsigned_T>(value) -
                                 static_cast<unsigned_T>(lower_limit)) <
         static_cast<unsigned_T>(static_cast<unsigned_T>(higher_limit) -
                                 static_cast<unsigned_T>(lower_limit));
}

// Checks if [index, index+length) is in range [0, max). Note that this check
// works even if {index+length} would wrap around.
template <typename T>
inline constexpr bool IsInBounds(T index, T length, T max)
  requires std::is_unsigned<T>::value
{
  return length <= max && index <= (max - length);
}

// Checks if [index, index+length) is in range [0, max). If not, {length} is
// clamped to its valid range. Note that this check works even if
// {index+length} would wrap around.
template <typename T>
inline bool ClampToBounds(T index, T* length, T max) {
  if (index > max) {
    *length = 0;
    return false;
  }
  T avail = max - index;
  bool oob = *length > avail;
  if (oob) *length = avail;
  return !oob;
}

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_BOUNDS_H_
