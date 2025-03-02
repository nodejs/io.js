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

#include "absl/flags/internal/private_handle_accessor.h"

#include <memory>
#include <string>

#include "absl/base/config.h"
#include "absl/flags/commandlineflag.h"
#include "absl/flags/internal/commandlineflag.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace flags_internal {

FlagFastTypeId PrivateHandleAccessor::TypeId(const CommandLineFlag& flag) {
  return flag.TypeId();
}

std::unique_ptr<FlagStateInterface> PrivateHandleAccessor::SaveState(
    CommandLineFlag& flag) {
  return flag.SaveState();
}

bool PrivateHandleAccessor::IsSpecifiedOnCommandLine(
    const CommandLineFlag& flag) {
  return flag.IsSpecifiedOnCommandLine();
}

bool PrivateHandleAccessor::ValidateInputValue(const CommandLineFlag& flag,
                                               absl::string_view value) {
  return flag.ValidateInputValue(value);
}

void PrivateHandleAccessor::CheckDefaultValueParsingRoundtrip(
    const CommandLineFlag& flag) {
  flag.CheckDefaultValueParsingRoundtrip();
}

bool PrivateHandleAccessor::ParseFrom(CommandLineFlag& flag,
                                      absl::string_view value,
                                      flags_internal::FlagSettingMode set_mode,
                                      flags_internal::ValueSource source,
                                      std::string& error) {
  return flag.ParseFrom(value, set_mode, source, error);
}

absl::string_view PrivateHandleAccessor::TypeName(const CommandLineFlag& flag) {
  return flag.TypeName();
}

}  // namespace flags_internal
ABSL_NAMESPACE_END
}  // namespace absl

