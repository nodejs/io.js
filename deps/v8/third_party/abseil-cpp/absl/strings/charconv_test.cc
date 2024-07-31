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

#include "absl/strings/charconv.h"

#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <limits>
#include <string>
#include <system_error>  // NOLINT(build/c++11)

#include "gtest/gtest.h"
#include "absl/strings/internal/pow10_helper.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

#ifdef _MSC_FULL_VER
#define ABSL_COMPILER_DOES_EXACT_ROUNDING 0
#define ABSL_STRTOD_HANDLES_NAN_CORRECTLY 0
#else
#define ABSL_COMPILER_DOES_EXACT_ROUNDING 1
#define ABSL_STRTOD_HANDLES_NAN_CORRECTLY 1
#endif

namespace {

using absl::strings_internal::Pow10;

#if ABSL_COMPILER_DOES_EXACT_ROUNDING

// Tests that the given string is accepted by absl::from_chars, and that it
// converts exactly equal to the given number.
void TestDoubleParse(absl::string_view str, double expected_number) {
  SCOPED_TRACE(str);
  double actual_number = 0.0;
  absl::from_chars_result result =
      absl::from_chars(str.data(), str.data() + str.length(), actual_number);
  EXPECT_EQ(result.ec, std::errc());
  EXPECT_EQ(result.ptr, str.data() + str.length());
  EXPECT_EQ(actual_number, expected_number);
}

void TestFloatParse(absl::string_view str, float expected_number) {
  SCOPED_TRACE(str);
  float actual_number = 0.0;
  absl::from_chars_result result =
      absl::from_chars(str.data(), str.data() + str.length(), actual_number);
  EXPECT_EQ(result.ec, std::errc());
  EXPECT_EQ(result.ptr, str.data() + str.length());
  EXPECT_EQ(actual_number, expected_number);
}

// Tests that the given double or single precision floating point literal is
// parsed correctly by absl::from_chars.
//
// These convenience macros assume that the C++ compiler being used also does
// fully correct decimal-to-binary conversions.
#define FROM_CHARS_TEST_DOUBLE(number)     \
  {                                        \
    TestDoubleParse(#number, number);      \
    TestDoubleParse("-" #number, -number); \
  }

#define FROM_CHARS_TEST_FLOAT(number)        \
  {                                          \
    TestFloatParse(#number, number##f);      \
    TestFloatParse("-" #number, -number##f); \
  }

TEST(FromChars, NearRoundingCases) {
  // Cases from "A Program for Testing IEEE Decimal-Binary Conversion"
  // by Vern Paxson.

  // Forms that should round towards zero.  (These are the hardest cases for
  // each decimal mantissa size.)
  FROM_CHARS_TEST_DOUBLE(5.e125);
  FROM_CHARS_TEST_DOUBLE(69.e267);
  FROM_CHARS_TEST_DOUBLE(999.e-026);
  FROM_CHARS_TEST_DOUBLE(7861.e-034);
  FROM_CHARS_TEST_DOUBLE(75569.e-254);
  FROM_CHARS_TEST_DOUBLE(928609.e-261);
  FROM_CHARS_TEST_DOUBLE(9210917.e080);
  FROM_CHARS_TEST_DOUBLE(84863171.e114);
  FROM_CHARS_TEST_DOUBLE(653777767.e273);
  FROM_CHARS_TEST_DOUBLE(5232604057.e-298);
  FROM_CHARS_TEST_DOUBLE(27235667517.e-109);
  FROM_CHARS_TEST_DOUBLE(653532977297.e-123);
  FROM_CHARS_TEST_DOUBLE(3142213164987.e-294);
  FROM_CHARS_TEST_DOUBLE(46202199371337.e-072);
  FROM_CHARS_TEST_DOUBLE(231010996856685.e-073);
  FROM_CHARS_TEST_DOUBLE(9324754620109615.e212);
  FROM_CHARS_TEST_DOUBLE(78459735791271921.e049);
  FROM_CHARS_TEST_DOUBLE(272104041512242479.e200);
  FROM_CHARS_TEST_DOUBLE(6802601037806061975.e198);
  FROM_CHARS_TEST_DOUBLE(20505426358836677347.e-221);
  FROM_CHARS_TEST_DOUBLE(836168422905420598437.e-234);
  FROM_CHARS_TEST_DOUBLE(4891559871276714924261.e222);
  FROM_CHARS_TEST_FLOAT(5.e-20);
  FROM_CHARS_TEST_FLOAT(67.e14);
  FROM_CHARS_TEST_FLOAT(985.e15);
  FROM_CHARS_TEST_FLOAT(7693.e-42);
  FROM_CHARS_TEST_FLOAT(55895.e-16);
  FROM_CHARS_TEST_FLOAT(996622.e-44);
  FROM_CHARS_TEST_FLOAT(7038531.e-32);
  FROM_CHARS_TEST_FLOAT(60419369.e-46);
  FROM_CHARS_TEST_FLOAT(702990899.e-20);
  FROM_CHARS_TEST_FLOAT(6930161142.e-48);
  FROM_CHARS_TEST_FLOAT(25933168707.e-13);
  FROM_CHARS_TEST_FLOAT(596428896559.e20);

  // Similarly, forms that should round away from zero.
  FROM_CHARS_TEST_DOUBLE(9.e-265);
  FROM_CHARS_TEST_DOUBLE(85.e-037);
  FROM_CHARS_TEST_DOUBLE(623.e100);
  FROM_CHARS_TEST_DOUBLE(3571.e263);
  FROM_CHARS_TEST_DOUBLE(81661.e153);
  FROM_CHARS_TEST_DOUBLE(920657.e-023);
  FROM_CHARS_TEST_DOUBLE(4603285.e-024);
  FROM_CHARS_TEST_DOUBLE(87575437.e-309);
  FROM_CHARS_TEST_DOUBLE(245540327.e122);
  FROM_CHARS_TEST_DOUBLE(6138508175.e120);
  FROM_CHARS_TEST_DOUBLE(83356057653.e193);
  FROM_CHARS_TEST_DOUBLE(619534293513.e124);
  FROM_CHARS_TEST_DOUBLE(2335141086879.e218);
  FROM_CHARS_TEST_DOUBLE(36167929443327.e-159);
  FROM_CHARS_TEST_DOUBLE(609610927149051.e-255);
  FROM_CHARS_TEST_DOUBLE(3743626360493413.e-165);
  FROM_CHARS_TEST_DOUBLE(94080055902682397.e-242);
  FROM_CHARS_TEST_DOUBLE(899810892172646163.e283);
  FROM_CHARS_TEST_DOUBLE(7120190517612959703.e120);
  FROM_CHARS_TEST_DOUBLE(25188282901709339043.e-252);
  FROM_CHARS_TEST_DOUBLE(308984926168550152811.e-052);
  FROM_CHARS_TEST_DOUBLE(6372891218502368041059.e064);
  FROM_CHARS_TEST_FLOAT(3.e-23);
  FROM_CHARS_TEST_FLOAT(57.e18);
  FROM_CHARS_TEST_FLOAT(789.e-35);
  FROM_CHARS_TEST_FLOAT(2539.e-18);
  FROM_CHARS_TEST_FLOAT(76173.e28);
  FROM_CHARS_TEST_FLOAT(887745.e-11);
  FROM_CHARS_TEST_FLOAT(5382571.e-37);
  FROM_CHARS_TEST_FLOAT(82381273.e-35);
  FROM_CHARS_TEST_FLOAT(750486563.e-38);
  FROM_CHARS_TEST_FLOAT(3752432815.e-39);
  FROM_CHARS_TEST_FLOAT(75224575729.e-45);
  FROM_CHARS_TEST_FLOAT(459926601011.e15);
}

#undef FROM_CHARS_TEST_DOUBLE
#undef FROM_CHARS_TEST_FLOAT
#endif

float ToFloat(absl::string_view s) {
  float f;
  absl::from_chars(s.data(), s.data() + s.size(), f);
  return f;
}

double ToDouble(absl::string_view s) {
  double d;
  absl::from_chars(s.data(), s.data() + s.size(), d);
  return d;
}

// A duplication of the test cases in "NearRoundingCases" above, but with
// expected values expressed with integers, using ldexp/ldexpf.  These test
// cases will work even on compilers that do not accurately round floating point
// literals.
TEST(FromChars, NearRoundingCasesExplicit) {
  EXPECT_EQ(ToDouble("5.e125"), ldexp(6653062250012735, 365));
  EXPECT_EQ(ToDouble("69.e267"), ldexp(4705683757438170, 841));
  EXPECT_EQ(ToDouble("999.e-026"), ldexp(6798841691080350, -129));
  EXPECT_EQ(ToDouble("7861.e-034"), ldexp(8975675289889240, -153));
  EXPECT_EQ(ToDouble("75569.e-254"), ldexp(6091718967192243, -880));
  EXPECT_EQ(ToDouble("928609.e-261"), ldexp(7849264900213743, -900));
  EXPECT_EQ(ToDouble("9210917.e080"), ldexp(8341110837370930, 236));
  EXPECT_EQ(ToDouble("84863171.e114"), ldexp(4625202867375927, 353));
  EXPECT_EQ(ToDouble("653777767.e273"), ldexp(5068902999763073, 884));
  EXPECT_EQ(ToDouble("5232604057.e-298"), ldexp(5741343011915040, -1010));
  EXPECT_EQ(ToDouble("27235667517.e-109"), ldexp(6707124626673586, -380));
  EXPECT_EQ(ToDouble("653532977297.e-123"), ldexp(7078246407265384, -422));
  EXPECT_EQ(ToDouble("3142213164987.e-294"), ldexp(8219991337640559, -988));
  EXPECT_EQ(ToDouble("46202199371337.e-072"), ldexp(5224462102115359, -246));
  EXPECT_EQ(ToDouble("231010996856685.e-073"), ldexp(5224462102115359, -247));
  EXPECT_EQ(ToDouble("9324754620109615.e212"), ldexp(5539753864394442, 705));
  EXPECT_EQ(ToDouble("78459735791271921.e049"), ldexp(8388176519442766, 166));
  EXPECT_EQ(ToDouble("272104041512242479.e200"), ldexp(5554409530847367, 670));
  EXPECT_EQ(ToDouble("6802601037806061975.e198"), ldexp(5554409530847367, 668));
  EXPECT_EQ(ToDouble("20505426358836677347.e-221"),
            ldexp(4524032052079546, -722));
  EXPECT_EQ(ToDouble("836168422905420598437.e-234"),
            ldexp(5070963299887562, -760));
  EXPECT_EQ(ToDouble("4891559871276714924261.e222"),
            ldexp(6452687840519111, 757));
  EXPECT_EQ(ToFloat("5.e-20"), ldexpf(15474250, -88));
  EXPECT_EQ(ToFloat("67.e14"), ldexpf(12479722, 29));
  EXPECT_EQ(ToFloat("985.e15"), ldexpf(14333636, 36));
  EXPECT_EQ(ToFloat("7693.e-42"), ldexpf(10979816, -150));
  EXPECT_EQ(ToFloat("55895.e-16"), ldexpf(12888509, -61));
  EXPECT_EQ(ToFloat("996622.e-44"), ldexpf(14224264, -150));
  EXPECT_EQ(ToFloat("7038531.e-32"), ldexpf(11420669, -107));
  EXPECT_EQ(ToFloat("60419369.e-46"), ldexpf(8623340, -150));
  EXPECT_EQ(ToFloat("702990899.e-20"), ldexpf(16209866, -61));
  EXPECT_EQ(ToFloat("6930161142.e-48"), ldexpf(9891056, -150));
  EXPECT_EQ(ToFloat("25933168707.e-13"), ldexpf(11138211, -32));
  EXPECT_EQ(ToFloat("596428896559.e20"), ldexpf(12333860, 82));


  EXPECT_EQ(ToDouble("9.e-265"), ldexp(8168427841980010, -930));
  EXPECT_EQ(ToDouble("85.e-037"), ldexp(6360455125664090, -169));
  EXPECT_EQ(ToDouble("623.e100"), ldexp(6263531988747231, 289));
  EXPECT_EQ(ToDouble("3571.e263"), ldexp(6234526311072170, 833));
  EXPECT_EQ(ToDouble("81661.e153"), ldexp(6696636728760206, 472));
  EXPECT_EQ(ToDouble("920657.e-023"), ldexp(5975405561110124, -109));
  EXPECT_EQ(ToDouble("4603285.e-024"), ldexp(5975405561110124, -110));
  EXPECT_EQ(ToDouble("87575437.e-309"), ldexp(8452160731874668, -1053));
  EXPECT_EQ(ToDouble("245540327.e122"), ldexp(4985336549131723, 381));
  EXPECT_EQ(ToDouble("6138508175.e120"), ldexp(4985336549131723, 379));
  EXPECT_EQ(ToDouble("83356057653.e193"), ldexp(5986732817132056, 625));
  EXPECT_EQ(ToDouble("619534293513.e124"), ldexp(4798406992060657, 399));
  EXPECT_EQ(ToDouble("2335141086879.e218"), ldexp(5419088166961646, 713));
  EXPECT_EQ(ToDouble("36167929443327.e-159"), ldexp(8135819834632444, -536));
  EXPECT_EQ(ToDouble("609610927149051.e-255"), ldexp(4576664294594737, -850));
  EXPECT_EQ(ToDouble("3743626360493413.e-165"), ldexp(6898586531774201, -549));
  EXPECT_EQ(ToDouble("94080055902682397.e-242"), ldexp(6273271706052298, -800));
  EXPECT_EQ(ToDouble("899810892172646163.e283"), ldexp(7563892574477827, 947));
  EXPECT_EQ(ToDouble("7120190517612959703.e120"), ldexp(5385467232557565, 409));
  EXPECT_EQ(ToDouble("25188282901709339043.e-252"),
            ldexp(5635662608542340, -825));
  EXPECT_EQ(ToDouble("308984926168550152811.e-052"),
            ldexp(5644774693823803, -157));
  EXPECT_EQ(ToDouble("6372891218502368041059.e064"),
            ldexp(4616868614322430, 233));

  EXPECT_EQ(ToFloat("3.e-23"), ldexpf(9507380, -98));
  EXPECT_EQ(ToFloat("57.e18"), ldexpf(12960300, 42));
  EXPECT_EQ(ToFloat("789.e-35"), ldexpf(10739312, -130));
  EXPECT_EQ(ToFloat("2539.e-18"), ldexpf(11990089, -72));
  EXPECT_EQ(ToFloat("76173.e28"), ldexpf(9845130, 86));
  EXPECT_EQ(ToFloat("887745.e-11"), ldexpf(9760860, -40));
  EXPECT_EQ(ToFloat("5382571.e-37"), ldexpf(11447463, -124));
  EXPECT_EQ(ToFloat("82381273.e-35"), ldexpf(8554961, -113));
  EXPECT_EQ(ToFloat("750486563.e-38"), ldexpf(9975678, -120));
  EXPECT_EQ(ToFloat("3752432815.e-39"), ldexpf(9975678, -121));
  EXPECT_EQ(ToFloat("75224575729.e-45"), ldexpf(13105970, -137));
  EXPECT_EQ(ToFloat("459926601011.e15"), ldexpf(12466336, 65));
}

// Common test logic for converting a string which lies exactly halfway between
// two target floats.
//
// mantissa and exponent represent the precise value between two floating point
// numbers, `expected_low` and `expected_high`.  The floating point
// representation to parse in `StrCat(mantissa, "e", exponent)`.
//
// This function checks that an input just slightly less than the exact value
// is rounded down to `expected_low`, and an input just slightly greater than
// the exact value is rounded up to `expected_high`.
//
// The exact value should round to `expected_half`, which must be either
// `expected_low` or `expected_high`.
template <typename FloatType>
void TestHalfwayValue(const std::string& mantissa, int exponent,
                      FloatType expected_low, FloatType expected_high,
                      FloatType expected_half) {
  std::string low_rep = mantissa;
  low_rep[low_rep.size() - 1] -= 1;
  absl::StrAppend(&low_rep, std::string(1000, '9'), "e", exponent);

  FloatType actual_low = 0;
  absl::from_chars(low_rep.data(), low_rep.data() + low_rep.size(), actual_low);
  EXPECT_EQ(expected_low, actual_low);

  std::string high_rep =
      absl::StrCat(mantissa, std::string(1000, '0'), "1e", exponent);
  FloatType actual_high = 0;
  absl::from_chars(high_rep.data(), high_rep.data() + high_rep.size(),
                   actual_high);
  EXPECT_EQ(expected_high, actual_high);

  std::string halfway_rep = absl::StrCat(mantissa, "e", exponent);
  FloatType actual_half = 0;
  absl::from_chars(halfway_rep.data(), halfway_rep.data() + halfway_rep.size(),
                   actual_half);
  EXPECT_EQ(expected_half, actual_half);
}

TEST(FromChars, DoubleRounding) {
  const double zero = 0.0;
  const double first_subnormal = nextafter(zero, 1.0);
  const double second_subnormal = nextafter(first_subnormal, 1.0);

  const double first_normal = DBL_MIN;
  const double last_subnormal = nextafter(first_normal, 0.0);
  const double second_normal = nextafter(first_normal, 1.0);

  const double last_normal = DBL_MAX;
  const double penultimate_normal = nextafter(last_normal, 0.0);

  // Various test cases for numbers between two representable floats.  Each
  // call to TestHalfwayValue tests a number just below and just above the
  // halfway point, as well as the number exactly between them.

  // Test between zero and first_subnormal.  Round-to-even tie rounds down.
  TestHalfwayValue(
      "2."
      "470328229206232720882843964341106861825299013071623822127928412503377536"
      "351043759326499181808179961898982823477228588654633283551779698981993873"
      "980053909390631503565951557022639229085839244910518443593180284993653615"
      "250031937045767824921936562366986365848075700158576926990370631192827955"
      "855133292783433840935197801553124659726357957462276646527282722005637400"
      "648549997709659947045402082816622623785739345073633900796776193057750674"
      "017632467360096895134053553745851666113422376667860416215968046191446729"
      "184030053005753084904876539171138659164623952491262365388187963623937328"
      "042389101867234849766823508986338858792562830275599565752445550725518931"
      "369083625477918694866799496832404970582102851318545139621383772282614543"
      "7693412532098591327667236328125",
      -324, zero, first_subnormal, zero);

  // first_subnormal and second_subnormal.  Round-to-even tie rounds up.
  TestHalfwayValue(
      "7."
      "410984687618698162648531893023320585475897039214871466383785237510132609"
      "053131277979497545424539885696948470431685765963899850655339096945981621"
      "940161728171894510697854671067917687257517734731555330779540854980960845"
      "750095811137303474765809687100959097544227100475730780971111893578483867"
      "565399878350301522805593404659373979179073872386829939581848166016912201"
      "945649993128979841136206248449867871357218035220901702390328579173252022"
      "052897402080290685402160661237554998340267130003581248647904138574340187"
      "552090159017259254714629617513415977493871857473787096164563890871811984"
      "127167305601704549300470526959016576377688490826798697257336652176556794"
      "107250876433756084600398490497214911746308553955635418864151316847843631"
      "3080237596295773983001708984375",
      -324, first_subnormal, second_subnormal, second_subnormal);

  // last_subnormal and first_normal.  Round-to-even tie rounds up.
  TestHalfwayValue(
      "2."
      "225073858507201136057409796709131975934819546351645648023426109724822222"
      "021076945516529523908135087914149158913039621106870086438694594645527657"
      "207407820621743379988141063267329253552286881372149012981122451451889849"
      "057222307285255133155755015914397476397983411801999323962548289017107081"
      "850690630666655994938275772572015763062690663332647565300009245888316433"
      "037779791869612049497390377829704905051080609940730262937128958950003583"
      "799967207254304360284078895771796150945516748243471030702609144621572289"
      "880258182545180325707018860872113128079512233426288368622321503775666622"
      "503982534335974568884423900265498198385487948292206894721689831099698365"
      "846814022854243330660339850886445804001034933970427567186443383770486037"
      "86162277173854562306587467901408672332763671875",
      -308, last_subnormal, first_normal, first_normal);

  // first_normal and second_normal.  Round-to-even tie rounds down.
  TestHalfwayValue(
      "2."
      "225073858507201630123055637955676152503612414573018013083228724049586647"
      "606759446192036794116886953213985520549032000903434781884412325572184367"
      "563347617020518175998922941393629966742598285899994830148971433555578567"
      "693279306015978183162142425067962460785295885199272493577688320732492479"
      "924816869232247165964934329258783950102250973957579510571600738343645738"
      "494324192997092179207389919761694314131497173265255020084997973676783743"
      "155205818804439163810572367791175177756227497413804253387084478193655533"
      "073867420834526162513029462022730109054820067654020201547112002028139700"
      "141575259123440177362244273712468151750189745559978653234255886219611516"
      "335924167958029604477064946470184777360934300451421683607013647479513962"
      "13837722826145437693412532098591327667236328125",
      -308, first_normal, second_normal, first_normal);

  // penultimate_normal and last_normal.  Round-to-even rounds down.
  TestHalfwayValue(
      "1."
      "797693134862315608353258760581052985162070023416521662616611746258695532"
      "672923265745300992879465492467506314903358770175220871059269879629062776"
      "047355692132901909191523941804762171253349609463563872612866401980290377"
      "995141836029815117562837277714038305214839639239356331336428021390916694"
      "57927874464075218944",
      308, penultimate_normal, last_normal, penultimate_normal);
}

// Same test cases as DoubleRounding, now with new and improved Much Smaller
// Precision!
TEST(FromChars, FloatRounding) {
  const float zero = 0.0;
  const float first_subnormal = nextafterf(zero, 1.0);
  const float second_subnormal = nextafterf(first_subnormal, 1.0);

  const float first_normal = FLT_MIN;
  const float last_subnormal = nextafterf(first_normal, 0.0);
  const float second_normal = nextafterf(first_normal, 1.0);

  const float last_normal = FLT_MAX;
  const float penultimate_normal = nextafterf(last_normal, 0.0);

  // Test between zero and first_subnormal.  Round-to-even tie rounds down.
  TestHalfwayValue(
      "7."
      "006492321624085354618647916449580656401309709382578858785341419448955413"
      "42930300743319094181060791015625",
      -46, zero, first_subnormal, zero);

  // first_subnormal and second_subnormal.  Round-to-even tie rounds up.
  TestHalfwayValue(
      "2."
      "101947696487225606385594374934874196920392912814773657635602425834686624"
      "028790902229957282543182373046875",
      -45, first_subnormal, second_subnormal, second_subnormal);

  // last_subnormal and first_normal.  Round-to-even tie rounds up.
  TestHalfwayValue(
      "1."
      "175494280757364291727882991035766513322858992758990427682963118425003064"
      "9651730385585324256680905818939208984375",
      -38, last_subnormal, first_normal, first_normal);

  // first_normal and second_normal.  Round-to-even tie rounds down.
  TestHalfwayValue(
      "1."
      "175494420887210724209590083408724842314472120785184615334540294131831453"
      "9442813071445925743319094181060791015625",
      -38, first_normal, second_normal, first_normal);

  // penultimate_normal and last_normal.  Round-to-even rounds down.
  TestHalfwayValue("3.40282336497324057985868971510891282432", 38,
                   penultimate_normal, last_normal, penultimate_normal);
}

TEST(FromChars, Underflow) {
  // Check that underflow is handled correctly, according to the specification
  // in DR 3081.
  double d;
  float f;
  absl::from_chars_result result;

  std::string negative_underflow = "-1e-1000";
  const char* begin = negative_underflow.data();
  const char* end = begin + negative_underflow.size();
  d = 100.0;
  result = absl::from_chars(begin, end, d);
  EXPECT_EQ(result.ptr, end);
  EXPECT_EQ(result.ec, std::errc::result_out_of_range);
  EXPECT_TRUE(std::signbit(d));  // negative
  EXPECT_GE(d, -std::numeric_limits<double>::min());
  f = 100.0;
  result = absl::from_chars(begin, end, f);
  EXPECT_EQ(result.ptr, end);
  EXPECT_EQ(result.ec, std::errc::result_out_of_range);
  EXPECT_TRUE(std::signbit(f));  // negative
  EXPECT_GE(f, -std::numeric_limits<float>::min());

  std::string positive_underflow = "1e-1000";
  begin = positive_underflow.data();
  end = begin + positive_underflow.size();
  d = -100.0;
  result = absl::from_chars(begin, end, d);
  EXPECT_EQ(result.ptr, end);
  EXPECT_EQ(result.ec, std::errc::result_out_of_range);
  EXPECT_FALSE(std::signbit(d));  // positive
  EXPECT_LE(d, std::numeric_limits<double>::min());
  f = -100.0;
  result = absl::from_chars(begin, end, f);
  EXPECT_EQ(result.ptr, end);
  EXPECT_EQ(result.ec, std::errc::result_out_of_range);
  EXPECT_FALSE(std::signbit(f));  // positive
  EXPECT_LE(f, std::numeric_limits<float>::min());
}

TEST(FromChars, Overflow) {
  // Check that overflow is handled correctly, according to the specification
  // in DR 3081.
  double d;
  float f;
  absl::from_chars_result result;

  std::string negative_overflow = "-1e1000";
  const char* begin = negative_overflow.data();
  const char* end = begin + negative_overflow.size();
  d = 100.0;
  result = absl::from_chars(begin, end, d);
  EXPECT_EQ(result.ptr, end);
  EXPECT_EQ(result.ec, std::errc::result_out_of_range);
  EXPECT_TRUE(std::signbit(d));  // negative
  EXPECT_EQ(d, -std::numeric_limits<double>::max());
  f = 100.0;
  result = absl::from_chars(begin, end, f);
  EXPECT_EQ(result.ptr, end);
  EXPECT_EQ(result.ec, std::errc::result_out_of_range);
  EXPECT_TRUE(std::signbit(f));  // negative
  EXPECT_EQ(f, -std::numeric_limits<float>::max());

  std::string positive_overflow = "1e1000";
  begin = positive_overflow.data();
  end = begin + positive_overflow.size();
  d = -100.0;
  result = absl::from_chars(begin, end, d);
  EXPECT_EQ(result.ptr, end);
  EXPECT_EQ(result.ec, std::errc::result_out_of_range);
  EXPECT_FALSE(std::signbit(d));  // positive
  EXPECT_EQ(d, std::numeric_limits<double>::max());
  f = -100.0;
  result = absl::from_chars(begin, end, f);
  EXPECT_EQ(result.ptr, end);
  EXPECT_EQ(result.ec, std::errc::result_out_of_range);
  EXPECT_FALSE(std::signbit(f));  // positive
  EXPECT_EQ(f, std::numeric_limits<float>::max());
}

TEST(FromChars, RegressionTestsFromFuzzer) {
  absl::string_view src = "0x21900000p00000000099";
  float f;
  auto result = absl::from_chars(src.data(), src.data() + src.size(), f);
  EXPECT_EQ(result.ec, std::errc::result_out_of_range);
}

TEST(FromChars, ReturnValuePtr) {
  // Check that `ptr` points one past the number scanned, even if that number
  // is not representable.
  double d;
  absl::from_chars_result result;

  std::string normal = "3.14@#$%@#$%";
  result = absl::from_chars(normal.data(), normal.data() + normal.size(), d);
  EXPECT_EQ(result.ec, std::errc());
  EXPECT_EQ(result.ptr - normal.data(), 4);

  std::string overflow = "1e1000@#$%@#$%";
  result = absl::from_chars(overflow.data(),
                            overflow.data() + overflow.size(), d);
  EXPECT_EQ(result.ec, std::errc::result_out_of_range);
  EXPECT_EQ(result.ptr - overflow.data(), 6);

  std::string garbage = "#$%@#$%";
  result = absl::from_chars(garbage.data(),
                            garbage.data() + garbage.size(), d);
  EXPECT_EQ(result.ec, std::errc::invalid_argument);
  EXPECT_EQ(result.ptr - garbage.data(), 0);
}

// Check for a wide range of inputs that strtod() and absl::from_chars() exactly
// agree on the conversion amount.
//
// This test assumes the platform's strtod() uses perfect round_to_nearest
// rounding.
TEST(FromChars, TestVersusStrtod) {
  for (int mantissa = 1000000; mantissa <= 9999999; mantissa += 501) {
    for (int exponent = -300; exponent < 300; ++exponent) {
      std::string candidate = absl::StrCat(mantissa, "e", exponent);
      double strtod_value = strtod(candidate.c_str(), nullptr);
      double absl_value = 0;
      absl::from_chars(candidate.data(), candidate.data() + candidate.size(),
                       absl_value);
      ASSERT_EQ(strtod_value, absl_value) << candidate;
    }
  }
}

// Check for a wide range of inputs that strtof() and absl::from_chars() exactly
// agree on the conversion amount.
//
// This test assumes the platform's strtof() uses perfect round_to_nearest
// rounding.
TEST(FromChars, TestVersusStrtof) {
  for (int mantissa = 1000000; mantissa <= 9999999; mantissa += 501) {
    for (int exponent = -43; exponent < 32; ++exponent) {
      std::string candidate = absl::StrCat(mantissa, "e", exponent);
      float strtod_value = strtof(candidate.c_str(), nullptr);
      float absl_value = 0;
      absl::from_chars(candidate.data(), candidate.data() + candidate.size(),
                       absl_value);
      ASSERT_EQ(strtod_value, absl_value) << candidate;
    }
  }
}

// Tests if two floating point values have identical bit layouts.  (EXPECT_EQ
// is not suitable for NaN testing, since NaNs are never equal.)
template <typename Float>
bool Identical(Float a, Float b) {
  return 0 == memcmp(&a, &b, sizeof(Float));
}

// Check that NaNs are parsed correctly.  The spec requires that
// std::from_chars on "NaN(123abc)" return the same value as std::nan("123abc").
// How such an n-char-sequence affects the generated NaN is unspecified, so we
// just test for symmetry with std::nan and strtod here.
//
// (In Linux, this parses the value as a number and stuffs that number into the
// free bits of a quiet NaN.)
TEST(FromChars, NaNDoubles) {
  for (std::string n_char_sequence :
       {"", "1", "2", "3", "fff", "FFF", "200000", "400000", "4000000000000",
        "8000000000000", "abc123", "legal_but_unexpected",
        "99999999999999999999999", "_"}) {
    std::string input = absl::StrCat("nan(", n_char_sequence, ")");
    SCOPED_TRACE(input);
    double from_chars_double;
    absl::from_chars(input.data(), input.data() + input.size(),
                     from_chars_double);
    double std_nan_double = std::nan(n_char_sequence.c_str());
    EXPECT_TRUE(Identical(from_chars_double, std_nan_double));

    // Also check that we match strtod()'s behavior.  This test assumes that the
    // platform has a compliant strtod().
#if ABSL_STRTOD_HANDLES_NAN_CORRECTLY
    double strtod_double = strtod(input.c_str(), nullptr);
    EXPECT_TRUE(Identical(from_chars_double, strtod_double));
#endif  // ABSL_STRTOD_HANDLES_NAN_CORRECTLY

    // Check that we can parse a negative NaN
    std::string negative_input = "-" + input;
    double negative_from_chars_double;
    absl::from_chars(negative_input.data(),
                     negative_input.data() + negative_input.size(),
                     negative_from_chars_double);
    EXPECT_TRUE(std::signbit(negative_from_chars_double));
    EXPECT_FALSE(Identical(negative_from_chars_double, from_chars_double));
    from_chars_double = std::copysign(from_chars_double, -1.0);
    EXPECT_TRUE(Identical(negative_from_chars_double, from_chars_double));
  }
}

TEST(FromChars, NaNFloats) {
  for (std::string n_char_sequence :
       {"", "1", "2", "3", "fff", "FFF", "200000", "400000", "4000000000000",
        "8000000000000", "abc123", "legal_but_unexpected",
        "99999999999999999999999", "_"}) {
    std::string input = absl::StrCat("nan(", n_char_sequence, ")");
    SCOPED_TRACE(input);
    float from_chars_float;
    absl::from_chars(input.data(), input.data() + input.size(),
                     from_chars_float);
    float std_nan_float = std::nanf(n_char_sequence.c_str());
    EXPECT_TRUE(Identical(from_chars_float, std_nan_float));

    // Also check that we match strtof()'s behavior.  This test assumes that the
    // platform has a compliant strtof().
#if ABSL_STRTOD_HANDLES_NAN_CORRECTLY
    float strtof_float = strtof(input.c_str(), nullptr);
    EXPECT_TRUE(Identical(from_chars_float, strtof_float));
#endif  // ABSL_STRTOD_HANDLES_NAN_CORRECTLY

    // Check that we can parse a negative NaN
    std::string negative_input = "-" + input;
    float negative_from_chars_float;
    absl::from_chars(negative_input.data(),
                     negative_input.data() + negative_input.size(),
                     negative_from_chars_float);
    EXPECT_TRUE(std::signbit(negative_from_chars_float));
    EXPECT_FALSE(Identical(negative_from_chars_float, from_chars_float));
    // Use the (float, float) overload of std::copysign to prevent narrowing;
    // see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=98251.
    from_chars_float = std::copysign(from_chars_float, -1.0f);
    EXPECT_TRUE(Identical(negative_from_chars_float, from_chars_float));
  }
}

// Returns an integer larger than step.  The values grow exponentially.
int NextStep(int step) {
  return step + (step >> 2) + 1;
}

// Test a conversion on a family of input strings, checking that the calculation
// is correct for in-bounds values, and that overflow and underflow are done
// correctly for out-of-bounds values.
//
// input_generator maps from an integer index to a string to test.
// expected_generator maps from an integer index to an expected Float value.
// from_chars conversion of input_generator(i) should result in
// expected_generator(i).
//
// lower_bound and upper_bound denote the smallest and largest values for which
// the conversion is expected to succeed.
template <typename Float>
void TestOverflowAndUnderflow(
    const std::function<std::string(int)>& input_generator,
    const std::function<Float(int)>& expected_generator, int lower_bound,
    int upper_bound) {
  // test legal values near lower_bound
  int index, step;
  for (index = lower_bound, step = 1; index < upper_bound;
       index += step, step = NextStep(step)) {
    std::string input = input_generator(index);
    SCOPED_TRACE(input);
    Float expected = expected_generator(index);
    Float actual;
    auto result =
        absl::from_chars(input.data(), input.data() + input.size(), actual);
    EXPECT_EQ(result.ec, std::errc());
    EXPECT_EQ(expected, actual)
        << absl::StrFormat("%a vs %a", expected, actual);
  }
  // test legal values near upper_bound
  for (index = upper_bound, step = 1; index > lower_bound;
       index -= step, step = NextStep(step)) {
    std::string input = input_generator(index);
    SCOPED_TRACE(input);
    Float expected = expected_generator(index);
    Float actual;
    auto result =
        absl::from_chars(input.data(), input.data() + input.size(), actual);
    EXPECT_EQ(result.ec, std::errc());
    EXPECT_EQ(expected, actual)
        << absl::StrFormat("%a vs %a", expected, actual);
  }
  // Test underflow values below lower_bound
  for (index = lower_bound - 1, step = 1; index > -1000000;
       index -= step, step = NextStep(step)) {
    std::string input = input_generator(index);
    SCOPED_TRACE(input);
    Float actual;
    auto result =
        absl::from_chars(input.data(), input.data() + input.size(), actual);
    EXPECT_EQ(result.ec, std::errc::result_out_of_range);
    EXPECT_LT(actual, 1.0);  // check for underflow
  }
  // Test overflow values above upper_bound
  for (index = upper_bound + 1, step = 1; index < 1000000;
       index += step, step = NextStep(step)) {
    std::string input = input_generator(index);
    SCOPED_TRACE(input);
    Float actual;
    auto result =
        absl::from_chars(input.data(), input.data() + input.size(), actual);
    EXPECT_EQ(result.ec, std::errc::result_out_of_range);
    EXPECT_GT(actual, 1.0);  // check for overflow
  }
}

// Check that overflow and underflow are caught correctly for hex doubles.
//
// The largest representable double is 0x1.fffffffffffffp+1023, and the
// smallest representable subnormal is 0x0.0000000000001p-1022, which equals
// 0x1p-1074.  Therefore 1023 and -1074 are the limits of acceptable exponents
// in this test.
TEST(FromChars, HexdecimalDoubleLimits) {
  auto input_gen = [](int index) { return absl::StrCat("0x1.0p", index); };
  auto expected_gen = [](int index) { return std::ldexp(1.0, index); };
  TestOverflowAndUnderflow<double>(input_gen, expected_gen, -1074, 1023);
}

// Check that overflow and underflow are caught correctly for hex floats.
//
// The largest representable float is 0x1.fffffep+127, and the smallest
// representable subnormal is 0x0.000002p-126, which equals 0x1p-149.
// Therefore 127 and -149 are the limits of acceptable exponents in this test.
TEST(FromChars, HexdecimalFloatLimits) {
  auto input_gen = [](int index) { return absl::StrCat("0x1.0p", index); };
  auto expected_gen = [](int index) { return std::ldexp(1.0f, index); };
  TestOverflowAndUnderflow<float>(input_gen, expected_gen, -149, 127);
}

// Check that overflow and underflow are caught correctly for decimal doubles.
//
// The largest representable double is about 1.8e308, and the smallest
// representable subnormal is about 5e-324.  '1e-324' therefore rounds away from
// the smallest representable positive value.  -323 and 308 are the limits of
// acceptable exponents in this test.
TEST(FromChars, DecimalDoubleLimits) {
  auto input_gen = [](int index) { return absl::StrCat("1.0e", index); };
  auto expected_gen = [](int index) { return Pow10(index); };
  TestOverflowAndUnderflow<double>(input_gen, expected_gen, -323, 308);
}

// Check that overflow and underflow are caught correctly for decimal floats.
//
// The largest representable float is about 3.4e38, and the smallest
// representable subnormal is about 1.45e-45.  '1e-45' therefore rounds towards
// the smallest representable positive value.  -45 and 38 are the limits of
// acceptable exponents in this test.
TEST(FromChars, DecimalFloatLimits) {
  auto input_gen = [](int index) { return absl::StrCat("1.0e", index); };
  auto expected_gen = [](int index) { return Pow10(index); };
  TestOverflowAndUnderflow<float>(input_gen, expected_gen, -45, 38);
}

}  // namespace
