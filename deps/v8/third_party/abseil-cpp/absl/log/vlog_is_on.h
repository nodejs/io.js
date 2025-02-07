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
// File: log/vlog_is_on.h
// -----------------------------------------------------------------------------
//
// This header defines the `VLOG_IS_ON()` macro that controls the
// variable-verbosity conditional logging.
//
// It's used by `VLOG` in log.h, or it can also be used directly like this:
//
//   if (VLOG_IS_ON(2)) {
//     foo_server.RecomputeStatisticsExpensive();
//     LOG(INFO) << foo_server.LastStatisticsAsString();
//   }
//
// Each source file has an effective verbosity level that's a non-negative
// integer computed from the `--vmodule` and `--v` flags.
// `VLOG_IS_ON(n)` is true, and `VLOG(n)` logs, if that effective verbosity
// level is greater than or equal to `n`.
//
// `--vmodule` takes a comma-delimited list of key=value pairs.  Each key is a
// pattern matched against filenames, and the values give the effective severity
// level applied to matching files.  '?' and '*' characters in patterns are
// interpreted as single-character and zero-or-more-character wildcards.
// Patterns including a slash character are matched against full pathnames,
// while those without are matched against basenames only.  One suffix (i.e. the
// last . and everything after it) is stripped from each filename prior to
// matching, as is the special suffix "-inl".
//
// Example: --vmodule=module_a=1,module_b=2
//
// Files are matched against globs in `--vmodule` in order, and the first match
// determines the verbosity level.
//
// Files which do not match any pattern in `--vmodule` use the value of `--v` as
// their effective verbosity level.  The default is 0.
//
// SetVLogLevel helper function is provided to do limited dynamic control over
// V-logging by appending to `--vmodule`. Because these go at the beginning of
// the list, they take priority over any globs previously added.
//
// Resetting --vmodule will override all previous modifications to `--vmodule`,
// including via SetVLogLevel.

#ifndef ABSL_LOG_VLOG_IS_ON_H_
#define ABSL_LOG_VLOG_IS_ON_H_

#include "absl/log/absl_vlog_is_on.h"  // IWYU pragma: export

// IWYU pragma: private, include "absl/log/log.h"

// Each VLOG_IS_ON call site gets its own VLogSite that registers with the
// global linked list of sites to asynchronously update its verbosity level on
// changes to --v or --vmodule. The verbosity can also be set by manually
// calling SetVLogLevel.
//
// VLOG_IS_ON is not async signal safe, but it is guaranteed not to allocate
// new memory.
#define VLOG_IS_ON(verbose_level) ABSL_VLOG_IS_ON(verbose_level)

#endif  // ABSL_LOG_VLOG_IS_ON_H_
