// Copyright 2019 The Abseil Authors.
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

#include "absl/strings/internal/cordz_sample_token.h"

#include "absl/base/config.h"
#include "absl/strings/internal/cordz_handle.h"
#include "absl/strings/internal/cordz_info.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

CordzSampleToken::Iterator& CordzSampleToken::Iterator::operator++() {
  if (current_) {
    current_ = current_->Next(*token_);
  }
  return *this;
}

CordzSampleToken::Iterator CordzSampleToken::Iterator::operator++(int) {
  Iterator it(*this);
  operator++();
  return it;
}

bool operator==(const CordzSampleToken::Iterator& lhs,
                const CordzSampleToken::Iterator& rhs) {
  return lhs.current_ == rhs.current_ &&
         (lhs.current_ == nullptr || lhs.token_ == rhs.token_);
}

bool operator!=(const CordzSampleToken::Iterator& lhs,
                const CordzSampleToken::Iterator& rhs) {
  return !(lhs == rhs);
}

CordzSampleToken::Iterator::reference CordzSampleToken::Iterator::operator*()
    const {
  return *current_;
}

CordzSampleToken::Iterator::pointer CordzSampleToken::Iterator::operator->()
    const {
  return current_;
}

CordzSampleToken::Iterator::Iterator(const CordzSampleToken* token)
    : token_(token), current_(CordzInfo::Head(*token)) {}

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
