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

#ifndef ABSL_BASE_INTERNAL_ERRNO_SAVER_H_
#define ABSL_BASE_INTERNAL_ERRNO_SAVER_H_

#include <cerrno>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {

// `ErrnoSaver` captures the value of `errno` upon construction and restores it
// upon deletion.  It is used in low-level code and must be super fast.  Do not
// add instrumentation, even in debug modes.
class ErrnoSaver {
 public:
  ErrnoSaver() : saved_errno_(errno) {}
  ~ErrnoSaver() { errno = saved_errno_; }
  int operator()() const { return saved_errno_; }

 private:
  const int saved_errno_;
};

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_BASE_INTERNAL_ERRNO_SAVER_H_
