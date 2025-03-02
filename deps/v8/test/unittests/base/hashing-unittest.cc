// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/hashing.h"

#include <limits>
#include <set>

#include "test/unittests/test-utils.h"

namespace v8 {
namespace base {

TEST(HashingTest, HashBool) {
  hash<bool> h, h1, h2;
  EXPECT_EQ(h1(true), h2(true));
  EXPECT_EQ(h1(false), h2(false));
  EXPECT_NE(h(true), h(false));
}

TEST(HashingTest, HashFloatZero) {
  hash<float> h;
  EXPECT_EQ(h(0.0f), h(-0.0f));
}

TEST(HashingTest, HashDoubleZero) {
  hash<double> h;
  EXPECT_EQ(h(0.0), h(-0.0));
}

namespace {

inline int64_t GetRandomSeedFromFlag(int random_seed) {
  return random_seed ? random_seed : TimeTicks::Now().ToInternalValue();
}

}  // namespace

template <typename T>
class HashingTest : public ::testing::Test {
 public:
  HashingTest()
      : rng_(GetRandomSeedFromFlag(::v8::internal::v8_flags.random_seed)) {}
  ~HashingTest() override = default;
  HashingTest(const HashingTest&) = delete;
  HashingTest& operator=(const HashingTest&) = delete;

  RandomNumberGenerator* rng() { return &rng_; }

 private:
  RandomNumberGenerator rng_;
};

using HashingTypes =
    ::testing::Types<signed char, unsigned char,
                     short,                    // NOLINT(runtime/int)
                     unsigned short,           // NOLINT(runtime/int)
                     int, unsigned int, long,  // NOLINT(runtime/int)
                     unsigned long,            // NOLINT(runtime/int)
                     long long,                // NOLINT(runtime/int)
                     unsigned long long,       // NOLINT(runtime/int)
                     int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t,
                     int64_t, uint64_t, float, double>;

TYPED_TEST_SUITE(HashingTest, HashingTypes);

TYPED_TEST(HashingTest, EqualToImpliesSameHashCode) {
  hash<TypeParam> h;
  std::equal_to<TypeParam> e;
  TypeParam values[32];
  this->rng()->NextBytes(values, sizeof(values));
  TRACED_FOREACH(TypeParam, v1, values) {
    TRACED_FOREACH(TypeParam, v2, values) {
      if (e(v1, v2)) {
        EXPECT_EQ(h(v1), h(v2));
      }
    }
  }
}

TYPED_TEST(HashingTest, HashEqualsHashValue) {
  for (int i = 0; i < 128; ++i) {
    TypeParam v;
    this->rng()->NextBytes(&v, sizeof(v));
    hash<TypeParam> h;
    EXPECT_EQ(h(v), hash_value(v));
  }
}

TYPED_TEST(HashingTest, HashIsStateless) {
  hash<TypeParam> h1, h2;
  for (int i = 0; i < 128; ++i) {
    TypeParam v;
    this->rng()->NextBytes(&v, sizeof(v));
    EXPECT_EQ(h1(v), h2(v));
  }
}

TYPED_TEST(HashingTest, HashIsOkish) {
  std::set<TypeParam> vs;
  for (size_t i = 0; i < 128; ++i) {
    TypeParam v;
    this->rng()->NextBytes(&v, sizeof(v));
    vs.insert(v);
  }
  std::set<size_t> hs;
  for (const auto& v : vs) {
    hash<TypeParam> h;
    hs.insert(h(v));
  }
  EXPECT_LE(vs.size() / 4u, hs.size());
}

TYPED_TEST(HashingTest, HashValueArrayUsesHashRange) {
  TypeParam values[128];
  this->rng()->NextBytes(&values, sizeof(values));
  EXPECT_EQ(hash_range(values, values + arraysize(values)), hash_value(values));
}

TYPED_TEST(HashingTest, BitEqualTo) {
  bit_equal_to<TypeParam> pred;
  for (size_t i = 0; i < 128; ++i) {
    TypeParam v1, v2;
    this->rng()->NextBytes(&v1, sizeof(v1));
    this->rng()->NextBytes(&v2, sizeof(v2));
    EXPECT_PRED2(pred, v1, v1);
    EXPECT_PRED2(pred, v2, v2);
    EXPECT_EQ(memcmp(&v1, &v2, sizeof(TypeParam)) == 0, pred(v1, v2));
  }
}

TYPED_TEST(HashingTest, BitEqualToImpliesSameBitHash) {
  bit_hash<TypeParam> h;
  bit_equal_to<TypeParam> e;
  TypeParam values[32];
  this->rng()->NextBytes(&values, sizeof(values));
  TRACED_FOREACH(TypeParam, v1, values) {
    TRACED_FOREACH(TypeParam, v2, values) {
      if (e(v1, v2)) {
        EXPECT_EQ(h(v1), h(v2));
      }
    }
  }
}

namespace {

struct Foo {
  int x;
  double y;
};

size_t hash_value(Foo const& v) { return hash_combine(v.x, v.y); }

}  // namespace

TEST(HashingTest, HashUsesArgumentDependentLookup) {
  const int kIntValues[] = {std::numeric_limits<int>::min(), -1, 0, 1, 42,
                            std::numeric_limits<int>::max()};
  const double kDoubleValues[] = {
      std::numeric_limits<double>::min(), -1, -0, 0, 1,
      std::numeric_limits<double>::max()};
  TRACED_FOREACH(int, x, kIntValues) {
    TRACED_FOREACH(double, y, kDoubleValues) {
      hash<Foo> h;
      Foo foo = {x, y};
      EXPECT_EQ(hash_combine(x, y), h(foo));
    }
  }
}

TEST(HashingTest, BitEqualToFloat) {
  bit_equal_to<float> pred;
  EXPECT_FALSE(pred(0.0f, -0.0f));
  EXPECT_FALSE(pred(-0.0f, 0.0f));
  float const qNaN = std::numeric_limits<float>::quiet_NaN();
  float const sNaN = std::numeric_limits<float>::signaling_NaN();
  EXPECT_PRED2(pred, qNaN, qNaN);
  EXPECT_PRED2(pred, sNaN, sNaN);
}

TEST(HashingTest, BitHashFloatDifferentForZeroAndMinusZero) {
  bit_hash<float> h;
  EXPECT_NE(h(0.0f), h(-0.0f));
}

TEST(HashingTest, BitEqualToDouble) {
  bit_equal_to<double> pred;
  EXPECT_FALSE(pred(0.0, -0.0));
  EXPECT_FALSE(pred(-0.0, 0.0));
  double const qNaN = std::numeric_limits<double>::quiet_NaN();
  double const sNaN = std::numeric_limits<double>::signaling_NaN();
  EXPECT_PRED2(pred, qNaN, qNaN);
  EXPECT_PRED2(pred, sNaN, sNaN);
}

TEST(HashingTest, BitHashDoubleDifferentForZeroAndMinusZero) {
  bit_hash<double> h;
  EXPECT_NE(h(0.0), h(-0.0));
}

}  // namespace base
}  // namespace v8
