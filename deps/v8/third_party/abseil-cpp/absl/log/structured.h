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
// File: log/structured.h
// -----------------------------------------------------------------------------
//
// This header declares APIs supporting structured logging, allowing log
// statements to be more easily parsed, especially by automated processes.
//
// When structured logging is in use, data streamed into a `LOG` statement are
// encoded as `Value` fields in a `logging.proto.Event` protocol buffer message.
// The individual data are exposed programmatically to `LogSink`s and to the
// user via some log reading tools which are able to query the structured data
// more usefully than would be possible if each message was a single opaque
// string.  These helpers allow user code to add additional structure to the
// data they stream.

#ifndef ABSL_LOG_STRUCTURED_H_
#define ABSL_LOG_STRUCTURED_H_

#include <ostream>

#include "absl/base/config.h"
#include "absl/log/internal/structured.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// LogAsLiteral()
//
// Annotates its argument as a string literal so that structured logging
// captures it as a `literal` field instead of a `str` field (the default).
// This does not affect the text representation, only the structure.
//
// Streaming `LogAsLiteral(s)` into a `std::ostream` behaves just like streaming
// `s` directly.
//
// Using `LogAsLiteral()` is occasionally appropriate and useful when proxying
// data logged from another system or another language.  For example:
//
//   void Logger::LogString(absl::string_view str, absl::LogSeverity severity,
//                          const char *file, int line) {
//     LOG(LEVEL(severity)).AtLocation(file, line) << str;
//   }
//   void Logger::LogStringLiteral(absl::string_view str,
//                                 absl::LogSeverity severity, const char *file,
//                                 int line) {
//     LOG(LEVEL(severity)).AtLocation(file, line) << absl::LogAsLiteral(str);
//   }
inline log_internal::AsLiteralImpl LogAsLiteral(absl::string_view s) {
  return log_internal::AsLiteralImpl(s);
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_LOG_STRUCTURED_H_
