// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_MUTEX_GUARD_IF_OFF_THREAD_H_
#define V8_EXECUTION_MUTEX_GUARD_IF_OFF_THREAD_H_

#include "src/base/platform/mutex.h"

namespace v8 {
namespace internal {

template <typename IsolateT>
class MutexGuardIfOffThread;

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_MUTEX_GUARD_IF_OFF_THREAD_H_
