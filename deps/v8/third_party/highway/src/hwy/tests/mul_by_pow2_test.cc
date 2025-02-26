// Copyright 2019 Google LLC
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stddef.h>
#include <stdint.h>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/mul_by_pow2_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

template <class D>
static void MulByPow2TestCases(
    D d, size_t& padded, AlignedFreeUniquePtr<TFromD<D>[]>& out_val,
    AlignedFreeUniquePtr<MakeSigned<TFromD<D>>[]>& out_exp,
    AlignedFreeUniquePtr<TFromD<D>[]>& out_expected) {
  using T = TFromD<D>;
  using TI = MakeSigned<T>;
  using TU = MakeUnsigned<T>;

  struct TestCaseVals {
    T val;
    TI exp;
    T expected;
  };

  HWY_BITCASTSCALAR_CXX14_CONSTEXPR const T k0 = ConvertScalarTo<T>(0.0);
  HWY_BITCASTSCALAR_CXX14_CONSTEXPR const T k1 = ConvertScalarTo<T>(1.0);
  HWY_BITCASTSCALAR_CXX14_CONSTEXPR const T kNeg1 = ConvertScalarTo<T>(-1.0);
  HWY_BITCASTSCALAR_CXX14_CONSTEXPR const T kPosInf =
      BitCastScalar<T>(ExponentMask<T>());
  HWY_BITCASTSCALAR_CXX14_CONSTEXPR const T kNegInf = BitCastScalar<T>(
      static_cast<TU>(ExponentMask<T>() | (TU{1} << (sizeof(TU) * 8 - 1))));
  HWY_BITCASTSCALAR_CXX14_CONSTEXPR const T kNaN = BitCastScalar<T>(
      static_cast<TU>(ExponentMask<T>() | (ExponentMask<T>() >> 1)));

  constexpr TI kMinInt = LimitsMin<TI>();
  constexpr TI kMaxInt = LimitsMax<TI>();

  constexpr int kNumOfMantBits = MantissaBits<T>();
  constexpr TI kExpBias = static_cast<TI>(MaxExponentField<T>() >> 1);
  static_assert(kExpBias > 0, "kExpBias > 0 must be true");

  constexpr TI kHugeExp = static_cast<TI>(kExpBias + 2);
  constexpr TI kTinyExp = static_cast<TI>(-kExpBias - kNumOfMantBits - 2);

  const TestCaseVals test_cases[] = {
      {k0, static_cast<TI>(0), k0},
      {k0, kTinyExp, k0},
      {k0, kMinInt, k0},
      {k0, kHugeExp, k0},
      {k0, kMaxInt, k0},
      {kNaN, static_cast<TI>(0), kNaN},
      {kNaN, kTinyExp, kNaN},
      {kNaN, kMinInt, kNaN},
      {kNaN, kHugeExp, kNaN},
      {kNaN, kMaxInt, kNaN},
      {k1, static_cast<TI>(0), k1},
      {k1, kTinyExp, k0},
      {k1, kMinInt, k0},
      {k1, kHugeExp, kPosInf},
      {k1, kMaxInt, kPosInf},
      {kNeg1, static_cast<TI>(0), kNeg1},
      {kNeg1, kTinyExp, k0},
      {kNeg1, kMinInt, k0},
      {kNeg1, kHugeExp, kNegInf},
      {kNeg1, kMaxInt, kNegInf},
      {kPosInf, static_cast<TI>(0), kPosInf},
      {kPosInf, kTinyExp, kPosInf},
      {kPosInf, kMinInt, kPosInf},
      {kPosInf, kHugeExp, kPosInf},
      {kPosInf, kMaxInt, kPosInf},
      {kNegInf, static_cast<TI>(0), kNegInf},
      {kNegInf, kTinyExp, kNegInf},
      {kNegInf, kMinInt, kNegInf},
      {kNegInf, kHugeExp, kNegInf},
      {kNegInf, kMaxInt, kNegInf},
      {ConvertScalarTo<T>(0.04222115867157571), kMaxInt, kPosInf},
      {ConvertScalarTo<T>(20.486009923543207), kMaxInt, kPosInf},
      {ConvertScalarTo<T>(-2.100807236091572), kMaxInt, kNegInf},
      {ConvertScalarTo<T>(-0.02786731762944917), kMinInt, k0},
      {ConvertScalarTo<T>(-87.33609795949775), kMinInt, k0},
      {ConvertScalarTo<T>(-0.01091789786912318), kMaxInt, kNegInf},
      {ConvertScalarTo<T>(13.598303329265983), kMinInt, k0},
      {ConvertScalarTo<T>(4.461800938646495), kMinInt, k0},
      {ConvertScalarTo<T>(13.8359375), static_cast<TI>(-4),
       ConvertScalarTo<T>(0.86474609375)},
      {ConvertScalarTo<T>(27.671875), static_cast<TI>(-3),
       ConvertScalarTo<T>(3.458984375)},
      {ConvertScalarTo<T>(41.5078125), static_cast<TI>(-2),
       ConvertScalarTo<T>(10.376953125)},
      {ConvertScalarTo<T>(55.34375), static_cast<TI>(-1),
       ConvertScalarTo<T>(27.671875)},
      {ConvertScalarTo<T>(69.1796875), static_cast<TI>(0),
       ConvertScalarTo<T>(69.1796875)},
      {ConvertScalarTo<T>(83.015625), static_cast<TI>(1),
       ConvertScalarTo<T>(166.03125)},
      {ConvertScalarTo<T>(96.8515625), static_cast<TI>(2),
       ConvertScalarTo<T>(387.40625)},
      {ConvertScalarTo<T>(110.6875), static_cast<TI>(3),
       ConvertScalarTo<T>(885.5)},
      {ConvertScalarTo<T>(124.5234375), static_cast<TI>(4),
       ConvertScalarTo<T>(1992.375)},
      {ConvertScalarTo<T>(138.359375), static_cast<TI>(-4),
       ConvertScalarTo<T>(8.6474609375)},
      {ConvertScalarTo<T>(152.1953125), static_cast<TI>(-3),
       ConvertScalarTo<T>(19.0244140625)},
      {ConvertScalarTo<T>(166.03125), static_cast<TI>(-2),
       ConvertScalarTo<T>(41.5078125)},
      {ConvertScalarTo<T>(179.8671875), static_cast<TI>(-1),
       ConvertScalarTo<T>(89.93359375)},
      {ConvertScalarTo<T>(193.703125), static_cast<TI>(0),
       ConvertScalarTo<T>(193.703125)},
      {ConvertScalarTo<T>(207.5390625), static_cast<TI>(1),
       ConvertScalarTo<T>(415.078125)},
      {ConvertScalarTo<T>(221.375), static_cast<TI>(2),
       ConvertScalarTo<T>(885.5)},
      {ConvertScalarTo<T>(235.2109375), static_cast<TI>(3),
       ConvertScalarTo<T>(1881.6875)},
      {ConvertScalarTo<T>(249.046875), static_cast<TI>(4),
       ConvertScalarTo<T>(3984.75)},
      {ConvertScalarTo<T>(262.8828125), static_cast<TI>(-4),
       ConvertScalarTo<T>(16.43017578125)},
      {ConvertScalarTo<T>(276.71875), static_cast<TI>(-3),
       ConvertScalarTo<T>(34.58984375)},
      {ConvertScalarTo<T>(290.5546875), static_cast<TI>(-2),
       ConvertScalarTo<T>(72.638671875)},
      {ConvertScalarTo<T>(304.390625), static_cast<TI>(-1),
       ConvertScalarTo<T>(152.1953125)},
      {ConvertScalarTo<T>(318.2265625), static_cast<TI>(0),
       ConvertScalarTo<T>(318.2265625)},
      {ConvertScalarTo<T>(332.0625), static_cast<TI>(1),
       ConvertScalarTo<T>(664.125)},
      {ConvertScalarTo<T>(345.8984375), static_cast<TI>(2),
       ConvertScalarTo<T>(1383.59375)},
      {ConvertScalarTo<T>(359.734375), static_cast<TI>(3),
       ConvertScalarTo<T>(2877.875)},
      {ConvertScalarTo<T>(373.5703125), static_cast<TI>(4),
       ConvertScalarTo<T>(5977.125)},
      {ConvertScalarTo<T>(387.40625), static_cast<TI>(-4),
       ConvertScalarTo<T>(24.212890625)},
      {ConvertScalarTo<T>(401.2421875), static_cast<TI>(-3),
       ConvertScalarTo<T>(50.1552734375)},
      {ConvertScalarTo<T>(415.078125), static_cast<TI>(-2),
       ConvertScalarTo<T>(103.76953125)},
      {ConvertScalarTo<T>(428.9140625), static_cast<TI>(-1),
       ConvertScalarTo<T>(214.45703125)},
      {ConvertScalarTo<T>(442.75), static_cast<TI>(0),
       ConvertScalarTo<T>(442.75)}};

  const size_t kNumTestCases = sizeof(test_cases) / sizeof(test_cases[0]);
  const size_t N = Lanes(d);
  padded = RoundUpTo(kNumTestCases, N);  // allow loading whole vectors

  out_val = AllocateAligned<T>(padded);
  out_exp = AllocateAligned<TI>(padded);
  out_expected = AllocateAligned<T>(padded);
  HWY_ASSERT(out_val && out_exp && out_expected);

  size_t i = 0;
  for (; i < kNumTestCases; ++i) {
    out_val[i] = test_cases[i].val;
    out_exp[i] = test_cases[i].exp;
    out_expected[i] = test_cases[i].expected;
  }
  for (; i < padded; ++i) {
    out_val[i] = k0;
    out_exp[i] = static_cast<TI>(0);
    out_expected[i] = k0;
  }
}

struct TestMulByPow2 {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    using TI = MakeSigned<T>;

    const RebindToSigned<decltype(d)> di;

    size_t padded;
    AlignedFreeUniquePtr<T[]> val_lanes;
    AlignedFreeUniquePtr<TI[]> exp_lanes;
    AlignedFreeUniquePtr<T[]> expected;

    MulByPow2TestCases(d, padded, val_lanes, exp_lanes, expected);

    const size_t N = Lanes(d);
    for (size_t i = 0; i < padded; i += N) {
      const auto val = Load(d, val_lanes.get() + i);
      const auto exp = Load(di, exp_lanes.get() + i);
      HWY_ASSERT_VEC_EQ(d, expected.get() + i, MulByPow2(val, exp));
    }
  }
};

HWY_NOINLINE void TestAllMulByPow2() {
  ForFloatTypes(ForPartialVectors<TestMulByPow2>());
}

template <class D>
static void MulByFloorPow2TestCases(
    D d, size_t& padded, AlignedFreeUniquePtr<TFromD<D>[]>& out_val,
    AlignedFreeUniquePtr<TFromD<D>[]>& out_exp,
    AlignedFreeUniquePtr<TFromD<D>[]>& out_expected) {
  using T = TFromD<D>;
  using TI = MakeSigned<T>;
  using TU = MakeUnsigned<T>;

  struct TestCaseVals {
    T val;
    T exp;
    T expected;
  };

  HWY_BITCASTSCALAR_CXX14_CONSTEXPR const T k0 = ConvertScalarTo<T>(0.0);
  HWY_BITCASTSCALAR_CXX14_CONSTEXPR const T k1 = ConvertScalarTo<T>(1.0);
  HWY_BITCASTSCALAR_CXX14_CONSTEXPR const T kNeg1 = ConvertScalarTo<T>(-1.0);
  HWY_BITCASTSCALAR_CXX14_CONSTEXPR const T kPosInf =
      BitCastScalar<T>(ExponentMask<T>());
  HWY_BITCASTSCALAR_CXX14_CONSTEXPR const T kNegInf = BitCastScalar<T>(
      static_cast<TU>(ExponentMask<T>() | (TU{1} << (sizeof(TU) * 8 - 1))));
  HWY_BITCASTSCALAR_CXX14_CONSTEXPR const T kNaN = BitCastScalar<T>(
      static_cast<TU>(ExponentMask<T>() | (ExponentMask<T>() >> 1)));

  HWY_BITCASTSCALAR_CXX14_CONSTEXPR const T kMinInt =
      ConvertScalarTo<T>(LimitsMin<TI>());
  HWY_BITCASTSCALAR_CXX14_CONSTEXPR const T kMaxInt =
      ConvertScalarTo<T>(LimitsMax<TI>());

  constexpr int kNumOfMantBits = MantissaBits<T>();
  constexpr TI kExpBias = static_cast<TI>(MaxExponentField<T>() >> 1);
  static_assert(kExpBias > 0, "kExpBias > 0 must be true");

  HWY_BITCASTSCALAR_CXX14_CONSTEXPR const T kHugeExp =
      ConvertScalarTo<T>(static_cast<TI>(kExpBias + 2));
  HWY_BITCASTSCALAR_CXX14_CONSTEXPR const T kTinyExp =
      ConvertScalarTo<T>(static_cast<TI>(-kExpBias - kNumOfMantBits - 2));

  const TestCaseVals test_cases[] = {
      {k0, k0, k0},
      {k0, kNegInf, k0},
      {k0, kTinyExp, k0},
      {k0, kMinInt, k0},
      {k0, kHugeExp, k0},
      {k0, kMaxInt, k0},
      {k0, kPosInf, kNaN},
      {k0, kNaN, kNaN},
      {kNaN, k0, kNaN},
      {kNaN, kTinyExp, kNaN},
      {kNaN, kMinInt, kNaN},
      {kNaN, kHugeExp, kNaN},
      {kNaN, kMaxInt, kNaN},
      {kNaN, kNaN, kNaN},
      {k1, k0, k1},
      {k1, kNegInf, k0},
      {k1, kTinyExp, k0},
      {k1, kMinInt, k0},
      {k1, kHugeExp, kPosInf},
      {k1, kMaxInt, kPosInf},
      {k1, kPosInf, kPosInf},
      {k1, kNaN, kNaN},
      {kNeg1, k0, kNeg1},
      {kNeg1, kNegInf, k0},
      {kNeg1, kTinyExp, k0},
      {kNeg1, kMinInt, k0},
      {kNeg1, kHugeExp, kNegInf},
      {kNeg1, kMaxInt, kNegInf},
      {kNeg1, kPosInf, kNegInf},
      {kNeg1, kNaN, kNaN},
      {kPosInf, k0, kPosInf},
      {kPosInf, kNegInf, kNaN},
      {kPosInf, kTinyExp, kPosInf},
      {kPosInf, kMinInt, kPosInf},
      {kPosInf, kHugeExp, kPosInf},
      {kPosInf, kMaxInt, kPosInf},
      {kPosInf, kPosInf, kPosInf},
      {kPosInf, kNaN, kNaN},
      {kNegInf, k0, kNegInf},
      {kNegInf, kNegInf, kNaN},
      {kNegInf, kTinyExp, kNegInf},
      {kNegInf, kMinInt, kNegInf},
      {kNegInf, kHugeExp, kNegInf},
      {kNegInf, kMaxInt, kNegInf},
      {kNegInf, kPosInf, kNegInf},
      {kNegInf, kNaN, kNaN},
      {ConvertScalarTo<T>(1059.5310687496399), kNegInf, k0},
      {ConvertScalarTo<T>(97.37421519431503), kHugeExp, kPosInf},
      {ConvertScalarTo<T>(-219.77349288592427), kHugeExp, kNegInf},
      {ConvertScalarTo<T>(-0.07908260899094358), kMinInt, k0},
      {ConvertScalarTo<T>(-0.37461255269075006), kPosInf, kNegInf},
      {ConvertScalarTo<T>(-1.4206473428158297), kHugeExp, kNegInf},
      {ConvertScalarTo<T>(-0.05931912296693983), kTinyExp, k0},
      {ConvertScalarTo<T>(6.775641488836299E-4), kMinInt, k0},
      {ConvertScalarTo<T>(0.17859422400264996), kNegInf, k0},
      {ConvertScalarTo<T>(-3.2925797046308722), kNaN, kNaN},
      {ConvertScalarTo<T>(63.76025467272075), kMaxInt, kPosInf},
      {ConvertScalarTo<T>(-949.496801885732), kPosInf, kNegInf},
      {ConvertScalarTo<T>(0.1711619674802602), kTinyExp, k0},
      {ConvertScalarTo<T>(307.00132360897254), kMaxInt, kPosInf},
      {ConvertScalarTo<T>(427.2906513922638), kNegInf, k0},
      {ConvertScalarTo<T>(0.0022915367711067672), kPosInf, kPosInf},
      {ConvertScalarTo<T>(0.049692878940840575), kTinyExp, k0},
      {ConvertScalarTo<T>(0.010399031566202213), kPosInf, kPosInf},
      {ConvertScalarTo<T>(-3.5924694052830457), kMaxInt, kNegInf},
      {ConvertScalarTo<T>(-35.6155724065998), kNegInf, k0},
      {ConvertScalarTo<T>(-626.2755450079259), kNaN, kNaN},
      {ConvertScalarTo<T>(1510.5106491633483), kNaN, kNaN},
      {ConvertScalarTo<T>(6.473376273141844E-4), kMinInt, k0},
      {ConvertScalarTo<T>(31.725913740728213), kHugeExp, kPosInf},
      {ConvertScalarTo<T>(-0.13049751588470118), kMinInt, k0},
      {ConvertScalarTo<T>(0.3124908582707691), kTinyExp, k0},
      {ConvertScalarTo<T>(-54.267473800450226), kMaxInt, kNegInf},
      {ConvertScalarTo<T>(0.019324406584034517),
       ConvertScalarTo<T>(-0.7529789930294555),
       ConvertScalarTo<T>(0.009662203292017259)},
      {ConvertScalarTo<T>(-23.589356466919032),
       ConvertScalarTo<T>(-2.658473701120114),
       ConvertScalarTo<T>(-2.948669558364879)},
      {ConvertScalarTo<T>(0.001358052526188437),
       ConvertScalarTo<T>(-1.668405066102655),
       ConvertScalarTo<T>(3.3951313154710925E-4)},
      {ConvertScalarTo<T>(12.387671870605264),
       ConvertScalarTo<T>(0.4676324281911401),
       ConvertScalarTo<T>(12.387671870605264)},
      {ConvertScalarTo<T>(-816.5904074657877),
       ConvertScalarTo<T>(-3.161282649113014),
       ConvertScalarTo<T>(-51.03690046661173)},
      {ConvertScalarTo<T>(1.9061852134515243),
       ConvertScalarTo<T>(3.7447600028715566),
       ConvertScalarTo<T>(15.249481707612194)},
      {ConvertScalarTo<T>(-0.27333495359539833),
       ConvertScalarTo<T>(-2.3750517462565393),
       ConvertScalarTo<T>(-0.03416686919942479)},
      {ConvertScalarTo<T>(-0.0014594113131094902),
       ConvertScalarTo<T>(3.085720424782481),
       ConvertScalarTo<T>(-0.011675290504875922)},
      {ConvertScalarTo<T>(637.3241672064872),
       ConvertScalarTo<T>(4.671463920871573),
       ConvertScalarTo<T>(10197.186675303796)},
      {ConvertScalarTo<T>(16.944732879565052),
       ConvertScalarTo<T>(-4.369178496185725),
       ConvertScalarTo<T>(0.5295229024864079)},
      {ConvertScalarTo<T>(-0.012238932661175737),
       ConvertScalarTo<T>(-2.3194292578861413),
       ConvertScalarTo<T>(-0.001529866582646967)},
      {ConvertScalarTo<T>(-1.9210108199587403),
       ConvertScalarTo<T>(0.14282162575576896),
       ConvertScalarTo<T>(-1.9210108199587403)},
      {ConvertScalarTo<T>(0.11043467327357612),
       ConvertScalarTo<T>(-2.415250859773567),
       ConvertScalarTo<T>(0.013804334159197015)},
      {ConvertScalarTo<T>(0.002707227717617229),
       ConvertScalarTo<T>(-2.6905028085982394),
       ConvertScalarTo<T>(3.3840346470215363E-4)},
      {ConvertScalarTo<T>(0.04979758601051541),
       ConvertScalarTo<T>(-0.9179775209533863),
       ConvertScalarTo<T>(0.024898793005257706)},
      {ConvertScalarTo<T>(3.309059475799939),
       ConvertScalarTo<T>(-3.6346379131430786),
       ConvertScalarTo<T>(0.20681621723749619)},
      {ConvertScalarTo<T>(-816.1508196321369),
       ConvertScalarTo<T>(1.2488081335071424),
       ConvertScalarTo<T>(-1632.3016392642737)},
      {ConvertScalarTo<T>(-0.6125198372033248),
       ConvertScalarTo<T>(-1.8407754804187284),
       ConvertScalarTo<T>(-0.1531299593008312)},
      {ConvertScalarTo<T>(0.09615218136362058),
       ConvertScalarTo<T>(-0.39747719382682956),
       ConvertScalarTo<T>(0.04807609068181029)},
      {ConvertScalarTo<T>(-0.0021822722598431347),
       ConvertScalarTo<T>(3.4238838799239812),
       ConvertScalarTo<T>(-0.017458178078745078)},
      {ConvertScalarTo<T>(-0.001069341172497859),
       ConvertScalarTo<T>(2.4555415282769784),
       ConvertScalarTo<T>(-0.004277364689991436)},
      {ConvertScalarTo<T>(0.7280398964478823),
       ConvertScalarTo<T>(-2.0683513150597843),
       ConvertScalarTo<T>(0.09100498705598528)},
      {ConvertScalarTo<T>(0.02852633429447464),
       ConvertScalarTo<T>(-1.1775178332450202),
       ConvertScalarTo<T>(0.00713158357361866)},
      {ConvertScalarTo<T>(-0.0018815213571262807),
       ConvertScalarTo<T>(-3.550897133834961),
       ConvertScalarTo<T>(-1.1759508482039255E-4)},
      {ConvertScalarTo<T>(0.0926207425083316),
       ConvertScalarTo<T>(0.366218741424757),
       ConvertScalarTo<T>(0.0926207425083316)},
      {ConvertScalarTo<T>(-64.3799309390691),
       ConvertScalarTo<T>(4.344404407280892),
       ConvertScalarTo<T>(-1030.0788950251056)},
      {ConvertScalarTo<T>(-0.02400946981744935),
       ConvertScalarTo<T>(-2.341821021170456),
       ConvertScalarTo<T>(-0.0030011837271811687)},
      {ConvertScalarTo<T>(-0.08822585458970057),
       ConvertScalarTo<T>(-0.945924667731356),
       ConvertScalarTo<T>(-0.044112927294850286)},
      {ConvertScalarTo<T>(122.34449441358599),
       ConvertScalarTo<T>(0.5100210866180717),
       ConvertScalarTo<T>(122.34449441358599)},
      {ConvertScalarTo<T>(3.2559200224126434),
       ConvertScalarTo<T>(1.5437558991072338),
       ConvertScalarTo<T>(6.511840044825287)},
      {ConvertScalarTo<T>(0.2933688026098801),
       ConvertScalarTo<T>(3.077238809133668),
       ConvertScalarTo<T>(2.3469504208790406)},
      {ConvertScalarTo<T>(21.628049007866707),
       ConvertScalarTo<T>(-2.8991237865697235),
       ConvertScalarTo<T>(2.7035061259833384)},
      {ConvertScalarTo<T>(63.435740493259665),
       ConvertScalarTo<T>(-4.41713462765055),
       ConvertScalarTo<T>(1.9823668904143645)},
      {ConvertScalarTo<T>(-1.079270457351184),
       ConvertScalarTo<T>(0.4150174591000348),
       ConvertScalarTo<T>(-1.079270457351184)},
      {ConvertScalarTo<T>(-0.05268637133114153),
       ConvertScalarTo<T>(2.6980126913247116),
       ConvertScalarTo<T>(-0.2107454853245661)},
      {ConvertScalarTo<T>(-0.22694789316698213),
       ConvertScalarTo<T>(1.7745238901054419),
       ConvertScalarTo<T>(-0.45389578633396427)},
      {ConvertScalarTo<T>(2.9243076223545907),
       ConvertScalarTo<T>(3.106526706578818),
       ConvertScalarTo<T>(23.394460978836726)},
      {ConvertScalarTo<T>(-0.023896337084267257),
       ConvertScalarTo<T>(4.237862481909235),
       ConvertScalarTo<T>(-0.3823413933482761)},
      {ConvertScalarTo<T>(8.839593057654776),
       ConvertScalarTo<T>(1.3978500181677522),
       ConvertScalarTo<T>(17.67918611530955)},
      {ConvertScalarTo<T>(0.229610115587917),
       ConvertScalarTo<T>(-4.211477622810463),
       ConvertScalarTo<T>(0.007175316112122406)},
      {ConvertScalarTo<T>(-1304.7253453052683),
       ConvertScalarTo<T>(-4.783018570610186),
       ConvertScalarTo<T>(-40.772667040789635)},
      {ConvertScalarTo<T>(-0.025447754903889074),
       ConvertScalarTo<T>(-1.710158670881026),
       ConvertScalarTo<T>(-0.0063619387259722686)},
      {ConvertScalarTo<T>(0.21914082057419035),
       ConvertScalarTo<T>(2.356083011091819),
       ConvertScalarTo<T>(0.8765632822967614)},
      {ConvertScalarTo<T>(5.42862647542766),
       ConvertScalarTo<T>(-3.5603790618102806),
       ConvertScalarTo<T>(0.33928915471422877)},
      {ConvertScalarTo<T>(-13.37913117518154),
       ConvertScalarTo<T>(0.8910026966707747),
       ConvertScalarTo<T>(-13.37913117518154)},
      {ConvertScalarTo<T>(0.0016923959034403932),
       ConvertScalarTo<T>(-0.0395086178195927),
       ConvertScalarTo<T>(8.461979517201966E-4)},
      {ConvertScalarTo<T>(-9.92565371535828),
       ConvertScalarTo<T>(1.863550485706607),
       ConvertScalarTo<T>(-19.85130743071656)},
      {ConvertScalarTo<T>(-0.20406817303532074),
       ConvertScalarTo<T>(0.4044778224004972),
       ConvertScalarTo<T>(-0.20406817303532074)},
      {ConvertScalarTo<T>(0.001427935411153914),
       ConvertScalarTo<T>(-3.623007282999475),
       ConvertScalarTo<T>(8.924596319711963E-5)},
      {ConvertScalarTo<T>(-0.001792980244032676),
       ConvertScalarTo<T>(-2.877489018179081),
       ConvertScalarTo<T>(-2.241225305040845E-4)},
      {ConvertScalarTo<T>(10.724293893361738),
       ConvertScalarTo<T>(-0.11727937890947453),
       ConvertScalarTo<T>(5.362146946680869)},
      {ConvertScalarTo<T>(-0.003683378523927509),
       ConvertScalarTo<T>(2.9771453279729068),
       ConvertScalarTo<T>(-0.014733514095710037)},
      {ConvertScalarTo<T>(-0.19933257881305555),
       ConvertScalarTo<T>(-4.935752394433358),
       ConvertScalarTo<T>(-0.006229143087907986)},
      {ConvertScalarTo<T>(-0.05446912403204286),
       ConvertScalarTo<T>(4.696207616307656),
       ConvertScalarTo<T>(-0.8715059845126858)},
      {ConvertScalarTo<T>(-0.9314238362892934),
       ConvertScalarTo<T>(0.8598717343606272),
       ConvertScalarTo<T>(-0.9314238362892934)},
      {ConvertScalarTo<T>(37.9580113757031),
       ConvertScalarTo<T>(0.16132223888265382),
       ConvertScalarTo<T>(37.9580113757031)},
      {ConvertScalarTo<T>(-0.0011390770646514143),
       ConvertScalarTo<T>(0.7120556932780127),
       ConvertScalarTo<T>(-0.0011390770646514143)},
      {ConvertScalarTo<T>(-0.0029396730491874634),
       ConvertScalarTo<T>(-0.5212230786233737),
       ConvertScalarTo<T>(-0.0014698365245937317)},
      {ConvertScalarTo<T>(-0.0028807746370910136),
       ConvertScalarTo<T>(1.8728808720779935),
       ConvertScalarTo<T>(-0.005761549274182027)},
      {ConvertScalarTo<T>(0.013988891550764875),
       ConvertScalarTo<T>(-3.3438559116920574),
       ConvertScalarTo<T>(8.743057219228047E-4)},
      {ConvertScalarTo<T>(745.7501203846241),
       ConvertScalarTo<T>(-1.0139927506606408),
       ConvertScalarTo<T>(186.43753009615602)},
      {ConvertScalarTo<T>(45.347287390908605),
       ConvertScalarTo<T>(-3.9706807059278515),
       ConvertScalarTo<T>(2.834205461931788)},
      {ConvertScalarTo<T>(-2.3894976687908356),
       ConvertScalarTo<T>(4.7410857372930515),
       ConvertScalarTo<T>(-38.23196270065337)},
      {ConvertScalarTo<T>(-0.001029165846088247),
       ConvertScalarTo<T>(3.8187838912978687),
       ConvertScalarTo<T>(-0.008233326768705976)},
      {ConvertScalarTo<T>(0.017570690697900453),
       ConvertScalarTo<T>(1.9090470977828633),
       ConvertScalarTo<T>(0.035141381395800905)}};

  const size_t kNumTestCases = sizeof(test_cases) / sizeof(test_cases[0]);
  const size_t N = Lanes(d);
  padded = RoundUpTo(kNumTestCases, N);  // allow loading whole vectors

  out_val = AllocateAligned<T>(padded);
  out_exp = AllocateAligned<T>(padded);
  out_expected = AllocateAligned<T>(padded);
  HWY_ASSERT(out_val && out_exp && out_expected);

  size_t i = 0;
  for (; i < kNumTestCases; ++i) {
    out_val[i] = test_cases[i].val;
    out_exp[i] = test_cases[i].exp;
    out_expected[i] = test_cases[i].expected;
  }
  for (; i < padded; ++i) {
    out_val[i] = k0;
    out_exp[i] = k0;
    out_expected[i] = k0;
  }
}

struct TestMulByFloorPow2 {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    using TU = MakeUnsigned<T>;

    size_t padded;
    AlignedFreeUniquePtr<T[]> val_lanes;
    AlignedFreeUniquePtr<T[]> exp_lanes;
    AlignedFreeUniquePtr<T[]> expected;

    MulByFloorPow2TestCases(d, padded, val_lanes, exp_lanes, expected);
    HWY_ASSERT(padded >= 2);

    const size_t N = Lanes(d);
    for (size_t i = 0; i < padded; i += N) {
      const auto val = Load(d, val_lanes.get() + i);
      const auto exp = Load(d, exp_lanes.get() + i);
      HWY_ASSERT_VEC_EQ(d, expected.get() + i, MulByFloorPow2(val, exp));
      HWY_ASSERT_VEC_EQ(d, expected.get() + i, MulByFloorPow2(val, Floor(exp)));
    }

    HWY_BITCASTSCALAR_CXX14_CONSTEXPR const T kNaN = BitCastScalar<T>(
        static_cast<TU>(ExponentMask<T>() | (ExponentMask<T>() >> 1)));
    HWY_BITCASTSCALAR_CXX14_CONSTEXPR const T kNegInf = BitCastScalar<T>(
        static_cast<TU>(ExponentMask<T>() | (TU{1} << (sizeof(TU) * 8 - 1))));
    HWY_BITCASTSCALAR_CXX14_CONSTEXPR const T kPosInf =
        BitCastScalar<T>(ExponentMask<T>());

    val_lanes[0] = kNaN;
    exp_lanes[0] = kNegInf;
    expected[0] = kNaN;

    val_lanes[1] = kNaN;
    exp_lanes[1] = kPosInf;
    expected[1] = kNaN;

    for (size_t i = 0; i < 2; i += N) {
      const auto val = Load(d, val_lanes.get());
      const auto exp = Load(d, exp_lanes.get());
      const auto mul_result = MulByFloorPow2(val, exp);
      const auto actual = Or(
          mul_result, VecFromMask(d, And(And(IsNaN(val), IsInf(exp)),
                                         Eq(mul_result, ZeroIfNegative(exp)))));

      HWY_ASSERT_VEC_EQ(d, expected.get(), actual);
    }
  }
};

HWY_NOINLINE void TestAllMulByFloorPow2() {
  ForFloatTypes(ForPartialVectors<TestMulByFloorPow2>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyMulByPow2Test);
HWY_EXPORT_AND_TEST_P(HwyMulByPow2Test, TestAllMulByPow2);
HWY_EXPORT_AND_TEST_P(HwyMulByPow2Test, TestAllMulByFloorPow2);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
