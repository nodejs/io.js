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
#include <tuple>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/tracing.h"

#if ABSL_HAVE_ATTRIBUTE_WEAK

namespace {

using ::testing::ElementsAre;

using ::absl::base_internal::ObjectKind;

enum Function { kWait, kContinue, kSignal, kObserved };

using Record = std::tuple<Function, const void*, ObjectKind>;

thread_local std::vector<Record>* tls_records = nullptr;

}  // namespace

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {

// Strong extern "C" implementation.
extern "C" {

void ABSL_INTERNAL_C_SYMBOL(AbslInternalTraceWait)(const void* object,
                                                   ObjectKind kind) {
  if (tls_records != nullptr) {
    tls_records->push_back({kWait, object, kind});
  }
}

void ABSL_INTERNAL_C_SYMBOL(AbslInternalTraceContinue)(const void* object,
                                                       ObjectKind kind) {
  if (tls_records != nullptr) {
    tls_records->push_back({kContinue, object, kind});
  }
}

void ABSL_INTERNAL_C_SYMBOL(AbslInternalTraceSignal)(const void* object,
                                                     ObjectKind kind) {
  if (tls_records != nullptr) {
    tls_records->push_back({kSignal, object, kind});
  }
}

void ABSL_INTERNAL_C_SYMBOL(AbslInternalTraceObserved)(const void* object,
                                                       ObjectKind kind) {
  if (tls_records != nullptr) {
    tls_records->push_back({kObserved, object, kind});
  }
}

}  // extern "C"

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl

namespace {

TEST(TracingInternal, InvokesStrongFunctionWithNullptr) {
  std::vector<Record> records;
  tls_records = &records;
  auto kind = absl::base_internal::ObjectKind::kUnknown;
  absl::base_internal::TraceWait(nullptr, kind);
  absl::base_internal::TraceContinue(nullptr, kind);
  absl::base_internal::TraceSignal(nullptr, kind);
  absl::base_internal::TraceObserved(nullptr, kind);
  tls_records = nullptr;

  EXPECT_THAT(records, ElementsAre(Record{kWait, nullptr, kind},
                                   Record{kContinue, nullptr, kind},
                                   Record{kSignal, nullptr, kind},
                                   Record{kObserved, nullptr, kind}));
}

TEST(TracingInternal, InvokesStrongFunctionWithObjectAddress) {
  int object = 0;
  std::vector<Record> records;
  tls_records = &records;
  auto kind = absl::base_internal::ObjectKind::kUnknown;
  absl::base_internal::TraceWait(&object, kind);
  absl::base_internal::TraceContinue(&object, kind);
  absl::base_internal::TraceSignal(&object, kind);
  absl::base_internal::TraceObserved(&object, kind);
  tls_records = nullptr;

  EXPECT_THAT(records, ElementsAre(Record{kWait, &object, kind},
                                   Record{kContinue, &object, kind},
                                   Record{kSignal, &object, kind},
                                   Record{kObserved, &object, kind}));
}

}  // namespace

#endif  // ABSL_HAVE_ATTRIBUTE_WEAK
