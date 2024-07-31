// Copyright 2020 The Abseil Authors.
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
#include "absl/status/statusor.h"

#include <cstdlib>
#include <utility>

#include "absl/base/call_once.h"
#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/nullability.h"
#include "absl/status/internal/statusor_internal.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

BadStatusOrAccess::BadStatusOrAccess(absl::Status status)
    : status_(std::move(status)) {}

BadStatusOrAccess::BadStatusOrAccess(const BadStatusOrAccess& other)
    : status_(other.status_) {}

BadStatusOrAccess& BadStatusOrAccess::operator=(
    const BadStatusOrAccess& other) {
  // Ensure assignment is correct regardless of whether this->InitWhat() has
  // already been called.
  other.InitWhat();
  status_ = other.status_;
  what_ = other.what_;
  return *this;
}

BadStatusOrAccess& BadStatusOrAccess::operator=(BadStatusOrAccess&& other) {
  // Ensure assignment is correct regardless of whether this->InitWhat() has
  // already been called.
  other.InitWhat();
  status_ = std::move(other.status_);
  what_ = std::move(other.what_);
  return *this;
}

BadStatusOrAccess::BadStatusOrAccess(BadStatusOrAccess&& other)
    : status_(std::move(other.status_)) {}

absl::Nonnull<const char*> BadStatusOrAccess::what() const noexcept {
  InitWhat();
  return what_.c_str();
}

const absl::Status& BadStatusOrAccess::status() const { return status_; }

void BadStatusOrAccess::InitWhat() const {
  absl::call_once(init_what_, [this] {
    what_ = absl::StrCat("Bad StatusOr access: ", status_.ToString());
  });
}

namespace internal_statusor {

void Helper::HandleInvalidStatusCtorArg(absl::Nonnull<absl::Status*> status) {
  const char* kMessage =
      "An OK status is not a valid constructor argument to StatusOr<T>";
#ifdef NDEBUG
  ABSL_INTERNAL_LOG(ERROR, kMessage);
#else
  ABSL_INTERNAL_LOG(FATAL, kMessage);
#endif
  // In optimized builds, we will fall back to InternalError.
  *status = absl::InternalError(kMessage);
}

void Helper::Crash(const absl::Status& status) {
  ABSL_INTERNAL_LOG(
      FATAL,
      absl::StrCat("Attempting to fetch value instead of handling error ",
                   status.ToString()));
}

void ThrowBadStatusOrAccess(absl::Status status) {
#ifdef ABSL_HAVE_EXCEPTIONS
  throw absl::BadStatusOrAccess(std::move(status));
#else
  ABSL_INTERNAL_LOG(
      FATAL,
      absl::StrCat("Attempting to fetch value instead of handling error ",
                   status.ToString()));
  std::abort();
#endif
}

}  // namespace internal_statusor
ABSL_NAMESPACE_END
}  // namespace absl
