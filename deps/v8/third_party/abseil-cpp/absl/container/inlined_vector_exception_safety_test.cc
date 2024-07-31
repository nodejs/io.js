// Copyright 2019 The Abseil Authors.
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

#include "absl/container/inlined_vector.h"

#include "absl/base/config.h"

#if defined(ABSL_HAVE_EXCEPTIONS)

#include <array>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <utility>

#include "gtest/gtest.h"
#include "absl/base/internal/exception_safety_testing.h"

namespace {

constexpr size_t kInlinedCapacity = 4;
constexpr size_t kLargeSize = kInlinedCapacity * 2;
constexpr size_t kSmallSize = kInlinedCapacity / 2;

using Thrower = testing::ThrowingValue<>;
using MovableThrower = testing::ThrowingValue<testing::TypeSpec::kNoThrowMove>;
using ThrowAlloc = testing::ThrowingAllocator<Thrower>;

using ThrowerVec = absl::InlinedVector<Thrower, kInlinedCapacity>;
using MovableThrowerVec = absl::InlinedVector<MovableThrower, kInlinedCapacity>;

using ThrowAllocThrowerVec =
    absl::InlinedVector<Thrower, kInlinedCapacity, ThrowAlloc>;
using ThrowAllocMovableThrowerVec =
    absl::InlinedVector<MovableThrower, kInlinedCapacity, ThrowAlloc>;

// In GCC, if an element of a `std::initializer_list` throws during construction
// the elements that were constructed before it are not destroyed. This causes
// incorrect exception safety test failures. Thus, `testing::nothrow_ctor` is
// required. See: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=66139
#define ABSL_INTERNAL_MAKE_INIT_LIST(T, N)                     \
  (N > kInlinedCapacity                                        \
       ? std::initializer_list<T>{T(0, testing::nothrow_ctor), \
                                  T(1, testing::nothrow_ctor), \
                                  T(2, testing::nothrow_ctor), \
                                  T(3, testing::nothrow_ctor), \
                                  T(4, testing::nothrow_ctor), \
                                  T(5, testing::nothrow_ctor), \
                                  T(6, testing::nothrow_ctor), \
                                  T(7, testing::nothrow_ctor)} \
                                                               \
       : std::initializer_list<T>{T(0, testing::nothrow_ctor), \
                                  T(1, testing::nothrow_ctor)})
static_assert(kLargeSize == 8, "Must update ABSL_INTERNAL_MAKE_INIT_LIST(...)");
static_assert(kSmallSize == 2, "Must update ABSL_INTERNAL_MAKE_INIT_LIST(...)");

template <typename TheVecT, size_t... TheSizes>
class TestParams {
 public:
  using VecT = TheVecT;
  constexpr static size_t GetSizeAt(size_t i) { return kSizes[1 + i]; }

 private:
  constexpr static size_t kSizes[1 + sizeof...(TheSizes)] = {1, TheSizes...};
};

using NoSizeTestParams =
    ::testing::Types<TestParams<ThrowerVec>, TestParams<MovableThrowerVec>,
                     TestParams<ThrowAllocThrowerVec>,
                     TestParams<ThrowAllocMovableThrowerVec>>;

using OneSizeTestParams =
    ::testing::Types<TestParams<ThrowerVec, kLargeSize>,
                     TestParams<ThrowerVec, kSmallSize>,
                     TestParams<MovableThrowerVec, kLargeSize>,
                     TestParams<MovableThrowerVec, kSmallSize>,
                     TestParams<ThrowAllocThrowerVec, kLargeSize>,
                     TestParams<ThrowAllocThrowerVec, kSmallSize>,
                     TestParams<ThrowAllocMovableThrowerVec, kLargeSize>,
                     TestParams<ThrowAllocMovableThrowerVec, kSmallSize>>;

using TwoSizeTestParams = ::testing::Types<
    TestParams<ThrowerVec, kLargeSize, kLargeSize>,
    TestParams<ThrowerVec, kLargeSize, kSmallSize>,
    TestParams<ThrowerVec, kSmallSize, kLargeSize>,
    TestParams<ThrowerVec, kSmallSize, kSmallSize>,
    TestParams<MovableThrowerVec, kLargeSize, kLargeSize>,
    TestParams<MovableThrowerVec, kLargeSize, kSmallSize>,
    TestParams<MovableThrowerVec, kSmallSize, kLargeSize>,
    TestParams<MovableThrowerVec, kSmallSize, kSmallSize>,
    TestParams<ThrowAllocThrowerVec, kLargeSize, kLargeSize>,
    TestParams<ThrowAllocThrowerVec, kLargeSize, kSmallSize>,
    TestParams<ThrowAllocThrowerVec, kSmallSize, kLargeSize>,
    TestParams<ThrowAllocThrowerVec, kSmallSize, kSmallSize>,
    TestParams<ThrowAllocMovableThrowerVec, kLargeSize, kLargeSize>,
    TestParams<ThrowAllocMovableThrowerVec, kLargeSize, kSmallSize>,
    TestParams<ThrowAllocMovableThrowerVec, kSmallSize, kLargeSize>,
    TestParams<ThrowAllocMovableThrowerVec, kSmallSize, kSmallSize>>;

template <typename>
struct NoSizeTest : ::testing::Test {};
TYPED_TEST_SUITE(NoSizeTest, NoSizeTestParams);

template <typename>
struct OneSizeTest : ::testing::Test {};
TYPED_TEST_SUITE(OneSizeTest, OneSizeTestParams);

template <typename>
struct TwoSizeTest : ::testing::Test {};
TYPED_TEST_SUITE(TwoSizeTest, TwoSizeTestParams);

template <typename VecT>
bool InlinedVectorInvariants(VecT* vec) {
  if (*vec != *vec) return false;
  if (vec->size() > vec->capacity()) return false;
  if (vec->size() > vec->max_size()) return false;
  if (vec->capacity() > vec->max_size()) return false;
  if (vec->data() != std::addressof(vec->at(0))) return false;
  if (vec->data() != vec->begin()) return false;
  if (*vec->data() != *vec->begin()) return false;
  if (vec->begin() > vec->end()) return false;
  if ((vec->end() - vec->begin()) != vec->size()) return false;
  if (std::distance(vec->begin(), vec->end()) != vec->size()) return false;
  return true;
}

// Function that always returns false is correct, but refactoring is required
// for clarity. It's needed to express that, as a contract, certain operations
// should not throw at all. Execution of this function means an exception was
// thrown and thus the test should fail.
// TODO(johnsoncj): Add `testing::NoThrowGuarantee` to the framework
template <typename VecT>
bool NoThrowGuarantee(VecT* /* vec */) {
  return false;
}

TYPED_TEST(NoSizeTest, DefaultConstructor) {
  using VecT = typename TypeParam::VecT;
  using allocator_type = typename VecT::allocator_type;

  testing::TestThrowingCtor<VecT>();

  testing::TestThrowingCtor<VecT>(allocator_type{});
}

TYPED_TEST(OneSizeTest, SizeConstructor) {
  using VecT = typename TypeParam::VecT;
  using allocator_type = typename VecT::allocator_type;
  constexpr static auto size = TypeParam::GetSizeAt(0);

  testing::TestThrowingCtor<VecT>(size);

  testing::TestThrowingCtor<VecT>(size, allocator_type{});
}

TYPED_TEST(OneSizeTest, SizeRefConstructor) {
  using VecT = typename TypeParam::VecT;
  using value_type = typename VecT::value_type;
  using allocator_type = typename VecT::allocator_type;
  constexpr static auto size = TypeParam::GetSizeAt(0);

  testing::TestThrowingCtor<VecT>(size, value_type{});

  testing::TestThrowingCtor<VecT>(size, value_type{}, allocator_type{});
}

TYPED_TEST(OneSizeTest, InitializerListConstructor) {
  using VecT = typename TypeParam::VecT;
  using value_type = typename VecT::value_type;
  using allocator_type = typename VecT::allocator_type;
  constexpr static auto size = TypeParam::GetSizeAt(0);

  testing::TestThrowingCtor<VecT>(
      ABSL_INTERNAL_MAKE_INIT_LIST(value_type, size));

  testing::TestThrowingCtor<VecT>(
      ABSL_INTERNAL_MAKE_INIT_LIST(value_type, size), allocator_type{});
}

TYPED_TEST(OneSizeTest, RangeConstructor) {
  using VecT = typename TypeParam::VecT;
  using value_type = typename VecT::value_type;
  using allocator_type = typename VecT::allocator_type;
  constexpr static auto size = TypeParam::GetSizeAt(0);

  std::array<value_type, size> arr{};

  testing::TestThrowingCtor<VecT>(arr.begin(), arr.end());

  testing::TestThrowingCtor<VecT>(arr.begin(), arr.end(), allocator_type{});
}

TYPED_TEST(OneSizeTest, CopyConstructor) {
  using VecT = typename TypeParam::VecT;
  using allocator_type = typename VecT::allocator_type;
  constexpr static auto size = TypeParam::GetSizeAt(0);

  VecT other_vec{size};

  testing::TestThrowingCtor<VecT>(other_vec);

  testing::TestThrowingCtor<VecT>(other_vec, allocator_type{});
}

TYPED_TEST(OneSizeTest, MoveConstructor) {
  using VecT = typename TypeParam::VecT;
  using allocator_type = typename VecT::allocator_type;
  constexpr static auto size = TypeParam::GetSizeAt(0);

  if (!absl::allocator_is_nothrow<allocator_type>::value) {
    testing::TestThrowingCtor<VecT>(VecT{size});

    testing::TestThrowingCtor<VecT>(VecT{size}, allocator_type{});
  }
}

TYPED_TEST(TwoSizeTest, Assign) {
  using VecT = typename TypeParam::VecT;
  using value_type = typename VecT::value_type;
  constexpr static auto from_size = TypeParam::GetSizeAt(0);
  constexpr static auto to_size = TypeParam::GetSizeAt(1);

  auto tester = testing::MakeExceptionSafetyTester()
                    .WithInitialValue(VecT{from_size})
                    .WithContracts(InlinedVectorInvariants<VecT>);

  EXPECT_TRUE(tester.Test([](VecT* vec) {
    *vec = ABSL_INTERNAL_MAKE_INIT_LIST(value_type, to_size);
  }));

  EXPECT_TRUE(tester.Test([](VecT* vec) {
    VecT other_vec{to_size};
    *vec = other_vec;
  }));

  EXPECT_TRUE(tester.Test([](VecT* vec) {
    VecT other_vec{to_size};
    *vec = std::move(other_vec);
  }));

  EXPECT_TRUE(tester.Test([](VecT* vec) {
    value_type val{};
    vec->assign(to_size, val);
  }));

  EXPECT_TRUE(tester.Test([](VecT* vec) {
    vec->assign(ABSL_INTERNAL_MAKE_INIT_LIST(value_type, to_size));
  }));

  EXPECT_TRUE(tester.Test([](VecT* vec) {
    std::array<value_type, to_size> arr{};
    vec->assign(arr.begin(), arr.end());
  }));
}

TYPED_TEST(TwoSizeTest, Resize) {
  using VecT = typename TypeParam::VecT;
  using value_type = typename VecT::value_type;
  constexpr static auto from_size = TypeParam::GetSizeAt(0);
  constexpr static auto to_size = TypeParam::GetSizeAt(1);

  auto tester = testing::MakeExceptionSafetyTester()
                    .WithInitialValue(VecT{from_size})
                    .WithContracts(InlinedVectorInvariants<VecT>,
                                   testing::strong_guarantee);

  EXPECT_TRUE(tester.Test([](VecT* vec) {
    vec->resize(to_size);  //
  }));

  EXPECT_TRUE(tester.Test([](VecT* vec) {
    vec->resize(to_size, value_type{});  //
  }));
}

TYPED_TEST(OneSizeTest, Insert) {
  using VecT = typename TypeParam::VecT;
  using value_type = typename VecT::value_type;
  constexpr static auto from_size = TypeParam::GetSizeAt(0);

  auto tester = testing::MakeExceptionSafetyTester()
                    .WithInitialValue(VecT{from_size})
                    .WithContracts(InlinedVectorInvariants<VecT>);

  EXPECT_TRUE(tester.Test([](VecT* vec) {
    auto it = vec->begin();
    vec->insert(it, value_type{});
  }));
  EXPECT_TRUE(tester.Test([](VecT* vec) {
    auto it = vec->begin() + (vec->size() / 2);
    vec->insert(it, value_type{});
  }));
  EXPECT_TRUE(tester.Test([](VecT* vec) {
    auto it = vec->end();
    vec->insert(it, value_type{});
  }));
}

TYPED_TEST(TwoSizeTest, Insert) {
  using VecT = typename TypeParam::VecT;
  using value_type = typename VecT::value_type;
  constexpr static auto from_size = TypeParam::GetSizeAt(0);
  constexpr static auto count = TypeParam::GetSizeAt(1);

  auto tester = testing::MakeExceptionSafetyTester()
                    .WithInitialValue(VecT{from_size})
                    .WithContracts(InlinedVectorInvariants<VecT>);

  EXPECT_TRUE(tester.Test([](VecT* vec) {
    auto it = vec->begin();
    vec->insert(it, count, value_type{});
  }));
  EXPECT_TRUE(tester.Test([](VecT* vec) {
    auto it = vec->begin() + (vec->size() / 2);
    vec->insert(it, count, value_type{});
  }));
  EXPECT_TRUE(tester.Test([](VecT* vec) {
    auto it = vec->end();
    vec->insert(it, count, value_type{});
  }));

  EXPECT_TRUE(tester.Test([](VecT* vec) {
    auto it = vec->begin();
    vec->insert(it, ABSL_INTERNAL_MAKE_INIT_LIST(value_type, count));
  }));
  EXPECT_TRUE(tester.Test([](VecT* vec) {
    auto it = vec->begin() + (vec->size() / 2);
    vec->insert(it, ABSL_INTERNAL_MAKE_INIT_LIST(value_type, count));
  }));
  EXPECT_TRUE(tester.Test([](VecT* vec) {
    auto it = vec->end();
    vec->insert(it, ABSL_INTERNAL_MAKE_INIT_LIST(value_type, count));
  }));

  EXPECT_TRUE(tester.Test([](VecT* vec) {
    auto it = vec->begin();
    std::array<value_type, count> arr{};
    vec->insert(it, arr.begin(), arr.end());
  }));
  EXPECT_TRUE(tester.Test([](VecT* vec) {
    auto it = vec->begin() + (vec->size() / 2);
    std::array<value_type, count> arr{};
    vec->insert(it, arr.begin(), arr.end());
  }));
  EXPECT_TRUE(tester.Test([](VecT* vec) {
    auto it = vec->end();
    std::array<value_type, count> arr{};
    vec->insert(it, arr.begin(), arr.end());
  }));
}

TYPED_TEST(OneSizeTest, EmplaceBack) {
  using VecT = typename TypeParam::VecT;
  constexpr static auto size = TypeParam::GetSizeAt(0);

  // For testing calls to `emplace_back(...)` that reallocate.
  VecT full_vec{size};
  full_vec.resize(full_vec.capacity());

  // For testing calls to `emplace_back(...)` that don't reallocate.
  VecT nonfull_vec{size};
  nonfull_vec.reserve(size + 1);

  auto tester = testing::MakeExceptionSafetyTester().WithContracts(
      InlinedVectorInvariants<VecT>);

  EXPECT_TRUE(tester.WithInitialValue(nonfull_vec).Test([](VecT* vec) {
    vec->emplace_back();
  }));

  EXPECT_TRUE(tester.WithInitialValue(full_vec).Test(
      [](VecT* vec) { vec->emplace_back(); }));
}

TYPED_TEST(OneSizeTest, PopBack) {
  using VecT = typename TypeParam::VecT;
  constexpr static auto size = TypeParam::GetSizeAt(0);

  auto tester = testing::MakeExceptionSafetyTester()
                    .WithInitialValue(VecT{size})
                    .WithContracts(NoThrowGuarantee<VecT>);

  EXPECT_TRUE(tester.Test([](VecT* vec) {
    vec->pop_back();  //
  }));
}

TYPED_TEST(OneSizeTest, Erase) {
  using VecT = typename TypeParam::VecT;
  constexpr static auto size = TypeParam::GetSizeAt(0);

  auto tester = testing::MakeExceptionSafetyTester()
                    .WithInitialValue(VecT{size})
                    .WithContracts(InlinedVectorInvariants<VecT>);

  EXPECT_TRUE(tester.Test([](VecT* vec) {
    auto it = vec->begin();
    vec->erase(it);
  }));
  EXPECT_TRUE(tester.Test([](VecT* vec) {
    auto it = vec->begin() + (vec->size() / 2);
    vec->erase(it);
  }));
  EXPECT_TRUE(tester.Test([](VecT* vec) {
    auto it = vec->begin() + (vec->size() - 1);
    vec->erase(it);
  }));

  EXPECT_TRUE(tester.Test([](VecT* vec) {
    auto it = vec->begin();
    vec->erase(it, it);
  }));
  EXPECT_TRUE(tester.Test([](VecT* vec) {
    auto it = vec->begin() + (vec->size() / 2);
    vec->erase(it, it);
  }));
  EXPECT_TRUE(tester.Test([](VecT* vec) {
    auto it = vec->begin() + (vec->size() - 1);
    vec->erase(it, it);
  }));

  EXPECT_TRUE(tester.Test([](VecT* vec) {
    auto it = vec->begin();
    vec->erase(it, it + 1);
  }));
  EXPECT_TRUE(tester.Test([](VecT* vec) {
    auto it = vec->begin() + (vec->size() / 2);
    vec->erase(it, it + 1);
  }));
  EXPECT_TRUE(tester.Test([](VecT* vec) {
    auto it = vec->begin() + (vec->size() - 1);
    vec->erase(it, it + 1);
  }));
}

TYPED_TEST(OneSizeTest, Clear) {
  using VecT = typename TypeParam::VecT;
  constexpr static auto size = TypeParam::GetSizeAt(0);

  auto tester = testing::MakeExceptionSafetyTester()
                    .WithInitialValue(VecT{size})
                    .WithContracts(NoThrowGuarantee<VecT>);

  EXPECT_TRUE(tester.Test([](VecT* vec) {
    vec->clear();  //
  }));
}

TYPED_TEST(TwoSizeTest, Reserve) {
  using VecT = typename TypeParam::VecT;
  constexpr static auto from_size = TypeParam::GetSizeAt(0);
  constexpr static auto to_capacity = TypeParam::GetSizeAt(1);

  auto tester = testing::MakeExceptionSafetyTester()
                    .WithInitialValue(VecT{from_size})
                    .WithContracts(InlinedVectorInvariants<VecT>);

  EXPECT_TRUE(tester.Test([](VecT* vec) { vec->reserve(to_capacity); }));
}

TYPED_TEST(OneSizeTest, ShrinkToFit) {
  using VecT = typename TypeParam::VecT;
  constexpr static auto size = TypeParam::GetSizeAt(0);

  auto tester = testing::MakeExceptionSafetyTester()
                    .WithInitialValue(VecT{size})
                    .WithContracts(InlinedVectorInvariants<VecT>);

  EXPECT_TRUE(tester.Test([](VecT* vec) {
    vec->shrink_to_fit();  //
  }));
}

TYPED_TEST(TwoSizeTest, Swap) {
  using VecT = typename TypeParam::VecT;
  constexpr static auto from_size = TypeParam::GetSizeAt(0);
  constexpr static auto to_size = TypeParam::GetSizeAt(1);

  auto tester = testing::MakeExceptionSafetyTester()
                    .WithInitialValue(VecT{from_size})
                    .WithContracts(InlinedVectorInvariants<VecT>);

  EXPECT_TRUE(tester.Test([](VecT* vec) {
    VecT other_vec{to_size};
    vec->swap(other_vec);
  }));

  EXPECT_TRUE(tester.Test([](VecT* vec) {
    using std::swap;
    VecT other_vec{to_size};
    swap(*vec, other_vec);
  }));
}

}  // namespace

#endif  // defined(ABSL_HAVE_EXCEPTIONS)
