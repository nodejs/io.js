// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_WRITE_BARRIER_H_
#define V8_HEAP_CPPGC_WRITE_BARRIER_H_

#include "include/cppgc/internal/write-barrier.h"
#include "src/base/lazy-instance.h"
#include "src/base/platform/mutex.h"

namespace cppgc {
namespace internal {

class WriteBarrier::FlagUpdater final {
 public:
  static void Enter() { write_barrier_enabled_.Enter(); }
  static void Exit() { write_barrier_enabled_.Exit(); }

 private:
  FlagUpdater() = delete;
};

#if defined(CPPGC_YOUNG_GENERATION)
class V8_EXPORT_PRIVATE YoungGenerationEnabler final {
 public:
  static void Enable();
  static void Disable();

  static bool IsEnabled();

 private:
  template <typename T>
  friend class v8::base::LeakyObject;

  static YoungGenerationEnabler& Instance();

  YoungGenerationEnabler() = default;

  size_t is_enabled_;
  v8::base::SpinningMutex mutex_;
};
#endif  // defined(CPPGC_YOUNG_GENERATION)

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_WRITE_BARRIER_H_
