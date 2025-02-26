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

#include "absl/hash/internal/hash.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/hash/internal/low_level_hash.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace hash_internal {

uint64_t MixingHashState::CombineLargeContiguousImpl32(
    uint64_t state, const unsigned char* first, size_t len) {
  while (len >= PiecewiseChunkSize()) {
    state = Mix(
        state ^ hash_internal::CityHash32(reinterpret_cast<const char*>(first),
                                          PiecewiseChunkSize()),
        kMul);
    len -= PiecewiseChunkSize();
    first += PiecewiseChunkSize();
  }
  // Handle the remainder.
  return CombineContiguousImpl(state, first, len,
                               std::integral_constant<int, 4>{});
}

uint64_t MixingHashState::CombineLargeContiguousImpl64(
    uint64_t state, const unsigned char* first, size_t len) {
  while (len >= PiecewiseChunkSize()) {
    state = Mix(state ^ Hash64(first, PiecewiseChunkSize()), kMul);
    len -= PiecewiseChunkSize();
    first += PiecewiseChunkSize();
  }
  // Handle the remainder.
  return CombineContiguousImpl(state, first, len,
                               std::integral_constant<int, 8>{});
}

ABSL_CONST_INIT const void* const MixingHashState::kSeed = &kSeed;

#ifdef ABSL_INTERNAL_NEED_REDUNDANT_CONSTEXPR_DECL
constexpr uint64_t MixingHashState::kStaticRandomData[];
#endif

uint64_t MixingHashState::LowLevelHashImpl(const unsigned char* data,
                                           size_t len) {
  return LowLevelHashLenGt16(data, len, Seed(), &kStaticRandomData[0]);
}

}  // namespace hash_internal
ABSL_NAMESPACE_END
}  // namespace absl
