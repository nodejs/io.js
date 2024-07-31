// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   https://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.

#ifndef ABSL_TIME_INTERNAL_CCTZ_TIME_ZONE_FIXED_H_
#define ABSL_TIME_INTERNAL_CCTZ_TIME_ZONE_FIXED_H_

#include <string>

#include "absl/base/config.h"
#include "absl/time/internal/cctz/include/cctz/time_zone.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace time_internal {
namespace cctz {

// Helper functions for dealing with the names and abbreviations
// of time zones that are a fixed offset (seconds east) from UTC.
// FixedOffsetFromName() extracts the offset from a valid fixed-offset
// name, while FixedOffsetToName() and FixedOffsetToAbbr() generate
// the canonical zone name and abbreviation respectively for the given
// offset.
//
// A fixed-offset name looks like "Fixed/UTC<+-><hours>:<mins>:<secs>".
// Its abbreviation is of the form "UTC(<+->H?H(MM(SS)?)?)?" where the
// optional pieces are omitted when their values are zero.  (Note that
// the sign is the opposite of that used in a POSIX TZ specification.)
//
// Note: FixedOffsetFromName() fails on syntax errors or when the parsed
// offset exceeds 24 hours.  FixedOffsetToName() and FixedOffsetToAbbr()
// both produce "UTC" when the argument offset exceeds 24 hours.
bool FixedOffsetFromName(const std::string& name, seconds* offset);
std::string FixedOffsetToName(const seconds& offset);
std::string FixedOffsetToAbbr(const seconds& offset);

}  // namespace cctz
}  // namespace time_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_TIME_INTERNAL_CCTZ_TIME_ZONE_FIXED_H_
