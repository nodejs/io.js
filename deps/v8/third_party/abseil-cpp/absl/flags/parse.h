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
//
// -----------------------------------------------------------------------------
// File: parse.h
// -----------------------------------------------------------------------------
//
// This file defines the main parsing function for Abseil flags:
// `absl::ParseCommandLine()`.

#ifndef ABSL_FLAGS_PARSE_H_
#define ABSL_FLAGS_PARSE_H_

#include <string>
#include <vector>

#include "absl/base/config.h"
#include "absl/flags/internal/parse.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// This type represent information about an unrecognized flag in the command
// line.
struct UnrecognizedFlag {
  enum Source { kFromArgv, kFromFlagfile };

  explicit UnrecognizedFlag(Source s, absl::string_view f)
      : source(s), flag_name(f) {}
  // This field indicates where we found this flag: on the original command line
  // or read in some flag file.
  Source source;
  // Name of the flag we did not recognize in --flag_name=value or --flag_name.
  std::string flag_name;
};

inline bool operator==(const UnrecognizedFlag& lhs,
                       const UnrecognizedFlag& rhs) {
  return lhs.source == rhs.source && lhs.flag_name == rhs.flag_name;
}

namespace flags_internal {

HelpMode ParseAbseilFlagsOnlyImpl(
    int argc, char* argv[], std::vector<char*>& positional_args,
    std::vector<UnrecognizedFlag>& unrecognized_flags,
    UsageFlagsAction usage_flag_action);

}  // namespace flags_internal

// ParseAbseilFlagsOnly()
//
// Parses a list of command-line arguments, passed in the `argc` and `argv[]`
// parameters, into a set of Abseil Flag values, returning any unparsed
// arguments in `positional_args` and `unrecognized_flags` output parameters.
//
// This function classifies all the arguments (including content of the
// flagfiles, if any) into one of the following groups:
//
//   * arguments specified as "--flag=value" or "--flag value" that match
//     registered or built-in Abseil Flags. These are "Abseil Flag arguments."
//   * arguments specified as "--flag" that are unrecognized as Abseil Flags
//   * arguments that are not specified as "--flag" are positional arguments
//   * arguments that follow the flag-terminating delimiter (`--`) are also
//     treated as positional arguments regardless of their syntax.
//
// All of the deduced Abseil Flag arguments are then parsed into their
// corresponding flag values. If any syntax errors are found in these arguments,
// the binary exits with code 1.
//
// This function also handles Abseil Flags built-in usage flags (e.g. --help)
// if any were present on the command line.
//
// All the remaining positional arguments including original program name
// (argv[0]) are are returned in the `positional_args` output parameter.
//
// All unrecognized flags that are not otherwise ignored are returned in the
// `unrecognized_flags` output parameter. Note that the special `undefok`
// flag allows you to specify flags which can be safely ignored; `undefok`
// specifies these flags as a comma-separated list. Any unrecognized flags
// that appear within `undefok` will therefore be ignored and not included in
// the `unrecognized_flag` output parameter.
//
void ParseAbseilFlagsOnly(int argc, char* argv[],
                          std::vector<char*>& positional_args,
                          std::vector<UnrecognizedFlag>& unrecognized_flags);

// ReportUnrecognizedFlags()
//
// Reports an error to `stderr` for all non-ignored unrecognized flags in
// the provided `unrecognized_flags` list.
void ReportUnrecognizedFlags(
    const std::vector<UnrecognizedFlag>& unrecognized_flags);

// ParseCommandLine()
//
// First parses Abseil Flags only from the command line according to the
// description in `ParseAbseilFlagsOnly`. In addition this function handles
// unrecognized and usage flags.
//
// If any unrecognized flags are located they are reported using
// `ReportUnrecognizedFlags`.
//
// If any errors detected during command line parsing, this routine reports a
// usage message and aborts the program.
//
// If any built-in usage flags were specified on the command line (e.g.
// `--help`), this function reports help messages and then gracefully exits the
// program.
//
// This function returns all the remaining positional arguments collected by
// `ParseAbseilFlagsOnly`.
std::vector<char*> ParseCommandLine(int argc, char* argv[]);

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_FLAGS_PARSE_H_
