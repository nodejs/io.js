/*
 * Copyright 2017 The Abseil Authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Interface for getting the current ThreadIdentity, creating one if necessary.
// See thread_identity.h.
//
// This file is separate from thread_identity.h because creating a new
// ThreadIdentity requires slightly higher level libraries (per_thread_sem
// and low_level_alloc) than accessing an existing one.  This separation allows
// us to have a smaller //absl/base:base.

#ifndef ABSL_SYNCHRONIZATION_INTERNAL_CREATE_THREAD_IDENTITY_H_
#define ABSL_SYNCHRONIZATION_INTERNAL_CREATE_THREAD_IDENTITY_H_

#include "absl/base/internal/thread_identity.h"
#include "absl/base/port.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace synchronization_internal {

// Allocates and attaches a ThreadIdentity object for the calling thread.
// For private use only.
base_internal::ThreadIdentity* CreateThreadIdentity();

// Returns the ThreadIdentity object representing the calling thread; guaranteed
// to be unique for its lifetime.  The returned object will remain valid for the
// program's lifetime; although it may be re-assigned to a subsequent thread.
// If one does not exist for the calling thread, allocate it now.
inline base_internal::ThreadIdentity* GetOrCreateCurrentThreadIdentity() {
  base_internal::ThreadIdentity* identity =
      base_internal::CurrentThreadIdentityIfPresent();
  if (ABSL_PREDICT_FALSE(identity == nullptr)) {
    return CreateThreadIdentity();
  }
  return identity;
}

}  // namespace synchronization_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_SYNCHRONIZATION_INTERNAL_CREATE_THREAD_IDENTITY_H_
