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

#include <memory>
#include <unordered_map>

#include "absl/container/internal/unordered_map_constructor_test.h"
#include "absl/container/internal/unordered_map_lookup_test.h"
#include "absl/container/internal/unordered_map_members_test.h"
#include "absl/container/internal/unordered_map_modifiers_test.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {
namespace {

using MapTypes = ::testing::Types<
    std::unordered_map<int, int, StatefulTestingHash, StatefulTestingEqual,
                       Alloc<std::pair<const int, int>>>,
    std::unordered_map<std::string, std::string, StatefulTestingHash,
                       StatefulTestingEqual,
                       Alloc<std::pair<const std::string, std::string>>>>;

INSTANTIATE_TYPED_TEST_SUITE_P(UnorderedMap, ConstructorTest, MapTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(UnorderedMap, LookupTest, MapTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(UnorderedMap, MembersTest, MapTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(UnorderedMap, ModifiersTest, MapTypes);

using UniquePtrMapTypes = ::testing::Types<std::unordered_map<
    int, std::unique_ptr<int>, StatefulTestingHash, StatefulTestingEqual,
    Alloc<std::pair<const int, std::unique_ptr<int>>>>>;

INSTANTIATE_TYPED_TEST_SUITE_P(UnorderedMap, UniquePtrModifiersTest,
                               UniquePtrMapTypes);

}  // namespace
}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl
