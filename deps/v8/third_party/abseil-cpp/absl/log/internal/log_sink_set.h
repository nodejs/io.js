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
//
// -----------------------------------------------------------------------------
// File: log/internal/log_sink_set.h
// -----------------------------------------------------------------------------

#ifndef ABSL_LOG_INTERNAL_LOG_SINK_SET_H_
#define ABSL_LOG_INTERNAL_LOG_SINK_SET_H_

#include "absl/base/config.h"
#include "absl/log/log_entry.h"
#include "absl/log/log_sink.h"
#include "absl/types/span.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {

// Returns true if a globally-registered `LogSink`'s `Send()` is currently
// being invoked on this thread.
bool ThreadIsLoggingToLogSink();

// This function may log to two sets of sinks:
//
// * If `extra_sinks_only` is true, it will dispatch only to `extra_sinks`.
//   `LogMessage::ToSinkAlso` and `LogMessage::ToSinkOnly` are used to attach
//    extra sinks to the entry.
// * Otherwise it will also log to the global sinks set. This set is managed
//   by `absl::AddLogSink` and `absl::RemoveLogSink`.
void LogToSinks(const absl::LogEntry& entry,
                absl::Span<absl::LogSink*> extra_sinks, bool extra_sinks_only);

// Implementation for operations with log sink set.
void AddLogSink(absl::LogSink* sink);
void RemoveLogSink(absl::LogSink* sink);
void FlushLogSinks();

}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_LOG_INTERNAL_LOG_SINK_SET_H_
