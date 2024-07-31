// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/machine-operator-reducer.h"

#include <cstdint>
#include <limits>

#include "src/base/bits.h"
#include "src/base/division-by-constant.h"
#include "src/base/ieee754.h"
#include "src/base/overflowing-math.h"
#include "src/builtins/builtins.h"
#include "src/common/globals.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/machine-operator.h"
#include "src/numbers/conversions-inl.h"
#include "test/unittests/compiler/graph-unittest.h"
#include "test/unittests/compiler/node-test-utils.h"
#include "testing/gmock-support.h"

using testing::AllOf;
using testing::BitEq;
using testing::Capture;
using testing::CaptureEq;
using testing::NanSensitiveDoubleEq;

namespace v8 {
namespace internal {
namespace compiler {

class MachineOperatorReducerTest : public GraphTest {
 public:
  explicit MachineOperatorReducerTest(int num_parameters = 2)
      : GraphTest(num_parameters),
        data_(std::make_unique<Data>(isolate(), zone(), graph(), tick_counter(),
                                     broker())) {}

 protected:
  void Reset() {
    data_ = nullptr;
    GraphTest::Reset();
    data_ = std::make_unique<Data>(isolate(), zone(), graph(), tick_counter(),
                                   broker());
  }

  Reduction Reduce(Node* node) {
    JSOperatorBuilder javascript(zone());
    JSGraph jsgraph(isolate(), graph(), common(), &javascript, nullptr,
                    &data_->machine_);
    MachineOperatorReducer reducer(
        &data_->graph_reducer_, &jsgraph,
        MachineOperatorReducer::kPropagateSignallingNan);
    return reducer.Reduce(node);
  }

  Matcher<Node*> IsTruncatingDiv(const Matcher<Node*>& dividend_matcher,
                                 const int32_t divisor) {
    base::MagicNumbersForDivision<uint32_t> const mag =
        base::SignedDivisionByConstant(base::bit_cast<uint32_t>(divisor));
    int32_t const multiplier = base::bit_cast<int32_t>(mag.multiplier);
    int32_t const shift = base::bit_cast<int32_t>(mag.shift);
    Matcher<Node*> quotient_matcher =
        IsInt32MulHigh(dividend_matcher, IsInt32Constant(multiplier));
    if (divisor > 0 && multiplier < 0) {
      quotient_matcher = IsInt32Add(quotient_matcher, dividend_matcher);
    } else if (divisor < 0 && multiplier > 0) {
      quotient_matcher = IsInt32Sub(quotient_matcher, dividend_matcher);
    }
    if (shift) {
      quotient_matcher = IsWord32Sar(quotient_matcher, IsInt32Constant(shift));
    }
    return IsInt32Add(quotient_matcher,
                      IsWord32Shr(dividend_matcher, IsInt32Constant(31)));
  }

  Matcher<Node*> IsTruncatingDiv64(const Matcher<Node*>& dividend_matcher,
                                   const int64_t divisor) {
    base::MagicNumbersForDivision<uint64_t> const mag =
        base::SignedDivisionByConstant(base::bit_cast<uint64_t>(divisor));
    int64_t const multiplier = base::bit_cast<int64_t>(mag.multiplier);
    int64_t const shift = base::bit_cast<int32_t>(mag.shift);
    Matcher<Node*> quotient_matcher =
        IsInt64MulHigh(dividend_matcher, IsInt64Constant(multiplier));
    if (divisor > 0 && multiplier < 0) {
      quotient_matcher = IsInt64Add(quotient_matcher, dividend_matcher);
    } else if (divisor < 0 && multiplier > 0) {
      quotient_matcher = IsInt64Sub(quotient_matcher, dividend_matcher);
    }
    if (shift) {
      quotient_matcher = IsWord64Sar(quotient_matcher, IsInt64Constant(shift));
    }
    return IsInt64Add(quotient_matcher,
                      IsWord64Shr(dividend_matcher, IsInt64Constant(63)));
  }

  MachineOperatorBuilder* machine() { return &data_->machine_; }

 private:
  struct Data {
    Data(Isolate* isolate, Zone* zone, Graph* graph, TickCounter* tick_counter,
         JSHeapBroker* broker)
        : machine_(zone, MachineType::PointerRepresentation(),
                   MachineOperatorBuilder::kAllOptionalOps),
          common_(zone),
          javascript_(zone),
          jsgraph_(isolate, graph, &common_, &javascript_, nullptr, &machine_),
          graph_reducer_(zone, graph, tick_counter, broker, jsgraph_.Dead()) {}
    MachineOperatorBuilder machine_;
    CommonOperatorBuilder common_;
    JSOperatorBuilder javascript_;
    JSGraph jsgraph_;
    GraphReducer graph_reducer_;
  };
  std::unique_ptr<Data> data_;
};


template <typename T>
class MachineOperatorReducerTestWithParam
    : public MachineOperatorReducerTest,
      public ::testing::WithParamInterface<T> {
 public:
  explicit MachineOperatorReducerTestWithParam(int num_parameters = 2)
      : MachineOperatorReducerTest(num_parameters) {}
  ~MachineOperatorReducerTestWithParam() override = default;
};


namespace {

const float kFloat32Values[] = {
    -std::numeric_limits<float>::infinity(), -2.70497e+38f, -1.4698e+37f,
    -1.22813e+35f,                           -1.20555e+35f, -1.34584e+34f,
    -1.0079e+32f,                            -6.49364e+26f, -3.06077e+25f,
    -1.46821e+25f,                           -1.17658e+23f, -1.9617e+22f,
    -2.7357e+20f,                            -1.48708e+13f, -1.89633e+12f,
    -4.66622e+11f,                           -2.22581e+11f, -1.45381e+10f,
    -1.3956e+09f,                            -1.32951e+09f, -1.30721e+09f,
    -1.19756e+09f,                           -9.26822e+08f, -6.35647e+08f,
    -4.00037e+08f,                           -1.81227e+08f, -5.09256e+07f,
    -964300.0f,                              -192446.0f,    -28455.0f,
    -27194.0f,                               -26401.0f,     -20575.0f,
    -17069.0f,                               -9167.0f,      -960.178f,
    -113.0f,                                 -62.0f,        -15.0f,
    -7.0f,                                   -0.0256635f,   -4.60374e-07f,
    -3.63759e-10f,                           -4.30175e-14f, -5.27385e-15f,
    -1.48084e-15f,                           -1.05755e-19f, -3.2995e-21f,
    -1.67354e-23f,                           -1.11885e-23f, -1.78506e-30f,
    -5.07594e-31f,                           -3.65799e-31f, -1.43718e-34f,
    -1.27126e-38f,                           -0.0f,         0.0f,
    1.17549e-38f,                            1.56657e-37f,  4.08512e-29f,
    3.31357e-28f,                            6.25073e-22f,  4.1723e-13f,
    1.44343e-09f,                            5.27004e-08f,  9.48298e-08f,
    5.57888e-07f,                            4.89988e-05f,  0.244326f,
    12.4895f,                                19.0f,         47.0f,
    106.0f,                                  538.324f,      564.536f,
    819.124f,                                7048.0f,       12611.0f,
    19878.0f,                                20309.0f,      797056.0f,
    1.77219e+09f,                            1.51116e+11f,  4.18193e+13f,
    3.59167e+16f,                            3.38211e+19f,  2.67488e+20f,
    1.78831e+21f,                            9.20914e+21f,  8.35654e+23f,
    1.4495e+24f,                             5.94015e+25f,  4.43608e+30f,
    2.44502e+33f,                            2.61152e+33f,  1.38178e+37f,
    1.71306e+37f,                            3.31899e+38f,  3.40282e+38f,
    std::numeric_limits<float>::infinity()};


const double kFloat64Values[] = {
    -V8_INFINITY,  -4.23878e+275, -5.82632e+265, -6.60355e+220, -6.26172e+212,
    -2.56222e+211, -4.82408e+201, -1.84106e+157, -1.63662e+127, -1.55772e+100,
    -1.67813e+72,  -2.3382e+55,   -3.179e+30,    -1.441e+09,    -1.0647e+09,
    -7.99361e+08,  -5.77375e+08,  -2.20984e+08,  -32757,        -13171,
    -9970,         -3984,         -107,          -105,          -92,
    -77,           -61,           -0.000208163,  -1.86685e-06,  -1.17296e-10,
    -9.26358e-11,  -5.08004e-60,  -1.74753e-65,  -1.06561e-71,  -5.67879e-79,
    -5.78459e-130, -2.90989e-171, -7.15489e-243, -3.76242e-252, -1.05639e-263,
    -4.40497e-267, -2.19666e-273, -4.9998e-276,  -5.59821e-278, -2.03855e-282,
    -5.99335e-283, -7.17554e-284, -3.11744e-309, -0.0,          0.0,
    2.22507e-308,  1.30127e-270,  7.62898e-260,  4.00313e-249,  3.16829e-233,
    1.85244e-228,  2.03544e-129,  1.35126e-110,  1.01182e-106,  5.26333e-94,
    1.35292e-90,   2.85394e-83,   1.78323e-77,   5.4967e-57,    1.03207e-25,
    4.57401e-25,   1.58738e-05,   2,             125,           2310,
    9636,          14802,         17168,         28945,         29305,
    4.81336e+07,   1.41207e+08,   4.65962e+08,   1.40499e+09,   2.12648e+09,
    8.80006e+30,   1.4446e+45,    1.12164e+54,   2.48188e+89,   6.71121e+102,
    3.074e+112,    4.9699e+152,   5.58383e+166,  4.30654e+172,  7.08824e+185,
    9.6586e+214,   2.028e+223,    6.63277e+243,  1.56192e+261,  1.23202e+269,
    5.72883e+289,  8.5798e+290,   1.40256e+294,  1.79769e+308,  V8_INFINITY};


const int32_t kInt32Values[] = {
    std::numeric_limits<int32_t>::min(), -1914954528, -1698749618,
    -1578693386,                         -1577976073, -1573998034,
    -1529085059,                         -1499540537, -1299205097,
    -1090814845,                         -938186388,  -806828902,
    -750927650,                          -520676892,  -513661538,
    -453036354,                          -433622833,  -282638793,
    -28375,                              -27788,      -22770,
    -18806,                              -14173,      -11956,
    -11200,                              -10212,      -8160,
    -3751,                               -2758,       -1522,
    -121,                                -120,        -118,
    -117,                                -106,        -84,
    -80,                                 -74,         -59,
    -52,                                 -48,         -39,
    -35,                                 -17,         -11,
    -10,                                 -9,          -7,
    -5,                                  0,           9,
    12,                                  17,          23,
    29,                                  31,          33,
    35,                                  40,          47,
    55,                                  56,          62,
    64,                                  67,          68,
    69,                                  74,          79,
    84,                                  89,          90,
    97,                                  104,         118,
    124,                                 126,         127,
    7278,                                17787,       24136,
    24202,                               25570,       26680,
    30242,                               32399,       420886487,
    642166225,                           821912648,   822577803,
    851385718,                           1212241078,  1411419304,
    1589626102,                          1596437184,  1876245816,
    1954730266,                          2008792749,  2045320228,
    std::numeric_limits<int32_t>::max()};

const int64_t kInt64Values[] = {std::numeric_limits<int64_t>::min(),
                                int64_t{-8974392461363618006},
                                int64_t{-8874367046689588135},
                                int64_t{-8269197512118230839},
                                int64_t{-8146091527100606733},
                                int64_t{-7550917981466150848},
                                int64_t{-7216590251577894337},
                                int64_t{-6464086891160048440},
                                int64_t{-6365616494908257190},
                                int64_t{-6305630541365849726},
                                int64_t{-5982222642272245453},
                                int64_t{-5510103099058504169},
                                int64_t{-5496838675802432701},
                                int64_t{-4047626578868642657},
                                int64_t{-4033755046900164544},
                                int64_t{-3554299241457877041},
                                int64_t{-2482258764588614470},
                                int64_t{-1688515425526875335},
                                int64_t{-924784137176548532},
                                int64_t{-725316567157391307},
                                int64_t{-439022654781092241},
                                int64_t{-105545757668917080},
                                int64_t{-2088319373},
                                int64_t{-2073699916},
                                int64_t{-1844949911},
                                int64_t{-1831090548},
                                int64_t{-1756711933},
                                int64_t{-1559409497},
                                int64_t{-1281179700},
                                int64_t{-1211513985},
                                int64_t{-1182371520},
                                int64_t{-785934753},
                                int64_t{-767480697},
                                int64_t{-705745662},
                                int64_t{-514362436},
                                int64_t{-459916580},
                                int64_t{-312328082},
                                int64_t{-302949707},
                                int64_t{-285499304},
                                int64_t{-125701262},
                                int64_t{-95139843},
                                int64_t{-32768},
                                int64_t{-27542},
                                int64_t{-23600},
                                int64_t{-18582},
                                int64_t{-17770},
                                int64_t{-9086},
                                int64_t{-9010},
                                int64_t{-8244},
                                int64_t{-2890},
                                int64_t{-103},
                                int64_t{-34},
                                int64_t{-27},
                                int64_t{-25},
                                int64_t{-9},
                                int64_t{-7},
                                int64_t{0},
                                int64_t{2},
                                int64_t{38},
                                int64_t{58},
                                int64_t{65},
                                int64_t{93},
                                int64_t{111},
                                int64_t{1003},
                                int64_t{1267},
                                int64_t{12797},
                                int64_t{23122},
                                int64_t{28200},
                                int64_t{30888},
                                int64_t{42648848},
                                int64_t{116836693},
                                int64_t{263003643},
                                int64_t{571039860},
                                int64_t{1079398689},
                                int64_t{1145196402},
                                int64_t{1184846321},
                                int64_t{1758281648},
                                int64_t{1859991374},
                                int64_t{1960251588},
                                int64_t{2042443199},
                                int64_t{296220586027987448},
                                int64_t{1015494173071134726},
                                int64_t{1151237951914455318},
                                int64_t{1331941174616854174},
                                int64_t{2022020418667972654},
                                int64_t{2450251424374977035},
                                int64_t{3668393562685561486},
                                int64_t{4858229301215502171},
                                int64_t{4919426235170669383},
                                int64_t{5034286595330341762},
                                int64_t{5055797915536941182},
                                int64_t{6072389716149252074},
                                int64_t{6185309910199801210},
                                int64_t{6297328311011094138},
                                int64_t{6932372858072165827},
                                int64_t{8483640924987737210},
                                int64_t{8663764179455849203},
                                int64_t{8877197042645298254},
                                int64_t{8901543506779157333},
                                std::numeric_limits<int64_t>::max()};

const uint32_t kUint32Values[] = {
    0x00000000, 0x00000001, 0xFFFFFFFF, 0x1B09788B, 0x04C5FCE8, 0xCC0DE5BF,
    0x273A798E, 0x187937A3, 0xECE3AF83, 0x5495A16B, 0x0B668ECC, 0x11223344,
    0x0000009E, 0x00000043, 0x0000AF73, 0x0000116B, 0x00658ECC, 0x002B3B4C,
    0x88776655, 0x70000000, 0x07200000, 0x7FFFFFFF, 0x56123761, 0x7FFFFF00,
    0x761C4761, 0x80000000, 0x88888888, 0xA0000000, 0xDDDDDDDD, 0xE0000000,
    0xEEEEEEEE, 0xFFFFFFFD, 0xF0000000, 0x007FFFFF, 0x003FFFFF, 0x001FFFFF,
    0x000FFFFF, 0x0007FFFF, 0x0003FFFF, 0x0001FFFF, 0x0000FFFF, 0x00007FFF,
    0x00003FFF, 0x00001FFF, 0x00000FFF, 0x000007FF, 0x000003FF, 0x000001FF};

const uint64_t kUint64Values[] = {
    0x0000000000000000, 0x0000000000000001, 0xFFFFFFFFFFFFFFFF,
    0x1B09788B1B09788B, 0x0000000004C5FCE8, 0xCC0DE5BFCC0DE5BF,
    0x273A798E273A798E, 0x187937A3187937A3, 0xECE3AF83ECE3AF83,
    0x5495A16B5495A16B, 0x000000000B668ECC, 0x1122334455667788,
    0x000000000000009E, 0x000000000000AF73, 0x000000000000116B,
    0x0000000000658ECC, 0x00000000002B3B4C, 0x8877665588776655,
    0x0720000000000000, 0x7FFFFFFFFFFFFFFF, 0x5612376156123761,
    0x7FFFFFFFFFFF0000, 0x761C4761761C4761, 0x8000000000000000,
    0xA000000000000000, 0xDDDDDDDDDDDDDDDD, 0xEEEEEEEEEEEEEEEE,
    0xFFFFFFFFFFFFFFFD, 0xF000000000000000, 0x007FFFFFFFFFFFFF,
    0x001FFFFFFFFFFFFF, 0x000FFFFFFFFFFFFF, 0x00007FFFFFFFFFFF,
    0x00001FFFFFFFFFFF, 0x00000FFFFFFFFFFF, 0x000007FFFFFFFFFF,
    0x000001FFFFFFFFFF, 0x00000000007FFFFF, 0x00000000001FFFFF,
    0x00000000000FFFFF, 0x00000000000007FF, 0x00000000000001FF};

struct ComparisonBinaryOperator {
  const Operator* (MachineOperatorBuilder::*constructor)();
  const char* constructor_name;
};


std::ostream& operator<<(std::ostream& os,
                         ComparisonBinaryOperator const& cbop) {
  return os << cbop.constructor_name;
}


const ComparisonBinaryOperator kComparisonBinaryOperators[] = {
#define OPCODE(Opcode)                         \
  { &MachineOperatorBuilder::Opcode, #Opcode } \
  ,
    MACHINE_COMPARE_BINOP_LIST(OPCODE)
#undef OPCODE
};

// Avoid undefined behavior on signed integer overflow.
int32_t Shl(int32_t x, int32_t y) { return static_cast<uint32_t>(x) << y; }
int64_t Shl(int64_t x, int64_t y) { return static_cast<uint64_t>(x) << y; }

}  // namespace


// -----------------------------------------------------------------------------
// ChangeFloat64ToFloat32


TEST_F(MachineOperatorReducerTest, ChangeFloat64ToFloat32WithConstant) {
  TRACED_FOREACH(float, x, kFloat32Values) {
    Reduction reduction = Reduce(graph()->NewNode(
        machine()->ChangeFloat32ToFloat64(), Float32Constant(x)));
    ASSERT_TRUE(reduction.Changed());
    EXPECT_THAT(reduction.replacement(), IsFloat64Constant(BitEq<double>(x)));
  }
}


// -----------------------------------------------------------------------------
// ChangeFloat64ToInt32


TEST_F(MachineOperatorReducerTest,
       ChangeFloat64ToInt32WithChangeInt32ToFloat64) {
  Node* value = Parameter(0);
  Reduction reduction = Reduce(graph()->NewNode(
      machine()->ChangeFloat64ToInt32(),
      graph()->NewNode(machine()->ChangeInt32ToFloat64(), value)));
  ASSERT_TRUE(reduction.Changed());
  EXPECT_EQ(value, reduction.replacement());
}

TEST_F(MachineOperatorReducerTest, ChangeFloat64ToInt32WithConstant) {
  TRACED_FOREACH(int32_t, x, kInt32Values) {
    Reduction reduction = Reduce(graph()->NewNode(
        machine()->ChangeFloat64ToInt32(), Float64Constant(FastI2D(x))));
    ASSERT_TRUE(reduction.Changed());
    EXPECT_THAT(reduction.replacement(), IsInt32Constant(x));
  }
}

// -----------------------------------------------------------------------------
// ChangeFloat64ToInt64

TEST_F(MachineOperatorReducerTest,
       ChangeFloat64ToInt64WithChangeInt64ToFloat64) {
  Node* value = Parameter(0);
  Reduction reduction = Reduce(graph()->NewNode(
      machine()->ChangeFloat64ToInt64(),
      graph()->NewNode(machine()->ChangeInt64ToFloat64(), value)));
  ASSERT_TRUE(reduction.Changed());
  EXPECT_EQ(value, reduction.replacement());
}

TEST_F(MachineOperatorReducerTest, ChangeFloat64ToInt64WithConstant) {
  TRACED_FOREACH(int32_t, x, kInt32Values) {
    Reduction reduction = Reduce(graph()->NewNode(
        machine()->ChangeFloat64ToInt64(), Float64Constant(FastI2D(x))));
    ASSERT_TRUE(reduction.Changed());
    EXPECT_THAT(reduction.replacement(), IsInt64Constant(x));
  }
}

// -----------------------------------------------------------------------------
// ChangeFloat64ToUint32


TEST_F(MachineOperatorReducerTest,
       ChangeFloat64ToUint32WithChangeUint32ToFloat64) {
  Node* value = Parameter(0);
  Reduction reduction = Reduce(graph()->NewNode(
      machine()->ChangeFloat64ToUint32(),
      graph()->NewNode(machine()->ChangeUint32ToFloat64(), value)));
  ASSERT_TRUE(reduction.Changed());
  EXPECT_EQ(value, reduction.replacement());
}


TEST_F(MachineOperatorReducerTest, ChangeFloat64ToUint32WithConstant) {
  TRACED_FOREACH(uint32_t, x, kUint32Values) {
    Reduction reduction = Reduce(graph()->NewNode(
        machine()->ChangeFloat64ToUint32(), Float64Constant(FastUI2D(x))));
    ASSERT_TRUE(reduction.Changed());
    EXPECT_THAT(reduction.replacement(),
                IsInt32Constant(base::bit_cast<int32_t>(x)));
  }
}


// -----------------------------------------------------------------------------
// ChangeInt32ToFloat64


TEST_F(MachineOperatorReducerTest, ChangeInt32ToFloat64WithConstant) {
  TRACED_FOREACH(int32_t, x, kInt32Values) {
    Reduction reduction = Reduce(
        graph()->NewNode(machine()->ChangeInt32ToFloat64(), Int32Constant(x)));
    ASSERT_TRUE(reduction.Changed());
    EXPECT_THAT(reduction.replacement(), IsFloat64Constant(BitEq(FastI2D(x))));
  }
}


// -----------------------------------------------------------------------------
// ChangeInt32ToInt64


TEST_F(MachineOperatorReducerTest, ChangeInt32ToInt64WithConstant) {
  TRACED_FOREACH(int32_t, x, kInt32Values) {
    Reduction reduction = Reduce(
        graph()->NewNode(machine()->ChangeInt32ToInt64(), Int32Constant(x)));
    ASSERT_TRUE(reduction.Changed());
    EXPECT_THAT(reduction.replacement(), IsInt64Constant(x));
  }
}

// -----------------------------------------------------------------------------
// ChangeInt64ToFloat64

TEST_F(MachineOperatorReducerTest,
       ChangeInt64ToFloat64WithChangeFloat64ToInt64) {
  Node* value = Parameter(0);
  Reduction reduction = Reduce(graph()->NewNode(
      machine()->ChangeInt64ToFloat64(),
      graph()->NewNode(machine()->ChangeFloat64ToInt64(), value)));
  ASSERT_TRUE(reduction.Changed());
  EXPECT_EQ(value, reduction.replacement());
}

TEST_F(MachineOperatorReducerTest, ChangeInt64ToFloat64WithConstant) {
  TRACED_FOREACH(int32_t, x, kInt32Values) {
    Reduction reduction = Reduce(
        graph()->NewNode(machine()->ChangeInt64ToFloat64(), Int64Constant(x)));
    ASSERT_TRUE(reduction.Changed());
    EXPECT_THAT(reduction.replacement(), IsFloat64Constant(BitEq(FastI2D(x))));
  }
}

// -----------------------------------------------------------------------------
// ChangeUint32ToFloat64


TEST_F(MachineOperatorReducerTest, ChangeUint32ToFloat64WithConstant) {
  TRACED_FOREACH(uint32_t, x, kUint32Values) {
    Reduction reduction =
        Reduce(graph()->NewNode(machine()->ChangeUint32ToFloat64(),
                                Int32Constant(base::bit_cast<int32_t>(x))));
    ASSERT_TRUE(reduction.Changed());
    EXPECT_THAT(reduction.replacement(), IsFloat64Constant(BitEq(FastUI2D(x))));
  }
}


// -----------------------------------------------------------------------------
// ChangeUint32ToUint64


TEST_F(MachineOperatorReducerTest, ChangeUint32ToUint64WithConstant) {
  TRACED_FOREACH(uint32_t, x, kUint32Values) {
    Reduction reduction =
        Reduce(graph()->NewNode(machine()->ChangeUint32ToUint64(),
                                Int32Constant(base::bit_cast<int32_t>(x))));
    ASSERT_TRUE(reduction.Changed());
    EXPECT_THAT(
        reduction.replacement(),
        IsInt64Constant(base::bit_cast<int64_t>(static_cast<uint64_t>(x))));
  }
}


// -----------------------------------------------------------------------------
// TruncateFloat64ToFloat32


TEST_F(MachineOperatorReducerTest,
       TruncateFloat64ToFloat32WithChangeFloat32ToFloat64) {
  Node* value = Parameter(0);
  Reduction reduction = Reduce(graph()->NewNode(
      machine()->TruncateFloat64ToFloat32(),
      graph()->NewNode(machine()->ChangeFloat32ToFloat64(), value)));
  ASSERT_TRUE(reduction.Changed());
  EXPECT_EQ(value, reduction.replacement());
}


TEST_F(MachineOperatorReducerTest, TruncateFloat64ToFloat32WithConstant) {
  TRACED_FOREACH(double, x, kFloat64Values) {
    Reduction reduction = Reduce(graph()->NewNode(
        machine()->TruncateFloat64ToFloat32(), Float64Constant(x)));
    ASSERT_TRUE(reduction.Changed());
    EXPECT_THAT(reduction.replacement(),
                IsFloat32Constant(BitEq(DoubleToFloat32(x))));
  }
}


// -----------------------------------------------------------------------------
// TruncateFloat64ToWord32

TEST_F(MachineOperatorReducerTest,
       TruncateFloat64ToWord32WithChangeInt32ToFloat64) {
  Node* value = Parameter(0);
  Reduction reduction = Reduce(graph()->NewNode(
      machine()->TruncateFloat64ToWord32(),
      graph()->NewNode(machine()->ChangeInt32ToFloat64(), value)));
  ASSERT_TRUE(reduction.Changed());
  EXPECT_EQ(value, reduction.replacement());
}

TEST_F(MachineOperatorReducerTest, TruncateFloat64ToWord32WithConstant) {
  TRACED_FOREACH(double, x, kFloat64Values) {
    Reduction reduction = Reduce(graph()->NewNode(
        machine()->TruncateFloat64ToWord32(), Float64Constant(x)));
    ASSERT_TRUE(reduction.Changed());
    EXPECT_THAT(reduction.replacement(), IsInt32Constant(DoubleToInt32(x)));
  }
}


// -----------------------------------------------------------------------------
// TruncateInt64ToInt32


TEST_F(MachineOperatorReducerTest, TruncateInt64ToInt32WithChangeInt32ToInt64) {
  Node* value = Parameter(0);
  Reduction reduction = Reduce(graph()->NewNode(
      machine()->TruncateInt64ToInt32(),
      graph()->NewNode(machine()->ChangeInt32ToInt64(), value)));
  ASSERT_TRUE(reduction.Changed());
  EXPECT_EQ(value, reduction.replacement());
}


TEST_F(MachineOperatorReducerTest, TruncateInt64ToInt32WithConstant) {
  TRACED_FOREACH(int64_t, x, kInt64Values) {
    Reduction reduction = Reduce(
        graph()->NewNode(machine()->TruncateInt64ToInt32(), Int64Constant(x)));
    ASSERT_TRUE(reduction.Changed());
    EXPECT_THAT(reduction.replacement(),
                IsInt32Constant(base::bit_cast<int32_t>(
                    static_cast<uint32_t>(base::bit_cast<uint64_t>(x)))));
  }
}

TEST_F(MachineOperatorReducerTest, TruncateInt64ToInt32AfterLoadAndBitcast) {
  Node* value = Parameter(0);
  Node* inputs[4] = {value, value, graph()->start(), graph()->start()};
  LoadRepresentation load_reps[3] = {LoadRepresentation::AnyTagged(),
                                     LoadRepresentation::TaggedPointer(),
                                     LoadRepresentation::TaggedSigned()};
  for (LoadRepresentation load_rep : load_reps) {
    if (ElementSizeLog2Of(load_rep.representation()) != 2) continue;
    {
      Node* load = graph()->NewNode(machine()->Load(load_rep), 4, inputs);
      Reduction reduction = Reduce(graph()->NewNode(
          machine()->TruncateInt64ToInt32(),
          graph()->NewNode(machine()->BitcastTaggedToWordForTagAndSmiBits(),
                           load)));
      ASSERT_TRUE(reduction.Changed());
      EXPECT_EQ(load, reduction.replacement());
      EXPECT_EQ(LoadRepresentationOf(load->op()), LoadRepresentation::Int32());
    }
    {
      Node* load =
          graph()->NewNode(machine()->LoadImmutable(load_rep), 2, inputs);
      Reduction reduction = Reduce(graph()->NewNode(
          machine()->TruncateInt64ToInt32(),
          graph()->NewNode(machine()->BitcastTaggedToWordForTagAndSmiBits(),
                           load)));
      ASSERT_TRUE(reduction.Changed());
      EXPECT_EQ(load, reduction.replacement());
      EXPECT_EQ(LoadRepresentationOf(load->op()), LoadRepresentation::Int32());
    }
  }
}

// -----------------------------------------------------------------------------
// RoundFloat64ToInt32

TEST_F(MachineOperatorReducerTest,
       RoundFloat64ToInt32WithChangeInt32ToFloat64) {
  Node* value = Parameter(0);
  Reduction reduction = Reduce(graph()->NewNode(
      machine()->RoundFloat64ToInt32(),
      graph()->NewNode(machine()->ChangeInt32ToFloat64(), value)));
  ASSERT_TRUE(reduction.Changed());
  EXPECT_EQ(value, reduction.replacement());
}

TEST_F(MachineOperatorReducerTest, RoundFloat64ToInt32WithConstant) {
  TRACED_FOREACH(double, x, kFloat64Values) {
    Reduction reduction = Reduce(
        graph()->NewNode(machine()->RoundFloat64ToInt32(), Float64Constant(x)));
    ASSERT_TRUE(reduction.Changed());
    EXPECT_THAT(reduction.replacement(), IsInt32Constant(DoubleToInt32(x)));
  }
}

// -----------------------------------------------------------------------------
// Word32And

TEST_F(MachineOperatorReducerTest, Word32AndWithWord32ShlWithConstant) {
  Node* const p0 = Parameter(0);

  TRACED_FORRANGE(int32_t, l, 1, 31) {
    TRACED_FORRANGE(int32_t, k, 1, l) {
      // (x << L) & (-1 << K) => x << L
      Reduction const r1 = Reduce(graph()->NewNode(
          machine()->Word32And(),
          graph()->NewNode(machine()->Word32Shl(), p0, Int32Constant(l)),
          Int32Constant(Shl(-1, k))));
      ASSERT_TRUE(r1.Changed());
      EXPECT_THAT(r1.replacement(), IsWord32Shl(p0, IsInt32Constant(l)));

      // (-1 << K) & (x << L) => x << L
      Reduction const r2 = Reduce(graph()->NewNode(
          machine()->Word32And(), Int32Constant(Shl(-1, k)),
          graph()->NewNode(machine()->Word32Shl(), p0, Int32Constant(l))));
      ASSERT_TRUE(r2.Changed());
      EXPECT_THAT(r2.replacement(), IsWord32Shl(p0, IsInt32Constant(l)));
    }
  }
}


TEST_F(MachineOperatorReducerTest, Word32AndWithWord32AndWithConstant) {
  TRACED_FOREACH(int32_t, k, kInt32Values) {
    Node* const p0 = Parameter(0);
    TRACED_FOREACH(int32_t, l, kInt32Values) {
      if (k == 0 || k == -1 || l == 0 || l == -1) continue;

      // (x & K) & L => x & (K & L)
      Reduction const r1 = Reduce(graph()->NewNode(
          machine()->Word32And(),
          graph()->NewNode(machine()->Word32And(), p0, Int32Constant(k)),
          Int32Constant(l)));
      ASSERT_TRUE(r1.Changed());
      EXPECT_THAT(r1.replacement(),
                  (k & l) ? IsWord32And(p0, IsInt32Constant(k & l))
                          : IsInt32Constant(0));

      // (K & x) & L => x & (K & L)
      Reduction const r2 = Reduce(graph()->NewNode(
          machine()->Word32And(),
          graph()->NewNode(machine()->Word32And(), Int32Constant(k), p0),
          Int32Constant(l)));
      ASSERT_TRUE(r2.Changed());
      EXPECT_THAT(r2.replacement(),
                  (k & l) ? IsWord32And(p0, IsInt32Constant(k & l))
                          : IsInt32Constant(0));
    }
    // This test uses too much memory if we don't periodically reset.
    Reset();
  }
}


TEST_F(MachineOperatorReducerTest, Word32AndWithInt32AddAndConstant) {
  Node* const p0 = Parameter(0);
  Node* const p1 = Parameter(1);

  TRACED_FORRANGE(int32_t, l, 1, 31) {
    TRACED_FOREACH(int32_t, k, kInt32Values) {
      if (Shl(k, l) == 0) continue;
      // (x + (K << L)) & (-1 << L) => (x & (-1 << L)) + (K << L)
      Reduction const r = Reduce(graph()->NewNode(
          machine()->Word32And(),
          graph()->NewNode(machine()->Int32Add(), p0, Int32Constant(Shl(k, l))),
          Int32Constant(Shl(-1, l))));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(),
                  IsInt32Add(IsWord32And(p0, IsInt32Constant(Shl(-1, l))),
                             IsInt32Constant(Shl(k, l))));
    }

    Node* s1 = graph()->NewNode(machine()->Word32Shl(), p1, Int32Constant(l));

    // (y << L + x) & (-1 << L) => (x & (-1 << L)) + y << L
    Reduction const r1 = Reduce(graph()->NewNode(
        machine()->Word32And(), graph()->NewNode(machine()->Int32Add(), s1, p0),
        Int32Constant(Shl(-1, l))));
    ASSERT_TRUE(r1.Changed());
    EXPECT_THAT(r1.replacement(),
                IsInt32Add(IsWord32And(p0, IsInt32Constant(Shl(-1, l))), s1));

    // (x + y << L) & (-1 << L) => (x & (-1 << L)) + y << L
    Reduction const r2 = Reduce(graph()->NewNode(
        machine()->Word32And(), graph()->NewNode(machine()->Int32Add(), p0, s1),
        Int32Constant(Shl(-1, l))));
    ASSERT_TRUE(r2.Changed());
    EXPECT_THAT(r2.replacement(),
                IsInt32Add(IsWord32And(p0, IsInt32Constant(Shl(-1, l))), s1));
  }
}


TEST_F(MachineOperatorReducerTest, Word32AndWithInt32MulAndConstant) {
  Node* const p0 = Parameter(0);

  TRACED_FORRANGE(int32_t, l, 1, 31) {
    TRACED_FOREACH(int32_t, k, kInt32Values) {
      if (Shl(k, l) == 0) continue;

      // (x * (K << L)) & (-1 << L) => x * (K << L)
      Reduction const r1 = Reduce(graph()->NewNode(
          machine()->Word32And(),
          graph()->NewNode(machine()->Int32Mul(), p0, Int32Constant(Shl(k, l))),
          Int32Constant(Shl(-1, l))));
      ASSERT_TRUE(r1.Changed());
      EXPECT_THAT(r1.replacement(), IsInt32Mul(p0, IsInt32Constant(Shl(k, l))));

      // ((K << L) * x) & (-1 << L) => x * (K << L)
      Reduction const r2 = Reduce(graph()->NewNode(
          machine()->Word32And(),
          graph()->NewNode(machine()->Int32Mul(), Int32Constant(Shl(k, l)), p0),
          Int32Constant(Shl(-1, l))));
      ASSERT_TRUE(r2.Changed());
      EXPECT_THAT(r2.replacement(), IsInt32Mul(p0, IsInt32Constant(Shl(k, l))));
    }
  }
}


TEST_F(MachineOperatorReducerTest,
       Word32AndWithInt32AddAndInt32MulAndConstant) {
  Node* const p0 = Parameter(0);
  Node* const p1 = Parameter(1);

  TRACED_FORRANGE(int32_t, l, 1, 31) {
    TRACED_FOREACH(int32_t, k, kInt32Values) {
      if (Shl(k, l) == 0) continue;
      // (y * (K << L) + x) & (-1 << L) => (x & (-1 << L)) + y * (K << L)
      Reduction const r1 = Reduce(graph()->NewNode(
          machine()->Word32And(),
          graph()->NewNode(machine()->Int32Add(),
                           graph()->NewNode(machine()->Int32Mul(), p1,
                                            Int32Constant(Shl(k, l))),
                           p0),
          Int32Constant(Shl(-1, l))));
      ASSERT_TRUE(r1.Changed());
      EXPECT_THAT(r1.replacement(),
                  IsInt32Add(IsWord32And(p0, IsInt32Constant(Shl(-1, l))),
                             IsInt32Mul(p1, IsInt32Constant(Shl(k, l)))));

      // (x + y * (K << L)) & (-1 << L) => (x & (-1 << L)) + y * (K << L)
      Reduction const r2 = Reduce(graph()->NewNode(
          machine()->Word32And(),
          graph()->NewNode(machine()->Int32Add(), p0,
                           graph()->NewNode(machine()->Int32Mul(), p1,
                                            Int32Constant(Shl(k, l)))),
          Int32Constant(Shl(-1, l))));
      ASSERT_TRUE(r2.Changed());
      EXPECT_THAT(r2.replacement(),
                  IsInt32Add(IsWord32And(p0, IsInt32Constant(Shl(-1, l))),
                             IsInt32Mul(p1, IsInt32Constant(Shl(k, l)))));
    }
  }
}


TEST_F(MachineOperatorReducerTest, Word32AndWithComparisonAndConstantOne) {
  Node* const p0 = Parameter(0);
  Node* const p1 = Parameter(1);
  TRACED_FOREACH(ComparisonBinaryOperator, cbop, kComparisonBinaryOperators) {
    Node* cmp = graph()->NewNode((machine()->*cbop.constructor)(), p0, p1);

    // cmp & 1 => cmp
    Reduction const r1 =
        Reduce(graph()->NewNode(machine()->Word32And(), cmp, Int32Constant(1)));
    ASSERT_TRUE(r1.Changed());
    EXPECT_EQ(cmp, r1.replacement());

    // 1 & cmp => cmp
    Reduction const r2 =
        Reduce(graph()->NewNode(machine()->Word32And(), Int32Constant(1), cmp));
    ASSERT_TRUE(r2.Changed());
    EXPECT_EQ(cmp, r2.replacement());
  }
}

TEST_F(MachineOperatorReducerTest, Word32AndWithBitFields) {
  Node* const p = Parameter(0);

  for (int i = 0; i < 2; ++i) {
    bool truncate_from_64_bit = i == 1;

    auto truncate = [&](Node* const input) {
      return truncate_from_64_bit
                 ? graph()->NewNode(machine()->TruncateInt64ToInt32(), input)
                 : input;
    };

    // Simulate getting some bitfields from a Torque bitfield struct and
    // checking them all, like `x.a == 5 & x.b & !x.c & x.d == 2`. This is
    // looking for the pattern: xxxxxxxxxxxxxxxxxxxx10xxx0x1x101. The inputs are
    // in an already-reduced state as would be created by
    // ReduceWord32EqualForConstantRhs, so the only shift operation remaining is
    // the one for selecting a single true bit.
    Node* three_bits =
        graph()->NewNode(machine()->Word32Equal(), Int32Constant(5),
                         graph()->NewNode(machine()->Word32And(),
                                          Int32Constant(7), truncate(p)));
    Node* single_bit_true =
        truncate_from_64_bit
            ? truncate(graph()->NewNode(machine()->Word64And(),
                                        Int64Constant(1),
                                        graph()->NewNode(machine()->Word64Shr(),
                                                         p, Int64Constant(4))))
            : graph()->NewNode(machine()->Word32And(), Int32Constant(1),
                               graph()->NewNode(machine()->Word32Shr(), p,
                                                Int32Constant(4)));
    Node* single_bit_false =
        graph()->NewNode(machine()->Word32Equal(), Int32Constant(0),
                         graph()->NewNode(machine()->Word32And(),
                                          Int32Constant(1 << 6), truncate(p)));
    Node* two_bits =
        graph()->NewNode(machine()->Word32Equal(), Int32Constant(2 << 10),
                         graph()->NewNode(machine()->Word32And(),
                                          Int32Constant(3 << 10), truncate(p)));

    Reduction r1 = Reduce(
        graph()->NewNode(machine()->Word32And(), three_bits, single_bit_true));
    ASSERT_TRUE(r1.Changed());
    EXPECT_THAT(
        r1.replacement(),
        IsWord32Equal(
            IsWord32And(truncate_from_64_bit ? IsTruncateInt64ToInt32(p) : p,
                        IsInt32Constant(7 | (1 << 4))),
            IsInt32Constant(5 | (1 << 4))));

    Reduction r2 = Reduce(
        graph()->NewNode(machine()->Word32And(), single_bit_false, two_bits));
    ASSERT_TRUE(r2.Changed());
    EXPECT_THAT(
        r2.replacement(),
        IsWord32Equal(
            IsWord32And(truncate_from_64_bit ? IsTruncateInt64ToInt32(p) : p,
                        IsInt32Constant((1 << 6) | (3 << 10))),
            IsInt32Constant(2 << 10)));

    Reduction const r3 = Reduce(graph()->NewNode(
        machine()->Word32And(), r1.replacement(), r2.replacement()));
    ASSERT_TRUE(r3.Changed());
    EXPECT_THAT(
        r3.replacement(),
        IsWord32Equal(
            IsWord32And(truncate_from_64_bit ? IsTruncateInt64ToInt32(p) : p,
                        IsInt32Constant(7 | (1 << 4) | (1 << 6) | (3 << 10))),
            IsInt32Constant(5 | (1 << 4) | (2 << 10))));
  }
}

TEST_F(MachineOperatorReducerTest, Word32AndWithIncorrectBitField) {
  Reduction const r = Reduce(graph()->NewNode(
      machine()->Word32And(), Parameter(0),
      graph()->NewNode(machine()->Word32Equal(),
                       graph()->NewNode(machine()->Word32And(), Parameter(0),
                                        Int32Constant(4)),
                       Parameter(0))));
  ASSERT_FALSE(r.Changed());
}

// -----------------------------------------------------------------------------
// Word32Or

TEST_F(MachineOperatorReducerTest, Word32OrWithWord32And) {
  Node* const p0 = Parameter(0);
  TRACED_FOREACH(int32_t, m, kUint32Values) {
    TRACED_FOREACH(int32_t, rhs, kUint32Values) {
      // To get better coverage of interesting cases, run this test twice:
      // once with the mask from kUint32Values, and once with its inverse.
      for (int32_t mask : {m, ~m}) {
        Reduction const r = Reduce(graph()->NewNode(
            machine()->Word32Or(),
            graph()->NewNode(machine()->Word32And(), p0, Int32Constant(mask)),
            Int32Constant(rhs)));
        switch (rhs) {
          case 0:  // x | 0 => x
            ASSERT_TRUE(r.Changed());
            EXPECT_THAT(r.replacement(),
                        IsWord32And(p0, IsInt32Constant(mask)));
            break;
          case -1:  // x | -1 => -1
            ASSERT_TRUE(r.Changed());
            EXPECT_THAT(r.replacement(), IsInt32Constant(-1));
            break;
          default:  // (x & K1) | K2 => x | K2, if K1 | K2 == -1
            if ((mask | rhs) == -1) {
              ASSERT_TRUE(r.Changed());
              EXPECT_THAT(r.replacement(),
                          IsWord32Or(p0, IsInt32Constant(rhs)));
            } else {
              ASSERT_TRUE(!r.Changed());
            }
            break;
        }
      }
    }
  }
}

// -----------------------------------------------------------------------------
// Word32Xor


TEST_F(MachineOperatorReducerTest, Word32XorWithWord32XorAndMinusOne) {
  Node* const p0 = Parameter(0);

  // (x ^ -1) ^ -1 => x
  Reduction r1 = Reduce(graph()->NewNode(
      machine()->Word32Xor(),
      graph()->NewNode(machine()->Word32Xor(), p0, Int32Constant(-1)),
      Int32Constant(-1)));
  ASSERT_TRUE(r1.Changed());
  EXPECT_EQ(r1.replacement(), p0);

  // -1 ^ (x ^ -1) => x
  Reduction r2 = Reduce(graph()->NewNode(
      machine()->Word32Xor(), Int32Constant(-1),
      graph()->NewNode(machine()->Word32Xor(), p0, Int32Constant(-1))));
  ASSERT_TRUE(r2.Changed());
  EXPECT_EQ(r2.replacement(), p0);

  // (-1 ^ x) ^ -1 => x
  Reduction r3 = Reduce(graph()->NewNode(
      machine()->Word32Xor(),
      graph()->NewNode(machine()->Word32Xor(), Int32Constant(-1), p0),
      Int32Constant(-1)));
  ASSERT_TRUE(r3.Changed());
  EXPECT_EQ(r3.replacement(), p0);

  // -1 ^ (-1 ^ x) => x
  Reduction r4 = Reduce(graph()->NewNode(
      machine()->Word32Xor(), Int32Constant(-1),
      graph()->NewNode(machine()->Word32Xor(), Int32Constant(-1), p0)));
  ASSERT_TRUE(r4.Changed());
  EXPECT_EQ(r4.replacement(), p0);
}


// -----------------------------------------------------------------------------
// Word32Ror


TEST_F(MachineOperatorReducerTest, ReduceToWord32RorWithParameters) {
  Node* value = Parameter(0);
  Node* shift = Parameter(1);
  Node* sub = graph()->NewNode(machine()->Int32Sub(), Int32Constant(32), shift);

  // Testing rotate left.
  Node* shl_l = graph()->NewNode(machine()->Word32Shl(), value, shift);
  Node* shr_l = graph()->NewNode(machine()->Word32Shr(), value, sub);

  // (x << y) | (x >>> (32 - y)) => x ror (32 - y)
  Node* node1 = graph()->NewNode(machine()->Word32Or(), shl_l, shr_l);
  Reduction reduction1 = Reduce(node1);
  EXPECT_TRUE(reduction1.Changed());
  EXPECT_EQ(reduction1.replacement(), node1);
  EXPECT_THAT(reduction1.replacement(), IsWord32Ror(value, sub));

  // (x >>> (32 - y)) | (x << y) => x ror (32 - y)
  Node* node2 = graph()->NewNode(machine()->Word32Or(), shr_l, shl_l);
  Reduction reduction2 = Reduce(node2);
  EXPECT_TRUE(reduction2.Changed());
  EXPECT_EQ(reduction2.replacement(), node2);
  EXPECT_THAT(reduction2.replacement(), IsWord32Ror(value, sub));

  // (x << y) ^ (x >>> (32 - y)) => x ror (32 - y)
  Node* node3 = graph()->NewNode(machine()->Word32Xor(), shl_l, shr_l);
  Reduction reduction3 = Reduce(node3);
  EXPECT_FALSE(reduction3.Changed());

  // (x >>> (32 - y)) ^ (x << y) => x ror (32 - y)
  Node* node4 = graph()->NewNode(machine()->Word32Xor(), shr_l, shl_l);
  Reduction reduction4 = Reduce(node4);
  EXPECT_FALSE(reduction4.Changed());

  // Testing rotate right.
  Node* shl_r = graph()->NewNode(machine()->Word32Shl(), value, sub);
  Node* shr_r = graph()->NewNode(machine()->Word32Shr(), value, shift);

  // (x << (32 - y)) | (x >>> y) => x ror y
  Node* node5 = graph()->NewNode(machine()->Word32Or(), shl_r, shr_r);
  Reduction reduction5 = Reduce(node5);
  EXPECT_TRUE(reduction5.Changed());
  EXPECT_EQ(reduction5.replacement(), node5);
  EXPECT_THAT(reduction5.replacement(), IsWord32Ror(value, shift));

  // (x >>> y) | (x << (32 - y)) => x ror y
  Node* node6 = graph()->NewNode(machine()->Word32Or(), shr_r, shl_r);
  Reduction reduction6 = Reduce(node6);
  EXPECT_TRUE(reduction6.Changed());
  EXPECT_EQ(reduction6.replacement(), node6);
  EXPECT_THAT(reduction6.replacement(), IsWord32Ror(value, shift));

  // (x << (32 - y)) ^ (x >>> y) => x ror y
  Node* node7 = graph()->NewNode(machine()->Word32Xor(), shl_r, shr_r);
  Reduction reduction7 = Reduce(node7);
  EXPECT_FALSE(reduction7.Changed());

  // (x >>> y) ^ (x << (32 - y)) => x ror y
  Node* node8 = graph()->NewNode(machine()->Word32Xor(), shr_r, shl_r);
  Reduction reduction8 = Reduce(node8);
  EXPECT_FALSE(reduction8.Changed());
}

TEST_F(MachineOperatorReducerTest, ReduceToWord32RorWithConstant) {
  Node* value = Parameter(0);
  TRACED_FORRANGE(int32_t, k, 0, 31) {
    Node* shl =
        graph()->NewNode(machine()->Word32Shl(), value, Int32Constant(k));
    Node* shr =
        graph()->NewNode(machine()->Word32Shr(), value, Int32Constant(32 - k));

    // (x << K) | (x >>> ((32 - K) - y)) => x ror (32 - K)
    Node* node1 = graph()->NewNode(machine()->Word32Or(), shl, shr);
    Reduction reduction1 = Reduce(node1);
    EXPECT_TRUE(reduction1.Changed());
    EXPECT_EQ(reduction1.replacement(), node1);
    EXPECT_THAT(reduction1.replacement(),
                IsWord32Ror(value, IsInt32Constant(32 - k)));

    // (x >>> (32 - K)) | (x << K) => x ror (32 - K)
    Node* node2 = graph()->NewNode(machine()->Word32Or(), shr, shl);
    Reduction reduction2 = Reduce(node2);
    EXPECT_TRUE(reduction2.Changed());
    EXPECT_EQ(reduction2.replacement(), node2);
    EXPECT_THAT(reduction2.replacement(),
                IsWord32Ror(value, IsInt32Constant(32 - k)));
  }
}


TEST_F(MachineOperatorReducerTest, Word32RorWithZeroShift) {
  Node* value = Parameter(0);
  Node* node =
      graph()->NewNode(machine()->Word32Ror(), value, Int32Constant(0));
  Reduction reduction = Reduce(node);
  EXPECT_TRUE(reduction.Changed());
  EXPECT_EQ(reduction.replacement(), value);
}


TEST_F(MachineOperatorReducerTest, Word32RorWithConstants) {
  TRACED_FOREACH(int32_t, x, kUint32Values) {
    TRACED_FORRANGE(int32_t, y, 0, 31) {
      Node* node = graph()->NewNode(machine()->Word32Ror(), Int32Constant(x),
                                    Int32Constant(y));
      Reduction reduction = Reduce(node);
      EXPECT_TRUE(reduction.Changed());
      EXPECT_THAT(reduction.replacement(),
                  IsInt32Constant(base::bits::RotateRight32(x, y)));
    }
  }
}


// -----------------------------------------------------------------------------
// Word32Sar


TEST_F(MachineOperatorReducerTest, Word32SarWithWord32ShlAndComparison) {
  Node* const p0 = Parameter(0);
  Node* const p1 = Parameter(1);

  TRACED_FOREACH(ComparisonBinaryOperator, cbop, kComparisonBinaryOperators) {
    Node* cmp = graph()->NewNode((machine()->*cbop.constructor)(), p0, p1);

    // cmp << 31 >> 31 => 0 - cmp
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Word32Sar(),
        graph()->NewNode(machine()->Word32Shl(), cmp, Int32Constant(31)),
        Int32Constant(31)));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt32Sub(IsInt32Constant(0), cmp));
  }
}


TEST_F(MachineOperatorReducerTest, Word32SarWithWord32ShlAndLoad) {
  Node* const p0 = Parameter(0);
  Node* const p1 = Parameter(1);
  {
    Node* const l = graph()->NewNode(machine()->Load(MachineType::Int8()), p0,
                                     p1, graph()->start(), graph()->start());
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Word32Sar(),
        graph()->NewNode(machine()->Word32Shl(), l, Int32Constant(24)),
        Int32Constant(24)));
    ASSERT_TRUE(r.Changed());
    EXPECT_EQ(l, r.replacement());
  }
  {
    Node* const l = graph()->NewNode(machine()->Load(MachineType::Int16()), p0,
                                     p1, graph()->start(), graph()->start());
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Word32Sar(),
        graph()->NewNode(machine()->Word32Shl(), l, Int32Constant(16)),
        Int32Constant(16)));
    ASSERT_TRUE(r.Changed());
    EXPECT_EQ(l, r.replacement());
  }
}


// -----------------------------------------------------------------------------
// Word32Shr

TEST_F(MachineOperatorReducerTest, Word32ShrWithWord32And) {
  Node* const p0 = Parameter(0);
  TRACED_FORRANGE(int32_t, shift, 1, 31) {
    uint32_t mask =
        base::SubWithWraparound(base::ShlWithWraparound(1, shift), 1);
    Node* node = graph()->NewNode(
        machine()->Word32Shr(),
        graph()->NewNode(machine()->Word32And(), p0, Int32Constant(mask)),
        Int32Constant(shift));
    Reduction r = Reduce(node);
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt32Constant(0));
  }
}

// -----------------------------------------------------------------------------
// Word32Shl

TEST_F(MachineOperatorReducerTest, Word32ShlWithZeroShift) {
  Node* p0 = Parameter(0);
  Node* node = graph()->NewNode(machine()->Word32Shl(), p0, Int32Constant(0));
  Reduction r = Reduce(node);
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(p0, r.replacement());
}


TEST_F(MachineOperatorReducerTest, Word32ShlWithWord32Sar) {
  Node* p0 = Parameter(0);
  TRACED_FORRANGE(int32_t, x, 1, 31) {
    Node* node = graph()->NewNode(
        machine()->Word32Shl(),
        graph()->NewNode(machine()->Word32Sar(), p0, Int32Constant(x)),
        Int32Constant(x));
    Reduction r = Reduce(node);
    ASSERT_TRUE(r.Changed());
    int32_t m = static_cast<int32_t>(~((1U << x) - 1U));
    EXPECT_THAT(r.replacement(), IsWord32And(p0, IsInt32Constant(m)));
  }
}


TEST_F(MachineOperatorReducerTest,
       Word32ShlWithWord32SarAndInt32AddAndConstant) {
  Node* const p0 = Parameter(0);
  TRACED_FOREACH(int32_t, k, kInt32Values) {
    TRACED_FORRANGE(int32_t, l, 1, 31) {
      if (Shl(k, l) == 0) continue;
      // (x + (K << L)) >> L << L => (x & (-1 << L)) + (K << L)
      Reduction const r = Reduce(graph()->NewNode(
          machine()->Word32Shl(),
          graph()->NewNode(machine()->Word32Sar(),
                           graph()->NewNode(machine()->Int32Add(), p0,
                                            Int32Constant(Shl(k, l))),
                           Int32Constant(l)),
          Int32Constant(l)));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(),
                  IsInt32Add(IsWord32And(p0, IsInt32Constant(Shl(-1, l))),
                             IsInt32Constant(Shl(k, l))));
    }
  }
}


TEST_F(MachineOperatorReducerTest, Word32ShlWithWord32Shr) {
  Node* p0 = Parameter(0);
  TRACED_FORRANGE(int32_t, x, 1, 31) {
    Node* node = graph()->NewNode(
        machine()->Word32Shl(),
        graph()->NewNode(machine()->Word32Shr(), p0, Int32Constant(x)),
        Int32Constant(x));
    Reduction r = Reduce(node);
    ASSERT_TRUE(r.Changed());
    int32_t m = static_cast<int32_t>(std::numeric_limits<uint32_t>::max() << x);
    EXPECT_THAT(r.replacement(), IsWord32And(p0, IsInt32Constant(m)));
  }
}

TEST_F(MachineOperatorReducerTest, Word32ShlWithWord32SarShiftOutZeros) {
  Node* p = Parameter(0);
  TRACED_FORRANGE(int32_t, x, 1, 31) {
    TRACED_FORRANGE(int32_t, y, 0, 31) {
      Node* node = graph()->NewNode(
          machine()->Word32Shl(),
          graph()->NewNode(machine()->Word32Sar(ShiftKind::kShiftOutZeros), p,
                           Int32Constant(x)),
          Int32Constant(y));
      Reduction r = Reduce(node);
      ASSERT_TRUE(r.Changed());
      if (x == y) {
        // (p >> x) << y => p
        EXPECT_THAT(r.replacement(), p);
      } else if (x < y) {
        // (p >> x) << y => p << (y - x)
        EXPECT_THAT(r.replacement(), IsWord32Shl(p, IsInt32Constant(y - x)));
      } else {
        // (p >> x) << y => p >> (x - y)
        EXPECT_THAT(r.replacement(), IsWord32Sar(p, IsInt32Constant(x - y)));
      }
    }
  }
}

// -----------------------------------------------------------------------------
// Word64Shl

TEST_F(MachineOperatorReducerTest, Word64ShlWithZeroShift) {
  Node* p0 = Parameter(0);
  Node* node = graph()->NewNode(machine()->Word64Shl(), p0, Int64Constant(0));
  Reduction r = Reduce(node);
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(p0, r.replacement());
}

TEST_F(MachineOperatorReducerTest, Word64ShlWithWord64Sar) {
  Node* p0 = Parameter(0);
  TRACED_FORRANGE(int64_t, x, 1, 63) {
    Node* node = graph()->NewNode(
        machine()->Word64Shl(),
        graph()->NewNode(machine()->Word64Sar(), p0, Int64Constant(x)),
        Int64Constant(x));
    Reduction r = Reduce(node);
    ASSERT_TRUE(r.Changed());
    int64_t m = static_cast<int64_t>(~((uint64_t{1} << x) - 1));
    EXPECT_THAT(r.replacement(), IsWord64And(p0, IsInt64Constant(m)));
  }
}

TEST_F(MachineOperatorReducerTest,
       Word64ShlWithWord64SarAndInt64AddAndConstant) {
  Node* const p0 = Parameter(0);
  TRACED_FOREACH(int64_t, k, kInt64Values) {
    TRACED_FORRANGE(int64_t, l, 1, 63) {
      if (Shl(k, l) == 0) continue;
      // (x + (K << L)) >> L << L => (x & (-1 << L)) + (K << L)
      Reduction const r = Reduce(graph()->NewNode(
          machine()->Word64Shl(),
          graph()->NewNode(machine()->Word64Sar(),
                           graph()->NewNode(machine()->Int64Add(), p0,
                                            Int64Constant(Shl(k, l))),
                           Int64Constant(l)),
          Int64Constant(l)));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(
          r.replacement(),
          IsInt64Add(IsWord64And(p0, IsInt64Constant(Shl(int64_t{-1}, l))),
                     IsInt64Constant(Shl(k, l))));
    }
  }
}

TEST_F(MachineOperatorReducerTest, Word64ShlWithWord64Shr) {
  Node* p0 = Parameter(0);
  TRACED_FORRANGE(int64_t, x, 1, 63) {
    Node* node = graph()->NewNode(
        machine()->Word64Shl(),
        graph()->NewNode(machine()->Word64Shr(), p0, Int64Constant(x)),
        Int64Constant(x));
    Reduction r = Reduce(node);
    ASSERT_TRUE(r.Changed());
    int64_t m = static_cast<int64_t>(std::numeric_limits<uint64_t>::max() << x);
    EXPECT_THAT(r.replacement(), IsWord64And(p0, IsInt64Constant(m)));
  }
}

TEST_F(MachineOperatorReducerTest, Word64ShlWithWord64SarShiftOutZeros) {
  Node* p = Parameter(0);
  TRACED_FORRANGE(int64_t, x, 1, 63) {
    TRACED_FORRANGE(int64_t, y, 0, 63) {
      Node* node = graph()->NewNode(
          machine()->Word64Shl(),
          graph()->NewNode(machine()->Word64Sar(ShiftKind::kShiftOutZeros), p,
                           Int64Constant(x)),
          Int64Constant(y));
      Reduction r = Reduce(node);
      ASSERT_TRUE(r.Changed());
      if (x == y) {
        // (p >> x) << y => p
        EXPECT_THAT(r.replacement(), p);
      } else if (x < y) {
        // (p >> x) << y => p << (y - x)
        EXPECT_THAT(r.replacement(), IsWord64Shl(p, IsInt64Constant(y - x)));
      } else {
        // (p >> x) << y => p >> (x - y)
        EXPECT_THAT(r.replacement(), IsWord64Sar(p, IsInt64Constant(x - y)));
      }
    }
  }
}

// -----------------------------------------------------------------------------
// Word32Equal

TEST_F(MachineOperatorReducerTest,
       Word32EqualWithShiftedMaskedValueAndConstant) {
  // ((x >> K1) & K2) == K3 => (x & (K2 << K1)) == (K3 << K1)
  TRACED_FOREACH(uint32_t, mask, kUint32Values) {
    TRACED_FOREACH(uint32_t, rhs, kUint32Values) {
      Node* const p0 = Parameter(0);
      TRACED_FORRANGE(uint32_t, shift_bits, 1, 31) {
        Node* node = graph()->NewNode(
            machine()->Word32Equal(),
            graph()->NewNode(machine()->Word32And(),
                             graph()->NewNode(machine()->Word32Shr(), p0,
                                              Uint32Constant(shift_bits)),
                             Uint32Constant(mask)),
            Uint32Constant(rhs));
        Reduction r = Reduce(node);
        uint32_t new_mask = mask << shift_bits;
        uint32_t new_rhs = rhs << shift_bits;
        if (new_mask >> shift_bits == mask && new_rhs >> shift_bits == rhs) {
          ASSERT_TRUE(r.Changed());
          // The left-hand side of the equality is now a Word32And operation,
          // unless the mask is zero in which case the newly-created Word32And
          // is immediately reduced away.
          Matcher<Node*> lhs = mask == 0
                                   ? IsInt32Constant(0)
                                   : IsWord32And(p0, IsInt32Constant(new_mask));
          EXPECT_THAT(r.replacement(),
                      IsWord32Equal(lhs, IsInt32Constant(new_rhs)));
        } else {
          ASSERT_FALSE(r.Changed());
        }
      }
      // This test uses too much memory if we don't periodically reset.
      Reset();
    }
  }
}

TEST_F(MachineOperatorReducerTest, Word32EqualWithAddAndConstant) {
  // (x+k1)==k2 => x==(k2-k1)
  Node* const p0 = Parameter(0);
  TRACED_FOREACH(int32_t, k1, kInt32Values) {
    TRACED_FOREACH(int32_t, k2, kInt32Values) {
      Node* node = graph()->NewNode(
          machine()->Word32Equal(),
          graph()->NewNode(machine()->Int32Add(), p0, Int32Constant(k1)),
          Int32Constant(k2));
      Reduction r = Reduce(node);
      ASSERT_TRUE(r.Changed());
    }
  }
}

// -----------------------------------------------------------------------------
// Word64Equal

TEST_F(MachineOperatorReducerTest,
       Word64EqualWithShiftedMaskedValueAndConstant) {
  // ((x >> K1) & K2) == K3 => (x & (K2 << K1)) == (K3 << K1)
  TRACED_FOREACH(uint64_t, mask, kUint64Values) {
    TRACED_FOREACH(uint64_t, rhs, kUint64Values) {
      Node* const p0 = Parameter(0);
      TRACED_FORRANGE(uint64_t, shift_bits, 1, 63) {
        Node* node = graph()->NewNode(
            machine()->Word64Equal(),
            graph()->NewNode(machine()->Word64And(),
                             graph()->NewNode(machine()->Word64Shr(), p0,
                                              Uint64Constant(shift_bits)),
                             Uint64Constant(mask)),
            Uint64Constant(rhs));
        Reduction r = Reduce(node);
        uint64_t new_mask = mask << shift_bits;
        uint64_t new_rhs = rhs << shift_bits;
        if (new_mask >> shift_bits == mask && new_rhs >> shift_bits == rhs) {
          ASSERT_TRUE(r.Changed());
          // The left-hand side of the equality is now a Word64And operation,
          // unless the mask is zero in which case the newly-created Word64And
          // is immediately reduced away.
          Matcher<Node*> lhs = mask == 0
                                   ? IsInt64Constant(0)
                                   : IsWord64And(p0, IsInt64Constant(new_mask));
          EXPECT_THAT(r.replacement(),
                      IsWord64Equal(lhs, IsInt64Constant(new_rhs)));
        } else {
          ASSERT_FALSE(r.Changed());
        }
      }
      // This test uses too much memory if we don't periodically reset.
      Reset();
    }
  }
}

TEST_F(MachineOperatorReducerTest, Word64EqualWithAddAndConstant) {
  // (x+k1)==k2 => x==(k2-k1)
  Node* const p0 = Parameter(0);
  TRACED_FOREACH(int64_t, k1, kInt64Values) {
    TRACED_FOREACH(int64_t, k2, kInt64Values) {
      Node* node = graph()->NewNode(
          machine()->Word64Equal(),
          graph()->NewNode(machine()->Int64Add(), p0, Int64Constant(k1)),
          Int64Constant(k2));
      Reduction r = Reduce(node);
      ASSERT_TRUE(r.Changed());
    }
  }
}

// -----------------------------------------------------------------------------
// Branch

TEST_F(MachineOperatorReducerTest, BranchWithShiftedMaskedValue) {
  // Branch condition (x >> K1) & K2 => x & (K2 << K1)
  Node* const p0 = Parameter(0);
  TRACED_FOREACH(uint32_t, mask, kUint32Values) {
    TRACED_FORRANGE(uint32_t, shift_bits, 1, 31) {
      Node* node = graph()->NewNode(
          common()->Branch(),
          graph()->NewNode(machine()->Word32And(),
                           graph()->NewNode(machine()->Word32Shr(), p0,
                                            Uint32Constant(shift_bits)),
                           Uint32Constant(mask)),
          graph()->start());
      Reduction r = Reduce(node);
      uint32_t new_mask = mask << shift_bits;
      if (new_mask >> shift_bits == mask) {
        ASSERT_TRUE(r.Changed());
        // The branch condition is now a Word32And operation, unless the mask is
        // zero in which case the newly-created Word32And is immediately reduced
        // away.
        Matcher<Node*> lhs = mask == 0
                                 ? IsInt32Constant(0)
                                 : IsWord32And(p0, IsInt32Constant(new_mask));
        EXPECT_THAT(r.replacement(), IsBranch(lhs, graph()->start()));
      } else {
        ASSERT_FALSE(r.Changed());
      }
    }
  }
}

// -----------------------------------------------------------------------------
// Int32Sub


TEST_F(MachineOperatorReducerTest, Int32SubWithConstant) {
  Node* const p0 = Parameter(0);
  TRACED_FOREACH(int32_t, k, kInt32Values) {
    Reduction const r =
        Reduce(graph()->NewNode(machine()->Int32Sub(), p0, Int32Constant(k)));
    ASSERT_TRUE(r.Changed());
    if (k == 0) {
      EXPECT_EQ(p0, r.replacement());
    } else {
      EXPECT_THAT(
          r.replacement(),
          IsInt32Add(p0, IsInt32Constant(base::NegateWithWraparound(k))));
    }
  }
}

// -----------------------------------------------------------------------------
// Int32Div, Int64Div

TEST_F(MachineOperatorReducerTest, Int32DivWithConstant) {
  Node* const p0 = Parameter(0);
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Int32Div(), p0, Int32Constant(0), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt32Constant(0));
  }
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Int32Div(), p0, Int32Constant(1), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_EQ(r.replacement(), p0);
  }
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Int32Div(), p0, Int32Constant(-1), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt32Sub(IsInt32Constant(0), p0));
  }
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Int32Div(), p0, Int32Constant(2), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(
        r.replacement(),
        IsWord32Sar(IsInt32Add(IsWord32Shr(p0, IsInt32Constant(31)), p0),
                    IsInt32Constant(1)));
  }
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Int32Div(), p0, Int32Constant(-2), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(
        r.replacement(),
        IsInt32Sub(
            IsInt32Constant(0),
            IsWord32Sar(IsInt32Add(IsWord32Shr(p0, IsInt32Constant(31)), p0),
                        IsInt32Constant(1))));
  }
  TRACED_FORRANGE(int32_t, shift, 2, 30) {
    Reduction const r =
        Reduce(graph()->NewNode(machine()->Int32Div(), p0,
                                Int32Constant(1 << shift), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(
        r.replacement(),
        IsWord32Sar(IsInt32Add(IsWord32Shr(IsWord32Sar(p0, IsInt32Constant(31)),
                                           IsInt32Constant(32 - shift)),
                               p0),
                    IsInt32Constant(shift)));
  }
  TRACED_FORRANGE(int32_t, shift, 2, 31) {
    Reduction const r = Reduce(graph()->NewNode(machine()->Int32Div(), p0,
                                                Int32Constant(Shl(-1, shift)),
                                                graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(
        r.replacement(),
        IsInt32Sub(
            IsInt32Constant(0),
            IsWord32Sar(
                IsInt32Add(IsWord32Shr(IsWord32Sar(p0, IsInt32Constant(31)),
                                       IsInt32Constant(32 - shift)),
                           p0),
                IsInt32Constant(shift))));
  }
  TRACED_FOREACH(int32_t, divisor, kInt32Values) {
    if (divisor < 0) {
      if (divisor == kMinInt || base::bits::IsPowerOfTwo(-divisor)) continue;
      Reduction const r = Reduce(graph()->NewNode(
          machine()->Int32Div(), p0, Int32Constant(divisor), graph()->start()));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(), IsInt32Sub(IsInt32Constant(0),
                                              IsTruncatingDiv(p0, -divisor)));
    } else if (divisor > 0) {
      if (base::bits::IsPowerOfTwo(divisor)) continue;
      Reduction const r = Reduce(graph()->NewNode(
          machine()->Int32Div(), p0, Int32Constant(divisor), graph()->start()));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(), IsTruncatingDiv(p0, divisor));
    }
  }
}

TEST_F(MachineOperatorReducerTest, Int64DivWithConstant) {
  Node* const p0 = Parameter(0);
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Int64Div(), p0, Int64Constant(0), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt64Constant(0));
  }
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Int64Div(), p0, Int64Constant(1), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_EQ(r.replacement(), p0);
  }
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Int64Div(), p0, Int64Constant(-1), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt64Sub(IsInt64Constant(0), p0));
  }
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Int64Div(), p0, Int64Constant(2), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(
        r.replacement(),
        IsWord64Sar(IsInt64Add(IsWord64Shr(p0, IsInt64Constant(63)), p0),
                    IsInt64Constant(1)));
  }
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Int64Div(), p0, Int64Constant(-2), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(
        r.replacement(),
        IsInt64Sub(
            IsInt64Constant(0),
            IsWord64Sar(IsInt64Add(IsWord64Shr(p0, IsInt64Constant(63)), p0),
                        IsInt64Constant(1))));
  }
  TRACED_FORRANGE(int64_t, shift, 2, 62) {
    Reduction const r = Reduce(
        graph()->NewNode(machine()->Int64Div(), p0,
                         Int64Constant(int64_t{1} << shift), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(
        r.replacement(),
        IsWord64Sar(IsInt64Add(IsWord64Shr(IsWord64Sar(p0, IsInt64Constant(63)),
                                           IsInt64Constant(64 - shift)),
                               p0),
                    IsInt64Constant(shift)));
  }
  TRACED_FORRANGE(int64_t, shift, 2, 63) {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Int64Div(), p0, Int64Constant(Shl(int64_t{-1}, shift)),
        graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(
        r.replacement(),
        IsInt64Sub(
            IsInt64Constant(0),
            IsWord64Sar(
                IsInt64Add(IsWord64Shr(IsWord64Sar(p0, IsInt64Constant(63)),
                                       IsInt64Constant(64 - shift)),
                           p0),
                IsInt64Constant(shift))));
  }
  TRACED_FOREACH(int64_t, divisor, kInt64Values) {
    if (divisor < 0) {
      if (divisor == std::numeric_limits<int64_t>::min() ||
          base::bits::IsPowerOfTwo(-divisor)) {
        continue;
      }
      Reduction const r = Reduce(graph()->NewNode(
          machine()->Int64Div(), p0, Int64Constant(divisor), graph()->start()));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(), IsInt64Sub(IsInt64Constant(0),
                                              IsTruncatingDiv64(p0, -divisor)));
    } else if (divisor > 0) {
      if (base::bits::IsPowerOfTwo(divisor)) continue;
      Reduction const r = Reduce(graph()->NewNode(
          machine()->Int64Div(), p0, Int64Constant(divisor), graph()->start()));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(), IsTruncatingDiv64(p0, divisor));
    }
  }
}

TEST_F(MachineOperatorReducerTest, Int32DivWithParameters) {
  Node* const p0 = Parameter(0);
  Reduction const r =
      Reduce(graph()->NewNode(machine()->Int32Div(), p0, p0, graph()->start()));
  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(
      r.replacement(),
      IsWord32Equal(IsWord32Equal(p0, IsInt32Constant(0)), IsInt32Constant(0)));
}

// -----------------------------------------------------------------------------
// Uint32Div, Uint64Div

TEST_F(MachineOperatorReducerTest, Uint32DivWithConstant) {
  Node* const p0 = Parameter(0);
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Uint32Div(), Int32Constant(0), p0, graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt32Constant(0));
  }
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Uint32Div(), p0, Int32Constant(0), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt32Constant(0));
  }
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Uint32Div(), p0, Int32Constant(1), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_EQ(r.replacement(), p0);
  }
  TRACED_FOREACH(uint32_t, dividend, kUint32Values) {
    TRACED_FOREACH(uint32_t, divisor, kUint32Values) {
      Reduction const r = Reduce(
          graph()->NewNode(machine()->Uint32Div(), Uint32Constant(dividend),
                           Uint32Constant(divisor), graph()->start()));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(),
                  IsInt32Constant(base::bit_cast<int32_t>(
                      base::bits::UnsignedDiv32(dividend, divisor))));
    }
  }
  TRACED_FORRANGE(uint32_t, shift, 1, 31) {
    Reduction const r =
        Reduce(graph()->NewNode(machine()->Uint32Div(), p0,
                                Uint32Constant(1u << shift), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(),
                IsWord32Shr(p0, IsInt32Constant(static_cast<int32_t>(shift))));
  }
}

TEST_F(MachineOperatorReducerTest, Uint64DivWithConstant) {
  Node* const p0 = Parameter(0);
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Uint64Div(), Int64Constant(0), p0, graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt64Constant(0));
  }
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Uint64Div(), p0, Int64Constant(0), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt64Constant(0));
  }
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Uint64Div(), p0, Int64Constant(1), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_EQ(r.replacement(), p0);
  }
  TRACED_FOREACH(uint64_t, dividend, kUint64Values) {
    TRACED_FOREACH(uint64_t, divisor, kUint64Values) {
      Reduction const r = Reduce(
          graph()->NewNode(machine()->Uint64Div(), Uint64Constant(dividend),
                           Uint64Constant(divisor), graph()->start()));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(),
                  IsInt64Constant(base::bit_cast<int64_t>(
                      base::bits::UnsignedDiv64(dividend, divisor))));
    }
  }
  TRACED_FORRANGE(uint64_t, shift, 1, 63) {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Uint64Div(), p0, Uint64Constant(uint64_t{1} << shift),
        graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(),
                IsWord64Shr(p0, IsInt64Constant(static_cast<int64_t>(shift))));
  }
}

TEST_F(MachineOperatorReducerTest, Uint32DivWithParameters) {
  Node* const p0 = Parameter(0);
  Reduction const r = Reduce(
      graph()->NewNode(machine()->Uint32Div(), p0, p0, graph()->start()));
  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(
      r.replacement(),
      IsWord32Equal(IsWord32Equal(p0, IsInt32Constant(0)), IsInt32Constant(0)));
}

// -----------------------------------------------------------------------------
// Int32Mod, Uint64Mod

TEST_F(MachineOperatorReducerTest, Int32ModWithConstant) {
  Node* const p0 = Parameter(0);
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Int32Mod(), Int32Constant(0), p0, graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt32Constant(0));
  }
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Int32Mod(), p0, Int32Constant(0), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt32Constant(0));
  }
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Int32Mod(), p0, Int32Constant(1), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt32Constant(0));
  }
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Int32Mod(), p0, Int32Constant(-1), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt32Constant(0));
  }
  TRACED_FOREACH(int32_t, dividend, kInt32Values) {
    TRACED_FOREACH(int32_t, divisor, kInt32Values) {
      Reduction const r = Reduce(
          graph()->NewNode(machine()->Int32Mod(), Int32Constant(dividend),
                           Int32Constant(divisor), graph()->start()));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(),
                  IsInt32Constant(base::bits::SignedMod32(dividend, divisor)));
    }
  }
  TRACED_FORRANGE(int32_t, shift, 1, 30) {
    Reduction const r =
        Reduce(graph()->NewNode(machine()->Int32Mod(), p0,
                                Int32Constant(1 << shift), graph()->start()));
    int32_t const mask = (1 << shift) - 1;
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(
        r.replacement(),
        IsPhi(
            MachineRepresentation::kWord32,
            IsInt32Sub(IsInt32Constant(0),
                       IsWord32And(IsInt32Sub(IsInt32Constant(0), p0),
                                   IsInt32Constant(mask))),
            IsWord32And(p0, IsInt32Constant(mask)),
            IsMerge(IsIfTrue(IsBranch(IsInt32LessThan(p0, IsInt32Constant(0)),
                                      graph()->start())),
                    IsIfFalse(IsBranch(IsInt32LessThan(p0, IsInt32Constant(0)),
                                       graph()->start())))));
  }
  TRACED_FORRANGE(int32_t, shift, 1, 31) {
    Reduction const r = Reduce(graph()->NewNode(machine()->Int32Mod(), p0,
                                                Int32Constant(Shl(-1, shift)),
                                                graph()->start()));
    int32_t const mask = static_cast<int32_t>((1U << shift) - 1U);
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(
        r.replacement(),
        IsPhi(
            MachineRepresentation::kWord32,
            IsInt32Sub(IsInt32Constant(0),
                       IsWord32And(IsInt32Sub(IsInt32Constant(0), p0),
                                   IsInt32Constant(mask))),
            IsWord32And(p0, IsInt32Constant(mask)),
            IsMerge(IsIfTrue(IsBranch(IsInt32LessThan(p0, IsInt32Constant(0)),
                                      graph()->start())),
                    IsIfFalse(IsBranch(IsInt32LessThan(p0, IsInt32Constant(0)),
                                       graph()->start())))));
  }
  TRACED_FOREACH(int32_t, divisor, kInt32Values) {
    if (divisor == 0 || base::bits::IsPowerOfTwo(Abs(divisor))) continue;
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Int32Mod(), p0, Int32Constant(divisor), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(),
                IsInt32Sub(p0, IsInt32Mul(IsTruncatingDiv(p0, Abs(divisor)),
                                          IsInt32Constant(Abs(divisor)))));
  }
}

TEST_F(MachineOperatorReducerTest, Int64ModWithConstant) {
  Node* const p0 = Parameter(0);
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Int64Mod(), Int64Constant(0), p0, graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt64Constant(0));
  }
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Int64Mod(), p0, Int64Constant(0), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt64Constant(0));
  }
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Int64Mod(), p0, Int64Constant(1), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt64Constant(0));
  }
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Int64Mod(), p0, Int64Constant(-1), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt64Constant(0));
  }
  TRACED_FOREACH(int64_t, dividend, kInt64Values) {
    TRACED_FOREACH(int64_t, divisor, kInt64Values) {
      Reduction const r = Reduce(
          graph()->NewNode(machine()->Int64Mod(), Int64Constant(dividend),
                           Int64Constant(divisor), graph()->start()));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(),
                  IsInt64Constant(base::bits::SignedMod64(dividend, divisor)));
    }
  }
  TRACED_FORRANGE(int64_t, shift, 1, 62) {
    Reduction const r = Reduce(
        graph()->NewNode(machine()->Int64Mod(), p0,
                         Int64Constant(int64_t{1} << shift), graph()->start()));
    int64_t const mask = (int64_t{1} << shift) - 1;
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(
        r.replacement(),
        IsPhi(
            MachineRepresentation::kWord64,
            IsInt64Sub(IsInt64Constant(0),
                       IsWord64And(IsInt64Sub(IsInt64Constant(0), p0),
                                   IsInt64Constant(mask))),
            IsWord64And(p0, IsInt64Constant(mask)),
            IsMerge(IsIfTrue(IsBranch(IsInt64LessThan(p0, IsInt64Constant(0)),
                                      graph()->start())),
                    IsIfFalse(IsBranch(IsInt64LessThan(p0, IsInt64Constant(0)),
                                       graph()->start())))));
  }
  TRACED_FORRANGE(int64_t, shift, 1, 63) {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Int64Mod(), p0, Int64Constant(Shl(int64_t{-1}, shift)),
        graph()->start()));
    int64_t const mask = static_cast<int64_t>((uint64_t{1} << shift) - 1U);
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(
        r.replacement(),
        IsPhi(
            MachineRepresentation::kWord64,
            IsInt64Sub(IsInt64Constant(0),
                       IsWord64And(IsInt64Sub(IsInt64Constant(0), p0),
                                   IsInt64Constant(mask))),
            IsWord64And(p0, IsInt64Constant(mask)),
            IsMerge(IsIfTrue(IsBranch(IsInt64LessThan(p0, IsInt64Constant(0)),
                                      graph()->start())),
                    IsIfFalse(IsBranch(IsInt64LessThan(p0, IsInt64Constant(0)),
                                       graph()->start())))));
  }
  TRACED_FOREACH(int64_t, divisor, kInt64Values) {
    if (divisor == 0 || base::bits::IsPowerOfTwo(Abs(divisor))) continue;
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Int64Mod(), p0, Int64Constant(divisor), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(),
                IsInt64Sub(p0, IsInt64Mul(IsTruncatingDiv64(p0, Abs(divisor)),
                                          IsInt64Constant(Abs(divisor)))));
  }
}

TEST_F(MachineOperatorReducerTest, Int32ModWithParameters) {
  Node* const p0 = Parameter(0);
  Reduction const r =
      Reduce(graph()->NewNode(machine()->Int32Mod(), p0, p0, graph()->start()));
  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsInt32Constant(0));
}

// -----------------------------------------------------------------------------
// Uint32Mod, Uint64Mod

TEST_F(MachineOperatorReducerTest, Uint32ModWithConstant) {
  Node* const p0 = Parameter(0);
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Uint32Mod(), p0, Int32Constant(0), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt32Constant(0));
  }
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Uint32Mod(), Int32Constant(0), p0, graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt32Constant(0));
  }
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Uint32Mod(), p0, Int32Constant(1), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt32Constant(0));
  }
  TRACED_FOREACH(uint32_t, dividend, kUint32Values) {
    TRACED_FOREACH(uint32_t, divisor, kUint32Values) {
      Reduction const r = Reduce(
          graph()->NewNode(machine()->Uint32Mod(), Uint32Constant(dividend),
                           Uint32Constant(divisor), graph()->start()));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(),
                  IsInt32Constant(base::bit_cast<int32_t>(
                      base::bits::UnsignedMod32(dividend, divisor))));
    }
  }
  TRACED_FORRANGE(uint32_t, shift, 1, 31) {
    Reduction const r =
        Reduce(graph()->NewNode(machine()->Uint32Mod(), p0,
                                Uint32Constant(1u << shift), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(),
                IsWord32And(p0, IsInt32Constant(
                                    static_cast<int32_t>((1u << shift) - 1u))));
  }
}

TEST_F(MachineOperatorReducerTest, Uint64ModWithConstant) {
  Node* const p0 = Parameter(0);
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Uint64Mod(), p0, Int64Constant(0), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt64Constant(0));
  }
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Uint64Mod(), Int64Constant(0), p0, graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt64Constant(0));
  }
  {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Uint64Mod(), p0, Int64Constant(1), graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt64Constant(0));
  }
  TRACED_FOREACH(uint64_t, dividend, kUint64Values) {
    TRACED_FOREACH(uint64_t, divisor, kUint64Values) {
      Reduction const r = Reduce(
          graph()->NewNode(machine()->Uint64Mod(), Uint64Constant(dividend),
                           Uint64Constant(divisor), graph()->start()));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(),
                  IsInt64Constant(base::bit_cast<int64_t>(
                      base::bits::UnsignedMod64(dividend, divisor))));
    }
  }
  TRACED_FORRANGE(uint64_t, shift, 1, 63) {
    Reduction const r = Reduce(graph()->NewNode(
        machine()->Uint64Mod(), p0, Uint64Constant(uint64_t{1} << shift),
        graph()->start()));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(),
                IsWord64And(p0, IsInt64Constant(static_cast<int64_t>(
                                    (uint64_t{1} << shift) - 1u))));
  }
}

TEST_F(MachineOperatorReducerTest, Uint32ModWithParameters) {
  Node* const p0 = Parameter(0);
  Reduction const r = Reduce(
      graph()->NewNode(machine()->Uint32Mod(), p0, p0, graph()->start()));
  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsInt32Constant(0));
}

// -----------------------------------------------------------------------------
// Int32Add, Int64Add

TEST_F(MachineOperatorReducerTest, Int32AddWithInt32SubWithConstantZero) {
  Node* const p0 = Parameter(0);
  Node* const p1 = Parameter(1);

  Reduction const r1 = Reduce(graph()->NewNode(
      machine()->Int32Add(),
      graph()->NewNode(machine()->Int32Sub(), Int32Constant(0), p0), p1));
  ASSERT_TRUE(r1.Changed());
  EXPECT_THAT(r1.replacement(), IsInt32Sub(p1, p0));

  Reduction const r2 = Reduce(graph()->NewNode(
      machine()->Int32Add(), p0,
      graph()->NewNode(machine()->Int32Sub(), Int32Constant(0), p1)));
  ASSERT_TRUE(r2.Changed());
  EXPECT_THAT(r2.replacement(), IsInt32Sub(p0, p1));
}

TEST_F(MachineOperatorReducerTest, Int32AddMergeConstants) {
  Node* const p0 = Parameter(0);

  Reduction const r1 = Reduce(graph()->NewNode(
      machine()->Int32Add(),
      graph()->NewNode(machine()->Int32Add(), p0, Int32Constant(1)),
      Int32Constant(2)));
  ASSERT_TRUE(r1.Changed());
  EXPECT_THAT(r1.replacement(), IsInt32Add(p0, IsInt32Constant(3)));

  Reduction const r2 = Reduce(graph()->NewNode(
      machine()->Int32Add(), Int32Constant(2),
      graph()->NewNode(machine()->Int32Add(), p0, Int32Constant(1))));
  ASSERT_TRUE(r2.Changed());
  EXPECT_THAT(r2.replacement(), IsInt32Add(p0, IsInt32Constant(3)));
}

TEST_F(MachineOperatorReducerTest, Int64AddMergeConstants) {
  Node* const p0 = Parameter(0);

  Reduction const r1 = Reduce(graph()->NewNode(
      machine()->Int64Add(),
      graph()->NewNode(machine()->Int64Add(), p0, Int64Constant(1)),
      Int64Constant(2)));
  ASSERT_TRUE(r1.Changed());
  EXPECT_THAT(r1.replacement(), IsInt64Add(p0, IsInt64Constant(3)));

  Reduction const r2 = Reduce(graph()->NewNode(
      machine()->Int64Add(), Int64Constant(2),
      graph()->NewNode(machine()->Int64Add(), p0, Int64Constant(1))));
  ASSERT_TRUE(r2.Changed());
  EXPECT_THAT(r2.replacement(), IsInt64Add(p0, IsInt64Constant(3)));
}

// -----------------------------------------------------------------------------
// Int32Mul, Int64Mul

TEST_F(MachineOperatorReducerTest, Int32MulMergeConstants) {
  Node* const p0 = Parameter(0);

  Reduction const r1 = Reduce(graph()->NewNode(
      machine()->Int32Mul(),
      graph()->NewNode(machine()->Int32Mul(), p0, Int32Constant(5)),
      Int32Constant(3)));
  ASSERT_TRUE(r1.Changed());
  EXPECT_THAT(r1.replacement(), IsInt32Mul(p0, IsInt32Constant(15)));

  Reduction const r2 = Reduce(graph()->NewNode(
      machine()->Int32Mul(), Int32Constant(5),
      graph()->NewNode(machine()->Int32Mul(), p0, Int32Constant(3))));
  ASSERT_TRUE(r2.Changed());
  EXPECT_THAT(r2.replacement(), IsInt32Mul(p0, IsInt32Constant(15)));
}

TEST_F(MachineOperatorReducerTest, Int64MulMergeConstants) {
  Node* const p0 = Parameter(0);

  Reduction const r1 = Reduce(graph()->NewNode(
      machine()->Int64Mul(),
      graph()->NewNode(machine()->Int64Mul(), p0, Int64Constant(5)),
      Int64Constant(3)));
  ASSERT_TRUE(r1.Changed());
  EXPECT_THAT(r1.replacement(), IsInt64Mul(p0, IsInt64Constant(15)));

  Reduction const r2 = Reduce(graph()->NewNode(
      machine()->Int64Mul(), Int64Constant(5),
      graph()->NewNode(machine()->Int64Mul(), p0, Int64Constant(3))));
  ASSERT_TRUE(r2.Changed());
  EXPECT_THAT(r2.replacement(), IsInt64Mul(p0, IsInt64Constant(15)));
}

// -----------------------------------------------------------------------------
// Int32AddWithOverflow


TEST_F(MachineOperatorReducerTest, Int32AddWithOverflowWithZero) {
  Node* control = graph()->start();
  Node* p0 = Parameter(0);
  {
    Node* add = graph()->NewNode(machine()->Int32AddWithOverflow(),
                                 Int32Constant(0), p0, control);

    Reduction r =
        Reduce(graph()->NewNode(common()->Projection(1), add, control));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt32Constant(0));

    r = Reduce(graph()->NewNode(common()->Projection(0), add, control));
    ASSERT_TRUE(r.Changed());
    EXPECT_EQ(p0, r.replacement());
  }
  {
    Node* add = graph()->NewNode(machine()->Int32AddWithOverflow(), p0,
                                 Int32Constant(0), control);

    Reduction r =
        Reduce(graph()->NewNode(common()->Projection(1), add, control));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt32Constant(0));

    r = Reduce(graph()->NewNode(common()->Projection(0), add, control));
    ASSERT_TRUE(r.Changed());
    EXPECT_EQ(p0, r.replacement());
  }
}


TEST_F(MachineOperatorReducerTest, Int32AddWithOverflowWithConstant) {
  TRACED_FOREACH(int32_t, x, kInt32Values) {
    Node* control = graph()->start();
    TRACED_FOREACH(int32_t, y, kInt32Values) {
      int32_t z;
      Node* add = graph()->NewNode(machine()->Int32AddWithOverflow(),
                                   Int32Constant(x), Int32Constant(y), control);

      Reduction r =
          Reduce(graph()->NewNode(common()->Projection(1), add, control));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(),
                  IsInt32Constant(base::bits::SignedAddOverflow32(x, y, &z)));

      r = Reduce(graph()->NewNode(common()->Projection(0), add, control));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(), IsInt32Constant(z));
    }
    // This test uses too much memory if we don't periodically reset.
    Reset();
  }
}


// -----------------------------------------------------------------------------
// Int32SubWithOverflow


TEST_F(MachineOperatorReducerTest, Int32SubWithOverflowWithZero) {
  Node* control = graph()->start();
  Node* p0 = Parameter(0);
  Node* add = graph()->NewNode(machine()->Int32SubWithOverflow(), p0,
                               Int32Constant(0), control);

  Reduction r = Reduce(graph()->NewNode(common()->Projection(1), add, control));
  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsInt32Constant(0));

  r = Reduce(graph()->NewNode(common()->Projection(0), add, control));
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(p0, r.replacement());
}


TEST_F(MachineOperatorReducerTest, Int32SubWithOverflowWithConstant) {
  TRACED_FOREACH(int32_t, x, kInt32Values) {
    Node* control = graph()->start();
    TRACED_FOREACH(int32_t, y, kInt32Values) {
      int32_t z;
      Node* add = graph()->NewNode(machine()->Int32SubWithOverflow(),
                                   Int32Constant(x), Int32Constant(y), control);

      Reduction r =
          Reduce(graph()->NewNode(common()->Projection(1), add, control));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(),
                  IsInt32Constant(base::bits::SignedSubOverflow32(x, y, &z)));

      r = Reduce(graph()->NewNode(common()->Projection(0), add, control));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(), IsInt32Constant(z));
    }
    // This test uses too much memory if we don't periodically reset.
    Reset();
  }
}


// -----------------------------------------------------------------------------
// Int32MulWithOverflow

TEST_F(MachineOperatorReducerTest, Int32MulWithOverflowWithZero) {
  Node* control = graph()->start();
  Node* p0 = Parameter(0);
  {
    Node* mul = graph()->NewNode(machine()->Int32MulWithOverflow(),
                                 Int32Constant(0), p0, control);

    Reduction r =
        Reduce(graph()->NewNode(common()->Projection(1), mul, control));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt32Constant(0));

    r = Reduce(graph()->NewNode(common()->Projection(0), mul, control));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt32Constant(0));
  }
  {
    Node* mul = graph()->NewNode(machine()->Int32MulWithOverflow(), p0,
                                 Int32Constant(0), control);

    Reduction r =
        Reduce(graph()->NewNode(common()->Projection(1), mul, control));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt32Constant(0));

    r = Reduce(graph()->NewNode(common()->Projection(0), mul, control));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt32Constant(0));
  }
}

TEST_F(MachineOperatorReducerTest, Int32MulWithOverflowWithOne) {
  Node* control = graph()->start();
  Node* p0 = Parameter(0);
  {
    Node* mul = graph()->NewNode(machine()->Int32MulWithOverflow(),
                                 Int32Constant(1), p0, control);

    Reduction r =
        Reduce(graph()->NewNode(common()->Projection(1), mul, control));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt32Constant(0));

    r = Reduce(graph()->NewNode(common()->Projection(0), mul, control));
    ASSERT_TRUE(r.Changed());
    EXPECT_EQ(p0, r.replacement());
  }
  {
    Node* mul = graph()->NewNode(machine()->Int32MulWithOverflow(), p0,
                                 Int32Constant(1), control);

    Reduction r =
        Reduce(graph()->NewNode(common()->Projection(1), mul, control));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt32Constant(0));

    r = Reduce(graph()->NewNode(common()->Projection(0), mul, control));
    ASSERT_TRUE(r.Changed());
    EXPECT_EQ(p0, r.replacement());
  }
}

TEST_F(MachineOperatorReducerTest, Int32MulWithOverflowWithMinusOne) {
  Node* control = graph()->start();
  Node* p0 = Parameter(0);

  {
    Reduction r = Reduce(graph()->NewNode(machine()->Int32MulWithOverflow(),
                                          Int32Constant(-1), p0, control));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(),
                IsInt32SubWithOverflow(IsInt32Constant(0), p0));
  }

  {
    Reduction r = Reduce(graph()->NewNode(machine()->Int32MulWithOverflow(), p0,
                                          Int32Constant(-1), control));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(),
                IsInt32SubWithOverflow(IsInt32Constant(0), p0));
  }
}

TEST_F(MachineOperatorReducerTest, Int32MulWithOverflowWithTwo) {
  Node* control = graph()->start();
  Node* p0 = Parameter(0);

  {
    Reduction r = Reduce(graph()->NewNode(machine()->Int32MulWithOverflow(),
                                          Int32Constant(2), p0, control));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt32AddWithOverflow(p0, p0));
  }

  {
    Reduction r = Reduce(graph()->NewNode(machine()->Int32MulWithOverflow(), p0,
                                          Int32Constant(2), control));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt32AddWithOverflow(p0, p0));
  }
}

TEST_F(MachineOperatorReducerTest, Int32MulWithOverflowWithConstant) {
  TRACED_FOREACH(int32_t, x, kInt32Values) {
    Node* control = graph()->start();
    TRACED_FOREACH(int32_t, y, kInt32Values) {
      int32_t z;
      Node* mul = graph()->NewNode(machine()->Int32MulWithOverflow(),
                                   Int32Constant(x), Int32Constant(y), control);

      Reduction r =
          Reduce(graph()->NewNode(common()->Projection(1), mul, control));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(),
                  IsInt32Constant(base::bits::SignedMulOverflow32(x, y, &z)));

      r = Reduce(graph()->NewNode(common()->Projection(0), mul, control));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(), IsInt32Constant(z));
    }
    // This test uses too much memory if we don't periodically reset.
    Reset();
  }
}

// -----------------------------------------------------------------------------
// Int64Mul

TEST_F(MachineOperatorReducerTest, Int64MulWithZero) {
  Node* p0 = Parameter(0);
  {
    Node* mul = graph()->NewNode(machine()->Int64Mul(), Int64Constant(0), p0);

    Reduction r = Reduce(mul);
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt64Constant(0));
  }
  {
    Node* mul = graph()->NewNode(machine()->Int64Mul(), p0, Int64Constant(0));

    Reduction r = Reduce(mul);
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt64Constant(0));
  }
}

TEST_F(MachineOperatorReducerTest, Int64MulWithOne) {
  Node* p0 = Parameter(0);
  {
    Node* mul = graph()->NewNode(machine()->Int64Mul(), Int64Constant(1), p0);

    Reduction r = Reduce(mul);
    ASSERT_TRUE(r.Changed());
    EXPECT_EQ(p0, r.replacement());
  }
  {
    Node* mul = graph()->NewNode(machine()->Int64Mul(), p0, Int64Constant(1));

    Reduction r = Reduce(mul);
    ASSERT_TRUE(r.Changed());
    EXPECT_EQ(p0, r.replacement());
  }
}

TEST_F(MachineOperatorReducerTest, Int64MulWithMinusOne) {
  Node* p0 = Parameter(0);

  {
    Reduction r =
        Reduce(graph()->NewNode(machine()->Int64Mul(), Int64Constant(-1), p0));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt64Sub(IsInt64Constant(0), p0));
  }

  {
    Reduction r =
        Reduce(graph()->NewNode(machine()->Int64Mul(), p0, Int64Constant(-1)));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt64Sub(IsInt64Constant(0), p0));
  }
}

TEST_F(MachineOperatorReducerTest, Int64MulWithPowerOfTwo) {
  Node* p0 = Parameter(0);

  {
    Reduction r =
        Reduce(graph()->NewNode(machine()->Int64Mul(), Int64Constant(8), p0));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsWord64Shl(p0, IsInt64Constant(3)));
  }

  {
    Reduction r =
        Reduce(graph()->NewNode(machine()->Int64Mul(), p0, Int64Constant(8)));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsWord64Shl(p0, IsInt64Constant(3)));
  }
}

TEST_F(MachineOperatorReducerTest, Int64MulWithConstant) {
  TRACED_FOREACH(int64_t, x, kInt64Values) {
    TRACED_FOREACH(int64_t, y, kInt64Values) {
      Node* mul = graph()->NewNode(machine()->Int64Mul(), Int64Constant(x),
                                   Int64Constant(y));

      Reduction r = Reduce(mul);
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(),
                  IsInt64Constant(base::MulWithWraparound(x, y)));
    }
  }
}

// -----------------------------------------------------------------------------
// Int32LessThan

TEST_F(MachineOperatorReducerTest, Int32LessThanWithWord32Or) {
  Node* const p0 = Parameter(0);
  TRACED_FOREACH(int32_t, x, kInt32Values) {
    Node* word32_or =
        graph()->NewNode(machine()->Word32Or(), p0, Int32Constant(x));
    Node* less_than = graph()->NewNode(machine()->Int32LessThan(), word32_or,
                                       Int32Constant(0));
    Reduction r = Reduce(less_than);
    if (x < 0) {
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(), IsInt32Constant(1));
    } else {
      ASSERT_FALSE(r.Changed());
    }
  }
}

TEST_F(MachineOperatorReducerTest, Int32LessThanWithWord32SarShiftOutZeros) {
  Node* const p0 = Parameter(0);
  Node* const p1 = Parameter(1);
  TRACED_FORRANGE(int32_t, shift0, 1, 3) {
    TRACED_FORRANGE(int32_t, shift1, 1, 3) {
      Node* const node =
          graph()->NewNode(machine()->Int32LessThan(),
                           graph()->NewNode(machine()->Word32SarShiftOutZeros(),
                                            p0, Int32Constant(shift0)),
                           graph()->NewNode(machine()->Word32SarShiftOutZeros(),
                                            p1, Int32Constant(shift1)));

      Reduction r = Reduce(node);
      if (shift0 == shift1) {
        ASSERT_TRUE(r.Changed());
        EXPECT_THAT(r.replacement(), IsInt32LessThan(p0, p1));
      } else {
        ASSERT_FALSE(r.Changed());
      }
    }
  }
}

// -----------------------------------------------------------------------------
// Uint32LessThan

TEST_F(MachineOperatorReducerTest, Uint32LessThanWithWord32Sar) {
  Node* const p0 = Parameter(0);
  TRACED_FORRANGE(uint32_t, shift, 1, 3) {
    const uint32_t limit = (kMaxInt >> shift) - 1;
    Node* const node = graph()->NewNode(
        machine()->Uint32LessThan(),
        graph()->NewNode(machine()->Word32Sar(), p0, Uint32Constant(shift)),
        Uint32Constant(limit));

    Reduction r = Reduce(node);
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(),
                IsUint32LessThan(
                    p0, IsInt32Constant(static_cast<int32_t>(limit << shift))));
  }
}

TEST_F(MachineOperatorReducerTest, Uint32LessThanWithWord32SarShiftOutZeros) {
  Node* const p0 = Parameter(0);
  Node* const p1 = Parameter(1);
  TRACED_FORRANGE(int32_t, shift0, 1, 3) {
    TRACED_FORRANGE(int32_t, shift1, 1, 3) {
      Node* const node =
          graph()->NewNode(machine()->Uint32LessThan(),
                           graph()->NewNode(machine()->Word32SarShiftOutZeros(),
                                            p0, Int32Constant(shift0)),
                           graph()->NewNode(machine()->Word32SarShiftOutZeros(),
                                            p1, Int32Constant(shift1)));

      Reduction r = Reduce(node);
      if (shift0 == shift1) {
        ASSERT_TRUE(r.Changed());
        EXPECT_THAT(r.replacement(), IsUint32LessThan(p0, p1));
      } else {
        ASSERT_FALSE(r.Changed());
      }
    }
  }
}

// -----------------------------------------------------------------------------
// Uint64LessThan

TEST_F(MachineOperatorReducerTest, Uint64LessThanWithWord64SarShiftOutZeros) {
  Node* const p0 = Parameter(0);
  Node* const p1 = Parameter(1);
  TRACED_FORRANGE(int64_t, shift0, 1, 3) {
    TRACED_FORRANGE(int64_t, shift1, 1, 3) {
      Node* const node =
          graph()->NewNode(machine()->Uint64LessThan(),
                           graph()->NewNode(machine()->Word64SarShiftOutZeros(),
                                            p0, Int64Constant(shift0)),
                           graph()->NewNode(machine()->Word64SarShiftOutZeros(),
                                            p1, Int64Constant(shift1)));

      Reduction r = Reduce(node);
      if (shift0 == shift1) {
        ASSERT_TRUE(r.Changed());
        EXPECT_THAT(r.replacement(), IsUint64LessThan(p0, p1));
      } else {
        ASSERT_FALSE(r.Changed());
      }
    }
  }
}

TEST_F(MachineOperatorReducerTest, Uint64LessThanWithUint32Reduction) {
  Node* const p = Parameter(0);
  TRACED_FORRANGE(int64_t, shift, 1, 3) {
    TRACED_FORRANGE(int64_t, rhs, 1, 3) {
      Node* const node = graph()->NewNode(
          machine()->Uint64LessThan(),
          graph()->NewNode(
              machine()->Word64SarShiftOutZeros(),
              graph()->NewNode(machine()->ChangeUint32ToUint64(), p),
              Int64Constant(shift)),
          Int64Constant(rhs));
      Reduction r = Reduce(node);
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(),
                  IsUint32LessThan(
                      p, IsInt32Constant(static_cast<int32_t>(rhs << shift))));
    }
  }
}

TEST_F(MachineOperatorReducerTest, Uint64LessThanWithInt64AddDontReduce) {
  Node* const p0 = Parameter(0);

  TRACED_FOREACH(uint64_t, k1, kUint64Values) {
    TRACED_FOREACH(uint64_t, k2, kUint64Values) {
      Node* node = graph()->NewNode(
          machine()->Uint64LessThan(),
          graph()->NewNode(machine()->Int64Add(), p0, Int64Constant(k1)),
          Int64Constant(k2));
      Reduction r = Reduce(node);
      // Don't reduce because of potential overflow
      ASSERT_FALSE(r.Changed());
    }
  }
}

TEST_F(MachineOperatorReducerTest,
       Uint64LessThanOrEqualWithInt64AddDontReduce) {
  Node* const p0 = Parameter(0);

  TRACED_FOREACH(uint64_t, k1, kUint64Values) {
    TRACED_FOREACH(uint64_t, k2, kUint64Values) {
      uint64_t k1 = 0;
      uint64_t k2 = 18446744073709551615u;
      Node* node = graph()->NewNode(
          machine()->Uint64LessThanOrEqual(),
          graph()->NewNode(machine()->Int64Add(), p0, Int64Constant(k1)),
          Int64Constant(k2));
      Reduction r = Reduce(node);
      if (k2 == 0) {
        // x <= 0  =>  x == 0
        ASSERT_TRUE(r.Changed());
      } else if (k2 == std::numeric_limits<uint64_t>::max()) {
        // x <= Max  =>  true
        ASSERT_TRUE(r.Changed());
      } else {
        // Don't reduce because of potential overflow
        ASSERT_FALSE(r.Changed());
      }
    }
  }
}

// -----------------------------------------------------------------------------
// Int64LessThan

TEST_F(MachineOperatorReducerTest, Int64LessThanWithWord64SarShiftOutZeros) {
  Node* const p0 = Parameter(0);
  Node* const p1 = Parameter(1);
  TRACED_FORRANGE(int64_t, shift0, 1, 3) {
    TRACED_FORRANGE(int64_t, shift1, 1, 3) {
      Node* const node =
          graph()->NewNode(machine()->Int64LessThan(),
                           graph()->NewNode(machine()->Word64SarShiftOutZeros(),
                                            p0, Int64Constant(shift0)),
                           graph()->NewNode(machine()->Word64SarShiftOutZeros(),
                                            p1, Int64Constant(shift1)));

      Reduction r = Reduce(node);
      if (shift0 == shift1) {
        ASSERT_TRUE(r.Changed());
        EXPECT_THAT(r.replacement(), IsInt64LessThan(p0, p1));
      } else {
        ASSERT_FALSE(r.Changed());
      }
    }
  }
}

TEST_F(MachineOperatorReducerTest, Int64LessThanWithInt32Reduction) {
  Node* const p = Parameter(0);
  TRACED_FORRANGE(int64_t, shift, 1, 3) {
    TRACED_FORRANGE(int64_t, rhs, 1, 3) {
      Node* const node = graph()->NewNode(
          machine()->Int64LessThan(),
          graph()->NewNode(machine()->Word64SarShiftOutZeros(),
                           graph()->NewNode(machine()->ChangeInt32ToInt64(), p),
                           Int64Constant(shift)),
          Int64Constant(rhs));
      Reduction r = Reduce(node);
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(),
                  IsInt32LessThan(
                      p, IsInt32Constant(static_cast<int32_t>(rhs << shift))));
    }
  }
}

// -----------------------------------------------------------------------------
// Float64Mul


TEST_F(MachineOperatorReducerTest, Float64MulWithMinusOne) {
  Node* const p0 = Parameter(0);
  {
    Reduction r = Reduce(
        graph()->NewNode(machine()->Float64Mul(), p0, Float64Constant(-1.0)));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(),
                IsFloat64Sub(IsFloat64Constant(BitEq(-0.0)), p0));
  }
  {
    Reduction r = Reduce(
        graph()->NewNode(machine()->Float64Mul(), Float64Constant(-1.0), p0));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(),
                IsFloat64Sub(IsFloat64Constant(BitEq(-0.0)), p0));
  }
}

TEST_F(MachineOperatorReducerTest, Float64SubMinusZeroMinusX) {
  Node* const p0 = Parameter(0);
  {
    Reduction r = Reduce(
        graph()->NewNode(machine()->Float64Sub(), Float64Constant(-0.0), p0));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsFloat64Neg(p0));
  }
}

TEST_F(MachineOperatorReducerTest, Float32SubMinusZeroMinusX) {
  Node* const p0 = Parameter(0);
  {
    Reduction r = Reduce(
        graph()->NewNode(machine()->Float32Sub(), Float32Constant(-0.0), p0));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsFloat32Neg(p0));
  }
}

TEST_F(MachineOperatorReducerTest, Float64MulWithTwo) {
  Node* const p0 = Parameter(0);
  {
    Reduction r = Reduce(
        graph()->NewNode(machine()->Float64Mul(), Float64Constant(2.0), p0));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsFloat64Add(p0, p0));
  }
  {
    Reduction r = Reduce(
        graph()->NewNode(machine()->Float64Mul(), p0, Float64Constant(2.0)));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsFloat64Add(p0, p0));
  }
}

// -----------------------------------------------------------------------------
// Float64Div

TEST_F(MachineOperatorReducerTest, Float64DivWithMinusOne) {
  Node* const p0 = Parameter(0);
  {
    Reduction r = Reduce(
        graph()->NewNode(machine()->Float64Div(), p0, Float64Constant(-1.0)));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsFloat64Neg(p0));
  }
}

TEST_F(MachineOperatorReducerTest, Float64DivWithPowerOfTwo) {
  Node* const p0 = Parameter(0);
  TRACED_FORRANGE(uint64_t, exponent, 1, 0x7FE) {
    base::Double divisor =
        base::Double(exponent << base::Double::kPhysicalSignificandSize);
    if (divisor.value() == 1.0) continue;  // Skip x / 1.0 => x.
    Reduction r = Reduce(graph()->NewNode(machine()->Float64Div(), p0,
                                          Float64Constant(divisor.value())));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(),
                IsFloat64Mul(p0, IsFloat64Constant(1.0 / divisor.value())));
  }
}

// -----------------------------------------------------------------------------
// Float64Acos

TEST_F(MachineOperatorReducerTest, Float64AcosWithConstant) {
  TRACED_FOREACH(double, x, kFloat64Values) {
    Reduction const r =
        Reduce(graph()->NewNode(machine()->Float64Acos(), Float64Constant(x)));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(
        r.replacement(),
        IsFloat64Constant(NanSensitiveDoubleEq(base::ieee754::acos(x))));
  }
}

// -----------------------------------------------------------------------------
// Float64Acosh

TEST_F(MachineOperatorReducerTest, Float64AcoshWithConstant) {
  TRACED_FOREACH(double, x, kFloat64Values) {
    Reduction const r =
        Reduce(graph()->NewNode(machine()->Float64Acosh(), Float64Constant(x)));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(
        r.replacement(),
        IsFloat64Constant(NanSensitiveDoubleEq(base::ieee754::acosh(x))));
  }
}

// -----------------------------------------------------------------------------
// Float64Asin

TEST_F(MachineOperatorReducerTest, Float64AsinWithConstant) {
  TRACED_FOREACH(double, x, kFloat64Values) {
    Reduction const r =
        Reduce(graph()->NewNode(machine()->Float64Asin(), Float64Constant(x)));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(
        r.replacement(),
        IsFloat64Constant(NanSensitiveDoubleEq(base::ieee754::asin(x))));
  }
}

// -----------------------------------------------------------------------------
// Float64Asinh

TEST_F(MachineOperatorReducerTest, Float64AsinhWithConstant) {
  TRACED_FOREACH(double, x, kFloat64Values) {
    Reduction const r =
        Reduce(graph()->NewNode(machine()->Float64Asinh(), Float64Constant(x)));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(
        r.replacement(),
        IsFloat64Constant(NanSensitiveDoubleEq(base::ieee754::asinh(x))));
  }
}

// -----------------------------------------------------------------------------
// Float64Atan

TEST_F(MachineOperatorReducerTest, Float64AtanWithConstant) {
  TRACED_FOREACH(double, x, kFloat64Values) {
    Reduction const r =
        Reduce(graph()->NewNode(machine()->Float64Atan(), Float64Constant(x)));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(
        r.replacement(),
        IsFloat64Constant(NanSensitiveDoubleEq(base::ieee754::atan(x))));
  }
}

// -----------------------------------------------------------------------------
// Float64Atanh

TEST_F(MachineOperatorReducerTest, Float64AtanhWithConstant) {
  TRACED_FOREACH(double, x, kFloat64Values) {
    Reduction const r =
        Reduce(graph()->NewNode(machine()->Float64Atanh(), Float64Constant(x)));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(
        r.replacement(),
        IsFloat64Constant(NanSensitiveDoubleEq(base::ieee754::atanh(x))));
  }
}

// -----------------------------------------------------------------------------
// Float64Atan2

TEST_F(MachineOperatorReducerTest, Float64Atan2WithConstant) {
  TRACED_FOREACH(double, y, kFloat64Values) {
    TRACED_FOREACH(double, x, kFloat64Values) {
      Reduction const r = Reduce(graph()->NewNode(
          machine()->Float64Atan2(), Float64Constant(y), Float64Constant(x)));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(
          r.replacement(),
          IsFloat64Constant(NanSensitiveDoubleEq(base::ieee754::atan2(y, x))));
    }
  }
}

TEST_F(MachineOperatorReducerTest, Float64Atan2WithNaN) {
  Node* const p0 = Parameter(0);
  const double nan = std::numeric_limits<double>::quiet_NaN();
  Node* const nan_node = Float64Constant(nan);
  {
    Reduction const r =
        Reduce(graph()->NewNode(machine()->Float64Atan2(), p0, nan_node));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsFloat64Constant(NanSensitiveDoubleEq(nan)));
  }
  {
    Reduction const r =
        Reduce(graph()->NewNode(machine()->Float64Atan2(), nan_node, p0));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsFloat64Constant(NanSensitiveDoubleEq(nan)));
  }
}

// -----------------------------------------------------------------------------
// Float64Cos

TEST_F(MachineOperatorReducerTest, Float64CosWithConstant) {
  TRACED_FOREACH(double, x, kFloat64Values) {
    Reduction const r =
        Reduce(graph()->NewNode(machine()->Float64Cos(), Float64Constant(x)));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(),
                IsFloat64Constant(NanSensitiveDoubleEq(COS_IMPL(x))));
  }
}

// -----------------------------------------------------------------------------
// Float64Cosh

TEST_F(MachineOperatorReducerTest, Float64CoshWithConstant) {
  TRACED_FOREACH(double, x, kFloat64Values) {
    Reduction const r =
        Reduce(graph()->NewNode(machine()->Float64Cosh(), Float64Constant(x)));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(
        r.replacement(),
        IsFloat64Constant(NanSensitiveDoubleEq(base::ieee754::cosh(x))));
  }
}

// -----------------------------------------------------------------------------
// Float64Exp

TEST_F(MachineOperatorReducerTest, Float64ExpWithConstant) {
  TRACED_FOREACH(double, x, kFloat64Values) {
    Reduction const r =
        Reduce(graph()->NewNode(machine()->Float64Exp(), Float64Constant(x)));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(),
                IsFloat64Constant(NanSensitiveDoubleEq(base::ieee754::exp(x))));
  }
}

// -----------------------------------------------------------------------------
// Float64Log

TEST_F(MachineOperatorReducerTest, Float64LogWithConstant) {
  TRACED_FOREACH(double, x, kFloat64Values) {
    Reduction const r =
        Reduce(graph()->NewNode(machine()->Float64Log(), Float64Constant(x)));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(),
                IsFloat64Constant(NanSensitiveDoubleEq(base::ieee754::log(x))));
  }
}

// -----------------------------------------------------------------------------
// Float64Log1p

TEST_F(MachineOperatorReducerTest, Float64Log1pWithConstant) {
  TRACED_FOREACH(double, x, kFloat64Values) {
    Reduction const r =
        Reduce(graph()->NewNode(machine()->Float64Log1p(), Float64Constant(x)));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(
        r.replacement(),
        IsFloat64Constant(NanSensitiveDoubleEq(base::ieee754::log1p(x))));
  }
}

// -----------------------------------------------------------------------------
// Float64Pow

TEST_F(MachineOperatorReducerTest, Float64PowWithConstant) {
  TRACED_FOREACH(double, x, kFloat64Values) {
    TRACED_FOREACH(double, y, kFloat64Values) {
      Reduction const r = Reduce(graph()->NewNode(
          machine()->Float64Pow(), Float64Constant(x), Float64Constant(y)));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(
          r.replacement(),
          IsFloat64Constant(NanSensitiveDoubleEq(base::ieee754::pow(x, y))));
    }
  }
}

TEST_F(MachineOperatorReducerTest, Float64PowWithZeroExponent) {
  Node* const p0 = Parameter(0);
  {
    Reduction const r = Reduce(
        graph()->NewNode(machine()->Float64Pow(), p0, Float64Constant(-0.0)));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsFloat64Constant(1.0));
  }
  {
    Reduction const r = Reduce(
        graph()->NewNode(machine()->Float64Pow(), p0, Float64Constant(0.0)));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsFloat64Constant(1.0));
  }
}

// -----------------------------------------------------------------------------
// Float64Sin

TEST_F(MachineOperatorReducerTest, Float64SinWithConstant) {
  TRACED_FOREACH(double, x, kFloat64Values) {
    Reduction const r =
        Reduce(graph()->NewNode(machine()->Float64Sin(), Float64Constant(x)));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(),
                IsFloat64Constant(NanSensitiveDoubleEq(SIN_IMPL(x))));
  }
}

// -----------------------------------------------------------------------------
// Float64Sinh

TEST_F(MachineOperatorReducerTest, Float64SinhWithConstant) {
  TRACED_FOREACH(double, x, kFloat64Values) {
    Reduction const r =
        Reduce(graph()->NewNode(machine()->Float64Sinh(), Float64Constant(x)));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(
        r.replacement(),
        IsFloat64Constant(NanSensitiveDoubleEq(base::ieee754::sinh(x))));
  }
}

// -----------------------------------------------------------------------------
// Float64Tan

TEST_F(MachineOperatorReducerTest, Float64TanWithConstant) {
  TRACED_FOREACH(double, x, kFloat64Values) {
    Reduction const r =
        Reduce(graph()->NewNode(machine()->Float64Tan(), Float64Constant(x)));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(),
                IsFloat64Constant(NanSensitiveDoubleEq(base::ieee754::tan(x))));
  }
}

// -----------------------------------------------------------------------------
// Float64Tanh

TEST_F(MachineOperatorReducerTest, Float64TanhWithConstant) {
  TRACED_FOREACH(double, x, kFloat64Values) {
    Reduction const r =
        Reduce(graph()->NewNode(machine()->Float64Tanh(), Float64Constant(x)));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(
        r.replacement(),
        IsFloat64Constant(NanSensitiveDoubleEq(base::ieee754::tanh(x))));
  }
}

// -----------------------------------------------------------------------------
// Float64InsertLowWord32

TEST_F(MachineOperatorReducerTest, Float64InsertLowWord32WithConstant) {
  TRACED_FOREACH(double, x, kFloat64Values) {
    TRACED_FOREACH(uint32_t, y, kUint32Values) {
      Reduction const r =
          Reduce(graph()->NewNode(machine()->Float64InsertLowWord32(),
                                  Float64Constant(x), Uint32Constant(y)));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(
          r.replacement(),
          IsFloat64Constant(BitEq(base::bit_cast<double>(
              (base::bit_cast<uint64_t>(x) & uint64_t{0xFFFFFFFF00000000}) |
              y))));
    }
  }
}


// -----------------------------------------------------------------------------
// Float64InsertHighWord32


TEST_F(MachineOperatorReducerTest, Float64InsertHighWord32WithConstant) {
  TRACED_FOREACH(double, x, kFloat64Values) {
    TRACED_FOREACH(uint32_t, y, kUint32Values) {
      Reduction const r =
          Reduce(graph()->NewNode(machine()->Float64InsertHighWord32(),
                                  Float64Constant(x), Uint32Constant(y)));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(),
                  IsFloat64Constant(BitEq(base::bit_cast<double>(
                      (base::bit_cast<uint64_t>(x) & uint64_t{0xFFFFFFFF}) |
                      (static_cast<uint64_t>(y) << 32)))));
    }
  }
}


// -----------------------------------------------------------------------------
// Float64Equal

TEST_F(MachineOperatorReducerTest, Float64EqualWithConstant) {
  TRACED_FOREACH(double, x, kFloat64Values) {
    TRACED_FOREACH(double, y, kFloat64Values) {
      Reduction const r = Reduce(graph()->NewNode(
          machine()->Float64Equal(), Float64Constant(x), Float64Constant(y)));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(), IsInt32Constant(x == y));
    }
  }
}

TEST_F(MachineOperatorReducerTest, Float64EqualWithFloat32Conversions) {
  Node* const p0 = Parameter(0);
  Node* const p1 = Parameter(1);
  Reduction const r = Reduce(graph()->NewNode(
      machine()->Float64Equal(),
      graph()->NewNode(machine()->ChangeFloat32ToFloat64(), p0),
      graph()->NewNode(machine()->ChangeFloat32ToFloat64(), p1)));
  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsFloat32Equal(p0, p1));
}


TEST_F(MachineOperatorReducerTest, Float64EqualWithFloat32Constant) {
  Node* const p0 = Parameter(0);
  TRACED_FOREACH(float, x, kFloat32Values) {
    Reduction r = Reduce(graph()->NewNode(
        machine()->Float64Equal(),
        graph()->NewNode(machine()->ChangeFloat32ToFloat64(), p0),
        Float64Constant(x)));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsFloat32Equal(p0, IsFloat32Constant(x)));
  }
}


// -----------------------------------------------------------------------------
// Float64LessThan

TEST_F(MachineOperatorReducerTest, Float64LessThanWithConstant) {
  TRACED_FOREACH(double, x, kFloat64Values) {
    TRACED_FOREACH(double, y, kFloat64Values) {
      Reduction const r =
          Reduce(graph()->NewNode(machine()->Float64LessThan(),
                                  Float64Constant(x), Float64Constant(y)));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(), IsInt32Constant(x < y));
    }
  }
}

TEST_F(MachineOperatorReducerTest, Float64LessThanWithFloat32Conversions) {
  Node* const p0 = Parameter(0);
  Node* const p1 = Parameter(1);
  Reduction const r = Reduce(graph()->NewNode(
      machine()->Float64LessThan(),
      graph()->NewNode(machine()->ChangeFloat32ToFloat64(), p0),
      graph()->NewNode(machine()->ChangeFloat32ToFloat64(), p1)));
  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsFloat32LessThan(p0, p1));
}


TEST_F(MachineOperatorReducerTest, Float64LessThanWithFloat32Constant) {
  Node* const p0 = Parameter(0);
  {
    TRACED_FOREACH(float, x, kFloat32Values) {
      Reduction r = Reduce(graph()->NewNode(
          machine()->Float64LessThan(),
          graph()->NewNode(machine()->ChangeFloat32ToFloat64(), p0),
          Float64Constant(x)));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(), IsFloat32LessThan(p0, IsFloat32Constant(x)));
    }
  }
  {
    TRACED_FOREACH(float, x, kFloat32Values) {
      Reduction r = Reduce(graph()->NewNode(
          machine()->Float64LessThan(), Float64Constant(x),
          graph()->NewNode(machine()->ChangeFloat32ToFloat64(), p0)));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(), IsFloat32LessThan(IsFloat32Constant(x), p0));
    }
  }
}


// -----------------------------------------------------------------------------
// Float64LessThanOrEqual

TEST_F(MachineOperatorReducerTest, Float64LessThanOrEqualWithConstant) {
  TRACED_FOREACH(double, x, kFloat64Values) {
    TRACED_FOREACH(double, y, kFloat64Values) {
      Reduction const r =
          Reduce(graph()->NewNode(machine()->Float64LessThanOrEqual(),
                                  Float64Constant(x), Float64Constant(y)));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(), IsInt32Constant(x <= y));
    }
  }
}

TEST_F(MachineOperatorReducerTest,
       Float64LessThanOrEqualWithFloat32Conversions) {
  Node* const p0 = Parameter(0);
  Node* const p1 = Parameter(1);
  Reduction const r = Reduce(graph()->NewNode(
      machine()->Float64LessThanOrEqual(),
      graph()->NewNode(machine()->ChangeFloat32ToFloat64(), p0),
      graph()->NewNode(machine()->ChangeFloat32ToFloat64(), p1)));
  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(), IsFloat32LessThanOrEqual(p0, p1));
}


TEST_F(MachineOperatorReducerTest, Float64LessThanOrEqualWithFloat32Constant) {
  Node* const p0 = Parameter(0);
  {
    TRACED_FOREACH(float, x, kFloat32Values) {
      Reduction r = Reduce(graph()->NewNode(
          machine()->Float64LessThanOrEqual(),
          graph()->NewNode(machine()->ChangeFloat32ToFloat64(), p0),
          Float64Constant(x)));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(),
                  IsFloat32LessThanOrEqual(p0, IsFloat32Constant(x)));
    }
  }
  {
    TRACED_FOREACH(float, x, kFloat32Values) {
      Reduction r = Reduce(graph()->NewNode(
          machine()->Float64LessThanOrEqual(), Float64Constant(x),
          graph()->NewNode(machine()->ChangeFloat32ToFloat64(), p0)));
      ASSERT_TRUE(r.Changed());
      EXPECT_THAT(r.replacement(),
                  IsFloat32LessThanOrEqual(IsFloat32Constant(x), p0));
    }
  }
}


// -----------------------------------------------------------------------------
// Float64RoundDown

TEST_F(MachineOperatorReducerTest, Float64RoundDownWithConstant) {
  TRACED_FOREACH(double, x, kFloat64Values) {
    Reduction r = Reduce(graph()->NewNode(
        machine()->Float64RoundDown().placeholder(), Float64Constant(x)));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsFloat64Constant(std::floor(x)));
  }
}

// -----------------------------------------------------------------------------
// Store

TEST_F(MachineOperatorReducerTest, StoreRepWord8WithWord32And) {
  const StoreRepresentation rep(MachineRepresentation::kWord8, kNoWriteBarrier);
  Node* const base = Parameter(0);
  Node* const index = Parameter(1);
  Node* const value = Parameter(2);
  Node* const effect = graph()->start();
  Node* const control = graph()->start();
  TRACED_FOREACH(uint32_t, x, kUint32Values) {
    Node* const node =
        graph()->NewNode(machine()->Store(rep), base, index,
                         graph()->NewNode(machine()->Word32And(), value,
                                          Uint32Constant(x | 0xFFu)),
                         effect, control);

    Reduction r = Reduce(node);
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(),
                IsStore(rep, base, index, value, effect, control));
  }
}


TEST_F(MachineOperatorReducerTest, StoreRepWord8WithWord32SarAndWord32Shl) {
  const StoreRepresentation rep(MachineRepresentation::kWord8, kNoWriteBarrier);
  Node* const base = Parameter(0);
  Node* const index = Parameter(1);
  Node* const value = Parameter(2);
  Node* const effect = graph()->start();
  Node* const control = graph()->start();
  TRACED_FORRANGE(int32_t, x, 1, 24) {
    Node* const node = graph()->NewNode(
        machine()->Store(rep), base, index,
        graph()->NewNode(
            machine()->Word32Sar(),
            graph()->NewNode(machine()->Word32Shl(), value, Int32Constant(x)),
            Int32Constant(x)),
        effect, control);

    Reduction r = Reduce(node);
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(),
                IsStore(rep, base, index, value, effect, control));
  }
}


TEST_F(MachineOperatorReducerTest, StoreRepWord16WithWord32And) {
  const StoreRepresentation rep(MachineRepresentation::kWord16,
                                kNoWriteBarrier);
  Node* const base = Parameter(0);
  Node* const index = Parameter(1);
  Node* const value = Parameter(2);
  Node* const effect = graph()->start();
  Node* const control = graph()->start();
  TRACED_FOREACH(uint32_t, x, kUint32Values) {
    Node* const node =
        graph()->NewNode(machine()->Store(rep), base, index,
                         graph()->NewNode(machine()->Word32And(), value,
                                          Uint32Constant(x | 0xFFFFu)),
                         effect, control);

    Reduction r = Reduce(node);
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(),
                IsStore(rep, base, index, value, effect, control));
  }
}


TEST_F(MachineOperatorReducerTest, StoreRepWord16WithWord32SarAndWord32Shl) {
  const StoreRepresentation rep(MachineRepresentation::kWord16,
                                kNoWriteBarrier);
  Node* const base = Parameter(0);
  Node* const index = Parameter(1);
  Node* const value = Parameter(2);
  Node* const effect = graph()->start();
  Node* const control = graph()->start();
  TRACED_FORRANGE(int32_t, x, 1, 16) {
    Node* const node = graph()->NewNode(
        machine()->Store(rep), base, index,
        graph()->NewNode(
            machine()->Word32Sar(),
            graph()->NewNode(machine()->Word32Shl(), value, Int32Constant(x)),
            Int32Constant(x)),
        effect, control);

    Reduction r = Reduce(node);
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(),
                IsStore(rep, base, index, value, effect, control));
  }
}

TEST_F(MachineOperatorReducerTest, Select) {
  static const std::vector<const Operator*> ops = {
      machine()->Float32Select().op(), machine()->Float64Select().op(),
      machine()->Word32Select().op(), machine()->Word64Select().op()};

  TRACED_FOREACH(const Operator*, op, ops) {
    Node* arg0 = Parameter(0);
    Node* arg1 = Parameter(1);

    Node* select_true = graph()->NewNode(op, Int32Constant(1), arg0, arg1);
    Reduction r_true = Reduce(select_true);
    ASSERT_TRUE(r_true.Changed());
    EXPECT_THAT(r_true.replacement(), IsParameter(0));

    Node* select_false = graph()->NewNode(op, Int32Constant(0), arg0, arg1);
    Reduction r_false = Reduce(select_false);
    ASSERT_TRUE(r_false.Changed());
    EXPECT_THAT(r_false.replacement(), IsParameter(1));
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
