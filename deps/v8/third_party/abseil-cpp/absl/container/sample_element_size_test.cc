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

#include <cstddef>
#include <unordered_set>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/container/node_hash_set.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {
namespace {

#if defined(ABSL_INTERNAL_HASHTABLEZ_SAMPLE)
// Create some tables of type `Table`, then look at all the new
// `HashtablezInfo`s to make sure that the `inline_element_size ==
// expected_element_size`.  The `inline_element_size` is the amount of memory
// allocated for each slot of a hash table, that is `sizeof(slot_type)`.  Add
// the new `HashtablezInfo`s to `preexisting_info`.  Store all the new tables
// into `tables`.
template <class Table>
void TestInlineElementSize(
    HashtablezSampler& sampler,
    // clang-tidy gives a false positive on this declaration.  This unordered
    // set cannot be flat_hash_set, however, since that would introduce a mutex
    // deadlock.
    std::unordered_set<const HashtablezInfo*>& preexisting_info,  // NOLINT
    std::vector<Table>& tables,
    const std::vector<typename Table::value_type>& values,
    size_t expected_element_size) {
  for (int i = 0; i < 10; ++i) {
    // We create a new table and must store it somewhere so that when we store
    // a pointer to the resulting `HashtablezInfo` into `preexisting_info`
    // that we aren't storing a dangling pointer.
    tables.emplace_back();
    // We must insert elements to get a hashtablez to instantiate.
    tables.back().insert(values.begin(), values.end());
  }
  size_t new_count = 0;
  sampler.Iterate([&](const HashtablezInfo& info) {
    if (preexisting_info.insert(&info).second) {
      EXPECT_EQ(info.inline_element_size, expected_element_size);
      ++new_count;
    }
  });
  // Make sure we actually did get a new hashtablez.
  EXPECT_GT(new_count, 0);
}

struct bigstruct {
  char a[1000];
  friend bool operator==(const bigstruct& x, const bigstruct& y) {
    return memcmp(x.a, y.a, sizeof(x.a)) == 0;
  }
  template <typename H>
  friend H AbslHashValue(H h, const bigstruct& c) {
    return H::combine_contiguous(std::move(h), c.a, sizeof(c.a));
  }
};
#endif

TEST(FlatHashMap, SampleElementSize) {
#if defined(ABSL_INTERNAL_HASHTABLEZ_SAMPLE)
  // Enable sampling even if the prod default is off.
  SetHashtablezEnabled(true);
  SetHashtablezSampleParameter(1);

  auto& sampler = GlobalHashtablezSampler();
  std::vector<flat_hash_map<int, bigstruct>> flat_map_tables;
  std::vector<flat_hash_set<bigstruct>> flat_set_tables;
  std::vector<node_hash_map<int, bigstruct>> node_map_tables;
  std::vector<node_hash_set<bigstruct>> node_set_tables;
  std::vector<bigstruct> set_values = {bigstruct{{0}}, bigstruct{{1}}};
  std::vector<std::pair<const int, bigstruct>> map_values = {{0, bigstruct{}},
                                                             {1, bigstruct{}}};

  // It takes thousands of new tables after changing the sampling parameters
  // before you actually get some instrumentation.  And if you must actually
  // put something into those tables.
  for (int i = 0; i < 10000; ++i) {
    flat_map_tables.emplace_back();
    flat_map_tables.back()[i] = bigstruct{};
  }

  // clang-tidy gives a false positive on this declaration.  This unordered set
  // cannot be a flat_hash_set, however, since that would introduce a mutex
  // deadlock.
  std::unordered_set<const HashtablezInfo*> preexisting_info;  // NOLINT
  sampler.Iterate(
      [&](const HashtablezInfo& info) { preexisting_info.insert(&info); });
  TestInlineElementSize(sampler, preexisting_info, flat_map_tables, map_values,
                        sizeof(int) + sizeof(bigstruct));
  TestInlineElementSize(sampler, preexisting_info, node_map_tables, map_values,
                        sizeof(void*));
  TestInlineElementSize(sampler, preexisting_info, flat_set_tables, set_values,
                        sizeof(bigstruct));
  TestInlineElementSize(sampler, preexisting_info, node_set_tables, set_values,
                        sizeof(void*));
#endif
}

}  // namespace
}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl
