// Copyright 2021 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef ABSL_STRINGS_INTERNAL_CORDZ_UPDATE_SCOPE_H_
#define ABSL_STRINGS_INTERNAL_CORDZ_UPDATE_SCOPE_H_

#include "absl/base/config.h"
#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/strings/internal/cord_internal.h"
#include "absl/strings/internal/cordz_info.h"
#include "absl/strings/internal/cordz_update_tracker.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

// CordzUpdateScope scopes an update to the provided CordzInfo.
// The class invokes `info->Lock(method)` and `info->Unlock()` to guard
// cordrep updates. This class does nothing if `info` is null.
// See also the 'Lock`, `Unlock` and `SetCordRep` methods in `CordzInfo`.
class ABSL_SCOPED_LOCKABLE CordzUpdateScope {
 public:
  CordzUpdateScope(CordzInfo* info, CordzUpdateTracker::MethodIdentifier method)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(info)
      : info_(info) {
    if (ABSL_PREDICT_FALSE(info_)) {
      info->Lock(method);
    }
  }

  // CordzUpdateScope can not be copied or assigned to.
  CordzUpdateScope(CordzUpdateScope&& rhs) = delete;
  CordzUpdateScope(const CordzUpdateScope&) = delete;
  CordzUpdateScope& operator=(CordzUpdateScope&& rhs) = delete;
  CordzUpdateScope& operator=(const CordzUpdateScope&) = delete;

  ~CordzUpdateScope() ABSL_UNLOCK_FUNCTION() {
    if (ABSL_PREDICT_FALSE(info_)) {
      info_->Unlock();
    }
  }

  void SetCordRep(CordRep* rep) const {
    if (ABSL_PREDICT_FALSE(info_)) {
      info_->SetCordRep(rep);
    }
  }

  CordzInfo* info() const { return info_; }

 private:
  CordzInfo* info_;
};

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_CORDZ_UPDATE_SCOPE_H_
