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

#include "absl/container/internal/layout.h"

// We need ::max_align_t because some libstdc++ versions don't provide
// std::max_align_t
#include <stddef.h>

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <ostream>
#include <string>
#include <tuple>
#include <type_traits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/log/check.h"
#include "absl/types/span.h"
#include "absl/utility/utility.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {
namespace {

using ::absl::Span;
using ::testing::ElementsAre;

size_t Distance(const void* from, const void* to) {
  CHECK_LE(from, to) << "Distance must be non-negative";
  return static_cast<const char*>(to) - static_cast<const char*>(from);
}

template <class Expected, class Actual>
Expected Type(Actual val) {
  static_assert(std::is_same<Expected, Actual>(), "");
  return val;
}

// Helper classes to test different size and alignments.
struct alignas(8) Int128 {
  uint64_t a, b;
  friend bool operator==(Int128 lhs, Int128 rhs) {
    return std::tie(lhs.a, lhs.b) == std::tie(rhs.a, rhs.b);
  }

  static std::string Name() {
    return internal_layout::adl_barrier::TypeName<Int128>();
  }
};

// int64_t is *not* 8-byte aligned on all platforms!
struct alignas(8) Int64 {
  int64_t a;
  friend bool operator==(Int64 lhs, Int64 rhs) { return lhs.a == rhs.a; }
};

// Properties of types that this test relies on.
static_assert(sizeof(int8_t) == 1, "");
static_assert(alignof(int8_t) == 1, "");
static_assert(sizeof(int16_t) == 2, "");
static_assert(alignof(int16_t) == 2, "");
static_assert(sizeof(int32_t) == 4, "");
static_assert(alignof(int32_t) == 4, "");
static_assert(sizeof(Int64) == 8, "");
static_assert(alignof(Int64) == 8, "");
static_assert(sizeof(Int128) == 16, "");
static_assert(alignof(Int128) == 8, "");

template <class Expected, class Actual>
void SameType() {
  static_assert(std::is_same<Expected, Actual>(), "");
}

TEST(Layout, ElementType) {
  {
    using L = Layout<int32_t>;
    SameType<int32_t, L::ElementType<0>>();
    SameType<int32_t, decltype(L::Partial())::ElementType<0>>();
    SameType<int32_t, decltype(L::Partial(0))::ElementType<0>>();
  }
  {
    using L = Layout<int32_t, int32_t>;
    SameType<int32_t, L::ElementType<0>>();
    SameType<int32_t, L::ElementType<1>>();
    SameType<int32_t, decltype(L::Partial())::ElementType<0>>();
    SameType<int32_t, decltype(L::Partial())::ElementType<1>>();
    SameType<int32_t, decltype(L::Partial(0))::ElementType<0>>();
    SameType<int32_t, decltype(L::Partial(0))::ElementType<1>>();
  }
  {
    using L = Layout<int8_t, int32_t, Int128>;
    SameType<int8_t, L::ElementType<0>>();
    SameType<int32_t, L::ElementType<1>>();
    SameType<Int128, L::ElementType<2>>();
    SameType<int8_t, decltype(L::Partial())::ElementType<0>>();
    SameType<int8_t, decltype(L::Partial(0))::ElementType<0>>();
    SameType<int32_t, decltype(L::Partial(0))::ElementType<1>>();
    SameType<int8_t, decltype(L::Partial(0, 0))::ElementType<0>>();
    SameType<int32_t, decltype(L::Partial(0, 0))::ElementType<1>>();
    SameType<Int128, decltype(L::Partial(0, 0))::ElementType<2>>();
    SameType<int8_t, decltype(L::Partial(0, 0, 0))::ElementType<0>>();
    SameType<int32_t, decltype(L::Partial(0, 0, 0))::ElementType<1>>();
    SameType<Int128, decltype(L::Partial(0, 0, 0))::ElementType<2>>();
  }
}

TEST(Layout, ElementTypes) {
  {
    using L = Layout<int32_t>;
    SameType<std::tuple<int32_t>, L::ElementTypes>();
    SameType<std::tuple<int32_t>, decltype(L::Partial())::ElementTypes>();
    SameType<std::tuple<int32_t>, decltype(L::Partial(0))::ElementTypes>();
  }
  {
    using L = Layout<int32_t, int32_t>;
    SameType<std::tuple<int32_t, int32_t>, L::ElementTypes>();
    SameType<std::tuple<int32_t, int32_t>,
             decltype(L::Partial())::ElementTypes>();
    SameType<std::tuple<int32_t, int32_t>,
             decltype(L::Partial(0))::ElementTypes>();
  }
  {
    using L = Layout<int8_t, int32_t, Int128>;
    SameType<std::tuple<int8_t, int32_t, Int128>, L::ElementTypes>();
    SameType<std::tuple<int8_t, int32_t, Int128>,
             decltype(L::Partial())::ElementTypes>();
    SameType<std::tuple<int8_t, int32_t, Int128>,
             decltype(L::Partial(0))::ElementTypes>();
    SameType<std::tuple<int8_t, int32_t, Int128>,
             decltype(L::Partial(0, 0))::ElementTypes>();
    SameType<std::tuple<int8_t, int32_t, Int128>,
             decltype(L::Partial(0, 0, 0))::ElementTypes>();
  }
}

TEST(Layout, OffsetByIndex) {
  {
    using L = Layout<int32_t>;
    EXPECT_EQ(0, L::Partial().Offset<0>());
    EXPECT_EQ(0, L::Partial(3).Offset<0>());
    EXPECT_EQ(0, L(3).Offset<0>());
  }
  {
    using L = Layout<int32_t, int32_t>;
    EXPECT_EQ(0, L::Partial().Offset<0>());
    EXPECT_EQ(0, L::Partial(3).Offset<0>());
    EXPECT_EQ(12, L::Partial(3).Offset<1>());
    EXPECT_EQ(0, L::Partial(3, 5).Offset<0>());
    EXPECT_EQ(12, L::Partial(3, 5).Offset<1>());
    EXPECT_EQ(0, L(3, 5).Offset<0>());
    EXPECT_EQ(12, L(3, 5).Offset<1>());
  }
  {
    using L = Layout<int8_t, int32_t, Int128>;
    EXPECT_EQ(0, L::Partial().Offset<0>());
    EXPECT_EQ(0, L::Partial(0).Offset<0>());
    EXPECT_EQ(0, L::Partial(0).Offset<1>());
    EXPECT_EQ(0, L::Partial(1).Offset<0>());
    EXPECT_EQ(4, L::Partial(1).Offset<1>());
    EXPECT_EQ(0, L::Partial(5).Offset<0>());
    EXPECT_EQ(8, L::Partial(5).Offset<1>());
    EXPECT_EQ(0, L::Partial(0, 0).Offset<0>());
    EXPECT_EQ(0, L::Partial(0, 0).Offset<1>());
    EXPECT_EQ(0, L::Partial(0, 0).Offset<2>());
    EXPECT_EQ(0, L::Partial(1, 0).Offset<0>());
    EXPECT_EQ(4, L::Partial(1, 0).Offset<1>());
    EXPECT_EQ(8, L::Partial(1, 0).Offset<2>());
    EXPECT_EQ(0, L::Partial(5, 3).Offset<0>());
    EXPECT_EQ(8, L::Partial(5, 3).Offset<1>());
    EXPECT_EQ(24, L::Partial(5, 3).Offset<2>());
    EXPECT_EQ(0, L::Partial(0, 0, 0).Offset<0>());
    EXPECT_EQ(0, L::Partial(0, 0, 0).Offset<1>());
    EXPECT_EQ(0, L::Partial(0, 0, 0).Offset<2>());
    EXPECT_EQ(0, L::Partial(1, 0, 0).Offset<0>());
    EXPECT_EQ(4, L::Partial(1, 0, 0).Offset<1>());
    EXPECT_EQ(8, L::Partial(1, 0, 0).Offset<2>());
    EXPECT_EQ(0, L::Partial(5, 3, 1).Offset<0>());
    EXPECT_EQ(24, L::Partial(5, 3, 1).Offset<2>());
    EXPECT_EQ(8, L::Partial(5, 3, 1).Offset<1>());
    EXPECT_EQ(0, L(5, 3, 1).Offset<0>());
    EXPECT_EQ(24, L(5, 3, 1).Offset<2>());
    EXPECT_EQ(8, L(5, 3, 1).Offset<1>());
  }
}

TEST(Layout, OffsetByType) {
  {
    using L = Layout<int32_t>;
    EXPECT_EQ(0, L::Partial().Offset<int32_t>());
    EXPECT_EQ(0, L::Partial(3).Offset<int32_t>());
    EXPECT_EQ(0, L(3).Offset<int32_t>());
  }
  {
    using L = Layout<int8_t, int32_t, Int128>;
    EXPECT_EQ(0, L::Partial().Offset<int8_t>());
    EXPECT_EQ(0, L::Partial(0).Offset<int8_t>());
    EXPECT_EQ(0, L::Partial(0).Offset<int32_t>());
    EXPECT_EQ(0, L::Partial(1).Offset<int8_t>());
    EXPECT_EQ(4, L::Partial(1).Offset<int32_t>());
    EXPECT_EQ(0, L::Partial(5).Offset<int8_t>());
    EXPECT_EQ(8, L::Partial(5).Offset<int32_t>());
    EXPECT_EQ(0, L::Partial(0, 0).Offset<int8_t>());
    EXPECT_EQ(0, L::Partial(0, 0).Offset<int32_t>());
    EXPECT_EQ(0, L::Partial(0, 0).Offset<Int128>());
    EXPECT_EQ(0, L::Partial(1, 0).Offset<int8_t>());
    EXPECT_EQ(4, L::Partial(1, 0).Offset<int32_t>());
    EXPECT_EQ(8, L::Partial(1, 0).Offset<Int128>());
    EXPECT_EQ(0, L::Partial(5, 3).Offset<int8_t>());
    EXPECT_EQ(8, L::Partial(5, 3).Offset<int32_t>());
    EXPECT_EQ(24, L::Partial(5, 3).Offset<Int128>());
    EXPECT_EQ(0, L::Partial(0, 0, 0).Offset<int8_t>());
    EXPECT_EQ(0, L::Partial(0, 0, 0).Offset<int32_t>());
    EXPECT_EQ(0, L::Partial(0, 0, 0).Offset<Int128>());
    EXPECT_EQ(0, L::Partial(1, 0, 0).Offset<int8_t>());
    EXPECT_EQ(4, L::Partial(1, 0, 0).Offset<int32_t>());
    EXPECT_EQ(8, L::Partial(1, 0, 0).Offset<Int128>());
    EXPECT_EQ(0, L::Partial(5, 3, 1).Offset<int8_t>());
    EXPECT_EQ(24, L::Partial(5, 3, 1).Offset<Int128>());
    EXPECT_EQ(8, L::Partial(5, 3, 1).Offset<int32_t>());
    EXPECT_EQ(0, L(5, 3, 1).Offset<int8_t>());
    EXPECT_EQ(24, L(5, 3, 1).Offset<Int128>());
    EXPECT_EQ(8, L(5, 3, 1).Offset<int32_t>());
  }
}

TEST(Layout, Offsets) {
  {
    using L = Layout<int32_t>;
    EXPECT_THAT(L::Partial().Offsets(), ElementsAre(0));
    EXPECT_THAT(L::Partial(3).Offsets(), ElementsAre(0));
    EXPECT_THAT(L(3).Offsets(), ElementsAre(0));
  }
  {
    using L = Layout<int32_t, int32_t>;
    EXPECT_THAT(L::Partial().Offsets(), ElementsAre(0));
    EXPECT_THAT(L::Partial(3).Offsets(), ElementsAre(0, 12));
    EXPECT_THAT(L::Partial(3, 5).Offsets(), ElementsAre(0, 12));
    EXPECT_THAT(L(3, 5).Offsets(), ElementsAre(0, 12));
  }
  {
    using L = Layout<int8_t, int32_t, Int128>;
    EXPECT_THAT(L::Partial().Offsets(), ElementsAre(0));
    EXPECT_THAT(L::Partial(1).Offsets(), ElementsAre(0, 4));
    EXPECT_THAT(L::Partial(5).Offsets(), ElementsAre(0, 8));
    EXPECT_THAT(L::Partial(0, 0).Offsets(), ElementsAre(0, 0, 0));
    EXPECT_THAT(L::Partial(1, 0).Offsets(), ElementsAre(0, 4, 8));
    EXPECT_THAT(L::Partial(5, 3).Offsets(), ElementsAre(0, 8, 24));
    EXPECT_THAT(L::Partial(0, 0, 0).Offsets(), ElementsAre(0, 0, 0));
    EXPECT_THAT(L::Partial(1, 0, 0).Offsets(), ElementsAre(0, 4, 8));
    EXPECT_THAT(L::Partial(5, 3, 1).Offsets(), ElementsAre(0, 8, 24));
    EXPECT_THAT(L(5, 3, 1).Offsets(), ElementsAre(0, 8, 24));
  }
}

TEST(Layout, StaticOffsets) {
  using L = Layout<int8_t, int32_t, Int128>;
  {
    using SL = L::WithStaticSizes<>;
    EXPECT_THAT(SL::Partial().Offsets(), ElementsAre(0));
    EXPECT_THAT(SL::Partial(5).Offsets(), ElementsAre(0, 8));
    EXPECT_THAT(SL::Partial(5, 3, 1).Offsets(), ElementsAre(0, 8, 24));
    EXPECT_THAT(SL(5, 3, 1).Offsets(), ElementsAre(0, 8, 24));
  }
  {
    using SL = L::WithStaticSizes<5>;
    EXPECT_THAT(SL::Partial().Offsets(), ElementsAre(0, 8));
    EXPECT_THAT(SL::Partial(3).Offsets(), ElementsAre(0, 8, 24));
    EXPECT_THAT(SL::Partial(3, 1).Offsets(), ElementsAre(0, 8, 24));
    EXPECT_THAT(SL(3, 1).Offsets(), ElementsAre(0, 8, 24));
  }
  {
    using SL = L::WithStaticSizes<5, 3>;
    EXPECT_THAT(SL::Partial().Offsets(), ElementsAre(0, 8, 24));
    EXPECT_THAT(SL::Partial(1).Offsets(), ElementsAre(0, 8, 24));
    EXPECT_THAT(SL(1).Offsets(), ElementsAre(0, 8, 24));
  }
  {
    using SL = L::WithStaticSizes<5, 3, 1>;
    EXPECT_THAT(SL::Partial().Offsets(), ElementsAre(0, 8, 24));
    EXPECT_THAT(SL().Offsets(), ElementsAre(0, 8, 24));
  }
}

TEST(Layout, AllocSize) {
  {
    using L = Layout<int32_t>;
    EXPECT_EQ(0, L::Partial(0).AllocSize());
    EXPECT_EQ(12, L::Partial(3).AllocSize());
    EXPECT_EQ(12, L(3).AllocSize());
  }
  {
    using L = Layout<int32_t, int32_t>;
    EXPECT_EQ(32, L::Partial(3, 5).AllocSize());
    EXPECT_EQ(32, L(3, 5).AllocSize());
  }
  {
    using L = Layout<int8_t, int32_t, Int128>;
    EXPECT_EQ(0, L::Partial(0, 0, 0).AllocSize());
    EXPECT_EQ(8, L::Partial(1, 0, 0).AllocSize());
    EXPECT_EQ(8, L::Partial(0, 1, 0).AllocSize());
    EXPECT_EQ(16, L::Partial(0, 0, 1).AllocSize());
    EXPECT_EQ(24, L::Partial(1, 1, 1).AllocSize());
    EXPECT_EQ(136, L::Partial(3, 5, 7).AllocSize());
    EXPECT_EQ(136, L(3, 5, 7).AllocSize());
  }
}

TEST(Layout, StaticAllocSize) {
  using L = Layout<int8_t, int32_t, Int128>;
  {
    using SL = L::WithStaticSizes<>;
    EXPECT_EQ(136, SL::Partial(3, 5, 7).AllocSize());
    EXPECT_EQ(136, SL(3, 5, 7).AllocSize());
  }
  {
    using SL = L::WithStaticSizes<3>;
    EXPECT_EQ(136, SL::Partial(5, 7).AllocSize());
    EXPECT_EQ(136, SL(5, 7).AllocSize());
  }
  {
    using SL = L::WithStaticSizes<3, 5>;
    EXPECT_EQ(136, SL::Partial(7).AllocSize());
    EXPECT_EQ(136, SL(7).AllocSize());
  }
  {
    using SL = L::WithStaticSizes<3, 5, 7>;
    EXPECT_EQ(136, SL::Partial().AllocSize());
    EXPECT_EQ(136, SL().AllocSize());
  }
}

TEST(Layout, SizeByIndex) {
  {
    using L = Layout<int32_t>;
    EXPECT_EQ(0, L::Partial(0).Size<0>());
    EXPECT_EQ(3, L::Partial(3).Size<0>());
    EXPECT_EQ(3, L(3).Size<0>());
  }
  {
    using L = Layout<int32_t, int32_t>;
    EXPECT_EQ(0, L::Partial(0).Size<0>());
    EXPECT_EQ(3, L::Partial(3).Size<0>());
    EXPECT_EQ(3, L::Partial(3, 5).Size<0>());
    EXPECT_EQ(5, L::Partial(3, 5).Size<1>());
    EXPECT_EQ(3, L(3, 5).Size<0>());
    EXPECT_EQ(5, L(3, 5).Size<1>());
  }
  {
    using L = Layout<int8_t, int32_t, Int128>;
    EXPECT_EQ(3, L::Partial(3).Size<0>());
    EXPECT_EQ(3, L::Partial(3, 5).Size<0>());
    EXPECT_EQ(5, L::Partial(3, 5).Size<1>());
    EXPECT_EQ(3, L::Partial(3, 5, 7).Size<0>());
    EXPECT_EQ(5, L::Partial(3, 5, 7).Size<1>());
    EXPECT_EQ(7, L::Partial(3, 5, 7).Size<2>());
    EXPECT_EQ(3, L(3, 5, 7).Size<0>());
    EXPECT_EQ(5, L(3, 5, 7).Size<1>());
    EXPECT_EQ(7, L(3, 5, 7).Size<2>());
  }
}

TEST(Layout, SizeByType) {
  {
    using L = Layout<int32_t>;
    EXPECT_EQ(0, L::Partial(0).Size<int32_t>());
    EXPECT_EQ(3, L::Partial(3).Size<int32_t>());
    EXPECT_EQ(3, L(3).Size<int32_t>());
  }
  {
    using L = Layout<int8_t, int32_t, Int128>;
    EXPECT_EQ(3, L::Partial(3).Size<int8_t>());
    EXPECT_EQ(3, L::Partial(3, 5).Size<int8_t>());
    EXPECT_EQ(5, L::Partial(3, 5).Size<int32_t>());
    EXPECT_EQ(3, L::Partial(3, 5, 7).Size<int8_t>());
    EXPECT_EQ(5, L::Partial(3, 5, 7).Size<int32_t>());
    EXPECT_EQ(7, L::Partial(3, 5, 7).Size<Int128>());
    EXPECT_EQ(3, L(3, 5, 7).Size<int8_t>());
    EXPECT_EQ(5, L(3, 5, 7).Size<int32_t>());
    EXPECT_EQ(7, L(3, 5, 7).Size<Int128>());
  }
}

TEST(Layout, Sizes) {
  {
    using L = Layout<int32_t>;
    EXPECT_THAT(L::Partial().Sizes(), ElementsAre());
    EXPECT_THAT(L::Partial(3).Sizes(), ElementsAre(3));
    EXPECT_THAT(L(3).Sizes(), ElementsAre(3));
  }
  {
    using L = Layout<int32_t, int32_t>;
    EXPECT_THAT(L::Partial().Sizes(), ElementsAre());
    EXPECT_THAT(L::Partial(3).Sizes(), ElementsAre(3));
    EXPECT_THAT(L::Partial(3, 5).Sizes(), ElementsAre(3, 5));
    EXPECT_THAT(L(3, 5).Sizes(), ElementsAre(3, 5));
  }
  {
    using L = Layout<int8_t, int32_t, Int128>;
    EXPECT_THAT(L::Partial().Sizes(), ElementsAre());
    EXPECT_THAT(L::Partial(3).Sizes(), ElementsAre(3));
    EXPECT_THAT(L::Partial(3, 5).Sizes(), ElementsAre(3, 5));
    EXPECT_THAT(L::Partial(3, 5, 7).Sizes(), ElementsAre(3, 5, 7));
    EXPECT_THAT(L(3, 5, 7).Sizes(), ElementsAre(3, 5, 7));
  }
}

TEST(Layout, StaticSize) {
  using L = Layout<int8_t, int32_t, Int128>;
  {
    using SL = L::WithStaticSizes<>;
    EXPECT_THAT(SL::Partial().Sizes(), ElementsAre());
    EXPECT_THAT(SL::Partial(3).Size<0>(), 3);
    EXPECT_THAT(SL::Partial(3).Size<int8_t>(), 3);
    EXPECT_THAT(SL::Partial(3).Sizes(), ElementsAre(3));
    EXPECT_THAT(SL::Partial(3, 5, 7).Size<0>(), 3);
    EXPECT_THAT(SL::Partial(3, 5, 7).Size<int8_t>(), 3);
    EXPECT_THAT(SL::Partial(3, 5, 7).Size<2>(), 7);
    EXPECT_THAT(SL::Partial(3, 5, 7).Size<Int128>(), 7);
    EXPECT_THAT(SL::Partial(3, 5, 7).Sizes(), ElementsAre(3, 5, 7));
    EXPECT_THAT(SL(3, 5, 7).Size<0>(), 3);
    EXPECT_THAT(SL(3, 5, 7).Size<int8_t>(), 3);
    EXPECT_THAT(SL(3, 5, 7).Size<2>(), 7);
    EXPECT_THAT(SL(3, 5, 7).Size<Int128>(), 7);
    EXPECT_THAT(SL(3, 5, 7).Sizes(), ElementsAre(3, 5, 7));
  }
}

TEST(Layout, PointerByIndex) {
  alignas(max_align_t) const unsigned char p[100] = {0};
  {
    using L = Layout<int32_t>;
    EXPECT_EQ(0, Distance(p, Type<const int32_t*>(L::Partial().Pointer<0>(p))));
    EXPECT_EQ(0,
              Distance(p, Type<const int32_t*>(L::Partial(3).Pointer<0>(p))));
    EXPECT_EQ(0, Distance(p, Type<const int32_t*>(L(3).Pointer<0>(p))));
  }
  {
    using L = Layout<int32_t, int32_t>;
    EXPECT_EQ(0, Distance(p, Type<const int32_t*>(L::Partial().Pointer<0>(p))));
    EXPECT_EQ(0,
              Distance(p, Type<const int32_t*>(L::Partial(3).Pointer<0>(p))));
    EXPECT_EQ(12,
              Distance(p, Type<const int32_t*>(L::Partial(3).Pointer<1>(p))));
    EXPECT_EQ(
        0, Distance(p, Type<const int32_t*>(L::Partial(3, 5).Pointer<0>(p))));
    EXPECT_EQ(
        12, Distance(p, Type<const int32_t*>(L::Partial(3, 5).Pointer<1>(p))));
    EXPECT_EQ(0, Distance(p, Type<const int32_t*>(L(3, 5).Pointer<0>(p))));
    EXPECT_EQ(12, Distance(p, Type<const int32_t*>(L(3, 5).Pointer<1>(p))));
  }
  {
    using L = Layout<int8_t, int32_t, Int128>;
    EXPECT_EQ(0, Distance(p, Type<const int8_t*>(L::Partial().Pointer<0>(p))));
    EXPECT_EQ(0, Distance(p, Type<const int8_t*>(L::Partial(0).Pointer<0>(p))));
    EXPECT_EQ(0,
              Distance(p, Type<const int32_t*>(L::Partial(0).Pointer<1>(p))));
    EXPECT_EQ(0, Distance(p, Type<const int8_t*>(L::Partial(1).Pointer<0>(p))));
    EXPECT_EQ(4,
              Distance(p, Type<const int32_t*>(L::Partial(1).Pointer<1>(p))));
    EXPECT_EQ(0, Distance(p, Type<const int8_t*>(L::Partial(5).Pointer<0>(p))));
    EXPECT_EQ(8,
              Distance(p, Type<const int32_t*>(L::Partial(5).Pointer<1>(p))));
    EXPECT_EQ(0,
              Distance(p, Type<const int8_t*>(L::Partial(0, 0).Pointer<0>(p))));
    EXPECT_EQ(
        0, Distance(p, Type<const int32_t*>(L::Partial(0, 0).Pointer<1>(p))));
    EXPECT_EQ(0,
              Distance(p, Type<const Int128*>(L::Partial(0, 0).Pointer<2>(p))));
    EXPECT_EQ(0,
              Distance(p, Type<const int8_t*>(L::Partial(1, 0).Pointer<0>(p))));
    EXPECT_EQ(
        4, Distance(p, Type<const int32_t*>(L::Partial(1, 0).Pointer<1>(p))));
    EXPECT_EQ(8,
              Distance(p, Type<const Int128*>(L::Partial(1, 0).Pointer<2>(p))));
    EXPECT_EQ(0,
              Distance(p, Type<const int8_t*>(L::Partial(5, 3).Pointer<0>(p))));
    EXPECT_EQ(
        8, Distance(p, Type<const int32_t*>(L::Partial(5, 3).Pointer<1>(p))));
    EXPECT_EQ(24,
              Distance(p, Type<const Int128*>(L::Partial(5, 3).Pointer<2>(p))));
    EXPECT_EQ(
        0, Distance(p, Type<const int8_t*>(L::Partial(0, 0, 0).Pointer<0>(p))));
    EXPECT_EQ(
        0,
        Distance(p, Type<const int32_t*>(L::Partial(0, 0, 0).Pointer<1>(p))));
    EXPECT_EQ(
        0, Distance(p, Type<const Int128*>(L::Partial(0, 0, 0).Pointer<2>(p))));
    EXPECT_EQ(
        0, Distance(p, Type<const int8_t*>(L::Partial(1, 0, 0).Pointer<0>(p))));
    EXPECT_EQ(
        4,
        Distance(p, Type<const int32_t*>(L::Partial(1, 0, 0).Pointer<1>(p))));
    EXPECT_EQ(
        8, Distance(p, Type<const Int128*>(L::Partial(1, 0, 0).Pointer<2>(p))));
    EXPECT_EQ(
        0, Distance(p, Type<const int8_t*>(L::Partial(5, 3, 1).Pointer<0>(p))));
    EXPECT_EQ(
        24,
        Distance(p, Type<const Int128*>(L::Partial(5, 3, 1).Pointer<2>(p))));
    EXPECT_EQ(
        8,
        Distance(p, Type<const int32_t*>(L::Partial(5, 3, 1).Pointer<1>(p))));
    EXPECT_EQ(0, Distance(p, Type<const int8_t*>(L(5, 3, 1).Pointer<0>(p))));
    EXPECT_EQ(24, Distance(p, Type<const Int128*>(L(5, 3, 1).Pointer<2>(p))));
    EXPECT_EQ(8, Distance(p, Type<const int32_t*>(L(5, 3, 1).Pointer<1>(p))));
  }
}

TEST(Layout, PointerByType) {
  alignas(max_align_t) const unsigned char p[100] = {0};
  {
    using L = Layout<int32_t>;
    EXPECT_EQ(
        0, Distance(p, Type<const int32_t*>(L::Partial().Pointer<int32_t>(p))));
    EXPECT_EQ(
        0,
        Distance(p, Type<const int32_t*>(L::Partial(3).Pointer<int32_t>(p))));
    EXPECT_EQ(0, Distance(p, Type<const int32_t*>(L(3).Pointer<int32_t>(p))));
  }
  {
    using L = Layout<int8_t, int32_t, Int128>;
    EXPECT_EQ(
        0, Distance(p, Type<const int8_t*>(L::Partial().Pointer<int8_t>(p))));
    EXPECT_EQ(
        0, Distance(p, Type<const int8_t*>(L::Partial(0).Pointer<int8_t>(p))));
    EXPECT_EQ(
        0,
        Distance(p, Type<const int32_t*>(L::Partial(0).Pointer<int32_t>(p))));
    EXPECT_EQ(
        0, Distance(p, Type<const int8_t*>(L::Partial(1).Pointer<int8_t>(p))));
    EXPECT_EQ(
        4,
        Distance(p, Type<const int32_t*>(L::Partial(1).Pointer<int32_t>(p))));
    EXPECT_EQ(
        0, Distance(p, Type<const int8_t*>(L::Partial(5).Pointer<int8_t>(p))));
    EXPECT_EQ(
        8,
        Distance(p, Type<const int32_t*>(L::Partial(5).Pointer<int32_t>(p))));
    EXPECT_EQ(
        0,
        Distance(p, Type<const int8_t*>(L::Partial(0, 0).Pointer<int8_t>(p))));
    EXPECT_EQ(0, Distance(p, Type<const int32_t*>(
                                 L::Partial(0, 0).Pointer<int32_t>(p))));
    EXPECT_EQ(
        0,
        Distance(p, Type<const Int128*>(L::Partial(0, 0).Pointer<Int128>(p))));
    EXPECT_EQ(
        0,
        Distance(p, Type<const int8_t*>(L::Partial(1, 0).Pointer<int8_t>(p))));
    EXPECT_EQ(4, Distance(p, Type<const int32_t*>(
                                 L::Partial(1, 0).Pointer<int32_t>(p))));
    EXPECT_EQ(
        8,
        Distance(p, Type<const Int128*>(L::Partial(1, 0).Pointer<Int128>(p))));
    EXPECT_EQ(
        0,
        Distance(p, Type<const int8_t*>(L::Partial(5, 3).Pointer<int8_t>(p))));
    EXPECT_EQ(8, Distance(p, Type<const int32_t*>(
                                 L::Partial(5, 3).Pointer<int32_t>(p))));
    EXPECT_EQ(
        24,
        Distance(p, Type<const Int128*>(L::Partial(5, 3).Pointer<Int128>(p))));
    EXPECT_EQ(0, Distance(p, Type<const int8_t*>(
                                 L::Partial(0, 0, 0).Pointer<int8_t>(p))));
    EXPECT_EQ(0, Distance(p, Type<const int32_t*>(
                                 L::Partial(0, 0, 0).Pointer<int32_t>(p))));
    EXPECT_EQ(0, Distance(p, Type<const Int128*>(
                                 L::Partial(0, 0, 0).Pointer<Int128>(p))));
    EXPECT_EQ(0, Distance(p, Type<const int8_t*>(
                                 L::Partial(1, 0, 0).Pointer<int8_t>(p))));
    EXPECT_EQ(4, Distance(p, Type<const int32_t*>(
                                 L::Partial(1, 0, 0).Pointer<int32_t>(p))));
    EXPECT_EQ(8, Distance(p, Type<const Int128*>(
                                 L::Partial(1, 0, 0).Pointer<Int128>(p))));
    EXPECT_EQ(0, Distance(p, Type<const int8_t*>(
                                 L::Partial(5, 3, 1).Pointer<int8_t>(p))));
    EXPECT_EQ(24, Distance(p, Type<const Int128*>(
                                  L::Partial(5, 3, 1).Pointer<Int128>(p))));
    EXPECT_EQ(8, Distance(p, Type<const int32_t*>(
                                 L::Partial(5, 3, 1).Pointer<int32_t>(p))));
    EXPECT_EQ(24,
              Distance(p, Type<const Int128*>(L(5, 3, 1).Pointer<Int128>(p))));
    EXPECT_EQ(
        8, Distance(p, Type<const int32_t*>(L(5, 3, 1).Pointer<int32_t>(p))));
  }
}

TEST(Layout, MutablePointerByIndex) {
  alignas(max_align_t) unsigned char p[100] = {0};
  {
    using L = Layout<int32_t>;
    EXPECT_EQ(0, Distance(p, Type<int32_t*>(L::Partial().Pointer<0>(p))));
    EXPECT_EQ(0, Distance(p, Type<int32_t*>(L::Partial(3).Pointer<0>(p))));
    EXPECT_EQ(0, Distance(p, Type<int32_t*>(L(3).Pointer<0>(p))));
  }
  {
    using L = Layout<int32_t, int32_t>;
    EXPECT_EQ(0, Distance(p, Type<int32_t*>(L::Partial().Pointer<0>(p))));
    EXPECT_EQ(0, Distance(p, Type<int32_t*>(L::Partial(3).Pointer<0>(p))));
    EXPECT_EQ(12, Distance(p, Type<int32_t*>(L::Partial(3).Pointer<1>(p))));
    EXPECT_EQ(0, Distance(p, Type<int32_t*>(L::Partial(3, 5).Pointer<0>(p))));
    EXPECT_EQ(12, Distance(p, Type<int32_t*>(L::Partial(3, 5).Pointer<1>(p))));
    EXPECT_EQ(0, Distance(p, Type<int32_t*>(L(3, 5).Pointer<0>(p))));
    EXPECT_EQ(12, Distance(p, Type<int32_t*>(L(3, 5).Pointer<1>(p))));
  }
  {
    using L = Layout<int8_t, int32_t, Int128>;
    EXPECT_EQ(0, Distance(p, Type<int8_t*>(L::Partial().Pointer<0>(p))));
    EXPECT_EQ(0, Distance(p, Type<int8_t*>(L::Partial(0).Pointer<0>(p))));
    EXPECT_EQ(0, Distance(p, Type<int32_t*>(L::Partial(0).Pointer<1>(p))));
    EXPECT_EQ(0, Distance(p, Type<int8_t*>(L::Partial(1).Pointer<0>(p))));
    EXPECT_EQ(4, Distance(p, Type<int32_t*>(L::Partial(1).Pointer<1>(p))));
    EXPECT_EQ(0, Distance(p, Type<int8_t*>(L::Partial(5).Pointer<0>(p))));
    EXPECT_EQ(8, Distance(p, Type<int32_t*>(L::Partial(5).Pointer<1>(p))));
    EXPECT_EQ(0, Distance(p, Type<int8_t*>(L::Partial(0, 0).Pointer<0>(p))));
    EXPECT_EQ(0, Distance(p, Type<int32_t*>(L::Partial(0, 0).Pointer<1>(p))));
    EXPECT_EQ(0, Distance(p, Type<Int128*>(L::Partial(0, 0).Pointer<2>(p))));
    EXPECT_EQ(0, Distance(p, Type<int8_t*>(L::Partial(1, 0).Pointer<0>(p))));
    EXPECT_EQ(4, Distance(p, Type<int32_t*>(L::Partial(1, 0).Pointer<1>(p))));
    EXPECT_EQ(8, Distance(p, Type<Int128*>(L::Partial(1, 0).Pointer<2>(p))));
    EXPECT_EQ(0, Distance(p, Type<int8_t*>(L::Partial(5, 3).Pointer<0>(p))));
    EXPECT_EQ(8, Distance(p, Type<int32_t*>(L::Partial(5, 3).Pointer<1>(p))));
    EXPECT_EQ(24, Distance(p, Type<Int128*>(L::Partial(5, 3).Pointer<2>(p))));
    EXPECT_EQ(0, Distance(p, Type<int8_t*>(L::Partial(0, 0, 0).Pointer<0>(p))));
    EXPECT_EQ(0,
              Distance(p, Type<int32_t*>(L::Partial(0, 0, 0).Pointer<1>(p))));
    EXPECT_EQ(0, Distance(p, Type<Int128*>(L::Partial(0, 0, 0).Pointer<2>(p))));
    EXPECT_EQ(0, Distance(p, Type<int8_t*>(L::Partial(1, 0, 0).Pointer<0>(p))));
    EXPECT_EQ(4,
              Distance(p, Type<int32_t*>(L::Partial(1, 0, 0).Pointer<1>(p))));
    EXPECT_EQ(8, Distance(p, Type<Int128*>(L::Partial(1, 0, 0).Pointer<2>(p))));
    EXPECT_EQ(0, Distance(p, Type<int8_t*>(L::Partial(5, 3, 1).Pointer<0>(p))));
    EXPECT_EQ(24,
              Distance(p, Type<Int128*>(L::Partial(5, 3, 1).Pointer<2>(p))));
    EXPECT_EQ(8,
              Distance(p, Type<int32_t*>(L::Partial(5, 3, 1).Pointer<1>(p))));
    EXPECT_EQ(0, Distance(p, Type<int8_t*>(L(5, 3, 1).Pointer<0>(p))));
    EXPECT_EQ(24, Distance(p, Type<Int128*>(L(5, 3, 1).Pointer<2>(p))));
    EXPECT_EQ(8, Distance(p, Type<int32_t*>(L(5, 3, 1).Pointer<1>(p))));
  }
}

TEST(Layout, MutablePointerByType) {
  alignas(max_align_t) unsigned char p[100] = {0};
  {
    using L = Layout<int32_t>;
    EXPECT_EQ(0, Distance(p, Type<int32_t*>(L::Partial().Pointer<int32_t>(p))));
    EXPECT_EQ(0,
              Distance(p, Type<int32_t*>(L::Partial(3).Pointer<int32_t>(p))));
    EXPECT_EQ(0, Distance(p, Type<int32_t*>(L(3).Pointer<int32_t>(p))));
  }
  {
    using L = Layout<int8_t, int32_t, Int128>;
    EXPECT_EQ(0, Distance(p, Type<int8_t*>(L::Partial().Pointer<int8_t>(p))));
    EXPECT_EQ(0, Distance(p, Type<int8_t*>(L::Partial(0).Pointer<int8_t>(p))));
    EXPECT_EQ(0,
              Distance(p, Type<int32_t*>(L::Partial(0).Pointer<int32_t>(p))));
    EXPECT_EQ(0, Distance(p, Type<int8_t*>(L::Partial(1).Pointer<int8_t>(p))));
    EXPECT_EQ(4,
              Distance(p, Type<int32_t*>(L::Partial(1).Pointer<int32_t>(p))));
    EXPECT_EQ(0, Distance(p, Type<int8_t*>(L::Partial(5).Pointer<int8_t>(p))));
    EXPECT_EQ(8,
              Distance(p, Type<int32_t*>(L::Partial(5).Pointer<int32_t>(p))));
    EXPECT_EQ(0,
              Distance(p, Type<int8_t*>(L::Partial(0, 0).Pointer<int8_t>(p))));
    EXPECT_EQ(
        0, Distance(p, Type<int32_t*>(L::Partial(0, 0).Pointer<int32_t>(p))));
    EXPECT_EQ(0,
              Distance(p, Type<Int128*>(L::Partial(0, 0).Pointer<Int128>(p))));
    EXPECT_EQ(0,
              Distance(p, Type<int8_t*>(L::Partial(1, 0).Pointer<int8_t>(p))));
    EXPECT_EQ(
        4, Distance(p, Type<int32_t*>(L::Partial(1, 0).Pointer<int32_t>(p))));
    EXPECT_EQ(8,
              Distance(p, Type<Int128*>(L::Partial(1, 0).Pointer<Int128>(p))));
    EXPECT_EQ(0,
              Distance(p, Type<int8_t*>(L::Partial(5, 3).Pointer<int8_t>(p))));
    EXPECT_EQ(
        8, Distance(p, Type<int32_t*>(L::Partial(5, 3).Pointer<int32_t>(p))));
    EXPECT_EQ(24,
              Distance(p, Type<Int128*>(L::Partial(5, 3).Pointer<Int128>(p))));
    EXPECT_EQ(
        0, Distance(p, Type<int8_t*>(L::Partial(0, 0, 0).Pointer<int8_t>(p))));
    EXPECT_EQ(
        0,
        Distance(p, Type<int32_t*>(L::Partial(0, 0, 0).Pointer<int32_t>(p))));
    EXPECT_EQ(
        0, Distance(p, Type<Int128*>(L::Partial(0, 0, 0).Pointer<Int128>(p))));
    EXPECT_EQ(
        0, Distance(p, Type<int8_t*>(L::Partial(1, 0, 0).Pointer<int8_t>(p))));
    EXPECT_EQ(
        4,
        Distance(p, Type<int32_t*>(L::Partial(1, 0, 0).Pointer<int32_t>(p))));
    EXPECT_EQ(
        8, Distance(p, Type<Int128*>(L::Partial(1, 0, 0).Pointer<Int128>(p))));
    EXPECT_EQ(
        0, Distance(p, Type<int8_t*>(L::Partial(5, 3, 1).Pointer<int8_t>(p))));
    EXPECT_EQ(
        24, Distance(p, Type<Int128*>(L::Partial(5, 3, 1).Pointer<Int128>(p))));
    EXPECT_EQ(
        8,
        Distance(p, Type<int32_t*>(L::Partial(5, 3, 1).Pointer<int32_t>(p))));
    EXPECT_EQ(0, Distance(p, Type<int8_t*>(L(5, 3, 1).Pointer<int8_t>(p))));
    EXPECT_EQ(24, Distance(p, Type<Int128*>(L(5, 3, 1).Pointer<Int128>(p))));
    EXPECT_EQ(8, Distance(p, Type<int32_t*>(L(5, 3, 1).Pointer<int32_t>(p))));
  }
}

TEST(Layout, Pointers) {
  alignas(max_align_t) const unsigned char p[100] = {0};
  using L = Layout<int8_t, int8_t, Int128>;
  {
    const auto x = L::Partial();
    EXPECT_EQ(std::make_tuple(x.Pointer<0>(p)),
              Type<std::tuple<const int8_t*>>(x.Pointers(p)));
  }
  {
    const auto x = L::Partial(1);
    EXPECT_EQ(std::make_tuple(x.Pointer<0>(p), x.Pointer<1>(p)),
              (Type<std::tuple<const int8_t*, const int8_t*>>(x.Pointers(p))));
  }
  {
    const auto x = L::Partial(1, 2);
    EXPECT_EQ(
        std::make_tuple(x.Pointer<0>(p), x.Pointer<1>(p), x.Pointer<2>(p)),
        (Type<std::tuple<const int8_t*, const int8_t*, const Int128*>>(
            x.Pointers(p))));
  }
  {
    const auto x = L::Partial(1, 2, 3);
    EXPECT_EQ(
        std::make_tuple(x.Pointer<0>(p), x.Pointer<1>(p), x.Pointer<2>(p)),
        (Type<std::tuple<const int8_t*, const int8_t*, const Int128*>>(
            x.Pointers(p))));
  }
  {
    const L x(1, 2, 3);
    EXPECT_EQ(
        std::make_tuple(x.Pointer<0>(p), x.Pointer<1>(p), x.Pointer<2>(p)),
        (Type<std::tuple<const int8_t*, const int8_t*, const Int128*>>(
            x.Pointers(p))));
  }
}

TEST(Layout, MutablePointers) {
  alignas(max_align_t) unsigned char p[100] = {0};
  using L = Layout<int8_t, int8_t, Int128>;
  {
    const auto x = L::Partial();
    EXPECT_EQ(std::make_tuple(x.Pointer<0>(p)),
              Type<std::tuple<int8_t*>>(x.Pointers(p)));
  }
  {
    const auto x = L::Partial(1);
    EXPECT_EQ(std::make_tuple(x.Pointer<0>(p), x.Pointer<1>(p)),
              (Type<std::tuple<int8_t*, int8_t*>>(x.Pointers(p))));
  }
  {
    const auto x = L::Partial(1, 2);
    EXPECT_EQ(
        std::make_tuple(x.Pointer<0>(p), x.Pointer<1>(p), x.Pointer<2>(p)),
        (Type<std::tuple<int8_t*, int8_t*, Int128*>>(x.Pointers(p))));
  }
  {
    const auto x = L::Partial(1, 2, 3);
    EXPECT_EQ(
        std::make_tuple(x.Pointer<0>(p), x.Pointer<1>(p), x.Pointer<2>(p)),
        (Type<std::tuple<int8_t*, int8_t*, Int128*>>(x.Pointers(p))));
  }
  {
    const L x(1, 2, 3);
    EXPECT_EQ(
        std::make_tuple(x.Pointer<0>(p), x.Pointer<1>(p), x.Pointer<2>(p)),
        (Type<std::tuple<int8_t*, int8_t*, Int128*>>(x.Pointers(p))));
  }
}

TEST(Layout, StaticPointers) {
  alignas(max_align_t) const unsigned char p[100] = {0};
  using L = Layout<int8_t, int8_t, Int128>;
  {
    const auto x = L::WithStaticSizes<>::Partial();
    EXPECT_EQ(std::make_tuple(x.Pointer<0>(p)),
              Type<std::tuple<const int8_t*>>(x.Pointers(p)));
  }
  {
    const auto x = L::WithStaticSizes<>::Partial(1);
    EXPECT_EQ(std::make_tuple(x.Pointer<0>(p), x.Pointer<1>(p)),
              (Type<std::tuple<const int8_t*, const int8_t*>>(x.Pointers(p))));
  }
  {
    const auto x = L::WithStaticSizes<1>::Partial();
    EXPECT_EQ(std::make_tuple(x.Pointer<0>(p), x.Pointer<1>(p)),
              (Type<std::tuple<const int8_t*, const int8_t*>>(x.Pointers(p))));
  }
  {
    const auto x = L::WithStaticSizes<>::Partial(1, 2, 3);
    EXPECT_EQ(
        std::make_tuple(x.Pointer<0>(p), x.Pointer<1>(p), x.Pointer<2>(p)),
        (Type<std::tuple<const int8_t*, const int8_t*, const Int128*>>(
            x.Pointers(p))));
  }
  {
    const auto x = L::WithStaticSizes<1>::Partial(2, 3);
    EXPECT_EQ(
        std::make_tuple(x.Pointer<0>(p), x.Pointer<1>(p), x.Pointer<2>(p)),
        (Type<std::tuple<const int8_t*, const int8_t*, const Int128*>>(
            x.Pointers(p))));
  }
  {
    const auto x = L::WithStaticSizes<1, 2>::Partial(3);
    EXPECT_EQ(
        std::make_tuple(x.Pointer<0>(p), x.Pointer<1>(p), x.Pointer<2>(p)),
        (Type<std::tuple<const int8_t*, const int8_t*, const Int128*>>(
            x.Pointers(p))));
  }
  {
    const auto x = L::WithStaticSizes<1, 2, 3>::Partial();
    EXPECT_EQ(
        std::make_tuple(x.Pointer<0>(p), x.Pointer<1>(p), x.Pointer<2>(p)),
        (Type<std::tuple<const int8_t*, const int8_t*, const Int128*>>(
            x.Pointers(p))));
  }
  {
    const L::WithStaticSizes<1, 2, 3> x;
    EXPECT_EQ(
        std::make_tuple(x.Pointer<0>(p), x.Pointer<1>(p), x.Pointer<2>(p)),
        (Type<std::tuple<const int8_t*, const int8_t*, const Int128*>>(
            x.Pointers(p))));
  }
}

TEST(Layout, SliceByIndexSize) {
  alignas(max_align_t) const unsigned char p[100] = {0};
  {
    using L = Layout<int32_t>;
    EXPECT_EQ(0, L::Partial(0).Slice<0>(p).size());
    EXPECT_EQ(3, L::Partial(3).Slice<0>(p).size());
    EXPECT_EQ(3, L(3).Slice<0>(p).size());
  }
  {
    using L = Layout<int32_t, int32_t>;
    EXPECT_EQ(3, L::Partial(3).Slice<0>(p).size());
    EXPECT_EQ(5, L::Partial(3, 5).Slice<1>(p).size());
    EXPECT_EQ(5, L(3, 5).Slice<1>(p).size());
  }
  {
    using L = Layout<int8_t, int32_t, Int128>;
    EXPECT_EQ(3, L::Partial(3).Slice<0>(p).size());
    EXPECT_EQ(3, L::Partial(3, 5).Slice<0>(p).size());
    EXPECT_EQ(5, L::Partial(3, 5).Slice<1>(p).size());
    EXPECT_EQ(3, L::Partial(3, 5, 7).Slice<0>(p).size());
    EXPECT_EQ(5, L::Partial(3, 5, 7).Slice<1>(p).size());
    EXPECT_EQ(7, L::Partial(3, 5, 7).Slice<2>(p).size());
    EXPECT_EQ(3, L(3, 5, 7).Slice<0>(p).size());
    EXPECT_EQ(5, L(3, 5, 7).Slice<1>(p).size());
    EXPECT_EQ(7, L(3, 5, 7).Slice<2>(p).size());
  }
}

TEST(Layout, SliceByTypeSize) {
  alignas(max_align_t) const unsigned char p[100] = {0};
  {
    using L = Layout<int32_t>;
    EXPECT_EQ(0, L::Partial(0).Slice<int32_t>(p).size());
    EXPECT_EQ(3, L::Partial(3).Slice<int32_t>(p).size());
    EXPECT_EQ(3, L(3).Slice<int32_t>(p).size());
  }
  {
    using L = Layout<int8_t, int32_t, Int128>;
    EXPECT_EQ(3, L::Partial(3).Slice<int8_t>(p).size());
    EXPECT_EQ(3, L::Partial(3, 5).Slice<int8_t>(p).size());
    EXPECT_EQ(5, L::Partial(3, 5).Slice<int32_t>(p).size());
    EXPECT_EQ(3, L::Partial(3, 5, 7).Slice<int8_t>(p).size());
    EXPECT_EQ(5, L::Partial(3, 5, 7).Slice<int32_t>(p).size());
    EXPECT_EQ(7, L::Partial(3, 5, 7).Slice<Int128>(p).size());
    EXPECT_EQ(3, L(3, 5, 7).Slice<int8_t>(p).size());
    EXPECT_EQ(5, L(3, 5, 7).Slice<int32_t>(p).size());
    EXPECT_EQ(7, L(3, 5, 7).Slice<Int128>(p).size());
  }
}
TEST(Layout, MutableSliceByIndexSize) {
  alignas(max_align_t) unsigned char p[100] = {0};
  {
    using L = Layout<int32_t>;
    EXPECT_EQ(0, L::Partial(0).Slice<0>(p).size());
    EXPECT_EQ(3, L::Partial(3).Slice<0>(p).size());
    EXPECT_EQ(3, L(3).Slice<0>(p).size());
  }
  {
    using L = Layout<int32_t, int32_t>;
    EXPECT_EQ(3, L::Partial(3).Slice<0>(p).size());
    EXPECT_EQ(5, L::Partial(3, 5).Slice<1>(p).size());
    EXPECT_EQ(5, L(3, 5).Slice<1>(p).size());
  }
  {
    using L = Layout<int8_t, int32_t, Int128>;
    EXPECT_EQ(3, L::Partial(3).Slice<0>(p).size());
    EXPECT_EQ(3, L::Partial(3, 5).Slice<0>(p).size());
    EXPECT_EQ(5, L::Partial(3, 5).Slice<1>(p).size());
    EXPECT_EQ(3, L::Partial(3, 5, 7).Slice<0>(p).size());
    EXPECT_EQ(5, L::Partial(3, 5, 7).Slice<1>(p).size());
    EXPECT_EQ(7, L::Partial(3, 5, 7).Slice<2>(p).size());
    EXPECT_EQ(3, L(3, 5, 7).Slice<0>(p).size());
    EXPECT_EQ(5, L(3, 5, 7).Slice<1>(p).size());
    EXPECT_EQ(7, L(3, 5, 7).Slice<2>(p).size());
  }
}

TEST(Layout, MutableSliceByTypeSize) {
  alignas(max_align_t) unsigned char p[100] = {0};
  {
    using L = Layout<int32_t>;
    EXPECT_EQ(0, L::Partial(0).Slice<int32_t>(p).size());
    EXPECT_EQ(3, L::Partial(3).Slice<int32_t>(p).size());
    EXPECT_EQ(3, L(3).Slice<int32_t>(p).size());
  }
  {
    using L = Layout<int8_t, int32_t, Int128>;
    EXPECT_EQ(3, L::Partial(3).Slice<int8_t>(p).size());
    EXPECT_EQ(3, L::Partial(3, 5).Slice<int8_t>(p).size());
    EXPECT_EQ(5, L::Partial(3, 5).Slice<int32_t>(p).size());
    EXPECT_EQ(3, L::Partial(3, 5, 7).Slice<int8_t>(p).size());
    EXPECT_EQ(5, L::Partial(3, 5, 7).Slice<int32_t>(p).size());
    EXPECT_EQ(7, L::Partial(3, 5, 7).Slice<Int128>(p).size());
    EXPECT_EQ(3, L(3, 5, 7).Slice<int8_t>(p).size());
    EXPECT_EQ(5, L(3, 5, 7).Slice<int32_t>(p).size());
    EXPECT_EQ(7, L(3, 5, 7).Slice<Int128>(p).size());
  }
}

TEST(Layout, StaticSliceSize) {
  alignas(max_align_t) const unsigned char cp[100] = {0};
  alignas(max_align_t) unsigned char p[100] = {0};
  using L = Layout<int8_t, int32_t, Int128>;
  using SL = L::WithStaticSizes<3, 5>;

  EXPECT_EQ(3, SL::Partial().Slice<0>(cp).size());
  EXPECT_EQ(3, SL::Partial().Slice<int8_t>(cp).size());
  EXPECT_EQ(3, SL::Partial(7).Slice<0>(cp).size());
  EXPECT_EQ(3, SL::Partial(7).Slice<int8_t>(cp).size());

  EXPECT_EQ(5, SL::Partial().Slice<1>(cp).size());
  EXPECT_EQ(5, SL::Partial().Slice<int32_t>(cp).size());
  EXPECT_EQ(5, SL::Partial(7).Slice<1>(cp).size());
  EXPECT_EQ(5, SL::Partial(7).Slice<int32_t>(cp).size());

  EXPECT_EQ(7, SL::Partial(7).Slice<2>(cp).size());
  EXPECT_EQ(7, SL::Partial(7).Slice<Int128>(cp).size());

  EXPECT_EQ(3, SL::Partial().Slice<0>(p).size());
  EXPECT_EQ(3, SL::Partial().Slice<int8_t>(p).size());
  EXPECT_EQ(3, SL::Partial(7).Slice<0>(p).size());
  EXPECT_EQ(3, SL::Partial(7).Slice<int8_t>(p).size());

  EXPECT_EQ(5, SL::Partial().Slice<1>(p).size());
  EXPECT_EQ(5, SL::Partial().Slice<int32_t>(p).size());
  EXPECT_EQ(5, SL::Partial(7).Slice<1>(p).size());
  EXPECT_EQ(5, SL::Partial(7).Slice<int32_t>(p).size());

  EXPECT_EQ(7, SL::Partial(7).Slice<2>(p).size());
  EXPECT_EQ(7, SL::Partial(7).Slice<Int128>(p).size());
}

TEST(Layout, SliceByIndexData) {
  alignas(max_align_t) const unsigned char p[100] = {0};
  {
    using L = Layout<int32_t>;
    EXPECT_EQ(
        0, Distance(
               p, Type<Span<const int32_t>>(L::Partial(0).Slice<0>(p)).data()));
    EXPECT_EQ(
        0, Distance(
               p, Type<Span<const int32_t>>(L::Partial(3).Slice<0>(p)).data()));
    EXPECT_EQ(0,
              Distance(p, Type<Span<const int32_t>>(L(3).Slice<0>(p)).data()));
  }
  {
    using L = Layout<int32_t, int32_t>;
    EXPECT_EQ(
        0, Distance(
               p, Type<Span<const int32_t>>(L::Partial(3).Slice<0>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(
            p, Type<Span<const int32_t>>(L::Partial(3, 5).Slice<0>(p)).data()));
    EXPECT_EQ(
        12,
        Distance(
            p, Type<Span<const int32_t>>(L::Partial(3, 5).Slice<1>(p)).data()));
    EXPECT_EQ(
        0, Distance(p, Type<Span<const int32_t>>(L(3, 5).Slice<0>(p)).data()));
    EXPECT_EQ(
        12, Distance(p, Type<Span<const int32_t>>(L(3, 5).Slice<1>(p)).data()));
  }
  {
    using L = Layout<int8_t, int32_t, Int128>;
    EXPECT_EQ(
        0, Distance(
               p, Type<Span<const int8_t>>(L::Partial(0).Slice<0>(p)).data()));
    EXPECT_EQ(
        0, Distance(
               p, Type<Span<const int8_t>>(L::Partial(1).Slice<0>(p)).data()));
    EXPECT_EQ(
        0, Distance(
               p, Type<Span<const int8_t>>(L::Partial(5).Slice<0>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(
            p, Type<Span<const int8_t>>(L::Partial(0, 0).Slice<0>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(
            p, Type<Span<const int32_t>>(L::Partial(0, 0).Slice<1>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(
            p, Type<Span<const int8_t>>(L::Partial(1, 0).Slice<0>(p)).data()));
    EXPECT_EQ(
        4,
        Distance(
            p, Type<Span<const int32_t>>(L::Partial(1, 0).Slice<1>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(
            p, Type<Span<const int8_t>>(L::Partial(5, 3).Slice<0>(p)).data()));
    EXPECT_EQ(
        8,
        Distance(
            p, Type<Span<const int32_t>>(L::Partial(5, 3).Slice<1>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(
            p,
            Type<Span<const int8_t>>(L::Partial(0, 0, 0).Slice<0>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(
            p,
            Type<Span<const int32_t>>(L::Partial(0, 0, 0).Slice<1>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(
            p,
            Type<Span<const Int128>>(L::Partial(0, 0, 0).Slice<2>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(
            p,
            Type<Span<const int8_t>>(L::Partial(1, 0, 0).Slice<0>(p)).data()));
    EXPECT_EQ(
        4,
        Distance(
            p,
            Type<Span<const int32_t>>(L::Partial(1, 0, 0).Slice<1>(p)).data()));
    EXPECT_EQ(
        8,
        Distance(
            p,
            Type<Span<const Int128>>(L::Partial(1, 0, 0).Slice<2>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(
            p,
            Type<Span<const int8_t>>(L::Partial(5, 3, 1).Slice<0>(p)).data()));
    EXPECT_EQ(
        24,
        Distance(
            p,
            Type<Span<const Int128>>(L::Partial(5, 3, 1).Slice<2>(p)).data()));
    EXPECT_EQ(
        8,
        Distance(
            p,
            Type<Span<const int32_t>>(L::Partial(5, 3, 1).Slice<1>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(p, Type<Span<const int8_t>>(L(5, 3, 1).Slice<0>(p)).data()));
    EXPECT_EQ(
        24,
        Distance(p, Type<Span<const Int128>>(L(5, 3, 1).Slice<2>(p)).data()));
    EXPECT_EQ(
        8,
        Distance(p, Type<Span<const int32_t>>(L(5, 3, 1).Slice<1>(p)).data()));
  }
}

TEST(Layout, SliceByTypeData) {
  alignas(max_align_t) const unsigned char p[100] = {0};
  {
    using L = Layout<int32_t>;
    EXPECT_EQ(
        0,
        Distance(
            p,
            Type<Span<const int32_t>>(L::Partial(0).Slice<int32_t>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(
            p,
            Type<Span<const int32_t>>(L::Partial(3).Slice<int32_t>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(p, Type<Span<const int32_t>>(L(3).Slice<int32_t>(p)).data()));
  }
  {
    using L = Layout<int8_t, int32_t, Int128>;
    EXPECT_EQ(
        0,
        Distance(
            p,
            Type<Span<const int8_t>>(L::Partial(0).Slice<int8_t>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(
            p,
            Type<Span<const int8_t>>(L::Partial(1).Slice<int8_t>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(
            p,
            Type<Span<const int8_t>>(L::Partial(5).Slice<int8_t>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(p, Type<Span<const int8_t>>(L::Partial(0, 0).Slice<int8_t>(p))
                        .data()));
    EXPECT_EQ(0, Distance(p, Type<Span<const int32_t>>(
                                 L::Partial(0, 0).Slice<int32_t>(p))
                                 .data()));
    EXPECT_EQ(
        0,
        Distance(p, Type<Span<const int8_t>>(L::Partial(1, 0).Slice<int8_t>(p))
                        .data()));
    EXPECT_EQ(4, Distance(p, Type<Span<const int32_t>>(
                                 L::Partial(1, 0).Slice<int32_t>(p))
                                 .data()));
    EXPECT_EQ(
        0,
        Distance(p, Type<Span<const int8_t>>(L::Partial(5, 3).Slice<int8_t>(p))
                        .data()));
    EXPECT_EQ(8, Distance(p, Type<Span<const int32_t>>(
                                 L::Partial(5, 3).Slice<int32_t>(p))
                                 .data()));
    EXPECT_EQ(0, Distance(p, Type<Span<const int8_t>>(
                                 L::Partial(0, 0, 0).Slice<int8_t>(p))
                                 .data()));
    EXPECT_EQ(0, Distance(p, Type<Span<const int32_t>>(
                                 L::Partial(0, 0, 0).Slice<int32_t>(p))
                                 .data()));
    EXPECT_EQ(0, Distance(p, Type<Span<const Int128>>(
                                 L::Partial(0, 0, 0).Slice<Int128>(p))
                                 .data()));
    EXPECT_EQ(0, Distance(p, Type<Span<const int8_t>>(
                                 L::Partial(1, 0, 0).Slice<int8_t>(p))
                                 .data()));
    EXPECT_EQ(4, Distance(p, Type<Span<const int32_t>>(
                                 L::Partial(1, 0, 0).Slice<int32_t>(p))
                                 .data()));
    EXPECT_EQ(8, Distance(p, Type<Span<const Int128>>(
                                 L::Partial(1, 0, 0).Slice<Int128>(p))
                                 .data()));
    EXPECT_EQ(0, Distance(p, Type<Span<const int8_t>>(
                                 L::Partial(5, 3, 1).Slice<int8_t>(p))
                                 .data()));
    EXPECT_EQ(24, Distance(p, Type<Span<const Int128>>(
                                  L::Partial(5, 3, 1).Slice<Int128>(p))
                                  .data()));
    EXPECT_EQ(8, Distance(p, Type<Span<const int32_t>>(
                                 L::Partial(5, 3, 1).Slice<int32_t>(p))
                                 .data()));
    EXPECT_EQ(
        0,
        Distance(p,
                 Type<Span<const int8_t>>(L(5, 3, 1).Slice<int8_t>(p)).data()));
    EXPECT_EQ(
        24,
        Distance(p,
                 Type<Span<const Int128>>(L(5, 3, 1).Slice<Int128>(p)).data()));
    EXPECT_EQ(
        8,
        Distance(
            p, Type<Span<const int32_t>>(L(5, 3, 1).Slice<int32_t>(p)).data()));
  }
}

TEST(Layout, MutableSliceByIndexData) {
  alignas(max_align_t) unsigned char p[100] = {0};
  {
    using L = Layout<int32_t>;
    EXPECT_EQ(
        0, Distance(p, Type<Span<int32_t>>(L::Partial(0).Slice<0>(p)).data()));
    EXPECT_EQ(
        0, Distance(p, Type<Span<int32_t>>(L::Partial(3).Slice<0>(p)).data()));
    EXPECT_EQ(0, Distance(p, Type<Span<int32_t>>(L(3).Slice<0>(p)).data()));
  }
  {
    using L = Layout<int32_t, int32_t>;
    EXPECT_EQ(
        0, Distance(p, Type<Span<int32_t>>(L::Partial(3).Slice<0>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(p, Type<Span<int32_t>>(L::Partial(3, 5).Slice<0>(p)).data()));
    EXPECT_EQ(
        12,
        Distance(p, Type<Span<int32_t>>(L::Partial(3, 5).Slice<1>(p)).data()));
    EXPECT_EQ(0, Distance(p, Type<Span<int32_t>>(L(3, 5).Slice<0>(p)).data()));
    EXPECT_EQ(12, Distance(p, Type<Span<int32_t>>(L(3, 5).Slice<1>(p)).data()));
  }
  {
    using L = Layout<int8_t, int32_t, Int128>;
    EXPECT_EQ(
        0, Distance(p, Type<Span<int8_t>>(L::Partial(0).Slice<0>(p)).data()));
    EXPECT_EQ(
        0, Distance(p, Type<Span<int8_t>>(L::Partial(1).Slice<0>(p)).data()));
    EXPECT_EQ(
        0, Distance(p, Type<Span<int8_t>>(L::Partial(5).Slice<0>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(p, Type<Span<int8_t>>(L::Partial(0, 0).Slice<0>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(p, Type<Span<int32_t>>(L::Partial(0, 0).Slice<1>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(p, Type<Span<int8_t>>(L::Partial(1, 0).Slice<0>(p)).data()));
    EXPECT_EQ(
        4,
        Distance(p, Type<Span<int32_t>>(L::Partial(1, 0).Slice<1>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(p, Type<Span<int8_t>>(L::Partial(5, 3).Slice<0>(p)).data()));
    EXPECT_EQ(
        8,
        Distance(p, Type<Span<int32_t>>(L::Partial(5, 3).Slice<1>(p)).data()));
    EXPECT_EQ(
        0, Distance(
               p, Type<Span<int8_t>>(L::Partial(0, 0, 0).Slice<0>(p)).data()));
    EXPECT_EQ(
        0, Distance(
               p, Type<Span<int32_t>>(L::Partial(0, 0, 0).Slice<1>(p)).data()));
    EXPECT_EQ(
        0, Distance(
               p, Type<Span<Int128>>(L::Partial(0, 0, 0).Slice<2>(p)).data()));
    EXPECT_EQ(
        0, Distance(
               p, Type<Span<int8_t>>(L::Partial(1, 0, 0).Slice<0>(p)).data()));
    EXPECT_EQ(
        4, Distance(
               p, Type<Span<int32_t>>(L::Partial(1, 0, 0).Slice<1>(p)).data()));
    EXPECT_EQ(
        8, Distance(
               p, Type<Span<Int128>>(L::Partial(1, 0, 0).Slice<2>(p)).data()));
    EXPECT_EQ(
        0, Distance(
               p, Type<Span<int8_t>>(L::Partial(5, 3, 1).Slice<0>(p)).data()));
    EXPECT_EQ(
        24, Distance(
                p, Type<Span<Int128>>(L::Partial(5, 3, 1).Slice<2>(p)).data()));
    EXPECT_EQ(
        8, Distance(
               p, Type<Span<int32_t>>(L::Partial(5, 3, 1).Slice<1>(p)).data()));
    EXPECT_EQ(0,
              Distance(p, Type<Span<int8_t>>(L(5, 3, 1).Slice<0>(p)).data()));
    EXPECT_EQ(24,
              Distance(p, Type<Span<Int128>>(L(5, 3, 1).Slice<2>(p)).data()));
    EXPECT_EQ(8,
              Distance(p, Type<Span<int32_t>>(L(5, 3, 1).Slice<1>(p)).data()));
  }
}

TEST(Layout, MutableSliceByTypeData) {
  alignas(max_align_t) unsigned char p[100] = {0};
  {
    using L = Layout<int32_t>;
    EXPECT_EQ(
        0, Distance(
               p, Type<Span<int32_t>>(L::Partial(0).Slice<int32_t>(p)).data()));
    EXPECT_EQ(
        0, Distance(
               p, Type<Span<int32_t>>(L::Partial(3).Slice<int32_t>(p)).data()));
    EXPECT_EQ(0,
              Distance(p, Type<Span<int32_t>>(L(3).Slice<int32_t>(p)).data()));
  }
  {
    using L = Layout<int8_t, int32_t, Int128>;
    EXPECT_EQ(
        0,
        Distance(p, Type<Span<int8_t>>(L::Partial(0).Slice<int8_t>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(p, Type<Span<int8_t>>(L::Partial(1).Slice<int8_t>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(p, Type<Span<int8_t>>(L::Partial(5).Slice<int8_t>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(p,
                 Type<Span<int8_t>>(L::Partial(0, 0).Slice<int8_t>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(
            p, Type<Span<int32_t>>(L::Partial(0, 0).Slice<int32_t>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(p,
                 Type<Span<int8_t>>(L::Partial(1, 0).Slice<int8_t>(p)).data()));
    EXPECT_EQ(
        4,
        Distance(
            p, Type<Span<int32_t>>(L::Partial(1, 0).Slice<int32_t>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(p,
                 Type<Span<int8_t>>(L::Partial(5, 3).Slice<int8_t>(p)).data()));
    EXPECT_EQ(
        8,
        Distance(
            p, Type<Span<int32_t>>(L::Partial(5, 3).Slice<int32_t>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(
            p,
            Type<Span<int8_t>>(L::Partial(0, 0, 0).Slice<int8_t>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(
            p,
            Type<Span<int32_t>>(L::Partial(0, 0, 0).Slice<int32_t>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(
            p,
            Type<Span<Int128>>(L::Partial(0, 0, 0).Slice<Int128>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(
            p,
            Type<Span<int8_t>>(L::Partial(1, 0, 0).Slice<int8_t>(p)).data()));
    EXPECT_EQ(
        4,
        Distance(
            p,
            Type<Span<int32_t>>(L::Partial(1, 0, 0).Slice<int32_t>(p)).data()));
    EXPECT_EQ(
        8,
        Distance(
            p,
            Type<Span<Int128>>(L::Partial(1, 0, 0).Slice<Int128>(p)).data()));
    EXPECT_EQ(
        0,
        Distance(
            p,
            Type<Span<int8_t>>(L::Partial(5, 3, 1).Slice<int8_t>(p)).data()));
    EXPECT_EQ(
        24,
        Distance(
            p,
            Type<Span<Int128>>(L::Partial(5, 3, 1).Slice<Int128>(p)).data()));
    EXPECT_EQ(
        8,
        Distance(
            p,
            Type<Span<int32_t>>(L::Partial(5, 3, 1).Slice<int32_t>(p)).data()));
    EXPECT_EQ(
        0, Distance(p, Type<Span<int8_t>>(L(5, 3, 1).Slice<int8_t>(p)).data()));
    EXPECT_EQ(
        24,
        Distance(p, Type<Span<Int128>>(L(5, 3, 1).Slice<Int128>(p)).data()));
    EXPECT_EQ(
        8,
        Distance(p, Type<Span<int32_t>>(L(5, 3, 1).Slice<int32_t>(p)).data()));
  }
}

TEST(Layout, StaticSliceData) {
  alignas(max_align_t) const unsigned char cp[100] = {0};
  alignas(max_align_t) unsigned char p[100] = {0};
  using L = Layout<int8_t, int32_t, Int128>;
  using SL = L::WithStaticSizes<3, 5>;

  EXPECT_EQ(0, Distance(cp, SL::Partial().Slice<0>(cp).data()));
  EXPECT_EQ(0, Distance(cp, SL::Partial().Slice<int8_t>(cp).data()));
  EXPECT_EQ(0, Distance(cp, SL::Partial(7).Slice<0>(cp).data()));
  EXPECT_EQ(0, Distance(cp, SL::Partial(7).Slice<int8_t>(cp).data()));

  EXPECT_EQ(4, Distance(cp, SL::Partial().Slice<1>(cp).data()));
  EXPECT_EQ(4, Distance(cp, SL::Partial().Slice<int32_t>(cp).data()));
  EXPECT_EQ(4, Distance(cp, SL::Partial(7).Slice<1>(cp).data()));
  EXPECT_EQ(4, Distance(cp, SL::Partial(7).Slice<int32_t>(cp).data()));

  EXPECT_EQ(24, Distance(cp, SL::Partial(7).Slice<2>(cp).data()));
  EXPECT_EQ(24, Distance(cp, SL::Partial(7).Slice<Int128>(cp).data()));

  EXPECT_EQ(0, Distance(p, SL::Partial().Slice<0>(p).data()));
  EXPECT_EQ(0, Distance(p, SL::Partial().Slice<int8_t>(p).data()));
  EXPECT_EQ(0, Distance(p, SL::Partial(7).Slice<0>(p).data()));
  EXPECT_EQ(0, Distance(p, SL::Partial(7).Slice<int8_t>(p).data()));

  EXPECT_EQ(4, Distance(p, SL::Partial().Slice<1>(p).data()));
  EXPECT_EQ(4, Distance(p, SL::Partial().Slice<int32_t>(p).data()));
  EXPECT_EQ(4, Distance(p, SL::Partial(7).Slice<1>(p).data()));
  EXPECT_EQ(4, Distance(p, SL::Partial(7).Slice<int32_t>(p).data()));

  EXPECT_EQ(24, Distance(p, SL::Partial(7).Slice<2>(p).data()));
  EXPECT_EQ(24, Distance(p, SL::Partial(7).Slice<Int128>(p).data()));
}

MATCHER_P(IsSameSlice, slice, "") {
  return arg.size() == slice.size() && arg.data() == slice.data();
}

template <typename... M>
class TupleMatcher {
 public:
  explicit TupleMatcher(M... matchers) : matchers_(std::move(matchers)...) {}

  template <typename Tuple>
  bool MatchAndExplain(const Tuple& p,
                       testing::MatchResultListener* /* listener */) const {
    static_assert(std::tuple_size<Tuple>::value == sizeof...(M), "");
    return MatchAndExplainImpl(
        p, absl::make_index_sequence<std::tuple_size<Tuple>::value>{});
  }

  // For the matcher concept. Left empty as we don't really need the diagnostics
  // right now.
  void DescribeTo(::std::ostream* os) const {}
  void DescribeNegationTo(::std::ostream* os) const {}

 private:
  template <typename Tuple, size_t... Is>
  bool MatchAndExplainImpl(const Tuple& p, absl::index_sequence<Is...>) const {
    // Using std::min as a simple variadic "and".
    return std::min(
        {true, testing::SafeMatcherCast<
                   const typename std::tuple_element<Is, Tuple>::type&>(
                   std::get<Is>(matchers_))
                   .Matches(std::get<Is>(p))...});
  }

  std::tuple<M...> matchers_;
};

template <typename... M>
testing::PolymorphicMatcher<TupleMatcher<M...>> Tuple(M... matchers) {
  return testing::MakePolymorphicMatcher(
      TupleMatcher<M...>(std::move(matchers)...));
}

TEST(Layout, Slices) {
  alignas(max_align_t) const unsigned char p[100] = {0};
  using L = Layout<int8_t, int8_t, Int128>;
  {
    const auto x = L::Partial();
    EXPECT_THAT(Type<std::tuple<>>(x.Slices(p)), Tuple());
  }
  {
    const auto x = L::Partial(1);
    EXPECT_THAT(Type<std::tuple<Span<const int8_t>>>(x.Slices(p)),
                Tuple(IsSameSlice(x.Slice<0>(p))));
  }
  {
    const auto x = L::Partial(1, 2);
    EXPECT_THAT(
        (Type<std::tuple<Span<const int8_t>, Span<const int8_t>>>(x.Slices(p))),
        Tuple(IsSameSlice(x.Slice<0>(p)), IsSameSlice(x.Slice<1>(p))));
  }
  {
    const auto x = L::Partial(1, 2, 3);
    EXPECT_THAT((Type<std::tuple<Span<const int8_t>, Span<const int8_t>,
                                 Span<const Int128>>>(x.Slices(p))),
                Tuple(IsSameSlice(x.Slice<0>(p)), IsSameSlice(x.Slice<1>(p)),
                      IsSameSlice(x.Slice<2>(p))));
  }
  {
    const L x(1, 2, 3);
    EXPECT_THAT((Type<std::tuple<Span<const int8_t>, Span<const int8_t>,
                                 Span<const Int128>>>(x.Slices(p))),
                Tuple(IsSameSlice(x.Slice<0>(p)), IsSameSlice(x.Slice<1>(p)),
                      IsSameSlice(x.Slice<2>(p))));
  }
}

TEST(Layout, MutableSlices) {
  alignas(max_align_t) unsigned char p[100] = {0};
  using L = Layout<int8_t, int8_t, Int128>;
  {
    const auto x = L::Partial();
    EXPECT_THAT(Type<std::tuple<>>(x.Slices(p)), Tuple());
  }
  {
    const auto x = L::Partial(1);
    EXPECT_THAT(Type<std::tuple<Span<int8_t>>>(x.Slices(p)),
                Tuple(IsSameSlice(x.Slice<0>(p))));
  }
  {
    const auto x = L::Partial(1, 2);
    EXPECT_THAT((Type<std::tuple<Span<int8_t>, Span<int8_t>>>(x.Slices(p))),
                Tuple(IsSameSlice(x.Slice<0>(p)), IsSameSlice(x.Slice<1>(p))));
  }
  {
    const auto x = L::Partial(1, 2, 3);
    EXPECT_THAT((Type<std::tuple<Span<int8_t>, Span<int8_t>, Span<Int128>>>(
                    x.Slices(p))),
                Tuple(IsSameSlice(x.Slice<0>(p)), IsSameSlice(x.Slice<1>(p)),
                      IsSameSlice(x.Slice<2>(p))));
  }
  {
    const L x(1, 2, 3);
    EXPECT_THAT((Type<std::tuple<Span<int8_t>, Span<int8_t>, Span<Int128>>>(
                    x.Slices(p))),
                Tuple(IsSameSlice(x.Slice<0>(p)), IsSameSlice(x.Slice<1>(p)),
                      IsSameSlice(x.Slice<2>(p))));
  }
}

TEST(Layout, StaticSlices) {
  alignas(max_align_t) const unsigned char cp[100] = {0};
  alignas(max_align_t) unsigned char p[100] = {0};
  using SL = Layout<int8_t, int8_t, Int128>::WithStaticSizes<1, 2>;
  {
    const auto x = SL::Partial();
    EXPECT_THAT(
        (Type<std::tuple<Span<const int8_t>, Span<const int8_t>>>(
            x.Slices(cp))),
        Tuple(IsSameSlice(x.Slice<0>(cp)), IsSameSlice(x.Slice<1>(cp))));
    EXPECT_THAT((Type<std::tuple<Span<int8_t>, Span<int8_t>>>(x.Slices(p))),
                Tuple(IsSameSlice(x.Slice<0>(p)), IsSameSlice(x.Slice<1>(p))));
  }
  {
    const auto x = SL::Partial(3);
    EXPECT_THAT((Type<std::tuple<Span<const int8_t>, Span<const int8_t>,
                                 Span<const Int128>>>(x.Slices(cp))),
                Tuple(IsSameSlice(x.Slice<0>(cp)), IsSameSlice(x.Slice<1>(cp)),
                      IsSameSlice(x.Slice<2>(cp))));
    EXPECT_THAT((Type<std::tuple<Span<int8_t>, Span<int8_t>, Span<Int128>>>(
                    x.Slices(p))),
                Tuple(IsSameSlice(x.Slice<0>(p)), IsSameSlice(x.Slice<1>(p)),
                      IsSameSlice(x.Slice<2>(p))));
  }
  {
    const SL x(3);
    EXPECT_THAT((Type<std::tuple<Span<const int8_t>, Span<const int8_t>,
                                 Span<const Int128>>>(x.Slices(cp))),
                Tuple(IsSameSlice(x.Slice<0>(cp)), IsSameSlice(x.Slice<1>(cp)),
                      IsSameSlice(x.Slice<2>(cp))));
    EXPECT_THAT((Type<std::tuple<Span<int8_t>, Span<int8_t>, Span<Int128>>>(
                    x.Slices(p))),
                Tuple(IsSameSlice(x.Slice<0>(p)), IsSameSlice(x.Slice<1>(p)),
                      IsSameSlice(x.Slice<2>(p))));
  }
}

TEST(Layout, UnalignedTypes) {
  constexpr Layout<unsigned char, unsigned char, unsigned char> x(1, 2, 3);
  alignas(max_align_t) unsigned char p[x.AllocSize() + 1];
  EXPECT_THAT(x.Pointers(p + 1), Tuple(p + 1, p + 2, p + 4));
}

TEST(Layout, CustomAlignment) {
  constexpr Layout<unsigned char, Aligned<unsigned char, 8>> x(1, 2);
  alignas(max_align_t) unsigned char p[x.AllocSize()];
  EXPECT_EQ(10, x.AllocSize());
  EXPECT_THAT(x.Pointers(p), Tuple(p + 0, p + 8));
}

TEST(Layout, OverAligned) {
  constexpr size_t M = alignof(max_align_t);
  constexpr Layout<unsigned char, Aligned<unsigned char, 2 * M>> x(1, 3);
#ifdef __GNUC__
  // Using __attribute__ ((aligned ())) instead of alignas to bypass a gcc bug:
  // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=89357
  __attribute__((aligned(2 * M))) unsigned char p[x.AllocSize()];
#else
  alignas(2 * M) unsigned char p[x.AllocSize()];
#endif
  EXPECT_EQ(2 * M + 3, x.AllocSize());
  EXPECT_THAT(x.Pointers(p), Tuple(p + 0, p + 2 * M));
}

TEST(Layout, Alignment) {
  static_assert(Layout<int8_t>::Alignment() == 1, "");
  static_assert(Layout<int32_t>::Alignment() == 4, "");
  static_assert(Layout<Int64>::Alignment() == 8, "");
  static_assert(Layout<Aligned<int8_t, 64>>::Alignment() == 64, "");
  static_assert(Layout<int8_t, int32_t, Int64>::Alignment() == 8, "");
  static_assert(Layout<int8_t, Int64, int32_t>::Alignment() == 8, "");
  static_assert(Layout<int32_t, int8_t, Int64>::Alignment() == 8, "");
  static_assert(Layout<int32_t, Int64, int8_t>::Alignment() == 8, "");
  static_assert(Layout<Int64, int8_t, int32_t>::Alignment() == 8, "");
  static_assert(Layout<Int64, int32_t, int8_t>::Alignment() == 8, "");
  static_assert(Layout<Int64, int32_t, int8_t>::Alignment() == 8, "");
  static_assert(
      Layout<Aligned<int8_t, 64>>::WithStaticSizes<>::Alignment() == 64, "");
  static_assert(
      Layout<Aligned<int8_t, 64>>::WithStaticSizes<2>::Alignment() == 64, "");
}

TEST(Layout, StaticAlignment) {
  static_assert(Layout<int8_t>::WithStaticSizes<>::Alignment() == 1, "");
  static_assert(Layout<int8_t>::WithStaticSizes<0>::Alignment() == 1, "");
  static_assert(Layout<int8_t>::WithStaticSizes<7>::Alignment() == 1, "");
  static_assert(Layout<int32_t>::WithStaticSizes<>::Alignment() == 4, "");
  static_assert(Layout<int32_t>::WithStaticSizes<0>::Alignment() == 4, "");
  static_assert(Layout<int32_t>::WithStaticSizes<3>::Alignment() == 4, "");
  static_assert(
      Layout<Aligned<int8_t, 64>>::WithStaticSizes<>::Alignment() == 64, "");
  static_assert(
      Layout<Aligned<int8_t, 64>>::WithStaticSizes<0>::Alignment() == 64, "");
  static_assert(
      Layout<Aligned<int8_t, 64>>::WithStaticSizes<2>::Alignment() == 64, "");
  static_assert(
      Layout<int32_t, Int64, int8_t>::WithStaticSizes<>::Alignment() == 8, "");
  static_assert(
      Layout<int32_t, Int64, int8_t>::WithStaticSizes<0, 0, 0>::Alignment() ==
          8,
      "");
  static_assert(
      Layout<int32_t, Int64, int8_t>::WithStaticSizes<1, 1, 1>::Alignment() ==
          8,
      "");
}

TEST(Layout, ConstexprPartial) {
  constexpr size_t M = alignof(max_align_t);
  constexpr Layout<unsigned char, Aligned<unsigned char, 2 * M>> x(1, 3);
  static_assert(x.Partial(1).template Offset<1>() == 2 * M, "");
}

TEST(Layout, StaticConstexpr) {
  constexpr size_t M = alignof(max_align_t);
  using L = Layout<unsigned char, Aligned<unsigned char, 2 * M>>;
  using SL = L::WithStaticSizes<1, 3>;
  constexpr SL x;
  static_assert(x.Offset<1>() == 2 * M, "");
}

// [from, to)
struct Region {
  size_t from;
  size_t to;
};

void ExpectRegionPoisoned(const unsigned char* p, size_t n, bool poisoned) {
#ifdef ABSL_HAVE_ADDRESS_SANITIZER
  for (size_t i = 0; i != n; ++i) {
    EXPECT_EQ(poisoned, __asan_address_is_poisoned(p + i));
  }
#endif
}

template <size_t N>
void ExpectPoisoned(const unsigned char (&buf)[N],
                    std::initializer_list<Region> reg) {
  size_t prev = 0;
  for (const Region& r : reg) {
    ExpectRegionPoisoned(buf + prev, r.from - prev, false);
    ExpectRegionPoisoned(buf + r.from, r.to - r.from, true);
    prev = r.to;
  }
  ExpectRegionPoisoned(buf + prev, N - prev, false);
}

TEST(Layout, PoisonPadding) {
  using L = Layout<int8_t, Int64, int32_t, Int128>;

  constexpr size_t n = L::Partial(1, 2, 3, 4).AllocSize();
  {
    constexpr auto x = L::Partial();
    alignas(max_align_t) const unsigned char c[n] = {};
    x.PoisonPadding(c);
    EXPECT_EQ(x.Slices(c), x.Slices(c));
    ExpectPoisoned(c, {});
  }
  {
    constexpr auto x = L::Partial(1);
    alignas(max_align_t) const unsigned char c[n] = {};
    x.PoisonPadding(c);
    EXPECT_EQ(x.Slices(c), x.Slices(c));
    ExpectPoisoned(c, {{1, 8}});
  }
  {
    constexpr auto x = L::Partial(1, 2);
    alignas(max_align_t) const unsigned char c[n] = {};
    x.PoisonPadding(c);
    EXPECT_EQ(x.Slices(c), x.Slices(c));
    ExpectPoisoned(c, {{1, 8}});
  }
  {
    constexpr auto x = L::Partial(1, 2, 3);
    alignas(max_align_t) const unsigned char c[n] = {};
    x.PoisonPadding(c);
    EXPECT_EQ(x.Slices(c), x.Slices(c));
    ExpectPoisoned(c, {{1, 8}, {36, 40}});
  }
  {
    constexpr auto x = L::Partial(1, 2, 3, 4);
    alignas(max_align_t) const unsigned char c[n] = {};
    x.PoisonPadding(c);
    EXPECT_EQ(x.Slices(c), x.Slices(c));
    ExpectPoisoned(c, {{1, 8}, {36, 40}});
  }
  {
    constexpr L x(1, 2, 3, 4);
    alignas(max_align_t) const unsigned char c[n] = {};
    x.PoisonPadding(c);
    EXPECT_EQ(x.Slices(c), x.Slices(c));
    ExpectPoisoned(c, {{1, 8}, {36, 40}});
  }
}

TEST(Layout, StaticPoisonPadding) {
  using L = Layout<int8_t, Int64, int32_t, Int128>;
  using SL = L::WithStaticSizes<1, 2>;

  constexpr size_t n = L::Partial(1, 2, 3, 4).AllocSize();
  {
    constexpr auto x = SL::Partial();
    alignas(max_align_t) const unsigned char c[n] = {};
    x.PoisonPadding(c);
    EXPECT_EQ(x.Slices(c), x.Slices(c));
    ExpectPoisoned(c, {{1, 8}});
  }
  {
    constexpr auto x = SL::Partial(3);
    alignas(max_align_t) const unsigned char c[n] = {};
    x.PoisonPadding(c);
    EXPECT_EQ(x.Slices(c), x.Slices(c));
    ExpectPoisoned(c, {{1, 8}, {36, 40}});
  }
  {
    constexpr auto x = SL::Partial(3, 4);
    alignas(max_align_t) const unsigned char c[n] = {};
    x.PoisonPadding(c);
    EXPECT_EQ(x.Slices(c), x.Slices(c));
    ExpectPoisoned(c, {{1, 8}, {36, 40}});
  }
  {
    constexpr SL x(3, 4);
    alignas(max_align_t) const unsigned char c[n] = {};
    x.PoisonPadding(c);
    EXPECT_EQ(x.Slices(c), x.Slices(c));
    ExpectPoisoned(c, {{1, 8}, {36, 40}});
  }
}

TEST(Layout, DebugString) {
  {
    constexpr auto x = Layout<int8_t, int32_t, int8_t, Int128>::Partial();
    EXPECT_EQ("@0<signed char>(1)", x.DebugString());
  }
  {
    constexpr auto x = Layout<int8_t, int32_t, int8_t, Int128>::Partial(1);
    EXPECT_EQ("@0<signed char>(1)[1]; @4<int>(4)", x.DebugString());
  }
  {
    constexpr auto x = Layout<int8_t, int32_t, int8_t, Int128>::Partial(1, 2);
    EXPECT_EQ("@0<signed char>(1)[1]; @4<int>(4)[2]; @12<signed char>(1)",
              x.DebugString());
  }
  {
    constexpr auto x =
        Layout<int8_t, int32_t, int8_t, Int128>::Partial(1, 2, 3);
    EXPECT_EQ(
        "@0<signed char>(1)[1]; @4<int>(4)[2]; @12<signed char>(1)[3]; "
        "@16" +
            Int128::Name() + "(16)",
        x.DebugString());
  }
  {
    constexpr auto x =
        Layout<int8_t, int32_t, int8_t, Int128>::Partial(1, 2, 3, 4);
    EXPECT_EQ(
        "@0<signed char>(1)[1]; @4<int>(4)[2]; @12<signed char>(1)[3]; "
        "@16" +
            Int128::Name() + "(16)[4]",
        x.DebugString());
  }
  {
    constexpr Layout<int8_t, int32_t, int8_t, Int128> x(1, 2, 3, 4);
    EXPECT_EQ(
        "@0<signed char>(1)[1]; @4<int>(4)[2]; @12<signed char>(1)[3]; "
        "@16" +
            Int128::Name() + "(16)[4]",
        x.DebugString());
  }
}

TEST(Layout, StaticDebugString) {
  {
    constexpr auto x =
        Layout<int8_t, int32_t, int8_t, Int128>::WithStaticSizes<>::Partial();
    EXPECT_EQ("@0<signed char>(1)", x.DebugString());
  }
  {
    constexpr auto x =
        Layout<int8_t, int32_t, int8_t, Int128>::WithStaticSizes<>::Partial(1);
    EXPECT_EQ("@0<signed char>(1)[1]; @4<int>(4)", x.DebugString());
  }
  {
    constexpr auto x =
        Layout<int8_t, int32_t, int8_t, Int128>::WithStaticSizes<1>::Partial();
    EXPECT_EQ("@0<signed char>(1)[1]; @4<int>(4)", x.DebugString());
  }
  {
    constexpr auto x =
        Layout<int8_t, int32_t, int8_t, Int128>::WithStaticSizes<>::Partial(1,
                                                                            2);
    EXPECT_EQ("@0<signed char>(1)[1]; @4<int>(4)[2]; @12<signed char>(1)",
              x.DebugString());
  }
  {
    constexpr auto x =
        Layout<int8_t, int32_t, int8_t, Int128>::WithStaticSizes<1>::Partial(2);
    EXPECT_EQ("@0<signed char>(1)[1]; @4<int>(4)[2]; @12<signed char>(1)",
              x.DebugString());
  }
  {
    constexpr auto x = Layout<int8_t, int32_t, int8_t,
                              Int128>::WithStaticSizes<1, 2>::Partial();
    EXPECT_EQ("@0<signed char>(1)[1]; @4<int>(4)[2]; @12<signed char>(1)",
              x.DebugString());
  }
  {
    constexpr auto x = Layout<int8_t, int32_t, int8_t,
                              Int128>::WithStaticSizes<1, 2, 3, 4>::Partial();
    EXPECT_EQ(
        "@0<signed char>(1)[1]; @4<int>(4)[2]; @12<signed char>(1)[3]; "
        "@16" +
            Int128::Name() + "(16)[4]",
        x.DebugString());
  }
  {
    constexpr Layout<int8_t, int32_t, int8_t, Int128>::WithStaticSizes<1, 2, 3,
                                                                       4>
        x;
    EXPECT_EQ(
        "@0<signed char>(1)[1]; @4<int>(4)[2]; @12<signed char>(1)[3]; "
        "@16" +
            Int128::Name() + "(16)[4]",
        x.DebugString());
  }
}

TEST(Layout, CharTypes) {
  constexpr Layout<int32_t> x(1);
  alignas(max_align_t) char c[x.AllocSize()] = {};
  alignas(max_align_t) unsigned char uc[x.AllocSize()] = {};
  alignas(max_align_t) signed char sc[x.AllocSize()] = {};
  alignas(max_align_t) const char cc[x.AllocSize()] = {};
  alignas(max_align_t) const unsigned char cuc[x.AllocSize()] = {};
  alignas(max_align_t) const signed char csc[x.AllocSize()] = {};

  Type<int32_t*>(x.Pointer<0>(c));
  Type<int32_t*>(x.Pointer<0>(uc));
  Type<int32_t*>(x.Pointer<0>(sc));
  Type<const int32_t*>(x.Pointer<0>(cc));
  Type<const int32_t*>(x.Pointer<0>(cuc));
  Type<const int32_t*>(x.Pointer<0>(csc));

  Type<int32_t*>(x.Pointer<int32_t>(c));
  Type<int32_t*>(x.Pointer<int32_t>(uc));
  Type<int32_t*>(x.Pointer<int32_t>(sc));
  Type<const int32_t*>(x.Pointer<int32_t>(cc));
  Type<const int32_t*>(x.Pointer<int32_t>(cuc));
  Type<const int32_t*>(x.Pointer<int32_t>(csc));

  Type<std::tuple<int32_t*>>(x.Pointers(c));
  Type<std::tuple<int32_t*>>(x.Pointers(uc));
  Type<std::tuple<int32_t*>>(x.Pointers(sc));
  Type<std::tuple<const int32_t*>>(x.Pointers(cc));
  Type<std::tuple<const int32_t*>>(x.Pointers(cuc));
  Type<std::tuple<const int32_t*>>(x.Pointers(csc));

  Type<Span<int32_t>>(x.Slice<0>(c));
  Type<Span<int32_t>>(x.Slice<0>(uc));
  Type<Span<int32_t>>(x.Slice<0>(sc));
  Type<Span<const int32_t>>(x.Slice<0>(cc));
  Type<Span<const int32_t>>(x.Slice<0>(cuc));
  Type<Span<const int32_t>>(x.Slice<0>(csc));

  Type<std::tuple<Span<int32_t>>>(x.Slices(c));
  Type<std::tuple<Span<int32_t>>>(x.Slices(uc));
  Type<std::tuple<Span<int32_t>>>(x.Slices(sc));
  Type<std::tuple<Span<const int32_t>>>(x.Slices(cc));
  Type<std::tuple<Span<const int32_t>>>(x.Slices(cuc));
  Type<std::tuple<Span<const int32_t>>>(x.Slices(csc));
}

TEST(Layout, ConstElementType) {
  constexpr Layout<const int32_t> x(1);
  alignas(int32_t) char c[x.AllocSize()] = {};
  const char* cc = c;
  const int32_t* p = reinterpret_cast<const int32_t*>(cc);

  EXPECT_EQ(alignof(int32_t), x.Alignment());

  EXPECT_EQ(0, x.Offset<0>());
  EXPECT_EQ(0, x.Offset<const int32_t>());

  EXPECT_THAT(x.Offsets(), ElementsAre(0));

  EXPECT_EQ(1, x.Size<0>());
  EXPECT_EQ(1, x.Size<const int32_t>());

  EXPECT_THAT(x.Sizes(), ElementsAre(1));

  EXPECT_EQ(sizeof(int32_t), x.AllocSize());

  EXPECT_EQ(p, Type<const int32_t*>(x.Pointer<0>(c)));
  EXPECT_EQ(p, Type<const int32_t*>(x.Pointer<0>(cc)));

  EXPECT_EQ(p, Type<const int32_t*>(x.Pointer<const int32_t>(c)));
  EXPECT_EQ(p, Type<const int32_t*>(x.Pointer<const int32_t>(cc)));

  EXPECT_THAT(Type<std::tuple<const int32_t*>>(x.Pointers(c)), Tuple(p));
  EXPECT_THAT(Type<std::tuple<const int32_t*>>(x.Pointers(cc)), Tuple(p));

  EXPECT_THAT(Type<Span<const int32_t>>(x.Slice<0>(c)),
              IsSameSlice(Span<const int32_t>(p, 1)));
  EXPECT_THAT(Type<Span<const int32_t>>(x.Slice<0>(cc)),
              IsSameSlice(Span<const int32_t>(p, 1)));

  EXPECT_THAT(Type<Span<const int32_t>>(x.Slice<const int32_t>(c)),
              IsSameSlice(Span<const int32_t>(p, 1)));
  EXPECT_THAT(Type<Span<const int32_t>>(x.Slice<const int32_t>(cc)),
              IsSameSlice(Span<const int32_t>(p, 1)));

  EXPECT_THAT(Type<std::tuple<Span<const int32_t>>>(x.Slices(c)),
              Tuple(IsSameSlice(Span<const int32_t>(p, 1))));
  EXPECT_THAT(Type<std::tuple<Span<const int32_t>>>(x.Slices(cc)),
              Tuple(IsSameSlice(Span<const int32_t>(p, 1))));
}

namespace example {

// Immutable move-only string with sizeof equal to sizeof(void*). The string
// size and the characters are kept in the same heap allocation.
class CompactString {
 public:
  CompactString(const char* s = "") {  // NOLINT
    const size_t size = strlen(s);
    // size_t[1], followed by char[size + 1].
    // This statement doesn't allocate memory.
    const L layout(1, size + 1);
    // AllocSize() tells us how much memory we need to allocate for all our
    // data.
    p_.reset(new unsigned char[layout.AllocSize()]);
    // If running under ASAN, mark the padding bytes, if any, to catch memory
    // errors.
    layout.PoisonPadding(p_.get());
    // Store the size in the allocation.
    // Pointer<size_t>() is a synonym for Pointer<0>().
    *layout.Pointer<size_t>(p_.get()) = size;
    // Store the characters in the allocation.
    memcpy(layout.Pointer<char>(p_.get()), s, size + 1);
  }

  size_t size() const {
    // Equivalent to reinterpret_cast<size_t&>(*p).
    return *L::Partial().Pointer<size_t>(p_.get());
  }

  const char* c_str() const {
    // Equivalent to reinterpret_cast<char*>(p.get() + sizeof(size_t)).
    // The argument in Partial(1) specifies that we have size_t[1] in front of
    // the characters.
    return L::Partial(1).Pointer<char>(p_.get());
  }

 private:
  // Our heap allocation contains a size_t followed by an array of chars.
  using L = Layout<size_t, char>;
  std::unique_ptr<unsigned char[]> p_;
};

TEST(CompactString, Works) {
  CompactString s = "hello";
  EXPECT_EQ(5, s.size());
  EXPECT_STREQ("hello", s.c_str());
}

// Same as the previous CompactString example, except we set the first array
// size to 1 statically, since we know it is always 1. This allows us to compute
// the offset of the character array at compile time.
class StaticCompactString {
 public:
  StaticCompactString(const char* s = "") {  // NOLINT
    const size_t size = strlen(s);
    const SL layout(size + 1);
    p_.reset(new unsigned char[layout.AllocSize()]);
    layout.PoisonPadding(p_.get());
    *layout.Pointer<size_t>(p_.get()) = size;
    memcpy(layout.Pointer<char>(p_.get()), s, size + 1);
  }

  size_t size() const { return *SL::Partial().Pointer<size_t>(p_.get()); }

  const char* c_str() const { return SL::Partial().Pointer<char>(p_.get()); }

 private:
  using SL = Layout<size_t, char>::WithStaticSizes<1>;
  std::unique_ptr<unsigned char[]> p_;
};

TEST(StaticCompactString, Works) {
  StaticCompactString s = "hello";
  EXPECT_EQ(5, s.size());
  EXPECT_STREQ("hello", s.c_str());
}

}  // namespace example

}  // namespace
}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl
