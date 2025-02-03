// Copyright 2024 The Abseil Authors
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

#include "gtest/gtest.h"
#include "absl/base/internal/tracing.h"

namespace {

TEST(TracingInternal, HasDefaultImplementation) {
  auto kind = absl::base_internal::ObjectKind::kUnknown;
  absl::base_internal::TraceWait(nullptr, kind);
  absl::base_internal::TraceContinue(nullptr, kind);
  absl::base_internal::TraceSignal(nullptr, kind);
  absl::base_internal::TraceObserved(nullptr, kind);

  int object = 0;
  absl::base_internal::TraceWait(&object, kind);
  absl::base_internal::TraceContinue(&object, kind);
  absl::base_internal::TraceSignal(&object, kind);
  absl::base_internal::TraceObserved(&object, kind);
}

}  // namespace
