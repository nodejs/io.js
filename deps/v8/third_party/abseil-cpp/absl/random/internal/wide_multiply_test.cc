// Copyright 2017 The Abseil Authors.
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

#include "absl/random/internal/wide_multiply.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/numeric/int128.h"

using absl::random_internal::MultiplyU128ToU256;
using absl::random_internal::U256;

namespace {

U256 LeftShift(U256 v, int s) {
  if (s == 0) {
    return v;
  } else if (s < 128) {
    return {(v.hi << s) | (v.lo >> (128 - s)), v.lo << s};
  } else {
    return {v.lo << (s - 128), 0};
  }
}

MATCHER_P2(Eq256, hi, lo, "") { return arg.hi == hi && arg.lo == lo; }
MATCHER_P(Eq256, v, "") { return arg.hi == v.hi && arg.lo == v.lo; }

TEST(WideMultiplyTest, MultiplyU128ToU256Test) {
  using absl::uint128;
  constexpr uint128 k1 = 1;
  constexpr uint128 kMax = ~static_cast<uint128>(0);

  EXPECT_THAT(MultiplyU128ToU256(0, 0), Eq256(0, 0));

  // Max uin128_t
  EXPECT_THAT(MultiplyU128ToU256(kMax, kMax), Eq256(kMax << 1, 1));
  EXPECT_THAT(MultiplyU128ToU256(kMax, 1), Eq256(0, kMax));
  EXPECT_THAT(MultiplyU128ToU256(1, kMax), Eq256(0, kMax));
  for (int i = 0; i < 64; ++i) {
    SCOPED_TRACE(i);
    EXPECT_THAT(MultiplyU128ToU256(kMax, k1 << i),
                Eq256(LeftShift({0, kMax}, i)));
    EXPECT_THAT(MultiplyU128ToU256(k1 << i, kMax),
                Eq256(LeftShift({0, kMax}, i)));
  }

  // 1-bit x 1-bit.
  for (int i = 0; i < 64; ++i) {
    for (int j = 0; j < 64; ++j) {
      EXPECT_THAT(MultiplyU128ToU256(k1 << i, k1 << j),
                  Eq256(LeftShift({0, 1}, i + j)));
    }
  }

  // Verified multiplies
  EXPECT_THAT(MultiplyU128ToU256(
                  absl::MakeUint128(0xc502da0d6ea99fe8, 0xfa3c9141a1f50912),
                  absl::MakeUint128(0x96bcf1ac37f97bd6, 0x27e2cdeb5fb2299e)),
              Eq256(absl::MakeUint128(0x740113d838f96a64, 0x22e8cfa4d71f89ea),
                    absl::MakeUint128(0x19184a345c62e993, 0x237871b630337b1c)));
  EXPECT_THAT(MultiplyU128ToU256(
                  absl::MakeUint128(0x6f29e670cee07230, 0xc3d8e6c3e4d86759),
                  absl::MakeUint128(0x3227d29fa6386db1, 0x231682bb1e4b764f)),
              Eq256(absl::MakeUint128(0x15c779d9d5d3b07c, 0xd7e6c827f0c81cbe),
                    absl::MakeUint128(0xf88e3914f7fa287a, 0x15b79975137dea77)));
  EXPECT_THAT(MultiplyU128ToU256(
                  absl::MakeUint128(0xafb77107215646e1, 0x3b844cb1ac5769e7),
                  absl::MakeUint128(0x1ff7b2d888b62479, 0x92f758ae96fcba0b)),
              Eq256(absl::MakeUint128(0x15f13b70181f6985, 0x2adb36bbabce7d02),
                    absl::MakeUint128(0x6c470d72e13aad04, 0x63fba3f5841762ed)));
  EXPECT_THAT(MultiplyU128ToU256(
                  absl::MakeUint128(0xd85d5558d67ac905, 0xf88c70654dae19b1),
                  absl::MakeUint128(0x17252c6727db3738, 0x399ff658c511eedc)),
              Eq256(absl::MakeUint128(0x138fcdaf8b0421ee, 0x1b465ddf2a0d03f6),
                    absl::MakeUint128(0x8f573ba68296860f, 0xf327d2738741a21c)));
  EXPECT_THAT(MultiplyU128ToU256(
                  absl::MakeUint128(0x46f0421a37ff6bee, 0xa61df89f09d140b1),
                  absl::MakeUint128(0x3d712ec9f37ca2e1, 0x9658a2cba47ef4b1)),
              Eq256(absl::MakeUint128(0x11069cc48ee7c95d, 0xd35fb1c7aa91c978),
                    absl::MakeUint128(0xbe2f4a6de874b015, 0xd2f7ac1b76746e61)));
  EXPECT_THAT(MultiplyU128ToU256(
                  absl::MakeUint128(0x730d27c72d58fa49, 0x3ebeda7498f8827c),
                  absl::MakeUint128(0xa2c959eca9f503af, 0x189c687eb842bbd8)),
              Eq256(absl::MakeUint128(0x4928d0ea356ba022, 0x1546d34a2963393),
                    absl::MakeUint128(0x7481531e1e0a16d1, 0xdd8025015cf6aca0)));
  EXPECT_THAT(MultiplyU128ToU256(
                  absl::MakeUint128(0x6ca41020f856d2f1, 0xb9b0838c04a7f4aa),
                  absl::MakeUint128(0x9cf41d28a8396f54, 0x1d681695e377ffe6)),
              Eq256(absl::MakeUint128(0x429b92934d9be6f1, 0xea182877157c1e7),
                    absl::MakeUint128(0x7135c23f0a4a475, 0xc1adc366f4a126bc)));
  EXPECT_THAT(MultiplyU128ToU256(
                  absl::MakeUint128(0x57472833797c332, 0x6c79272fdec4687a),
                  absl::MakeUint128(0xb5f022ea3838e46b, 0x16face2f003e27a6)),
              Eq256(absl::MakeUint128(0x3e072e0962b3400, 0x5d9fe8fdc3d0e1f4),
                    absl::MakeUint128(0x7dc0df47cedafd62, 0xbe6501f1acd2551c)));
  EXPECT_THAT(MultiplyU128ToU256(
                  absl::MakeUint128(0xf0fb4198322eb1c2, 0xfe7f5f31f3885938),
                  absl::MakeUint128(0xd99012b71bb7aa31, 0xac7a6f9eb190789)),
              Eq256(absl::MakeUint128(0xcccc998cf075ca01, 0x642d144322fb873a),
                    absl::MakeUint128(0xc79dc12b69d91ed4, 0xa83459132ce046f8)));
  EXPECT_THAT(MultiplyU128ToU256(
                  absl::MakeUint128(0xb5c04120848cdb47, 0x8aa62a827bf52635),
                  absl::MakeUint128(0x8d07a359be2f1380, 0x467bb90d59da0dea)),
              Eq256(absl::MakeUint128(0x64205019d139a9ce, 0x99425c5fb6e7a977),
                    absl::MakeUint128(0xd3e99628a9e5fca7, 0x9c7824cb7279d72)));
}

}  // namespace
