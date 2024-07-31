//
//  Copyright 2019 The Abseil Authors.
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

#ifndef ABSL_FLAGS_USAGE_H_
#define ABSL_FLAGS_USAGE_H_

#include "absl/base/config.h"
#include "absl/strings/string_view.h"

// --------------------------------------------------------------------
// Usage reporting interfaces

namespace absl {
ABSL_NAMESPACE_BEGIN

// Sets the "usage" message to be used by help reporting routines.
// For example:
//  absl::SetProgramUsageMessage(
//      absl::StrCat("This program does nothing.  Sample usage:\n", argv[0],
//                   " <uselessarg1> <uselessarg2>"));
// Do not include commandline flags in the usage: we do that for you!
// Note: Calling SetProgramUsageMessage twice will trigger a call to std::exit.
void SetProgramUsageMessage(absl::string_view new_usage_message);

// Returns the usage message set by SetProgramUsageMessage().
absl::string_view ProgramUsageMessage();

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_FLAGS_USAGE_H_
