// Copyright 2020 The Abseil Authors.
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

#include "absl/strings/cord.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <iterator>
#include <limits>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/endian.h"
#include "absl/base/macros.h"
#include "absl/base/no_destructor.h"
#include "absl/base/options.h"
#include "absl/container/fixed_array.h"
#include "absl/functional/function_ref.h"
#include "absl/hash/hash.h"
#include "absl/hash/hash_testing.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/strings/cord_buffer.h"
#include "absl/strings/cord_test_helpers.h"
#include "absl/strings/cordz_test_helpers.h"
#include "absl/strings/internal/cord_internal.h"
#include "absl/strings/internal/cord_rep_crc.h"
#include "absl/strings/internal/cord_rep_flat.h"
#include "absl/strings/internal/cordz_statistics.h"
#include "absl/strings/internal/cordz_update_tracker.h"
#include "absl/strings/internal/string_constant.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/compare.h"
#include "absl/types/optional.h"

// convenience local constants
static constexpr auto FLAT = absl::cord_internal::FLAT;
static constexpr auto MAX_FLAT_TAG = absl::cord_internal::MAX_FLAT_TAG;

typedef std::mt19937_64 RandomEngine;

using absl::cord_internal::CordRep;
using absl::cord_internal::CordRepBtree;
using absl::cord_internal::CordRepConcat;
using absl::cord_internal::CordRepCrc;
using absl::cord_internal::CordRepExternal;
using absl::cord_internal::CordRepFlat;
using absl::cord_internal::CordRepSubstring;
using absl::cord_internal::CordzUpdateTracker;
using absl::cord_internal::kFlatOverhead;
using absl::cord_internal::kMaxFlatLength;
using ::testing::ElementsAre;
using ::testing::Le;

static std::string RandomLowercaseString(RandomEngine* rng);
static std::string RandomLowercaseString(RandomEngine* rng, size_t length);

static int GetUniformRandomUpTo(RandomEngine* rng, int upper_bound) {
  if (upper_bound > 0) {
    std::uniform_int_distribution<int> uniform(0, upper_bound - 1);
    return uniform(*rng);
  } else {
    return 0;
  }
}

static size_t GetUniformRandomUpTo(RandomEngine* rng, size_t upper_bound) {
  if (upper_bound > 0) {
    std::uniform_int_distribution<size_t> uniform(0, upper_bound - 1);
    return uniform(*rng);
  } else {
    return 0;
  }
}

static int32_t GenerateSkewedRandom(RandomEngine* rng, int max_log) {
  const uint32_t base = (*rng)() % (max_log + 1);
  const uint32_t mask = ((base < 32) ? (1u << base) : 0u) - 1u;
  return (*rng)() & mask;
}

static std::string RandomLowercaseString(RandomEngine* rng) {
  int length;
  std::bernoulli_distribution one_in_1k(0.001);
  std::bernoulli_distribution one_in_10k(0.0001);
  // With low probability, make a large fragment
  if (one_in_10k(*rng)) {
    length = GetUniformRandomUpTo(rng, 1048576);
  } else if (one_in_1k(*rng)) {
    length = GetUniformRandomUpTo(rng, 10000);
  } else {
    length = GenerateSkewedRandom(rng, 10);
  }
  return RandomLowercaseString(rng, length);
}

static std::string RandomLowercaseString(RandomEngine* rng, size_t length) {
  std::string result(length, '\0');
  std::uniform_int_distribution<int> chars('a', 'z');
  std::generate(result.begin(), result.end(),
                [&]() { return static_cast<char>(chars(*rng)); });
  return result;
}

static void DoNothing(absl::string_view /* data */, void* /* arg */) {}

static void DeleteExternalString(absl::string_view data, void* arg) {
  std::string* s = reinterpret_cast<std::string*>(arg);
  EXPECT_EQ(data, *s);
  delete s;
}

// Add "s" to *dst via `MakeCordFromExternal`
static void AddExternalMemory(absl::string_view s, absl::Cord* dst) {
  std::string* str = new std::string(s.data(), s.size());
  dst->Append(absl::MakeCordFromExternal(*str, [str](absl::string_view data) {
    DeleteExternalString(data, str);
  }));
}

static void DumpGrowth() {
  absl::Cord str;
  for (int i = 0; i < 1000; i++) {
    char c = 'a' + i % 26;
    str.Append(absl::string_view(&c, 1));
  }
}

// Make a Cord with some number of fragments.  Return the size (in bytes)
// of the smallest fragment.
static size_t AppendWithFragments(const std::string& s, RandomEngine* rng,
                                  absl::Cord* cord) {
  size_t j = 0;
  const size_t max_size = s.size() / 5;  // Make approx. 10 fragments
  size_t min_size = max_size;            // size of smallest fragment
  while (j < s.size()) {
    size_t N = 1 + GetUniformRandomUpTo(rng, max_size);
    if (N > (s.size() - j)) {
      N = s.size() - j;
    }
    if (N < min_size) {
      min_size = N;
    }

    std::bernoulli_distribution coin_flip(0.5);
    if (coin_flip(*rng)) {
      // Grow by adding an external-memory.
      AddExternalMemory(absl::string_view(s.data() + j, N), cord);
    } else {
      cord->Append(absl::string_view(s.data() + j, N));
    }
    j += N;
  }
  return min_size;
}

// Add an external memory that contains the specified std::string to cord
static void AddNewStringBlock(const std::string& str, absl::Cord* dst) {
  char* data = new char[str.size()];
  memcpy(data, str.data(), str.size());
  dst->Append(absl::MakeCordFromExternal(
      absl::string_view(data, str.size()),
      [](absl::string_view s) { delete[] s.data(); }));
}

// Make a Cord out of many different types of nodes.
static absl::Cord MakeComposite() {
  absl::Cord cord;
  cord.Append("the");
  AddExternalMemory(" quick brown", &cord);
  AddExternalMemory(" fox jumped", &cord);

  absl::Cord full(" over");
  AddExternalMemory(" the lazy", &full);
  AddNewStringBlock(" dog slept the whole day away", &full);
  absl::Cord substring = full.Subcord(0, 18);

  // Make substring long enough to defeat the copying fast path in Append.
  substring.Append(std::string(1000, '.'));
  cord.Append(substring);
  cord = cord.Subcord(0, cord.size() - 998);  // Remove most of extra junk

  return cord;
}

namespace absl {
ABSL_NAMESPACE_BEGIN

class CordTestPeer {
 public:
  static void ForEachChunk(
      const Cord& c, absl::FunctionRef<void(absl::string_view)> callback) {
    c.ForEachChunk(callback);
  }

  static bool IsTree(const Cord& c) { return c.contents_.is_tree(); }
  static CordRep* Tree(const Cord& c) { return c.contents_.tree(); }

  static cord_internal::CordzInfo* GetCordzInfo(const Cord& c) {
    return c.contents_.cordz_info();
  }

  static Cord MakeSubstring(Cord src, size_t offset, size_t length) {
    CHECK(src.contents_.is_tree()) << "Can not be inlined";
    CHECK(!src.ExpectedChecksum().has_value()) << "Can not be hardened";
    Cord cord;
    auto* tree = cord_internal::SkipCrcNode(src.contents_.tree());
    auto* rep = CordRepSubstring::Create(CordRep::Ref(tree), offset, length);
    cord.contents_.EmplaceTree(rep, CordzUpdateTracker::kSubCord);
    return cord;
  }
};

ABSL_NAMESPACE_END
}  // namespace absl



// The CordTest fixture runs all tests with and without expected CRCs being set
// on the subject Cords.
class CordTest : public testing::TestWithParam<bool /*useCrc*/> {
 public:
  // Returns true if test is running with Crc enabled.
  bool UseCrc() const { return GetParam(); }
  void MaybeHarden(absl::Cord& c) {
    if (UseCrc()) {
      c.SetExpectedChecksum(1);
    }
  }
  absl::Cord MaybeHardened(absl::Cord c) {
    MaybeHarden(c);
    return c;
  }

  // Returns human readable string representation of the test parameter.
  static std::string ToString(testing::TestParamInfo<bool> useCrc) {
    if (useCrc.param) {
      return "BtreeHardened";
    } else {
      return "Btree";
    }
  }
};

INSTANTIATE_TEST_SUITE_P(WithParam, CordTest, testing::Bool(),
                         CordTest::ToString);

TEST(CordRepFlat, AllFlatCapacities) {
  // Explicitly and redundantly assert built-in min/max limits
  static_assert(absl::cord_internal::kFlatOverhead < 32, "");
  static_assert(absl::cord_internal::kMinFlatSize == 32, "");
  static_assert(absl::cord_internal::kMaxLargeFlatSize == 256 << 10, "");
  EXPECT_EQ(absl::cord_internal::TagToAllocatedSize(FLAT), 32);
  EXPECT_EQ(absl::cord_internal::TagToAllocatedSize(MAX_FLAT_TAG), 256 << 10);

  // Verify all tags to map perfectly back and forth, and
  // that sizes are monotonically increasing.
  size_t last_size = 0;
  for (int tag = FLAT; tag <= MAX_FLAT_TAG; ++tag) {
    size_t size = absl::cord_internal::TagToAllocatedSize(tag);
    ASSERT_GT(size, last_size);
    ASSERT_EQ(absl::cord_internal::TagToAllocatedSize(tag), size);
    last_size = size;
  }

  // All flat size from 32 - 512 are 8 byte granularity
  for (size_t size = 32; size <= 512; size += 8) {
    ASSERT_EQ(absl::cord_internal::RoundUpForTag(size), size);
    uint8_t tag = absl::cord_internal::AllocatedSizeToTag(size);
    ASSERT_EQ(absl::cord_internal::TagToAllocatedSize(tag), size);
  }

  // All flat sizes from 512 - 8192 are 64 byte granularity
  for (size_t size = 512; size <= 8192; size += 64) {
    ASSERT_EQ(absl::cord_internal::RoundUpForTag(size), size);
    uint8_t tag = absl::cord_internal::AllocatedSizeToTag(size);
    ASSERT_EQ(absl::cord_internal::TagToAllocatedSize(tag), size);
  }

  // All flat sizes from 8KB to 256KB are 4KB granularity
  for (size_t size = 8192; size <= 256 * 1024; size += 4 * 1024) {
    ASSERT_EQ(absl::cord_internal::RoundUpForTag(size), size);
    uint8_t tag = absl::cord_internal::AllocatedSizeToTag(size);
    ASSERT_EQ(absl::cord_internal::TagToAllocatedSize(tag), size);
  }
}

TEST(CordRepFlat, MaxFlatSize) {
  CordRepFlat* flat = CordRepFlat::New(kMaxFlatLength);
  EXPECT_EQ(flat->Capacity(), kMaxFlatLength);
  CordRep::Unref(flat);

  flat = CordRepFlat::New(kMaxFlatLength * 4);
  EXPECT_EQ(flat->Capacity(), kMaxFlatLength);
  CordRep::Unref(flat);
}

TEST(CordRepFlat, MaxLargeFlatSize) {
  const size_t size = 256 * 1024 - kFlatOverhead;
  CordRepFlat* flat = CordRepFlat::New(CordRepFlat::Large(), size);
  EXPECT_GE(flat->Capacity(), size);
  CordRep::Unref(flat);
}

TEST(CordRepFlat, AllFlatSizes) {
  const size_t kMaxSize = 256 * 1024;
  for (size_t size = 32; size <= kMaxSize; size *=2) {
    const size_t length = size - kFlatOverhead - 1;
    CordRepFlat* flat = CordRepFlat::New(CordRepFlat::Large(), length);
    EXPECT_GE(flat->Capacity(), length);
    memset(flat->Data(), 0xCD, flat->Capacity());
    CordRep::Unref(flat);
  }
}

TEST_P(CordTest, AllFlatSizes) {
  using absl::strings_internal::CordTestAccess;

  for (size_t s = 0; s < CordTestAccess::MaxFlatLength(); s++) {
    // Make a string of length s.
    std::string src;
    while (src.size() < s) {
      src.push_back('a' + (src.size() % 26));
    }

    absl::Cord dst(src);
    MaybeHarden(dst);
    EXPECT_EQ(std::string(dst), src) << s;
  }
}

// We create a Cord at least 128GB in size using the fact that Cords can
// internally reference-count; thus the Cord is enormous without actually
// consuming very much memory.
TEST_P(CordTest, GigabyteCordFromExternal) {
  const size_t one_gig = 1024U * 1024U * 1024U;
  size_t max_size = 2 * one_gig;
  if (sizeof(max_size) > 4) max_size = 128 * one_gig;

  size_t length = 128 * 1024;
  char* data = new char[length];
  absl::Cord from = absl::MakeCordFromExternal(
      absl::string_view(data, length),
      [](absl::string_view sv) { delete[] sv.data(); });

  // This loop may seem odd due to its combination of exponential doubling of
  // size and incremental size increases.  We do it incrementally to be sure the
  // Cord will need rebalancing and will exercise code that, in the past, has
  // caused crashes in production.  We grow exponentially so that the code will
  // execute in a reasonable amount of time.
  absl::Cord c;
  c.Append(from);
  while (c.size() < max_size) {
    c.Append(c);
    c.Append(from);
    c.Append(from);
    c.Append(from);
    c.Append(from);
    MaybeHarden(c);
  }

  for (int i = 0; i < 1024; ++i) {
    c.Append(from);
  }
  LOG(INFO) << "Made a Cord with " << c.size() << " bytes!";
  // Note: on a 32-bit build, this comes out to   2,818,048,000 bytes.
  // Note: on a 64-bit build, this comes out to 171,932,385,280 bytes.
}

static absl::Cord MakeExternalCord(int size) {
  char* buffer = new char[size];
  memset(buffer, 'x', size);
  absl::Cord cord;
  cord.Append(absl::MakeCordFromExternal(
      absl::string_view(buffer, size),
      [](absl::string_view s) { delete[] s.data(); }));
  return cord;
}

// Extern to fool clang that this is not constant. Needed to suppress
// a warning of unsafe code we want to test.
extern bool my_unique_true_boolean;
bool my_unique_true_boolean = true;

TEST_P(CordTest, Assignment) {
  absl::Cord x(absl::string_view("hi there"));
  absl::Cord y(x);
  MaybeHarden(y);
  ASSERT_EQ(x.ExpectedChecksum(), absl::nullopt);
  ASSERT_EQ(std::string(x), "hi there");
  ASSERT_EQ(std::string(y), "hi there");
  ASSERT_TRUE(x == y);
  ASSERT_TRUE(x <= y);
  ASSERT_TRUE(y <= x);

  x = absl::string_view("foo");
  ASSERT_EQ(std::string(x), "foo");
  ASSERT_EQ(std::string(y), "hi there");
  ASSERT_TRUE(x < y);
  ASSERT_TRUE(y > x);
  ASSERT_TRUE(x != y);
  ASSERT_TRUE(x <= y);
  ASSERT_TRUE(y >= x);

  x = "foo";
  ASSERT_EQ(x, "foo");

  // Test that going from inline rep to tree we don't leak memory.
  std::vector<std::pair<absl::string_view, absl::string_view>>
      test_string_pairs = {{"hi there", "foo"},
                           {"loooooong coooooord", "short cord"},
                           {"short cord", "loooooong coooooord"},
                           {"loooooong coooooord1", "loooooong coooooord2"}};
  for (std::pair<absl::string_view, absl::string_view> test_strings :
       test_string_pairs) {
    absl::Cord tmp(test_strings.first);
    absl::Cord z(std::move(tmp));
    ASSERT_EQ(std::string(z), test_strings.first);
    tmp = test_strings.second;
    z = std::move(tmp);
    ASSERT_EQ(std::string(z), test_strings.second);
  }
  {
    // Test that self-move assignment doesn't crash/leak.
    // Do not write such code!
    absl::Cord my_small_cord("foo");
    absl::Cord my_big_cord("loooooong coooooord");
    // Bypass clang's warning on self move-assignment.
    absl::Cord* my_small_alias =
        my_unique_true_boolean ? &my_small_cord : &my_big_cord;
    absl::Cord* my_big_alias =
        !my_unique_true_boolean ? &my_small_cord : &my_big_cord;

    *my_small_alias = std::move(my_small_cord);
    *my_big_alias = std::move(my_big_cord);
    // my_small_cord and my_big_cord are in an unspecified but valid
    // state, and will be correctly destroyed here.
  }
}

TEST_P(CordTest, StartsEndsWith) {
  absl::Cord x(absl::string_view("abcde"));
  MaybeHarden(x);
  absl::Cord empty("");

  ASSERT_TRUE(x.StartsWith(absl::Cord("abcde")));
  ASSERT_TRUE(x.StartsWith(absl::Cord("abc")));
  ASSERT_TRUE(x.StartsWith(absl::Cord("")));
  ASSERT_TRUE(empty.StartsWith(absl::Cord("")));
  ASSERT_TRUE(x.EndsWith(absl::Cord("abcde")));
  ASSERT_TRUE(x.EndsWith(absl::Cord("cde")));
  ASSERT_TRUE(x.EndsWith(absl::Cord("")));
  ASSERT_TRUE(empty.EndsWith(absl::Cord("")));

  ASSERT_TRUE(!x.StartsWith(absl::Cord("xyz")));
  ASSERT_TRUE(!empty.StartsWith(absl::Cord("xyz")));
  ASSERT_TRUE(!x.EndsWith(absl::Cord("xyz")));
  ASSERT_TRUE(!empty.EndsWith(absl::Cord("xyz")));

  ASSERT_TRUE(x.StartsWith("abcde"));
  ASSERT_TRUE(x.StartsWith("abc"));
  ASSERT_TRUE(x.StartsWith(""));
  ASSERT_TRUE(empty.StartsWith(""));
  ASSERT_TRUE(x.EndsWith("abcde"));
  ASSERT_TRUE(x.EndsWith("cde"));
  ASSERT_TRUE(x.EndsWith(""));
  ASSERT_TRUE(empty.EndsWith(""));

  ASSERT_TRUE(!x.StartsWith("xyz"));
  ASSERT_TRUE(!empty.StartsWith("xyz"));
  ASSERT_TRUE(!x.EndsWith("xyz"));
  ASSERT_TRUE(!empty.EndsWith("xyz"));
}

TEST_P(CordTest, Contains) {
  auto flat_haystack = absl::Cord("this is a flat cord");
  auto fragmented_haystack = absl::MakeFragmentedCord(
      {"this", " ", "is", " ", "a", " ", "fragmented", " ", "cord"});

  EXPECT_TRUE(flat_haystack.Contains(""));
  EXPECT_TRUE(fragmented_haystack.Contains(""));
  EXPECT_TRUE(flat_haystack.Contains(absl::Cord("")));
  EXPECT_TRUE(fragmented_haystack.Contains(absl::Cord("")));
  EXPECT_TRUE(absl::Cord("").Contains(""));
  EXPECT_TRUE(absl::Cord("").Contains(absl::Cord("")));
  EXPECT_FALSE(absl::Cord("").Contains(flat_haystack));
  EXPECT_FALSE(absl::Cord("").Contains(fragmented_haystack));

  EXPECT_FALSE(flat_haystack.Contains("z"));
  EXPECT_FALSE(fragmented_haystack.Contains("z"));
  EXPECT_FALSE(flat_haystack.Contains(absl::Cord("z")));
  EXPECT_FALSE(fragmented_haystack.Contains(absl::Cord("z")));

  EXPECT_FALSE(flat_haystack.Contains("is an"));
  EXPECT_FALSE(fragmented_haystack.Contains("is an"));
  EXPECT_FALSE(flat_haystack.Contains(absl::Cord("is an")));
  EXPECT_FALSE(fragmented_haystack.Contains(absl::Cord("is an")));
  EXPECT_FALSE(
      flat_haystack.Contains(absl::MakeFragmentedCord({"is", " ", "an"})));
  EXPECT_FALSE(fragmented_haystack.Contains(
      absl::MakeFragmentedCord({"is", " ", "an"})));

  EXPECT_TRUE(flat_haystack.Contains("is a"));
  EXPECT_TRUE(fragmented_haystack.Contains("is a"));
  EXPECT_TRUE(flat_haystack.Contains(absl::Cord("is a")));
  EXPECT_TRUE(fragmented_haystack.Contains(absl::Cord("is a")));
  EXPECT_TRUE(
      flat_haystack.Contains(absl::MakeFragmentedCord({"is", " ", "a"})));
  EXPECT_TRUE(
      fragmented_haystack.Contains(absl::MakeFragmentedCord({"is", " ", "a"})));
}

TEST_P(CordTest, Find) {
  auto flat_haystack = absl::Cord("this is a flat cord");
  auto fragmented_haystack = absl::MakeFragmentedCord(
      {"this", " ", "is", " ", "a", " ", "fragmented", " ", "cord"});
  auto empty_haystack = absl::Cord("");

  EXPECT_EQ(flat_haystack.Find(""), flat_haystack.char_begin());
  EXPECT_EQ(fragmented_haystack.Find(""), fragmented_haystack.char_begin());
  EXPECT_EQ(flat_haystack.Find(absl::Cord("")), flat_haystack.char_begin());
  EXPECT_EQ(fragmented_haystack.Find(absl::Cord("")),
            fragmented_haystack.char_begin());
  EXPECT_EQ(empty_haystack.Find(""), empty_haystack.char_begin());
  EXPECT_EQ(empty_haystack.Find(absl::Cord("")), empty_haystack.char_begin());
  EXPECT_EQ(empty_haystack.Find(flat_haystack), empty_haystack.char_end());
  EXPECT_EQ(empty_haystack.Find(fragmented_haystack),
            empty_haystack.char_end());

  EXPECT_EQ(flat_haystack.Find("z"), flat_haystack.char_end());
  EXPECT_EQ(fragmented_haystack.Find("z"), fragmented_haystack.char_end());
  EXPECT_EQ(flat_haystack.Find(absl::Cord("z")), flat_haystack.char_end());
  EXPECT_EQ(fragmented_haystack.Find(absl::Cord("z")),
            fragmented_haystack.char_end());

  EXPECT_EQ(flat_haystack.Find("is an"), flat_haystack.char_end());
  EXPECT_EQ(fragmented_haystack.Find("is an"), fragmented_haystack.char_end());
  EXPECT_EQ(flat_haystack.Find(absl::Cord("is an")), flat_haystack.char_end());
  EXPECT_EQ(fragmented_haystack.Find(absl::Cord("is an")),
            fragmented_haystack.char_end());
  EXPECT_EQ(flat_haystack.Find(absl::MakeFragmentedCord({"is", " ", "an"})),
            flat_haystack.char_end());
  EXPECT_EQ(
      fragmented_haystack.Find(absl::MakeFragmentedCord({"is", " ", "an"})),
      fragmented_haystack.char_end());

  EXPECT_EQ(flat_haystack.Find("is a"),
            std::next(flat_haystack.char_begin(), 5));
  EXPECT_EQ(fragmented_haystack.Find("is a"),
            std::next(fragmented_haystack.char_begin(), 5));
  EXPECT_EQ(flat_haystack.Find(absl::Cord("is a")),
            std::next(flat_haystack.char_begin(), 5));
  EXPECT_EQ(fragmented_haystack.Find(absl::Cord("is a")),
            std::next(fragmented_haystack.char_begin(), 5));
  EXPECT_EQ(flat_haystack.Find(absl::MakeFragmentedCord({"is", " ", "a"})),
            std::next(flat_haystack.char_begin(), 5));
  EXPECT_EQ(
      fragmented_haystack.Find(absl::MakeFragmentedCord({"is", " ", "a"})),
      std::next(fragmented_haystack.char_begin(), 5));
}

TEST_P(CordTest, Subcord) {
  RandomEngine rng(GTEST_FLAG_GET(random_seed));
  const std::string s = RandomLowercaseString(&rng, 1024);

  absl::Cord a;
  AppendWithFragments(s, &rng, &a);
  MaybeHarden(a);
  ASSERT_EQ(s, std::string(a));

  // Check subcords of a, from a variety of interesting points.
  std::set<size_t> positions;
  for (int i = 0; i <= 32; ++i) {
    positions.insert(i);
    positions.insert(i * 32 - 1);
    positions.insert(i * 32);
    positions.insert(i * 32 + 1);
    positions.insert(a.size() - i);
  }
  positions.insert(237);
  positions.insert(732);
  for (size_t pos : positions) {
    if (pos > a.size()) continue;
    for (size_t end_pos : positions) {
      if (end_pos < pos || end_pos > a.size()) continue;
      absl::Cord sa = a.Subcord(pos, end_pos - pos);
      ASSERT_EQ(absl::string_view(s).substr(pos, end_pos - pos),
                std::string(sa))
          << a;
      if (pos != 0 || end_pos != a.size()) {
        ASSERT_EQ(sa.ExpectedChecksum(), absl::nullopt);
      }
    }
  }

  // Do the same thing for an inline cord.
  const std::string sh = "short";
  absl::Cord c(sh);
  for (size_t pos = 0; pos <= sh.size(); ++pos) {
    for (size_t n = 0; n <= sh.size() - pos; ++n) {
      absl::Cord sc = c.Subcord(pos, n);
      ASSERT_EQ(sh.substr(pos, n), std::string(sc)) << c;
    }
  }

  // Check subcords of subcords.
  absl::Cord sa = a.Subcord(0, a.size());
  std::string ss = s.substr(0, s.size());
  while (sa.size() > 1) {
    sa = sa.Subcord(1, sa.size() - 2);
    ss = ss.substr(1, ss.size() - 2);
    ASSERT_EQ(ss, std::string(sa)) << a;
    if (HasFailure()) break;  // halt cascade
  }

  // It is OK to ask for too much.
  sa = a.Subcord(0, a.size() + 1);
  EXPECT_EQ(s, std::string(sa));

  // It is OK to ask for something beyond the end.
  sa = a.Subcord(a.size() + 1, 0);
  EXPECT_TRUE(sa.empty());
  sa = a.Subcord(a.size() + 1, 1);
  EXPECT_TRUE(sa.empty());
}

TEST_P(CordTest, Swap) {
  absl::string_view a("Dexter");
  absl::string_view b("Mandark");
  absl::Cord x(a);
  absl::Cord y(b);
  MaybeHarden(x);
  swap(x, y);
  if (UseCrc()) {
    ASSERT_EQ(x.ExpectedChecksum(), absl::nullopt);
    ASSERT_EQ(y.ExpectedChecksum(), 1);
  }
  ASSERT_EQ(x, absl::Cord(b));
  ASSERT_EQ(y, absl::Cord(a));
  x.swap(y);
  if (UseCrc()) {
    ASSERT_EQ(x.ExpectedChecksum(), 1);
    ASSERT_EQ(y.ExpectedChecksum(), absl::nullopt);
  }
  ASSERT_EQ(x, absl::Cord(a));
  ASSERT_EQ(y, absl::Cord(b));
}

static void VerifyCopyToString(const absl::Cord& cord) {
  std::string initially_empty;
  absl::CopyCordToString(cord, &initially_empty);
  EXPECT_EQ(initially_empty, cord);

  constexpr size_t kInitialLength = 1024;
  std::string has_initial_contents(kInitialLength, 'x');
  const char* address_before_copy = has_initial_contents.data();
  absl::CopyCordToString(cord, &has_initial_contents);
  EXPECT_EQ(has_initial_contents, cord);

  if (cord.size() <= kInitialLength) {
    EXPECT_EQ(has_initial_contents.data(), address_before_copy)
        << "CopyCordToString allocated new string storage; "
           "has_initial_contents = \""
        << has_initial_contents << "\"";
  }
}

TEST_P(CordTest, CopyToString) {
  VerifyCopyToString(absl::Cord());  // empty cords cannot carry CRCs
  VerifyCopyToString(MaybeHardened(absl::Cord("small cord")));
  VerifyCopyToString(MaybeHardened(
      absl::MakeFragmentedCord({"fragmented ", "cord ", "to ", "test ",
                                "copying ", "to ", "a ", "string."})));
}

static void VerifyAppendCordToString(const absl::Cord& cord) {
  std::string initially_empty;
  absl::AppendCordToString(cord, &initially_empty);
  EXPECT_EQ(initially_empty, cord);

  const absl::string_view kInitialContents = "initial contents.";
  std::string expected_after_append =
      absl::StrCat(kInitialContents, std::string(cord));

  std::string no_reserve(kInitialContents);
  absl::AppendCordToString(cord, &no_reserve);
  EXPECT_EQ(no_reserve, expected_after_append);

  std::string has_reserved_capacity(kInitialContents);
  has_reserved_capacity.reserve(has_reserved_capacity.size() + cord.size());
  const char* address_before_copy = has_reserved_capacity.data();
  absl::AppendCordToString(cord, &has_reserved_capacity);
  EXPECT_EQ(has_reserved_capacity, expected_after_append);
  EXPECT_EQ(has_reserved_capacity.data(), address_before_copy)
      << "AppendCordToString allocated new string storage; "
         "has_reserved_capacity = \""
      << has_reserved_capacity << "\"";
}

TEST_P(CordTest, AppendToString) {
  VerifyAppendCordToString(absl::Cord());  // empty cords cannot carry CRCs
  VerifyAppendCordToString(MaybeHardened(absl::Cord("small cord")));
  VerifyAppendCordToString(MaybeHardened(
      absl::MakeFragmentedCord({"fragmented ", "cord ", "to ", "test ",
                                "appending ", "to ", "a ", "string."})));
}

TEST_P(CordTest, AppendEmptyBuffer) {
  absl::Cord cord;
  cord.Append(absl::CordBuffer());
  cord.Append(absl::CordBuffer::CreateWithDefaultLimit(2000));
}

TEST_P(CordTest, AppendEmptyBufferToFlat) {
  absl::Cord cord(std::string(2000, 'x'));
  cord.Append(absl::CordBuffer());
  cord.Append(absl::CordBuffer::CreateWithDefaultLimit(2000));
}

TEST_P(CordTest, AppendEmptyBufferToTree) {
  absl::Cord cord(std::string(2000, 'x'));
  cord.Append(std::string(2000, 'y'));
  cord.Append(absl::CordBuffer());
  cord.Append(absl::CordBuffer::CreateWithDefaultLimit(2000));
}

TEST_P(CordTest, AppendSmallBuffer) {
  absl::Cord cord;
  absl::CordBuffer buffer = absl::CordBuffer::CreateWithDefaultLimit(3);
  ASSERT_THAT(buffer.capacity(), Le(15));
  memcpy(buffer.data(), "Abc", 3);
  buffer.SetLength(3);
  cord.Append(std::move(buffer));
  EXPECT_EQ(buffer.length(), 0);    // NOLINT
  EXPECT_GT(buffer.capacity(), 0);  // NOLINT

  buffer = absl::CordBuffer::CreateWithDefaultLimit(3);
  memcpy(buffer.data(), "defgh", 5);
  buffer.SetLength(5);
  cord.Append(std::move(buffer));
  EXPECT_EQ(buffer.length(), 0);    // NOLINT
  EXPECT_GT(buffer.capacity(), 0);  // NOLINT

  EXPECT_THAT(cord.Chunks(), ElementsAre("Abcdefgh"));
}

TEST_P(CordTest, AppendAndPrependBufferArePrecise) {
  // Create a cord large enough to force 40KB flats.
  std::string test_data(absl::cord_internal::kMaxFlatLength * 10, 'x');
  absl::Cord cord1(test_data);
  absl::Cord cord2(test_data);
  const size_t size1 = cord1.EstimatedMemoryUsage();
  const size_t size2 = cord2.EstimatedMemoryUsage();

  absl::CordBuffer buffer = absl::CordBuffer::CreateWithDefaultLimit(3);
  memcpy(buffer.data(), "Abc", 3);
  buffer.SetLength(3);
  cord1.Append(std::move(buffer));

  buffer = absl::CordBuffer::CreateWithDefaultLimit(3);
  memcpy(buffer.data(), "Abc", 3);
  buffer.SetLength(3);
  cord2.Prepend(std::move(buffer));

#ifndef NDEBUG
  // Allow 32 bytes new CordRepFlat, and 128 bytes for 'glue nodes'
  constexpr size_t kMaxDelta = 128 + 32;
#else
  // Allow 256 bytes extra for 'allocation debug overhead'
  constexpr size_t kMaxDelta = 128 + 32 + 256;
#endif

  EXPECT_LE(cord1.EstimatedMemoryUsage() - size1, kMaxDelta);
  EXPECT_LE(cord2.EstimatedMemoryUsage() - size2, kMaxDelta);

  EXPECT_EQ(cord1, absl::StrCat(test_data, "Abc"));
  EXPECT_EQ(cord2, absl::StrCat("Abc", test_data));
}

TEST_P(CordTest, PrependSmallBuffer) {
  absl::Cord cord;
  absl::CordBuffer buffer = absl::CordBuffer::CreateWithDefaultLimit(3);
  ASSERT_THAT(buffer.capacity(), Le(15));
  memcpy(buffer.data(), "Abc", 3);
  buffer.SetLength(3);
  cord.Prepend(std::move(buffer));
  EXPECT_EQ(buffer.length(), 0);    // NOLINT
  EXPECT_GT(buffer.capacity(), 0);  // NOLINT

  buffer = absl::CordBuffer::CreateWithDefaultLimit(3);
  memcpy(buffer.data(), "defgh", 5);
  buffer.SetLength(5);
  cord.Prepend(std::move(buffer));
  EXPECT_EQ(buffer.length(), 0);    // NOLINT
  EXPECT_GT(buffer.capacity(), 0);  // NOLINT

  EXPECT_THAT(cord.Chunks(), ElementsAre("defghAbc"));
}

TEST_P(CordTest, AppendLargeBuffer) {
  absl::Cord cord;

  std::string s1(700, '1');
  absl::CordBuffer buffer = absl::CordBuffer::CreateWithDefaultLimit(s1.size());
  memcpy(buffer.data(), s1.data(), s1.size());
  buffer.SetLength(s1.size());
  cord.Append(std::move(buffer));
  EXPECT_EQ(buffer.length(), 0);    // NOLINT
  EXPECT_GT(buffer.capacity(), 0);  // NOLINT

  std::string s2(1000, '2');
  buffer = absl::CordBuffer::CreateWithDefaultLimit(s2.size());
  memcpy(buffer.data(), s2.data(), s2.size());
  buffer.SetLength(s2.size());
  cord.Append(std::move(buffer));
  EXPECT_EQ(buffer.length(), 0);    // NOLINT
  EXPECT_GT(buffer.capacity(), 0);  // NOLINT

  EXPECT_THAT(cord.Chunks(), ElementsAre(s1, s2));
}

TEST_P(CordTest, PrependLargeBuffer) {
  absl::Cord cord;

  std::string s1(700, '1');
  absl::CordBuffer buffer = absl::CordBuffer::CreateWithDefaultLimit(s1.size());
  memcpy(buffer.data(), s1.data(), s1.size());
  buffer.SetLength(s1.size());
  cord.Prepend(std::move(buffer));
  EXPECT_EQ(buffer.length(), 0);    // NOLINT
  EXPECT_GT(buffer.capacity(), 0);  // NOLINT

  std::string s2(1000, '2');
  buffer = absl::CordBuffer::CreateWithDefaultLimit(s2.size());
  memcpy(buffer.data(), s2.data(), s2.size());
  buffer.SetLength(s2.size());
  cord.Prepend(std::move(buffer));
  EXPECT_EQ(buffer.length(), 0);    // NOLINT
  EXPECT_GT(buffer.capacity(), 0);  // NOLINT

  EXPECT_THAT(cord.Chunks(), ElementsAre(s2, s1));
}

class CordAppendBufferTest : public testing::TestWithParam<bool> {
 public:
  size_t is_default() const { return GetParam(); }

  // Returns human readable string representation of the test parameter.
  static std::string ToString(testing::TestParamInfo<bool> param) {
    return param.param ? "DefaultLimit" : "CustomLimit";
  }

  size_t limit() const {
    return is_default() ? absl::CordBuffer::kDefaultLimit
                        : absl::CordBuffer::kCustomLimit;
  }

  size_t maximum_payload() const {
    return is_default() ? absl::CordBuffer::MaximumPayload()
                        : absl::CordBuffer::MaximumPayload(limit());
  }

  absl::CordBuffer GetAppendBuffer(absl::Cord& cord, size_t capacity,
                                   size_t min_capacity = 16) {
    return is_default()
               ? cord.GetAppendBuffer(capacity, min_capacity)
               : cord.GetCustomAppendBuffer(limit(), capacity, min_capacity);
  }
};

INSTANTIATE_TEST_SUITE_P(WithParam, CordAppendBufferTest, testing::Bool(),
                         CordAppendBufferTest::ToString);

TEST_P(CordAppendBufferTest, GetAppendBufferOnEmptyCord) {
  absl::Cord cord;
  absl::CordBuffer buffer = GetAppendBuffer(cord, 1000);
  EXPECT_GE(buffer.capacity(), 1000);
  EXPECT_EQ(buffer.length(), 0);
}

TEST_P(CordAppendBufferTest, GetAppendBufferOnInlinedCord) {
  static constexpr int kInlinedSize = sizeof(absl::CordBuffer) - 1;
  for (int size : {6, kInlinedSize - 3, kInlinedSize - 2, 1000}) {
    absl::Cord cord("Abc");
    absl::CordBuffer buffer = GetAppendBuffer(cord, size, 1);
    EXPECT_GE(buffer.capacity(), 3 + size);
    EXPECT_EQ(buffer.length(), 3);
    EXPECT_EQ(absl::string_view(buffer.data(), buffer.length()), "Abc");
    EXPECT_TRUE(cord.empty());
  }
}

TEST_P(CordAppendBufferTest, GetAppendBufferOnInlinedCordCapacityCloseToMax) {
  // Cover the use case where we have a non empty inlined cord with some size
  // 'n', and ask for something like 'uint64_max - k', assuming internal logic
  // could overflow on 'uint64_max - k + size', and return a valid, but
  // inefficiently smaller buffer if it would provide is the max allowed size.
  for (size_t dist_from_max = 0; dist_from_max <= 4; ++dist_from_max) {
    absl::Cord cord("Abc");
    size_t size = std::numeric_limits<size_t>::max() - dist_from_max;
    absl::CordBuffer buffer = GetAppendBuffer(cord, size, 1);
    EXPECT_GE(buffer.capacity(), maximum_payload());
    EXPECT_EQ(buffer.length(), 3);
    EXPECT_EQ(absl::string_view(buffer.data(), buffer.length()), "Abc");
    EXPECT_TRUE(cord.empty());
  }
}

TEST_P(CordAppendBufferTest, GetAppendBufferOnFlat) {
  // Create a cord with a single flat and extra capacity
  absl::Cord cord;
  absl::CordBuffer buffer = absl::CordBuffer::CreateWithDefaultLimit(500);
  const size_t expected_capacity = buffer.capacity();
  buffer.SetLength(3);
  memcpy(buffer.data(), "Abc", 3);
  cord.Append(std::move(buffer));

  buffer = GetAppendBuffer(cord, 6);
  EXPECT_EQ(buffer.capacity(), expected_capacity);
  EXPECT_EQ(buffer.length(), 3);
  EXPECT_EQ(absl::string_view(buffer.data(), buffer.length()), "Abc");
  EXPECT_TRUE(cord.empty());
}

TEST_P(CordAppendBufferTest, GetAppendBufferOnFlatWithoutMinCapacity) {
  // Create a cord with a single flat and extra capacity
  absl::Cord cord;
  absl::CordBuffer buffer = absl::CordBuffer::CreateWithDefaultLimit(500);
  buffer.SetLength(30);
  memset(buffer.data(), 'x', 30);
  cord.Append(std::move(buffer));

  buffer = GetAppendBuffer(cord, 1000, 900);
  EXPECT_GE(buffer.capacity(), 1000);
  EXPECT_EQ(buffer.length(), 0);
  EXPECT_EQ(cord, std::string(30, 'x'));
}

TEST_P(CordAppendBufferTest, GetAppendBufferOnTree) {
  RandomEngine rng;
  for (int num_flats : {2, 3, 100}) {
    // Create a cord with `num_flats` flats and extra capacity
    absl::Cord cord;
    std::string prefix;
    std::string last;
    for (int i = 0; i < num_flats - 1; ++i) {
      prefix += last;
      last = RandomLowercaseString(&rng, 10);
      absl::CordBuffer buffer = absl::CordBuffer::CreateWithDefaultLimit(500);
      buffer.SetLength(10);
      memcpy(buffer.data(), last.data(), 10);
      cord.Append(std::move(buffer));
    }
    absl::CordBuffer buffer = GetAppendBuffer(cord, 6);
    EXPECT_GE(buffer.capacity(), 500);
    EXPECT_EQ(buffer.length(), 10);
    EXPECT_EQ(absl::string_view(buffer.data(), buffer.length()), last);
    EXPECT_EQ(cord, prefix);
  }
}

TEST_P(CordAppendBufferTest, GetAppendBufferOnTreeWithoutMinCapacity) {
  absl::Cord cord;
  for (int i = 0; i < 2; ++i) {
    absl::CordBuffer buffer = absl::CordBuffer::CreateWithDefaultLimit(500);
    buffer.SetLength(3);
    memcpy(buffer.data(), i ? "def" : "Abc", 3);
    cord.Append(std::move(buffer));
  }
  absl::CordBuffer buffer = GetAppendBuffer(cord, 1000, 900);
  EXPECT_GE(buffer.capacity(), 1000);
  EXPECT_EQ(buffer.length(), 0);
  EXPECT_EQ(cord, "Abcdef");
}

TEST_P(CordAppendBufferTest, GetAppendBufferOnSubstring) {
  // Create a large cord with a single flat and some extra capacity
  absl::Cord cord;
  absl::CordBuffer buffer = absl::CordBuffer::CreateWithDefaultLimit(500);
  buffer.SetLength(450);
  memset(buffer.data(), 'x', 450);
  cord.Append(std::move(buffer));
  cord.RemovePrefix(1);

  // Deny on substring
  buffer = GetAppendBuffer(cord, 6);
  EXPECT_EQ(buffer.length(), 0);
  EXPECT_EQ(cord, std::string(449, 'x'));
}

TEST_P(CordAppendBufferTest, GetAppendBufferOnSharedCord) {
  // Create a shared cord with a single flat and extra capacity
  absl::Cord cord;
  absl::CordBuffer buffer = absl::CordBuffer::CreateWithDefaultLimit(500);
  buffer.SetLength(3);
  memcpy(buffer.data(), "Abc", 3);
  cord.Append(std::move(buffer));
  absl::Cord shared_cord = cord;

  // Deny on flat
  buffer = GetAppendBuffer(cord, 6);
  EXPECT_EQ(buffer.length(), 0);
  EXPECT_EQ(cord, "Abc");

  buffer = absl::CordBuffer::CreateWithDefaultLimit(500);
  buffer.SetLength(3);
  memcpy(buffer.data(), "def", 3);
  cord.Append(std::move(buffer));
  shared_cord = cord;

  // Deny on tree
  buffer = GetAppendBuffer(cord, 6);
  EXPECT_EQ(buffer.length(), 0);
  EXPECT_EQ(cord, "Abcdef");
}

TEST_P(CordTest, TryFlatEmpty) {
  absl::Cord c;
  EXPECT_EQ(c.TryFlat(), "");
}

TEST_P(CordTest, TryFlatFlat) {
  absl::Cord c("hello");
  MaybeHarden(c);
  EXPECT_EQ(c.TryFlat(), "hello");
}

TEST_P(CordTest, TryFlatSubstrInlined) {
  absl::Cord c("hello");
  c.RemovePrefix(1);
  MaybeHarden(c);
  EXPECT_EQ(c.TryFlat(), "ello");
}

TEST_P(CordTest, TryFlatSubstrFlat) {
  absl::Cord c("longer than 15 bytes");
  absl::Cord sub = absl::CordTestPeer::MakeSubstring(c, 1, c.size() - 1);
  MaybeHarden(sub);
  EXPECT_EQ(sub.TryFlat(), "onger than 15 bytes");
}

TEST_P(CordTest, TryFlatConcat) {
  absl::Cord c = absl::MakeFragmentedCord({"hel", "lo"});
  MaybeHarden(c);
  EXPECT_EQ(c.TryFlat(), absl::nullopt);
}

TEST_P(CordTest, TryFlatExternal) {
  absl::Cord c = absl::MakeCordFromExternal("hell", [](absl::string_view) {});
  MaybeHarden(c);
  EXPECT_EQ(c.TryFlat(), "hell");
}

TEST_P(CordTest, TryFlatSubstrExternal) {
  absl::Cord c = absl::MakeCordFromExternal("hell", [](absl::string_view) {});
  absl::Cord sub = absl::CordTestPeer::MakeSubstring(c, 1, c.size() - 1);
  MaybeHarden(sub);
  EXPECT_EQ(sub.TryFlat(), "ell");
}

TEST_P(CordTest, TryFlatCommonlyAssumedInvariants) {
  // The behavior tested below is not part of the API contract of Cord, but it's
  // something we intend to be true in our current implementation.  This test
  // exists to detect and prevent accidental breakage of the implementation.
  absl::string_view fragments[] = {"A fragmented test",
                                   " cord",
                                   " to test subcords",
                                   " of ",
                                   "a",
                                   " cord for",
                                   " each chunk "
                                   "returned by the ",
                                   "iterator"};
  absl::Cord c = absl::MakeFragmentedCord(fragments);
  MaybeHarden(c);
  int fragment = 0;
  int offset = 0;
  absl::Cord::CharIterator itc = c.char_begin();
  for (absl::string_view sv : c.Chunks()) {
    absl::string_view expected = fragments[fragment];
    absl::Cord subcord1 = c.Subcord(offset, sv.length());
    absl::Cord subcord2 = absl::Cord::AdvanceAndRead(&itc, sv.size());
    EXPECT_EQ(subcord1.TryFlat(), expected);
    EXPECT_EQ(subcord2.TryFlat(), expected);
    ++fragment;
    offset += sv.length();
  }
}

static bool IsFlat(const absl::Cord& c) {
  return c.chunk_begin() == c.chunk_end() || ++c.chunk_begin() == c.chunk_end();
}

static void VerifyFlatten(absl::Cord c) {
  std::string old_contents(c);
  absl::string_view old_flat;
  bool already_flat_and_non_empty = IsFlat(c) && !c.empty();
  if (already_flat_and_non_empty) {
    old_flat = *c.chunk_begin();
  }
  absl::string_view new_flat = c.Flatten();

  // Verify that the contents of the flattened Cord are correct.
  EXPECT_EQ(new_flat, old_contents);
  EXPECT_EQ(std::string(c), old_contents);

  // If the Cord contained data and was already flat, verify that the data
  // wasn't copied.
  if (already_flat_and_non_empty) {
    EXPECT_EQ(old_flat.data(), new_flat.data())
        << "Allocated new memory even though the Cord was already flat.";
  }

  // Verify that the flattened Cord is in fact flat.
  EXPECT_TRUE(IsFlat(c));
}

TEST_P(CordTest, Flatten) {
  VerifyFlatten(absl::Cord());
  VerifyFlatten(MaybeHardened(absl::Cord("small cord")));
  VerifyFlatten(
      MaybeHardened(absl::Cord("larger than small buffer optimization")));
  VerifyFlatten(MaybeHardened(
      absl::MakeFragmentedCord({"small ", "fragmented ", "cord"})));

  // Test with a cord that is longer than the largest flat buffer
  RandomEngine rng(GTEST_FLAG_GET(random_seed));
  VerifyFlatten(MaybeHardened(absl::Cord(RandomLowercaseString(&rng, 8192))));
}

// Test data
namespace {
class TestData {
 private:
  std::vector<std::string> data_;

  // Return a std::string of the specified length.
  static std::string MakeString(int length) {
    std::string result;
    char buf[30];
    snprintf(buf, sizeof(buf), "(%d)", length);
    while (result.size() < length) {
      result += buf;
    }
    result.resize(length);
    return result;
  }

 public:
  TestData() {
    // short strings increasing in length by one
    for (int i = 0; i < 30; i++) {
      data_.push_back(MakeString(i));
    }

    // strings around half kMaxFlatLength
    static const int kMaxFlatLength = 4096 - 9;
    static const int kHalf = kMaxFlatLength / 2;

    for (int i = -10; i <= +10; i++) {
      data_.push_back(MakeString(kHalf + i));
    }

    for (int i = -10; i <= +10; i++) {
      data_.push_back(MakeString(kMaxFlatLength + i));
    }
  }

  size_t size() const { return data_.size(); }
  const std::string& data(size_t i) const { return data_[i]; }
};
}  // namespace

TEST_P(CordTest, MultipleLengths) {
  TestData d;
  for (size_t i = 0; i < d.size(); i++) {
    std::string a = d.data(i);

    {  // Construct from Cord
      absl::Cord tmp(a);
      absl::Cord x(tmp);
      MaybeHarden(x);
      EXPECT_EQ(a, std::string(x)) << "'" << a << "'";
    }

    {  // Construct from absl::string_view
      absl::Cord x(a);
      MaybeHarden(x);
      EXPECT_EQ(a, std::string(x)) << "'" << a << "'";
    }

    {  // Append cord to self
      absl::Cord self(a);
      MaybeHarden(self);
      self.Append(self);
      EXPECT_EQ(a + a, std::string(self)) << "'" << a << "' + '" << a << "'";
    }

    {  // Prepend cord to self
      absl::Cord self(a);
      MaybeHarden(self);
      self.Prepend(self);
      EXPECT_EQ(a + a, std::string(self)) << "'" << a << "' + '" << a << "'";
    }

    // Try to append/prepend others
    for (size_t j = 0; j < d.size(); j++) {
      std::string b = d.data(j);

      {  // CopyFrom Cord
        absl::Cord x(a);
        absl::Cord y(b);
        MaybeHarden(x);
        x = y;
        EXPECT_EQ(b, std::string(x)) << "'" << a << "' + '" << b << "'";
      }

      {  // CopyFrom absl::string_view
        absl::Cord x(a);
        MaybeHarden(x);
        x = b;
        EXPECT_EQ(b, std::string(x)) << "'" << a << "' + '" << b << "'";
      }

      {  // Cord::Append(Cord)
        absl::Cord x(a);
        absl::Cord y(b);
        MaybeHarden(x);
        x.Append(y);
        EXPECT_EQ(a + b, std::string(x)) << "'" << a << "' + '" << b << "'";
      }

      {  // Cord::Append(absl::string_view)
        absl::Cord x(a);
        MaybeHarden(x);
        x.Append(b);
        EXPECT_EQ(a + b, std::string(x)) << "'" << a << "' + '" << b << "'";
      }

      {  // Cord::Prepend(Cord)
        absl::Cord x(a);
        absl::Cord y(b);
        MaybeHarden(x);
        x.Prepend(y);
        EXPECT_EQ(b + a, std::string(x)) << "'" << b << "' + '" << a << "'";
      }

      {  // Cord::Prepend(absl::string_view)
        absl::Cord x(a);
        MaybeHarden(x);
        x.Prepend(b);
        EXPECT_EQ(b + a, std::string(x)) << "'" << b << "' + '" << a << "'";
      }
    }
  }
}

namespace {

TEST_P(CordTest, RemoveSuffixWithExternalOrSubstring) {
  absl::Cord cord = absl::MakeCordFromExternal(
      "foo bar baz", [](absl::string_view s) { DoNothing(s, nullptr); });
  EXPECT_EQ("foo bar baz", std::string(cord));

  MaybeHarden(cord);

  // This RemoveSuffix() will wrap the EXTERNAL node in a SUBSTRING node.
  cord.RemoveSuffix(4);
  EXPECT_EQ("foo bar", std::string(cord));

  MaybeHarden(cord);

  // This RemoveSuffix() will adjust the SUBSTRING node in-place.
  cord.RemoveSuffix(4);
  EXPECT_EQ("foo", std::string(cord));
}

TEST_P(CordTest, RemoveSuffixMakesZeroLengthNode) {
  absl::Cord c;
  c.Append(absl::Cord(std::string(100, 'x')));
  absl::Cord other_ref = c;  // Prevent inplace appends
  EXPECT_THAT(other_ref, testing::Eq(c));
  MaybeHarden(c);
  c.Append(absl::Cord(std::string(200, 'y')));
  c.RemoveSuffix(200);
  EXPECT_EQ(std::string(100, 'x'), std::string(c));
}

}  // namespace

// CordSpliceTest contributed by hendrie.
namespace {

// Create a cord with an external memory block filled with 'z'
absl::Cord CordWithZedBlock(size_t size) {
  char* data = new char[size];
  if (size > 0) {
    memset(data, 'z', size);
  }
  absl::Cord cord = absl::MakeCordFromExternal(
      absl::string_view(data, size),
      [](absl::string_view s) { delete[] s.data(); });
  return cord;
}

// Establish that ZedBlock does what we think it does.
TEST_P(CordTest, CordSpliceTestZedBlock) {
  absl::Cord blob = CordWithZedBlock(10);
  MaybeHarden(blob);
  EXPECT_EQ(10, blob.size());
  std::string s;
  absl::CopyCordToString(blob, &s);
  EXPECT_EQ("zzzzzzzzzz", s);
}

TEST_P(CordTest, CordSpliceTestZedBlock0) {
  absl::Cord blob = CordWithZedBlock(0);
  MaybeHarden(blob);
  EXPECT_EQ(0, blob.size());
  std::string s;
  absl::CopyCordToString(blob, &s);
  EXPECT_EQ("", s);
}

TEST_P(CordTest, CordSpliceTestZedBlockSuffix1) {
  absl::Cord blob = CordWithZedBlock(10);
  MaybeHarden(blob);
  EXPECT_EQ(10, blob.size());
  absl::Cord suffix(blob);
  suffix.RemovePrefix(9);
  EXPECT_EQ(1, suffix.size());
  std::string s;
  absl::CopyCordToString(suffix, &s);
  EXPECT_EQ("z", s);
}

// Remove all of a prefix block
TEST_P(CordTest, CordSpliceTestZedBlockSuffix0) {
  absl::Cord blob = CordWithZedBlock(10);
  MaybeHarden(blob);
  EXPECT_EQ(10, blob.size());
  absl::Cord suffix(blob);
  suffix.RemovePrefix(10);
  EXPECT_EQ(0, suffix.size());
  std::string s;
  absl::CopyCordToString(suffix, &s);
  EXPECT_EQ("", s);
}

absl::Cord BigCord(size_t len, char v) {
  std::string s(len, v);
  return absl::Cord(s);
}

// Splice block into cord.
absl::Cord SpliceCord(const absl::Cord& blob, int64_t offset,
                      const absl::Cord& block) {
  CHECK_GE(offset, 0);
  CHECK_LE(static_cast<size_t>(offset) + block.size(), blob.size());
  absl::Cord result(blob);
  result.RemoveSuffix(blob.size() - offset);
  result.Append(block);
  absl::Cord suffix(blob);
  suffix.RemovePrefix(offset + block.size());
  result.Append(suffix);
  CHECK_EQ(blob.size(), result.size());
  return result;
}

// Taking an empty suffix of a block breaks appending.
TEST_P(CordTest, CordSpliceTestRemoveEntireBlock1) {
  absl::Cord zero = CordWithZedBlock(10);
  MaybeHarden(zero);
  absl::Cord suffix(zero);
  suffix.RemovePrefix(10);
  absl::Cord result;
  result.Append(suffix);
}

TEST_P(CordTest, CordSpliceTestRemoveEntireBlock2) {
  absl::Cord zero = CordWithZedBlock(10);
  MaybeHarden(zero);
  absl::Cord prefix(zero);
  prefix.RemoveSuffix(10);
  absl::Cord suffix(zero);
  suffix.RemovePrefix(10);
  absl::Cord result(prefix);
  result.Append(suffix);
}

TEST_P(CordTest, CordSpliceTestRemoveEntireBlock3) {
  absl::Cord blob = CordWithZedBlock(10);
  absl::Cord block = BigCord(10, 'b');
  MaybeHarden(blob);
  MaybeHarden(block);
  blob = SpliceCord(blob, 0, block);
}

struct CordCompareTestCase {
  template <typename LHS, typename RHS>
  CordCompareTestCase(const LHS& lhs, const RHS& rhs, bool use_crc)
      : lhs_cord(lhs), rhs_cord(rhs) {
    if (use_crc) {
      lhs_cord.SetExpectedChecksum(1);
    }
  }

  absl::Cord lhs_cord;
  absl::Cord rhs_cord;
};

const auto sign = [](int x) { return x == 0 ? 0 : (x > 0 ? 1 : -1); };

void VerifyComparison(const CordCompareTestCase& test_case) {
  std::string lhs_string(test_case.lhs_cord);
  std::string rhs_string(test_case.rhs_cord);
  int expected = sign(lhs_string.compare(rhs_string));
  EXPECT_EQ(expected, test_case.lhs_cord.Compare(test_case.rhs_cord))
      << "LHS=" << lhs_string << "; RHS=" << rhs_string;
  EXPECT_EQ(expected, test_case.lhs_cord.Compare(rhs_string))
      << "LHS=" << lhs_string << "; RHS=" << rhs_string;
  EXPECT_EQ(-expected, test_case.rhs_cord.Compare(test_case.lhs_cord))
      << "LHS=" << rhs_string << "; RHS=" << lhs_string;
  EXPECT_EQ(-expected, test_case.rhs_cord.Compare(lhs_string))
      << "LHS=" << rhs_string << "; RHS=" << lhs_string;
}

TEST_P(CordTest, Compare) {
  absl::Cord subcord("aaaaaBBBBBcccccDDDDD");
  subcord = subcord.Subcord(3, 10);

  absl::Cord tmp("aaaaaaaaaaaaaaaa");
  tmp.Append("BBBBBBBBBBBBBBBB");
  absl::Cord concat = absl::Cord("cccccccccccccccc");
  concat.Append("DDDDDDDDDDDDDDDD");
  concat.Prepend(tmp);

  absl::Cord concat2("aaaaaaaaaaaaa");
  concat2.Append("aaaBBBBBBBBBBBBBBBBccccc");
  concat2.Append("cccccccccccDDDDDDDDDDDDDD");
  concat2.Append("DD");

  const bool use_crc = UseCrc();

  std::vector<CordCompareTestCase> test_cases = {{
      // Inline cords
      {"abcdef", "abcdef", use_crc},
      {"abcdef", "abcdee", use_crc},
      {"abcdef", "abcdeg", use_crc},
      {"bbcdef", "abcdef", use_crc},
      {"bbcdef", "abcdeg", use_crc},
      {"abcdefa", "abcdef", use_crc},
      {"abcdef", "abcdefa", use_crc},

      // Small flat cords
      {"aaaaaBBBBBcccccDDDDD", "aaaaaBBBBBcccccDDDDD", use_crc},
      {"aaaaaBBBBBcccccDDDDD", "aaaaaBBBBBxccccDDDDD", use_crc},
      {"aaaaaBBBBBcxcccDDDDD", "aaaaaBBBBBcccccDDDDD", use_crc},
      {"aaaaaBBBBBxccccDDDDD", "aaaaaBBBBBcccccDDDDX", use_crc},
      {"aaaaaBBBBBcccccDDDDDa", "aaaaaBBBBBcccccDDDDD", use_crc},
      {"aaaaaBBBBBcccccDDDDD", "aaaaaBBBBBcccccDDDDDa", use_crc},

      // Subcords
      {subcord, subcord, use_crc},
      {subcord, "aaBBBBBccc", use_crc},
      {subcord, "aaBBBBBccd", use_crc},
      {subcord, "aaBBBBBccb", use_crc},
      {subcord, "aaBBBBBxcb", use_crc},
      {subcord, "aaBBBBBccca", use_crc},
      {subcord, "aaBBBBBcc", use_crc},

      // Concats
      {concat, concat, use_crc},
      {concat,
       "aaaaaaaaaaaaaaaaBBBBBBBBBBBBBBBBccccccccccccccccDDDDDDDDDDDDDDDD",
       use_crc},
      {concat,
       "aaaaaaaaaaaaaaaaBBBBBBBBBBBBBBBBcccccccccccccccxDDDDDDDDDDDDDDDD",
       use_crc},
      {concat,
       "aaaaaaaaaaaaaaaaBBBBBBBBBBBBBBBBacccccccccccccccDDDDDDDDDDDDDDDD",
       use_crc},
      {concat,
       "aaaaaaaaaaaaaaaaBBBBBBBBBBBBBBBBccccccccccccccccDDDDDDDDDDDDDDD",
       use_crc},
      {concat,
       "aaaaaaaaaaaaaaaaBBBBBBBBBBBBBBBBccccccccccccccccDDDDDDDDDDDDDDDDe",
       use_crc},

      {concat, concat2, use_crc},
  }};

  for (const auto& tc : test_cases) {
    VerifyComparison(tc);
  }
}

TEST_P(CordTest, CompareAfterAssign) {
  absl::Cord a("aaaaaa1111111");
  absl::Cord b("aaaaaa2222222");
  MaybeHarden(a);
  a = "cccccc";
  b = "cccccc";
  EXPECT_EQ(a, b);
  EXPECT_FALSE(a < b);

  a = "aaaa";
  b = "bbbbb";
  a = "";
  b = "";
  EXPECT_EQ(a, b);
  EXPECT_FALSE(a < b);
}

// Test CompareTo() and ComparePrefix() against string and substring
// comparison methods from basic_string.
static void TestCompare(const absl::Cord& c, const absl::Cord& d,
                        RandomEngine* rng) {
  // char_traits<char>::lt is guaranteed to do an unsigned comparison:
  // https://en.cppreference.com/w/cpp/string/char_traits/cmp. We also expect
  // Cord comparisons to be based on unsigned byte comparisons regardless of
  // whether char is signed.
  int expected = sign(std::string(c).compare(std::string(d)));
  EXPECT_EQ(expected, sign(c.Compare(d))) << c << ", " << d;
}

TEST_P(CordTest, CompareComparisonIsUnsigned) {
  RandomEngine rng(GTEST_FLAG_GET(random_seed));
  std::uniform_int_distribution<uint32_t> uniform_uint8(0, 255);
  char x = static_cast<char>(uniform_uint8(rng));
  TestCompare(
      absl::Cord(std::string(GetUniformRandomUpTo(&rng, 100), x)),
      absl::Cord(std::string(GetUniformRandomUpTo(&rng, 100), x ^ 0x80)), &rng);
}

TEST_P(CordTest, CompareRandomComparisons) {
  const int kIters = 5000;
  RandomEngine rng(GTEST_FLAG_GET(random_seed));

  int n = GetUniformRandomUpTo(&rng, 5000);
  absl::Cord a[] = {MakeExternalCord(n),
                    absl::Cord("ant"),
                    absl::Cord("elephant"),
                    absl::Cord("giraffe"),
                    absl::Cord(std::string(GetUniformRandomUpTo(&rng, 100),
                                           GetUniformRandomUpTo(&rng, 100))),
                    absl::Cord(""),
                    absl::Cord("x"),
                    absl::Cord("A"),
                    absl::Cord("B"),
                    absl::Cord("C")};
  for (int i = 0; i < kIters; i++) {
    absl::Cord c, d;
    for (int j = 0; j < (i % 7) + 1; j++) {
      c.Append(a[GetUniformRandomUpTo(&rng, ABSL_ARRAYSIZE(a))]);
      d.Append(a[GetUniformRandomUpTo(&rng, ABSL_ARRAYSIZE(a))]);
    }
    std::bernoulli_distribution coin_flip(0.5);
    MaybeHarden(c);
    MaybeHarden(d);
    TestCompare(coin_flip(rng) ? c : absl::Cord(std::string(c)),
                coin_flip(rng) ? d : absl::Cord(std::string(d)), &rng);
  }
}

template <typename T1, typename T2>
void CompareOperators() {
  const T1 a("a");
  const T2 b("b");

  EXPECT_TRUE(a == a);
  // For pointer type (i.e. `const char*`), operator== compares the address
  // instead of the string, so `a == const char*("a")` isn't necessarily true.
  EXPECT_TRUE(std::is_pointer<T1>::value || a == T1("a"));
  EXPECT_TRUE(std::is_pointer<T2>::value || a == T2("a"));
  EXPECT_FALSE(a == b);

  EXPECT_TRUE(a != b);
  EXPECT_FALSE(a != a);

  EXPECT_TRUE(a < b);
  EXPECT_FALSE(b < a);

  EXPECT_TRUE(b > a);
  EXPECT_FALSE(a > b);

  EXPECT_TRUE(a >= a);
  EXPECT_TRUE(b >= a);
  EXPECT_FALSE(a >= b);

  EXPECT_TRUE(a <= a);
  EXPECT_TRUE(a <= b);
  EXPECT_FALSE(b <= a);
}

TEST_P(CordTest, ComparisonOperators_Cord_Cord) {
  CompareOperators<absl::Cord, absl::Cord>();
}

TEST_P(CordTest, ComparisonOperators_Cord_StringPiece) {
  CompareOperators<absl::Cord, absl::string_view>();
}

TEST_P(CordTest, ComparisonOperators_StringPiece_Cord) {
  CompareOperators<absl::string_view, absl::Cord>();
}

TEST_P(CordTest, ComparisonOperators_Cord_string) {
  CompareOperators<absl::Cord, std::string>();
}

TEST_P(CordTest, ComparisonOperators_string_Cord) {
  CompareOperators<std::string, absl::Cord>();
}

TEST_P(CordTest, ComparisonOperators_stdstring_Cord) {
  CompareOperators<std::string, absl::Cord>();
}

TEST_P(CordTest, ComparisonOperators_Cord_stdstring) {
  CompareOperators<absl::Cord, std::string>();
}

TEST_P(CordTest, ComparisonOperators_charstar_Cord) {
  CompareOperators<const char*, absl::Cord>();
}

TEST_P(CordTest, ComparisonOperators_Cord_charstar) {
  CompareOperators<absl::Cord, const char*>();
}

TEST_P(CordTest, ConstructFromExternalReleaserInvoked) {
  // Empty external memory means the releaser should be called immediately.
  {
    bool invoked = false;
    auto releaser = [&invoked](absl::string_view) { invoked = true; };
    {
      auto c = absl::MakeCordFromExternal("", releaser);
      EXPECT_THAT(c, testing::Eq(""));
      EXPECT_TRUE(invoked);
    }
  }

  // If the size of the data is small enough, a future constructor
  // implementation may copy the bytes and immediately invoke the releaser
  // instead of creating an external node. We make a large dummy std::string to
  // make this test independent of such an optimization.
  std::string large_dummy(2048, 'c');
  {
    bool invoked = false;
    auto releaser = [&invoked](absl::string_view) { invoked = true; };
    {
      auto c = absl::MakeCordFromExternal(large_dummy, releaser);
      EXPECT_THAT(c, testing::Eq(large_dummy));
      EXPECT_FALSE(invoked);
    }
    EXPECT_TRUE(invoked);
  }

  {
    bool invoked = false;
    auto releaser = [&invoked](absl::string_view) { invoked = true; };
    {
      absl::Cord copy;
      {
        auto c = absl::MakeCordFromExternal(large_dummy, releaser);
        copy = c;
        EXPECT_FALSE(invoked);
      }
      EXPECT_FALSE(invoked);
    }
    EXPECT_TRUE(invoked);
  }
}

TEST_P(CordTest, ConstructFromExternalCompareContents) {
  RandomEngine rng(GTEST_FLAG_GET(random_seed));

  for (int length = 1; length <= 2048; length *= 2) {
    std::string data = RandomLowercaseString(&rng, length);
    auto* external = new std::string(data);
    auto cord =
        absl::MakeCordFromExternal(*external, [external](absl::string_view sv) {
          EXPECT_EQ(external->data(), sv.data());
          EXPECT_EQ(external->size(), sv.size());
          delete external;
        });
    MaybeHarden(cord);
    EXPECT_EQ(data, cord);
  }
}

TEST_P(CordTest, ConstructFromExternalLargeReleaser) {
  RandomEngine rng(GTEST_FLAG_GET(random_seed));
  constexpr size_t kLength = 256;
  std::string data = RandomLowercaseString(&rng, kLength);
  std::array<char, kLength> data_array;
  for (size_t i = 0; i < kLength; ++i) data_array[i] = data[i];
  bool invoked = false;
  auto releaser = [data_array, &invoked](absl::string_view data) {
    EXPECT_EQ(data, absl::string_view(data_array.data(), data_array.size()));
    invoked = true;
  };
  (void)MaybeHardened(absl::MakeCordFromExternal(data, releaser));
  EXPECT_TRUE(invoked);
}

TEST_P(CordTest, ConstructFromExternalFunctionPointerReleaser) {
  static absl::string_view data("hello world");
  static bool invoked;
  auto* releaser =
      static_cast<void (*)(absl::string_view)>([](absl::string_view sv) {
        EXPECT_EQ(data, sv);
        invoked = true;
      });
  invoked = false;
  (void)MaybeHardened(absl::MakeCordFromExternal(data, releaser));
  EXPECT_TRUE(invoked);

  invoked = false;
  (void)MaybeHardened(absl::MakeCordFromExternal(data, *releaser));
  EXPECT_TRUE(invoked);
}

TEST_P(CordTest, ConstructFromExternalMoveOnlyReleaser) {
  struct Releaser {
    explicit Releaser(bool* invoked) : invoked(invoked) {}
    Releaser(Releaser&& other) noexcept : invoked(other.invoked) {}
    void operator()(absl::string_view) const { *invoked = true; }

    bool* invoked;
  };

  bool invoked = false;
  (void)MaybeHardened(absl::MakeCordFromExternal("dummy", Releaser(&invoked)));
  EXPECT_TRUE(invoked);
}

TEST_P(CordTest, ConstructFromExternalNoArgLambda) {
  bool invoked = false;
  (void)MaybeHardened(
      absl::MakeCordFromExternal("dummy", [&invoked]() { invoked = true; }));
  EXPECT_TRUE(invoked);
}

TEST_P(CordTest, ConstructFromExternalStringViewArgLambda) {
  bool invoked = false;
  (void)MaybeHardened(absl::MakeCordFromExternal(
      "dummy", [&invoked](absl::string_view) { invoked = true; }));
  EXPECT_TRUE(invoked);
}

TEST_P(CordTest, ConstructFromExternalNonTrivialReleaserDestructor) {
  struct Releaser {
    explicit Releaser(bool* destroyed) : destroyed(destroyed) {}
    ~Releaser() { *destroyed = true; }
    void operator()(absl::string_view) const {}

    bool* destroyed;
  };

  bool destroyed = false;
  Releaser releaser(&destroyed);
  (void)MaybeHardened(absl::MakeCordFromExternal("dummy", releaser));
  EXPECT_TRUE(destroyed);
}

TEST_P(CordTest, ConstructFromExternalReferenceQualifierOverloads) {
  enum InvokedAs { kMissing, kLValue, kRValue };
  enum CopiedAs { kNone, kMove, kCopy };
  struct Tracker {
    CopiedAs copied_as = kNone;
    InvokedAs invoked_as = kMissing;

    void Record(InvokedAs rhs) {
      ASSERT_EQ(invoked_as, kMissing);
      invoked_as = rhs;
    }

    void Record(CopiedAs rhs) {
      if (copied_as == kNone || rhs == kCopy) copied_as = rhs;
    }
  } tracker;

  class Releaser {
   public:
    explicit Releaser(Tracker* tracker) : tr_(tracker) { *tracker = Tracker(); }
    Releaser(Releaser&& rhs) : tr_(rhs.tr_) { tr_->Record(kMove); }
    Releaser(const Releaser& rhs) : tr_(rhs.tr_) { tr_->Record(kCopy); }

    void operator()(absl::string_view) & { tr_->Record(kLValue); }
    void operator()(absl::string_view) && { tr_->Record(kRValue); }

   private:
    Tracker* tr_;
  };

  const Releaser releaser1(&tracker);
  (void)MaybeHardened(absl::MakeCordFromExternal("", releaser1));
  EXPECT_EQ(tracker.copied_as, kCopy);
  EXPECT_EQ(tracker.invoked_as, kRValue);

  const Releaser releaser2(&tracker);
  (void)MaybeHardened(absl::MakeCordFromExternal("", releaser2));
  EXPECT_EQ(tracker.copied_as, kCopy);
  EXPECT_EQ(tracker.invoked_as, kRValue);

  Releaser releaser3(&tracker);
  (void)MaybeHardened(absl::MakeCordFromExternal("", std::move(releaser3)));
  EXPECT_EQ(tracker.copied_as, kMove);
  EXPECT_EQ(tracker.invoked_as, kRValue);

  Releaser releaser4(&tracker);
  (void)MaybeHardened(absl::MakeCordFromExternal("dummy", releaser4));
  EXPECT_EQ(tracker.copied_as, kCopy);
  EXPECT_EQ(tracker.invoked_as, kRValue);

  const Releaser releaser5(&tracker);
  (void)MaybeHardened(absl::MakeCordFromExternal("dummy", releaser5));
  EXPECT_EQ(tracker.copied_as, kCopy);
  EXPECT_EQ(tracker.invoked_as, kRValue);

  Releaser releaser6(&tracker);
  (void)MaybeHardened(absl::MakeCordFromExternal("foo", std::move(releaser6)));
  EXPECT_EQ(tracker.copied_as, kMove);
  EXPECT_EQ(tracker.invoked_as, kRValue);
}

TEST_P(CordTest, ExternalMemoryBasicUsage) {
  static const char* strings[] = {"", "hello", "there"};
  for (const char* str : strings) {
    absl::Cord dst("(prefix)");
    MaybeHarden(dst);
    AddExternalMemory(str, &dst);
    MaybeHarden(dst);
    dst.Append("(suffix)");
    EXPECT_EQ((std::string("(prefix)") + str + std::string("(suffix)")),
              std::string(dst));
  }
}

TEST_P(CordTest, ExternalMemoryRemovePrefixSuffix) {
  // Exhaustively try all sub-strings.
  absl::Cord cord = MakeComposite();
  std::string s = std::string(cord);
  for (int offset = 0; offset <= s.size(); offset++) {
    for (int length = 0; length <= s.size() - offset; length++) {
      absl::Cord result(cord);
      MaybeHarden(result);
      result.RemovePrefix(offset);
      MaybeHarden(result);
      result.RemoveSuffix(result.size() - length);
      EXPECT_EQ(s.substr(offset, length), std::string(result))
          << offset << " " << length;
    }
  }
}

TEST_P(CordTest, ExternalMemoryGet) {
  absl::Cord cord("hello");
  AddExternalMemory(" world!", &cord);
  MaybeHarden(cord);
  AddExternalMemory(" how are ", &cord);
  cord.Append(" you?");
  MaybeHarden(cord);
  std::string s = std::string(cord);
  for (int i = 0; i < s.size(); i++) {
    EXPECT_EQ(s[i], cord[i]);
  }
}

// CordMemoryUsage tests verify the correctness of the EstimatedMemoryUsage()
// We use whiteboxed expectations based on our knowledge of the layout and size
// of empty and inlined cords, and flat nodes.

constexpr auto kFairShare = absl::CordMemoryAccounting::kFairShare;
constexpr auto kTotalMorePrecise =
    absl::CordMemoryAccounting::kTotalMorePrecise;

// Creates a cord of `n` `c` values, making sure no string stealing occurs.
absl::Cord MakeCord(size_t n, char c) {
  const std::string s(n, c);
  return absl::Cord(s);
}

TEST(CordTest, CordMemoryUsageEmpty) {
  absl::Cord cord;
  EXPECT_EQ(sizeof(absl::Cord), cord.EstimatedMemoryUsage());
  EXPECT_EQ(sizeof(absl::Cord), cord.EstimatedMemoryUsage(kFairShare));
  EXPECT_EQ(sizeof(absl::Cord), cord.EstimatedMemoryUsage(kTotalMorePrecise));
}

TEST(CordTest, CordMemoryUsageInlined) {
  absl::Cord a("hello");
  EXPECT_EQ(a.EstimatedMemoryUsage(), sizeof(absl::Cord));
  EXPECT_EQ(a.EstimatedMemoryUsage(kFairShare), sizeof(absl::Cord));
  EXPECT_EQ(a.EstimatedMemoryUsage(kTotalMorePrecise), sizeof(absl::Cord));
}

TEST(CordTest, CordMemoryUsageExternalMemory) {
  absl::Cord cord;
  AddExternalMemory(std::string(1000, 'x'), &cord);
  const size_t expected =
      sizeof(absl::Cord) + 1000 + sizeof(CordRepExternal) + sizeof(intptr_t);
  EXPECT_EQ(cord.EstimatedMemoryUsage(), expected);
  EXPECT_EQ(cord.EstimatedMemoryUsage(kFairShare), expected);
  EXPECT_EQ(cord.EstimatedMemoryUsage(kTotalMorePrecise), expected);
}

TEST(CordTest, CordMemoryUsageFlat) {
  absl::Cord cord = MakeCord(1000, 'a');
  const size_t flat_size =
      absl::CordTestPeer::Tree(cord)->flat()->AllocatedSize();
  EXPECT_EQ(cord.EstimatedMemoryUsage(), sizeof(absl::Cord) + flat_size);
  EXPECT_EQ(cord.EstimatedMemoryUsage(kFairShare),
            sizeof(absl::Cord) + flat_size);
  EXPECT_EQ(cord.EstimatedMemoryUsage(kTotalMorePrecise),
            sizeof(absl::Cord) + flat_size);
}

TEST(CordTest, CordMemoryUsageSubStringSharedFlat) {
  absl::Cord flat = MakeCord(2000, 'a');
  const size_t flat_size =
      absl::CordTestPeer::Tree(flat)->flat()->AllocatedSize();
  absl::Cord cord = flat.Subcord(500, 1000);
  EXPECT_EQ(cord.EstimatedMemoryUsage(),
            sizeof(absl::Cord) + sizeof(CordRepSubstring) + flat_size);
  EXPECT_EQ(cord.EstimatedMemoryUsage(kTotalMorePrecise),
            sizeof(absl::Cord) + sizeof(CordRepSubstring) + flat_size);
  EXPECT_EQ(cord.EstimatedMemoryUsage(kFairShare),
            sizeof(absl::Cord) + sizeof(CordRepSubstring) + flat_size / 2);
}

TEST(CordTest, CordMemoryUsageFlatShared) {
  absl::Cord shared = MakeCord(1000, 'a');
  absl::Cord cord(shared);
  const size_t flat_size =
      absl::CordTestPeer::Tree(cord)->flat()->AllocatedSize();
  EXPECT_EQ(cord.EstimatedMemoryUsage(), sizeof(absl::Cord) + flat_size);
  EXPECT_EQ(cord.EstimatedMemoryUsage(kTotalMorePrecise),
            sizeof(absl::Cord) + flat_size);
  EXPECT_EQ(cord.EstimatedMemoryUsage(kFairShare),
            sizeof(absl::Cord) + flat_size / 2);
}

TEST(CordTest, CordMemoryUsageFlatHardenedAndShared) {
  absl::Cord shared = MakeCord(1000, 'a');
  absl::Cord cord(shared);
  const size_t flat_size =
      absl::CordTestPeer::Tree(cord)->flat()->AllocatedSize();
  cord.SetExpectedChecksum(1);
  EXPECT_EQ(cord.EstimatedMemoryUsage(),
            sizeof(absl::Cord) + sizeof(CordRepCrc) + flat_size);
  EXPECT_EQ(cord.EstimatedMemoryUsage(kFairShare),
            sizeof(absl::Cord) + sizeof(CordRepCrc) + flat_size / 2);

  absl::Cord cord2(cord);
  EXPECT_EQ(cord2.EstimatedMemoryUsage(),
            sizeof(absl::Cord) + sizeof(CordRepCrc) + flat_size);
  EXPECT_EQ(cord2.EstimatedMemoryUsage(kTotalMorePrecise),
            sizeof(absl::Cord) + sizeof(CordRepCrc) + flat_size);
  EXPECT_EQ(cord2.EstimatedMemoryUsage(kFairShare),
            sizeof(absl::Cord) + (sizeof(CordRepCrc) + flat_size / 2) / 2);
}

TEST(CordTest, CordMemoryUsageBTree) {
  absl::Cord cord1;
  size_t flats1_size = 0;
  absl::Cord flats1[4] = {MakeCord(1000, 'a'), MakeCord(1100, 'a'),
                          MakeCord(1200, 'a'), MakeCord(1300, 'a')};
  for (absl::Cord flat : flats1) {
    flats1_size += absl::CordTestPeer::Tree(flat)->flat()->AllocatedSize();
    cord1.Append(std::move(flat));
  }

  // Make sure the created cord is a BTREE tree. Under some builds such as
  // windows DLL, we may have ODR like effects on the flag, meaning the DLL
  // code will run with the picked up default.
  if (!absl::CordTestPeer::Tree(cord1)->IsBtree()) {
    LOG(WARNING) << "Cord library code not respecting btree flag";
    return;
  }

  size_t rep1_size = sizeof(CordRepBtree) + flats1_size;
  size_t rep1_shared_size = sizeof(CordRepBtree) + flats1_size / 2;

  EXPECT_EQ(cord1.EstimatedMemoryUsage(), sizeof(absl::Cord) + rep1_size);
  EXPECT_EQ(cord1.EstimatedMemoryUsage(kTotalMorePrecise),
            sizeof(absl::Cord) + rep1_size);
  EXPECT_EQ(cord1.EstimatedMemoryUsage(kFairShare),
            sizeof(absl::Cord) + rep1_shared_size);

  absl::Cord cord2;
  size_t flats2_size = 0;
  absl::Cord flats2[4] = {MakeCord(600, 'a'), MakeCord(700, 'a'),
                          MakeCord(800, 'a'), MakeCord(900, 'a')};
  for (absl::Cord& flat : flats2) {
    flats2_size += absl::CordTestPeer::Tree(flat)->flat()->AllocatedSize();
    cord2.Append(std::move(flat));
  }
  size_t rep2_size = sizeof(CordRepBtree) + flats2_size;

  EXPECT_EQ(cord2.EstimatedMemoryUsage(), sizeof(absl::Cord) + rep2_size);
  EXPECT_EQ(cord2.EstimatedMemoryUsage(kTotalMorePrecise),
            sizeof(absl::Cord) + rep2_size);
  EXPECT_EQ(cord2.EstimatedMemoryUsage(kFairShare),
            sizeof(absl::Cord) + rep2_size);

  absl::Cord cord(cord1);
  cord.Append(std::move(cord2));

  EXPECT_EQ(cord.EstimatedMemoryUsage(),
            sizeof(absl::Cord) + sizeof(CordRepBtree) + rep1_size + rep2_size);
  EXPECT_EQ(cord.EstimatedMemoryUsage(kTotalMorePrecise),
            sizeof(absl::Cord) + sizeof(CordRepBtree) + rep1_size + rep2_size);
  EXPECT_EQ(cord.EstimatedMemoryUsage(kFairShare),
            sizeof(absl::Cord) + sizeof(CordRepBtree) + rep1_shared_size / 2 +
                rep2_size);
}

TEST(CordTest, TestHashFragmentation) {
  // Make sure we hit these boundary cases precisely.
  EXPECT_EQ(1024, absl::hash_internal::PiecewiseChunkSize());
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly({
      absl::Cord(),
      absl::MakeFragmentedCord({std::string(600, 'a'), std::string(600, 'a')}),
      absl::MakeFragmentedCord({std::string(1200, 'a')}),
      absl::MakeFragmentedCord({std::string(900, 'b'), std::string(900, 'b')}),
      absl::MakeFragmentedCord({std::string(1800, 'b')}),
      absl::MakeFragmentedCord(
          {std::string(2000, 'c'), std::string(2000, 'c')}),
      absl::MakeFragmentedCord({std::string(4000, 'c')}),
      absl::MakeFragmentedCord({std::string(1024, 'd')}),
      absl::MakeFragmentedCord({std::string(1023, 'd'), "d"}),
      absl::MakeFragmentedCord({std::string(1025, 'e')}),
      absl::MakeFragmentedCord({std::string(1024, 'e'), "e"}),
      absl::MakeFragmentedCord({std::string(1023, 'e'), "e", "e"}),
  }));
}

// Regtest for a change that had to be rolled back because it expanded out
// of the InlineRep too soon, which was observable through MemoryUsage().
TEST_P(CordTest, CordMemoryUsageInlineRep) {
  constexpr size_t kMaxInline = 15;  // Cord::InlineRep::N
  const std::string small_string(kMaxInline, 'x');
  absl::Cord c1(small_string);

  absl::Cord c2;
  c2.Append(small_string);
  EXPECT_EQ(c1, c2);
  EXPECT_EQ(c1.EstimatedMemoryUsage(), c2.EstimatedMemoryUsage());
}

TEST_P(CordTest, CordMemoryUsageTotalMorePreciseMode) {
  constexpr size_t kChunkSize = 2000;
  std::string tmp_str(kChunkSize, 'x');
  const absl::Cord flat(std::move(tmp_str));

  // Construct `fragmented` with two references into the same
  // underlying buffer shared with `flat`:
  absl::Cord fragmented(flat);
  fragmented.Append(flat);

  // Memory usage of `flat`, minus the top-level Cord object:
  const size_t flat_internal_usage =
      flat.EstimatedMemoryUsage() - sizeof(absl::Cord);

  // `fragmented` holds a Cord and a CordRepBtree. That tree points to two
  // copies of flat's internals, which we expect to dedup:
  EXPECT_EQ(fragmented.EstimatedMemoryUsage(kTotalMorePrecise),
            sizeof(absl::Cord) +
            sizeof(CordRepBtree) +
            flat_internal_usage);

  // This is a case where kTotal produces an overestimate:
  EXPECT_EQ(fragmented.EstimatedMemoryUsage(),
            sizeof(absl::Cord) +
            sizeof(CordRepBtree) +
            2 * flat_internal_usage);
}

TEST_P(CordTest, CordMemoryUsageTotalMorePreciseModeWithSubstring) {
  constexpr size_t kChunkSize = 2000;
  std::string tmp_str(kChunkSize, 'x');
  const absl::Cord flat(std::move(tmp_str));

  // Construct `fragmented` with two references into the same
  // underlying buffer shared with `flat`.
  //
  // This time, each reference is through a Subcord():
  absl::Cord fragmented;
  fragmented.Append(flat.Subcord(1, kChunkSize - 2));
  fragmented.Append(flat.Subcord(1, kChunkSize - 2));

  // Memory usage of `flat`, minus the top-level Cord object:
  const size_t flat_internal_usage =
      flat.EstimatedMemoryUsage() - sizeof(absl::Cord);

  // `fragmented` holds a Cord and a CordRepBtree. That tree points to two
  // CordRepSubstrings, each pointing at flat's internals.
  EXPECT_EQ(fragmented.EstimatedMemoryUsage(kTotalMorePrecise),
            sizeof(absl::Cord) +
            sizeof(CordRepBtree) +
            2 * sizeof(CordRepSubstring) +
            flat_internal_usage);

  // This is a case where kTotal produces an overestimate:
  EXPECT_EQ(fragmented.EstimatedMemoryUsage(),
            sizeof(absl::Cord) +
            sizeof(CordRepBtree) +
            2 * sizeof(CordRepSubstring) +
            2 * flat_internal_usage);
}
}  // namespace

// Regtest for 7510292 (fix a bug introduced by 7465150)
TEST_P(CordTest, Concat_Append) {
  // Create a rep of type CONCAT
  absl::Cord s1("foobarbarbarbarbar");
  MaybeHarden(s1);
  s1.Append("abcdefgabcdefgabcdefgabcdefgabcdefgabcdefgabcdefg");
  size_t size = s1.size();

  // Create a copy of s1 and append to it.
  absl::Cord s2 = s1;
  MaybeHarden(s2);
  s2.Append("x");

  // 7465150 modifies s1 when it shouldn't.
  EXPECT_EQ(s1.size(), size);
  EXPECT_EQ(s2.size(), size + 1);
}

TEST_P(CordTest, DiabolicalGrowth) {
  // This test exercises a diabolical Append(<one char>) on a cord, making the
  // cord shared before each Append call resulting in a terribly fragmented
  // resulting cord.
  RandomEngine rng(GTEST_FLAG_GET(random_seed));
  const std::string expected = RandomLowercaseString(&rng, 5000);
  absl::Cord cord;
  for (char c : expected) {
    absl::Cord shared(cord);
    EXPECT_THAT(cord, testing::Eq(shared));
    cord.Append(absl::string_view(&c, 1));
    MaybeHarden(cord);
  }
  std::string value;
  absl::CopyCordToString(cord, &value);
  EXPECT_EQ(value, expected);
  LOG(INFO) << "Diabolical size allocated = " << cord.EstimatedMemoryUsage();
}

// The following tests check support for >4GB cords in 64-bit binaries, and
// 2GB-4GB cords in 32-bit binaries.  This function returns the large cord size
// that's appropriate for the binary.

// Construct a huge cord with the specified valid prefix.
static absl::Cord MakeHuge(absl::string_view prefix) {
  absl::Cord cord;
  if (sizeof(size_t) > 4) {
    // In 64-bit binaries, test 64-bit Cord support.
    const size_t size =
        static_cast<size_t>(std::numeric_limits<uint32_t>::max()) + 314;
    cord.Append(absl::MakeCordFromExternal(
        absl::string_view(prefix.data(), size),
        [](absl::string_view s) { DoNothing(s, nullptr); }));
  } else {
    // Cords are limited to 32-bit lengths in 32-bit binaries.  The following
    // tests check for use of "signed int" to represent Cord length/offset.
    // However absl::string_view does not allow lengths >= (1u<<31), so we need
    // to append in two parts;
    const size_t s1 = (1u << 31) - 1;
    // For shorter cord, `Append` copies the data rather than allocating a new
    // node. The threshold is currently set to 511, so `s2` needs to be bigger
    // to not trigger the copy.
    const size_t s2 = 600;
    cord.Append(absl::MakeCordFromExternal(
        absl::string_view(prefix.data(), s1),
        [](absl::string_view s) { DoNothing(s, nullptr); }));
    cord.Append(absl::MakeCordFromExternal(
        absl::string_view("", s2),
        [](absl::string_view s) { DoNothing(s, nullptr); }));
  }
  return cord;
}

TEST_P(CordTest, HugeCord) {
  absl::Cord cord = MakeHuge("huge cord");
  MaybeHarden(cord);

  const size_t acceptable_delta =
      100 + (UseCrc() ? sizeof(absl::cord_internal::CordRepCrc) : 0);
  EXPECT_LE(cord.size(), cord.EstimatedMemoryUsage());
  EXPECT_GE(cord.size() + acceptable_delta, cord.EstimatedMemoryUsage());
}

// Tests that Append() works ok when handed a self reference
TEST_P(CordTest, AppendSelf) {
  // Test the empty case.
  absl::Cord empty;
  MaybeHarden(empty);
  empty.Append(empty);
  ASSERT_EQ(empty, "");

  // We run the test until data is ~16K
  // This guarantees it covers small, medium and large data.
  std::string control_data = "Abc";
  absl::Cord data(control_data);
  while (control_data.length() < 0x4000) {
    MaybeHarden(data);
    data.Append(data);
    control_data.append(control_data);
    ASSERT_EQ(control_data, data);
  }
}

TEST_P(CordTest, MakeFragmentedCordFromInitializerList) {
  absl::Cord fragmented =
      absl::MakeFragmentedCord({"A ", "fragmented ", "Cord"});

  MaybeHarden(fragmented);

  EXPECT_EQ("A fragmented Cord", fragmented);

  auto chunk_it = fragmented.chunk_begin();

  ASSERT_TRUE(chunk_it != fragmented.chunk_end());
  EXPECT_EQ("A ", *chunk_it);

  ASSERT_TRUE(++chunk_it != fragmented.chunk_end());
  EXPECT_EQ("fragmented ", *chunk_it);

  ASSERT_TRUE(++chunk_it != fragmented.chunk_end());
  EXPECT_EQ("Cord", *chunk_it);

  ASSERT_TRUE(++chunk_it == fragmented.chunk_end());
}

TEST_P(CordTest, MakeFragmentedCordFromVector) {
  std::vector<absl::string_view> chunks = {"A ", "fragmented ", "Cord"};
  absl::Cord fragmented = absl::MakeFragmentedCord(chunks);

  MaybeHarden(fragmented);

  EXPECT_EQ("A fragmented Cord", fragmented);

  auto chunk_it = fragmented.chunk_begin();

  ASSERT_TRUE(chunk_it != fragmented.chunk_end());
  EXPECT_EQ("A ", *chunk_it);

  ASSERT_TRUE(++chunk_it != fragmented.chunk_end());
  EXPECT_EQ("fragmented ", *chunk_it);

  ASSERT_TRUE(++chunk_it != fragmented.chunk_end());
  EXPECT_EQ("Cord", *chunk_it);

  ASSERT_TRUE(++chunk_it == fragmented.chunk_end());
}

TEST_P(CordTest, CordChunkIteratorTraits) {
  static_assert(std::is_copy_constructible<absl::Cord::ChunkIterator>::value,
                "");
  static_assert(std::is_copy_assignable<absl::Cord::ChunkIterator>::value, "");

  // Move semantics to satisfy swappable via std::swap
  static_assert(std::is_move_constructible<absl::Cord::ChunkIterator>::value,
                "");
  static_assert(std::is_move_assignable<absl::Cord::ChunkIterator>::value, "");

  static_assert(
      std::is_same<
          std::iterator_traits<absl::Cord::ChunkIterator>::iterator_category,
          std::input_iterator_tag>::value,
      "");
  static_assert(
      std::is_same<std::iterator_traits<absl::Cord::ChunkIterator>::value_type,
                   absl::string_view>::value,
      "");
  static_assert(
      std::is_same<
          std::iterator_traits<absl::Cord::ChunkIterator>::difference_type,
          ptrdiff_t>::value,
      "");
  static_assert(
      std::is_same<std::iterator_traits<absl::Cord::ChunkIterator>::pointer,
                   const absl::string_view*>::value,
      "");
  static_assert(
      std::is_same<std::iterator_traits<absl::Cord::ChunkIterator>::reference,
                   absl::string_view>::value,
      "");
}

static void VerifyChunkIterator(const absl::Cord& cord,
                                size_t expected_chunks) {
  EXPECT_EQ(cord.chunk_begin() == cord.chunk_end(), cord.empty()) << cord;
  EXPECT_EQ(cord.chunk_begin() != cord.chunk_end(), !cord.empty());

  absl::Cord::ChunkRange range = cord.Chunks();
  EXPECT_EQ(range.begin() == range.end(), cord.empty());
  EXPECT_EQ(range.begin() != range.end(), !cord.empty());

  std::string content(cord);
  size_t pos = 0;
  auto pre_iter = cord.chunk_begin(), post_iter = cord.chunk_begin();
  size_t n_chunks = 0;
  while (pre_iter != cord.chunk_end() && post_iter != cord.chunk_end()) {
    EXPECT_FALSE(pre_iter == cord.chunk_end());   // NOLINT: explicitly test ==
    EXPECT_FALSE(post_iter == cord.chunk_end());  // NOLINT

    EXPECT_EQ(pre_iter, post_iter);
    EXPECT_EQ(*pre_iter, *post_iter);

    EXPECT_EQ(pre_iter->data(), (*pre_iter).data());
    EXPECT_EQ(pre_iter->size(), (*pre_iter).size());

    absl::string_view chunk = *pre_iter;
    EXPECT_FALSE(chunk.empty());
    EXPECT_LE(pos + chunk.size(), content.size());
    EXPECT_EQ(absl::string_view(content.c_str() + pos, chunk.size()), chunk);

    int n_equal_iterators = 0;
    for (absl::Cord::ChunkIterator it = range.begin(); it != range.end();
         ++it) {
      n_equal_iterators += static_cast<int>(it == pre_iter);
    }
    EXPECT_EQ(n_equal_iterators, 1);

    ++pre_iter;
    EXPECT_EQ(*post_iter++, chunk);

    pos += chunk.size();
    ++n_chunks;
  }
  EXPECT_EQ(expected_chunks, n_chunks);
  EXPECT_EQ(pos, content.size());
  EXPECT_TRUE(pre_iter == cord.chunk_end());   // NOLINT: explicitly test ==
  EXPECT_TRUE(post_iter == cord.chunk_end());  // NOLINT
}

TEST_P(CordTest, CordChunkIteratorOperations) {
  absl::Cord empty_cord;
  VerifyChunkIterator(empty_cord, 0);

  absl::Cord small_buffer_cord("small cord");
  MaybeHarden(small_buffer_cord);
  VerifyChunkIterator(small_buffer_cord, 1);

  absl::Cord flat_node_cord("larger than small buffer optimization");
  MaybeHarden(flat_node_cord);
  VerifyChunkIterator(flat_node_cord, 1);

  VerifyChunkIterator(MaybeHardened(absl::MakeFragmentedCord(
                          {"a ", "small ", "fragmented ", "cord ", "for ",
                           "testing ", "chunk ", "iterations."})),
                      8);

  absl::Cord reused_nodes_cord(std::string(40, 'c'));
  reused_nodes_cord.Prepend(absl::Cord(std::string(40, 'b')));
  MaybeHarden(reused_nodes_cord);
  reused_nodes_cord.Prepend(absl::Cord(std::string(40, 'a')));
  size_t expected_chunks = 3;
  for (int i = 0; i < 8; ++i) {
    reused_nodes_cord.Prepend(reused_nodes_cord);
    MaybeHarden(reused_nodes_cord);
    expected_chunks *= 2;
    VerifyChunkIterator(reused_nodes_cord, expected_chunks);
  }

  RandomEngine rng(GTEST_FLAG_GET(random_seed));
  absl::Cord flat_cord(RandomLowercaseString(&rng, 256));
  absl::Cord subcords;
  for (int i = 0; i < 128; ++i) subcords.Prepend(flat_cord.Subcord(i, 128));
  VerifyChunkIterator(subcords, 128);
}


TEST_P(CordTest, AdvanceAndReadOnDataEdge) {
  RandomEngine rng(GTEST_FLAG_GET(random_seed));
  const std::string data = RandomLowercaseString(&rng, 2000);
  for (bool as_flat : {true, false}) {
    SCOPED_TRACE(as_flat ? "Flat" : "External");

    absl::Cord cord =
        as_flat ? absl::Cord(data)
                : absl::MakeCordFromExternal(data, [](absl::string_view) {});
    auto it = cord.Chars().begin();
#if !defined(NDEBUG) || ABSL_OPTION_HARDENED
    EXPECT_DEATH_IF_SUPPORTED(cord.AdvanceAndRead(&it, 2001), ".*");
#endif

    it = cord.Chars().begin();
    absl::Cord frag = cord.AdvanceAndRead(&it, 2000);
    EXPECT_EQ(frag, data);
    EXPECT_TRUE(it == cord.Chars().end());

    it = cord.Chars().begin();
    frag = cord.AdvanceAndRead(&it, 200);
    EXPECT_EQ(frag, data.substr(0, 200));
    EXPECT_FALSE(it == cord.Chars().end());

    frag = cord.AdvanceAndRead(&it, 1500);
    EXPECT_EQ(frag, data.substr(200, 1500));
    EXPECT_FALSE(it == cord.Chars().end());

    frag = cord.AdvanceAndRead(&it, 300);
    EXPECT_EQ(frag, data.substr(1700, 300));
    EXPECT_TRUE(it == cord.Chars().end());
  }
}

TEST_P(CordTest, AdvanceAndReadOnSubstringDataEdge) {
  RandomEngine rng(GTEST_FLAG_GET(random_seed));
  const std::string data = RandomLowercaseString(&rng, 2500);
  for (bool as_flat : {true, false}) {
    SCOPED_TRACE(as_flat ? "Flat" : "External");

    absl::Cord cord =
        as_flat ? absl::Cord(data)
                : absl::MakeCordFromExternal(data, [](absl::string_view) {});
    cord = cord.Subcord(200, 2000);
    const std::string substr = data.substr(200, 2000);

    auto it = cord.Chars().begin();
#if !defined(NDEBUG) || ABSL_OPTION_HARDENED
    EXPECT_DEATH_IF_SUPPORTED(cord.AdvanceAndRead(&it, 2001), ".*");
#endif

    it = cord.Chars().begin();
    absl::Cord frag = cord.AdvanceAndRead(&it, 2000);
    EXPECT_EQ(frag, substr);
    EXPECT_TRUE(it == cord.Chars().end());

    it = cord.Chars().begin();
    frag = cord.AdvanceAndRead(&it, 200);
    EXPECT_EQ(frag, substr.substr(0, 200));
    EXPECT_FALSE(it == cord.Chars().end());

    frag = cord.AdvanceAndRead(&it, 1500);
    EXPECT_EQ(frag, substr.substr(200, 1500));
    EXPECT_FALSE(it == cord.Chars().end());

    frag = cord.AdvanceAndRead(&it, 300);
    EXPECT_EQ(frag, substr.substr(1700, 300));
    EXPECT_TRUE(it == cord.Chars().end());
  }
}

TEST_P(CordTest, CharIteratorTraits) {
  static_assert(std::is_copy_constructible<absl::Cord::CharIterator>::value,
                "");
  static_assert(std::is_copy_assignable<absl::Cord::CharIterator>::value, "");

  // Move semantics to satisfy swappable via std::swap
  static_assert(std::is_move_constructible<absl::Cord::CharIterator>::value,
                "");
  static_assert(std::is_move_assignable<absl::Cord::CharIterator>::value, "");

  static_assert(
      std::is_same<
          std::iterator_traits<absl::Cord::CharIterator>::iterator_category,
          std::input_iterator_tag>::value,
      "");
  static_assert(
      std::is_same<std::iterator_traits<absl::Cord::CharIterator>::value_type,
                   char>::value,
      "");
  static_assert(
      std::is_same<
          std::iterator_traits<absl::Cord::CharIterator>::difference_type,
          ptrdiff_t>::value,
      "");
  static_assert(
      std::is_same<std::iterator_traits<absl::Cord::CharIterator>::pointer,
                   const char*>::value,
      "");
  static_assert(
      std::is_same<std::iterator_traits<absl::Cord::CharIterator>::reference,
                   const char&>::value,
      "");
}

static void VerifyCharIterator(const absl::Cord& cord) {
  EXPECT_EQ(cord.char_begin() == cord.char_end(), cord.empty());
  EXPECT_EQ(cord.char_begin() != cord.char_end(), !cord.empty());

  absl::Cord::CharRange range = cord.Chars();
  EXPECT_EQ(range.begin() == range.end(), cord.empty());
  EXPECT_EQ(range.begin() != range.end(), !cord.empty());

  size_t i = 0;
  absl::Cord::CharIterator pre_iter = cord.char_begin();
  absl::Cord::CharIterator post_iter = cord.char_begin();
  std::string content(cord);
  while (pre_iter != cord.char_end() && post_iter != cord.char_end()) {
    EXPECT_FALSE(pre_iter == cord.char_end());   // NOLINT: explicitly test ==
    EXPECT_FALSE(post_iter == cord.char_end());  // NOLINT

    EXPECT_LT(i, cord.size());
    EXPECT_EQ(content[i], *pre_iter);

    EXPECT_EQ(pre_iter, post_iter);
    EXPECT_EQ(*pre_iter, *post_iter);
    EXPECT_EQ(&*pre_iter, &*post_iter);

    const char* character_address = &*pre_iter;
    absl::Cord::CharIterator copy = pre_iter;
    ++copy;
    EXPECT_EQ(character_address, &*pre_iter);

    int n_equal_iterators = 0;
    for (absl::Cord::CharIterator it = range.begin(); it != range.end(); ++it) {
      n_equal_iterators += static_cast<int>(it == pre_iter);
    }
    EXPECT_EQ(n_equal_iterators, 1);

    absl::Cord::CharIterator advance_iter = range.begin();
    absl::Cord::Advance(&advance_iter, i);
    EXPECT_EQ(pre_iter, advance_iter);

    advance_iter = range.begin();
    EXPECT_EQ(absl::Cord::AdvanceAndRead(&advance_iter, i), cord.Subcord(0, i));
    EXPECT_EQ(pre_iter, advance_iter);

    advance_iter = pre_iter;
    absl::Cord::Advance(&advance_iter, cord.size() - i);
    EXPECT_EQ(range.end(), advance_iter);

    advance_iter = pre_iter;
    EXPECT_EQ(absl::Cord::AdvanceAndRead(&advance_iter, cord.size() - i),
              cord.Subcord(i, cord.size() - i));
    EXPECT_EQ(range.end(), advance_iter);

    ++i;
    ++pre_iter;
    post_iter++;
  }
  EXPECT_EQ(i, cord.size());
  EXPECT_TRUE(pre_iter == cord.char_end());   // NOLINT: explicitly test ==
  EXPECT_TRUE(post_iter == cord.char_end());  // NOLINT

  absl::Cord::CharIterator zero_advanced_end = cord.char_end();
  absl::Cord::Advance(&zero_advanced_end, 0);
  EXPECT_EQ(zero_advanced_end, cord.char_end());

  absl::Cord::CharIterator it = cord.char_begin();
  for (absl::string_view chunk : cord.Chunks()) {
    while (!chunk.empty()) {
      EXPECT_EQ(absl::Cord::ChunkRemaining(it), chunk);
      chunk.remove_prefix(1);
      ++it;
    }
  }
}

TEST_P(CordTest, CharIteratorOperations) {
  absl::Cord empty_cord;
  VerifyCharIterator(empty_cord);

  absl::Cord small_buffer_cord("small cord");
  MaybeHarden(small_buffer_cord);
  VerifyCharIterator(small_buffer_cord);

  absl::Cord flat_node_cord("larger than small buffer optimization");
  MaybeHarden(flat_node_cord);
  VerifyCharIterator(flat_node_cord);

  VerifyCharIterator(MaybeHardened(
      absl::MakeFragmentedCord({"a ", "small ", "fragmented ", "cord ", "for ",
                                "testing ", "character ", "iteration."})));

  absl::Cord reused_nodes_cord("ghi");
  reused_nodes_cord.Prepend(absl::Cord("def"));
  reused_nodes_cord.Prepend(absl::Cord("abc"));
  for (int i = 0; i < 4; ++i) {
    reused_nodes_cord.Prepend(reused_nodes_cord);
    MaybeHarden(reused_nodes_cord);
    VerifyCharIterator(reused_nodes_cord);
  }

  RandomEngine rng(GTEST_FLAG_GET(random_seed));
  absl::Cord flat_cord(RandomLowercaseString(&rng, 256));
  absl::Cord subcords;
  for (int i = 0; i < 4; ++i) {
    subcords.Prepend(flat_cord.Subcord(16 * i, 128));
    MaybeHarden(subcords);
  }
  VerifyCharIterator(subcords);
}

TEST_P(CordTest, CharIteratorAdvanceAndRead) {
  // Create a Cord holding 6 flats of 2500 bytes each, and then iterate over it
  // reading 150, 1500, 2500 and 3000 bytes. This will result in all possible
  // partial, full and straddled read combinations including reads below
  // kMaxBytesToCopy. b/197776822 surfaced a bug for a specific partial, small
  // read 'at end' on Cord which caused a failure on attempting to read past the
  // end in CordRepBtreeReader which was not covered by any existing test.
  constexpr int kBlocks = 6;
  constexpr size_t kBlockSize = 2500;
  constexpr size_t kChunkSize1 = 1500;
  constexpr size_t kChunkSize2 = 2500;
  constexpr size_t kChunkSize3 = 3000;
  constexpr size_t kChunkSize4 = 150;
  RandomEngine rng;
  std::string data = RandomLowercaseString(&rng, kBlocks * kBlockSize);
  absl::Cord cord;
  for (int i = 0; i < kBlocks; ++i) {
    const std::string block = data.substr(i * kBlockSize, kBlockSize);
    cord.Append(absl::Cord(block));
  }

  MaybeHarden(cord);

  for (size_t chunk_size :
       {kChunkSize1, kChunkSize2, kChunkSize3, kChunkSize4}) {
    absl::Cord::CharIterator it = cord.char_begin();
    size_t offset = 0;
    while (offset < data.length()) {
      const size_t n = std::min<size_t>(data.length() - offset, chunk_size);
      absl::Cord chunk = cord.AdvanceAndRead(&it, n);
      ASSERT_EQ(chunk.size(), n);
      ASSERT_EQ(chunk.Compare(data.substr(offset, n)), 0);
      offset += n;
    }
  }
}

TEST_P(CordTest, StreamingOutput) {
  absl::Cord c =
      absl::MakeFragmentedCord({"A ", "small ", "fragmented ", "Cord", "."});
  MaybeHarden(c);
  std::stringstream output;
  output << c;
  EXPECT_EQ("A small fragmented Cord.", output.str());
}

TEST_P(CordTest, ForEachChunk) {
  for (int num_elements : {1, 10, 200}) {
    SCOPED_TRACE(num_elements);
    std::vector<std::string> cord_chunks;
    for (int i = 0; i < num_elements; ++i) {
      cord_chunks.push_back(absl::StrCat("[", i, "]"));
    }
    absl::Cord c = absl::MakeFragmentedCord(cord_chunks);
    MaybeHarden(c);

    std::vector<std::string> iterated_chunks;
    absl::CordTestPeer::ForEachChunk(c,
                                     [&iterated_chunks](absl::string_view sv) {
                                       iterated_chunks.emplace_back(sv);
                                     });
    EXPECT_EQ(iterated_chunks, cord_chunks);
  }
}

TEST_P(CordTest, SmallBufferAssignFromOwnData) {
  constexpr size_t kMaxInline = 15;
  std::string contents = "small buff cord";
  EXPECT_EQ(contents.size(), kMaxInline);
  for (size_t pos = 0; pos < contents.size(); ++pos) {
    for (size_t count = contents.size() - pos; count > 0; --count) {
      absl::Cord c(contents);
      MaybeHarden(c);
      absl::string_view flat = c.Flatten();
      c = flat.substr(pos, count);
      EXPECT_EQ(c, contents.substr(pos, count))
          << "pos = " << pos << "; count = " << count;
    }
  }
}

TEST_P(CordTest, Format) {
  absl::Cord c;
  absl::Format(&c, "There were %04d little %s.", 3, "pigs");
  EXPECT_EQ(c, "There were 0003 little pigs.");
  MaybeHarden(c);
  absl::Format(&c, "And %-3llx bad wolf!", 1);
  MaybeHarden(c);
  EXPECT_EQ(c, "There were 0003 little pigs.And 1   bad wolf!");
}

TEST_P(CordTest, Stringify) {
  absl::Cord c =
      absl::MakeFragmentedCord({"A ", "small ", "fragmented ", "Cord", "."});
  MaybeHarden(c);
  EXPECT_EQ(absl::StrCat(c), "A small fragmented Cord.");
}

TEST_P(CordTest, Hardening) {
  absl::Cord cord("hello");
  MaybeHarden(cord);

  // These statement should abort the program in all builds modes.
  EXPECT_DEATH_IF_SUPPORTED(cord.RemovePrefix(6), "");
  EXPECT_DEATH_IF_SUPPORTED(cord.RemoveSuffix(6), "");

  bool test_hardening = false;
  ABSL_HARDENING_ASSERT([&]() {
    // This only runs when ABSL_HARDENING_ASSERT is active.
    test_hardening = true;
    return true;
  }());
  if (!test_hardening) return;

  EXPECT_DEATH_IF_SUPPORTED(cord[5], "");
  EXPECT_DEATH_IF_SUPPORTED(*cord.chunk_end(), "");
  EXPECT_DEATH_IF_SUPPORTED(static_cast<void>(cord.chunk_end()->empty()), "");
  EXPECT_DEATH_IF_SUPPORTED(++cord.chunk_end(), "");
}

// This test mimics a specific (and rare) application repeatedly splitting a
// cord, inserting (overwriting) a string value, and composing a new cord from
// the three pieces. This is hostile towards a Btree implementation: A split of
// a node at any level is likely to have the right-most edge of the left split,
// and the left-most edge of the right split shared. For example, splitting a
// leaf node with 6 edges will result likely in a 1-6, 2-5, 3-4, etc. split,
// sharing the 'split node'. When recomposing such nodes, we 'injected' an edge
// in that node. As this happens with some probability on each level of the
// tree, this will quickly grow the tree until it reaches maximum height.
TEST_P(CordTest, BtreeHostileSplitInsertJoin) {
  absl::BitGen bitgen;

  // Start with about 1GB of data
  std::string data(1 << 10, 'x');
  absl::Cord buffer(data);
  absl::Cord cord;
  for (int i = 0; i < 1000000; ++i) {
    cord.Append(buffer);
  }

  for (int j = 0; j < 1000; ++j) {
    MaybeHarden(cord);
    size_t offset = absl::Uniform(bitgen, 0u, cord.size());
    size_t length = absl::Uniform(bitgen, 100u, data.size());
    if (cord.size() == offset) {
      cord.Append(absl::string_view(data.data(), length));
    } else {
      absl::Cord suffix;
      if (offset + length < cord.size()) {
        suffix = cord;
        suffix.RemovePrefix(offset + length);
      }
      if (cord.size() > offset) {
        cord.RemoveSuffix(cord.size() - offset);
      }
      cord.Append(absl::string_view(data.data(), length));
      if (!suffix.empty()) {
        cord.Append(suffix);
      }
    }
  }
}

class AfterExitCordTester {
 public:
  bool Set(absl::Cord* cord, absl::string_view expected) {
    cord_ = cord;
    expected_ = expected;
    return true;
  }

  ~AfterExitCordTester() {
    EXPECT_EQ(*cord_, expected_);
  }
 private:
  absl::Cord* cord_;
  absl::string_view expected_;
};

template <typename Str>
void TestAfterExit(Str) {
  const auto expected = Str::value;
  // Defined before `cord` to be destroyed after it.
  static AfterExitCordTester exit_tester;  // NOLINT
  static absl::NoDestructor<absl::Cord> cord_leaker(Str{});
  // cord_leaker is static, so this reference will remain valid through the end
  // of program execution.
  static absl::Cord& cord = *cord_leaker;
  static bool init_exit_tester = exit_tester.Set(&cord, expected);
  (void)init_exit_tester;

  EXPECT_EQ(cord, expected);
  // Copy the object and test the copy, and the original.
  {
    absl::Cord copy = cord;
    EXPECT_EQ(copy, expected);
  }
  // The original still works
  EXPECT_EQ(cord, expected);

  // Try making adding more structure to the tree.
  {
    absl::Cord copy = cord;
    std::string expected_copy(expected);
    for (int i = 0; i < 10; ++i) {
      copy.Append(cord);
      absl::StrAppend(&expected_copy, expected);
      EXPECT_EQ(copy, expected_copy);
    }
  }

  // Make sure we are using the right branch during constant evaluation.
  EXPECT_EQ(absl::CordTestPeer::IsTree(cord), cord.size() >= 16);

  for (int i = 0; i < 10; ++i) {
    // Make a few more Cords from the same global rep.
    // This tests what happens when the refcount for it gets below 1.
    EXPECT_EQ(expected, absl::Cord(Str{}));
  }
}

constexpr int SimpleStrlen(const char* p) {
  return *p ? 1 + SimpleStrlen(p + 1) : 0;
}

struct ShortView {
  constexpr absl::string_view operator()() const {
    return absl::string_view("SSO string", SimpleStrlen("SSO string"));
  }
};

struct LongView {
  constexpr absl::string_view operator()() const {
    return absl::string_view("String that does not fit SSO.",
                             SimpleStrlen("String that does not fit SSO."));
  }
};


TEST_P(CordTest, AfterExit) {
  TestAfterExit(absl::strings_internal::MakeStringConstant(ShortView{}));
  TestAfterExit(absl::strings_internal::MakeStringConstant(LongView{}));
}

namespace {

// Test helper that generates a populated cord for future manipulation.
//
// By test convention, all generated cords begin with the characters "abcde" at
// the start of the first chunk.
class PopulatedCordFactory {
 public:
  constexpr PopulatedCordFactory(absl::string_view name,
                                 absl::Cord (*generator)())
      : name_(name), generator_(generator) {}

  absl::string_view Name() const { return name_; }
  absl::Cord Generate() const { return generator_(); }

 private:
  absl::string_view name_;
  absl::Cord (*generator_)();
};

// clang-format off
// This array is constant-initialized in conformant compilers.
PopulatedCordFactory cord_factories[] = {
  {"sso", [] { return absl::Cord("abcde"); }},
  {"flat", [] {
    // Too large to live in SSO space, but small enough to be a simple FLAT.
    absl::Cord flat(absl::StrCat("abcde", std::string(1000, 'x')));
    flat.Flatten();
    return flat;
  }},
  {"external", [] {
    // A cheat: we are using a string literal as the external storage, so a
    // no-op releaser is correct here.
    return absl::MakeCordFromExternal("abcde External!", []{});
  }},
  {"external substring", [] {
    // A cheat: we are using a string literal as the external storage, so a
    // no-op releaser is correct here.
    absl::Cord ext = absl::MakeCordFromExternal("-abcde External!", []{});
    return absl::CordTestPeer::MakeSubstring(ext, 1, ext.size() - 1);
  }},
  {"substring", [] {
    absl::Cord flat(absl::StrCat("-abcde", std::string(1000, 'x')));
    flat.Flatten();
    return flat.Subcord(1, 998);
  }},
  {"fragmented", [] {
    std::string fragment = absl::StrCat("abcde", std::string(195, 'x'));
    std::vector<std::string> fragments(200, fragment);
    absl::Cord cord = absl::MakeFragmentedCord(fragments);
    assert(cord.size() == 40000);
    return cord;
  }},
};
// clang-format on

// Test helper that can mutate a cord, and possibly undo the mutation, for
// testing.
class CordMutator {
 public:
  constexpr CordMutator(absl::string_view name, void (*mutate)(absl::Cord&),
                        void (*undo)(absl::Cord&) = nullptr)
      : name_(name), mutate_(mutate), undo_(undo) {}

  absl::string_view Name() const { return name_; }
  void Mutate(absl::Cord& cord) const { mutate_(cord); }
  bool CanUndo() const { return undo_ != nullptr; }
  void Undo(absl::Cord& cord) const { undo_(cord); }

 private:
  absl::string_view name_;
  void (*mutate_)(absl::Cord&);
  void (*undo_)(absl::Cord&);
};

// clang-format off
// This array is constant-initialized in conformant compilers.
CordMutator cord_mutators[] = {
  {"clear", [](absl::Cord& c) { c.Clear(); }},
  {"overwrite", [](absl::Cord& c) { c = "overwritten"; }},
  {
    "append string",
    [](absl::Cord& c) { c.Append("0123456789"); },
    [](absl::Cord& c) { c.RemoveSuffix(10); }
  },
  {
    "append cord",
    [](absl::Cord& c) {
      c.Append(absl::MakeFragmentedCord({"12345", "67890"}));
    },
    [](absl::Cord& c) { c.RemoveSuffix(10); }
  },
  {
    "append checksummed cord",
    [](absl::Cord& c) {
      absl::Cord to_append = absl::MakeFragmentedCord({"12345", "67890"});
      to_append.SetExpectedChecksum(999);
      c.Append(to_append);
    },
    [](absl::Cord& c) { c.RemoveSuffix(10); }
  },
  {
    "append self",
    [](absl::Cord& c) { c.Append(c); },
    [](absl::Cord& c) { c.RemoveSuffix(c.size() / 2); }
  },
  {
    "append empty string",
    [](absl::Cord& c) { c.Append(""); },
    [](absl::Cord& c) { }
  },
  {
    "append empty cord",
    [](absl::Cord& c) { c.Append(absl::Cord()); },
    [](absl::Cord& c) { }
  },
  {
    "append empty checksummed cord",
    [](absl::Cord& c) {
      absl::Cord to_append;
      to_append.SetExpectedChecksum(999);
      c.Append(to_append);
    },
    [](absl::Cord& c) { }
  },
  {
    "prepend string",
    [](absl::Cord& c) { c.Prepend("9876543210"); },
    [](absl::Cord& c) { c.RemovePrefix(10); }
  },
  {
    "prepend cord",
    [](absl::Cord& c) {
      c.Prepend(absl::MakeFragmentedCord({"98765", "43210"}));
    },
    [](absl::Cord& c) { c.RemovePrefix(10); }
  },
  {
    "prepend checksummed cord",
    [](absl::Cord& c) {
      absl::Cord to_prepend = absl::MakeFragmentedCord({"98765", "43210"});
      to_prepend.SetExpectedChecksum(999);
      c.Prepend(to_prepend);
    },
    [](absl::Cord& c) { c.RemovePrefix(10); }
  },
  {
    "prepend empty string",
    [](absl::Cord& c) { c.Prepend(""); },
    [](absl::Cord& c) { }
  },
  {
    "prepend empty cord",
    [](absl::Cord& c) { c.Prepend(absl::Cord()); },
    [](absl::Cord& c) { }
  },
  {
    "prepend empty checksummed cord",
    [](absl::Cord& c) {
      absl::Cord to_prepend;
      to_prepend.SetExpectedChecksum(999);
      c.Prepend(to_prepend);
    },
    [](absl::Cord& c) { }
  },
  {
    "prepend self",
    [](absl::Cord& c) { c.Prepend(c); },
    [](absl::Cord& c) { c.RemovePrefix(c.size() / 2); }
  },
  {"remove prefix", [](absl::Cord& c) { c.RemovePrefix(c.size() / 2); }},
  {"remove suffix", [](absl::Cord& c) { c.RemoveSuffix(c.size() / 2); }},
  {"remove 0-prefix", [](absl::Cord& c) { c.RemovePrefix(0); }},
  {"remove 0-suffix", [](absl::Cord& c) { c.RemoveSuffix(0); }},
  {"subcord", [](absl::Cord& c) { c = c.Subcord(1, c.size() - 2); }},
  {
    "swap inline",
    [](absl::Cord& c) {
      absl::Cord other("swap");
      c.swap(other);
    }
  },
  {
    "swap tree",
    [](absl::Cord& c) {
      absl::Cord other(std::string(10000, 'x'));
      c.swap(other);
    }
  },
};
// clang-format on
}  // namespace

TEST_P(CordTest, ExpectedChecksum) {
  for (const PopulatedCordFactory& factory : cord_factories) {
    SCOPED_TRACE(factory.Name());
    for (bool shared : {false, true}) {
      SCOPED_TRACE(shared);

      absl::Cord shared_cord_source = factory.Generate();
      auto make_instance = [=] {
        return shared ? shared_cord_source : factory.Generate();
      };

      const absl::Cord base_value = factory.Generate();
      const std::string base_value_as_string(factory.Generate().Flatten());

      absl::Cord c1 = make_instance();
      EXPECT_FALSE(c1.ExpectedChecksum().has_value());

      // Setting an expected checksum works, and retains the cord's bytes
      c1.SetExpectedChecksum(12345);
      EXPECT_EQ(c1.ExpectedChecksum().value_or(0), 12345);
      EXPECT_EQ(c1, base_value);

      // Test that setting an expected checksum again doesn't crash or leak
      // memory.
      c1.SetExpectedChecksum(12345);
      EXPECT_EQ(c1.ExpectedChecksum().value_or(0), 12345);
      EXPECT_EQ(c1, base_value);

      // CRC persists through copies, assignments, and moves:
      absl::Cord c1_copy_construct = c1;
      EXPECT_EQ(c1_copy_construct.ExpectedChecksum().value_or(0), 12345);

      absl::Cord c1_copy_assign;
      c1_copy_assign = c1;
      EXPECT_EQ(c1_copy_assign.ExpectedChecksum().value_or(0), 12345);

      absl::Cord c1_move(std::move(c1_copy_assign));
      EXPECT_EQ(c1_move.ExpectedChecksum().value_or(0), 12345);

      EXPECT_EQ(c1.ExpectedChecksum().value_or(0), 12345);

      // A CRC Cord compares equal to its non-CRC value.
      EXPECT_EQ(c1, make_instance());

      for (const CordMutator& mutator : cord_mutators) {
        SCOPED_TRACE(mutator.Name());

        // Test that mutating a cord removes its stored checksum
        absl::Cord c2 = make_instance();
        c2.SetExpectedChecksum(24680);

        mutator.Mutate(c2);

        if (c1 == c2) {
          // Not a mutation (for example, appending the empty string).
          // Whether the checksum is removed is not defined.
          continue;
        }

        EXPECT_EQ(c2.ExpectedChecksum(), absl::nullopt);

        if (mutator.CanUndo()) {
          // Undoing an operation should not restore the checksum
          mutator.Undo(c2);
          EXPECT_EQ(c2, base_value);
          EXPECT_EQ(c2.ExpectedChecksum(), absl::nullopt);
        }
      }

      absl::Cord c3 = make_instance();
      c3.SetExpectedChecksum(999);
      const absl::Cord& cc3 = c3;

      // Test that all cord reading operations function in the face of an
      // expected checksum.

      // Test data precondition
      ASSERT_TRUE(cc3.StartsWith("abcde"));

      EXPECT_EQ(cc3.size(), base_value_as_string.size());
      EXPECT_FALSE(cc3.empty());
      EXPECT_EQ(cc3.Compare(base_value), 0);
      EXPECT_EQ(cc3.Compare(base_value_as_string), 0);
      EXPECT_EQ(cc3.Compare("wxyz"), -1);
      EXPECT_EQ(cc3.Compare(absl::Cord("wxyz")), -1);
      EXPECT_EQ(cc3.Compare("aaaa"), 1);
      EXPECT_EQ(cc3.Compare(absl::Cord("aaaa")), 1);
      EXPECT_EQ(absl::Cord("wxyz").Compare(cc3), 1);
      EXPECT_EQ(absl::Cord("aaaa").Compare(cc3), -1);
      EXPECT_TRUE(cc3.StartsWith("abcd"));
      EXPECT_EQ(std::string(cc3), base_value_as_string);

      std::string dest;
      absl::CopyCordToString(cc3, &dest);
      EXPECT_EQ(dest, base_value_as_string);

      bool first_pass = true;
      for (absl::string_view chunk : cc3.Chunks()) {
        if (first_pass) {
          EXPECT_TRUE(absl::StartsWith(chunk, "abcde"));
        }
        first_pass = false;
      }
      first_pass = true;
      for (char ch : cc3.Chars()) {
        if (first_pass) {
          EXPECT_EQ(ch, 'a');
        }
        first_pass = false;
      }
      EXPECT_TRUE(absl::StartsWith(*cc3.chunk_begin(), "abcde"));
      EXPECT_EQ(*cc3.char_begin(), 'a');

      auto char_it = cc3.char_begin();
      absl::Cord::Advance(&char_it, 2);
      EXPECT_EQ(absl::Cord::AdvanceAndRead(&char_it, 2), "cd");
      EXPECT_EQ(*char_it, 'e');
      char_it = cc3.char_begin();
      absl::Cord::Advance(&char_it, 2);
      EXPECT_TRUE(absl::StartsWith(absl::Cord::ChunkRemaining(char_it), "cde"));

      EXPECT_EQ(cc3[0], 'a');
      EXPECT_EQ(cc3[4], 'e');
      EXPECT_EQ(absl::HashOf(cc3), absl::HashOf(base_value));
      EXPECT_EQ(absl::HashOf(cc3), absl::HashOf(base_value_as_string));
    }
  }
}

// Test the special cases encountered with an empty checksummed cord.
TEST_P(CordTest, ChecksummedEmptyCord) {
  absl::Cord c1;
  EXPECT_FALSE(c1.ExpectedChecksum().has_value());

  // Setting an expected checksum works.
  c1.SetExpectedChecksum(12345);
  EXPECT_EQ(c1.ExpectedChecksum().value_or(0), 12345);
  EXPECT_EQ(c1, "");
  EXPECT_TRUE(c1.empty());

  // Test that setting an expected checksum again doesn't crash or leak memory.
  c1.SetExpectedChecksum(12345);
  EXPECT_EQ(c1.ExpectedChecksum().value_or(0), 12345);
  EXPECT_EQ(c1, "");
  EXPECT_TRUE(c1.empty());

  // CRC persists through copies, assignments, and moves:
  absl::Cord c1_copy_construct = c1;
  EXPECT_EQ(c1_copy_construct.ExpectedChecksum().value_or(0), 12345);

  absl::Cord c1_copy_assign;
  c1_copy_assign = c1;
  EXPECT_EQ(c1_copy_assign.ExpectedChecksum().value_or(0), 12345);

  absl::Cord c1_move(std::move(c1_copy_assign));
  EXPECT_EQ(c1_move.ExpectedChecksum().value_or(0), 12345);

  EXPECT_EQ(c1.ExpectedChecksum().value_or(0), 12345);

  // A CRC Cord compares equal to its non-CRC value.
  EXPECT_EQ(c1, absl::Cord());

  for (const CordMutator& mutator : cord_mutators) {
    SCOPED_TRACE(mutator.Name());

    // Exercise mutating an empty checksummed cord to catch crashes and exercise
    // memory sanitizers.
    absl::Cord c2;
    c2.SetExpectedChecksum(24680);
    mutator.Mutate(c2);

    if (c2.empty()) {
      // Not a mutation
      continue;
    }
    EXPECT_EQ(c2.ExpectedChecksum(), absl::nullopt);

    if (mutator.CanUndo()) {
      mutator.Undo(c2);
    }
  }

  absl::Cord c3;
  c3.SetExpectedChecksum(999);
  const absl::Cord& cc3 = c3;

  // Test that all cord reading operations function in the face of an
  // expected checksum.
  EXPECT_TRUE(cc3.StartsWith(""));
  EXPECT_TRUE(cc3.EndsWith(""));
  EXPECT_TRUE(cc3.empty());
  EXPECT_EQ(cc3, "");
  EXPECT_EQ(cc3, absl::Cord());
  EXPECT_EQ(cc3.size(), 0);
  EXPECT_EQ(cc3.Compare(absl::Cord()), 0);
  EXPECT_EQ(cc3.Compare(c1), 0);
  EXPECT_EQ(cc3.Compare(cc3), 0);
  EXPECT_EQ(cc3.Compare(""), 0);
  EXPECT_EQ(cc3.Compare("wxyz"), -1);
  EXPECT_EQ(cc3.Compare(absl::Cord("wxyz")), -1);
  EXPECT_EQ(absl::Cord("wxyz").Compare(cc3), 1);
  EXPECT_EQ(std::string(cc3), "");

  std::string dest;
  absl::CopyCordToString(cc3, &dest);
  EXPECT_EQ(dest, "");

  for (absl::string_view chunk : cc3.Chunks()) {  // NOLINT(unreachable loop)
    static_cast<void>(chunk);
    GTEST_FAIL() << "no chunks expected";
  }
  EXPECT_TRUE(cc3.chunk_begin() == cc3.chunk_end());

  for (char ch : cc3.Chars()) {  // NOLINT(unreachable loop)
    static_cast<void>(ch);
    GTEST_FAIL() << "no chars expected";
  }
  EXPECT_TRUE(cc3.char_begin() == cc3.char_end());

  EXPECT_EQ(cc3.TryFlat(), "");
  EXPECT_EQ(absl::HashOf(c3), absl::HashOf(absl::Cord()));
  EXPECT_EQ(absl::HashOf(c3), absl::HashOf(absl::string_view()));
}

// This must not be static to avoid aggressive optimizations.
ABSL_ATTRIBUTE_WEAK
size_t FalseReport(const absl::Cord& a, bool f);

ABSL_ATTRIBUTE_NOINLINE
size_t FalseReport(const absl::Cord& a, bool f) {
  absl::Cord b;
  const absl::Cord& ref = f ? b : a;
  // Test that sanitizers report nothing here. Without
  // InlineData::Rep::annotated_this() compiler can unconditionally load
  // poisoned parts, assuming that local variable is fully accessible.
  return ref.size();
}

TEST(CordSanitizerTest, SanitizesCordFalseReport) {
  absl::Cord c;
  for (int i = 0; i < 1000; ++i) c.Append("a");
  FalseReport(c, false);
}

TEST(CrcCordTest, ChecksummedEmptyCordEstimateMemoryUsage) {
  absl::Cord cord;
  cord.SetExpectedChecksum(0);
  EXPECT_NE(cord.EstimatedMemoryUsage(), 0);
}

TEST(CordThreeWayComparisonTest, CompareCords) {
#ifndef __cpp_impl_three_way_comparison
  GTEST_SKIP() << "C++20 three-way <=> comparison not supported";
#else
  EXPECT_EQ(absl::Cord("a") <=> absl::Cord("a"), std::strong_ordering::equal);
  EXPECT_EQ(absl::Cord("aaaa") <=> absl::Cord("aaab"),
            std::strong_ordering::less);
  EXPECT_EQ(absl::Cord("baaa") <=> absl::Cord("a"),
            std::strong_ordering::greater);
#endif
}

TEST(CordThreeWayComparisonTest, CompareCordsAndStringViews) {
#ifndef __cpp_impl_three_way_comparison
  GTEST_SKIP() << "C++20 three-way <=> comparison not supported";
#else
  EXPECT_EQ(absl::string_view("a") <=> absl::Cord("a"),
            std::strong_ordering::equal);
  EXPECT_EQ(absl::Cord("a") <=> absl::string_view("b"),
            std::strong_ordering::less);
  EXPECT_EQ(absl::string_view("b") <=> absl::Cord("a"),
            std::strong_ordering::greater);
#endif
}

#if defined(GTEST_HAS_DEATH_TEST) && defined(ABSL_INTERNAL_CORD_HAVE_SANITIZER)

// Returns an expected poison / uninitialized death message expression.
const char* MASanDeathExpr() {
  return "(use-after-poison|use-of-uninitialized-value)";
}

TEST(CordSanitizerTest, SanitizesEmptyCord) {
  absl::Cord cord;
  const char* data = cord.Flatten().data();
  EXPECT_DEATH(EXPECT_EQ(data[0], 0), MASanDeathExpr());
}

TEST(CordSanitizerTest, SanitizesSmallCord) {
  absl::Cord cord("Hello");
  const char* data = cord.Flatten().data();
  EXPECT_DEATH(EXPECT_EQ(data[5], 0), MASanDeathExpr());
}

TEST(CordSanitizerTest, SanitizesCordOnSetSSOValue) {
  absl::Cord cord("String that is too big to be an SSO value");
  cord = "Hello";
  const char* data = cord.Flatten().data();
  EXPECT_DEATH(EXPECT_EQ(data[5], 0), MASanDeathExpr());
}

TEST(CordSanitizerTest, SanitizesCordOnCopyCtor) {
  absl::Cord src("hello");
  absl::Cord dst(src);
  const char* data = dst.Flatten().data();
  EXPECT_DEATH(EXPECT_EQ(data[5], 0), MASanDeathExpr());
}

TEST(CordSanitizerTest, SanitizesCordOnMoveCtor) {
  absl::Cord src("hello");
  absl::Cord dst(std::move(src));
  const char* data = dst.Flatten().data();
  EXPECT_DEATH(EXPECT_EQ(data[5], 0), MASanDeathExpr());
}

TEST(CordSanitizerTest, SanitizesCordOnAssign) {
  absl::Cord src("hello");
  absl::Cord dst;
  dst = src;
  const char* data = dst.Flatten().data();
  EXPECT_DEATH(EXPECT_EQ(data[5], 0), MASanDeathExpr());
}

TEST(CordSanitizerTest, SanitizesCordOnMoveAssign) {
  absl::Cord src("hello");
  absl::Cord dst;
  dst = std::move(src);
  const char* data = dst.Flatten().data();
  EXPECT_DEATH(EXPECT_EQ(data[5], 0), MASanDeathExpr());
}

TEST(CordSanitizerTest, SanitizesCordOnSsoAssign) {
  absl::Cord src("hello");
  absl::Cord dst("String that is too big to be an SSO value");
  dst = src;
  const char* data = dst.Flatten().data();
  EXPECT_DEATH(EXPECT_EQ(data[5], 0), MASanDeathExpr());
}

#endif  // GTEST_HAS_DEATH_TEST && ABSL_INTERNAL_CORD_HAVE_SANITIZER
