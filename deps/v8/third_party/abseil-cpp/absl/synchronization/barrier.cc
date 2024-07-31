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

#include "absl/synchronization/barrier.h"

#include "absl/base/internal/raw_logging.h"
#include "absl/synchronization/mutex.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// Return whether int *arg is zero.
static bool IsZero(void *arg) {
  return 0 == *reinterpret_cast<int *>(arg);
}

bool Barrier::Block() {
  MutexLock l(&this->lock_);

  this->num_to_block_--;
  if (this->num_to_block_ < 0) {
    ABSL_RAW_LOG(
        FATAL,
        "Block() called too many times.  num_to_block_=%d out of total=%d",
        this->num_to_block_, this->num_to_exit_);
  }

  this->lock_.Await(Condition(IsZero, &this->num_to_block_));

  // Determine which thread can safely delete this Barrier object
  this->num_to_exit_--;
  ABSL_RAW_CHECK(this->num_to_exit_ >= 0, "barrier underflow");

  // If num_to_exit_ == 0 then all other threads in the barrier have
  // exited the Wait() and have released the Mutex so this thread is
  // free to delete the barrier.
  return this->num_to_exit_ == 0;
}

ABSL_NAMESPACE_END
}  // namespace absl
