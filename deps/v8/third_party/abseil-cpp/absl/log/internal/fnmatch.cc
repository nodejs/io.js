// Copyright 2023 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/log/internal/fnmatch.h"

#include <cstddef>

#include "absl/base/config.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {
bool FNMatch(absl::string_view pattern, absl::string_view str) {
  bool in_wildcard_match = false;
  while (true) {
    if (pattern.empty()) {
      // `pattern` is exhausted; succeed if all of `str` was consumed matching
      // it.
      return in_wildcard_match || str.empty();
    }
    if (str.empty()) {
      // `str` is exhausted; succeed if `pattern` is empty or all '*'s.
      return pattern.find_first_not_of('*') == pattern.npos;
    }
    switch (pattern.front()) {
      case '*':
        pattern.remove_prefix(1);
        in_wildcard_match = true;
        break;
      case '?':
        pattern.remove_prefix(1);
        str.remove_prefix(1);
        break;
      default:
        if (in_wildcard_match) {
          absl::string_view fixed_portion = pattern;
          const size_t end = fixed_portion.find_first_of("*?");
          if (end != fixed_portion.npos) {
            fixed_portion = fixed_portion.substr(0, end);
          }
          const size_t match = str.find(fixed_portion);
          if (match == str.npos) {
            return false;
          }
          pattern.remove_prefix(fixed_portion.size());
          str.remove_prefix(match + fixed_portion.size());
          in_wildcard_match = false;
        } else {
          if (pattern.front() != str.front()) {
            return false;
          }
          pattern.remove_prefix(1);
          str.remove_prefix(1);
        }
        break;
    }
  }
}
}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl
