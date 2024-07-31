//
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

#ifndef ABSL_FLAGS_INTERNAL_PARSE_H_
#define ABSL_FLAGS_INTERNAL_PARSE_H_

#include <iostream>
#include <ostream>
#include <string>
#include <vector>

#include "absl/base/config.h"
#include "absl/flags/declare.h"
#include "absl/flags/internal/usage.h"
#include "absl/strings/string_view.h"

ABSL_DECLARE_FLAG(std::vector<std::string>, flagfile);
ABSL_DECLARE_FLAG(std::vector<std::string>, fromenv);
ABSL_DECLARE_FLAG(std::vector<std::string>, tryfromenv);
ABSL_DECLARE_FLAG(std::vector<std::string>, undefok);

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace flags_internal {

enum class UsageFlagsAction { kHandleUsage, kIgnoreUsage };
enum class OnUndefinedFlag {
  kIgnoreUndefined,
  kReportUndefined,
  kAbortIfUndefined
};

// This is not a public interface. This interface exists to expose the ability
// to change help output stream in case of parsing errors. This is used by
// internal unit tests to validate expected outputs.
// When this was written, `EXPECT_EXIT` only supported matchers on stderr,
// but not on stdout.
std::vector<char*> ParseCommandLineImpl(
    int argc, char* argv[], UsageFlagsAction usage_flag_action,
    OnUndefinedFlag undef_flag_action,
    std::ostream& error_help_output = std::cout);

// --------------------------------------------------------------------
// Inspect original command line

// Returns true if flag with specified name was either present on the original
// command line or specified in flag file present on the original command line.
bool WasPresentOnCommandLine(absl::string_view flag_name);

// Return existing flags similar to the parameter, in order to help in case of
// misspellings.
std::vector<std::string> GetMisspellingHints(absl::string_view flag);

}  // namespace flags_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_FLAGS_INTERNAL_PARSE_H_
