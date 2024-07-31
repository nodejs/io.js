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
// File: log/initialize.h
// -----------------------------------------------------------------------------
//
// This header declares the Abseil Log initialization routine InitializeLog().

#ifndef ABSL_LOG_INITIALIZE_H_
#define ABSL_LOG_INITIALIZE_H_

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// InitializeLog()
//
// Initializes the Abseil logging library.
//
// Before this function is called, all log messages are directed only to stderr.
// After initialization is finished, log messages are directed to all registered
// `LogSink`s.
//
// It is an error to call this function twice.
//
// There is no corresponding function to shut down the logging library.
void InitializeLog();

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_LOG_INITIALIZE_H_
