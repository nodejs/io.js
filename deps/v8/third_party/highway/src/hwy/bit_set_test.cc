// Copyright 2024 Google LLC
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "hwy/bit_set.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <algorithm>  // std::find
#include <map>
#include <utility>  // std::make_pair
#include <vector>

#include "hwy/base.h"
#include "hwy/tests/hwy_gtest.h"
#include "hwy/tests/test_util-inl.h"
#include "hwy/tests/test_util.h"

namespace hwy {
namespace {
using HWY_NAMESPACE::AdjustedReps;

// Template arg for kMin avoids compiler behavior mismatch for lambda capture.
template <class Set, size_t kMax, size_t kMin = 0>
void TestSet() {
  Set set;
  // Defaults to empty.
  HWY_ASSERT(!set.Any());
  HWY_ASSERT(set.Count() == 0);
  set.Foreach(
      [](size_t i) { HWY_ABORT("Set should be empty but got %zu\n", i); });
  HWY_ASSERT(!set.Get(0));
  HWY_ASSERT(!set.Get(kMax));

  // After setting, we can retrieve it.
  set.Set(kMax);
  HWY_ASSERT(set.Get(kMax));
  HWY_ASSERT(set.Any());
  HWY_ASSERT(set.First() == kMax);
  HWY_ASSERT(set.Count() == 1);
  set.Foreach([](size_t i) { HWY_ASSERT(i == kMax); });

  // SetNonzeroBitsFrom64 does not clear old bits.
  set.SetNonzeroBitsFrom64(1ull << kMin);
  HWY_ASSERT(set.Any());
  HWY_ASSERT(set.First() == kMin);
  HWY_ASSERT(set.Get(kMin));
  HWY_ASSERT(set.Get(kMax));
  HWY_ASSERT(set.Count() == 2);
  set.Foreach([](size_t i) { HWY_ASSERT(i == kMin || i == kMax); });

  // After clearing, it is empty again.
  set.Clear(kMin);
  set.Clear(kMax);
  HWY_ASSERT(!set.Any());
  HWY_ASSERT(set.Count() == 0);
  set.Foreach(
      [](size_t i) { HWY_ABORT("Set should be empty but got %zu\n", i); });
  HWY_ASSERT(!set.Get(0));
  HWY_ASSERT(!set.Get(kMax));
}

TEST(BitSetTest, TestSet64) { TestSet<BitSet64, 63>(); }
TEST(BitSetTest, TestSet4096) { TestSet<BitSet4096<>, 4095>(); }

// Supports membership and random choice, for testing BitSet4096.
class SlowSet {
 public:
  // Inserting multiple times is a no-op.
  void Set(size_t i) {
    const auto ib = idx_for_i_.insert(std::make_pair(i, vec_.size()));
    if (ib.second) {  // inserted
      vec_.push_back(i);
      HWY_ASSERT(idx_for_i_.size() == vec_.size());
    } else {
      // Already have `i` and it can be found at the stored index.
      HWY_ASSERT(ib.first->first == i);
      const size_t idx = ib.first->second;
      HWY_ASSERT(vec_[idx] == i);
    }
    HWY_ASSERT(Get(i));
  }

  bool Get(size_t i) const {
    const auto it = idx_for_i_.find(i);
    if (it == idx_for_i_.end()) {
      HWY_ASSERT(std::find(vec_.begin(), vec_.end(), i) == vec_.end());
      return false;
    }
    HWY_ASSERT(vec_[it->second] == i);
    return true;
  }

  void Clear(size_t i) {
    if (!Get(i)) return;
    const size_t idx = idx_for_i_[i];
    idx_for_i_.erase(i);
    // Move last into gap, unless it was equal to `i`.
    const size_t last = vec_.back();
    vec_.pop_back();
    if (last == i) {
      HWY_ASSERT(idx == vec_.size());  // was the last item
    } else {
      HWY_ASSERT(vec_[idx] == i);
      vec_[idx] = last;
      idx_for_i_[last] = idx;
      HWY_ASSERT(Get(last));  // can still find `last`
    }
    HWY_ASSERT(!Get(i));
  }

  size_t Count() const {
    HWY_ASSERT(idx_for_i_.size() == vec_.size());
    return vec_.size();
  }

  // Must not call if Count() == 0.
  size_t RandomChoice(RandomState& rng) const {
    HWY_ASSERT(Count() != 0);
    const size_t idx = static_cast<size_t>(hwy::Random32(&rng)) % vec_.size();
    return vec_[idx];
  }

  template <class Set>
  void CheckSame(const Set& set) {
    HWY_ASSERT(set.Any() == (set.Count() != 0));
    HWY_ASSERT(Count() == set.Count());
    // Everything set has, we also have.
    set.Foreach([this](size_t i) { HWY_ASSERT(Get(i)); });
    // Everything we have, set also has.
    std::for_each(vec_.begin(), vec_.end(),
                  [&set](size_t i) { HWY_ASSERT(set.Get(i)); });
    // First matches first in the map
    if (set.Any()) {
      HWY_ASSERT(set.First() == idx_for_i_.begin()->first);
    }
  }

 private:
  std::vector<size_t> vec_;
  std::map<size_t, size_t> idx_for_i_;
};

void TestSetRandom(uint64_t grow_prob) {
  const uint32_t mod = 4096;
  RandomState rng;

  // Multiple independent random tests:
  for (size_t rep = 0; rep < AdjustedReps(100); ++rep) {
    BitSet4096<> set;
    SlowSet slow_set;
    // Mutate sets via random walk and ensure they are the same afterwards.
    for (size_t iter = 0; iter < 200; ++iter) {
      const uint64_t bits = (Random64(&rng) >> 10) & 0x3FF;
      if (bits > 980 && slow_set.Count() != 0) {
        // Small chance of reinsertion: already present, unchanged after.
        const size_t i = slow_set.RandomChoice(rng);
        const size_t count = set.Count();
        HWY_ASSERT(set.Get(i));
        slow_set.Set(i);
        set.Set(i);
        HWY_ASSERT(set.Get(i));
        HWY_ASSERT(count == set.Count());
      } else if (bits < grow_prob) {
        // Set random value; no harm if already set.
        const size_t i = static_cast<size_t>(Random32(&rng) % mod);
        slow_set.Set(i);
        set.Set(i);
        HWY_ASSERT(set.Get(i));
      } else if (slow_set.Count() != 0) {
        // Remove existing item.
        const size_t i = slow_set.RandomChoice(rng);
        const size_t count = set.Count();
        HWY_ASSERT(set.Get(i));
        slow_set.Clear(i);
        set.Clear(i);
        HWY_ASSERT(!set.Get(i));
        HWY_ASSERT(count == set.Count() + 1);
      }
    }
    slow_set.CheckSame(set);
  }
}

// Lower probability of growth so that the set is often nearly empty.
TEST(BitSetTest, TestSetRandomShrink) { TestSetRandom(400); }
TEST(BitSetTest, TestSetRandomGrow) { TestSetRandom(600); }

}  // namespace
}  // namespace hwy

HWY_TEST_MAIN();
