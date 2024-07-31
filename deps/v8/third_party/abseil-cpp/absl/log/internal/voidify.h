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
// File: log/internal/voidify.h
// -----------------------------------------------------------------------------
//
// This class is used to explicitly ignore values in the conditional logging
// macros. This avoids compiler warnings like "value computed is not used" and
// "statement has no effect".

#ifndef ABSL_LOG_INTERNAL_VOIDIFY_H_
#define ABSL_LOG_INTERNAL_VOIDIFY_H_

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {

class Voidify final {
 public:
  // This has to be an operator with a precedence lower than << but higher than
  // ?:
  template <typename T>
  void operator&&(const T&) const&& {}
};

}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_LOG_INTERNAL_VOIDIFY_H_
