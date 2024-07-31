// Copyright 2020 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/hash/internal/low_level_hash.h"

#include <cinttypes>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/escaping.h"

#define UPDATE_GOLDEN 0

namespace {

static const uint64_t kSalt[5] = {0xa0761d6478bd642f, 0xe7037ed1a0b428dbl,
                                  0x8ebc6af09c88c6e3, 0x589965cc75374cc3l,
                                  0x1d8e4e27c47d124f};

TEST(LowLevelHashTest, VerifyGolden) {
  constexpr size_t kNumGoldenOutputs = 134;
  static struct {
    absl::string_view base64_data;
    uint64_t seed;
  } cases[] = {
      {"", uint64_t{0xec42b7ab404b8acb}},
      {"ICAg", uint64_t{0}},
      {"YWFhYQ==", uint64_t{0}},
      {"AQID", uint64_t{0}},
      {"AQIDBA==", uint64_t{0}},
      {"dGhpcmRfcGFydHl8d3loYXNofDY0", uint64_t{0}},
      {"Zw==", uint64_t{0xeeee074043a3ee0f}},
      {"xmk=", uint64_t{0x857902089c393de}},
      {"c1H/", uint64_t{0x993df040024ca3af}},
      {"SuwpzQ==", uint64_t{0xc4e4c2acea740e96}},
      {"uqvy++M=", uint64_t{0x6a214b3db872d0cf}},
      {"RnzCVPgb", uint64_t{0x44343db6a89dba4d}},
      {"6OeNdlouYw==", uint64_t{0x77b5d6d1ae1dd483}},
      {"M5/JmmYyDbc=", uint64_t{0x89ab8ecb44d221f1}},
      {"MVijWiVdBRdY", uint64_t{0x60244b17577ca81b}},
      {"6V7Uq7LNxpu0VA==", uint64_t{0x59a08dcee0717067}},
      {"EQ6CdEEhPdyHcOk=", uint64_t{0xf5f20db3ade57396}},
      {"PqFB4fxnPgF+l+rc", uint64_t{0xbf8dee0751ad3efb}},
      {"a5aPOFwq7LA7+zKvPA==", uint64_t{0x6b7a06b268d63e30}},
      {"VOwY21wCGv5D+/qqOvs=", uint64_t{0xb8c37f0ae0f54c82}},
      {"KdHmBTx8lHXYvmGJ+Vy7", uint64_t{0x9fcbed0c38e50eef}},
      {"qJkPlbHr8bMF7/cA6aE65Q==", uint64_t{0x2af4bade1d8e3a1d}},
      {"ygvL0EhHZL0fIx6oHHtkxRQ=", uint64_t{0x714e3aa912da2f2c}},
      {"c1rFXkt5YztwZCQRngncqtSs", uint64_t{0xf5ee75e3cbb82c1c}},
      {"8hsQrzszzeNQSEcVXLtvIhm6mw==", uint64_t{0x620e7007321b93b9}},
      {"ffUL4RocfyP4KfikGxO1yk7omDI=", uint64_t{0xc08528cac2e551fc}},
      {"OOB5TT00vF9Od/rLbAWshiErqhpV", uint64_t{0x6a1debf9cc3ad39}},
      {"or5wtXM7BFzTNpSzr+Lw5J5PMhVJ/Q==", uint64_t{0x7e0a3c88111fc226}},
      {"gk6pCHDUsoopVEiaCrzVDhioRKxb844=", uint64_t{0x1301fef15df39edb}},
      {"TNctmwlC5QbEM6/No4R/La3UdkfeMhzs", uint64_t{0x64e181f3d5817ab}},
      {"SsQw9iAjhWz7sgcE9OwLuSC6hsM+BfHs2Q==", uint64_t{0xafafc44961078ecb}},
      {"ZzO3mVCj4xTT2TT3XqDyEKj2BZQBvrS8RHg=", uint64_t{0x4f7bb45549250094}},
      {"+klp5iPQGtppan5MflEls0iEUzqU+zGZkDJX", uint64_t{0xa30061abaa2818c}},
      {"RO6bvOnlJc8I9eniXlNgqtKy0IX6VNg16NRmgg==",
       uint64_t{0xd902ee3e44a5705f}},
      {"ZJjZqId1ZXBaij9igClE3nyliU5XWdNRrayGlYA=", uint64_t{0x316d36da516f583}},
      {"7BfkhfGMDGbxfMB8uyL85GbaYQtjr2K8g7RpLzr/",
       uint64_t{0x402d83f9f834f616}},
      {"rycWk6wHH7htETQtje9PidS2YzXBx+Qkg2fY7ZYS7A==",
       uint64_t{0x9c604164c016b72c}},
      {"RTkC2OUK+J13CdGllsH0H5WqgspsSa6QzRZouqx6pvI=",
       uint64_t{0x3f4507e01f9e73ba}},
      {"tKjKmbLCNyrLCM9hycOAXm4DKNpM12oZ7dLTmUx5iwAi",
       uint64_t{0xc3fe0d5be8d2c7c7}},
      {"VprUGNH+5NnNRaORxgH/ySrZFQFDL+4VAodhfBNinmn8cg==",
       uint64_t{0x531858a40bfa7ea1}},
      {"gc1xZaY+q0nPcUvOOnWnT3bqfmT/geth/f7Dm2e/DemMfk4=",
       uint64_t{0x86689478a7a7e8fa}},
      {"Mr35fIxqx1ukPAL0su1yFuzzAU3wABCLZ8+ZUFsXn47UmAph",
       uint64_t{0x4ec948b8e7f27288}},
      {"A9G8pw2+m7+rDtWYAdbl8tb2fT7FFo4hLi2vAsa5Y8mKH3CX3g==",
       uint64_t{0xce46c7213c10032}},
      {"DFaJGishGwEHDdj9ixbCoaTjz9KS0phLNWHVVdFsM93CvPft3hM=",
       uint64_t{0xf63e96ee6f32a8b6}},
      {"7+Ugx+Kr3aRNgYgcUxru62YkTDt5Hqis+2po81hGBkcrJg4N0uuy",
       uint64_t{0x1cfe85e65fc5225}},
      {"H2w6O8BUKqu6Tvj2xxaecxEI2wRgIgqnTTG1WwOgDSINR13Nm4d4Vg==",
       uint64_t{0x45c474f1cee1d2e8}},
      {"1XBMnIbqD5jy65xTDaf6WtiwtdtQwv1dCVoqpeKj+7cTR1SaMWMyI04=",
       uint64_t{0x6e024e14015f329c}},
      {"znZbdXG2TSFrKHEuJc83gPncYpzXGbAebUpP0XxzH0rpe8BaMQ17nDbt",
       uint64_t{0x760c40502103ae1c}},
      {"ylu8Atu13j1StlcC1MRMJJXIl7USgDDS22HgVv0WQ8hx/8pNtaiKB17hCQ==",
       uint64_t{0x17fd05c3c560c320}},
      {"M6ZVVzsd7vAvbiACSYHioH/440dp4xG2mLlBnxgiqEvI/aIEGpD0Sf4VS0g=",
       uint64_t{0x8b34200a6f8e90d9}},
      {"li3oFSXLXI+ubUVGJ4blP6mNinGKLHWkvGruun85AhVn6iuMtocbZPVhqxzn",
       uint64_t{0x6be89e50818bdf69}},
      {"kFuQHuUCqBF3Tc3hO4dgdIp223ShaCoog48d5Do5zMqUXOh5XpGK1t5XtxnfGA==",
       uint64_t{0xfb389773315b47d8}},
      {"jWmOad0v0QhXVJd1OdGuBZtDYYS8wBVHlvOeTQx9ZZnm8wLEItPMeihj72E0nWY=",
       uint64_t{0x4f2512a23f61efee}},
      {"z+DHU52HaOQdW4JrZwDQAebEA6rm13Zg/9lPYA3txt3NjTBqFZlOMvTRnVzRbl23",
       uint64_t{0x59ccd92fc16c6fda}},
      {"MmBiGDfYeTayyJa/tVycg+rN7f9mPDFaDc+23j0TlW9094er0ADigsl4QX7V3gG/qw==",
       uint64_t{0x25c5a7f5bd330919}},
      {"774RK+9rOL4iFvs1q2qpo/JVc/I39buvNjqEFDtDvyoB0FXxPI2vXqOrk08VPfIHkmU=",
       uint64_t{0x51df4174d34c97d7}},
      {"+slatXiQ7/2lK0BkVUI1qzNxOOLP3I1iK6OfHaoxgqT63FpzbElwEXSwdsryq3UlHK0I",
       uint64_t{0x80ce6d76f89cb57}},
      {"64mVTbQ47dHjHlOHGS/hjJwr/"
       "K2frCNpn87exOqMzNUVYiPKmhCbfS7vBUce5tO6Ec9osQ==",
       uint64_t{0x20961c911965f684}},
      {"fIsaG1r530SFrBqaDj1kqE0AJnvvK8MNEZbII2Yw1OK77v0V59xabIh0B5axaz/"
       "+a2V5WpA=",
       uint64_t{0x4e5b926ec83868e7}},
      {"PGih0zDEOWCYGxuHGDFu9Ivbff/"
       "iE7BNUq65tycTR2R76TerrXALRosnzaNYO5fjFhTi+CiS",
       uint64_t{0x3927b30b922eecef}},
      {"RnpA/"
       "zJnEnnLjmICORByRVb9bCOgxF44p3VMiW10G7PvW7IhwsWajlP9kIwNA9FjAD2GoQHk2Q="
       "=",
       uint64_t{0xbd0291284a49b61c}},
      {"qFklMceaTHqJpy2qavJE+EVBiNFOi6OxjOA3LeIcBop1K7w8xQi3TrDk+"
       "BrWPRIbfprszSaPfrI=",
       uint64_t{0x73a77c575bcc956}},
      {"cLbfUtLl3EcQmITWoTskUR8da/VafRDYF/ylPYwk7/"
       "zazk6ssyrzxMN3mmSyvrXR2yDGNZ3WDrTT",
       uint64_t{0x766a0e2ade6d09a6}},
      {"s/"
       "Jf1+"
       "FbsbCpXWPTUSeWyMH6e4CvTFvPE5Fs6Z8hvFITGyr0dtukHzkI84oviVLxhM1xMxrMAy1db"
       "w==",
       uint64_t{0x2599f4f905115869}},
      {"FvyQ00+j7nmYZVQ8hI1Edxd0AWplhTfWuFGiu34AK5X8u2hLX1bE97sZM0CmeLe+"
       "7LgoUT1fJ/axybE=",
       uint64_t{0xd8256e5444d21e53}},
      {"L8ncxMaYLBH3g9buPu8hfpWZNlOF7nvWLNv9IozH07uQsIBWSKxoPy8+"
       "LW4tTuzC6CIWbRGRRD1sQV/4",
       uint64_t{0xf664a91333fb8dfd}},
      {"CDK0meI07yrgV2kQlZZ+"
       "wuVqhc2NmzqeLH7bmcA6kchsRWFPeVF5Wqjjaj556ABeUoUr3yBmfU3kWOakkg==",
       uint64_t{0x9625b859be372cd1}},
      {"d23/vc5ONh/"
       "HkMiq+gYk4gaCNYyuFKwUkvn46t+dfVcKfBTYykr4kdvAPNXGYLjM4u1YkAEFpJP+"
       "nX7eOvs=",
       uint64_t{0x7b99940782e29898}},
      {"NUR3SRxBkxTSbtQORJpu/GdR6b/h6sSGfsMj/KFd99ahbh+9r7LSgSGmkGVB/"
       "mGoT0pnMTQst7Lv2q6QN6Vm",
       uint64_t{0x4fe12fa5383b51a8}},
      {"2BOFlcI3Z0RYDtS9T9Ie9yJoXlOdigpPeeT+CRujb/"
       "O39Ih5LPC9hP6RQk1kYESGyaLZZi3jtabHs7DiVx/VDg==",
       uint64_t{0xe2ccb09ac0f5b4b6}},
      {"FF2HQE1FxEvWBpg6Z9zAMH+Zlqx8S1JD/"
       "wIlViL6ZDZY63alMDrxB0GJQahmAtjlm26RGLnjW7jmgQ4Ie3I+014=",
       uint64_t{0x7d0a37adbd7b753b}},
      {"tHmO7mqVL/PX11nZrz50Hc+M17Poj5lpnqHkEN+4bpMx/"
       "YGbkrGOaYjoQjgmt1X2QyypK7xClFrjeWrCMdlVYtbW",
       uint64_t{0xd3ae96ef9f7185f2}},
      {"/WiHi9IQcxRImsudkA/KOTqGe8/"
       "gXkhKIHkjddv5S9hi02M049dIK3EUyAEjkjpdGLUs+BN0QzPtZqjIYPOgwsYE9g==",
       uint64_t{0x4fb88ea63f79a0d8}},
      {"qds+1ExSnU11L4fTSDz/QE90g4Jh6ioqSh3KDOTOAo2pQGL1k/"
       "9CCC7J23YF27dUTzrWsCQA2m4epXoCc3yPHb3xElA=",
       uint64_t{0xed564e259bb5ebe9}},
      {"8FVYHx40lSQPTHheh08Oq0/"
       "pGm2OlG8BEf8ezvAxHuGGdgCkqpXIueJBF2mQJhTfDy5NncO8ntS7vaKs7sCNdDaNGOEi",
       uint64_t{0x3e3256b60c428000}},
      {"4ZoEIrJtstiCkeew3oRzmyJHVt/pAs2pj0HgHFrBPztbQ10NsQ/"
       "lM6DM439QVxpznnBSiHMgMQJhER+70l72LqFTO1JiIQ==",
       uint64_t{0xfb05bad59ec8705}},
      {"hQPtaYI+wJyxXgwD5n8jGIKFKaFA/"
       "P83KqCKZfPthnjwdOFysqEOYwAaZuaaiv4cDyi9TyS8hk5cEbNP/jrI7q6pYGBLbsM=",
       uint64_t{0xafdc251dbf97b5f8}},
      {"S4gpMSKzMD7CWPsSfLeYyhSpfWOntyuVZdX1xSBjiGvsspwOZcxNKCRIOqAA0moUfOh3I5+"
       "juQV4rsqYElMD/gWfDGpsWZKQ",
       uint64_t{0x10ec9c92ddb5dcbc}},
      {"oswxop+"
       "bthuDLT4j0PcoSKby4LhF47ZKg8K17xxHf74UsGCzTBbOz0MM8hQEGlyqDT1iUiAYnaPaUp"
       "L2mRK0rcIUYA4qLt5uOw==",
       uint64_t{0x9a767d5822c7dac4}},
      {"0II/"
       "697p+"
       "BtLSjxj5989OXI004TogEb94VUnDzOVSgMXie72cuYRvTFNIBgtXlKfkiUjeqVpd4a+"
       "n5bxNOD1TGrjQtzKU5r7obo=",
       uint64_t{0xee46254080d6e2db}},
      {"E84YZW2qipAlMPmctrg7TKlwLZ68l4L+c0xRDUfyyFrA4MAti0q9sHq3TDFviH0Y+"
       "Kq3tEE5srWFA8LM9oomtmvm5PYxoaarWPLc",
       uint64_t{0xbbb669588d8bf398}},
      {"x3pa4HIElyZG0Nj7Vdy9IdJIR4izLmypXw5PCmZB5y68QQ4uRaVVi3UthsoJROvbjDJkP2D"
       "Q6L/eN8pFeLFzNPKBYzcmuMOb5Ull7w==",
       uint64_t{0xdc2afaa529beef44}},
      {"jVDKGYIuWOP/"
       "QKLdd2wi8B2VJA8Wh0c8PwrXJVM8FOGM3voPDVPyDJOU6QsBDPseoR8uuKd19OZ/"
       "zAvSCB+zlf6upAsBlheUKgCfKww=",
       uint64_t{0xf1f67391d45013a8}},
      {"mkquunhmYe1aR2wmUz4vcvLEcKBoe6H+kjUok9VUn2+eTSkWs4oDDtJvNCWtY5efJwg/"
       "j4PgjRYWtqnrCkhaqJaEvkkOwVfgMIwF3e+d",
       uint64_t{0x16fce2b8c65a3429}},
      {"fRelvKYonTQ+s+rnnvQw+JzGfFoPixtna0vzcSjiDqX5s2Kg2//"
       "UGrK+AVCyMUhO98WoB1DDbrsOYSw2QzrcPe0+3ck9sePvb+Q/IRaHbw==",
       uint64_t{0xf4b096699f49fe67}},
      {"DUwXFJzagljo44QeJ7/"
       "6ZKw4QXV18lhkYT2jglMr8WB3CHUU4vdsytvw6AKv42ZcG6fRkZkq9fpnmXy6xG0aO3WPT1"
       "eHuyFirAlkW+zKtwg=",
       uint64_t{0xca584c4bc8198682}},
      {"cYmZCrOOBBongNTr7e4nYn52uQUy2mfe48s50JXx2AZ6cRAt/"
       "xRHJ5QbEoEJOeOHsJyM4nbzwFm++SlT6gFZZHJpkXJ92JkR86uS/eV1hJUR",
       uint64_t{0xed269fc3818b6aad}},
      {"EXeHBDfhwzAKFhsMcH9+2RHwV+mJaN01+9oacF6vgm8mCXRd6jeN9U2oAb0of5c5cO4i+"
       "Vb/LlHZSMI490SnHU0bejhSCC2gsC5d2K30ER3iNA==",
       uint64_t{0x33f253cbb8fe66a8}},
      {"FzkzRYoNjkxFhZDso94IHRZaJUP61nFYrh5MwDwv9FNoJ5jyNCY/"
       "eazPZk+tbmzDyJIGw2h3GxaWZ9bSlsol/vK98SbkMKCQ/wbfrXRLcDzdd/8=",
       uint64_t{0xd0b76b2c1523d99c}},
      {"Re4aXISCMlYY/XsX7zkIFR04ta03u4zkL9dVbLXMa/q6hlY/CImVIIYRN3VKP4pnd0AUr/"
       "ugkyt36JcstAInb4h9rpAGQ7GMVOgBniiMBZ/MGU7H",
       uint64_t{0xfd28f0811a2a237f}},
      {"ueLyMcqJXX+MhO4UApylCN9WlTQ+"
       "ltJmItgG7vFUtqs2qNwBMjmAvr5u0sAKd8jpzV0dDPTwchbIeAW5zbtkA2NABJV6hFM48ib"
       "4/J3A5mseA3cS8w==",
       uint64_t{0x6261fb136482e84}},
      {"6Si7Yi11L+jZMkwaN+GUuzXMrlvEqviEkGOilNq0h8TdQyYKuFXzkYc/"
       "q74gP3pVCyiwz9KpVGMM9vfnq36riMHRknkmhQutxLZs5fbmOgEO69HglCU=",
       uint64_t{0x458efc750bca7c3a}},
      {"Q6AbOofGuTJOegPh9Clm/"
       "9crtUMQqylKrTc1fhfJo1tqvpXxhU4k08kntL1RG7woRnFrVh2UoMrL1kjin+s9CanT+"
       "y4hHwLqRranl9FjvxfVKm3yvg68",
       uint64_t{0xa7e69ff84e5e7c27}},
      {"ieQEbIPvqY2YfIjHnqfJiO1/MIVRk0RoaG/WWi3kFrfIGiNLCczYoklgaecHMm/"
       "1sZ96AjO+a5stQfZbJQwS7Sc1ODABEdJKcTsxeW2hbh9A6CFzpowP1A==",
       uint64_t{0x3c59bfd0c29efe9e}},
      {"zQUv8hFB3zh2GGl3KTvCmnfzE+"
       "SUgQPVaSVIELFX5H9cE3FuVFGmymkPQZJLAyzC90Cmi8GqYCvPqTuAAB//"
       "XTJxy4bCcVArgZG9zJXpjowpNBfr3ngWrSE=",
       uint64_t{0x10befacc6afd298d}},
      {"US4hcC1+op5JKGC7eIs8CUgInjKWKlvKQkapulxW262E/"
       "B2ye79QxOexf188u2mFwwe3WTISJHRZzS61IwljqAWAWoBAqkUnW8SHmIDwHUP31J0p5sGd"
       "P47L",
       uint64_t{0x41d5320b0a38efa7}},
      {"9bHUWFna2LNaGF6fQLlkx1Hkt24nrkLE2CmFdWgTQV3FFbUe747SSqYw6ebpTa07MWSpWRP"
       "sHesVo2B9tqHbe7eQmqYebPDFnNqrhSdZwFm9arLQVs+7a3Ic6A==",
       uint64_t{0x58db1c7450fe17f3}},
      {"Kb3DpHRUPhtyqgs3RuXjzA08jGb59hjKTOeFt1qhoINfYyfTt2buKhD6YVffRCPsgK9SeqZ"
       "qRPJSyaqsa0ovyq1WnWW8jI/NhvAkZTVHUrX2pC+cD3OPYT05Dag=",
       uint64_t{0x6098c055a335b7a6}},
      {"gzxyMJIPlU+bJBwhFUCHSofZ/"
       "319LxqMoqnt3+L6h2U2+ZXJCSsYpE80xmR0Ta77Jq54o92SMH87HV8dGOaCTuAYF+"
       "lDL42SY1P316Cl0sZTS2ow3ZqwGbcPNs/1",
       uint64_t{0x1bbacec67845a801}},
      {"uR7V0TW+FGVMpsifnaBAQ3IGlr1wx5sKd7TChuqRe6OvUXTlD4hKWy8S+"
       "8yyOw8lQabism19vOQxfmocEOW/"
       "vzY0pEa87qHrAZy4s9fH2Bltu8vaOIe+agYohhYORQ==",
       uint64_t{0xc419cfc7442190}},
      {"1UR5eoo2aCwhacjZHaCh9bkOsITp6QunUxHQ2SfeHv0imHetzt/"
       "Z70mhyWZBalv6eAx+YfWKCUib2SHDtz/"
       "A2dc3hqUWX5VfAV7FQsghPUAtu6IiRatq4YSLpDvKZBQ=",
       uint64_t{0xc95e510d94ba270c}},
      {"opubR7H63BH7OtY+Avd7QyQ25UZ8kLBdFDsBTwZlY6gA/"
       "u+x+"
       "czC9AaZMgmQrUy15DH7YMGsvdXnviTtI4eVI4aF1H9Rl3NXMKZgwFOsdTfdcZeeHVRzBBKX"
       "8jUfh1il",
       uint64_t{0xff1ae05c98089c3f}},
      {"DC0kXcSXtfQ9FbSRwirIn5tgPri0sbzHSa78aDZVDUKCMaBGyFU6BmrulywYX8yzvwprdLs"
       "oOwTWN2wMjHlPDqrvVHNEjnmufRDblW+nSS+xtKNs3N5xsxXdv6JXDrAB/Q==",
       uint64_t{0x90c02b8dceced493}},
      {"BXRBk+3wEP3Lpm1y75wjoz+PgB0AMzLe8tQ1AYU2/"
       "oqrQB2YMC6W+9QDbcOfkGbeH+b7IBkt/"
       "gwCMw2HaQsRFEsurXtcQ3YwRuPz5XNaw5NAvrNa67Fm7eRzdE1+hWLKtA8=",
       uint64_t{0x9f8a76697ab1aa36}},
      {"RRBSvEGYnzR9E45Aps/+WSnpCo/X7gJLO4DRnUqFrJCV/kzWlusLE/"
       "6ZU6RoUf2ROwcgEvUiXTGjLs7ts3t9SXnJHxC1KiOzxHdYLMhVvgNd3hVSAXODpKFSkVXND"
       "55G2L1W",
       uint64_t{0x6ba1bf3d811a531d}},
      {"jeh6Qazxmdi57pa9S3XSnnZFIRrnc6s8QLrah5OX3SB/V2ErSPoEAumavzQPkdKF1/"
       "SfvmdL+qgF1C+Yawy562QaFqwVGq7+tW0yxP8FStb56ZRgNI4IOmI30s1Ei7iops9Uuw==",
       uint64_t{0x6a418974109c67b4}},
      {"6QO5nnDrY2/"
       "wrUXpltlKy2dSBcmK15fOY092CR7KxAjNfaY+"
       "aAmtWbbzQk3MjBg03x39afSUN1fkrWACdyQKRaGxgwq6MGNxI6W+8DLWJBHzIXrntrE/"
       "ml6fnNXEpxplWJ1vEs4=",
       uint64_t{0x8472f1c2b3d230a3}},
      {"0oPxeEHhqhcFuwonNfLd5jF3RNATGZS6NPoS0WklnzyokbTqcl4BeBkMn07+fDQv83j/"
       "BpGUwcWO05f3+DYzocfnizpFjLJemFGsls3gxcBYxcbqWYev51tG3lN9EvRE+X9+Pwww",
       uint64_t{0x5e06068f884e73a7}},
      {"naSBSjtOKgAOg8XVbR5cHAW3Y+QL4Pb/JO9/"
       "oy6L08wvVRZqo0BrssMwhzBP401Um7A4ppAupbQeJFdMrysY34AuSSNvtNUy5VxjNECwiNt"
       "gwYHw7yakDUv8WvonctmnoSPKENegQg==",
       uint64_t{0x55290b1a8f170f59}},
      {"vPyl8DxVeRe1OpilKb9KNwpGkQRtA94UpAHetNh+"
       "95V7nIW38v7PpzhnTWIml5kw3So1Si0TXtIUPIbsu32BNhoH7QwFvLM+"
       "JACgSpc5e3RjsL6Qwxxi11npwxRmRUqATDeMUfRAjxg=",
       uint64_t{0x5501cfd83dfe706a}},
      {"QC9i2GjdTMuNC1xQJ74ngKfrlA4w3o58FhvNCltdIpuMhHP1YsDA78scQPLbZ3OCUgeQguY"
       "f/vw6zAaVKSgwtaykqg5ka/4vhz4hYqWU5ficdXqClHl+zkWEY26slCNYOM5nnDlly8Cj",
       uint64_t{0xe43ed13d13a66990}},
      {"7CNIgQhAHX27nxI0HeB5oUTnTdgKpRDYDKwRcXfSFGP1XeT9nQF6WKCMjL1tBV6x7KuJ91G"
       "Zz11F4c+8s+MfqEAEpd4FHzamrMNjGcjCyrVtU6y+7HscMVzr7Q/"
       "ODLcPEFztFnwjvCjmHw==",
       uint64_t{0xdf43bc375cf5283f}},
      {"Qa/hC2RPXhANSospe+gUaPfjdK/yhQvfm4cCV6/pdvCYWPv8p1kMtKOX3h5/"
       "8oZ31fsmx4Axphu5qXJokuhZKkBUJueuMpxRyXpwSWz2wELx5glxF7CM0Fn+"
       "OevnkhUn5jsPlG2r5jYlVn8=",
       uint64_t{0x8112b806d288d7b5}},
      {"kUw/0z4l3a89jTwN5jpG0SHY5km/"
       "IVhTjgM5xCiPRLncg40aqWrJ5vcF891AOq5hEpSq0bUCJUMFXgct7kvnys905HjerV7Vs1G"
       "y84tgVJ70/2+pAZTsB/PzNOE/G6sOj4+GbTzkQu819OLB",
       uint64_t{0xd52a18abb001cb46}},
      {"VDdfSDbO8Tdj3T5W0XM3EI7iHh5xpIutiM6dvcJ/fhe23V/srFEkDy5iZf/"
       "VnA9kfi2C79ENnFnbOReeuZW1b3MUXB9lgC6U4pOTuC+"
       "jHK3Qnpyiqzj7h3ISJSuo2pob7vY6VHZo6Fn7exEqHg==",
       uint64_t{0xe12b76a2433a1236}},
      {"Ldfvy3ORdquM/R2fIkhH/ONi69mcP1AEJ6n/"
       "oropwecAsLJzQSgezSY8bEiEs0VnFTBBsW+RtZY6tDj03fnb3amNUOq1b7jbqyQkL9hpl+"
       "2Z2J8IaVSeownWl+bQcsR5/xRktIMckC5AtF4YHfU=",
       uint64_t{0x175bf7319cf1fa00}},
      {"BrbNpb42+"
       "VzZAjJw6QLirXzhweCVRfwlczzZ0VX2xluskwBqyfnGovz5EuX79JJ31VNXa5hTkAyQat3l"
       "YKRADTdAdwE5PqM1N7YaMqqsqoAAAeuYVXuk5eWCykYmClNdSspegwgCuT+403JigBzi",
       uint64_t{0xd63d57b3f67525ae}},
      {"gB3NGHJJvVcuPyF0ZSvHwnWSIfmaI7La24VMPQVoIIWF7Z74NltPZZpx2f+cocESM+"
       "ILzQW9p+BC8x5IWz7N4Str2WLGKMdgmaBfNkEhSHQDU0IJEOnpUt0HmjhFaBlx0/"
       "LTmhua+rQ6Wup8ezLwfg==",
       uint64_t{0x933faea858832b73}},
      {"hTKHlRxx6Pl4gjG+6ksvvj0CWFicUg3WrPdSJypDpq91LUWRni2KF6+"
       "81ZoHBFhEBrCdogKqeK+hy9bLDnx7g6rAFUjtn1+cWzQ2YjiOpz4+"
       "ROBB7lnwjyTGWzJD1rXtlso1g2qVH8XJVigC5M9AIxM=",
       uint64_t{0x53d061e5f8e7c04f}},
      {"IWQBelSQnhrr0F3BhUpXUIDauhX6f95Qp+A0diFXiUK7irwPG1oqBiqHyK/SH/"
       "9S+"
       "rln9DlFROAmeFdH0OCJi2tFm4afxYzJTFR4HnR4cG4x12JqHaZLQx6iiu6CE3rtWBVz99oA"
       "wCZUOEXIsLU24o2Y",
       uint64_t{0xdb4124556dd515e0}},
      {"TKo+l+"
       "1dOXdLvIrFqeLaHdm0HZnbcdEgOoLVcGRiCbAMR0j5pIFw8D36tefckAS1RCFOH5IgP8yiF"
       "T0Gd0a2hI3+"
       "fTKA7iK96NekxWeoeqzJyctc6QsoiyBlkZerRxs5RplrxoeNg29kKDTM0K94mnhD9g==",
       uint64_t{0x4fb31a0dd681ee71}},
      {"YU4e7G6EfQYvxCFoCrrT0EFgVLHFfOWRTJQJ5gxM3G2b+"
       "1kJf9YPrpsxF6Xr6nYtS8reEEbDoZJYqnlk9lXSkVArm88Cqn6d25VCx3+"
       "49MqC0trIlXtb7SXUUhwpJK16T0hJUfPH7s5cMZXc6YmmbFuBNPE=",
       uint64_t{0x27cc72eefa138e4c}},
      {"/I/"
       "eImMwPo1U6wekNFD1Jxjk9XQVi1D+"
       "FPdqcHifYXQuP5aScNQfxMAmaPR2XhuOQhADV5tTVbBKwCDCX4E3jcDNHzCiPvViZF1W27t"
       "xaf2BbFQdwKrNCmrtzcluBFYu0XZfc7RU1RmxK/RtnF1qHsq/O4pp",
       uint64_t{0x44bc2dfba4bd3ced}},
      {"CJTT9WGcY2XykTdo8KodRIA29qsqY0iHzWZRjKHb9alwyJ7RZAE3V5Juv4MY3MeYEr1EPCC"
       "MxO7yFXqT8XA8YTjaMp3bafRt17Pw8JC4iKJ1zN+WWKOESrj+"
       "3aluGQqn8z1EzqY4PH7rLG575PYeWsP98BugdA==",
       uint64_t{0x242da1e3a439bed8}},
      {"ZlhyQwLhXQyIUEnMH/"
       "AEW27vh9xrbNKJxpWGtrEmKhd+nFqAfbeNBQjW0SfG1YI0xQkQMHXjuTt4P/"
       "EpZRtA47ibZDVS8TtaxwyBjuIDwqcN09eCtpC+Ls+"
       "vWDTLmBeDM3u4hmzz4DQAYsLiZYSJcldg9Q3wszw=",
       uint64_t{0xdc559c746e35c139}},
      {"v2KU8y0sCrBghmnm8lzGJlwo6D6ObccAxCf10heoDtYLosk4ztTpLlpSFEyu23MLA1tJkcg"
       "Rko04h19QMG0mOw/"
       "wc93EXAweriBqXfvdaP85sZABwiKO+6rtS9pacRVpYYhHJeVTQ5NzrvBvi1huxAr+"
       "xswhVMfL",
       uint64_t{0xd0b0350275b9989}},
      {"QhKlnIS6BuVCTQsnoE67E/"
       "yrgogE8EwO7xLaEGei26m0gEU4OksefJgppDh3X0x0Cs78Dr9IHK5b977CmZlrTRmwhlP8p"
       "M+UzXPNRNIZuN3ntOum/QhUWP8SGpirheXENWsXMQ/"
       "nxtxakyEtrNkKk471Oov9juP8oQ==",
       uint64_t{0xb04489e41d17730c}},
      {"/ZRMgnoRt+Uo6fUPr9FqQvKX7syhgVqWu+"
       "WUSsiQ68UlN0efSP6Eced5gJZL6tg9gcYJIkhjuQNITU0Q3TjVAnAcobgbJikCn6qZ6pRxK"
       "BY4MTiAlfGD3T7R7hwJwx554MAy++Zb/YUFlnCaCJiwQMnowF7aQzwYFCo=",
       uint64_t{0x2217285eb4572156}},
      {"NB7tU5fNE8nI+SXGfipc7sRkhnSkUF1krjeo6k+8FITaAtdyz+"
       "o7mONgXmGLulBPH9bEwyYhKNVY0L+njNQrZ9YC2aXsFD3PdZsxAFaBT3VXEzh+"
       "NGBTjDASNL3mXyS8Yv1iThGfHoY7T4aR0NYGJ+k+pR6f+KrPC96M",
       uint64_t{0x12c2e8e68aede73b}},
      {"8T6wrqCtEO6/rwxF6lvMeyuigVOLwPipX/FULvwyu+1wa5sQGav/"
       "2FsLHUVn6cGSi0LlFwLewGHPFJDLR0u4t7ZUyM//"
       "x6da0sWgOa5hzDqjsVGmjxEHXiaXKW3i4iSZNuxoNbMQkIbVML+"
       "DkYu9ND0O2swg4itGeVSzXA==",
       uint64_t{0x4d612125bdc4fd00}},
      {"Ntf1bMRdondtMv1CYr3G80iDJ4WSAlKy5H34XdGruQiCrnRGDBa+"
       "eUi7vKp4gp3BBcVGl8eYSasVQQjn7MLvb3BjtXx6c/"
       "bCL7JtpzQKaDnPr9GWRxpBXVxKREgMM7d8lm35EODv0w+"
       "hQLfVSh8OGs7fsBb68nNWPLeeSOo=",
       uint64_t{0x81826b553954464e}},
      {"VsSAw72Ro6xks02kaiLuiTEIWBC5bgqr4WDnmP8vglXzAhixk7td926rm9jNimL+"
       "kroPSygZ9gl63aF5DCPOACXmsbmhDrAQuUzoh9ZKhWgElLQsrqo1KIjWoZT5b5QfVUXY9lS"
       "IBg3U75SqORoTPq7HalxxoIT5diWOcJQi",
       uint64_t{0xc2e5d345dc0ddd2d}},
      {"j+loZ+C87+"
       "bJxNVebg94gU0mSLeDulcHs84tQT7BZM2rzDSLiCNxUedHr1ZWJ9ejTiBa0dqy2I2ABc++"
       "xzOLcv+//YfibtjKtYggC6/3rv0XCc7xu6d/"
       "O6xO+XOBhOWAQ+IHJVHf7wZnDxIXB8AUHsnjEISKj7823biqXjyP3g==",
       uint64_t{0x3da6830a9e32631e}},
      {"f3LlpcPElMkspNtDq5xXyWU62erEaKn7RWKlo540gR6mZsNpK1czV/"
       "sOmqaq8XAQLEn68LKj6/"
       "cFkJukxRzCa4OF1a7cCAXYFp9+wZDu0bw4y63qbpjhdCl8GO6Z2lkcXy7KOzbPE01ukg7+"
       "gN+7uKpoohgAhIwpAKQXmX5xtd0=",
       uint64_t{0xc9ae5c8759b4877a}},
  };

#if defined(ABSL_IS_BIG_ENDIAN)
  constexpr uint64_t kGolden[kNumGoldenOutputs] = {
      0x4c34aacf38f6eee4, 0x88b1366815e50b88, 0x1a36bd0c6150fb9c,
      0xa783aba8a67366c7, 0x5e4a92123ae874f2, 0x0cc9ecf27067ee9a,
      0xbe77aa94940527f9, 0x7ea5c12f2669fe31, 0xa33eed8737d946b9,
      0x310aec5b1340bb36, 0x354e400861c5d8ff, 0x15be98166adcf42f,
      0xc51910b62a90ae51, 0x539d47fc7fdf6a1f, 0x3ebba9daa46eef93,
      0xd96bcd3a9113c17f, 0xc78eaf6256ded15a, 0x98902ed321c2f0d9,
      0x75a4ac96414b954a, 0x2cb90e00a39e307b, 0x46539574626c3637,
      0x186ec89a2be3ff45, 0x972a3bf7531519d2, 0xa14df0d25922364b,
      0xa351e19d22752109, 0x08bd311d8fed4f82, 0xea2b52ddc6af54f9,
      0x5f20549941338336, 0xd43b07422dc2782e, 0x377c68e2acda4835,
      0x1b31a0a663b1d7b3, 0x7388ba5d68058a1a, 0xe382794ea816f032,
      0xd4c3fe7889276ee0, 0x2833030545582ea9, 0x554d32a55e55df32,
      0x8d6d33d7e17b424d, 0xe51a193d03ae1e34, 0xabb6a80835bd66b3,
      0x0e4ba5293f9ce9b7, 0x1ebd8642cb762cdf, 0xcb54b555850888ee,
      0x1e4195e4717c701f, 0x6235a13937f6532a, 0xd460960741e845c0,
      0x2a72168a2d6af7b1, 0x6be38fbbfc5b17de, 0x4ee97cffa0d0fb39,
      0xfdf1119ad5e71a55, 0x0dff7f66b3070727, 0x812d791d6ed62744,
      0x60962919074b70b8, 0x956fa5c7d6872547, 0xee892daa58aae597,
      0xeeda546e998ee369, 0x454481f5eb9b1fa8, 0x1054394634c98b1b,
      0x55bb425415f591fb, 0x9601fa97416232c4, 0xd7a18506519daad7,
      0x90935cb5de039acf, 0xe64054c5146ed359, 0xe5b323fb1e866c09,
      0x10a472555f5ba1bc, 0xe3c0cd57d26e0972, 0x7ca3db7c121da3e8,
      0x7004a89c800bb466, 0x865f69c1a1ff7f39, 0xbe0edd48f0cf2b99,
      0x10e5e4ba3cc400f5, 0xafc2b91a220eef50, 0x6f04a259289b24f1,
      0x2179a8070e880ef0, 0xd6a9a3d023a740c2, 0x96e6d7954755d9b8,
      0xc8e4bddecce5af9f, 0x93941f0fbc724c92, 0xbef5fb15bf76a479,
      0x534dca8f5da86529, 0x70789790feec116b, 0x2a296e167eea1fe9,
      0x54cb1efd2a3ec7ea, 0x357b43897dfeb9f7, 0xd1eda89bc7ff89d3,
      0x434f2e10cbb83c98, 0xeec4cdac46ca69ce, 0xd46aafd52a303206,
      0x4bf05968ff50a5c9, 0x71c533747a6292df, 0xa40bd0d16a36118c,
      0x597b4ee310c395ab, 0xc5b3e3e386172583, 0x12ca0b32284e6c70,
      0xb48995fadcf35630, 0x0646368454cd217d, 0xa21c168e40d765b5,
      0x4260d3811337da30, 0xb72728a01cff78e4, 0x8586920947f4756f,
      0xc21e5f853cae7dc1, 0xf08c9533be9de285, 0x72df06653b4256d6,
      0xf7b7f937f8db1779, 0x976db27dd0418127, 0x9ce863b7bc3f9e00,
      0xebb679854fcf3a0a, 0x2ccebabbcf1afa99, 0x44201d6be451dac5,
      0xb4af71c0e9a537d1, 0xad8fe9bb33ed2681, 0xcb30128bb68df43b,
      0x154d8328903e8d07, 0x5844276dabeabdff, 0xd99017d7d36d930b,
      0xabb0b4774fb261ca, 0x0a43f075d62e67e0, 0x8df7b371355ada6b,
      0xf4c7a40d06513dcf, 0x257a3615955a0372, 0x987ac410bba74c06,
      0xa011a46f25a632a2, 0xa14384b963ddd995, 0xf51b6b8cf9d50ba7,
      0x3acdb91ee3abf18d, 0x34e799be08920e8c, 0x8766748a31304b36,
      0x0aa239d5d0092f2e, 0xadf473ed26628594, 0xc4094b798eb4b79b,
      0xe04ee5f33cd130f4, 0x85045d098c341d46, 0xf936cdf115a890ec,
      0x51d137b6d8d2eb4f, 0xd10738bb2fccc1ef,
  };
#else
  constexpr uint64_t kGolden[kNumGoldenOutputs] = {
      0x4c34aacf38f6eee4, 0x88b1366815e50b88, 0x1a36bd0c6150fb9c,
      0xa783aba8a67366c7, 0xbc89ebdc622314e4, 0x632bc3cfcc7544d8,
      0xbe77aa94940527f9, 0x7ea5c12f2669fe31, 0xa33eed8737d946b9,
      0x74d832ea11fd18ab, 0x49c0487486246cdc, 0x3fdd986c87ddb0a0,
      0xac3fa52a64d7c09a, 0xbff0e330196e7ed2, 0x8c8138d3ad7d3cce,
      0x968c7d4b48e93778, 0xa04c78d3a421f529, 0x8854bc9c3c3c0241,
      0xcccfcdf5a41113fe, 0xe6fc63dc543d984d, 0x00a39ff89e903c05,
      0xaf7e9da25f9a26f9, 0x6e269a13d01a43df, 0x846d2300ce2ecdf8,
      0xe7ea8c8f08478260, 0x9a2db0d62f6232f3, 0x6f66c761d168c59f,
      0x55f9feacaae82043, 0x518084043700f614, 0xb0c8cfc11bead99f,
      0xe4a68fdab6359d80, 0x97b17caa8f92236e, 0x96edf5e8363643dc,
      0x9b3fbcd8d5b254cd, 0x22a263621d9b3a8b, 0xde90bf6f81800a6d,
      0x1b51cae38c2e9513, 0x689215b3c414ef21, 0x064dc85afae8f557,
      0xa2f3a8b51f408378, 0x6907c197ec1f6a3b, 0xfe83a42ef5c1cf13,
      0x9b8b1d8f7a20cc13, 0x1f1681d52ca895d0, 0xd7b1670bf28e0f96,
      0xb32f20f82d8b038a, 0x6a61d030fb2f5253, 0x8eb2bb0bc29ebb39,
      0x144f36f7a9eef95c, 0xe77aa47d29808d8c, 0xf14d34c1fc568bad,
      0x9796dcd4383f3c73, 0xa2f685fc1be7225b, 0xf3791295b16068b1,
      0xb6b8f63424618948, 0x8ac4fd587045db19, 0x7e2aec2c34feb72e,
      0x72e135a6910ccbb1, 0x661ff16f3c904e6f, 0xdf92cf9d67ca092d,
      0x98a9953d79722eef, 0xe0649ed2181d1707, 0xcd8b8478636a297b,
      0x9516258709c8471b, 0xc703b675b51f4394, 0xdb740eae020139f3,
      0x57d1499ac4212ff2, 0x355cc03713d43825, 0x0e71ac9b8b1e101e,
      0x8029fa72258ff559, 0xa2159726b4c16a50, 0x04e61582fba43007,
      0xdab25af835be8cce, 0x13510b1b184705ee, 0xabdbc9e53666fdeb,
      0x94a788fcb8173cef, 0x750d5e031286e722, 0x02559e72f4f5b497,
      0x7d6e0e5996a646fa, 0x66e871b73b014132, 0x2ec170083f8b784f,
      0x34ac9540cfce3fd9, 0x75c5622c6aad1295, 0xf799a6bb2651acc1,
      0x8f6bcd3145bdc452, 0xddd9d326eb584a04, 0x5411af1e3532f8dc,
      0xeb34722f2ad0f509, 0x835bc952a82298cc, 0xeb3839ff60ea92ad,
      0x70bddf1bcdc8a4bc, 0x4bfb3ee86fcde525, 0xc7b3b93b81dfa386,
      0xe66db544d57997e8, 0xf68a1b83fd363187, 0xe9b99bec615b171b,
      0x093fba04d04ad28a, 0xba6117ed4231a303, 0x594bef25f9d4e206,
      0x0a8cba60578b8f67, 0x88f6c7ca10b06019, 0x32a74082aef17b08,
      0xe758222f971e22df, 0x4af14ff4a593e51e, 0xdba651e16cb09044,
      0x3f3ac837d181eaac, 0xa5589a3f89610c01, 0xd409a7c3a18d5643,
      0x8a89444f82962f26, 0x22eb62a13b9771b9, 0xd3a617615256ddd8,
      0x7089b990c4bba297, 0x7d752893783eac4f, 0x1f2fcbb79372c915,
      0x67a4446b17eb9839, 0x70d11df5cae46788, 0x52621e1780b47d0f,
      0xcf63b93a6e590ee6, 0xb6bc96b58ee064b8, 0x2587f8d635ca9c75,
      0xc6bddd62ec5e5d01, 0x957398ad3009cdb7, 0x05b6890b20bcd0d3,
      0xbe6e965ff837222e, 0x47383a87d2b04b1a, 0x7d42207e6d8d7950,
      0x7e981ed12a7f4aa3, 0xdebb05b30769441a, 0xaac5d86f4ff76c49,
      0x384f195ca3248331, 0xec4c4b855e909ca1, 0x6a7eeb5a657d73d5,
      0x9efbebe2fa9c2791, 0x19e7fa0546900c4d,
  };
#endif

#if UPDATE_GOLDEN
  (void)kGolden;  // Silence warning.
  for (size_t i = 0; i < kNumGoldenOutputs; ++i) {
    std::string str;
    ASSERT_TRUE(absl::Base64Unescape(cases[i].base64_data, &str));
    uint64_t h = absl::hash_internal::LowLevelHash(str.data(), str.size(),
                                                   cases[i].seed, kSalt);
    printf("0x%016" PRIx64 ", ", h);
    if (i % 3 == 2) {
      printf("\n");
    }
  }
  printf("\n\n\n");
  EXPECT_FALSE(true);
#else
  for (size_t i = 0; i < kNumGoldenOutputs; ++i) {
    SCOPED_TRACE(::testing::Message()
                 << "i = " << i << "; input = " << cases[i].base64_data);
    std::string str;
    ASSERT_TRUE(absl::Base64Unescape(cases[i].base64_data, &str));
    EXPECT_EQ(absl::hash_internal::LowLevelHash(str.data(), str.size(),
                                                cases[i].seed, kSalt),
              kGolden[i]);
  }
#endif
}

}  // namespace
