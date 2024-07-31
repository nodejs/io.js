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

#include "absl/log/initialize.h"

#include "absl/base/config.h"
#include "absl/log/internal/globals.h"
#include "absl/time/time.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

namespace {
void InitializeLogImpl(absl::TimeZone time_zone) {
  // This comes first since it is used by RAW_LOG.
  absl::log_internal::SetTimeZone(time_zone);

  // Note that initialization is complete, so logs can now be sent to their
  // proper destinations rather than stderr.
  log_internal::SetInitialized();
}
}  // namespace

void InitializeLog() { InitializeLogImpl(absl::LocalTimeZone()); }

ABSL_NAMESPACE_END
}  // namespace absl
