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

#include <unordered_set>

#include "absl/container/internal/unordered_set_constructor_test.h"
#include "absl/container/internal/unordered_set_lookup_test.h"
#include "absl/container/internal/unordered_set_members_test.h"
#include "absl/container/internal/unordered_set_modifiers_test.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {
namespace {

using SetTypes = ::testing::Types<
    std::unordered_set<int, StatefulTestingHash, StatefulTestingEqual,
                       Alloc<int>>,
    std::unordered_set<std::string, StatefulTestingHash, StatefulTestingEqual,
                       Alloc<std::string>>>;

INSTANTIATE_TYPED_TEST_SUITE_P(UnorderedSet, ConstructorTest, SetTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(UnorderedSet, LookupTest, SetTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(UnorderedSet, MembersTest, SetTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(UnorderedSet, ModifiersTest, SetTypes);

}  // namespace
}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl
