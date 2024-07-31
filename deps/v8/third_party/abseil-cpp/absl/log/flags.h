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
// File: log/flags.h
// -----------------------------------------------------------------------------
//

#ifndef ABSL_LOG_FLAGS_H_
#define ABSL_LOG_FLAGS_H_

// The Abseil Logging library supports the following command line flags to
// configure logging behavior at runtime:
//
// --stderrthreshold=<value>
// Log messages at or above this threshold level are copied to stderr.
//
// --minloglevel=<value>
// Messages logged at a lower level than this are discarded and don't actually
// get logged anywhere.
//
// --log_backtrace_at=<file:linenum>
// Emit a backtrace (stack trace) when logging at file:linenum.
//
// To use these commandline flags, the //absl/log:flags library must be
// explicitly linked, and absl::ParseCommandLine() must be called before the
// call to absl::InitializeLog().
//
// To configure the Log library programmatically, use the interfaces defined in
// absl/log/globals.h.

#endif  // ABSL_LOG_FLAGS_H_
