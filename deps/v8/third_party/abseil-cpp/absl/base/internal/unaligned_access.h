//
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

#ifndef ABSL_BASE_INTERNAL_UNALIGNED_ACCESS_H_
#define ABSL_BASE_INTERNAL_UNALIGNED_ACCESS_H_

#include <string.h>

#include <cstdint>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/nullability.h"

// unaligned APIs

// Portable handling of unaligned loads, stores, and copies.

// The unaligned API is C++ only.  The declarations use C++ features
// (namespaces, inline) which are absent or incompatible in C.
#if defined(__cplusplus)
namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {

inline uint16_t UnalignedLoad16(absl::Nonnull<const void *> p) {
  uint16_t t;
  memcpy(&t, p, sizeof t);
  return t;
}

inline uint32_t UnalignedLoad32(absl::Nonnull<const void *> p) {
  uint32_t t;
  memcpy(&t, p, sizeof t);
  return t;
}

inline uint64_t UnalignedLoad64(absl::Nonnull<const void *> p) {
  uint64_t t;
  memcpy(&t, p, sizeof t);
  return t;
}

inline void UnalignedStore16(absl::Nonnull<void *> p, uint16_t v) {
  memcpy(p, &v, sizeof v);
}

inline void UnalignedStore32(absl::Nonnull<void *> p, uint32_t v) {
  memcpy(p, &v, sizeof v);
}

inline void UnalignedStore64(absl::Nonnull<void *> p, uint64_t v) {
  memcpy(p, &v, sizeof v);
}

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl

#define ABSL_INTERNAL_UNALIGNED_LOAD16(_p) \
  (absl::base_internal::UnalignedLoad16(_p))
#define ABSL_INTERNAL_UNALIGNED_LOAD32(_p) \
  (absl::base_internal::UnalignedLoad32(_p))
#define ABSL_INTERNAL_UNALIGNED_LOAD64(_p) \
  (absl::base_internal::UnalignedLoad64(_p))

#define ABSL_INTERNAL_UNALIGNED_STORE16(_p, _val) \
  (absl::base_internal::UnalignedStore16(_p, _val))
#define ABSL_INTERNAL_UNALIGNED_STORE32(_p, _val) \
  (absl::base_internal::UnalignedStore32(_p, _val))
#define ABSL_INTERNAL_UNALIGNED_STORE64(_p, _val) \
  (absl::base_internal::UnalignedStore64(_p, _val))

#endif  // defined(__cplusplus), end of unaligned API

#endif  // ABSL_BASE_INTERNAL_UNALIGNED_ACCESS_H_
