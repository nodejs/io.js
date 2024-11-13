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

#include "absl/container/internal/compressed_tuple.h"

#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/internal/test_instance_tracker.h"
#include "absl/memory/memory.h"
#include "absl/types/any.h"
#include "absl/types/optional.h"
#include "absl/utility/utility.h"

// These are declared at global scope purely so that error messages
// are smaller and easier to understand.
enum class CallType { kMutableRef, kConstRef, kMutableMove, kConstMove };

template <int>
struct Empty {
  constexpr CallType value() & { return CallType::kMutableRef; }
  constexpr CallType value() const& { return CallType::kConstRef; }
  constexpr CallType value() && { return CallType::kMutableMove; }
  constexpr CallType value() const&& { return CallType::kConstMove; }
};

// Unconditionally return an lvalue reference to `t`.
template <typename T>
constexpr T& AsLValue(T&& t) {
  return t;
}

template <typename T>
struct NotEmpty {
  T value;
};

template <typename T, typename U>
struct TwoValues {
  T value1;
  U value2;
};


namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {
namespace {

using absl::test_internal::CopyableMovableInstance;
using absl::test_internal::InstanceTracker;
using ::testing::Each;

TEST(CompressedTupleTest, Sizeof) {
  EXPECT_EQ(sizeof(int), sizeof(CompressedTuple<int>));
  EXPECT_EQ(sizeof(int), sizeof(CompressedTuple<int, Empty<0>>));
  EXPECT_EQ(sizeof(int), sizeof(CompressedTuple<int, Empty<0>, Empty<1>>));
  EXPECT_EQ(sizeof(int),
            sizeof(CompressedTuple<int, Empty<0>, Empty<1>, Empty<2>>));

  EXPECT_EQ(sizeof(TwoValues<int, double>),
            sizeof(CompressedTuple<int, NotEmpty<double>>));
  EXPECT_EQ(sizeof(TwoValues<int, double>),
            sizeof(CompressedTuple<int, Empty<0>, NotEmpty<double>>));
  EXPECT_EQ(sizeof(TwoValues<int, double>),
            sizeof(CompressedTuple<int, Empty<0>, NotEmpty<double>, Empty<1>>));
}

TEST(CompressedTupleTest, PointerToEmpty) {
  auto to_void_ptrs = [](const auto&... objs) {
    return std::vector<const void*>{static_cast<const void*>(&objs)...};
  };
  {
    using Tuple = CompressedTuple<int, Empty<0>>;
    EXPECT_EQ(sizeof(int), sizeof(Tuple));
    Tuple t;
    EXPECT_THAT(to_void_ptrs(t.get<1>()), Each(&t));
  }
  {
    using Tuple = CompressedTuple<int, Empty<0>, Empty<1>>;
    EXPECT_EQ(sizeof(int), sizeof(Tuple));
    Tuple t;
    EXPECT_THAT(to_void_ptrs(t.get<1>(), t.get<2>()), Each(&t));
  }
  {
    using Tuple = CompressedTuple<int, Empty<0>, Empty<1>, Empty<2>>;
    EXPECT_EQ(sizeof(int), sizeof(Tuple));
    Tuple t;
    EXPECT_THAT(to_void_ptrs(t.get<1>(), t.get<2>(), t.get<3>()), Each(&t));
  }
}

TEST(CompressedTupleTest, OneMoveOnRValueConstructionTemp) {
  InstanceTracker tracker;
  CompressedTuple<CopyableMovableInstance> x1(CopyableMovableInstance(1));
  EXPECT_EQ(tracker.instances(), 1);
  EXPECT_EQ(tracker.copies(), 0);
  EXPECT_LE(tracker.moves(), 1);
  EXPECT_EQ(x1.get<0>().value(), 1);
}

TEST(CompressedTupleTest, OneMoveOnRValueConstructionMove) {
  InstanceTracker tracker;

  CopyableMovableInstance i1(1);
  CompressedTuple<CopyableMovableInstance> x1(std::move(i1));
  EXPECT_EQ(tracker.instances(), 2);
  EXPECT_EQ(tracker.copies(), 0);
  EXPECT_LE(tracker.moves(), 1);
  EXPECT_EQ(x1.get<0>().value(), 1);
}

TEST(CompressedTupleTest, OneMoveOnRValueConstructionMixedTypes) {
  InstanceTracker tracker;
  CopyableMovableInstance i1(1);
  CopyableMovableInstance i2(2);
  Empty<0> empty;
  CompressedTuple<CopyableMovableInstance, CopyableMovableInstance&, Empty<0>>
      x1(std::move(i1), i2, empty);
  EXPECT_EQ(x1.get<0>().value(), 1);
  EXPECT_EQ(x1.get<1>().value(), 2);
  EXPECT_EQ(tracker.copies(), 0);
  EXPECT_EQ(tracker.moves(), 1);
}

struct IncompleteType;
CompressedTuple<CopyableMovableInstance, IncompleteType&, Empty<0>>
MakeWithIncomplete(CopyableMovableInstance i1,
                   IncompleteType& t,  // NOLINT
                   Empty<0> empty) {
  return CompressedTuple<CopyableMovableInstance, IncompleteType&, Empty<0>>{
      std::move(i1), t, empty};
}

struct IncompleteType {};
TEST(CompressedTupleTest, OneMoveOnRValueConstructionWithIncompleteType) {
  InstanceTracker tracker;
  CopyableMovableInstance i1(1);
  Empty<0> empty;
  struct DerivedType : IncompleteType {int value = 0;};
  DerivedType fd;
  fd.value = 7;

  CompressedTuple<CopyableMovableInstance, IncompleteType&, Empty<0>> x1 =
      MakeWithIncomplete(std::move(i1), fd, empty);

  EXPECT_EQ(x1.get<0>().value(), 1);
  EXPECT_EQ(static_cast<DerivedType&>(x1.get<1>()).value, 7);

  EXPECT_EQ(tracker.copies(), 0);
  EXPECT_EQ(tracker.moves(), 2);
}

TEST(CompressedTupleTest,
     OneMoveOnRValueConstructionMixedTypes_BraceInitPoisonPillExpected) {
  InstanceTracker tracker;
  CopyableMovableInstance i1(1);
  CopyableMovableInstance i2(2);
  CompressedTuple<CopyableMovableInstance, CopyableMovableInstance&, Empty<0>>
      x1(std::move(i1), i2, {});  // NOLINT
  EXPECT_EQ(x1.get<0>().value(), 1);
  EXPECT_EQ(x1.get<1>().value(), 2);
  EXPECT_EQ(tracker.instances(), 3);
  // We are forced into the `const Ts&...` constructor (invoking copies)
  // because we need it to deduce the type of `{}`.
  // std::tuple also has this behavior.
  // Note, this test is proof that this is expected behavior, but it is not
  // _desired_ behavior.
  EXPECT_EQ(tracker.copies(), 1);
  EXPECT_EQ(tracker.moves(), 0);
}

TEST(CompressedTupleTest, OneCopyOnLValueConstruction) {
  InstanceTracker tracker;
  CopyableMovableInstance i1(1);

  CompressedTuple<CopyableMovableInstance> x1(i1);
  EXPECT_EQ(tracker.copies(), 1);
  EXPECT_EQ(tracker.moves(), 0);

  tracker.ResetCopiesMovesSwaps();

  CopyableMovableInstance i2(2);
  const CopyableMovableInstance& i2_ref = i2;
  CompressedTuple<CopyableMovableInstance> x2(i2_ref);
  EXPECT_EQ(tracker.copies(), 1);
  EXPECT_EQ(tracker.moves(), 0);
}

TEST(CompressedTupleTest, OneMoveOnRValueAccess) {
  InstanceTracker tracker;
  CopyableMovableInstance i1(1);
  CompressedTuple<CopyableMovableInstance> x(std::move(i1));
  tracker.ResetCopiesMovesSwaps();

  CopyableMovableInstance i2 = std::move(x).get<0>();
  EXPECT_EQ(tracker.copies(), 0);
  EXPECT_EQ(tracker.moves(), 1);
}

TEST(CompressedTupleTest, OneCopyOnLValueAccess) {
  InstanceTracker tracker;

  CompressedTuple<CopyableMovableInstance> x(CopyableMovableInstance(0));
  EXPECT_EQ(tracker.copies(), 0);
  EXPECT_EQ(tracker.moves(), 1);

  CopyableMovableInstance t = x.get<0>();
  EXPECT_EQ(tracker.copies(), 1);
  EXPECT_EQ(tracker.moves(), 1);
}

TEST(CompressedTupleTest, ZeroCopyOnRefAccess) {
  InstanceTracker tracker;

  CompressedTuple<CopyableMovableInstance> x(CopyableMovableInstance(0));
  EXPECT_EQ(tracker.copies(), 0);
  EXPECT_EQ(tracker.moves(), 1);

  CopyableMovableInstance& t1 = x.get<0>();
  const CopyableMovableInstance& t2 = x.get<0>();
  EXPECT_EQ(tracker.copies(), 0);
  EXPECT_EQ(tracker.moves(), 1);
  EXPECT_EQ(t1.value(), 0);
  EXPECT_EQ(t2.value(), 0);
}

TEST(CompressedTupleTest, Access) {
  struct S {
    std::string x;
  };
  CompressedTuple<int, Empty<0>, S> x(7, {}, S{"ABC"});
  EXPECT_EQ(sizeof(x), sizeof(TwoValues<int, S>));
  EXPECT_EQ(7, x.get<0>());
  EXPECT_EQ("ABC", x.get<2>().x);
}

TEST(CompressedTupleTest, NonClasses) {
  CompressedTuple<int, const char*> x(7, "ABC");
  EXPECT_EQ(7, x.get<0>());
  EXPECT_STREQ("ABC", x.get<1>());
}

TEST(CompressedTupleTest, MixClassAndNonClass) {
  CompressedTuple<int, const char*, Empty<0>, NotEmpty<double>> x(7, "ABC", {},
                                                                  {1.25});
  struct Mock {
    int v;
    const char* p;
    double d;
  };
  EXPECT_EQ(sizeof(x), sizeof(Mock));
  EXPECT_EQ(7, x.get<0>());
  EXPECT_STREQ("ABC", x.get<1>());
  EXPECT_EQ(1.25, x.get<3>().value);
}

TEST(CompressedTupleTest, Nested) {
  CompressedTuple<int, CompressedTuple<int>,
                  CompressedTuple<int, CompressedTuple<int>>>
      x(1, CompressedTuple<int>(2),
        CompressedTuple<int, CompressedTuple<int>>(3, CompressedTuple<int>(4)));
  EXPECT_EQ(1, x.get<0>());
  EXPECT_EQ(2, x.get<1>().get<0>());
  EXPECT_EQ(3, x.get<2>().get<0>());
  EXPECT_EQ(4, x.get<2>().get<1>().get<0>());

  CompressedTuple<Empty<0>, Empty<0>,
                  CompressedTuple<Empty<0>, CompressedTuple<Empty<0>>>>
      y;
  std::set<Empty<0>*> empties{&y.get<0>(), &y.get<1>(), &y.get<2>().get<0>(),
                              &y.get<2>().get<1>().get<0>()};
#ifdef _MSC_VER
  // MSVC has a bug where many instances of the same base class are layed out in
  // the same address when using __declspec(empty_bases).
  // This will be fixed in a future version of MSVC.
  int expected = 1;
#else
  int expected = 4;
#endif
  EXPECT_EQ(expected, sizeof(y));
  EXPECT_EQ(expected, empties.size());
  EXPECT_EQ(sizeof(y), sizeof(Empty<0>) * empties.size());

  EXPECT_EQ(4 * sizeof(char),
            sizeof(CompressedTuple<CompressedTuple<char, char>,
                                   CompressedTuple<char, char>>));
  EXPECT_TRUE((std::is_empty<CompressedTuple<Empty<0>, Empty<1>>>::value));

  // Make sure everything still works when things are nested.
  struct CT_Empty : CompressedTuple<Empty<0>> {};
  CompressedTuple<Empty<0>, CT_Empty> nested_empty;
  auto contained = nested_empty.get<0>();
  auto nested = nested_empty.get<1>().get<0>();
  EXPECT_TRUE((std::is_same<decltype(contained), decltype(nested)>::value));
}

TEST(CompressedTupleTest, Reference) {
  int i = 7;
  std::string s = "Very long string that goes in the heap";
  CompressedTuple<int, int&, std::string, std::string&> x(i, i, s, s);

  // Sanity check. We should have not moved from `s`
  EXPECT_EQ(s, "Very long string that goes in the heap");

  EXPECT_EQ(x.get<0>(), x.get<1>());
  EXPECT_NE(&x.get<0>(), &x.get<1>());
  EXPECT_EQ(&x.get<1>(), &i);

  EXPECT_EQ(x.get<2>(), x.get<3>());
  EXPECT_NE(&x.get<2>(), &x.get<3>());
  EXPECT_EQ(&x.get<3>(), &s);
}

TEST(CompressedTupleTest, NoElements) {
  CompressedTuple<> x;
  static_cast<void>(x);  // Silence -Wunused-variable.
  EXPECT_TRUE(std::is_empty<CompressedTuple<>>::value);
}

TEST(CompressedTupleTest, MoveOnlyElements) {
  CompressedTuple<std::unique_ptr<std::string>> str_tup(
      absl::make_unique<std::string>("str"));

  CompressedTuple<CompressedTuple<std::unique_ptr<std::string>>,
                  std::unique_ptr<int>>
  x(std::move(str_tup), absl::make_unique<int>(5));

  EXPECT_EQ(*x.get<0>().get<0>(), "str");
  EXPECT_EQ(*x.get<1>(), 5);

  std::unique_ptr<std::string> x0 = std::move(x.get<0>()).get<0>();
  std::unique_ptr<int> x1 = std::move(x).get<1>();

  EXPECT_EQ(*x0, "str");
  EXPECT_EQ(*x1, 5);
}

TEST(CompressedTupleTest, MoveConstructionMoveOnlyElements) {
  CompressedTuple<std::unique_ptr<std::string>> base(
      absl::make_unique<std::string>("str"));
  EXPECT_EQ(*base.get<0>(), "str");

  CompressedTuple<std::unique_ptr<std::string>> copy(std::move(base));
  EXPECT_EQ(*copy.get<0>(), "str");
}

TEST(CompressedTupleTest, AnyElements) {
  any a(std::string("str"));
  CompressedTuple<any, any&> x(any(5), a);
  EXPECT_EQ(absl::any_cast<int>(x.get<0>()), 5);
  EXPECT_EQ(absl::any_cast<std::string>(x.get<1>()), "str");

  a = 0.5f;
  EXPECT_EQ(absl::any_cast<float>(x.get<1>()), 0.5);
}

TEST(CompressedTupleTest, Constexpr) {
  struct NonTrivialStruct {
    constexpr NonTrivialStruct() = default;
    constexpr int value() const { return v; }
    int v = 5;
  };
  struct TrivialStruct {
    TrivialStruct() = default;
    constexpr int value() const { return v; }
    int v;
  };

  using Tuple = CompressedTuple<int, double, CompressedTuple<int>, Empty<0>>;

  constexpr int r0 =
      AsLValue(Tuple(1, 0.75, CompressedTuple<int>(9), {})).get<0>();
  constexpr double r1 =
      AsLValue(Tuple(1, 0.75, CompressedTuple<int>(9), {})).get<1>();
  constexpr int r2 =
      AsLValue(Tuple(1, 0.75, CompressedTuple<int>(9), {})).get<2>().get<0>();
  constexpr CallType r3 =
      AsLValue(Tuple(1, 0.75, CompressedTuple<int>(9), {})).get<3>().value();

  EXPECT_EQ(r0, 1);
  EXPECT_EQ(r1, 0.75);
  EXPECT_EQ(r2, 9);
  EXPECT_EQ(r3, CallType::kMutableRef);

  constexpr Tuple x(7, 1.25, CompressedTuple<int>(5), {});
  constexpr int x0 = x.get<0>();
  constexpr double x1 = x.get<1>();
  constexpr int x2 = x.get<2>().get<0>();
  constexpr CallType x3 = x.get<3>().value();

  EXPECT_EQ(x0, 7);
  EXPECT_EQ(x1, 1.25);
  EXPECT_EQ(x2, 5);
  EXPECT_EQ(x3, CallType::kConstRef);

  constexpr int m0 = Tuple(5, 0.25, CompressedTuple<int>(3), {}).get<0>();
  constexpr double m1 = Tuple(5, 0.25, CompressedTuple<int>(3), {}).get<1>();
  constexpr int m2 =
      Tuple(5, 0.25, CompressedTuple<int>(3), {}).get<2>().get<0>();
  constexpr CallType m3 =
      Tuple(5, 0.25, CompressedTuple<int>(3), {}).get<3>().value();

  EXPECT_EQ(m0, 5);
  EXPECT_EQ(m1, 0.25);
  EXPECT_EQ(m2, 3);
  EXPECT_EQ(m3, CallType::kMutableMove);

  constexpr CompressedTuple<Empty<0>, TrivialStruct, int> trivial = {};
  constexpr CallType trivial0 = trivial.get<0>().value();
  constexpr int trivial1 = trivial.get<1>().value();
  constexpr int trivial2 = trivial.get<2>();

  EXPECT_EQ(trivial0, CallType::kConstRef);
  EXPECT_EQ(trivial1, 0);
  EXPECT_EQ(trivial2, 0);

  constexpr CompressedTuple<Empty<0>, NonTrivialStruct, absl::optional<int>>
      non_trivial = {};
  constexpr CallType non_trivial0 = non_trivial.get<0>().value();
  constexpr int non_trivial1 = non_trivial.get<1>().value();
  constexpr absl::optional<int> non_trivial2 = non_trivial.get<2>();

  EXPECT_EQ(non_trivial0, CallType::kConstRef);
  EXPECT_EQ(non_trivial1, 5);
  EXPECT_EQ(non_trivial2, absl::nullopt);

  static constexpr char data[] = "DEF";
  constexpr CompressedTuple<const char*> z(data);
  constexpr const char* z1 = z.get<0>();
  EXPECT_EQ(std::string(z1), std::string(data));

#if defined(__clang__)
  // An apparent bug in earlier versions of gcc claims these are ambiguous.
  constexpr int x2m = std::move(x.get<2>()).get<0>();
  constexpr CallType x3m = std::move(x).get<3>().value();
  EXPECT_EQ(x2m, 5);
  EXPECT_EQ(x3m, CallType::kConstMove);
#endif
}

#if defined(__clang__) || defined(__GNUC__)
TEST(CompressedTupleTest, EmptyFinalClass) {
  struct S final {
    int f() const { return 5; }
  };
  CompressedTuple<S> x;
  EXPECT_EQ(x.get<0>().f(), 5);
}
#endif

// TODO(b/214288561): enable this test.
TEST(CompressedTupleTest, DISABLED_NestedEbo) {
  struct Empty1 {};
  struct Empty2 {};
  CompressedTuple<Empty1, CompressedTuple<Empty2>, int> x;
  CompressedTuple<Empty1, Empty2, int> y;
  // Currently fails with sizeof(x) == 8, sizeof(y) == 4.
  EXPECT_EQ(sizeof(x), sizeof(y));
}

}  // namespace
}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl
