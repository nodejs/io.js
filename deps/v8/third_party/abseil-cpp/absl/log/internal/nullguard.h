// Copyright 2022 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// -----------------------------------------------------------------------------
// File: log/internal/nullguard.h
// -----------------------------------------------------------------------------
//
// NullGuard exists such that NullGuard<T>::Guard(v) returns v, unless passed a
// nullptr_t, or a null char* or const char*, in which case it returns "(null)".
// This allows streaming NullGuard<T>::Guard(v) to an output stream without
// hitting undefined behavior for null values.

#ifndef ABSL_LOG_INTERNAL_NULLGUARD_H_
#define ABSL_LOG_INTERNAL_NULLGUARD_H_

#include <array>
#include <cstddef>

#include "absl/base/attributes.h"
#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {

ABSL_CONST_INIT ABSL_DLL extern const std::array<char, 7> kCharNull;
ABSL_CONST_INIT ABSL_DLL extern const std::array<signed char, 7>
    kSignedCharNull;
ABSL_CONST_INIT ABSL_DLL extern const std::array<unsigned char, 7>
    kUnsignedCharNull;

template <typename T>
struct NullGuard final {
  static const T& Guard(const T& v) { return v; }
};
template <>
struct NullGuard<char*> final {
  static const char* Guard(const char* v) { return v ? v : kCharNull.data(); }
};
template <>
struct NullGuard<const char*> final {
  static const char* Guard(const char* v) { return v ? v : kCharNull.data(); }
};
template <>
struct NullGuard<signed char*> final {
  static const signed char* Guard(const signed char* v) {
    return v ? v : kSignedCharNull.data();
  }
};
template <>
struct NullGuard<const signed char*> final {
  static const signed char* Guard(const signed char* v) {
    return v ? v : kSignedCharNull.data();
  }
};
template <>
struct NullGuard<unsigned char*> final {
  static const unsigned char* Guard(const unsigned char* v) {
    return v ? v : kUnsignedCharNull.data();
  }
};
template <>
struct NullGuard<const unsigned char*> final {
  static const unsigned char* Guard(const unsigned char* v) {
    return v ? v : kUnsignedCharNull.data();
  }
};
template <>
struct NullGuard<std::nullptr_t> final {
  static const char* Guard(const std::nullptr_t&) { return kCharNull.data(); }
};

}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_LOG_INTERNAL_NULLGUARD_H_
