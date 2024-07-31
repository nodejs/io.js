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

#include "absl/random/internal/pcg_engine.h"

#include <algorithm>
#include <bitset>
#include <random>
#include <sstream>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/random/internal/explicit_seed_seq.h"
#include "absl/time/clock.h"

#define UPDATE_GOLDEN 0

namespace {

using absl::random_internal::ExplicitSeedSeq;
using absl::random_internal::pcg32_2018_engine;
using absl::random_internal::pcg64_2018_engine;

template <typename EngineType>
class PCGEngineTest : public ::testing::Test {};

using EngineTypes = ::testing::Types<pcg64_2018_engine, pcg32_2018_engine>;

TYPED_TEST_SUITE(PCGEngineTest, EngineTypes);

TYPED_TEST(PCGEngineTest, VerifyReseedChangesAllValues) {
  using engine_type = TypeParam;
  using result_type = typename engine_type::result_type;

  const size_t kNumOutputs = 16;
  engine_type engine;

  // MSVC emits error 2719 without the use of std::ref below.
  //  * formal parameter with __declspec(align('#')) won't be aligned

  {
    std::seed_seq seq1{1, 2, 3, 4, 5, 6, 7};
    engine.seed(seq1);
  }
  result_type a[kNumOutputs];
  std::generate(std::begin(a), std::end(a), std::ref(engine));

  {
    std::random_device rd;
    std::seed_seq seq2{rd(), rd(), rd()};
    engine.seed(seq2);
  }
  result_type b[kNumOutputs];
  std::generate(std::begin(b), std::end(b), std::ref(engine));

  // Verify that two uncorrelated values have ~50% of there bits in common. Use
  // a 10% margin-of-error to reduce flakiness.
  size_t changed_bits = 0;
  size_t unchanged_bits = 0;
  size_t total_set = 0;
  size_t total_bits = 0;
  size_t equal_count = 0;
  for (size_t i = 0; i < kNumOutputs; ++i) {
    equal_count += (a[i] == b[i]) ? 1 : 0;
    std::bitset<sizeof(result_type) * 8> bitset(a[i] ^ b[i]);
    changed_bits += bitset.count();
    unchanged_bits += bitset.size() - bitset.count();

    std::bitset<sizeof(result_type) * 8> a_set(a[i]);
    std::bitset<sizeof(result_type) * 8> b_set(b[i]);
    total_set += a_set.count() + b_set.count();
    total_bits += 2 * 8 * sizeof(result_type);
  }
  // On average, half the bits are changed between two calls.
  EXPECT_LE(changed_bits, 0.60 * (changed_bits + unchanged_bits));
  EXPECT_GE(changed_bits, 0.40 * (changed_bits + unchanged_bits));

  // verify using a quick normal-approximation to the binomial.
  EXPECT_NEAR(total_set, total_bits * 0.5, 4 * std::sqrt(total_bits))
      << "@" << total_set / static_cast<double>(total_bits);

  // Also, A[i] == B[i] with probability (1/range) * N.
  // Give this a pretty wide latitude, though.
  const double kExpected = kNumOutputs / (1.0 * sizeof(result_type) * 8);
  EXPECT_LE(equal_count, 1.0 + kExpected);
}

// Number of values that needs to be consumed to clean two sizes of buffer
// and trigger third refresh. (slightly overestimates the actual state size).
constexpr size_t kTwoBufferValues = 16;

TYPED_TEST(PCGEngineTest, VerifyDiscard) {
  using engine_type = TypeParam;

  for (size_t num_used = 0; num_used < kTwoBufferValues; ++num_used) {
    engine_type engine_used;
    for (size_t i = 0; i < num_used; ++i) {
      engine_used();
    }

    for (size_t num_discard = 0; num_discard < kTwoBufferValues;
         ++num_discard) {
      engine_type engine1 = engine_used;
      engine_type engine2 = engine_used;
      for (size_t i = 0; i < num_discard; ++i) {
        engine1();
      }
      engine2.discard(num_discard);
      for (size_t i = 0; i < kTwoBufferValues; ++i) {
        const auto r1 = engine1();
        const auto r2 = engine2();
        ASSERT_EQ(r1, r2) << "used=" << num_used << " discard=" << num_discard;
      }
    }
  }
}

TYPED_TEST(PCGEngineTest, StreamOperatorsResult) {
  using engine_type = TypeParam;

  std::wostringstream os;
  std::wistringstream is;
  engine_type engine;

  EXPECT_EQ(&(os << engine), &os);
  EXPECT_EQ(&(is >> engine), &is);
}

TYPED_TEST(PCGEngineTest, StreamSerialization) {
  using engine_type = TypeParam;

  for (size_t discard = 0; discard < kTwoBufferValues; ++discard) {
    ExplicitSeedSeq seed_sequence{12, 34, 56};
    engine_type engine(seed_sequence);
    engine.discard(discard);

    std::stringstream stream;
    stream << engine;

    engine_type new_engine;
    stream >> new_engine;
    for (size_t i = 0; i < 64; ++i) {
      EXPECT_EQ(engine(), new_engine()) << " " << i;
    }
  }
}

constexpr size_t kNumGoldenOutputs = 127;

// This test is checking if randen_engine is meets interface requirements
// defined in [rand.req.urbg].
TYPED_TEST(PCGEngineTest, RandomNumberEngineInterface) {
  using engine_type = TypeParam;

  using E = engine_type;
  using T = typename E::result_type;

  static_assert(std::is_copy_constructible<E>::value,
                "engine_type must be copy constructible");

  static_assert(absl::is_copy_assignable<E>::value,
                "engine_type must be copy assignable");

  static_assert(std::is_move_constructible<E>::value,
                "engine_type must be move constructible");

  static_assert(absl::is_move_assignable<E>::value,
                "engine_type must be move assignable");

  static_assert(std::is_same<decltype(std::declval<E>()()), T>::value,
                "return type of operator() must be result_type");

  // Names after definition of [rand.req.urbg] in C++ standard.
  // e us a value of E
  // v is a lvalue of E
  // x, y are possibly const values of E
  // s is a value of T
  // q is a value satisfying requirements of seed_sequence
  // z is a value of type unsigned long long
  // os is a some specialization of basic_ostream
  // is is a some specialization of basic_istream

  E e, v;
  const E x, y;
  T s = 1;
  std::seed_seq q{1, 2, 3};
  unsigned long long z = 1;  // NOLINT(runtime/int)
  std::wostringstream os;
  std::wistringstream is;

  E{};
  E{x};
  E{s};
  E{q};

  e.seed();

  // MSVC emits error 2718 when using EXPECT_EQ(e, x)
  //  * actual parameter with __declspec(align('#')) won't be aligned
  EXPECT_TRUE(e == x);

  e.seed(q);
  {
    E tmp(q);
    EXPECT_TRUE(e == tmp);
  }

  e();
  {
    E tmp(q);
    EXPECT_TRUE(e != tmp);
  }

  e.discard(z);

  static_assert(std::is_same<decltype(x == y), bool>::value,
                "return type of operator== must be bool");

  static_assert(std::is_same<decltype(x != y), bool>::value,
                "return type of operator== must be bool");
}

TYPED_TEST(PCGEngineTest, RandenEngineSFINAETest) {
  using engine_type = TypeParam;
  using result_type = typename engine_type::result_type;

  {
    engine_type engine(result_type(1));
    engine.seed(result_type(1));
  }

  {
    result_type n = 1;
    engine_type engine(n);
    engine.seed(n);
  }

  {
    engine_type engine(1);
    engine.seed(1);
  }

  {
    int n = 1;
    engine_type engine(n);
    engine.seed(n);
  }

  {
    std::seed_seq seed_seq;
    engine_type engine(seed_seq);
    engine.seed(seed_seq);
  }

  {
    engine_type engine{std::seed_seq()};
    engine.seed(std::seed_seq());
  }
}

// ------------------------------------------------------------------
// Stability tests for pcg64_2018_engine
// ------------------------------------------------------------------
TEST(PCG642018EngineTest, VerifyGolden) {
  constexpr uint64_t kGolden[kNumGoldenOutputs] = {
      0x01070196e695f8f1, 0x703ec840c59f4493, 0xe54954914b3a44fa,
      0x96130ff204b9285e, 0x7d9fdef535ceb21a, 0x666feed42e1219a0,
      0x981f685721c8326f, 0xad80710d6eab4dda, 0xe202c480b037a029,
      0x5d3390eaedd907e2, 0x0756befb39c6b8aa, 0x1fb44ba6634d62a3,
      0x8d20423662426642, 0x34ea910167a39fb4, 0x93010b43a80d0ab6,
      0x663db08a98fc568a, 0x720b0a1335956fae, 0x2c35483e31e1d3ba,
      0x429f39776337409d, 0xb46d99e638687344, 0x105370b96aedcaee,
      0x3999e92f811cff71, 0xd230f8bcb591cfc9, 0x0dce3db2ba7bdea5,
      0xcf2f52c91eec99af, 0x2bc7c24a8b998a39, 0xbd8af1b0d599a19c,
      0x56bc45abc66059f5, 0x170a46dc170f7f1e, 0xc25daf5277b85fad,
      0xe629c2e0c948eadb, 0x1720a796915542ed, 0x22fb0caa4f909951,
      0x7e0c0f4175acd83d, 0xd9fcab37ff2a860c, 0xab2280fb2054bad1,
      0x58e8a06f37fa9e99, 0xc3a52a30b06528c7, 0x0175f773a13fc1bd,
      0x731cfc584b00e840, 0x404cc7b2648069cb, 0x5bc29153b0b7f783,
      0x771310a38cc999d1, 0x766a572f0a71a916, 0x90f450fb4fc48348,
      0xf080ea3e1c7b1a0d, 0x15471a4507d66a44, 0x7d58e55a78f3df69,
      0x0130a094576ac99c, 0x46669cb2d04b1d87, 0x17ab5bed20191840,
      0x95b177d260adff3e, 0x025fb624b6ee4c07, 0xb35de4330154a95f,
      0xe8510fff67e24c79, 0x132c3cbcd76ed2d3, 0x35e7cc145a093904,
      0x9f5b5b5f81583b79, 0x3ee749a533966233, 0x4af85886cdeda8cd,
      0x0ca5380ecb3ef3aa, 0x4f674eb7661d3192, 0x88a29aad00cd7733,
      0x70b627ca045ffac6, 0x5912b43ea887623d, 0x95dc9fc6f62cf221,
      0x926081a12a5c905b, 0x9c57d4cd7dfce651, 0x85ab2cbf23e3bb5d,
      0xc5cd669f63023152, 0x3067be0fad5d898e, 0x12b56f444cb53d05,
      0xbc2e5a640c3434fc, 0x9280bff0e4613fe1, 0x98819094c528743e,
      0x999d1c98d829df33, 0x9ff82a012dc89242, 0xf99183ed39c8be94,
      0xf0f59161cd421c55, 0x3c705730c2f6c48d, 0x66ad85c6e9278a61,
      0x2a3428e4a428d5d0, 0x79207d68fd04940d, 0xea7f2b402edc8430,
      0xa06b419ac857f63b, 0xcb1dd0e6fbc47e1c, 0x4f55229200ada6a4,
      0x9647b5e6359c927f, 0x30bf8f9197c7efe5, 0xa79519529cc384d0,
      0xbb22c4f339ad6497, 0xd7b9782f59d14175, 0x0dff12fff2ec0118,
      0xa331ad8305343a7c, 0x48dad7e3f17e0862, 0x324c6fb3fd3c9665,
      0xf0e4350e7933dfc4, 0x7ccda2f30b8b03b6, 0xa0afc6179005de40,
      0xee65da6d063b3a30, 0xb9506f42f2bfe87a, 0xc9a2e26b0ef5baa0,
      0x39fa9d4f495011d6, 0xbecc21a45d023948, 0x6bf484c6593f737f,
      0x8065e0070cadc3b7, 0x9ef617ed8d419799, 0xac692cf8c233dd15,
      0xd2ed87583c4ebb98, 0xad95ba1bebfedc62, 0x9b60b160a8264e43,
      0x0bc8c45f71fcf25b, 0x4a78035cdf1c9931, 0x4602dc106667e029,
      0xb335a3c250498ac8, 0x0256ebc4df20cab8, 0x0c61efd153f0c8d9,
      0xe5d0150a4f806f88, 0x99d6521d351e7d87, 0x8d4888c9f80f4325,
      0x106c5735c1ba868d, 0x73414881b880a878, 0x808a9a58a3064751,
      0x339a29f3746de3d5, 0x5410d7fa4f873896, 0xd84623c81d7b8a03,
      0x1f7c7e7a7f47f462,
  };

  pcg64_2018_engine engine(0);
#if UPDATE_GOLDEN
  (void)kGolden;  // Silence warning.
  for (size_t i = 0; i < kNumGoldenOutputs; ++i) {
    printf("0x%016lx, ", engine());
    if (i % 3 == 2) {
      printf("\n");
    }
  }
  printf("\n\n\n");
#else
  for (const auto& elem : kGolden) {
    EXPECT_EQ(elem, engine());
  }
  engine.seed();
  for (const auto& elem : kGolden) {
    EXPECT_EQ(elem, engine());
  }
#endif
}

TEST(PCG642018EngineTest, VerifyGoldenSeeded) {
  constexpr uint64_t kGolden[kNumGoldenOutputs] = {
      0xb03988f1e39691ee, 0xbd2a1eb5ac31e97a, 0x8f00d6d433634d02,
      0x1823c28d483d5776, 0x000c3ee3e1aeb74a, 0xfa82ef27a4f3df9c,
      0xc6f382308654e454, 0x414afb1a238996c2, 0x4703a4bc252eb411,
      0x99d64f62c8f7f654, 0xbb07ebe11a34fa44, 0x79eb06a363c06131,
      0xf66ad3756f1c6b21, 0x130c01d5e869f457, 0x5ca2b9963aecbc81,
      0xfef7bebc1de27e6c, 0x1d174faa5ed2cdbf, 0xd75b7a773f2bb889,
      0xc35c872327a170a5, 0x46da6d88646a42fe, 0x4622985e0442dae2,
      0xbe3cbd67297f1f9b, 0xe7c37b4a4798bfd1, 0x173d5dfad15a25c3,
      0x0eb6849ba2961522, 0xb0ff7246e6700d73, 0x88cb9c42d3afa577,
      0xb609731dbd94d917, 0xd3941cda04b40081, 0x28d140f7409bea3a,
      0x3c96699a920a124a, 0xdb28be521958b2fd, 0x0a3f44db3d4c5124,
      0x7ac8e60ba13b70d2, 0x75f03a41ded5195a, 0xaed10ac7c4e4825d,
      0xb92a3b18aadb7adc, 0xda45e0081f2bca46, 0x74d39ab3753143fc,
      0xb686038018fac9ca, 0x4cc309fe99542dbb, 0xf3e1a4fcb311097c,
      0x58763d6fa698d69d, 0xd11c365dbecd8d60, 0x2c15d55725b1dee7,
      0x89805f254d85658c, 0x2374c44dfc62158b, 0x9a8350fa7995328d,
      0x198f838970cf91da, 0x96aff569562c0e53, 0xd76c8c52b7ec6e3f,
      0x23a01cd9ae4baa81, 0x3adb366b6d02a893, 0xb3313e2a4c5b333f,
      0x04c11230b96a5425, 0x1f7f7af04787d571, 0xaddb019365275ec7,
      0x5c960468ccb09f42, 0x8438db698c69a44a, 0x492be1e46111637e,
      0x9c6c01e18100c610, 0xbfe48e75b7d0aceb, 0xb5e0b89ec1ce6a00,
      0x9d280ecbc2fe8997, 0x290d9e991ba5fcab, 0xeec5bec7d9d2a4f0,
      0x726e81488f19150e, 0x1a6df7955a7e462c, 0x37a12d174ba46bb5,
      0x3cdcdffd96b1b5c5, 0x2c5d5ac10661a26e, 0xa742ed18f22e50c4,
      0x00e0ed88ff0d8a35, 0x3d3c1718cb1efc0b, 0x1d70c51ffbccbf11,
      0xfbbb895132a4092f, 0x619d27f2fb095f24, 0x69af68200985e5c4,
      0xbee4885f57373f8d, 0x10b7a6bfe0587e40, 0xa885e6cf2f7e5f0a,
      0x59f879464f767550, 0x24e805d69056990d, 0x860970b911095891,
      0xca3189954f84170d, 0x6652a5edd4590134, 0x5e1008cef76174bf,
      0xcbd417881f2bcfe5, 0xfd49fc9d706ecd17, 0xeebf540221ebd066,
      0x46af7679464504cb, 0xd4028486946956f1, 0xd4f41864b86c2103,
      0x7af090e751583372, 0x98cdaa09278cb642, 0xffd42b921215602f,
      0x1d05bec8466b1740, 0xf036fa78a0132044, 0x787880589d1ecc78,
      0x5644552cfef33230, 0x0a97e275fe06884b, 0x96d1b13333d470b5,
      0xc8b3cdad52d3b034, 0x091357b9db7376fd, 0xa5fe4232555edf8c,
      0x3371bc3b6ada76b5, 0x7deeb2300477c995, 0x6fc6d4244f2849c1,
      0x750e8cc797ca340a, 0x81728613cd79899f, 0x3467f4ee6f9aeb93,
      0x5ef0a905f58c640f, 0x432db85e5101c98a, 0x6488e96f46ac80c2,
      0x22fddb282625048c, 0x15b287a0bc2d4c5d, 0xa7e2343ef1f28bce,
      0xc87ee1aa89bed09e, 0x220610107812c5e9, 0xcbdab6fcd640f586,
      0x8d41047970928784, 0x1aa431509ec1ade0, 0xac3f0be53f518ddc,
      0x16f4428ad81d0cbb, 0x675b13c2736fc4bb, 0x6db073afdd87e32d,
      0x572f3ca2f1a078c6,
  };

  ExplicitSeedSeq seed_sequence{12, 34, 56};
  pcg64_2018_engine engine(seed_sequence);
#if UPDATE_GOLDEN
  (void)kGolden;  // Silence warning.
  for (size_t i = 0; i < kNumGoldenOutputs; ++i) {
    printf("0x%016lx, ", engine());
    if (i % 3 == 2) {
      printf("\n");
    }
  }
  printf("\n\n\n");
#else
  for (const auto& elem : kGolden) {
    EXPECT_EQ(elem, engine());
  }
  engine.seed(seed_sequence);
  for (const auto& elem : kGolden) {
    EXPECT_EQ(elem, engine());
  }
#endif
}

TEST(PCG642018EngineTest, VerifyGoldenFromDeserializedEngine) {
  constexpr uint64_t kGolden[kNumGoldenOutputs] = {
      0xdd425b47b4113dea, 0x1b07176479d444b0, 0x6b391027586f2e42,
      0xa166f2b15f4a2143, 0xffb6dbd7a179ee97, 0xb2c00035365bf0b1,
      0x8fbb518b45855521, 0xfc789a55ddf87c3b, 0x429531f0f17ff355,
      0xbe708560d603d283, 0x5bff415175c5cb6b, 0xe813491f4ad45394,
      0xa853f4506d55880d, 0x7e538453e568172e, 0xe101f1e098ddd0ec,
      0x6ee31266ee4c766d, 0xa8786d92d66b39d7, 0xfee622a2acf5e5b0,
      0x5fe8e82c102fa7b3, 0x01f10be4cdb53c9d, 0xbe0545366f857022,
      0x12e74f010a339bca, 0xb10d85ca40d5ce34, 0xe80d6feba5054875,
      0x2b7c1ee6d567d4ee, 0x2a9cd043bfd03b66, 0x5cfc531bd239f3f1,
      0x1c4734e4647d70f5, 0x85a8f60f006b5760, 0x6a4239ce76dca387,
      0x8da0f86d7339335c, 0xf055b0468551374d, 0x486e8567e9bea9a0,
      0x4cb531b8405192dd, 0xf813b1ee3157110b, 0x214c2a664a875d8e,
      0x74531237b29b35f7, 0xa6f0267bb77a771e, 0x64b552bff54184a4,
      0xa2d6f7af2d75b6fc, 0x460a10018e03b5ab, 0x76fd1fdcb81d0800,
      0x76f5f81805070d9d, 0x1fb75cb1a70b289a, 0x9dfd25a022c4b27f,
      0x9a31a14a80528e9e, 0x910dc565ddc25820, 0xd6aef8e2b0936c10,
      0xe1773c507fe70225, 0xe027fd7aadd632bc, 0xc1fecb427089c8b8,
      0xb5c74c69fa9dbf26, 0x71bf9b0e4670227d, 0x25f48fad205dcfdd,
      0x905248ec4d689c56, 0x5c2b7631b0de5c9d, 0x9f2ee0f8f485036c,
      0xfd6ce4ebb90bf7ea, 0xd435d20046085574, 0x6b7eadcb0625f986,
      0x679d7d44b48be89e, 0x49683b8e1cdc49de, 0x4366cf76e9a2f4ca,
      0x54026ec1cdad7bed, 0xa9a04385207f28d3, 0xc8e66de4eba074b2,
      0x40b08c42de0f4cc0, 0x1d4c5e0e93c5bbc0, 0x19b80792e470ae2d,
      0x6fcaaeaa4c2a5bd9, 0xa92cb07c4238438e, 0x8bb5c918a007e298,
      0x7cd671e944874cf4, 0x88166470b1ba3cac, 0xd013d476eaeeade6,
      0xcee416947189b3c3, 0x5d7c16ab0dce6088, 0xd3578a5c32b13d27,
      0x3875db5adc9cc973, 0xfbdaba01c5b5dc56, 0xffc4fdd391b231c3,
      0x2334520ecb164fec, 0x361c115e7b6de1fa, 0xeee58106cc3563d7,
      0x8b7f35a8db25ebb8, 0xb29d00211e2cafa6, 0x22a39fe4614b646b,
      0x92ca6de8b998506d, 0x40922fe3d388d1db, 0x9da47f1e540f802a,
      0x811dceebf16a25db, 0xf6524ae22e0e53a9, 0x52d9e780a16eb99d,
      0x4f504286bb830207, 0xf6654d4786bd5cc3, 0x00bd98316003a7e1,
      0xefda054a6ab8f5f3, 0x46cfb0f4c1872827, 0xc22b316965c0f3b2,
      0xd1a28087c7e7562a, 0xaa4f6a094b7f5cff, 0xfe2bc853a041f7da,
      0xe9d531402a83c3ba, 0xe545d8663d3ce4dd, 0xfa2dcd7d91a13fa8,
      0xda1a080e52a127b8, 0x19c98f1f809c3d84, 0x2cef109af4678c88,
      0x53462accab3b9132, 0x176b13a80415394e, 0xea70047ef6bc178b,
      0x57bca80506d6dcdf, 0xd853ba09ff09f5c4, 0x75f4df3a7ddd4775,
      0x209c367ade62f4fe, 0xa9a0bbc74d5f4682, 0x5dfe34bada86c21a,
      0xc2c05bbcd38566d1, 0x6de8088e348c916a, 0x6a7001c6000c2196,
      0xd9fb51865fc4a367, 0x12f320e444ece8ff, 0x6d56f7f793d65035,
      0x138f31b7a865f8aa, 0x58fc68b4026b9adf, 0xcd48954b79fb6436,
      0x27dfce4a0232af87,
  };

#if UPDATE_GOLDEN
  (void)kGolden;  // Silence warning.
  std::seed_seq seed_sequence{1, 2, 3};
  pcg64_2018_engine engine(seed_sequence);
  std::ostringstream stream;
  stream << engine;
  auto str = stream.str();
  printf("%s\n\n", str.c_str());
  for (size_t i = 0; i < kNumGoldenOutputs; ++i) {
    printf("0x%016lx, ", engine());
    if (i % 3 == 2) {
      printf("\n");
    }
  }
  printf("\n\n\n");
#else
  pcg64_2018_engine engine;
  std::istringstream stream(
      "2549297995355413924 4865540595714422341 6364136223846793005 "
      "1442695040888963407 18088519957565336995 4845369368158826708");
  stream >> engine;
  for (const auto& elem : kGolden) {
    EXPECT_EQ(elem, engine());
  }
#endif
}

// ------------------------------------------------------------------
// Stability tests for pcg32_2018_engine
// ------------------------------------------------------------------
TEST(PCG322018EngineTest, VerifyGolden) {
  constexpr uint32_t kGolden[kNumGoldenOutputs] = {
      0x7a7ecbd9, 0x89fd6c06, 0xae646aa8, 0xcd3cf945, 0x6204b303, 0x198c8585,
      0x49fce611, 0xd1e9297a, 0x142d9440, 0xee75f56b, 0x473a9117, 0xe3a45903,
      0xbce807a1, 0xe54e5f4d, 0x497d6c51, 0x61829166, 0xa740474b, 0x031912a8,
      0x9de3defa, 0xd266dbf1, 0x0f38bebb, 0xec3c4f65, 0x07c5057d, 0xbbce03c8,
      0xfd2ac7a8, 0xffcf4773, 0x5b10affb, 0xede1c842, 0xe22b01b7, 0xda133c8c,
      0xaf89b0f4, 0x25d1b8bc, 0x9f625482, 0x7bfd6882, 0x2e2210c0, 0x2c8fb9a6,
      0x42cb3b83, 0x40ce0dab, 0x644a3510, 0x36230ef2, 0xe2cb6d43, 0x1012b343,
      0x746c6c9f, 0x36714cf8, 0xed1f5026, 0x8bbbf83e, 0xe98710f4, 0x8a2afa36,
      0x09035349, 0x6dc1a487, 0x682b634b, 0xc106794f, 0x7dd78beb, 0x628c262b,
      0x852fb232, 0xb153ac4c, 0x4f169d1b, 0xa69ab774, 0x4bd4b6f2, 0xdc351dd3,
      0x93ff3c8c, 0xa30819ab, 0xff07758c, 0x5ab13c62, 0xd16d7fb5, 0xc4950ffa,
      0xd309ae49, 0xb9677a87, 0x4464e317, 0x90dc44f1, 0xc694c1d4, 0x1d5e1168,
      0xadf37a2d, 0xda38990d, 0x1ec4bd33, 0x36ca25ce, 0xfa0dc76a, 0x968a9d43,
      0x6950ac39, 0xdd3276bc, 0x06d5a71e, 0x1f6f282d, 0x5c626c62, 0xdde3fc31,
      0x152194ce, 0xc35ed14c, 0xb1f7224e, 0x47f76bb8, 0xb34fdd08, 0x7011395e,
      0x162d2a49, 0x0d1bf09f, 0x9428a952, 0x03c5c344, 0xd3525616, 0x7816fff3,
      0x6bceb8a8, 0x8345a081, 0x366420fd, 0x182abeda, 0x70f82745, 0xaf15ded8,
      0xc7f52ca2, 0xa98db9c5, 0x919d99ba, 0x9c376c1c, 0xed8d34c2, 0x716ae9f5,
      0xef062fa5, 0xee3b6c56, 0x52325658, 0x61afa9c3, 0xfdaf02f0, 0x961cf3ab,
      0x9f291565, 0x4fbf3045, 0x0590c899, 0xde901385, 0x45005ffb, 0x509db162,
      0x262fa941, 0x4c421653, 0x4b17c21e, 0xea0d1530, 0xde803845, 0x61bfd515,
      0x438523ef,
  };

  pcg32_2018_engine engine(0);
#if UPDATE_GOLDEN
  (void)kGolden;  // Silence warning.
  for (size_t i = 0; i < kNumGoldenOutputs; ++i) {
    printf("0x%08x, ", engine());
    if (i % 6 == 5) {
      printf("\n");
    }
  }
  printf("\n\n\n");
#else
  for (const auto& elem : kGolden) {
    EXPECT_EQ(elem, engine());
  }
  engine.seed();
  for (const auto& elem : kGolden) {
    EXPECT_EQ(elem, engine());
  }
#endif
}

TEST(PCG322018EngineTest, VerifyGoldenSeeded) {
  constexpr uint32_t kGolden[kNumGoldenOutputs] = {
      0x60b5a64c, 0x978502f9, 0x80a75f60, 0x241f1158, 0xa4cd1dbb, 0xe7284017,
      0x3b678da5, 0x5223ec99, 0xe4bdd5d9, 0x72190e6d, 0xe6e702c9, 0xff80c768,
      0xcf126ed3, 0x1fbd20ab, 0x60980489, 0xbc72bf89, 0x407ac6c0, 0x00bf3c51,
      0xf9087897, 0x172e4eb6, 0xe9e4f443, 0x1a6098bf, 0xbf44f8c2, 0xdd84a0e5,
      0xd9a52364, 0xc0e2e786, 0x061ae2ba, 0x9facb8e3, 0x6109432d, 0xd4e0a013,
      0xbd8eb9a6, 0x7e86c3b6, 0x629c0e68, 0x05337430, 0xb495b9f4, 0x11ccd65d,
      0xb578db25, 0x66f1246d, 0x6ef20a7f, 0x5e429812, 0x11772130, 0xb944b5c2,
      0x01624128, 0xa2385ab7, 0xd3e10d35, 0xbe570ec3, 0xc951656f, 0xbe8944a0,
      0x7be41062, 0x5709f919, 0xd745feda, 0x9870b9ae, 0xb44b8168, 0x19e7683b,
      0xded8017f, 0xc6e4d544, 0x91ae4225, 0xd6745fba, 0xb992f284, 0x65b12b33,
      0xa9d5fdb4, 0xf105ce1a, 0x35ca1a6e, 0x2ff70dd0, 0xd8335e49, 0xfb71ddf2,
      0xcaeabb89, 0x5c6f5f84, 0x9a811a7d, 0xbcecbbd1, 0x0f661ba0, 0x9ad93b9d,
      0xedd23e0b, 0x42062f48, 0xd38dd7e4, 0x6cd63c9c, 0x640b98ae, 0x4bff5653,
      0x12626371, 0x13266017, 0xe7a698d8, 0x39c74667, 0xe8fdf2e3, 0x52803bf8,
      0x2af6895b, 0x91335b7b, 0x699e4961, 0x00a40fff, 0x253ff2b6, 0x4a6cf672,
      0x9584e85f, 0xf2a5000c, 0x4d58aba8, 0xb8513e6a, 0x767fad65, 0x8e326f9e,
      0x182f15a1, 0x163dab52, 0xdf99c780, 0x047282a1, 0xee4f90dd, 0xd50394ae,
      0x6c9fd5f0, 0xb06a9194, 0x387e3840, 0x04a9487b, 0xf678a4c2, 0xd0a78810,
      0xd502c97e, 0xd6a9b12a, 0x4accc5dc, 0x416ed53e, 0x50411536, 0xeeb89c24,
      0x813a7902, 0x034ebca6, 0xffa52e7c, 0x7ecd3d0e, 0xfa37a0d2, 0xb1fbe2c1,
      0xb7efc6d1, 0xefa4ccee, 0xf6f80424, 0x2283f3d9, 0x68732284, 0x94f3b5c8,
      0xbbdeceb9,
  };

  ExplicitSeedSeq seed_sequence{12, 34, 56};
  pcg32_2018_engine engine(seed_sequence);
#if UPDATE_GOLDEN
  (void)kGolden;  // Silence warning.
  for (size_t i = 0; i < kNumGoldenOutputs; ++i) {
    printf("0x%08x, ", engine());
    if (i % 6 == 5) {
      printf("\n");
    }
  }
  printf("\n\n\n");
#else
  for (const auto& elem : kGolden) {
    EXPECT_EQ(elem, engine());
  }
  engine.seed(seed_sequence);
  for (const auto& elem : kGolden) {
    EXPECT_EQ(elem, engine());
  }
#endif
}

TEST(PCG322018EngineTest, VerifyGoldenFromDeserializedEngine) {
  constexpr uint64_t kGolden[kNumGoldenOutputs] = {
      0x780f7042, 0xba137215, 0x43ab6f22, 0x0cb55f46, 0x44b2627d, 0x835597af,
      0xea973ea1, 0x0d2abd35, 0x4fdd601c, 0xac4342fe, 0x7db7e93c, 0xe56ebcaf,
      0x3596470a, 0x7770a9ad, 0x9b893320, 0x57db3415, 0xb432de54, 0xa02baf71,
      0xa256aadb, 0x88921fc7, 0xa35fa6b3, 0xde3eca46, 0x605739a7, 0xa890b82b,
      0xe457b7ad, 0x335fb903, 0xeb06790c, 0xb3c54bf6, 0x6141e442, 0xa599a482,
      0xb78987cc, 0xc61dfe9d, 0x0f1d6ace, 0x17460594, 0x8f6a5061, 0x083dc354,
      0xe9c337fb, 0xcfd105f7, 0x926764b6, 0x638d24dc, 0xeaac650a, 0x67d2cb9c,
      0xd807733c, 0x205fc52e, 0xf5399e2e, 0x6c46ddcc, 0xb603e875, 0xce113a25,
      0x3c8d4813, 0xfb584db8, 0xf6d255ff, 0xea80954f, 0x42e8be85, 0xb2feee72,
      0x62bd8d16, 0x1be4a142, 0x97dca1a4, 0xdd6e7333, 0xb2caa20e, 0xa12b1588,
      0xeb3a5a1a, 0x6fa5ba89, 0x077ea931, 0x8ddb1713, 0x0dd03079, 0x2c2ba965,
      0xa77fac17, 0xc8325742, 0x8bb893bf, 0xc2315741, 0xeaceee92, 0x81dd2ee2,
      0xe5214216, 0x1b9b8fb2, 0x01646d03, 0x24facc25, 0xd8c0e0bb, 0xa33fe106,
      0xf34fe976, 0xb3b4b44e, 0x65618fed, 0x032c6192, 0xa9dd72ce, 0xf391887b,
      0xf41c6a6e, 0x05c4bd6d, 0x37fa260e, 0x46b05659, 0xb5f6348a, 0x62d26d89,
      0x39f6452d, 0xb17b30a2, 0xbdd82743, 0x38ecae3b, 0xfe90f0a2, 0xcb2d226d,
      0xcf8a0b1c, 0x0eed3d4d, 0xa1f69cfc, 0xd7ac3ba5, 0xce9d9a6b, 0x121deb4c,
      0x4a0d03f3, 0xc1821ed1, 0x59c249ac, 0xc0abb474, 0x28149985, 0xfd9a82ba,
      0x5960c3b2, 0xeff00cba, 0x6073aa17, 0x25dc0919, 0x9976626e, 0xdd2ccc33,
      0x39ecb6ec, 0xc6e15d13, 0xfac94cfd, 0x28cfd34f, 0xf2d2c32d, 0x51c23d08,
      0x4fdb2f48, 0x97baa807, 0xf2c1004c, 0xc4ae8136, 0x71f31c94, 0x8c92d601,
      0x36caf5cd,
  };

#if UPDATE_GOLDEN
  (void)kGolden;  // Silence warning.
  std::seed_seq seed_sequence{1, 2, 3};
  pcg32_2018_engine engine(seed_sequence);
  std::ostringstream stream;
  stream << engine;
  auto str = stream.str();
  printf("%s\n\n", str.c_str());
  for (size_t i = 0; i < kNumGoldenOutputs; ++i) {
    printf("0x%08x, ", engine());
    if (i % 6 == 5) {
      printf("\n");
    }
  }
  printf("\n\n\n");

  EXPECT_FALSE(true);
#else
  pcg32_2018_engine engine;
  std::istringstream stream(
      "6364136223846793005 1442695040888963407 6537028157270659894");
  stream >> engine;
  for (const auto& elem : kGolden) {
    EXPECT_EQ(elem, engine());
  }
#endif
}

}  // namespace
