//
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

#include "absl/flags/commandlineflag.h"

#include <string>

#include "absl/base/config.h"
#include "absl/flags/internal/commandlineflag.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

absl::string_view CommandLineFlag::TypeName() const { return ""; }
bool CommandLineFlag::IsRetired() const { return false; }
bool CommandLineFlag::ParseFrom(absl::string_view value, std::string* error) {
  return ParseFrom(value, flags_internal::SET_FLAGS_VALUE,
                   flags_internal::kProgrammaticChange, *error);
}

ABSL_NAMESPACE_END
}  // namespace absl
