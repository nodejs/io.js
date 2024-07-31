// Copyright 2017 The Abseil Authors.
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

#include "absl/base/log_severity.h"

#include <ostream>

#include "absl/base/attributes.h"
#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

std::ostream& operator<<(std::ostream& os, absl::LogSeverity s) {
  if (s == absl::NormalizeLogSeverity(s)) return os << absl::LogSeverityName(s);
  return os << "absl::LogSeverity(" << static_cast<int>(s) << ")";
}

std::ostream& operator<<(std::ostream& os, absl::LogSeverityAtLeast s) {
  switch (s) {
    case absl::LogSeverityAtLeast::kInfo:
    case absl::LogSeverityAtLeast::kWarning:
    case absl::LogSeverityAtLeast::kError:
    case absl::LogSeverityAtLeast::kFatal:
      return os << ">=" << static_cast<absl::LogSeverity>(s);
    case absl::LogSeverityAtLeast::kInfinity:
      return os << "INFINITY";
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, absl::LogSeverityAtMost s) {
  switch (s) {
    case absl::LogSeverityAtMost::kInfo:
    case absl::LogSeverityAtMost::kWarning:
    case absl::LogSeverityAtMost::kError:
    case absl::LogSeverityAtMost::kFatal:
      return os << "<=" << static_cast<absl::LogSeverity>(s);
    case absl::LogSeverityAtMost::kNegativeInfinity:
      return os << "NEGATIVE_INFINITY";
  }
  return os;
}
ABSL_NAMESPACE_END
}  // namespace absl
