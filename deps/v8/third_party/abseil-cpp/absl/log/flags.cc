//
// Copyright 2022 The Abseil Authors.
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

#include "absl/log/internal/flags.h"

#include <stddef.h>

#include <algorithm>
#include <cstdlib>
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/log_severity.h"
#include "absl/flags/flag.h"
#include "absl/flags/marshalling.h"
#include "absl/log/globals.h"
#include "absl/log/internal/config.h"
#include "absl/log/internal/vlog_config.h"
#include "absl/strings/numbers.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {
namespace {

void SyncLoggingFlags() {
  absl::SetFlag(&FLAGS_minloglevel, static_cast<int>(absl::MinLogLevel()));
  absl::SetFlag(&FLAGS_log_prefix, absl::ShouldPrependLogPrefix());
}

bool RegisterSyncLoggingFlags() {
  log_internal::SetLoggingGlobalsListener(&SyncLoggingFlags);
  return true;
}

ABSL_ATTRIBUTE_UNUSED const bool unused = RegisterSyncLoggingFlags();

template <typename T>
T GetFromEnv(const char* varname, T dflt) {
  const char* val = ::getenv(varname);
  if (val != nullptr) {
    std::string err;
    ABSL_INTERNAL_CHECK(absl::ParseFlag(val, &dflt, &err), err.c_str());
  }
  return dflt;
}

constexpr absl::LogSeverityAtLeast StderrThresholdDefault() {
  return absl::LogSeverityAtLeast::kError;
}

}  // namespace
}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl

ABSL_FLAG(int, stderrthreshold,
          static_cast<int>(absl::log_internal::StderrThresholdDefault()),
          "Log messages at or above this threshold level are copied to stderr.")
    .OnUpdate([] {
      absl::log_internal::RawSetStderrThreshold(
          static_cast<absl::LogSeverityAtLeast>(
              absl::GetFlag(FLAGS_stderrthreshold)));
    });

ABSL_FLAG(int, minloglevel, static_cast<int>(absl::LogSeverityAtLeast::kInfo),
          "Messages logged at a lower level than this don't actually "
          "get logged anywhere")
    .OnUpdate([] {
      absl::log_internal::RawSetMinLogLevel(
          static_cast<absl::LogSeverityAtLeast>(
              absl::GetFlag(FLAGS_minloglevel)));
    });

ABSL_FLAG(std::string, log_backtrace_at, "",
          "Emit a backtrace when logging at file:linenum.")
    .OnUpdate([] {
      const std::string log_backtrace_at =
          absl::GetFlag(FLAGS_log_backtrace_at);
      if (log_backtrace_at.empty()) {
        absl::ClearLogBacktraceLocation();
        return;
      }

      const size_t last_colon = log_backtrace_at.rfind(':');
      if (last_colon == log_backtrace_at.npos) {
        absl::ClearLogBacktraceLocation();
        return;
      }

      const absl::string_view file =
          absl::string_view(log_backtrace_at).substr(0, last_colon);
      int line;
      if (!absl::SimpleAtoi(
              absl::string_view(log_backtrace_at).substr(last_colon + 1),
              &line)) {
        absl::ClearLogBacktraceLocation();
        return;
      }
      absl::SetLogBacktraceLocation(file, line);
    });

ABSL_FLAG(bool, log_prefix, true,
          "Prepend the log prefix to the start of each log line")
    .OnUpdate([] {
      absl::log_internal::RawEnableLogPrefix(absl::GetFlag(FLAGS_log_prefix));
    });

ABSL_FLAG(int, v, 0,
          "Show all VLOG(m) messages for m <= this. Overridable by --vmodule.")
    .OnUpdate([] {
      absl::log_internal::UpdateGlobalVLogLevel(absl::GetFlag(FLAGS_v));
    });

ABSL_FLAG(
    std::string, vmodule, "",
    "per-module log verbosity level."
    " Argument is a comma-separated list of <module name>=<log level>."
    " <module name> is a glob pattern, matched against the filename base"
    " (that is, name ignoring .cc/.h./-inl.h)."
    " A pattern without slashes matches just the file name portion, otherwise"
    " the whole file path below the workspace root"
    " (still without .cc/.h./-inl.h) is matched."
    " ? and * in the glob pattern match any single or sequence of characters"
    " respectively including slashes."
    " <log level> overrides any value given by --v.")
    .OnUpdate([] {
      absl::log_internal::UpdateVModule(absl::GetFlag(FLAGS_vmodule));
    });
