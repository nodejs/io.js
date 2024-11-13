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

#include "absl/hash/hash.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <ios>
#include <limits>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/container/flat_hash_set.h"
#include "absl/hash/hash_testing.h"
#include "absl/hash/internal/hash_test.h"
#include "absl/hash/internal/spy_hash_state.h"
#include "absl/memory/memory.h"
#include "absl/meta/type_traits.h"
#include "absl/strings/cord_test_helpers.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"

#ifdef ABSL_INTERNAL_STD_FILESYSTEM_PATH_HASH_AVAILABLE
#include <filesystem>  // NOLINT
#endif

#ifdef ABSL_HAVE_STD_STRING_VIEW
#include <string_view>
#endif

namespace {

using ::absl::hash_test_internal::is_hashable;
using ::absl::hash_test_internal::TypeErasedContainer;
using ::absl::hash_test_internal::TypeErasedValue;

template <typename T>
using TypeErasedVector = TypeErasedContainer<std::vector<T>>;

using absl::Hash;
using absl::hash_internal::SpyHashState;

template <typename T>
class HashValueIntTest : public testing::Test {
};
TYPED_TEST_SUITE_P(HashValueIntTest);

template <typename T>
SpyHashState SpyHash(const T& value) {
  return SpyHashState::combine(SpyHashState(), value);
}

TYPED_TEST_P(HashValueIntTest, BasicUsage) {
  EXPECT_TRUE((is_hashable<TypeParam>::value));

  TypeParam n = 42;
  EXPECT_EQ(SpyHash(n), SpyHash(TypeParam{42}));
  EXPECT_NE(SpyHash(n), SpyHash(TypeParam{0}));
  EXPECT_NE(SpyHash(std::numeric_limits<TypeParam>::max()),
            SpyHash(std::numeric_limits<TypeParam>::min()));
}

TYPED_TEST_P(HashValueIntTest, FastPath) {
  // Test the fast-path to make sure the values are the same.
  TypeParam n = 42;
  EXPECT_EQ(absl::Hash<TypeParam>{}(n),
            absl::Hash<std::tuple<TypeParam>>{}(std::tuple<TypeParam>(n)));
}

REGISTER_TYPED_TEST_SUITE_P(HashValueIntTest, BasicUsage, FastPath);
using IntTypes = testing::Types<unsigned char, char, int, int32_t, int64_t,
                                uint32_t, uint64_t, size_t>;
INSTANTIATE_TYPED_TEST_SUITE_P(My, HashValueIntTest, IntTypes);

enum LegacyEnum { kValue1, kValue2, kValue3 };

enum class EnumClass { kValue4, kValue5, kValue6 };

TEST(HashValueTest, EnumAndBool) {
  EXPECT_TRUE((is_hashable<LegacyEnum>::value));
  EXPECT_TRUE((is_hashable<EnumClass>::value));
  EXPECT_TRUE((is_hashable<bool>::value));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(std::make_tuple(
      LegacyEnum::kValue1, LegacyEnum::kValue2, LegacyEnum::kValue3)));
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(std::make_tuple(
      EnumClass::kValue4, EnumClass::kValue5, EnumClass::kValue6)));
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      std::make_tuple(true, false)));
}

TEST(HashValueTest, FloatingPoint) {
  EXPECT_TRUE((is_hashable<float>::value));
  EXPECT_TRUE((is_hashable<double>::value));
  EXPECT_TRUE((is_hashable<long double>::value));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      std::make_tuple(42.f, 0.f, -0.f, std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity())));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      std::make_tuple(42., 0., -0., std::numeric_limits<double>::infinity(),
                      -std::numeric_limits<double>::infinity())));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(std::make_tuple(
      // Add some values with small exponent to test that NORMAL values also
      // append their category.
      .5L, 1.L, 2.L, 4.L, 42.L, 0.L, -0.L,
      17 * static_cast<long double>(std::numeric_limits<double>::max()),
      std::numeric_limits<long double>::infinity(),
      -std::numeric_limits<long double>::infinity())));
}

TEST(HashValueTest, Pointer) {
  EXPECT_TRUE((is_hashable<int*>::value));
  EXPECT_TRUE((is_hashable<int(*)(char, float)>::value));
  EXPECT_TRUE((is_hashable<void(*)(int, int, ...)>::value));

  int i;
  int* ptr = &i;
  int* n = nullptr;

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      std::make_tuple(&i, ptr, nullptr, ptr + 1, n)));
}

TEST(HashValueTest, PointerAlignment) {
  // We want to make sure that pointer alignment will not cause bits to be
  // stuck.

  constexpr size_t kTotalSize = 1 << 20;
  std::unique_ptr<char[]> data(new char[kTotalSize]);
  constexpr size_t kLog2NumValues = 5;
  constexpr size_t kNumValues = 1 << kLog2NumValues;

  for (size_t align = 1; align < kTotalSize / kNumValues;
       align < 8 ? align += 1 : align < 1024 ? align += 8 : align += 32) {
    SCOPED_TRACE(align);
    ASSERT_LE(align * kNumValues, kTotalSize);

    size_t bits_or = 0;
    size_t bits_and = ~size_t{};

    for (size_t i = 0; i < kNumValues; ++i) {
      size_t hash = absl::Hash<void*>()(data.get() + i * align);
      bits_or |= hash;
      bits_and &= hash;
    }

    // Limit the scope to the bits we would be using for Swisstable.
    constexpr size_t kMask = (1 << (kLog2NumValues + 7)) - 1;
    size_t stuck_bits = (~bits_or | bits_and) & kMask;
    EXPECT_EQ(stuck_bits, 0u) << "0x" << std::hex << stuck_bits;
  }
}

TEST(HashValueTest, PointerToMember) {
  struct Bass {
    void q() {}
  };

  struct A : Bass {
    virtual ~A() = default;
    virtual void vfa() {}

    static auto pq() -> void (A::*)() { return &A::q; }
  };

  struct B : Bass {
    virtual ~B() = default;
    virtual void vfb() {}

    static auto pq() -> void (B::*)() { return &B::q; }
  };

  struct Foo : A, B {
    void f1() {}
    void f2() const {}

    int g1() & { return 0; }
    int g2() const & { return 0; }
    int g3() && { return 0; }
    int g4() const && { return 0; }

    int h1() & { return 0; }
    int h2() const & { return 0; }
    int h3() && { return 0; }
    int h4() const && { return 0; }

    int a;
    int b;

    const int c = 11;
    const int d = 22;
  };

  EXPECT_TRUE((is_hashable<float Foo::*>::value));
  EXPECT_TRUE((is_hashable<double (Foo::*)(int, int)&&>::value));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      std::make_tuple(&Foo::a, &Foo::b, static_cast<int Foo::*>(nullptr))));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      std::make_tuple(&Foo::c, &Foo::d, static_cast<const int Foo::*>(nullptr),
                      &Foo::a, &Foo::b)));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(std::make_tuple(
      &Foo::f1, static_cast<void (Foo::*)()>(nullptr))));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(std::make_tuple(
      &Foo::f2, static_cast<void (Foo::*)() const>(nullptr))));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(std::make_tuple(
      &Foo::g1, &Foo::h1, static_cast<int (Foo::*)() &>(nullptr))));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(std::make_tuple(
      &Foo::g2, &Foo::h2, static_cast<int (Foo::*)() const &>(nullptr))));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(std::make_tuple(
      &Foo::g3, &Foo::h3, static_cast<int (Foo::*)() &&>(nullptr))));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(std::make_tuple(
      &Foo::g4, &Foo::h4, static_cast<int (Foo::*)() const &&>(nullptr))));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      std::make_tuple(static_cast<void (Foo::*)()>(&Foo::vfa),
                      static_cast<void (Foo::*)()>(&Foo::vfb),
                      static_cast<void (Foo::*)()>(nullptr))));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      std::make_tuple(static_cast<void (Foo::*)()>(Foo::A::pq()),
                      static_cast<void (Foo::*)()>(Foo::B::pq()),
                      static_cast<void (Foo::*)()>(nullptr))));
}

TEST(HashValueTest, PairAndTuple) {
  EXPECT_TRUE((is_hashable<std::pair<int, int>>::value));
  EXPECT_TRUE((is_hashable<std::pair<const int&, const int&>>::value));
  EXPECT_TRUE((is_hashable<std::tuple<int&, int&>>::value));
  EXPECT_TRUE((is_hashable<std::tuple<int&&, int&&>>::value));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(std::make_tuple(
      std::make_pair(0, 42), std::make_pair(0, 42), std::make_pair(42, 0),
      std::make_pair(0, 0), std::make_pair(42, 42), std::make_pair(1, 42))));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      std::make_tuple(std::make_tuple(0, 0, 0), std::make_tuple(0, 0, 42),
                      std::make_tuple(0, 23, 0), std::make_tuple(17, 0, 0),
                      std::make_tuple(42, 0, 0), std::make_tuple(3, 9, 9),
                      std::make_tuple(0, 0, -42))));

  // Test that tuples of lvalue references work (so we need a few lvalues):
  int a = 0, b = 1, c = 17, d = 23;
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(std::make_tuple(
      std::tie(a, a), std::tie(a, b), std::tie(b, c), std::tie(c, d))));

  // Test that tuples of rvalue references work:
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(std::make_tuple(
      std::forward_as_tuple(0, 0, 0), std::forward_as_tuple(0, 0, 42),
      std::forward_as_tuple(0, 23, 0), std::forward_as_tuple(17, 0, 0),
      std::forward_as_tuple(42, 0, 0), std::forward_as_tuple(3, 9, 9),
      std::forward_as_tuple(0, 0, -42))));
}

TEST(HashValueTest, CombineContiguousWorks) {
  std::vector<std::tuple<int>> v1 = {std::make_tuple(1), std::make_tuple(3)};
  std::vector<std::tuple<int>> v2 = {std::make_tuple(1), std::make_tuple(2)};

  auto vh1 = SpyHash(v1);
  auto vh2 = SpyHash(v2);
  EXPECT_NE(vh1, vh2);
}

struct DummyDeleter {
  template <typename T>
  void operator() (T* ptr) {}
};

struct SmartPointerEq {
  template <typename T, typename U>
  bool operator()(const T& t, const U& u) const {
    return GetPtr(t) == GetPtr(u);
  }

  template <typename T>
  static auto GetPtr(const T& t) -> decltype(&*t) {
    return t ? &*t : nullptr;
  }

  static std::nullptr_t GetPtr(std::nullptr_t) { return nullptr; }
};

TEST(HashValueTest, SmartPointers) {
  EXPECT_TRUE((is_hashable<std::unique_ptr<int>>::value));
  EXPECT_TRUE((is_hashable<std::unique_ptr<int, DummyDeleter>>::value));
  EXPECT_TRUE((is_hashable<std::shared_ptr<int>>::value));

  int i, j;
  std::unique_ptr<int, DummyDeleter> unique1(&i);
  std::unique_ptr<int, DummyDeleter> unique2(&i);
  std::unique_ptr<int, DummyDeleter> unique_other(&j);
  std::unique_ptr<int, DummyDeleter> unique_null;

  std::shared_ptr<int> shared1(&i, DummyDeleter());
  std::shared_ptr<int> shared2(&i, DummyDeleter());
  std::shared_ptr<int> shared_other(&j, DummyDeleter());
  std::shared_ptr<int> shared_null;

  // Sanity check of the Eq function.
  ASSERT_TRUE(SmartPointerEq{}(unique1, shared1));
  ASSERT_FALSE(SmartPointerEq{}(unique1, shared_other));
  ASSERT_TRUE(SmartPointerEq{}(unique_null, nullptr));
  ASSERT_FALSE(SmartPointerEq{}(shared2, nullptr));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      std::forward_as_tuple(&i, nullptr,                    //
                            unique1, unique2, unique_null,  //
                            absl::make_unique<int>(),       //
                            shared1, shared2, shared_null,  //
                            std::make_shared<int>()),
      SmartPointerEq{}));
}

TEST(HashValueTest, FunctionPointer) {
  using Func = int (*)();
  EXPECT_TRUE(is_hashable<Func>::value);

  Func p1 = [] { return 2; }, p2 = [] { return 1; };
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      std::make_tuple(p1, p2, nullptr)));
}

struct WrapInTuple {
  template <typename T>
  std::tuple<int, T, size_t> operator()(const T& t) const {
    return std::make_tuple(7, t, 0xdeadbeef);
  }
};

absl::Cord FlatCord(absl::string_view sv) {
  absl::Cord c(sv);
  c.Flatten();
  return c;
}

absl::Cord FragmentedCord(absl::string_view sv) {
  if (sv.size() < 2) {
    return absl::Cord(sv);
  }
  size_t halfway = sv.size() / 2;
  std::vector<absl::string_view> parts = {sv.substr(0, halfway),
                                          sv.substr(halfway)};
  return absl::MakeFragmentedCord(parts);
}

TEST(HashValueTest, Strings) {
  EXPECT_TRUE((is_hashable<std::string>::value));

  const std::string small = "foo";
  const std::string dup = "foofoo";
  const std::string large = std::string(2048, 'x');  // multiple of chunk size
  const std::string huge = std::string(5000, 'a');   // not a multiple

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(std::make_tuple(  //
      std::string(), absl::string_view(), absl::Cord(),                     //
      std::string(""), absl::string_view(""), absl::Cord(""),               //
      std::string(small), absl::string_view(small), absl::Cord(small),      //
      std::string(dup), absl::string_view(dup), absl::Cord(dup),            //
      std::string(large), absl::string_view(large), absl::Cord(large),      //
      std::string(huge), absl::string_view(huge), FlatCord(huge),           //
      FragmentedCord(huge))));

  // Also check that nested types maintain the same hash.
  const WrapInTuple t{};
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(std::make_tuple(  //
      t(std::string()), t(absl::string_view()), t(absl::Cord()),            //
      t(std::string("")), t(absl::string_view("")), t(absl::Cord("")),      //
      t(std::string(small)), t(absl::string_view(small)),                   //
          t(absl::Cord(small)),                                             //
      t(std::string(dup)), t(absl::string_view(dup)), t(absl::Cord(dup)),   //
      t(std::string(large)), t(absl::string_view(large)),                   //
          t(absl::Cord(large)),                                             //
      t(std::string(huge)), t(absl::string_view(huge)),                     //
          t(FlatCord(huge)), t(FragmentedCord(huge)))));

  // Make sure that hashing a `const char*` does not use its string-value.
  EXPECT_NE(SpyHash(static_cast<const char*>("ABC")),
            SpyHash(absl::string_view("ABC")));
}

TEST(HashValueTest, WString) {
  EXPECT_TRUE((is_hashable<std::wstring>::value));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(std::make_tuple(
      std::wstring(), std::wstring(L"ABC"), std::wstring(L"ABC"),
      std::wstring(L"Some other different string"),
      std::wstring(L"Iñtërnâtiônàlizætiøn"))));
}

TEST(HashValueTest, U16String) {
  EXPECT_TRUE((is_hashable<std::u16string>::value));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(std::make_tuple(
      std::u16string(), std::u16string(u"ABC"), std::u16string(u"ABC"),
      std::u16string(u"Some other different string"),
      std::u16string(u"Iñtërnâtiônàlizætiøn"))));
}

TEST(HashValueTest, U32String) {
  EXPECT_TRUE((is_hashable<std::u32string>::value));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(std::make_tuple(
      std::u32string(), std::u32string(U"ABC"), std::u32string(U"ABC"),
      std::u32string(U"Some other different string"),
      std::u32string(U"Iñtërnâtiônàlizætiøn"))));
}

TEST(HashValueTest, WStringView) {
#ifndef ABSL_HAVE_STD_STRING_VIEW
  GTEST_SKIP();
#else
  EXPECT_TRUE((is_hashable<std::wstring_view>::value));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(std::make_tuple(
      std::wstring_view(), std::wstring_view(L"ABC"), std::wstring_view(L"ABC"),
      std::wstring_view(L"Some other different string_view"),
      std::wstring_view(L"Iñtërnâtiônàlizætiøn"))));
#endif
}

TEST(HashValueTest, U16StringView) {
#ifndef ABSL_HAVE_STD_STRING_VIEW
  GTEST_SKIP();
#else
  EXPECT_TRUE((is_hashable<std::u16string_view>::value));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      std::make_tuple(std::u16string_view(), std::u16string_view(u"ABC"),
                      std::u16string_view(u"ABC"),
                      std::u16string_view(u"Some other different string_view"),
                      std::u16string_view(u"Iñtërnâtiônàlizætiøn"))));
#endif
}

TEST(HashValueTest, U32StringView) {
#ifndef ABSL_HAVE_STD_STRING_VIEW
  GTEST_SKIP();
#else
  EXPECT_TRUE((is_hashable<std::u32string_view>::value));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      std::make_tuple(std::u32string_view(), std::u32string_view(U"ABC"),
                      std::u32string_view(U"ABC"),
                      std::u32string_view(U"Some other different string_view"),
                      std::u32string_view(U"Iñtërnâtiônàlizætiøn"))));
#endif
}

TEST(HashValueTest, StdFilesystemPath) {
#ifndef ABSL_INTERNAL_STD_FILESYSTEM_PATH_HASH_AVAILABLE
  GTEST_SKIP() << "std::filesystem::path is unavailable on this platform";
#else
  EXPECT_TRUE((is_hashable<std::filesystem::path>::value));

  // clang-format off
  const auto kTestCases = std::make_tuple(
      std::filesystem::path(),
      std::filesystem::path("/"),
#ifndef __GLIBCXX__
      // libstdc++ has a known issue normalizing "//".
      // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=106452
      std::filesystem::path("//"),
#endif
      std::filesystem::path("/a/b"),
      std::filesystem::path("/a//b"),
      std::filesystem::path("a/b"),
      std::filesystem::path("a/b/"),
      std::filesystem::path("a//b"),
      std::filesystem::path("a//b/"),
      std::filesystem::path("c:/"),
      std::filesystem::path("c:\\"),
      std::filesystem::path("c:\\/"),
      std::filesystem::path("c:\\//"),
      std::filesystem::path("c://"),
      std::filesystem::path("c://\\"),
      std::filesystem::path("/e/p"),
      std::filesystem::path("/s/../e/p"),
      std::filesystem::path("e/p"),
      std::filesystem::path("s/../e/p"));
  // clang-format on

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(kTestCases));
#endif
}

TEST(HashValueTest, StdArray) {
  EXPECT_TRUE((is_hashable<std::array<int, 3>>::value));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      std::make_tuple(std::array<int, 3>{}, std::array<int, 3>{{0, 23, 42}})));
}

TEST(HashValueTest, StdBitset) {
  EXPECT_TRUE((is_hashable<std::bitset<257>>::value));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      {std::bitset<2>("00"), std::bitset<2>("01"), std::bitset<2>("10"),
       std::bitset<2>("11")}));
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      {std::bitset<5>("10101"), std::bitset<5>("10001"), std::bitset<5>()}));

  constexpr int kNumBits = 256;
  std::array<std::string, 6> bit_strings;
  bit_strings.fill(std::string(kNumBits, '1'));
  bit_strings[1][0] = '0';
  bit_strings[2][1] = '0';
  bit_strings[3][kNumBits / 3] = '0';
  bit_strings[4][kNumBits - 2] = '0';
  bit_strings[5][kNumBits - 1] = '0';
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      {std::bitset<kNumBits>(bit_strings[0].c_str()),
       std::bitset<kNumBits>(bit_strings[1].c_str()),
       std::bitset<kNumBits>(bit_strings[2].c_str()),
       std::bitset<kNumBits>(bit_strings[3].c_str()),
       std::bitset<kNumBits>(bit_strings[4].c_str()),
       std::bitset<kNumBits>(bit_strings[5].c_str())}));
}  // namespace

// Private type that only supports AbslHashValue to make sure our chosen hash
// implementation is recursive within absl::Hash.
// It uses std::abs() on the value to provide different bitwise representations
// of the same logical value.
struct Private {
  int i;
  template <typename H>
  friend H AbslHashValue(H h, Private p) {
    return H::combine(std::move(h), std::abs(p.i));
  }

  friend bool operator==(Private a, Private b) {
    return std::abs(a.i) == std::abs(b.i);
  }

  friend std::ostream& operator<<(std::ostream& o, Private p) {
    return o << p.i;
  }
};

// Test helper for combine_piecewise_buffer.  It holds a string_view to the
// buffer-to-be-hashed.  Its AbslHashValue specialization will split up its
// contents at the character offsets requested.
class PiecewiseHashTester {
 public:
  // Create a hash view of a buffer to be hashed contiguously.
  explicit PiecewiseHashTester(absl::string_view buf)
      : buf_(buf), piecewise_(false), split_locations_() {}

  // Create a hash view of a buffer to be hashed piecewise, with breaks at the
  // given locations.
  PiecewiseHashTester(absl::string_view buf, std::set<size_t> split_locations)
      : buf_(buf),
        piecewise_(true),
        split_locations_(std::move(split_locations)) {}

  template <typename H>
  friend H AbslHashValue(H h, const PiecewiseHashTester& p) {
    if (!p.piecewise_) {
      return H::combine_contiguous(std::move(h), p.buf_.data(), p.buf_.size());
    }
    absl::hash_internal::PiecewiseCombiner combiner;
    if (p.split_locations_.empty()) {
      h = combiner.add_buffer(std::move(h), p.buf_.data(), p.buf_.size());
      return combiner.finalize(std::move(h));
    }
    size_t begin = 0;
    for (size_t next : p.split_locations_) {
      absl::string_view chunk = p.buf_.substr(begin, next - begin);
      h = combiner.add_buffer(std::move(h), chunk.data(), chunk.size());
      begin = next;
    }
    absl::string_view last_chunk = p.buf_.substr(begin);
    if (!last_chunk.empty()) {
      h = combiner.add_buffer(std::move(h), last_chunk.data(),
                              last_chunk.size());
    }
    return combiner.finalize(std::move(h));
  }

 private:
  absl::string_view buf_;
  bool piecewise_;
  std::set<size_t> split_locations_;
};

// Dummy object that hashes as two distinct contiguous buffers, "foo" followed
// by "bar"
struct DummyFooBar {
  template <typename H>
  friend H AbslHashValue(H h, const DummyFooBar&) {
    const char* foo = "foo";
    const char* bar = "bar";
    h = H::combine_contiguous(std::move(h), foo, 3);
    h = H::combine_contiguous(std::move(h), bar, 3);
    return h;
  }
};

TEST(HashValueTest, CombinePiecewiseBuffer) {
  absl::Hash<PiecewiseHashTester> hash;

  // Check that hashing an empty buffer through the piecewise API works.
  EXPECT_EQ(hash(PiecewiseHashTester("")), hash(PiecewiseHashTester("", {})));

  // Similarly, small buffers should give consistent results
  EXPECT_EQ(hash(PiecewiseHashTester("foobar")),
            hash(PiecewiseHashTester("foobar", {})));
  EXPECT_EQ(hash(PiecewiseHashTester("foobar")),
            hash(PiecewiseHashTester("foobar", {3})));

  // But hashing "foobar" in pieces gives a different answer than hashing "foo"
  // contiguously, then "bar" contiguously.
  EXPECT_NE(hash(PiecewiseHashTester("foobar", {3})),
            absl::Hash<DummyFooBar>()(DummyFooBar{}));

  // Test hashing a large buffer incrementally, broken up in several different
  // ways.  Arrange for breaks on and near the stride boundaries to look for
  // off-by-one errors in the implementation.
  //
  // This test is run on a buffer that is a multiple of the stride size, and one
  // that isn't.
  for (size_t big_buffer_size : {1024u * 2 + 512u, 1024u * 3}) {
    SCOPED_TRACE(big_buffer_size);
    std::string big_buffer;
    for (size_t i = 0; i < big_buffer_size; ++i) {
      // Arbitrary string
      big_buffer.push_back(32 + (i * (i / 3)) % 64);
    }
    auto big_buffer_hash = hash(PiecewiseHashTester(big_buffer));

    const int possible_breaks = 9;
    size_t breaks[possible_breaks] = {1,    512,  1023, 1024, 1025,
                                      1536, 2047, 2048, 2049};
    for (unsigned test_mask = 0; test_mask < (1u << possible_breaks);
         ++test_mask) {
      SCOPED_TRACE(test_mask);
      std::set<size_t> break_locations;
      for (int j = 0; j < possible_breaks; ++j) {
        if (test_mask & (1u << j)) {
          break_locations.insert(breaks[j]);
        }
      }
      EXPECT_EQ(
          hash(PiecewiseHashTester(big_buffer, std::move(break_locations))),
          big_buffer_hash);
    }
  }
}

TEST(HashValueTest, PrivateSanity) {
  // Sanity check that Private is working as the tests below expect it to work.
  EXPECT_TRUE(is_hashable<Private>::value);
  EXPECT_NE(SpyHash(Private{0}), SpyHash(Private{1}));
  EXPECT_EQ(SpyHash(Private{1}), SpyHash(Private{1}));
}

TEST(HashValueTest, Optional) {
  EXPECT_TRUE(is_hashable<absl::optional<Private>>::value);

  using O = absl::optional<Private>;
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      std::make_tuple(O{}, O{{1}}, O{{-1}}, O{{10}})));
}

TEST(HashValueTest, Variant) {
  using V = absl::variant<Private, std::string>;
  EXPECT_TRUE(is_hashable<V>::value);

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(std::make_tuple(
      V(Private{1}), V(Private{-1}), V(Private{2}), V("ABC"), V("BCD"))));

#if ABSL_META_INTERNAL_STD_HASH_SFINAE_FRIENDLY_
  struct S {};
  EXPECT_FALSE(is_hashable<absl::variant<S>>::value);
#endif
}

TEST(HashValueTest, ReferenceWrapper) {
  EXPECT_TRUE(is_hashable<std::reference_wrapper<Private>>::value);

  Private p1{1}, p10{10};
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(std::make_tuple(
      p1, p10, std::ref(p1), std::ref(p10), std::cref(p1), std::cref(p10))));

  EXPECT_TRUE(is_hashable<std::reference_wrapper<int>>::value);
  int one = 1, ten = 10;
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(std::make_tuple(
      one, ten, std::ref(one), std::ref(ten), std::cref(one), std::cref(ten))));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      std::make_tuple(std::tuple<std::reference_wrapper<int>>(std::ref(one)),
                      std::tuple<std::reference_wrapper<int>>(std::ref(ten)),
                      std::tuple<int>(one), std::tuple<int>(ten))));
}

template <typename T, typename = void>
struct IsHashCallable : std::false_type {};

template <typename T>
struct IsHashCallable<T, absl::void_t<decltype(std::declval<absl::Hash<T>>()(
                            std::declval<const T&>()))>> : std::true_type {};

template <typename T, typename = void>
struct IsAggregateInitializable : std::false_type {};

template <typename T>
struct IsAggregateInitializable<T, absl::void_t<decltype(T{})>>
    : std::true_type {};

TEST(IsHashableTest, ValidHash) {
  EXPECT_TRUE((is_hashable<int>::value));
  EXPECT_TRUE(std::is_default_constructible<absl::Hash<int>>::value);
  EXPECT_TRUE(std::is_copy_constructible<absl::Hash<int>>::value);
  EXPECT_TRUE(std::is_move_constructible<absl::Hash<int>>::value);
  EXPECT_TRUE(absl::is_copy_assignable<absl::Hash<int>>::value);
  EXPECT_TRUE(absl::is_move_assignable<absl::Hash<int>>::value);
  EXPECT_TRUE(IsHashCallable<int>::value);
  EXPECT_TRUE(IsAggregateInitializable<absl::Hash<int>>::value);
}

#if ABSL_META_INTERNAL_STD_HASH_SFINAE_FRIENDLY_
TEST(IsHashableTest, PoisonHash) {
  struct X {};
  EXPECT_FALSE((is_hashable<X>::value));
  EXPECT_FALSE(std::is_default_constructible<absl::Hash<X>>::value);
  EXPECT_FALSE(std::is_copy_constructible<absl::Hash<X>>::value);
  EXPECT_FALSE(std::is_move_constructible<absl::Hash<X>>::value);
  EXPECT_FALSE(absl::is_copy_assignable<absl::Hash<X>>::value);
  EXPECT_FALSE(absl::is_move_assignable<absl::Hash<X>>::value);
  EXPECT_FALSE(IsHashCallable<X>::value);
#if !defined(__GNUC__) || defined(__clang__)
  // TODO(b/144368551): As of GCC 8.4 this does not compile.
  EXPECT_FALSE(IsAggregateInitializable<absl::Hash<X>>::value);
#endif
}
#endif  // ABSL_META_INTERNAL_STD_HASH_SFINAE_FRIENDLY_

// Hashable types
//
// These types exist simply to exercise various AbslHashValue behaviors, so
// they are named by what their AbslHashValue overload does.
struct NoOp {
  template <typename HashCode>
  friend HashCode AbslHashValue(HashCode h, NoOp n) {
    return h;
  }
};

struct EmptyCombine {
  template <typename HashCode>
  friend HashCode AbslHashValue(HashCode h, EmptyCombine e) {
    return HashCode::combine(std::move(h));
  }
};

template <typename Int>
struct CombineIterative {
  template <typename HashCode>
  friend HashCode AbslHashValue(HashCode h, CombineIterative c) {
    for (int i = 0; i < 5; ++i) {
      h = HashCode::combine(std::move(h), Int(i));
    }
    return h;
  }
};

template <typename Int>
struct CombineVariadic {
  template <typename HashCode>
  friend HashCode AbslHashValue(HashCode h, CombineVariadic c) {
    return HashCode::combine(std::move(h), Int(0), Int(1), Int(2), Int(3),
                             Int(4));
  }
};
enum class InvokeTag {
  kUniquelyRepresented,
  kHashValue,
#if ABSL_HASH_INTERNAL_SUPPORT_LEGACY_HASH_
  kLegacyHash,
#endif  // ABSL_HASH_INTERNAL_SUPPORT_LEGACY_HASH_
  kStdHash,
  kNone
};

template <InvokeTag T>
using InvokeTagConstant = std::integral_constant<InvokeTag, T>;

template <InvokeTag... Tags>
struct MinTag;

template <InvokeTag a, InvokeTag b, InvokeTag... Tags>
struct MinTag<a, b, Tags...> : MinTag<(a < b ? a : b), Tags...> {};

template <InvokeTag a>
struct MinTag<a> : InvokeTagConstant<a> {};

template <InvokeTag... Tags>
struct CustomHashType {
  explicit CustomHashType(size_t val) : value(val) {}
  size_t value;
};

template <InvokeTag allowed, InvokeTag... tags>
struct EnableIfContained
    : std::enable_if<absl::disjunction<
          std::integral_constant<bool, allowed == tags>...>::value> {};

template <
    typename H, InvokeTag... Tags,
    typename = typename EnableIfContained<InvokeTag::kHashValue, Tags...>::type>
H AbslHashValue(H state, CustomHashType<Tags...> t) {
  static_assert(MinTag<Tags...>::value == InvokeTag::kHashValue, "");
  return H::combine(std::move(state),
                    t.value + static_cast<int>(InvokeTag::kHashValue));
}

}  // namespace

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace hash_internal {
template <InvokeTag... Tags>
struct is_uniquely_represented<
    CustomHashType<Tags...>,
    typename EnableIfContained<InvokeTag::kUniquelyRepresented, Tags...>::type>
    : std::true_type {};
}  // namespace hash_internal
ABSL_NAMESPACE_END
}  // namespace absl

#if ABSL_HASH_INTERNAL_SUPPORT_LEGACY_HASH_
namespace ABSL_INTERNAL_LEGACY_HASH_NAMESPACE {
template <InvokeTag... Tags>
struct hash<CustomHashType<Tags...>> {
  template <InvokeTag... TagsIn, typename = typename EnableIfContained<
                                     InvokeTag::kLegacyHash, TagsIn...>::type>
  size_t operator()(CustomHashType<TagsIn...> t) const {
    static_assert(MinTag<Tags...>::value == InvokeTag::kLegacyHash, "");
    return t.value + static_cast<int>(InvokeTag::kLegacyHash);
  }
};
}  // namespace ABSL_INTERNAL_LEGACY_HASH_NAMESPACE
#endif  // ABSL_HASH_INTERNAL_SUPPORT_LEGACY_HASH_

namespace std {
template <InvokeTag... Tags>  // NOLINT
struct hash<CustomHashType<Tags...>> {
  template <InvokeTag... TagsIn, typename = typename EnableIfContained<
                                     InvokeTag::kStdHash, TagsIn...>::type>
  size_t operator()(CustomHashType<TagsIn...> t) const {
    static_assert(MinTag<Tags...>::value == InvokeTag::kStdHash, "");
    return t.value + static_cast<int>(InvokeTag::kStdHash);
  }
};
}  // namespace std

namespace {

template <typename... T>
void TestCustomHashType(InvokeTagConstant<InvokeTag::kNone>, T...) {
  using type = CustomHashType<T::value...>;
  SCOPED_TRACE(testing::PrintToString(std::vector<InvokeTag>{T::value...}));
  EXPECT_TRUE(is_hashable<type>());
  EXPECT_TRUE(is_hashable<const type>());
  EXPECT_TRUE(is_hashable<const type&>());

  const size_t offset = static_cast<int>(std::min({T::value...}));
  EXPECT_EQ(SpyHash(type(7)), SpyHash(size_t{7 + offset}));
}

void TestCustomHashType(InvokeTagConstant<InvokeTag::kNone>) {
#if ABSL_META_INTERNAL_STD_HASH_SFINAE_FRIENDLY_
  // is_hashable is false if we don't support any of the hooks.
  using type = CustomHashType<>;
  EXPECT_FALSE(is_hashable<type>());
  EXPECT_FALSE(is_hashable<const type>());
  EXPECT_FALSE(is_hashable<const type&>());
#endif  // ABSL_META_INTERNAL_STD_HASH_SFINAE_FRIENDLY_
}

template <InvokeTag Tag, typename... T>
void TestCustomHashType(InvokeTagConstant<Tag> tag, T... t) {
  constexpr auto next = static_cast<InvokeTag>(static_cast<int>(Tag) + 1);
  TestCustomHashType(InvokeTagConstant<next>(), tag, t...);
  TestCustomHashType(InvokeTagConstant<next>(), t...);
}

TEST(HashTest, CustomHashType) {
  TestCustomHashType(InvokeTagConstant<InvokeTag{}>());
}

TEST(HashTest, NoOpsAreEquivalent) {
  EXPECT_EQ(Hash<NoOp>()({}), Hash<NoOp>()({}));
  EXPECT_EQ(Hash<NoOp>()({}), Hash<EmptyCombine>()({}));
}

template <typename T>
class HashIntTest : public testing::Test {
};
TYPED_TEST_SUITE_P(HashIntTest);

TYPED_TEST_P(HashIntTest, BasicUsage) {
  EXPECT_NE(Hash<NoOp>()({}), Hash<TypeParam>()(0));
  EXPECT_NE(Hash<NoOp>()({}),
            Hash<TypeParam>()(std::numeric_limits<TypeParam>::max()));
  if (std::numeric_limits<TypeParam>::min() != 0) {
    EXPECT_NE(Hash<NoOp>()({}),
              Hash<TypeParam>()(std::numeric_limits<TypeParam>::min()));
  }

  EXPECT_EQ(Hash<CombineIterative<TypeParam>>()({}),
            Hash<CombineVariadic<TypeParam>>()({}));
}

REGISTER_TYPED_TEST_SUITE_P(HashIntTest, BasicUsage);
using IntTypes = testing::Types<unsigned char, char, int, int32_t, int64_t,
                                uint32_t, uint64_t, size_t>;
INSTANTIATE_TYPED_TEST_SUITE_P(My, HashIntTest, IntTypes);

struct StructWithPadding {
  char c;
  int i;

  template <typename H>
  friend H AbslHashValue(H hash_state, const StructWithPadding& s) {
    return H::combine(std::move(hash_state), s.c, s.i);
  }
};

static_assert(sizeof(StructWithPadding) > sizeof(char) + sizeof(int),
              "StructWithPadding doesn't have padding");
static_assert(std::is_standard_layout<StructWithPadding>::value, "");

// This check has to be disabled because libstdc++ doesn't support it.
// static_assert(std::is_trivially_constructible<StructWithPadding>::value, "");

template <typename T>
struct ArraySlice {
  T* begin;
  T* end;

  template <typename H>
  friend H AbslHashValue(H hash_state, const ArraySlice& slice) {
    for (auto t = slice.begin; t != slice.end; ++t) {
      hash_state = H::combine(std::move(hash_state), *t);
    }
    return hash_state;
  }
};

TEST(HashTest, HashNonUniquelyRepresentedType) {
  // Create equal StructWithPadding objects that are known to have non-equal
  // padding bytes.
  static const size_t kNumStructs = 10;
  unsigned char buffer1[kNumStructs * sizeof(StructWithPadding)];
  std::memset(buffer1, 0, sizeof(buffer1));
  auto* s1 = reinterpret_cast<StructWithPadding*>(buffer1);

  unsigned char buffer2[kNumStructs * sizeof(StructWithPadding)];
  std::memset(buffer2, 255, sizeof(buffer2));
  auto* s2 = reinterpret_cast<StructWithPadding*>(buffer2);
  for (size_t i = 0; i < kNumStructs; ++i) {
    SCOPED_TRACE(i);
    s1[i].c = s2[i].c = static_cast<char>('0' + i);
    s1[i].i = s2[i].i = static_cast<int>(i);
    ASSERT_FALSE(memcmp(buffer1 + i * sizeof(StructWithPadding),
                        buffer2 + i * sizeof(StructWithPadding),
                        sizeof(StructWithPadding)) == 0)
        << "Bug in test code: objects do not have unequal"
        << " object representations";
  }

  EXPECT_EQ(Hash<StructWithPadding>()(s1[0]), Hash<StructWithPadding>()(s2[0]));
  EXPECT_EQ(Hash<ArraySlice<StructWithPadding>>()({s1, s1 + kNumStructs}),
            Hash<ArraySlice<StructWithPadding>>()({s2, s2 + kNumStructs}));
}

TEST(HashTest, StandardHashContainerUsage) {
  std::unordered_map<int, std::string, Hash<int>> map = {{0, "foo"},
                                                         {42, "bar"}};

  EXPECT_NE(map.find(0), map.end());
  EXPECT_EQ(map.find(1), map.end());
  EXPECT_NE(map.find(0u), map.end());
}

struct ConvertibleFromNoOp {
  ConvertibleFromNoOp(NoOp) {}  // NOLINT(runtime/explicit)

  template <typename H>
  friend H AbslHashValue(H hash_state, ConvertibleFromNoOp) {
    return H::combine(std::move(hash_state), 1);
  }
};

TEST(HashTest, HeterogeneousCall) {
  EXPECT_NE(Hash<ConvertibleFromNoOp>()(NoOp()),
            Hash<NoOp>()(NoOp()));
}

TEST(IsUniquelyRepresentedTest, SanityTest) {
  using absl::hash_internal::is_uniquely_represented;

  EXPECT_TRUE(is_uniquely_represented<unsigned char>::value);
  EXPECT_TRUE(is_uniquely_represented<int>::value);
  EXPECT_FALSE(is_uniquely_represented<bool>::value);
  EXPECT_FALSE(is_uniquely_represented<int*>::value);
}

struct IntAndString {
  int i;
  std::string s;

  template <typename H>
  friend H AbslHashValue(H hash_state, IntAndString int_and_string) {
    return H::combine(std::move(hash_state), int_and_string.s,
                      int_and_string.i);
  }
};

TEST(HashTest, SmallValueOn64ByteBoundary) {
  Hash<IntAndString>()(IntAndString{0, std::string(63, '0')});
}

TEST(HashTest, TypeErased) {
  EXPECT_TRUE((is_hashable<TypeErasedValue<size_t>>::value));
  EXPECT_TRUE((is_hashable<std::pair<TypeErasedValue<size_t>, int>>::value));

  EXPECT_EQ(SpyHash(TypeErasedValue<size_t>(7)), SpyHash(size_t{7}));
  EXPECT_NE(SpyHash(TypeErasedValue<size_t>(7)), SpyHash(size_t{13}));

  EXPECT_EQ(SpyHash(std::make_pair(TypeErasedValue<size_t>(7), 17)),
            SpyHash(std::make_pair(size_t{7}, 17)));

  absl::flat_hash_set<absl::flat_hash_set<int>> ss = {{1, 2}, {3, 4}};
  TypeErasedContainer<absl::flat_hash_set<absl::flat_hash_set<int>>> es = {
      absl::flat_hash_set<int>{1, 2}, {3, 4}};
  absl::flat_hash_set<TypeErasedContainer<absl::flat_hash_set<int>>> se = {
      {1, 2}, {3, 4}};
  EXPECT_EQ(SpyHash(ss), SpyHash(es));
  EXPECT_EQ(SpyHash(ss), SpyHash(se));
}

struct ValueWithBoolConversion {
  operator bool() const { return false; }
  int i;
};

}  // namespace
namespace std {
template <>
struct hash<ValueWithBoolConversion> {
  size_t operator()(ValueWithBoolConversion v) {
    return static_cast<size_t>(v.i);
  }
};
}  // namespace std

namespace {

TEST(HashTest, DoesNotUseImplicitConversionsToBool) {
  EXPECT_NE(absl::Hash<ValueWithBoolConversion>()(ValueWithBoolConversion{0}),
            absl::Hash<ValueWithBoolConversion>()(ValueWithBoolConversion{1}));
}

TEST(HashOf, MatchesHashForSingleArgument) {
  std::string s = "forty two";
  double d = 42.0;
  std::tuple<int, int> t{4, 2};
  int i = 42;
  int neg_i = -42;
  int16_t i16 = 42;
  int16_t neg_i16 = -42;
  int8_t i8 = 42;
  int8_t neg_i8 = -42;

  EXPECT_EQ(absl::HashOf(s), absl::Hash<std::string>{}(s));
  EXPECT_EQ(absl::HashOf(d), absl::Hash<double>{}(d));
  EXPECT_EQ(absl::HashOf(t), (absl::Hash<std::tuple<int, int>>{}(t)));
  EXPECT_EQ(absl::HashOf(i), absl::Hash<int>{}(i));
  EXPECT_EQ(absl::HashOf(neg_i), absl::Hash<int>{}(neg_i));
  EXPECT_EQ(absl::HashOf(i16), absl::Hash<int16_t>{}(i16));
  EXPECT_EQ(absl::HashOf(neg_i16), absl::Hash<int16_t>{}(neg_i16));
  EXPECT_EQ(absl::HashOf(i8), absl::Hash<int8_t>{}(i8));
  EXPECT_EQ(absl::HashOf(neg_i8), absl::Hash<int8_t>{}(neg_i8));
}

TEST(HashOf, MatchesHashOfTupleForMultipleArguments) {
  std::string hello = "hello";
  std::string world = "world";

  EXPECT_EQ(absl::HashOf(), absl::HashOf(std::make_tuple()));
  EXPECT_EQ(absl::HashOf(hello), absl::HashOf(std::make_tuple(hello)));
  EXPECT_EQ(absl::HashOf(hello, world),
            absl::HashOf(std::make_tuple(hello, world)));
}

template <typename T>
std::true_type HashOfExplicitParameter(decltype(absl::HashOf<T>(0))) {
  return {};
}
template <typename T>
std::false_type HashOfExplicitParameter(size_t) {
  return {};
}

TEST(HashOf, CantPassExplicitTemplateParameters) {
  EXPECT_FALSE(HashOfExplicitParameter<int>(0));
}

}  // namespace
