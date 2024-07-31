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

#ifndef ABSL_BASE_INTERNAL_HIDE_PTR_H_
#define ABSL_BASE_INTERNAL_HIDE_PTR_H_

#include <cstdint>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {

// Arbitrary value with high bits set. Xor'ing with it is unlikely
// to map one valid pointer to another valid pointer.
constexpr uintptr_t HideMask() {
  return (uintptr_t{0xF03A5F7BU} << (sizeof(uintptr_t) - 4) * 8) | 0xF03A5F7BU;
}

// Hide a pointer from the leak checker. For internal use only.
// Differs from absl::IgnoreLeak(ptr) in that absl::IgnoreLeak(ptr) causes ptr
// and all objects reachable from ptr to be ignored by the leak checker.
template <class T>
inline uintptr_t HidePtr(T* ptr) {
  return reinterpret_cast<uintptr_t>(ptr) ^ HideMask();
}

// Return a pointer that has been hidden from the leak checker.
// For internal use only.
template <class T>
inline T* UnhidePtr(uintptr_t hidden) {
  return reinterpret_cast<T*>(hidden ^ HideMask());
}

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_BASE_INTERNAL_HIDE_PTR_H_
