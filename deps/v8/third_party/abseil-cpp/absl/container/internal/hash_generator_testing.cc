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

#include "absl/container/internal/hash_generator_testing.h"

#include <deque>

#include "absl/base/no_destructor.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {
namespace hash_internal {
namespace {

class RandomDeviceSeedSeq {
 public:
  using result_type = typename std::random_device::result_type;

  template <class Iterator>
  void generate(Iterator start, Iterator end) {
    while (start != end) {
      *start = gen_();
      ++start;
    }
  }

 private:
  std::random_device gen_;
};

}  // namespace

std::mt19937_64* GetSharedRng() {
  static absl::NoDestructor<std::mt19937_64> rng([] {
    RandomDeviceSeedSeq seed_seq;
    return std::mt19937_64(seed_seq);
  }());
  return rng.get();
}

std::string Generator<std::string>::operator()() const {
  // NOLINTNEXTLINE(runtime/int)
  std::uniform_int_distribution<short> chars(0x20, 0x7E);
  std::string res;
  res.resize(32);
  std::generate(res.begin(), res.end(),
                [&]() { return chars(*GetSharedRng()); });
  return res;
}

absl::string_view Generator<absl::string_view>::operator()() const {
  static absl::NoDestructor<std::deque<std::string>> arena;
  // NOLINTNEXTLINE(runtime/int)
  std::uniform_int_distribution<short> chars(0x20, 0x7E);
  arena->emplace_back();
  auto& res = arena->back();
  res.resize(32);
  std::generate(res.begin(), res.end(),
                [&]() { return chars(*GetSharedRng()); });
  return res;
}

}  // namespace hash_internal
}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl
