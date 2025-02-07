// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/ieee754.h"

#include <limits>

#include "src/base/overflowing-math.h"
#include "testing/gmock-support.h"

using testing::BitEq;
using testing::IsNaN;

namespace v8 {
namespace base {
namespace ieee754 {

namespace {

double const kE = 2.718281828459045;
double const kPI = 3.141592653589793;
double const kTwo120 = 1.329227995784916e+36;
double const kInfinity = std::numeric_limits<double>::infinity();
double const kQNaN = std::numeric_limits<double>::quiet_NaN();
double const kSNaN = std::numeric_limits<double>::signaling_NaN();

}  // namespace

TEST(Ieee754, Acos) {
  EXPECT_THAT(acos(kInfinity), IsNaN());
  EXPECT_THAT(acos(-kInfinity), IsNaN());
  EXPECT_THAT(acos(kQNaN), IsNaN());
  EXPECT_THAT(acos(kSNaN), IsNaN());

  EXPECT_EQ(0.0, acos(1.0));
}

TEST(Ieee754, Acosh) {
  // Tests for acosh for exceptional values
  EXPECT_EQ(kInfinity, acosh(kInfinity));
  EXPECT_THAT(acosh(-kInfinity), IsNaN());
  EXPECT_THAT(acosh(kQNaN), IsNaN());
  EXPECT_THAT(acosh(kSNaN), IsNaN());
  EXPECT_THAT(acosh(0.9), IsNaN());

  // Test basic acosh functionality
  EXPECT_EQ(0.0, acosh(1.0));
  // acosh(1.5) = log((sqrt(5)+3)/2), case 1 < x < 2
  EXPECT_EQ(0.9624236501192069e0, acosh(1.5));
  // acosh(4) = log(sqrt(15)+4), case 2 < x < 2^28
  EXPECT_EQ(2.0634370688955608e0, acosh(4.0));
  // acosh(2^50), case 2^28 < x
  EXPECT_EQ(35.35050620855721e0, acosh(1125899906842624.0));
  // acosh(most-positive-float), no overflow
  EXPECT_EQ(710.4758600739439e0, acosh(1.7976931348623157e308));
}

TEST(Ieee754, Asin) {
  EXPECT_THAT(asin(kInfinity), IsNaN());
  EXPECT_THAT(asin(-kInfinity), IsNaN());
  EXPECT_THAT(asin(kQNaN), IsNaN());
  EXPECT_THAT(asin(kSNaN), IsNaN());

  EXPECT_THAT(asin(0.0), BitEq(0.0));
  EXPECT_THAT(asin(-0.0), BitEq(-0.0));
}

TEST(Ieee754, Asinh) {
  // Tests for asinh for exceptional values
  EXPECT_EQ(kInfinity, asinh(kInfinity));
  EXPECT_EQ(-kInfinity, asinh(-kInfinity));
  EXPECT_THAT(asin(kQNaN), IsNaN());
  EXPECT_THAT(asin(kSNaN), IsNaN());

  // Test basic asinh functionality
  EXPECT_THAT(asinh(0.0), BitEq(0.0));
  EXPECT_THAT(asinh(-0.0), BitEq(-0.0));
  // asinh(2^-29) = 2^-29, case |x| < 2^-28, where acosh(x) = x
  EXPECT_EQ(1.862645149230957e-9, asinh(1.862645149230957e-9));
  // asinh(-2^-29) = -2^-29, case |x| < 2^-28, where acosh(x) = x
  EXPECT_EQ(-1.862645149230957e-9, asinh(-1.862645149230957e-9));
  // asinh(2^-28), case 2 > |x| >= 2^-28
  EXPECT_EQ(3.725290298461914e-9, asinh(3.725290298461914e-9));
  // asinh(-2^-28), case 2 > |x| >= 2^-28
  EXPECT_EQ(-3.725290298461914e-9, asinh(-3.725290298461914e-9));
  // asinh(1), case 2 > |x| > 2^-28
  EXPECT_EQ(0.881373587019543e0, asinh(1.0));
  // asinh(-1), case 2 > |x| > 2^-28
  EXPECT_EQ(-0.881373587019543e0, asinh(-1.0));
  // asinh(5), case 2^28 > |x| > 2
  EXPECT_EQ(2.3124383412727525e0, asinh(5.0));
  // asinh(-5), case 2^28 > |x| > 2
  EXPECT_EQ(-2.3124383412727525e0, asinh(-5.0));
  // asinh(2^28), case 2^28 > |x|
  EXPECT_EQ(20.101268236238415e0, asinh(268435456.0));
  // asinh(-2^28), case 2^28 > |x|
  EXPECT_EQ(-20.101268236238415e0, asinh(-268435456.0));
  // asinh(<most-positive-float>), no overflow
  EXPECT_EQ(710.4758600739439e0, asinh(1.7976931348623157e308));
  // asinh(-<most-positive-float>), no overflow
  EXPECT_EQ(-710.4758600739439e0, asinh(-1.7976931348623157e308));
}

TEST(Ieee754, Atan) {
  EXPECT_THAT(atan(kQNaN), IsNaN());
  EXPECT_THAT(atan(kSNaN), IsNaN());
  EXPECT_THAT(atan(-0.0), BitEq(-0.0));
  EXPECT_THAT(atan(0.0), BitEq(0.0));
  EXPECT_DOUBLE_EQ(1.5707963267948966, atan(kInfinity));
  EXPECT_DOUBLE_EQ(-1.5707963267948966, atan(-kInfinity));
}

TEST(Ieee754, Atan2) {
  EXPECT_THAT(atan2(kQNaN, kQNaN), IsNaN());
  EXPECT_THAT(atan2(kQNaN, kSNaN), IsNaN());
  EXPECT_THAT(atan2(kSNaN, kQNaN), IsNaN());
  EXPECT_THAT(atan2(kSNaN, kSNaN), IsNaN());
  EXPECT_DOUBLE_EQ(0.7853981633974483, atan2(kInfinity, kInfinity));
  EXPECT_DOUBLE_EQ(2.356194490192345, atan2(kInfinity, -kInfinity));
  EXPECT_DOUBLE_EQ(-0.7853981633974483, atan2(-kInfinity, kInfinity));
  EXPECT_DOUBLE_EQ(-2.356194490192345, atan2(-kInfinity, -kInfinity));
}

TEST(Ieee754, Atanh) {
  EXPECT_THAT(atanh(kQNaN), IsNaN());
  EXPECT_THAT(atanh(kSNaN), IsNaN());
  EXPECT_THAT(atanh(kInfinity), IsNaN());
  EXPECT_EQ(kInfinity, atanh(1));
  EXPECT_EQ(-kInfinity, atanh(-1));
  EXPECT_DOUBLE_EQ(0.54930614433405478, atanh(0.5));
}

#if defined(V8_USE_LIBM_TRIG_FUNCTIONS)
TEST(Ieee754, LibmCos) {
  // Test values mentioned in the ECMAScript spec.
  EXPECT_THAT(libm_cos(kQNaN), IsNaN());
  EXPECT_THAT(libm_cos(kSNaN), IsNaN());
  EXPECT_THAT(libm_cos(kInfinity), IsNaN());
  EXPECT_THAT(libm_cos(-kInfinity), IsNaN());

  // Tests for cos for |x| < pi/4
  EXPECT_EQ(1.0, 1 / libm_cos(-0.0));
  EXPECT_EQ(1.0, 1 / libm_cos(0.0));
  // cos(x) = 1 for |x| < 2^-27
  EXPECT_EQ(1, libm_cos(2.3283064365386963e-10));
  EXPECT_EQ(1, libm_cos(-2.3283064365386963e-10));
  // Test KERNELCOS for |x| < 0.3.
  // cos(pi/20) = sqrt(sqrt(2)*sqrt(sqrt(5)+5)+4)/2^(3/2)
  EXPECT_EQ(0.9876883405951378, libm_cos(0.15707963267948966));
  // Test KERNELCOS for x ~= 0.78125
  EXPECT_EQ(0.7100335477927638, libm_cos(0.7812504768371582));
  EXPECT_EQ(0.7100338835660797, libm_cos(0.78125));
  // Test KERNELCOS for |x| > 0.3.
  // cos(pi/8) = sqrt(sqrt(2)+1)/2^(3/4)
  EXPECT_EQ(0.9238795325112867, libm_cos(0.39269908169872414));
  // Test KERNELTAN for |x| < 0.67434.
  EXPECT_EQ(0.9238795325112867, libm_cos(-0.39269908169872414));

  // Tests for cos.
  EXPECT_EQ(1, libm_cos(3.725290298461914e-9));
  // Cover different code paths in KERNELCOS.
  EXPECT_EQ(0.9689124217106447, libm_cos(0.25));
  EXPECT_EQ(0.8775825618903728, libm_cos(0.5));
  EXPECT_EQ(0.7073882691671998, libm_cos(0.785));
  // Test that cos(Math.PI/2) != 0 since Math.PI is not exact.
  EXPECT_EQ(6.123233995736766e-17, libm_cos(1.5707963267948966));
  // Test cos for various phases.
  EXPECT_EQ(0.7071067811865474, libm_cos(7.0 / 4 * kPI));
  EXPECT_EQ(0.7071067811865477, libm_cos(9.0 / 4 * kPI));
  EXPECT_EQ(-0.7071067811865467, libm_cos(11.0 / 4 * kPI));
  EXPECT_EQ(-0.7071067811865471, libm_cos(13.0 / 4 * kPI));
  EXPECT_EQ(0.9367521275331447, libm_cos(1000000.0));
  EXPECT_EQ(-3.435757038074824e-12, libm_cos(1048575.0 / 2 * kPI));

  // Test Hayne-Panek reduction.
  EXPECT_EQ(-0.9258790228548379e0, libm_cos(kTwo120));
  EXPECT_EQ(-0.9258790228548379e0, libm_cos(-kTwo120));
}

TEST(Ieee754, LibmSin) {
  // Test values mentioned in the ECMAScript spec.
  EXPECT_THAT(libm_sin(kQNaN), IsNaN());
  EXPECT_THAT(libm_sin(kSNaN), IsNaN());
  EXPECT_THAT(libm_sin(kInfinity), IsNaN());
  EXPECT_THAT(libm_sin(-kInfinity), IsNaN());

  // Tests for sin for |x| < pi/4
  EXPECT_EQ(-kInfinity, Divide(1.0, libm_sin(-0.0)));
  EXPECT_EQ(kInfinity, Divide(1.0, libm_sin(0.0)));
  // sin(x) = x for x < 2^-27
  EXPECT_EQ(2.3283064365386963e-10, libm_sin(2.3283064365386963e-10));
  EXPECT_EQ(-2.3283064365386963e-10, libm_sin(-2.3283064365386963e-10));
  // sin(pi/8) = sqrt(sqrt(2)-1)/2^(3/4)
  EXPECT_EQ(0.3826834323650898, libm_sin(0.39269908169872414));
  EXPECT_EQ(-0.3826834323650898, libm_sin(-0.39269908169872414));

  // Tests for sin.
  EXPECT_EQ(0.479425538604203, libm_sin(0.5));
  EXPECT_EQ(-0.479425538604203, libm_sin(-0.5));
  EXPECT_EQ(1, libm_sin(kPI / 2.0));
  EXPECT_EQ(-1, libm_sin(-kPI / 2.0));
  // Test that sin(Math.PI) != 0 since Math.PI is not exact.
  EXPECT_EQ(1.2246467991473532e-16, libm_sin(kPI));
  EXPECT_EQ(-7.047032979958965e-14, libm_sin(2200.0 * kPI));
  // Test sin for various phases.
  EXPECT_EQ(-0.7071067811865477, libm_sin(7.0 / 4.0 * kPI));
  EXPECT_EQ(0.7071067811865474, libm_sin(9.0 / 4.0 * kPI));
  EXPECT_EQ(0.7071067811865483, libm_sin(11.0 / 4.0 * kPI));
  EXPECT_EQ(-0.7071067811865479, libm_sin(13.0 / 4.0 * kPI));
  EXPECT_EQ(-3.2103381051568376e-11, libm_sin(1048576.0 / 4 * kPI));

  // Test Hayne-Panek reduction.
  EXPECT_EQ(0.377820109360752e0, libm_sin(kTwo120));
  EXPECT_EQ(-0.377820109360752e0, libm_sin(-kTwo120));
}

TEST(Ieee754, FdlibmCos) {
  // Test values mentioned in the ECMAScript spec.
  EXPECT_THAT(fdlibm_cos(kQNaN), IsNaN());
  EXPECT_THAT(fdlibm_cos(kSNaN), IsNaN());
  EXPECT_THAT(fdlibm_cos(kInfinity), IsNaN());
  EXPECT_THAT(fdlibm_cos(-kInfinity), IsNaN());

  // Tests for cos for |x| < pi/4
  EXPECT_EQ(1.0, 1 / fdlibm_cos(-0.0));
  EXPECT_EQ(1.0, 1 / fdlibm_cos(0.0));
  // cos(x) = 1 for |x| < 2^-27
  EXPECT_EQ(1, fdlibm_cos(2.3283064365386963e-10));
  EXPECT_EQ(1, fdlibm_cos(-2.3283064365386963e-10));
  // Test KERNELCOS for |x| < 0.3.
  // cos(pi/20) = sqrt(sqrt(2)*sqrt(sqrt(5)+5)+4)/2^(3/2)
  EXPECT_EQ(0.9876883405951378, fdlibm_cos(0.15707963267948966));
  // Test KERNELCOS for x ~= 0.78125
  EXPECT_EQ(0.7100335477927638, fdlibm_cos(0.7812504768371582));
  EXPECT_EQ(0.7100338835660797, fdlibm_cos(0.78125));
  // Test KERNELCOS for |x| > 0.3.
  // cos(pi/8) = sqrt(sqrt(2)+1)/2^(3/4)
  EXPECT_EQ(0.9238795325112867, fdlibm_cos(0.39269908169872414));
  // Test KERNELTAN for |x| < 0.67434.
  EXPECT_EQ(0.9238795325112867, fdlibm_cos(-0.39269908169872414));

  // Tests for cos.
  EXPECT_EQ(1, fdlibm_cos(3.725290298461914e-9));
  // Cover different code paths in KERNELCOS.
  EXPECT_EQ(0.9689124217106447, fdlibm_cos(0.25));
  EXPECT_EQ(0.8775825618903728, fdlibm_cos(0.5));
  EXPECT_EQ(0.7073882691671998, fdlibm_cos(0.785));
  // Test that cos(Math.PI/2) != 0 since Math.PI is not exact.
  EXPECT_EQ(6.123233995736766e-17, fdlibm_cos(1.5707963267948966));
  // Test cos for various phases.
  EXPECT_EQ(0.7071067811865474, fdlibm_cos(7.0 / 4 * kPI));
  EXPECT_EQ(0.7071067811865477, fdlibm_cos(9.0 / 4 * kPI));
  EXPECT_EQ(-0.7071067811865467, fdlibm_cos(11.0 / 4 * kPI));
  EXPECT_EQ(-0.7071067811865471, fdlibm_cos(13.0 / 4 * kPI));
  EXPECT_EQ(0.9367521275331447, fdlibm_cos(1000000.0));
  EXPECT_EQ(-3.435757038074824e-12, fdlibm_cos(1048575.0 / 2 * kPI));

  // Test Hayne-Panek reduction.
  EXPECT_EQ(-0.9258790228548379e0, fdlibm_cos(kTwo120));
  EXPECT_EQ(-0.9258790228548379e0, fdlibm_cos(-kTwo120));
}

TEST(Ieee754, FdlibmSin) {
  // Test values mentioned in the ECMAScript spec.
  EXPECT_THAT(fdlibm_sin(kQNaN), IsNaN());
  EXPECT_THAT(fdlibm_sin(kSNaN), IsNaN());
  EXPECT_THAT(fdlibm_sin(kInfinity), IsNaN());
  EXPECT_THAT(fdlibm_sin(-kInfinity), IsNaN());

  // Tests for sin for |x| < pi/4
  EXPECT_EQ(-kInfinity, Divide(1.0, fdlibm_sin(-0.0)));
  EXPECT_EQ(kInfinity, Divide(1.0, fdlibm_sin(0.0)));
  // sin(x) = x for x < 2^-27
  EXPECT_EQ(2.3283064365386963e-10, fdlibm_sin(2.3283064365386963e-10));
  EXPECT_EQ(-2.3283064365386963e-10, fdlibm_sin(-2.3283064365386963e-10));
  // sin(pi/8) = sqrt(sqrt(2)-1)/2^(3/4)
  EXPECT_EQ(0.3826834323650898, fdlibm_sin(0.39269908169872414));
  EXPECT_EQ(-0.3826834323650898, fdlibm_sin(-0.39269908169872414));

  // Tests for sin.
  EXPECT_EQ(0.479425538604203, fdlibm_sin(0.5));
  EXPECT_EQ(-0.479425538604203, fdlibm_sin(-0.5));
  EXPECT_EQ(1, fdlibm_sin(kPI / 2.0));
  EXPECT_EQ(-1, fdlibm_sin(-kPI / 2.0));
  // Test that sin(Math.PI) != 0 since Math.PI is not exact.
  EXPECT_EQ(1.2246467991473532e-16, fdlibm_sin(kPI));
  EXPECT_EQ(-7.047032979958965e-14, fdlibm_sin(2200.0 * kPI));
  // Test sin for various phases.
  EXPECT_EQ(-0.7071067811865477, fdlibm_sin(7.0 / 4.0 * kPI));
  EXPECT_EQ(0.7071067811865474, fdlibm_sin(9.0 / 4.0 * kPI));
  EXPECT_EQ(0.7071067811865483, fdlibm_sin(11.0 / 4.0 * kPI));
  EXPECT_EQ(-0.7071067811865479, fdlibm_sin(13.0 / 4.0 * kPI));
  EXPECT_EQ(-3.2103381051568376e-11, fdlibm_sin(1048576.0 / 4 * kPI));

  // Test Hayne-Panek reduction.
  EXPECT_EQ(0.377820109360752e0, fdlibm_sin(kTwo120));
  EXPECT_EQ(-0.377820109360752e0, fdlibm_sin(-kTwo120));
}

#else

TEST(Ieee754, Cos) {
  // Test values mentioned in the ECMAScript spec.
  EXPECT_THAT(cos(kQNaN), IsNaN());
  EXPECT_THAT(cos(kSNaN), IsNaN());
  EXPECT_THAT(cos(kInfinity), IsNaN());
  EXPECT_THAT(cos(-kInfinity), IsNaN());

  // Tests for cos for |x| < pi/4
  EXPECT_EQ(1.0, 1 / cos(-0.0));
  EXPECT_EQ(1.0, 1 / cos(0.0));
  // cos(x) = 1 for |x| < 2^-27
  EXPECT_EQ(1, cos(2.3283064365386963e-10));
  EXPECT_EQ(1, cos(-2.3283064365386963e-10));
  // Test KERNELCOS for |x| < 0.3.
  // cos(pi/20) = sqrt(sqrt(2)*sqrt(sqrt(5)+5)+4)/2^(3/2)
  EXPECT_EQ(0.9876883405951378, cos(0.15707963267948966));
  // Test KERNELCOS for x ~= 0.78125
  EXPECT_EQ(0.7100335477927638, cos(0.7812504768371582));
  EXPECT_EQ(0.7100338835660797, cos(0.78125));
  // Test KERNELCOS for |x| > 0.3.
  // cos(pi/8) = sqrt(sqrt(2)+1)/2^(3/4)
  EXPECT_EQ(0.9238795325112867, cos(0.39269908169872414));
  // Test KERNELTAN for |x| < 0.67434.
  EXPECT_EQ(0.9238795325112867, cos(-0.39269908169872414));

  // Tests for cos.
  EXPECT_EQ(1, cos(3.725290298461914e-9));
  // Cover different code paths in KERNELCOS.
  EXPECT_EQ(0.9689124217106447, cos(0.25));
  EXPECT_EQ(0.8775825618903728, cos(0.5));
  EXPECT_EQ(0.7073882691671998, cos(0.785));
  // Test that cos(Math.PI/2) != 0 since Math.PI is not exact.
  EXPECT_EQ(6.123233995736766e-17, cos(1.5707963267948966));
  // Test cos for various phases.
  EXPECT_EQ(0.7071067811865474, cos(7.0 / 4 * kPI));
  EXPECT_EQ(0.7071067811865477, cos(9.0 / 4 * kPI));
  EXPECT_EQ(-0.7071067811865467, cos(11.0 / 4 * kPI));
  EXPECT_EQ(-0.7071067811865471, cos(13.0 / 4 * kPI));
  EXPECT_EQ(0.9367521275331447, cos(1000000.0));
  EXPECT_EQ(-3.435757038074824e-12, cos(1048575.0 / 2 * kPI));

  // Test Hayne-Panek reduction.
  EXPECT_EQ(-0.9258790228548379e0, cos(kTwo120));
  EXPECT_EQ(-0.9258790228548379e0, cos(-kTwo120));
}

TEST(Ieee754, Sin) {
  // Test values mentioned in the ECMAScript spec.
  EXPECT_THAT(sin(kQNaN), IsNaN());
  EXPECT_THAT(sin(kSNaN), IsNaN());
  EXPECT_THAT(sin(kInfinity), IsNaN());
  EXPECT_THAT(sin(-kInfinity), IsNaN());

  // Tests for sin for |x| < pi/4
  EXPECT_EQ(-kInfinity, Divide(1.0, sin(-0.0)));
  EXPECT_EQ(kInfinity, Divide(1.0, sin(0.0)));
  // sin(x) = x for x < 2^-27
  EXPECT_EQ(2.3283064365386963e-10, sin(2.3283064365386963e-10));
  EXPECT_EQ(-2.3283064365386963e-10, sin(-2.3283064365386963e-10));
  // sin(pi/8) = sqrt(sqrt(2)-1)/2^(3/4)
  EXPECT_EQ(0.3826834323650898, sin(0.39269908169872414));
  EXPECT_EQ(-0.3826834323650898, sin(-0.39269908169872414));

  // Tests for sin.
  EXPECT_EQ(0.479425538604203, sin(0.5));
  EXPECT_EQ(-0.479425538604203, sin(-0.5));
  EXPECT_EQ(1, sin(kPI / 2.0));
  EXPECT_EQ(-1, sin(-kPI / 2.0));
  // Test that sin(Math.PI) != 0 since Math.PI is not exact.
  EXPECT_EQ(1.2246467991473532e-16, sin(kPI));
  EXPECT_EQ(-7.047032979958965e-14, sin(2200.0 * kPI));
  // Test sin for various phases.
  EXPECT_EQ(-0.7071067811865477, sin(7.0 / 4.0 * kPI));
  EXPECT_EQ(0.7071067811865474, sin(9.0 / 4.0 * kPI));
  EXPECT_EQ(0.7071067811865483, sin(11.0 / 4.0 * kPI));
  EXPECT_EQ(-0.7071067811865479, sin(13.0 / 4.0 * kPI));
  EXPECT_EQ(-3.2103381051568376e-11, sin(1048576.0 / 4 * kPI));

  // Test Hayne-Panek reduction.
  EXPECT_EQ(0.377820109360752e0, sin(kTwo120));
  EXPECT_EQ(-0.377820109360752e0, sin(-kTwo120));
}

#endif

TEST(Ieee754, Cosh) {
  // Test values mentioned in the ECMAScript spec.
  EXPECT_THAT(cosh(kQNaN), IsNaN());
  EXPECT_THAT(cosh(kSNaN), IsNaN());
  EXPECT_THAT(cosh(kInfinity), kInfinity);
  EXPECT_THAT(cosh(-kInfinity), kInfinity);
  EXPECT_EQ(1, cosh(0.0));
  EXPECT_EQ(1, cosh(-0.0));
}

TEST(Ieee754, Exp) {
  EXPECT_THAT(exp(kQNaN), IsNaN());
  EXPECT_THAT(exp(kSNaN), IsNaN());
  EXPECT_EQ(0.0, exp(-kInfinity));
  EXPECT_EQ(0.0, exp(-1000));
  EXPECT_EQ(0.0, exp(-745.1332191019412));
  EXPECT_EQ(2.2250738585072626e-308, exp(-708.39641853226408));
  EXPECT_EQ(3.307553003638408e-308, exp(-708.0));
  EXPECT_EQ(4.9406564584124654e-324, exp(-7.45133219101941108420e+02));
  EXPECT_EQ(0.36787944117144233, exp(-1.0));
  EXPECT_EQ(1.0, exp(-0.0));
  EXPECT_EQ(1.0, exp(0.0));
  EXPECT_EQ(1.0, exp(2.2250738585072014e-308));

  // Test that exp(x) is monotonic near 1.
  EXPECT_GE(exp(1.0), exp(0.9999999999999999));
  EXPECT_LE(exp(1.0), exp(1.0000000000000002));

  // Test that we produce the correctly rounded result for 1.
  EXPECT_EQ(kE, exp(1.0));

  EXPECT_EQ(7.38905609893065e0, exp(2.0));
  EXPECT_EQ(1.7976931348622732e308, exp(7.09782712893383973096e+02));
  EXPECT_EQ(2.6881171418161356e+43, exp(100.0));
  EXPECT_EQ(8.218407461554972e+307, exp(709.0));
  EXPECT_EQ(1.7968190737295725e308, exp(709.7822265625e0));
  EXPECT_EQ(kInfinity, exp(709.7827128933841e0));
  EXPECT_EQ(kInfinity, exp(710.0));
  EXPECT_EQ(kInfinity, exp(1000.0));
  EXPECT_EQ(kInfinity, exp(kInfinity));
}

TEST(Ieee754, Expm1) {
  EXPECT_THAT(expm1(kQNaN), IsNaN());
  EXPECT_THAT(expm1(kSNaN), IsNaN());
  EXPECT_EQ(-1.0, expm1(-kInfinity));
  EXPECT_EQ(kInfinity, expm1(kInfinity));
  EXPECT_EQ(0.0, expm1(-0.0));
  EXPECT_EQ(0.0, expm1(0.0));
  EXPECT_EQ(1.718281828459045, expm1(1.0));
  EXPECT_EQ(2.6881171418161356e+43, expm1(100.0));
  EXPECT_EQ(8.218407461554972e+307, expm1(709.0));
  EXPECT_EQ(kInfinity, expm1(710.0));
}

TEST(Ieee754, Log) {
  EXPECT_THAT(log(kQNaN), IsNaN());
  EXPECT_THAT(log(kSNaN), IsNaN());
  EXPECT_THAT(log(-kInfinity), IsNaN());
  EXPECT_THAT(log(-1.0), IsNaN());
  EXPECT_EQ(-kInfinity, log(-0.0));
  EXPECT_EQ(-kInfinity, log(0.0));
  EXPECT_EQ(0.0, log(1.0));
  EXPECT_EQ(kInfinity, log(kInfinity));

  // Test that log(E) produces the correctly rounded result.
  EXPECT_EQ(1.0, log(kE));
}

TEST(Ieee754, Log1p) {
  EXPECT_THAT(log1p(kQNaN), IsNaN());
  EXPECT_THAT(log1p(kSNaN), IsNaN());
  EXPECT_THAT(log1p(-kInfinity), IsNaN());
  EXPECT_EQ(-kInfinity, log1p(-1.0));
  EXPECT_EQ(0.0, log1p(0.0));
  EXPECT_EQ(-0.0, log1p(-0.0));
  EXPECT_EQ(kInfinity, log1p(kInfinity));
  EXPECT_EQ(6.9756137364252422e-03, log1p(0.007));
  EXPECT_EQ(709.782712893384, log1p(1.7976931348623157e308));
  EXPECT_EQ(2.7755575615628914e-17, log1p(2.7755575615628914e-17));
  EXPECT_EQ(9.313225741817976e-10, log1p(9.313225746154785e-10));
  EXPECT_EQ(-0.2876820724517809, log1p(-0.25));
  EXPECT_EQ(0.22314355131420976, log1p(0.25));
  EXPECT_EQ(2.3978952727983707, log1p(10));
  EXPECT_EQ(36.841361487904734, log1p(10e15));
  EXPECT_EQ(37.08337388996168, log1p(12738099905822720));
  EXPECT_EQ(37.08336444902049, log1p(12737979646738432));
  EXPECT_EQ(1.3862943611198906, log1p(3));
  EXPECT_EQ(1.3862945995384413, log1p(3 + 9.5367431640625e-7));
  EXPECT_EQ(0.5596157879354227, log1p(0.75));
  EXPECT_EQ(0.8109302162163288, log1p(1.25));
}

TEST(Ieee754, Log2) {
  EXPECT_THAT(log2(kQNaN), IsNaN());
  EXPECT_THAT(log2(kSNaN), IsNaN());
  EXPECT_THAT(log2(-kInfinity), IsNaN());
  EXPECT_THAT(log2(-1.0), IsNaN());
  EXPECT_EQ(-kInfinity, log2(0.0));
  EXPECT_EQ(-kInfinity, log2(-0.0));
  EXPECT_EQ(kInfinity, log2(kInfinity));
}

TEST(Ieee754, Log10) {
  EXPECT_THAT(log10(kQNaN), IsNaN());
  EXPECT_THAT(log10(kSNaN), IsNaN());
  EXPECT_THAT(log10(-kInfinity), IsNaN());
  EXPECT_THAT(log10(-1.0), IsNaN());
  EXPECT_EQ(-kInfinity, log10(0.0));
  EXPECT_EQ(-kInfinity, log10(-0.0));
  EXPECT_EQ(kInfinity, log10(kInfinity));
  EXPECT_EQ(3.0, log10(1000.0));
  EXPECT_EQ(14.0, log10(100000000000000));  // log10(10 ^ 14)
  EXPECT_EQ(3.7389561269540406, log10(5482.2158));
  EXPECT_EQ(14.661551142893833, log10(458723662312872.125782332587));
  EXPECT_EQ(-0.9083828622192334, log10(0.12348583358871));
  EXPECT_EQ(5.0, log10(100000.0));
}

TEST(Ieee754, Cbrt) {
  EXPECT_THAT(cbrt(kQNaN), IsNaN());
  EXPECT_THAT(cbrt(kSNaN), IsNaN());
  EXPECT_EQ(kInfinity, cbrt(kInfinity));
  EXPECT_EQ(-kInfinity, cbrt(-kInfinity));
  EXPECT_EQ(1.4422495703074083, cbrt(3));
  EXPECT_EQ(100, cbrt(100 * 100 * 100));
  EXPECT_EQ(46.415888336127786, cbrt(100000));
}

TEST(Ieee754, Sinh) {
  // Test values mentioned in the ECMAScript spec.
  EXPECT_THAT(sinh(kQNaN), IsNaN());
  EXPECT_THAT(sinh(kSNaN), IsNaN());
  EXPECT_THAT(sinh(kInfinity), kInfinity);
  EXPECT_THAT(sinh(-kInfinity), -kInfinity);
  EXPECT_EQ(0.0, sinh(0.0));
  EXPECT_EQ(-0.0, sinh(-0.0));
}

TEST(Ieee754, Tan) {
  // Test values mentioned in the ECMAScript spec.
  EXPECT_THAT(tan(kQNaN), IsNaN());
  EXPECT_THAT(tan(kSNaN), IsNaN());
  EXPECT_THAT(tan(kInfinity), IsNaN());
  EXPECT_THAT(tan(-kInfinity), IsNaN());

  // Tests for tan for |x| < pi/4
  EXPECT_EQ(kInfinity, Divide(1.0, tan(0.0)));
  EXPECT_EQ(-kInfinity, Divide(1.0, tan(-0.0)));
  // tan(x) = x for |x| < 2^-28
  EXPECT_EQ(2.3283064365386963e-10, tan(2.3283064365386963e-10));
  EXPECT_EQ(-2.3283064365386963e-10, tan(-2.3283064365386963e-10));
  // Test KERNELTAN for |x| > 0.67434.
  EXPECT_EQ(0.8211418015898941, tan(11.0 / 16.0));
  EXPECT_EQ(-0.8211418015898941, tan(-11.0 / 16.0));
  EXPECT_EQ(0.41421356237309503, tan(0.39269908169872414));
  // crbug/427468
  EXPECT_EQ(0.7993357819992383, tan(0.6743358));

  // Tests for tan.
  EXPECT_EQ(3.725290298461914e-9, tan(3.725290298461914e-9));
  // Test that tan(PI/2) != Infinity since PI is not exact.
  EXPECT_EQ(1.633123935319537e16, tan(kPI / 2));
  // Cover different code paths in KERNELTAN (tangent and cotangent)
  EXPECT_EQ(0.5463024898437905, tan(0.5));
  EXPECT_EQ(2.0000000000000027, tan(1.107148717794091));
  EXPECT_EQ(-1.0000000000000004, tan(7.0 / 4.0 * kPI));
  EXPECT_EQ(0.9999999999999994, tan(9.0 / 4.0 * kPI));
  EXPECT_EQ(-6.420676210313675e-11, tan(1048576.0 / 2.0 * kPI));
  EXPECT_EQ(2.910566692924059e11, tan(1048575.0 / 2.0 * kPI));

  // Test Hayne-Panek reduction.
  EXPECT_EQ(-0.40806638884180424e0, tan(kTwo120));
  EXPECT_EQ(0.40806638884180424e0, tan(-kTwo120));
}

TEST(Ieee754, Tanh) {
  // Test values mentioned in the ECMAScript spec.
  EXPECT_THAT(tanh(kQNaN), IsNaN());
  EXPECT_THAT(tanh(kSNaN), IsNaN());
  EXPECT_THAT(tanh(kInfinity), 1);
  EXPECT_THAT(tanh(-kInfinity), -1);
  EXPECT_EQ(0.0, tanh(0.0));
  EXPECT_EQ(-0.0, tanh(-0.0));
}

}  // namespace ieee754
}  // namespace base
}  // namespace v8
