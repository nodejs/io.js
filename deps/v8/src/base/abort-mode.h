// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the way aborts are handled in OS::Abort and the way
// DCHECKs are working.

#ifndef V8_BASE_ABORT_MODE_H_
#define V8_BASE_ABORT_MODE_H_

#include "src/base/base-export.h"

namespace v8 {
namespace base {

enum class AbortMode {
  // Used for example for fuzzing when controlled crashes are harmless, such
  // as for example for the sandbox. With this:
  //  - DCHECKs are turned into No-ops and as such V8 is allowed to continue
  //    execution. This way, the fuzzer can progress past them.
  //  - CHECKs, FATAL, etc. are turned into regular exits, which allows fuzzers
  //    to ignore them, as they are harmless in this context.
  //  - The exit code will either be zero (signaling success) or non-zero
  //    (signaling failure). The former is for example used in tests in which a
  //    controlled crash counts as success (for example in sandbox regression
  //    tests), the latter is typically used for fuzzing where samples that exit
  //    in this way should be discarded and not mutated further.
  kExitWithSuccessAndIgnoreDcheckFailures,
  kExitWithFailureAndIgnoreDcheckFailures,

  // DCHECKs, CHECKs, etc. use IMMEDIATE_CRASH() to signal abnormal program
  // termination. See the --hard-abort flag for more details.
  kImmediateCrash,

  // CHECKs, DCHECKs, etc. use abort() to signal abnormal program termination.
  kDefault
};

V8_BASE_EXPORT extern AbortMode g_abort_mode;

V8_INLINE bool ControlledCrashesAreHarmless() {
  return g_abort_mode == AbortMode::kExitWithSuccessAndIgnoreDcheckFailures ||
         g_abort_mode == AbortMode::kExitWithFailureAndIgnoreDcheckFailures;
}

V8_INLINE bool DcheckFailuresAreIgnored() {
  return g_abort_mode == AbortMode::kExitWithSuccessAndIgnoreDcheckFailures ||
         g_abort_mode == AbortMode::kExitWithFailureAndIgnoreDcheckFailures;
}

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_ABORT_MODE_H_
