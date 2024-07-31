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
// File: log/internal/nullstream.h
// -----------------------------------------------------------------------------
//
// Classes `NullStream`, `NullStreamMaybeFatal ` and `NullStreamFatal`
// implement a subset of the `LogMessage` API and are used instead when logging
// of messages has been disabled.

#ifndef ABSL_LOG_INTERNAL_NULLSTREAM_H_
#define ABSL_LOG_INTERNAL_NULLSTREAM_H_

#ifdef _WIN32
#include <cstdlib>
#else
#include <unistd.h>
#endif
#include <ios>
#include <ostream>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/log_severity.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {

// A `NullStream` implements the API of `LogMessage` (a few methods and
// `operator<<`) but does nothing.  All methods are defined inline so the
// compiler can eliminate the whole instance and discard anything that's
// streamed in.
class NullStream {
 public:
  NullStream& AtLocation(absl::string_view, int) { return *this; }
  template <typename SourceLocationType>
  NullStream& AtLocation(SourceLocationType) {
    return *this;
  }
  NullStream& NoPrefix() { return *this; }
  NullStream& WithVerbosity(int) { return *this; }
  template <typename TimeType>
  NullStream& WithTimestamp(TimeType) {
    return *this;
  }
  template <typename Tid>
  NullStream& WithThreadID(Tid) {
    return *this;
  }
  template <typename LogEntryType>
  NullStream& WithMetadataFrom(const LogEntryType&) {
    return *this;
  }
  NullStream& WithPerror() { return *this; }
  template <typename LogSinkType>
  NullStream& ToSinkAlso(LogSinkType*) {
    return *this;
  }
  template <typename LogSinkType>
  NullStream& ToSinkOnly(LogSinkType*) {
    return *this;
  }
  template <typename LogSinkType>
  NullStream& OutputToSink(LogSinkType*, bool) {
    return *this;
  }
  NullStream& InternalStream() { return *this; }
};
template <typename T>
inline NullStream& operator<<(NullStream& str, const T&) {
  return str;
}
inline NullStream& operator<<(NullStream& str,
                              std::ostream& (*)(std::ostream& os)) {
  return str;
}
inline NullStream& operator<<(NullStream& str,
                              std::ios_base& (*)(std::ios_base& os)) {
  return str;
}

// `NullStreamMaybeFatal` implements the process termination semantics of
// `LogMessage`, which is used for `DFATAL` severity and expression-defined
// severity e.g. `LOG(LEVEL(HowBadIsIt()))`.  Like `LogMessage`, it terminates
// the process when destroyed if the passed-in severity equals `FATAL`.
class NullStreamMaybeFatal final : public NullStream {
 public:
  explicit NullStreamMaybeFatal(absl::LogSeverity severity)
      : fatal_(severity == absl::LogSeverity::kFatal) {}
  ~NullStreamMaybeFatal() {
    if (fatal_) {
      _exit(1);
    }
  }

 private:
  bool fatal_;
};

// `NullStreamFatal` implements the process termination semantics of
// `LogMessageFatal`, which means it always terminates the process.  `DFATAL`
// and expression-defined severity use `NullStreamMaybeFatal` above.
class NullStreamFatal final : public NullStream {
 public:
  NullStreamFatal() = default;
  // ABSL_ATTRIBUTE_NORETURN doesn't seem to work on destructors with msvc, so
  // disable msvc's warning about the d'tor never returning.
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(push)
#pragma warning(disable : 4722)
#endif
  ABSL_ATTRIBUTE_NORETURN ~NullStreamFatal() { _exit(1); }
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(pop)
#endif
};

}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_LOG_INTERNAL_GLOBALS_H_
