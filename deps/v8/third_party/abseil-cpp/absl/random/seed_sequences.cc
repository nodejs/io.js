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

#include "absl/random/seed_sequences.h"

#include "absl/random/internal/pool_urbg.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

SeedSeq MakeSeedSeq() {
  SeedSeq::result_type seed_material[8];
  random_internal::RandenPool<uint32_t>::Fill(absl::MakeSpan(seed_material));
  return SeedSeq(std::begin(seed_material), std::end(seed_material));
}

ABSL_NAMESPACE_END
}  // namespace absl
