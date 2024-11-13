//
// Copyright 2018 The Abseil Authors.
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

// Helper function for measuring stack consumption of signal handlers.

#ifndef ABSL_DEBUGGING_INTERNAL_STACK_CONSUMPTION_H_
#define ABSL_DEBUGGING_INTERNAL_STACK_CONSUMPTION_H_

#include "absl/base/config.h"

// The code in this module is not portable.
// Use this feature test macro to detect its availability.
#ifdef ABSL_INTERNAL_HAVE_DEBUGGING_STACK_CONSUMPTION
#error ABSL_INTERNAL_HAVE_DEBUGGING_STACK_CONSUMPTION cannot be set directly
#elif !defined(__APPLE__) && !defined(_WIN32) && !defined(__Fuchsia__) && \
    (defined(__i386__) || defined(__x86_64__) || defined(__ppc__) || \
     defined(__aarch64__) || defined(__riscv))
#define ABSL_INTERNAL_HAVE_DEBUGGING_STACK_CONSUMPTION 1

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {

// Returns the stack consumption in bytes for the code exercised by
// signal_handler.  To measure stack consumption, signal_handler is registered
// as a signal handler, so the code that it exercises must be async-signal
// safe.  The argument of signal_handler is an implementation detail of signal
// handlers and should ignored by the code for signal_handler.  Use global
// variables to pass information between your test code and signal_handler.
int GetSignalHandlerStackConsumption(void (*signal_handler)(int));

}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_INTERNAL_HAVE_DEBUGGING_STACK_CONSUMPTION

#endif  // ABSL_DEBUGGING_INTERNAL_STACK_CONSUMPTION_H_
