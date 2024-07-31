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
// File: log/log_flags.h
// -----------------------------------------------------------------------------
//
// This header declares set of flags which can be used to configure Abseil
// Logging library behaviour at runtime.

#ifndef ABSL_LOG_INTERNAL_FLAGS_H_
#define ABSL_LOG_INTERNAL_FLAGS_H_

#include <string>

#include "absl/flags/declare.h"

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// These flags should not be used in C++ code to access logging library
// configuration knobs. Use interfaces defined in absl/log/globals.h
// instead. It is still ok to use these flags on a command line.
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

// Log messages at this severity or above are sent to stderr in *addition* to
// `LogSink`s.  Defaults to `ERROR`.  See log_severity.h for numeric values of
// severity levels.
ABSL_DECLARE_FLAG(int, stderrthreshold);

// Log messages at this severity or above are logged; others are discarded.
// Defaults to `INFO`, i.e. log all severities.  See log_severity.h for numeric
// values of severity levels.
ABSL_DECLARE_FLAG(int, minloglevel);

// If specified in the form file:linenum, any messages logged from a matching
// location will also include a backtrace.
ABSL_DECLARE_FLAG(std::string, log_backtrace_at);

// If true, the log prefix (severity, date, time, PID, etc.) is prepended to
// each message logged. Defaults to true.
ABSL_DECLARE_FLAG(bool, log_prefix);

// Global log verbosity level. Default is 0.
ABSL_DECLARE_FLAG(int, v);

// Per-module log verbosity level. By default is empty and is unused.
ABSL_DECLARE_FLAG(std::string, vmodule);

#endif  // ABSL_LOG_INTERNAL_FLAGS_H_
