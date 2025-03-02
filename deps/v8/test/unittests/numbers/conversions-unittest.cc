// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/numbers/conversions.h"

#include <stdlib.h>

#include "src/base/platform/platform.h"
#include "src/base/vector.h"
#include "src/execution/isolate.h"
#include "src/heap/factory-inl.h"
#include "src/init/v8.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/objects.h"
#include "src/objects/smi.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {
namespace interpreter {

class ConversionsTest : public TestWithIsolate {
 public:
  ConversionsTest() = default;
  ~ConversionsTest() override = default;

  SourcePosition toPos(int offset) {
    return SourcePosition(offset, offset % 10 - 1);
  }

  void CheckNonArrayIndex(bool expected, const char* chars) {
    auto isolate = i_isolate();
    auto string = isolate->factory()->NewStringFromAsciiChecked(chars);
    CHECK_EQ(expected, IsSpecialIndex(*string));
  }
};

TEST_F(ConversionsTest, Hex) {
  CHECK_EQ(0.0, StringToDouble("0x0", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(0.0, StringToDouble("0X0", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(1.0, StringToDouble("0x1", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(16.0, StringToDouble("0x10", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(255.0, StringToDouble("0xFF", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(175.0, StringToDouble("0xAF", ALLOW_NON_DECIMAL_PREFIX));

  CHECK_EQ(0.0, HexStringToDouble(base::OneByteVector("0x0")));
  CHECK_EQ(0.0, HexStringToDouble(base::OneByteVector("0X0")));
  CHECK_EQ(1.0, HexStringToDouble(base::OneByteVector("0x1")));
  CHECK_EQ(16.0, HexStringToDouble(base::OneByteVector("0x10")));
  CHECK_EQ(255.0, HexStringToDouble(base::OneByteVector("0xFF")));
  CHECK_EQ(175.0, HexStringToDouble(base::OneByteVector("0xAF")));
}

TEST_F(ConversionsTest, Octal) {
  CHECK_EQ(0.0, StringToDouble("0o0", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(0.0, StringToDouble("0O0", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(1.0, StringToDouble("0o1", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(7.0, StringToDouble("0o7", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(8.0, StringToDouble("0o10", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(63.0, StringToDouble("0o77", ALLOW_NON_DECIMAL_PREFIX));

  CHECK_EQ(0.0, OctalStringToDouble(base::OneByteVector("0o0")));
  CHECK_EQ(0.0, OctalStringToDouble(base::OneByteVector("0O0")));
  CHECK_EQ(1.0, OctalStringToDouble(base::OneByteVector("0o1")));
  CHECK_EQ(7.0, OctalStringToDouble(base::OneByteVector("0o7")));
  CHECK_EQ(8.0, OctalStringToDouble(base::OneByteVector("0o10")));
  CHECK_EQ(63.0, OctalStringToDouble(base::OneByteVector("0o77")));

  const double x = 010000000000;  // Power of 2, no rounding errors.
  CHECK_EQ(x * x * x * x * x,
           OctalStringToDouble(base::OneByteVector("0o01"
                                                   "0000000000"
                                                   "0000000000"
                                                   "0000000000"
                                                   "0000000000"
                                                   "0000000000")));
}

TEST_F(ConversionsTest, ImplicitOctal) {
  CHECK_EQ(0.0, ImplicitOctalStringToDouble(base::OneByteVector("0")));
  CHECK_EQ(0.0, ImplicitOctalStringToDouble(base::OneByteVector("00")));
  CHECK_EQ(1.0, ImplicitOctalStringToDouble(base::OneByteVector("01")));
  CHECK_EQ(7.0, ImplicitOctalStringToDouble(base::OneByteVector("07")));
  CHECK_EQ(8.0, ImplicitOctalStringToDouble(base::OneByteVector("010")));
  CHECK_EQ(63.0, ImplicitOctalStringToDouble(base::OneByteVector("077")));

  CHECK_EQ(0.0, StringToDouble("0", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(0.0, StringToDouble("00", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(1.0, StringToDouble("01", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(7.0, StringToDouble("07", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(10.0, StringToDouble("010", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(77.0, StringToDouble("077", ALLOW_NON_DECIMAL_PREFIX));

  const double x = 010000000000;  // Power of 2, no rounding errors.
  CHECK_EQ(x * x * x * x * x,
           ImplicitOctalStringToDouble(base::OneByteVector("01"
                                                           "0000000000"
                                                           "0000000000"
                                                           "0000000000"
                                                           "0000000000"
                                                           "0000000000")));
}

TEST_F(ConversionsTest, Binary) {
  CHECK_EQ(0.0, StringToDouble("0b0", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(0.0, StringToDouble("0B0", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(1.0, StringToDouble("0b1", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(2.0, StringToDouble("0b10", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(3.0, StringToDouble("0b11", ALLOW_NON_DECIMAL_PREFIX));

  CHECK_EQ(0.0, BinaryStringToDouble(base::OneByteVector("0b0")));
  CHECK_EQ(0.0, BinaryStringToDouble(base::OneByteVector("0B0")));
  CHECK_EQ(1.0, BinaryStringToDouble(base::OneByteVector("0b1")));
  CHECK_EQ(2.0, BinaryStringToDouble(base::OneByteVector("0b10")));
  CHECK_EQ(3.0, BinaryStringToDouble(base::OneByteVector("0b11")));
}

TEST_F(ConversionsTest, MalformedOctal) {
  CHECK_EQ(8.0, StringToDouble("08", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(81.0, StringToDouble("081", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(78.0, StringToDouble("078", ALLOW_NON_DECIMAL_PREFIX));

  CHECK_EQ(7.7, StringToDouble("07.7", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(7.8, StringToDouble("07.8", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(7e8, StringToDouble("07e8", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(7e7, StringToDouble("07e7", ALLOW_NON_DECIMAL_PREFIX));

  CHECK_EQ(8.7, StringToDouble("08.7", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(8e7, StringToDouble("08e7", ALLOW_NON_DECIMAL_PREFIX));

  CHECK_EQ(0.001, StringToDouble("0.001", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(0.713, StringToDouble("0.713", ALLOW_NON_DECIMAL_PREFIX));
}

TEST_F(ConversionsTest, TrailingJunk) {
  CHECK_EQ(8.0, StringToDouble("8q", ALLOW_TRAILING_JUNK));
  CHECK_EQ(10.0, StringToDouble("10e", ALLOW_TRAILING_JUNK));
  CHECK_EQ(10.0, StringToDouble("10e-", ALLOW_TRAILING_JUNK));
}

TEST_F(ConversionsTest, NonStrDecimalLiteral) {
  CHECK(std::isnan(StringToDouble(" ", NO_CONVERSION_FLAG,
                                  std::numeric_limits<double>::quiet_NaN())));
  CHECK(std::isnan(StringToDouble("", NO_CONVERSION_FLAG,
                                  std::numeric_limits<double>::quiet_NaN())));
  CHECK(std::isnan(StringToDouble(" ", NO_CONVERSION_FLAG,
                                  std::numeric_limits<double>::quiet_NaN())));
  CHECK_EQ(0.0, StringToDouble("", NO_CONVERSION_FLAG));
  CHECK_EQ(0.0, StringToDouble(" ", NO_CONVERSION_FLAG));
}

TEST_F(ConversionsTest, IntegerStrLiteral) {
  CHECK_EQ(0.0, StringToDouble("0.0", NO_CONVERSION_FLAG));
  CHECK_EQ(0.0, StringToDouble("0", NO_CONVERSION_FLAG));
  CHECK_EQ(0.0, StringToDouble("00", NO_CONVERSION_FLAG));
  CHECK_EQ(0.0, StringToDouble("000", NO_CONVERSION_FLAG));
  CHECK_EQ(1.0, StringToDouble("1", NO_CONVERSION_FLAG));
  CHECK_EQ(-1.0, StringToDouble("-1", NO_CONVERSION_FLAG));
  CHECK_EQ(-1.0, StringToDouble("  -1  ", NO_CONVERSION_FLAG));
  CHECK_EQ(1.0, StringToDouble("  +1  ", NO_CONVERSION_FLAG));
  CHECK(std::isnan(StringToDouble("  -  1  ", NO_CONVERSION_FLAG)));
  CHECK(std::isnan(StringToDouble("  +  1  ", NO_CONVERSION_FLAG)));

  CHECK_EQ(0.0, StringToDouble("0e0", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(0.0, StringToDouble("0e1", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(0.0, StringToDouble("0e-1", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(0.0, StringToDouble("0e-100000", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(0.0, StringToDouble("0e+100000", ALLOW_NON_DECIMAL_PREFIX));
  CHECK_EQ(0.0, StringToDouble("0.", ALLOW_NON_DECIMAL_PREFIX));
}

TEST_F(ConversionsTest, LongNumberStr) {
  CHECK_EQ(1e10, StringToDouble("1"
                                "0000000000",
                                NO_CONVERSION_FLAG));
  CHECK_EQ(1e20, StringToDouble("1"
                                "0000000000"
                                "0000000000",
                                NO_CONVERSION_FLAG));

  CHECK_EQ(1e60, StringToDouble("1"
                                "0000000000"
                                "0000000000"
                                "0000000000"
                                "0000000000"
                                "0000000000"
                                "0000000000",
                                NO_CONVERSION_FLAG));

  CHECK_EQ(1e-2, StringToDouble("."
                                "0"
                                "1",
                                NO_CONVERSION_FLAG));
  CHECK_EQ(1e-11, StringToDouble("."
                                 "0000000000"
                                 "1",
                                 NO_CONVERSION_FLAG));
  CHECK_EQ(1e-21, StringToDouble("."
                                 "0000000000"
                                 "0000000000"
                                 "1",
                                 NO_CONVERSION_FLAG));

  CHECK_EQ(1e-61, StringToDouble("."
                                 "0000000000"
                                 "0000000000"
                                 "0000000000"
                                 "0000000000"
                                 "0000000000"
                                 "0000000000"
                                 "1",
                                 NO_CONVERSION_FLAG));

  // x = 24414062505131248.0 and y = 24414062505131252.0 are representable in
  // double. Check chat z = (x + y) / 2 is rounded to x...
  CHECK_EQ(24414062505131248.0,
           StringToDouble("24414062505131250.0", NO_CONVERSION_FLAG));

  // ... and z = (x + y) / 2 + delta is rounded to y.
  CHECK_EQ(24414062505131252.0,
           StringToDouble("24414062505131250.000000001", NO_CONVERSION_FLAG));
}

TEST_F(ConversionsTest, MaximumSignificantDigits) {
  char num[] =
      "4.4501477170144020250819966727949918635852426585926051135169509"
      "122872622312493126406953054127118942431783801370080830523154578"
      "251545303238277269592368457430440993619708911874715081505094180"
      "604803751173783204118519353387964161152051487413083163272520124"
      "606023105869053620631175265621765214646643181420505164043632222"
      "668006474326056011713528291579642227455489682133472873831754840"
      "341397809846934151055619529382191981473003234105366170879223151"
      "087335413188049110555339027884856781219017754500629806224571029"
      "581637117459456877330110324211689177656713705497387108207822477"
      "584250967061891687062782163335299376138075114200886249979505279"
      "101870966346394401564490729731565935244123171539810221213221201"
      "847003580761626016356864581135848683152156368691976240370422601"
      "6998291015625000000000000000000000000000000000e-308";

  CHECK_EQ(4.4501477170144017780491e-308,
           StringToDouble(num, NO_CONVERSION_FLAG));

  // Changes the result of strtod (at least in glibc implementation).
  num[sizeof(num) - 8] = '1';

  CHECK_EQ(4.4501477170144022721148e-308,
           StringToDouble(num, NO_CONVERSION_FLAG));
}

TEST_F(ConversionsTest, MinimumExponent) {
  // Same test but with different point-position.
  char num[] =
      "445014771701440202508199667279499186358524265859260511351695091"
      "228726223124931264069530541271189424317838013700808305231545782"
      "515453032382772695923684574304409936197089118747150815050941806"
      "048037511737832041185193533879641611520514874130831632725201246"
      "060231058690536206311752656217652146466431814205051640436322226"
      "680064743260560117135282915796422274554896821334728738317548403"
      "413978098469341510556195293821919814730032341053661708792231510"
      "873354131880491105553390278848567812190177545006298062245710295"
      "816371174594568773301103242116891776567137054973871082078224775"
      "842509670618916870627821633352993761380751142008862499795052791"
      "018709663463944015644907297315659352441231715398102212132212018"
      "470035807616260163568645811358486831521563686919762403704226016"
      "998291015625000000000000000000000000000000000e-1108";

  CHECK_EQ(4.4501477170144017780491e-308,
           StringToDouble(num, NO_CONVERSION_FLAG));

  // Changes the result of strtod (at least in glibc implementation).
  num[sizeof(num) - 8] = '1';

  CHECK_EQ(4.4501477170144022721148e-308,
           StringToDouble(num, NO_CONVERSION_FLAG));
}

TEST_F(ConversionsTest, MaximumExponent) {
  char num[] = "0.16e309";

  CHECK_EQ(1.59999999999999997765e+308,
           StringToDouble(num, NO_CONVERSION_FLAG));
}

TEST_F(ConversionsTest, ExponentNumberStr) {
  CHECK_EQ(1e1, StringToDouble("1e1", NO_CONVERSION_FLAG));
  CHECK_EQ(1e1, StringToDouble("1e+1", NO_CONVERSION_FLAG));
  CHECK_EQ(1e-1, StringToDouble("1e-1", NO_CONVERSION_FLAG));
  CHECK_EQ(1e100, StringToDouble("1e+100", NO_CONVERSION_FLAG));
  CHECK_EQ(1e-100, StringToDouble("1e-100", NO_CONVERSION_FLAG));
  CHECK_EQ(1e-106, StringToDouble(".000001e-100", NO_CONVERSION_FLAG));
}

using OneBit1 = base::BitField<uint32_t, 0, 1>;
using OneBit2 = base::BitField<uint32_t, 7, 1>;
using EightBit1 = base::BitField<uint32_t, 0, 8>;
using EightBit2 = base::BitField<uint32_t, 13, 8>;

TEST_F(ConversionsTest, BitField) {
  uint32_t x;

  // One bit bit field can hold values 0 and 1.
  CHECK(!OneBit1::is_valid(static_cast<uint32_t>(-1)));
  CHECK(!OneBit2::is_valid(static_cast<uint32_t>(-1)));
  for (unsigned i = 0; i < 2; i++) {
    CHECK(OneBit1::is_valid(i));
    x = OneBit1::encode(i);
    CHECK_EQ(i, OneBit1::decode(x));

    CHECK(OneBit2::is_valid(i));
    x = OneBit2::encode(i);
    CHECK_EQ(i, OneBit2::decode(x));
  }
  CHECK(!OneBit1::is_valid(2));
  CHECK(!OneBit2::is_valid(2));

  // Eight bit bit field can hold values from 0 tp 255.
  CHECK(!EightBit1::is_valid(static_cast<uint32_t>(-1)));
  CHECK(!EightBit2::is_valid(static_cast<uint32_t>(-1)));
  for (unsigned i = 0; i < 256; i++) {
    CHECK(EightBit1::is_valid(i));
    x = EightBit1::encode(i);
    CHECK_EQ(i, EightBit1::decode(x));
    CHECK(EightBit2::is_valid(i));
    x = EightBit2::encode(i);
    CHECK_EQ(i, EightBit2::decode(x));
  }
  CHECK(!EightBit1::is_valid(256));
  CHECK(!EightBit2::is_valid(256));
}

using UpperBits = base::BitField64<int, 61, 3>;
using MiddleBits = base::BitField64<int, 31, 2>;

TEST_F(ConversionsTest, BitField64) {
  uint64_t x;

  // Test most significant bits.
  x = 0xE000'0000'0000'0000;
  CHECK(x == UpperBits::encode(7));
  CHECK_EQ(7, UpperBits::decode(x));

  // Test the 32/64-bit boundary bits.
  x = 0x0000'0001'8000'0000;
  CHECK(x == MiddleBits::encode(3));
  CHECK_EQ(3, MiddleBits::decode(x));
}

TEST_F(ConversionsTest, SpecialIndexParsing) {
  HandleScope scope(i_isolate());
  CheckNonArrayIndex(false, "");
  CheckNonArrayIndex(false, "-");
  CheckNonArrayIndex(true, "0");
  CheckNonArrayIndex(true, "-0");
  CheckNonArrayIndex(false, "01");
  CheckNonArrayIndex(false, "-01");
  CheckNonArrayIndex(true, "0.5");
  CheckNonArrayIndex(true, "-0.5");
  CheckNonArrayIndex(true, "1");
  CheckNonArrayIndex(true, "-1");
  CheckNonArrayIndex(true, "10");
  CheckNonArrayIndex(true, "-10");
  CheckNonArrayIndex(true, "NaN");
  CheckNonArrayIndex(true, "Infinity");
  CheckNonArrayIndex(true, "-Infinity");
  CheckNonArrayIndex(true, "4294967295");
  CheckNonArrayIndex(true, "429496.7295");
  CheckNonArrayIndex(true, "1.3333333333333333");
  CheckNonArrayIndex(false, "1.3333333333333339");
  CheckNonArrayIndex(true, "1.333333333333331e+222");
  CheckNonArrayIndex(true, "-1.3333333333333211e+222");
  CheckNonArrayIndex(false, "-1.3333333333333311e+222");
  CheckNonArrayIndex(true, "429496.7295");
  CheckNonArrayIndex(false, "43s3");
  CheckNonArrayIndex(true, "4294967296");
  CheckNonArrayIndex(true, "-4294967296");
  CheckNonArrayIndex(true, "999999999999999");
  CheckNonArrayIndex(false, "9999999999999999");
  CheckNonArrayIndex(true, "-999999999999999");
  CheckNonArrayIndex(false, "-9999999999999999");
  CheckNonArrayIndex(false, "42949672964294967296429496729694966");
}

TEST_F(ConversionsTest, NoHandlesForTryNumberToSize) {
  size_t result = 0;
  {
    SealHandleScope no_handles(i_isolate());
    Tagged<Smi> smi = Smi::FromInt(1);
    CHECK(TryNumberToSize(smi, &result));
    CHECK_EQ(result, 1u);
  }
  result = 0;
  {
    HandleScope scope(i_isolate());
    DirectHandle<HeapNumber> heap_number1 =
        i_isolate()->factory()->NewHeapNumber(2.0);
    {
      SealHandleScope no_handles(i_isolate());
      CHECK(TryNumberToSize(*heap_number1, &result));
      CHECK_EQ(result, 2u);
    }
    DirectHandle<HeapNumber> heap_number2 =
        i_isolate()->factory()->NewHeapNumber(
            static_cast<double>(std::numeric_limits<size_t>::max()) + 10000.0);
    {
      SealHandleScope no_handles(i_isolate());
      CHECK(!TryNumberToSize(*heap_number2, &result));
    }
  }
}

TEST_F(ConversionsTest, TryNumberToSizeWithMaxSizePlusOne) {
  {
    HandleScope scope(i_isolate());
    // 1 << 64, larger than the limit of size_t.
    double value = 18446744073709551616.0;
    size_t result = 0;
    DirectHandle<HeapNumber> heap_number =
        i_isolate()->factory()->NewHeapNumber(value);
    CHECK(!TryNumberToSize(*heap_number, &result));
  }
}

TEST_F(ConversionsTest, PositiveNumberToUint32) {
  i::Factory* factory = i_isolate()->factory();
  uint32_t max = std::numeric_limits<uint32_t>::max();
  HandleScope scope(i_isolate());
  // Test Smi conversions.
  DirectHandle<Object> number(Smi::FromInt(0), i_isolate());
  CHECK_EQ(PositiveNumberToUint32(*number), 0u);
  number = direct_handle(Smi::FromInt(-1), i_isolate());
  CHECK_EQ(PositiveNumberToUint32(*number), 0u);
  number = direct_handle(Smi::FromInt(-1), i_isolate());
  CHECK_EQ(PositiveNumberToUint32(*number), 0u);
  number = direct_handle(Smi::FromInt(Smi::kMinValue), i_isolate());
  CHECK_EQ(PositiveNumberToUint32(*number), 0u);
  number = direct_handle(Smi::FromInt(Smi::kMaxValue), i_isolate());
  CHECK_EQ(PositiveNumberToUint32(*number),
           static_cast<uint32_t>(Smi::kMaxValue));
  // Test Double conversions.
  number = factory->NewHeapNumber(0.0);
  CHECK_EQ(PositiveNumberToUint32(*number), 0u);
  number = factory->NewHeapNumber(0.999);
  CHECK_EQ(PositiveNumberToUint32(*number), 0u);
  number = factory->NewHeapNumber(1.999);
  CHECK_EQ(PositiveNumberToUint32(*number), 1u);
  number = factory->NewHeapNumber(-12.0);
  CHECK_EQ(PositiveNumberToUint32(*number), 0u);
  number = factory->NewHeapNumber(12000.0);
  CHECK_EQ(PositiveNumberToUint32(*number), 12000u);
  number = factory->NewHeapNumber(static_cast<double>(Smi::kMaxValue) + 1);
  CHECK_EQ(PositiveNumberToUint32(*number),
           static_cast<uint32_t>(Smi::kMaxValue) + 1);
  number = factory->NewHeapNumber(max);
  CHECK_EQ(PositiveNumberToUint32(*number), max);
  number = factory->NewHeapNumber(static_cast<double>(max) * 1000);
  CHECK_EQ(PositiveNumberToUint32(*number), max);
  number = factory->NewHeapNumber(std::numeric_limits<double>::max());
  CHECK_EQ(PositiveNumberToUint32(*number), max);
  number = factory->NewHeapNumber(std::numeric_limits<double>::infinity());
  CHECK_EQ(PositiveNumberToUint32(*number), max);
  number =
      factory->NewHeapNumber(-1.0 * std::numeric_limits<double>::infinity());
  CHECK_EQ(PositiveNumberToUint32(*number), 0u);
  number = factory->NewHeapNumber(std::nan(""));
  CHECK_EQ(PositiveNumberToUint32(*number), 0u);
}

// Some random offsets, mostly at 'suspicious' bit boundaries.

struct IntStringPair {
  int integer;
  std::string string;
};

static IntStringPair int_pairs[] = {{0, "0"},
                                    {101, "101"},
                                    {-1, "-1"},
                                    {1024, "1024"},
                                    {200000, "200000"},
                                    {-1024, "-1024"},
                                    {-200000, "-200000"},
                                    {kMinInt, "-2147483648"},
                                    {kMaxInt, "2147483647"}};

TEST_F(ConversionsTest, IntToStringView) {
  std::unique_ptr<char[]> buf(new char[4096]);

  for (size_t i = 0; i < arraysize(int_pairs); i++) {
    ASSERT_EQ(
        std::string(IntToStringView(int_pairs[i].integer, {buf.get(), 4096})),
        int_pairs[i].string);
  }
}

struct DoubleStringPair {
  double number;
  std::string string;
};

static DoubleStringPair double_pairs[] = {
    {0.0, "0"},
    {kMinInt, "-2147483648"},
    {kMaxInt, "2147483647"},
    // ES section 7.1.12.1 #sec-tostring-applied-to-the-number-type:
    // -0.0 is stringified to "0".
    {-0.0, "0"},
    {1.1, "1.1"},
    {0.1, "0.1"}};

TEST_F(ConversionsTest, DoubleToStringView) {
  std::unique_ptr<char[]> buf(new char[4096]);

  for (size_t i = 0; i < arraysize(double_pairs); i++) {
    ASSERT_EQ(std::string(DoubleToStringView(double_pairs[i].number,
                                             {buf.get(), 4096})),
              double_pairs[i].string);
  }
}

struct DoubleInt32Pair {
  double number;
  int integer;
};

static DoubleInt32Pair double_int32_pairs[] = {
    {0.0, 0},
    {-0.0, 0},
    {std::numeric_limits<double>::quiet_NaN(), 0},
    {std::numeric_limits<double>::infinity(), 0},
    {-std::numeric_limits<double>::infinity(), 0},
    {3.14, 3},
    {1.99, 1},
    {-1.99, -1},
    {static_cast<double>(kMinInt), kMinInt},
    {static_cast<double>(kMaxInt), kMaxInt},
    {kMaxSafeInteger, -1},
    {kMinSafeInteger, 1},
    {kMaxSafeInteger + 1, 0},
    {kMinSafeInteger - 1, 0},
};

TEST_F(ConversionsTest, DoubleToInt32) {
  for (size_t i = 0; i < arraysize(double_int32_pairs); i++) {
    ASSERT_EQ(DoubleToInt32(double_int32_pairs[i].number),
              double_int32_pairs[i].integer);
  }
}

struct DoubleInt64Pair {
  double number;
  int64_t integer;
};

static DoubleInt64Pair double_int64_pairs[] = {
    {0.0, 0},
    {-0.0, 0},
    {std::numeric_limits<double>::quiet_NaN(), 0},
    {std::numeric_limits<double>::infinity(), 0},
    {-std::numeric_limits<double>::infinity(), 0},
    {3.14, 3},
    {1.99, 1},
    {-1.99, -1},
    {kMinSafeInteger, static_cast<int64_t>(kMinSafeInteger)},
    {kMaxSafeInteger, static_cast<int64_t>(kMaxSafeIntegerUint64)},
    {kMinSafeInteger - 1, static_cast<int64_t>(kMinSafeInteger) - 1},
    {kMaxSafeInteger + 1, static_cast<int64_t>(kMaxSafeIntegerUint64) + 1},
    {static_cast<double>(std::numeric_limits<int64_t>::min()),
     std::numeric_limits<int64_t>::min()},
    // Max int64_t is not representable as a double, the closest is -2^63.
    {static_cast<double>(std::numeric_limits<int64_t>::max()),
     std::numeric_limits<int64_t>::min()},
    // So we test for a smaller number, representable as a double.
    {static_cast<double>((1ull << 63) - 1024), (1ull << 63) - 1024}};

TEST_F(ConversionsTest, DoubleToWebIDLInt64) {
  for (size_t i = 0; i < arraysize(double_int64_pairs); i++) {
    ASSERT_EQ(DoubleToWebIDLInt64(double_int64_pairs[i].number),
              double_int64_pairs[i].integer);
  }
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
