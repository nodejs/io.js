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
// -----------------------------------------------------------------------------
// File: stacktrace.h
// -----------------------------------------------------------------------------
//
// This file contains routines to extract the current stack trace and associated
// stack frames. These functions are thread-safe and async-signal-safe.
//
// Note that stack trace functionality is platform dependent and requires
// additional support from the compiler/build system in most cases. (That is,
// this functionality generally only works on platforms/builds that have been
// specifically configured to support it.)
//
// Note: stack traces in Abseil that do not utilize a symbolizer will result in
// frames consisting of function addresses rather than human-readable function
// names. (See symbolize.h for information on symbolizing these values.)

#ifndef ABSL_DEBUGGING_STACKTRACE_H_
#define ABSL_DEBUGGING_STACKTRACE_H_

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// GetStackFrames()
//
// Records program counter values for up to `max_depth` frames, skipping the
// most recent `skip_count` stack frames, stores their corresponding values
// and sizes in `results` and `sizes` buffers, and returns the number of frames
// stored. (Note that the frame generated for the `absl::GetStackFrames()`
// routine itself is also skipped.)
//
// Example:
//
//      main() { foo(); }
//      foo() { bar(); }
//      bar() {
//        void* result[10];
//        int sizes[10];
//        int depth = absl::GetStackFrames(result, sizes, 10, 1);
//      }
//
// The current stack frame would consist of three function calls: `bar()`,
// `foo()`, and then `main()`; however, since the `GetStackFrames()` call sets
// `skip_count` to `1`, it will skip the frame for `bar()`, the most recently
// invoked function call. It will therefore return 2 and fill `result` with
// program counters within the following functions:
//
//      result[0]       foo()
//      result[1]       main()
//
// (Note: in practice, a few more entries after `main()` may be added to account
// for startup processes.)
//
// Corresponding stack frame sizes will also be recorded:
//
//    sizes[0]       16
//    sizes[1]       16
//
// (Stack frame sizes of `16` above are just for illustration purposes.)
//
// Stack frame sizes of 0 or less indicate that those frame sizes couldn't
// be identified.
//
// This routine may return fewer stack frame entries than are
// available. Also note that `result` and `sizes` must both be non-null.
extern int GetStackFrames(void** result, int* sizes, int max_depth,
                          int skip_count);

// GetStackFramesWithContext()
//
// Records program counter values obtained from a signal handler. Records
// program counter values for up to `max_depth` frames, skipping the most recent
// `skip_count` stack frames, stores their corresponding values and sizes in
// `results` and `sizes` buffers, and returns the number of frames stored. (Note
// that the frame generated for the `absl::GetStackFramesWithContext()` routine
// itself is also skipped.)
//
// The `uc` parameter, if non-null, should be a pointer to a `ucontext_t` value
// passed to a signal handler registered via the `sa_sigaction` field of a
// `sigaction` struct. (See
// http://man7.org/linux/man-pages/man2/sigaction.2.html.) The `uc` value may
// help a stack unwinder to provide a better stack trace under certain
// conditions. `uc` may safely be null.
//
// The `min_dropped_frames` output parameter, if non-null, points to the
// location to note any dropped stack frames, if any, due to buffer limitations
// or other reasons. (This value will be set to `0` if no frames were dropped.)
// The number of total stack frames is guaranteed to be >= skip_count +
// max_depth + *min_dropped_frames.
extern int GetStackFramesWithContext(void** result, int* sizes, int max_depth,
                                     int skip_count, const void* uc,
                                     int* min_dropped_frames);

// GetStackTrace()
//
// Records program counter values for up to `max_depth` frames, skipping the
// most recent `skip_count` stack frames, stores their corresponding values
// in `results`, and returns the number of frames
// stored. Note that this function is similar to `absl::GetStackFrames()`
// except that it returns the stack trace only, and not stack frame sizes.
//
// Example:
//
//      main() { foo(); }
//      foo() { bar(); }
//      bar() {
//        void* result[10];
//        int depth = absl::GetStackTrace(result, 10, 1);
//      }
//
// This produces:
//
//      result[0]       foo
//      result[1]       main
//           ....       ...
//
// `result` must not be null.
extern int GetStackTrace(void** result, int max_depth, int skip_count);

// GetStackTraceWithContext()
//
// Records program counter values obtained from a signal handler. Records
// program counter values for up to `max_depth` frames, skipping the most recent
// `skip_count` stack frames, stores their corresponding values in `results`,
// and returns the number of frames stored. (Note that the frame generated for
// the `absl::GetStackFramesWithContext()` routine itself is also skipped.)
//
// The `uc` parameter, if non-null, should be a pointer to a `ucontext_t` value
// passed to a signal handler registered via the `sa_sigaction` field of a
// `sigaction` struct. (See
// http://man7.org/linux/man-pages/man2/sigaction.2.html.) The `uc` value may
// help a stack unwinder to provide a better stack trace under certain
// conditions. `uc` may safely be null.
//
// The `min_dropped_frames` output parameter, if non-null, points to the
// location to note any dropped stack frames, if any, due to buffer limitations
// or other reasons. (This value will be set to `0` if no frames were dropped.)
// The number of total stack frames is guaranteed to be >= skip_count +
// max_depth + *min_dropped_frames.
extern int GetStackTraceWithContext(void** result, int max_depth,
                                    int skip_count, const void* uc,
                                    int* min_dropped_frames);

// SetStackUnwinder()
//
// Provides a custom function for unwinding stack frames that will be used in
// place of the default stack unwinder when invoking the static
// GetStack{Frames,Trace}{,WithContext}() functions above.
//
// The arguments passed to the unwinder function will match the
// arguments passed to `absl::GetStackFramesWithContext()` except that sizes
// will be non-null iff the caller is interested in frame sizes.
//
// If unwinder is set to null, we revert to the default stack-tracing behavior.
//
// *****************************************************************************
// WARNING
// *****************************************************************************
//
// absl::SetStackUnwinder is not suitable for general purpose use.  It is
// provided for custom runtimes.
// Some things to watch out for when calling `absl::SetStackUnwinder()`:
//
// (a) The unwinder may be called from within signal handlers and
// therefore must be async-signal-safe.
//
// (b) Even after a custom stack unwinder has been unregistered, other
// threads may still be in the process of using that unwinder.
// Therefore do not clean up any state that may be needed by an old
// unwinder.
// *****************************************************************************
extern void SetStackUnwinder(int (*unwinder)(void** pcs, int* sizes,
                                             int max_depth, int skip_count,
                                             const void* uc,
                                             int* min_dropped_frames));

// DefaultStackUnwinder()
//
// Records program counter values of up to `max_depth` frames, skipping the most
// recent `skip_count` stack frames, and stores their corresponding values in
// `pcs`. (Note that the frame generated for this call itself is also skipped.)
// This function acts as a generic stack-unwinder; prefer usage of the more
// specific `GetStack{Trace,Frames}{,WithContext}()` functions above.
//
// If you have set your own stack unwinder (with the `SetStackUnwinder()`
// function above, you can still get the default stack unwinder by calling
// `DefaultStackUnwinder()`, which will ignore any previously set stack unwinder
// and use the default one instead.
//
// Because this function is generic, only `pcs` is guaranteed to be non-null
// upon return. It is legal for `sizes`, `uc`, and `min_dropped_frames` to all
// be null when called.
//
// The semantics are the same as the corresponding `GetStack*()` function in the
// case where `absl::SetStackUnwinder()` was never called. Equivalents are:
//
//                       null sizes         |        non-nullptr sizes
//             |==========================================================|
//     null uc | GetStackTrace()            | GetStackFrames()            |
// non-null uc | GetStackTraceWithContext() | GetStackFramesWithContext() |
//             |==========================================================|
extern int DefaultStackUnwinder(void** pcs, int* sizes, int max_depth,
                                int skip_count, const void* uc,
                                int* min_dropped_frames);

namespace debugging_internal {
// Returns true for platforms which are expected to have functioning stack trace
// implementations. Intended to be used for tests which want to exclude
// verification of logic known to be broken because stack traces are not
// working.
extern bool StackTraceWorksForTest();
}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_DEBUGGING_STACKTRACE_H_
