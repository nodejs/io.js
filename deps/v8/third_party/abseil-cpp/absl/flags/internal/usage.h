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

#ifndef ABSL_FLAGS_INTERNAL_USAGE_H_
#define ABSL_FLAGS_INTERNAL_USAGE_H_

#include <iosfwd>
#include <ostream>
#include <string>

#include "absl/base/config.h"
#include "absl/flags/commandlineflag.h"
#include "absl/strings/string_view.h"

// --------------------------------------------------------------------
// Usage reporting interfaces

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace flags_internal {

// The format to report the help messages in.
enum class HelpFormat {
  kHumanReadable,
};

// The kind of usage help requested.
enum class HelpMode {
  kNone,
  kImportant,
  kShort,
  kFull,
  kPackage,
  kMatch,
  kVersion,
  kOnlyCheckArgs
};

// Streams the help message describing `flag` to `out`.
// The default value for `flag` is included in the output.
void FlagHelp(std::ostream& out, const CommandLineFlag& flag,
              HelpFormat format = HelpFormat::kHumanReadable);

// Produces the help messages for all flags matching the filter. A flag matches
// the filter if it is defined in a file with a filename which includes
// filter string as a substring. You can use '/' and '.' to restrict the
// matching to a specific file names. For example:
//   FlagsHelp(out, "/path/to/file.");
// restricts help to only flags which resides in files named like:
//  .../path/to/file.<ext>
// for any extension 'ext'. If the filter is empty this function produces help
// messages for all flags.
void FlagsHelp(std::ostream& out, absl::string_view filter,
               HelpFormat format, absl::string_view program_usage_message);

// --------------------------------------------------------------------

// If any of the 'usage' related command line flags (listed on the bottom of
// this file) has been set this routine produces corresponding help message in
// the specified output stream and returns HelpMode that was handled. Otherwise
// it returns HelpMode::kNone.
HelpMode HandleUsageFlags(std::ostream& out,
                          absl::string_view program_usage_message);

// --------------------------------------------------------------------
// Encapsulates the logic of exiting the binary depending on handled help mode.

void MaybeExit(HelpMode mode);

// --------------------------------------------------------------------
// Globals representing usage reporting flags

// Returns substring to filter help output (--help=substr argument)
std::string GetFlagsHelpMatchSubstr();
// Returns the requested help mode.
HelpMode GetFlagsHelpMode();
// Returns the requested help format.
HelpFormat GetFlagsHelpFormat();

// These are corresponding setters to the attributes above.
void SetFlagsHelpMatchSubstr(absl::string_view);
void SetFlagsHelpMode(HelpMode);
void SetFlagsHelpFormat(HelpFormat);

// Deduces usage flags from the input argument in a form --name=value or
// --name. argument is already split into name and value before we call this
// function.
bool DeduceUsageFlags(absl::string_view name, absl::string_view value);

}  // namespace flags_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_FLAGS_INTERNAL_USAGE_H_
