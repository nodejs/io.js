// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TEST_COMMON_FLAG_UTILS_H
#define V8_TEST_COMMON_FLAG_UTILS_H

#include "src/base/macros.h"
#include "src/flags/flags.h"

namespace v8 {
namespace internal {

template <typename T>
class V8_NODISCARD FlagScope {
 public:
  FlagScope(FlagValue<T>* flag, T new_value)
      : flag_(flag), previous_value_(*flag) {
    *flag = new_value;
  }
  ~FlagScope() { *flag_ = previous_value_; }

 private:
  FlagValue<T>* flag_;
  T previous_value_;
};

}  // namespace internal
}  // namespace v8

#define FLAG_VALUE_SCOPE(flag, value)                                    \
  ::v8::internal::FlagScope<                                             \
      typename decltype(::v8::internal::v8_flags.flag)::underlying_type> \
  UNIQUE_IDENTIFIER(__scope_##flag)(&::v8::internal::v8_flags.flag, value)

#define FLAG_SCOPE(flag) FLAG_VALUE_SCOPE(flag, true)

#endif  // V8_TEST_COMMON_FLAG_UTILS_H
