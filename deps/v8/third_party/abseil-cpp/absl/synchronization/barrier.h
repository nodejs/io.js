// Copyright 2017 The Abseil Authors.
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
// barrier.h
// -----------------------------------------------------------------------------

#ifndef ABSL_SYNCHRONIZATION_BARRIER_H_
#define ABSL_SYNCHRONIZATION_BARRIER_H_

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// Barrier
//
// This class creates a barrier which blocks threads until a prespecified
// threshold of threads (`num_threads`) utilizes the barrier. A thread utilizes
// the `Barrier` by calling `Block()` on the barrier, which will block that
// thread; no call to `Block()` will return until `num_threads` threads have
// called it.
//
// Exactly one call to `Block()` will return `true`, which is then responsible
// for destroying the barrier; because stack allocation will cause the barrier
// to be deleted when it is out of scope, barriers should not be stack
// allocated.
//
// Example:
//
//   // Main thread creates a `Barrier`:
//   barrier = new Barrier(num_threads);
//
//   // Each participating thread could then call:
//   if (barrier->Block()) delete barrier;  // Exactly one call to `Block()`
//                                          // returns `true`; that call
//                                          // deletes the barrier.
class Barrier {
 public:
  // `num_threads` is the number of threads that will participate in the barrier
  explicit Barrier(int num_threads)
      : num_to_block_(num_threads), num_to_exit_(num_threads) {}

  Barrier(const Barrier&) = delete;
  Barrier& operator=(const Barrier&) = delete;

  // Barrier::Block()
  //
  // Blocks the current thread, and returns only when the `num_threads`
  // threshold of threads utilizing this barrier has been reached. `Block()`
  // returns `true` for precisely one caller, which may then destroy the
  // barrier.
  //
  // Memory ordering: For any threads X and Y, any action taken by X
  // before X calls `Block()` will be visible to Y after Y returns from
  // `Block()`.
  bool Block();

 private:
  Mutex lock_;
  int num_to_block_ ABSL_GUARDED_BY(lock_);
  int num_to_exit_ ABSL_GUARDED_BY(lock_);
};

ABSL_NAMESPACE_END
}  // namespace absl
#endif  // ABSL_SYNCHRONIZATION_BARRIER_H_
