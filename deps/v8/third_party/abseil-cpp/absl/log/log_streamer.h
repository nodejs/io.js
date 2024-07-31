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
// File: log/log_streamer.h
// -----------------------------------------------------------------------------
//
// This header declares the class `LogStreamer` and convenience functions to
// construct LogStreamer objects with different associated log severity levels.

#ifndef ABSL_LOG_LOG_STREAMER_H_
#define ABSL_LOG_LOG_STREAMER_H_

#include <ios>
#include <memory>
#include <ostream>
#include <string>
#include <utility>

#include "absl/base/config.h"
#include "absl/base/log_severity.h"
#include "absl/log/absl_log.h"
#include "absl/strings/internal/ostringstream.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/utility/utility.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// LogStreamer
//
// Although you can stream into `LOG(INFO)`, you can't pass it into a function
// that takes a `std::ostream` parameter. `LogStreamer::stream()` provides a
// `std::ostream` that buffers everything that's streamed in.  The buffer's
// contents are logged as if by `LOG` when the `LogStreamer` is destroyed.
// If nothing is streamed in, an empty message is logged.  If the specified
// severity is `absl::LogSeverity::kFatal`, the program will be terminated when
// the `LogStreamer` is destroyed regardless of whether any data were streamed
// in.
//
// Factory functions corresponding to the `absl::LogSeverity` enumerators
// are provided for convenience; if the desired severity is variable, invoke the
// constructor directly.
//
// LogStreamer is movable, but not copyable.
//
// Examples:
//
//   ShaveYakAndWriteToStream(
//       yak, absl::LogInfoStreamer(__FILE__, __LINE__).stream());
//
//   {
//     // This logs a single line containing data streamed by all three function
//     // calls.
//     absl::LogStreamer streamer(absl::LogSeverity::kInfo, __FILE__, __LINE__);
//     ShaveYakAndWriteToStream(yak1, streamer.stream());
//     streamer.stream() << " ";
//     ShaveYakAndWriteToStream(yak2, streamer.stream());
//     streamer.stream() << " ";
//     ShaveYakAndWriteToStreamPointer(yak3, &streamer.stream());
//   }
class LogStreamer final {
 public:
  // LogStreamer::LogStreamer()
  //
  // Creates a LogStreamer with a given `severity` that will log a message
  // attributed to the given `file` and `line`.
  explicit LogStreamer(absl::LogSeverity severity, absl::string_view file,
                       int line)
      : severity_(severity),
        line_(line),
        file_(file),
        stream_(absl::in_place, &buf_) {
    // To match `LOG`'s defaults:
    stream_->setf(std::ios_base::showbase | std::ios_base::boolalpha);
  }

  // A moved-from `absl::LogStreamer` does not `LOG` when destroyed,
  // and a program that streams into one has undefined behavior.
  LogStreamer(LogStreamer&& that) noexcept
      : severity_(that.severity_),
        line_(that.line_),
        file_(std::move(that.file_)),
        buf_(std::move(that.buf_)),
        stream_(std::move(that.stream_)) {
    if (stream_.has_value()) stream_->str(&buf_);
    that.stream_.reset();
  }
  LogStreamer& operator=(LogStreamer&& that) {
    ABSL_LOG_IF(LEVEL(severity_), stream_).AtLocation(file_, line_) << buf_;
    severity_ = that.severity_;
    file_ = std::move(that.file_);
    line_ = that.line_;
    buf_ = std::move(that.buf_);
    stream_ = std::move(that.stream_);
    if (stream_.has_value()) stream_->str(&buf_);
    that.stream_.reset();
    return *this;
  }

  // LogStreamer::~LogStreamer()
  //
  // Logs this LogStreamer's buffered content as if by LOG.
  ~LogStreamer() {
    ABSL_LOG_IF(LEVEL(severity_), stream_.has_value()).AtLocation(file_, line_)
        << buf_;
  }

  // LogStreamer::stream()
  //
  // Returns the `std::ostream` to use to write into this LogStreamer' internal
  // buffer.
  std::ostream& stream() { return *stream_; }

 private:
  absl::LogSeverity severity_;
  int line_;
  std::string file_;
  std::string buf_;
  // A disengaged `stream_` indicates a moved-from `LogStreamer` that should not
  // `LOG` upon destruction.
  absl::optional<absl::strings_internal::OStringStream> stream_;
};

// LogInfoStreamer()
//
// Returns a LogStreamer that writes at level LogSeverity::kInfo.
inline LogStreamer LogInfoStreamer(absl::string_view file, int line) {
  return absl::LogStreamer(absl::LogSeverity::kInfo, file, line);
}

// LogWarningStreamer()
//
// Returns a LogStreamer that writes at level LogSeverity::kWarning.
inline LogStreamer LogWarningStreamer(absl::string_view file, int line) {
  return absl::LogStreamer(absl::LogSeverity::kWarning, file, line);
}

// LogErrorStreamer()
//
// Returns a LogStreamer that writes at level LogSeverity::kError.
inline LogStreamer LogErrorStreamer(absl::string_view file, int line) {
  return absl::LogStreamer(absl::LogSeverity::kError, file, line);
}

// LogFatalStreamer()
//
// Returns a LogStreamer that writes at level LogSeverity::kFatal.
//
// The program will be terminated when this `LogStreamer` is destroyed,
// regardless of whether any data were streamed in.
inline LogStreamer LogFatalStreamer(absl::string_view file, int line) {
  return absl::LogStreamer(absl::LogSeverity::kFatal, file, line);
}

// LogDebugFatalStreamer()
//
// Returns a LogStreamer that writes at level LogSeverity::kLogDebugFatal.
//
// In debug mode, the program will be terminated when this `LogStreamer` is
// destroyed, regardless of whether any data were streamed in.
inline LogStreamer LogDebugFatalStreamer(absl::string_view file, int line) {
  return absl::LogStreamer(absl::kLogDebugFatal, file, line);
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_LOG_LOG_STREAMER_H_
