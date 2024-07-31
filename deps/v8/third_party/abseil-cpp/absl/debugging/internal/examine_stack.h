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
//

#ifndef ABSL_DEBUGGING_INTERNAL_EXAMINE_STACK_H_
#define ABSL_DEBUGGING_INTERNAL_EXAMINE_STACK_H_

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {

// Type of function used for printing in stack trace dumping, etc.
// We avoid closures to keep things simple.
typedef void OutputWriter(const char*, void*);

// RegisterDebugStackTraceHook() allows to register a single routine
// `hook` that is called each time DumpStackTrace() is called.
// `hook` may be called from a signal handler.
typedef void (*SymbolizeUrlEmitter)(void* const stack[], int depth,
                                    OutputWriter* writer, void* writer_arg);

// Registration of SymbolizeUrlEmitter for use inside of a signal handler.
// This is inherently unsafe and must be signal safe code.
void RegisterDebugStackTraceHook(SymbolizeUrlEmitter hook);
SymbolizeUrlEmitter GetDebugStackTraceHook();

// Returns the program counter from signal context, or nullptr if
// unknown. `vuc` is a ucontext_t*. We use void* to avoid the use of
// ucontext_t on non-POSIX systems.
void* GetProgramCounter(void* const vuc);

// Uses `writer` to dump the program counter, stack trace, and stack
// frame sizes.
void DumpPCAndFrameSizesAndStackTrace(void* const pc, void* const stack[],
                                      int frame_sizes[], int depth,
                                      int min_dropped_frames,
                                      bool symbolize_stacktrace,
                                      OutputWriter* writer, void* writer_arg);

// Dump current stack trace omitting the topmost `min_dropped_frames` stack
// frames.
void DumpStackTrace(int min_dropped_frames, int max_num_frames,
                    bool symbolize_stacktrace, OutputWriter* writer,
                    void* writer_arg);

}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_DEBUGGING_INTERNAL_EXAMINE_STACK_H_
