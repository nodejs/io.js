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

#include "time_zone_if.h"

#include "absl/base/config.h"
#include "time_zone_info.h"
#include "time_zone_libc.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace time_internal {
namespace cctz {

std::unique_ptr<TimeZoneIf> TimeZoneIf::UTC() { return TimeZoneInfo::UTC(); }

std::unique_ptr<TimeZoneIf> TimeZoneIf::Make(const std::string& name) {
  // Support "libc:localtime" and "libc:*" to access the legacy
  // localtime and UTC support respectively from the C library.
  // NOTE: The "libc:*" zones are internal, test-only interfaces, and
  // are subject to change/removal without notice. Do not use them.
  if (name.compare(0, 5, "libc:") == 0) {
    return TimeZoneLibC::Make(name.substr(5));
  }

  // Otherwise use the "zoneinfo" implementation.
  return TimeZoneInfo::Make(name);
}

// Defined out-of-line to avoid emitting a weak vtable in all TUs.
TimeZoneIf::~TimeZoneIf() {}

}  // namespace cctz
}  // namespace time_internal
ABSL_NAMESPACE_END
}  // namespace absl
