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

#ifndef ABSL_TIME_INTERNAL_CCTZ_ZONE_INFO_SOURCE_H_
#define ABSL_TIME_INTERNAL_CCTZ_ZONE_INFO_SOURCE_H_

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace time_internal {
namespace cctz {

// A stdio-like interface for providing zoneinfo data for a particular zone.
class ZoneInfoSource {
 public:
  virtual ~ZoneInfoSource();

  virtual std::size_t Read(void* ptr, std::size_t size) = 0;  // like fread()
  virtual int Skip(std::size_t offset) = 0;                   // like fseek()

  // Until the zoneinfo data supports versioning information, we provide
  // a way for a ZoneInfoSource to indicate it out-of-band.  The default
  // implementation returns an empty string.
  virtual std::string Version() const;
};

}  // namespace cctz
}  // namespace time_internal
ABSL_NAMESPACE_END
}  // namespace absl

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace time_internal {
namespace cctz_extension {

// A function-pointer type for a factory that returns a ZoneInfoSource
// given the name of a time zone and a fallback factory.  Returns null
// when the data for the named zone cannot be found.
using ZoneInfoSourceFactory =
    std::unique_ptr<absl::time_internal::cctz::ZoneInfoSource> (*)(
        const std::string&,
        const std::function<std::unique_ptr<
            absl::time_internal::cctz::ZoneInfoSource>(const std::string&)>&);

// The user can control the mapping of zone names to zoneinfo data by
// providing a definition for cctz_extension::zone_info_source_factory.
// For example, given functions my_factory() and my_other_factory() that
// can return a ZoneInfoSource for a named zone, we could inject them into
// cctz::load_time_zone() with:
//
//   namespace cctz_extension {
//   namespace {
//   std::unique_ptr<cctz::ZoneInfoSource> CustomFactory(
//       const std::string& name,
//       const std::function<std::unique_ptr<cctz::ZoneInfoSource>(
//           const std::string& name)>& fallback_factory) {
//     if (auto zip = my_factory(name)) return zip;
//     if (auto zip = fallback_factory(name)) return zip;
//     if (auto zip = my_other_factory(name)) return zip;
//     return nullptr;
//   }
//   }  // namespace
//   ZoneInfoSourceFactory zone_info_source_factory = CustomFactory;
//   }  // namespace cctz_extension
//
// This might be used, say, to use zoneinfo data embedded in the program,
// or read from a (possibly compressed) file archive, or both.
//
// cctz_extension::zone_info_source_factory() will be called:
//   (1) from the same thread as the cctz::load_time_zone() call,
//   (2) only once for any zone name, and
//   (3) serially (i.e., no concurrent execution).
//
// The fallback factory obtains zoneinfo data by reading files in ${TZDIR},
// and it is used automatically when no zone_info_source_factory definition
// is linked into the program.
extern ZoneInfoSourceFactory zone_info_source_factory;

}  // namespace cctz_extension
}  // namespace time_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_TIME_INTERNAL_CCTZ_ZONE_INFO_SOURCE_H_
