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

#include "absl/strings/internal/pow10_helper.h"

#include <cmath>

#include "gtest/gtest.h"
#include "absl/strings/str_format.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace strings_internal {

namespace {

struct TestCase {
  int power;           // Testing Pow10(power)
  uint64_t significand;  // Raw bits of the expected value
  int radix;           // significand is adjusted by 2^radix
};

TEST(Pow10HelperTest, Works) {
  // The logic in pow10_helper.cc is so simple that theoretically we don't even
  // need a test. However, we're paranoid and believe that there may be
  // compilers that don't round floating-point literals correctly, even though
  // it is specified by the standard. We check various edge cases, just to be
  // sure.
  constexpr TestCase kTestCases[] = {
      // Subnormals
      {-323, 0x2, -1074},
      {-322, 0x14, -1074},
      {-321, 0xca, -1074},
      {-320, 0x7e8, -1074},
      {-319, 0x4f10, -1074},
      {-318, 0x316a2, -1074},
      {-317, 0x1ee257, -1074},
      {-316, 0x134d761, -1074},
      {-315, 0xc1069cd, -1074},
      {-314, 0x78a42205, -1074},
      {-313, 0x4b6695433, -1074},
      {-312, 0x2f201d49fb, -1074},
      {-311, 0x1d74124e3d1, -1074},
      {-310, 0x12688b70e62b, -1074},
      {-309, 0xb8157268fdaf, -1074},
      {-308, 0x730d67819e8d2, -1074},
      // Values that are very close to rounding the other way.
      // Comment shows difference of significand from the true value.
      {-307, 0x11fa182c40c60d, -1072},  // -.4588
      {-290, 0x18f2b061aea072, -1016},  //  .4854
      {-276, 0x11BA03F5B21000, -969},   //  .4709
      {-259, 0x1899C2F6732210, -913},   //  .4830
      {-252, 0x1D53844EE47DD1, -890},   // -.4743
      {-227, 0x1E5297287C2F45, -807},   // -.4708
      {-198, 0x1322E220A5B17E, -710},   // -.4714
      {-195, 0x12B010D3E1CF56, -700},   //  .4928
      {-192, 0x123FF06EEA847A, -690},   //  .4968
      {-163, 0x1708D0F84D3DE7, -594},   // -.4977
      {-145, 0x13FAAC3E3FA1F3, -534},   // -.4785
      {-111, 0x133D4032C2C7F5, -421},   //  .4774
      {-106, 0x1D5B561574765B, -405},   // -.4869
      {-104, 0x16EF5B40C2FC77, -398},   // -.4741
      {-88, 0x197683DF2F268D, -345},    // -.4738
      {-86, 0x13E497065CD61F, -338},    //  .4736
      {-76, 0x17288E1271F513, -305},    // -.4761
      {-63, 0x1A53FC9631D10D, -262},    //  .4929
      {-30, 0x14484BFEEBC2A0, -152},    //  .4758
      {-21, 0x12E3B40A0E9B4F, -122},    // -.4916
      {-5, 0x14F8B588E368F1, -69},      //  .4829
      {23, 0x152D02C7E14AF6, 24},       // -.5000 (exactly, round-to-even)
      {29, 0x1431E0FAE6D721, 44},       // -.4870
      {34, 0x1ED09BEAD87C03, 60},       // -.4721
      {70, 0x172EBAD6DDC73D, 180},      //  .4733
      {105, 0x1BE7ABD3781ECA, 296},     // -.4850
      {126, 0x17A2ECC414A03F, 366},     // -.4999
      {130, 0x1CDA62055B2D9E, 379},     //  .4855
      {165, 0x115D847AD00087, 496},     // -.4913
      {172, 0x14B378469B6732, 519},     //  .4818
      {187, 0x1262DFEEBBB0F9, 569},     // -.4805
      {210, 0x18557F31326BBB, 645},     // -.4992
      {212, 0x1302CB5E6F642A, 652},     // -.4838
      {215, 0x1290BA9A38C7D1, 662},     // -.4881
      {236, 0x1F736F9B3494E9, 731},     //  .4707
      {244, 0x176EC98994F489, 758},     //  .4924
      {250, 0x1658E3AB795204, 778},     // -.4963
      {252, 0x117571DDF6C814, 785},     //  .4873
      {254, 0x1B4781EAD1989E, 791},     // -.4887
      {260, 0x1A03FDE214CAF1, 811},     //  .4784
      {284, 0x1585041B2C477F, 891},     //  .4798
      {304, 0x1D2A1BE4048F90, 957},     // -.4987
      // Out-of-range values
      {-324, 0x0, 0},
      {-325, 0x0, 0},
      {-326, 0x0, 0},
      {309, 1, 2000},
      {310, 1, 2000},
      {311, 1, 2000},
  };
  for (const TestCase& test_case : kTestCases) {
    EXPECT_EQ(Pow10(test_case.power),
              std::ldexp(test_case.significand, test_case.radix))
        << absl::StrFormat("Failure for Pow10(%d): %a vs %a", test_case.power,
                           Pow10(test_case.power),
                           std::ldexp(test_case.significand, test_case.radix));
  }
}

}  // namespace
}  // namespace strings_internal
ABSL_NAMESPACE_END
}  // namespace absl
