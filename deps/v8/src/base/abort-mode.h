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

// kSoft:
//  - DCHECKs are turned into No-ops and as such V8 is allowed to continue
//    execution.
//  - CHECKs, FATAL, etc. are turned into regular exits, which allows fuzzers
//    to ignore them, this is such that we can try to find useful crashes.
// kHard:
//  - see definition of --hard-abort flag. DCHECKs / CHECKs are using
//    IMMEDIATE_CRASH() to signal abnormal program termination.
// kDefault:
//  - DHCECKs / CHECKs are using std abort() to signal abnormal program
//    termination.
enum class AbortMode { kSoft, kHard, kDefault };

V8_BASE_EXPORT extern AbortMode g_abort_mode;

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_ABORT_MODE_H_
