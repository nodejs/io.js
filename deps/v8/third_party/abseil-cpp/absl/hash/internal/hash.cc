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

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace hash_internal {

uint64_t MixingHashState::CombineLargeContiguousImpl32(
    uint64_t state, const unsigned char* first, size_t len) {
  while (len >= PiecewiseChunkSize()) {
    state = Mix(state,
                hash_internal::CityHash32(reinterpret_cast<const char*>(first),
                                          PiecewiseChunkSize()));
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
    state = Mix(state, Hash64(first, PiecewiseChunkSize()));
    len -= PiecewiseChunkSize();
    first += PiecewiseChunkSize();
  }
  // Handle the remainder.
  return CombineContiguousImpl(state, first, len,
                               std::integral_constant<int, 8>{});
}

ABSL_CONST_INIT const void* const MixingHashState::kSeed = &kSeed;

// The salt array used by LowLevelHash. This array is NOT the mechanism used to
// make absl::Hash non-deterministic between program invocations.  See `Seed()`
// for that mechanism.
//
// Any random values are fine. These values are just digits from the decimal
// part of pi.
// https://en.wikipedia.org/wiki/Nothing-up-my-sleeve_number
constexpr uint64_t kHashSalt[5] = {
    uint64_t{0x243F6A8885A308D3}, uint64_t{0x13198A2E03707344},
    uint64_t{0xA4093822299F31D0}, uint64_t{0x082EFA98EC4E6C89},
    uint64_t{0x452821E638D01377},
};

uint64_t MixingHashState::LowLevelHashImpl(const unsigned char* data,
                                           size_t len) {
  return LowLevelHashLenGt16(data, len, Seed(), kHashSalt);
}

}  // namespace hash_internal
ABSL_NAMESPACE_END
}  // namespace absl
