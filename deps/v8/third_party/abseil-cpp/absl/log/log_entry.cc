//
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

#include "absl/log/log_entry.h"

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

#ifdef ABSL_INTERNAL_NEED_REDUNDANT_CONSTEXPR_DECL
constexpr int LogEntry::kNoVerbosityLevel;
constexpr int LogEntry::kNoVerboseLevel;
#endif

// https://github.com/abseil/abseil-cpp/issues/1465
// CMake builds on Apple platforms error when libraries are empty.
// Our CMake configuration can avoid this error on header-only libraries,
// but since this library is conditionally empty, including a single
// variable is an easy workaround.
#ifdef __APPLE__
namespace log_internal {
extern const char kAvoidEmptyLogEntryLibraryWarning;
const char kAvoidEmptyLogEntryLibraryWarning = 0;
}  // namespace log_internal
#endif  // __APPLE__

ABSL_NAMESPACE_END
}  // namespace absl
