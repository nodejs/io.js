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

#include "absl/random/internal/chi_square.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <numeric>
#include <vector>

#include "gtest/gtest.h"
#include "absl/base/macros.h"

using absl::random_internal::ChiSquare;
using absl::random_internal::ChiSquarePValue;
using absl::random_internal::ChiSquareValue;
using absl::random_internal::ChiSquareWithExpected;

namespace {

TEST(ChiSquare, Value) {
  struct {
    int line;
    double chi_square;
    int df;
    double confidence;
  } const specs[] = {
      // Testing lookup at 1% confidence
      {__LINE__, 0, 0, 0.01},
      {__LINE__, 0.00016, 1, 0.01},
      {__LINE__, 1.64650, 8, 0.01},
      {__LINE__, 5.81221, 16, 0.01},
      {__LINE__, 156.4319, 200, 0.01},
      {__LINE__, 1121.3784, 1234, 0.01},
      {__LINE__, 53557.1629, 54321, 0.01},
      {__LINE__, 651662.6647, 654321, 0.01},

      // Testing lookup at 99% confidence
      {__LINE__, 0, 0, 0.99},
      {__LINE__, 6.635, 1, 0.99},
      {__LINE__, 20.090, 8, 0.99},
      {__LINE__, 32.000, 16, 0.99},
      {__LINE__, 249.4456, 200, 0.99},
      {__LINE__, 1131.1573, 1023, 0.99},
      {__LINE__, 1352.5038, 1234, 0.99},
      {__LINE__, 55090.7356, 54321, 0.99},
      {__LINE__, 656985.1514, 654321, 0.99},

      // Testing lookup at 99.9% confidence
      {__LINE__, 16.2659, 3, 0.999},
      {__LINE__, 22.4580, 6, 0.999},
      {__LINE__, 267.5409, 200, 0.999},
      {__LINE__, 1168.5033, 1023, 0.999},
      {__LINE__, 55345.1741, 54321, 0.999},
      {__LINE__, 657861.7284, 654321, 0.999},
      {__LINE__, 51.1772, 24, 0.999},
      {__LINE__, 59.7003, 30, 0.999},
      {__LINE__, 37.6984, 15, 0.999},
      {__LINE__, 29.5898, 10, 0.999},
      {__LINE__, 27.8776, 9, 0.999},

      // Testing lookup at random confidences
      {__LINE__, 0.000157088, 1, 0.01},
      {__LINE__, 5.31852, 2, 0.93},
      {__LINE__, 1.92256, 4, 0.25},
      {__LINE__, 10.7709, 13, 0.37},
      {__LINE__, 26.2514, 17, 0.93},
      {__LINE__, 36.4799, 29, 0.84},
      {__LINE__, 25.818, 31, 0.27},
      {__LINE__, 63.3346, 64, 0.50},
      {__LINE__, 196.211, 128, 0.9999},
      {__LINE__, 215.21, 243, 0.10},
      {__LINE__, 285.393, 256, 0.90},
      {__LINE__, 984.504, 1024, 0.1923},
      {__LINE__, 2043.85, 2048, 0.4783},
      {__LINE__, 48004.6, 48273, 0.194},
  };
  for (const auto& spec : specs) {
    SCOPED_TRACE(spec.line);
    // Verify all values are have at most a 1% relative error.
    const double val = ChiSquareValue(spec.df, spec.confidence);
    const double err = std::max(5e-6, spec.chi_square / 5e3);  // 1 part in 5000
    EXPECT_NEAR(spec.chi_square, val, err) << spec.line;
  }

  // Relaxed test for extreme values, from
  //  http://www.ciphersbyritter.com/JAVASCRP/NORMCHIK.HTM#ChiSquare
  EXPECT_NEAR(49.2680, ChiSquareValue(100, 1e-6), 5);  // 0.000'005 mark
  EXPECT_NEAR(123.499, ChiSquareValue(200, 1e-6), 5);  // 0.000'005 mark

  EXPECT_NEAR(149.449, ChiSquareValue(100, 0.999), 0.01);
  EXPECT_NEAR(161.318, ChiSquareValue(100, 0.9999), 0.01);
  EXPECT_NEAR(172.098, ChiSquareValue(100, 0.99999), 0.01);

  EXPECT_NEAR(381.426, ChiSquareValue(300, 0.999), 0.05);
  EXPECT_NEAR(399.756, ChiSquareValue(300, 0.9999), 0.1);
  EXPECT_NEAR(416.126, ChiSquareValue(300, 0.99999), 0.2);
}

TEST(ChiSquareTest, PValue) {
  struct {
    int line;
    double pval;
    double chi_square;
    int df;
  } static const specs[] = {
      {__LINE__, 1, 0, 0},
      {__LINE__, 0, 0.001, 0},
      {__LINE__, 1.000, 0, 453},
      {__LINE__, 0.134471, 7972.52, 7834},
      {__LINE__, 0.203922, 28.32, 23},
      {__LINE__, 0.737171, 48274, 48472},
      {__LINE__, 0.444146, 583.1234, 579},
      {__LINE__, 0.294814, 138.2, 130},
      {__LINE__, 0.0816532, 12.63, 7},
      {__LINE__, 0, 682.32, 67},
      {__LINE__, 0.49405, 999, 999},
      {__LINE__, 1.000, 0, 9999},
      {__LINE__, 0.997477, 0.00001, 1},
      {__LINE__, 0, 5823.21, 5040},
  };
  for (const auto& spec : specs) {
    SCOPED_TRACE(spec.line);
    const double pval = ChiSquarePValue(spec.chi_square, spec.df);
    EXPECT_NEAR(spec.pval, pval, 1e-3);
  }
}

TEST(ChiSquareTest, CalcChiSquare) {
  struct {
    int line;
    std::vector<int> expected;
    std::vector<int> actual;
  } const specs[] = {
      {__LINE__,
       {56, 234, 76, 1, 546, 1, 87, 345, 1, 234},
       {2, 132, 4, 43, 234, 8, 345, 8, 236, 56}},
      {__LINE__,
       {123, 36, 234, 367, 345, 2, 456, 567, 234, 567},
       {123, 56, 2345, 8, 345, 8, 2345, 23, 48, 267}},
      {__LINE__,
       {123, 234, 345, 456, 567, 678, 789, 890, 98, 76},
       {123, 234, 345, 456, 567, 678, 789, 890, 98, 76}},
      {__LINE__, {3, 675, 23, 86, 2, 8, 2}, {456, 675, 23, 86, 23, 65, 2}},
      {__LINE__, {1}, {23}},
  };
  for (const auto& spec : specs) {
    SCOPED_TRACE(spec.line);
    double chi_square = 0;
    for (int i = 0; i < spec.expected.size(); ++i) {
      const double diff = spec.actual[i] - spec.expected[i];
      chi_square += (diff * diff) / spec.expected[i];
    }
    EXPECT_NEAR(chi_square,
                ChiSquare(std::begin(spec.actual), std::end(spec.actual),
                          std::begin(spec.expected), std::end(spec.expected)),
                1e-5);
  }
}

TEST(ChiSquareTest, CalcChiSquareInt64) {
  const int64_t data[3] = {910293487, 910292491, 910216780};
  // $ python -c "import scipy.stats
  // > print scipy.stats.chisquare([910293487, 910292491, 910216780])[0]"
  // 4.25410123524
  double sum = std::accumulate(std::begin(data), std::end(data), double{0});
  size_t n = std::distance(std::begin(data), std::end(data));
  double a = ChiSquareWithExpected(std::begin(data), std::end(data), sum / n);
  EXPECT_NEAR(4.254101, a, 1e-6);

  // ... Or with known values.
  double b =
      ChiSquareWithExpected(std::begin(data), std::end(data), 910267586.0);
  EXPECT_NEAR(4.254101, b, 1e-6);
}

TEST(ChiSquareTest, TableData) {
  // Test data from
  // http://www.itl.nist.gov/div898/handbook/eda/section3/eda3674.htm
  //    0.90      0.95     0.975      0.99     0.999
  const double data[100][5] = {
      /* 1*/ {2.706, 3.841, 5.024, 6.635, 10.828},
      /* 2*/ {4.605, 5.991, 7.378, 9.210, 13.816},
      /* 3*/ {6.251, 7.815, 9.348, 11.345, 16.266},
      /* 4*/ {7.779, 9.488, 11.143, 13.277, 18.467},
      /* 5*/ {9.236, 11.070, 12.833, 15.086, 20.515},
      /* 6*/ {10.645, 12.592, 14.449, 16.812, 22.458},
      /* 7*/ {12.017, 14.067, 16.013, 18.475, 24.322},
      /* 8*/ {13.362, 15.507, 17.535, 20.090, 26.125},
      /* 9*/ {14.684, 16.919, 19.023, 21.666, 27.877},
      /*10*/ {15.987, 18.307, 20.483, 23.209, 29.588},
      /*11*/ {17.275, 19.675, 21.920, 24.725, 31.264},
      /*12*/ {18.549, 21.026, 23.337, 26.217, 32.910},
      /*13*/ {19.812, 22.362, 24.736, 27.688, 34.528},
      /*14*/ {21.064, 23.685, 26.119, 29.141, 36.123},
      /*15*/ {22.307, 24.996, 27.488, 30.578, 37.697},
      /*16*/ {23.542, 26.296, 28.845, 32.000, 39.252},
      /*17*/ {24.769, 27.587, 30.191, 33.409, 40.790},
      /*18*/ {25.989, 28.869, 31.526, 34.805, 42.312},
      /*19*/ {27.204, 30.144, 32.852, 36.191, 43.820},
      /*20*/ {28.412, 31.410, 34.170, 37.566, 45.315},
      /*21*/ {29.615, 32.671, 35.479, 38.932, 46.797},
      /*22*/ {30.813, 33.924, 36.781, 40.289, 48.268},
      /*23*/ {32.007, 35.172, 38.076, 41.638, 49.728},
      /*24*/ {33.196, 36.415, 39.364, 42.980, 51.179},
      /*25*/ {34.382, 37.652, 40.646, 44.314, 52.620},
      /*26*/ {35.563, 38.885, 41.923, 45.642, 54.052},
      /*27*/ {36.741, 40.113, 43.195, 46.963, 55.476},
      /*28*/ {37.916, 41.337, 44.461, 48.278, 56.892},
      /*29*/ {39.087, 42.557, 45.722, 49.588, 58.301},
      /*30*/ {40.256, 43.773, 46.979, 50.892, 59.703},
      /*31*/ {41.422, 44.985, 48.232, 52.191, 61.098},
      /*32*/ {42.585, 46.194, 49.480, 53.486, 62.487},
      /*33*/ {43.745, 47.400, 50.725, 54.776, 63.870},
      /*34*/ {44.903, 48.602, 51.966, 56.061, 65.247},
      /*35*/ {46.059, 49.802, 53.203, 57.342, 66.619},
      /*36*/ {47.212, 50.998, 54.437, 58.619, 67.985},
      /*37*/ {48.363, 52.192, 55.668, 59.893, 69.347},
      /*38*/ {49.513, 53.384, 56.896, 61.162, 70.703},
      /*39*/ {50.660, 54.572, 58.120, 62.428, 72.055},
      /*40*/ {51.805, 55.758, 59.342, 63.691, 73.402},
      /*41*/ {52.949, 56.942, 60.561, 64.950, 74.745},
      /*42*/ {54.090, 58.124, 61.777, 66.206, 76.084},
      /*43*/ {55.230, 59.304, 62.990, 67.459, 77.419},
      /*44*/ {56.369, 60.481, 64.201, 68.710, 78.750},
      /*45*/ {57.505, 61.656, 65.410, 69.957, 80.077},
      /*46*/ {58.641, 62.830, 66.617, 71.201, 81.400},
      /*47*/ {59.774, 64.001, 67.821, 72.443, 82.720},
      /*48*/ {60.907, 65.171, 69.023, 73.683, 84.037},
      /*49*/ {62.038, 66.339, 70.222, 74.919, 85.351},
      /*50*/ {63.167, 67.505, 71.420, 76.154, 86.661},
      /*51*/ {64.295, 68.669, 72.616, 77.386, 87.968},
      /*52*/ {65.422, 69.832, 73.810, 78.616, 89.272},
      /*53*/ {66.548, 70.993, 75.002, 79.843, 90.573},
      /*54*/ {67.673, 72.153, 76.192, 81.069, 91.872},
      /*55*/ {68.796, 73.311, 77.380, 82.292, 93.168},
      /*56*/ {69.919, 74.468, 78.567, 83.513, 94.461},
      /*57*/ {71.040, 75.624, 79.752, 84.733, 95.751},
      /*58*/ {72.160, 76.778, 80.936, 85.950, 97.039},
      /*59*/ {73.279, 77.931, 82.117, 87.166, 98.324},
      /*60*/ {74.397, 79.082, 83.298, 88.379, 99.607},
      /*61*/ {75.514, 80.232, 84.476, 89.591, 100.888},
      /*62*/ {76.630, 81.381, 85.654, 90.802, 102.166},
      /*63*/ {77.745, 82.529, 86.830, 92.010, 103.442},
      /*64*/ {78.860, 83.675, 88.004, 93.217, 104.716},
      /*65*/ {79.973, 84.821, 89.177, 94.422, 105.988},
      /*66*/ {81.085, 85.965, 90.349, 95.626, 107.258},
      /*67*/ {82.197, 87.108, 91.519, 96.828, 108.526},
      /*68*/ {83.308, 88.250, 92.689, 98.028, 109.791},
      /*69*/ {84.418, 89.391, 93.856, 99.228, 111.055},
      /*70*/ {85.527, 90.531, 95.023, 100.425, 112.317},
      /*71*/ {86.635, 91.670, 96.189, 101.621, 113.577},
      /*72*/ {87.743, 92.808, 97.353, 102.816, 114.835},
      /*73*/ {88.850, 93.945, 98.516, 104.010, 116.092},
      /*74*/ {89.956, 95.081, 99.678, 105.202, 117.346},
      /*75*/ {91.061, 96.217, 100.839, 106.393, 118.599},
      /*76*/ {92.166, 97.351, 101.999, 107.583, 119.850},
      /*77*/ {93.270, 98.484, 103.158, 108.771, 121.100},
      /*78*/ {94.374, 99.617, 104.316, 109.958, 122.348},
      /*79*/ {95.476, 100.749, 105.473, 111.144, 123.594},
      /*80*/ {96.578, 101.879, 106.629, 112.329, 124.839},
      /*81*/ {97.680, 103.010, 107.783, 113.512, 126.083},
      /*82*/ {98.780, 104.139, 108.937, 114.695, 127.324},
      /*83*/ {99.880, 105.267, 110.090, 115.876, 128.565},
      /*84*/ {100.980, 106.395, 111.242, 117.057, 129.804},
      /*85*/ {102.079, 107.522, 112.393, 118.236, 131.041},
      /*86*/ {103.177, 108.648, 113.544, 119.414, 132.277},
      /*87*/ {104.275, 109.773, 114.693, 120.591, 133.512},
      /*88*/ {105.372, 110.898, 115.841, 121.767, 134.746},
      /*89*/ {106.469, 112.022, 116.989, 122.942, 135.978},
      /*90*/ {107.565, 113.145, 118.136, 124.116, 137.208},
      /*91*/ {108.661, 114.268, 119.282, 125.289, 138.438},
      /*92*/ {109.756, 115.390, 120.427, 126.462, 139.666},
      /*93*/ {110.850, 116.511, 121.571, 127.633, 140.893},
      /*94*/ {111.944, 117.632, 122.715, 128.803, 142.119},
      /*95*/ {113.038, 118.752, 123.858, 129.973, 143.344},
      /*96*/ {114.131, 119.871, 125.000, 131.141, 144.567},
      /*97*/ {115.223, 120.990, 126.141, 132.309, 145.789},
      /*98*/ {116.315, 122.108, 127.282, 133.476, 147.010},
      /*99*/ {117.407, 123.225, 128.422, 134.642, 148.230},
      /*100*/ {118.498, 124.342, 129.561, 135.807, 149.449} /**/};

  //    0.90      0.95     0.975      0.99     0.999
  for (int i = 0; i < ABSL_ARRAYSIZE(data); i++) {
    const double E = 0.0001;
    EXPECT_NEAR(ChiSquarePValue(data[i][0], i + 1), 0.10, E)
        << i << " " << data[i][0];
    EXPECT_NEAR(ChiSquarePValue(data[i][1], i + 1), 0.05, E)
        << i << " " << data[i][1];
    EXPECT_NEAR(ChiSquarePValue(data[i][2], i + 1), 0.025, E)
        << i << " " << data[i][2];
    EXPECT_NEAR(ChiSquarePValue(data[i][3], i + 1), 0.01, E)
        << i << " " << data[i][3];
    EXPECT_NEAR(ChiSquarePValue(data[i][4], i + 1), 0.001, E)
        << i << " " << data[i][4];

    const double F = 0.1;
    EXPECT_NEAR(ChiSquareValue(i + 1, 0.90), data[i][0], F) << i;
    EXPECT_NEAR(ChiSquareValue(i + 1, 0.95), data[i][1], F) << i;
    EXPECT_NEAR(ChiSquareValue(i + 1, 0.975), data[i][2], F) << i;
    EXPECT_NEAR(ChiSquareValue(i + 1, 0.99), data[i][3], F) << i;
    EXPECT_NEAR(ChiSquareValue(i + 1, 0.999), data[i][4], F) << i;
  }
}

TEST(ChiSquareTest, ChiSquareTwoIterator) {
  // Test data from http://www.stat.yale.edu/Courses/1997-98/101/chigf.htm
  // Null-hypothesis: This data is normally distributed.
  const int counts[10] = {6, 6, 18, 33, 38, 38, 28, 21, 9, 3};
  const double expected[10] = {4.6,  8.8,  18.4, 30.0, 38.2,
                               38.2, 30.0, 18.4, 8.8,  4.6};
  double chi_square = ChiSquare(std::begin(counts), std::end(counts),
                                std::begin(expected), std::end(expected));
  EXPECT_NEAR(chi_square, 2.69, 0.001);

  // Degrees of freedom: 10 bins. two estimated parameters. = 10 - 2 - 1.
  const int dof = 7;
  // The critical value of 7, 95% => 14.067 (see above test)
  double p_value_05 = ChiSquarePValue(14.067, dof);
  EXPECT_NEAR(p_value_05, 0.05, 0.001);  // 95%-ile p-value

  double p_actual = ChiSquarePValue(chi_square, dof);
  EXPECT_GT(p_actual, 0.05);  // Accept the null hypothesis.
}

TEST(ChiSquareTest, DiceRolls) {
  // Assume we are testing 102 fair dice rolls.
  // Null-hypothesis: This data is fairly distributed.
  //
  // The dof value of 4, @95% = 9.488 (see above test)
  // The dof value of 5, @95% = 11.070
  const int rolls[6] = {22, 11, 17, 14, 20, 18};
  double sum = std::accumulate(std::begin(rolls), std::end(rolls), double{0});
  size_t n = std::distance(std::begin(rolls), std::end(rolls));

  double a = ChiSquareWithExpected(std::begin(rolls), std::end(rolls), sum / n);
  EXPECT_NEAR(a, 4.70588, 1e-5);
  EXPECT_LT(a, ChiSquareValue(4, 0.95));

  double p_a = ChiSquarePValue(a, 4);
  EXPECT_NEAR(p_a, 0.318828, 1e-5);  // Accept the null hypothesis.

  double b = ChiSquareWithExpected(std::begin(rolls), std::end(rolls), 17.0);
  EXPECT_NEAR(b, 4.70588, 1e-5);
  EXPECT_LT(b, ChiSquareValue(5, 0.95));

  double p_b = ChiSquarePValue(b, 5);
  EXPECT_NEAR(p_b, 0.4528180, 1e-5);  // Accept the null hypothesis.
}

}  // namespace
