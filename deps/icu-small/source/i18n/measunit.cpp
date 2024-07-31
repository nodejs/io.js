// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
* Copyright (c) 2004-2016, International Business Machines
* Corporation and others.  All Rights Reserved.
**********************************************************************
* Author: Alan Liu
* Created: April 26, 2004
* Since: ICU 3.0
**********************************************************************
*/
#include "utypeinfo.h" // for 'typeid' to work

#include "unicode/measunit.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/uenum.h"
#include "unicode/errorcode.h"
#include "ustrenum.h"
#include "cstring.h"
#include "uassert.h"
#include "measunit_impl.h"

U_NAMESPACE_BEGIN

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(MeasureUnit)

// All code between the "Start generated code" comment and
// the "End generated code" comment is auto generated code
// and must not be edited manually. For instructions on how to correctly
// update this code, refer to:
// https://icu.unicode.org/design/formatting/measureformat/updating-measure-unit
//
// Start generated code for measunit.cpp

// Maps from Type ID to offset in gSubTypes.
static const int32_t gOffsets[] = {
    0,
    2,
    7,
    17,
    27,
    31,
    332,
    343,
    360,
    364,
    373,
    376,
    380,
    388,
    410,
    414,
    429,
    430,
    436,
    447,
    452,
    456,
    458,
    492
};

static const int32_t kCurrencyOffset = 5;

// Must be sorted alphabetically.
static const char * const gTypes[] = {
    "acceleration",
    "angle",
    "area",
    "concentr",
    "consumption",
    "currency",
    "digital",
    "duration",
    "electric",
    "energy",
    "force",
    "frequency",
    "graphics",
    "length",
    "light",
    "mass",
    "none",
    "power",
    "pressure",
    "speed",
    "temperature",
    "torque",
    "volume"
};

// Must be grouped by type and sorted alphabetically within each type.
static const char * const gSubTypes[] = {
    "g-force",
    "meter-per-square-second",
    "arc-minute",
    "arc-second",
    "degree",
    "radian",
    "revolution",
    "acre",
    "dunam",
    "hectare",
    "square-centimeter",
    "square-foot",
    "square-inch",
    "square-kilometer",
    "square-meter",
    "square-mile",
    "square-yard",
    "item",
    "karat",
    "milligram-ofglucose-per-deciliter",
    "milligram-per-deciliter",
    "millimole-per-liter",
    "mole",
    "percent",
    "permille",
    "permillion",
    "permyriad",
    "liter-per-100-kilometer",
    "liter-per-kilometer",
    "mile-per-gallon",
    "mile-per-gallon-imperial",
    "ADP",
    "AED",
    "AFA",
    "AFN",
    "ALK",
    "ALL",
    "AMD",
    "ANG",
    "AOA",
    "AOK",
    "AON",
    "AOR",
    "ARA",
    "ARP",
    "ARS",
    "ARY",
    "ATS",
    "AUD",
    "AWG",
    "AYM",
    "AZM",
    "AZN",
    "BAD",
    "BAM",
    "BBD",
    "BDT",
    "BEC",
    "BEF",
    "BEL",
    "BGJ",
    "BGK",
    "BGL",
    "BGN",
    "BHD",
    "BIF",
    "BMD",
    "BND",
    "BOB",
    "BOP",
    "BOV",
    "BRB",
    "BRC",
    "BRE",
    "BRL",
    "BRN",
    "BRR",
    "BSD",
    "BTN",
    "BUK",
    "BWP",
    "BYB",
    "BYN",
    "BYR",
    "BZD",
    "CAD",
    "CDF",
    "CHC",
    "CHE",
    "CHF",
    "CHW",
    "CLF",
    "CLP",
    "CNY",
    "COP",
    "COU",
    "CRC",
    "CSD",
    "CSJ",
    "CSK",
    "CUC",
    "CUP",
    "CVE",
    "CYP",
    "CZK",
    "DDM",
    "DEM",
    "DJF",
    "DKK",
    "DOP",
    "DZD",
    "ECS",
    "ECV",
    "EEK",
    "EGP",
    "ERN",
    "ESA",
    "ESB",
    "ESP",
    "ETB",
    "EUR",
    "FIM",
    "FJD",
    "FKP",
    "FRF",
    "GBP",
    "GEK",
    "GEL",
    "GHC",
    "GHP",
    "GHS",
    "GIP",
    "GMD",
    "GNE",
    "GNF",
    "GNS",
    "GQE",
    "GRD",
    "GTQ",
    "GWE",
    "GWP",
    "GYD",
    "HKD",
    "HNL",
    "HRD",
    "HRK",
    "HTG",
    "HUF",
    "IDR",
    "IEP",
    "ILP",
    "ILR",
    "ILS",
    "INR",
    "IQD",
    "IRR",
    "ISJ",
    "ISK",
    "ITL",
    "JMD",
    "JOD",
    "JPY",
    "KES",
    "KGS",
    "KHR",
    "KMF",
    "KPW",
    "KRW",
    "KWD",
    "KYD",
    "KZT",
    "LAJ",
    "LAK",
    "LBP",
    "LKR",
    "LRD",
    "LSL",
    "LSM",
    "LTL",
    "LTT",
    "LUC",
    "LUF",
    "LUL",
    "LVL",
    "LVR",
    "LYD",
    "MAD",
    "MDL",
    "MGA",
    "MGF",
    "MKD",
    "MLF",
    "MMK",
    "MNT",
    "MOP",
    "MRO",
    "MRU",
    "MTL",
    "MTP",
    "MUR",
    "MVQ",
    "MVR",
    "MWK",
    "MXN",
    "MXP",
    "MXV",
    "MYR",
    "MZE",
    "MZM",
    "MZN",
    "NAD",
    "NGN",
    "NIC",
    "NIO",
    "NLG",
    "NOK",
    "NPR",
    "NZD",
    "OMR",
    "PAB",
    "PEH",
    "PEI",
    "PEN",
    "PES",
    "PGK",
    "PHP",
    "PKR",
    "PLN",
    "PLZ",
    "PTE",
    "PYG",
    "QAR",
    "RHD",
    "ROK",
    "ROL",
    "RON",
    "RSD",
    "RUB",
    "RUR",
    "RWF",
    "SAR",
    "SBD",
    "SCR",
    "SDD",
    "SDG",
    "SDP",
    "SEK",
    "SGD",
    "SHP",
    "SIT",
    "SKK",
    "SLE",
    "SLL",
    "SOS",
    "SRD",
    "SRG",
    "SSP",
    "STD",
    "STN",
    "SUR",
    "SVC",
    "SYP",
    "SZL",
    "THB",
    "TJR",
    "TJS",
    "TMM",
    "TMT",
    "TND",
    "TOP",
    "TPE",
    "TRL",
    "TRY",
    "TTD",
    "TWD",
    "TZS",
    "UAH",
    "UAK",
    "UGS",
    "UGW",
    "UGX",
    "USD",
    "USN",
    "USS",
    "UYI",
    "UYN",
    "UYP",
    "UYU",
    "UYW",
    "UZS",
    "VEB",
    "VED",
    "VEF",
    "VES",
    "VNC",
    "VND",
    "VUV",
    "WST",
    "XAF",
    "XAG",
    "XAU",
    "XBA",
    "XBB",
    "XBC",
    "XBD",
    "XCD",
    "XDR",
    "XEU",
    "XOF",
    "XPD",
    "XPF",
    "XPT",
    "XSU",
    "XTS",
    "XUA",
    "XXX",
    "YDD",
    "YER",
    "YUD",
    "YUM",
    "YUN",
    "ZAL",
    "ZAR",
    "ZMK",
    "ZMW",
    "ZRN",
    "ZRZ",
    "ZWC",
    "ZWD",
    "ZWL",
    "ZWN",
    "ZWR",
    "bit",
    "byte",
    "gigabit",
    "gigabyte",
    "kilobit",
    "kilobyte",
    "megabit",
    "megabyte",
    "petabyte",
    "terabit",
    "terabyte",
    "century",
    "day",
    "day-person",
    "decade",
    "hour",
    "microsecond",
    "millisecond",
    "minute",
    "month",
    "month-person",
    "nanosecond",
    "quarter",
    "second",
    "week",
    "week-person",
    "year",
    "year-person",
    "ampere",
    "milliampere",
    "ohm",
    "volt",
    "british-thermal-unit",
    "calorie",
    "electronvolt",
    "foodcalorie",
    "joule",
    "kilocalorie",
    "kilojoule",
    "kilowatt-hour",
    "therm-us",
    "kilowatt-hour-per-100-kilometer",
    "newton",
    "pound-force",
    "gigahertz",
    "hertz",
    "kilohertz",
    "megahertz",
    "dot",
    "dot-per-centimeter",
    "dot-per-inch",
    "em",
    "megapixel",
    "pixel",
    "pixel-per-centimeter",
    "pixel-per-inch",
    "astronomical-unit",
    "centimeter",
    "decimeter",
    "earth-radius",
    "fathom",
    "foot",
    "furlong",
    "inch",
    "kilometer",
    "light-year",
    "meter",
    "micrometer",
    "mile",
    "mile-scandinavian",
    "millimeter",
    "nanometer",
    "nautical-mile",
    "parsec",
    "picometer",
    "point",
    "solar-radius",
    "yard",
    "candela",
    "lumen",
    "lux",
    "solar-luminosity",
    "carat",
    "dalton",
    "earth-mass",
    "grain",
    "gram",
    "kilogram",
    "microgram",
    "milligram",
    "ounce",
    "ounce-troy",
    "pound",
    "solar-mass",
    "stone",
    "ton",
    "tonne",
    "",
    "gigawatt",
    "horsepower",
    "kilowatt",
    "megawatt",
    "milliwatt",
    "watt",
    "atmosphere",
    "bar",
    "gasoline-energy-density",
    "hectopascal",
    "inch-ofhg",
    "kilopascal",
    "megapascal",
    "millibar",
    "millimeter-ofhg",
    "pascal",
    "pound-force-per-square-inch",
    "beaufort",
    "kilometer-per-hour",
    "knot",
    "meter-per-second",
    "mile-per-hour",
    "celsius",
    "fahrenheit",
    "generic",
    "kelvin",
    "newton-meter",
    "pound-force-foot",
    "acre-foot",
    "barrel",
    "bushel",
    "centiliter",
    "cubic-centimeter",
    "cubic-foot",
    "cubic-inch",
    "cubic-kilometer",
    "cubic-meter",
    "cubic-mile",
    "cubic-yard",
    "cup",
    "cup-metric",
    "deciliter",
    "dessert-spoon",
    "dessert-spoon-imperial",
    "dram",
    "drop",
    "fluid-ounce",
    "fluid-ounce-imperial",
    "gallon",
    "gallon-imperial",
    "hectoliter",
    "jigger",
    "liter",
    "megaliter",
    "milliliter",
    "pinch",
    "pint",
    "pint-metric",
    "quart",
    "quart-imperial",
    "tablespoon",
    "teaspoon"
};

// Shortcuts to the base unit in order to make the default constructor fast
static const int32_t kBaseTypeIdx = 16;
static const int32_t kBaseSubTypeIdx = 0;

MeasureUnit *MeasureUnit::createGForce(UErrorCode &status) {
    return MeasureUnit::create(0, 0, status);
}

MeasureUnit MeasureUnit::getGForce() {
    return MeasureUnit(0, 0);
}

MeasureUnit *MeasureUnit::createMeterPerSecondSquared(UErrorCode &status) {
    return MeasureUnit::create(0, 1, status);
}

MeasureUnit MeasureUnit::getMeterPerSecondSquared() {
    return MeasureUnit(0, 1);
}

MeasureUnit *MeasureUnit::createArcMinute(UErrorCode &status) {
    return MeasureUnit::create(1, 0, status);
}

MeasureUnit MeasureUnit::getArcMinute() {
    return MeasureUnit(1, 0);
}

MeasureUnit *MeasureUnit::createArcSecond(UErrorCode &status) {
    return MeasureUnit::create(1, 1, status);
}

MeasureUnit MeasureUnit::getArcSecond() {
    return MeasureUnit(1, 1);
}

MeasureUnit *MeasureUnit::createDegree(UErrorCode &status) {
    return MeasureUnit::create(1, 2, status);
}

MeasureUnit MeasureUnit::getDegree() {
    return MeasureUnit(1, 2);
}

MeasureUnit *MeasureUnit::createRadian(UErrorCode &status) {
    return MeasureUnit::create(1, 3, status);
}

MeasureUnit MeasureUnit::getRadian() {
    return MeasureUnit(1, 3);
}

MeasureUnit *MeasureUnit::createRevolutionAngle(UErrorCode &status) {
    return MeasureUnit::create(1, 4, status);
}

MeasureUnit MeasureUnit::getRevolutionAngle() {
    return MeasureUnit(1, 4);
}

MeasureUnit *MeasureUnit::createAcre(UErrorCode &status) {
    return MeasureUnit::create(2, 0, status);
}

MeasureUnit MeasureUnit::getAcre() {
    return MeasureUnit(2, 0);
}

MeasureUnit *MeasureUnit::createDunam(UErrorCode &status) {
    return MeasureUnit::create(2, 1, status);
}

MeasureUnit MeasureUnit::getDunam() {
    return MeasureUnit(2, 1);
}

MeasureUnit *MeasureUnit::createHectare(UErrorCode &status) {
    return MeasureUnit::create(2, 2, status);
}

MeasureUnit MeasureUnit::getHectare() {
    return MeasureUnit(2, 2);
}

MeasureUnit *MeasureUnit::createSquareCentimeter(UErrorCode &status) {
    return MeasureUnit::create(2, 3, status);
}

MeasureUnit MeasureUnit::getSquareCentimeter() {
    return MeasureUnit(2, 3);
}

MeasureUnit *MeasureUnit::createSquareFoot(UErrorCode &status) {
    return MeasureUnit::create(2, 4, status);
}

MeasureUnit MeasureUnit::getSquareFoot() {
    return MeasureUnit(2, 4);
}

MeasureUnit *MeasureUnit::createSquareInch(UErrorCode &status) {
    return MeasureUnit::create(2, 5, status);
}

MeasureUnit MeasureUnit::getSquareInch() {
    return MeasureUnit(2, 5);
}

MeasureUnit *MeasureUnit::createSquareKilometer(UErrorCode &status) {
    return MeasureUnit::create(2, 6, status);
}

MeasureUnit MeasureUnit::getSquareKilometer() {
    return MeasureUnit(2, 6);
}

MeasureUnit *MeasureUnit::createSquareMeter(UErrorCode &status) {
    return MeasureUnit::create(2, 7, status);
}

MeasureUnit MeasureUnit::getSquareMeter() {
    return MeasureUnit(2, 7);
}

MeasureUnit *MeasureUnit::createSquareMile(UErrorCode &status) {
    return MeasureUnit::create(2, 8, status);
}

MeasureUnit MeasureUnit::getSquareMile() {
    return MeasureUnit(2, 8);
}

MeasureUnit *MeasureUnit::createSquareYard(UErrorCode &status) {
    return MeasureUnit::create(2, 9, status);
}

MeasureUnit MeasureUnit::getSquareYard() {
    return MeasureUnit(2, 9);
}

MeasureUnit *MeasureUnit::createItem(UErrorCode &status) {
    return MeasureUnit::create(3, 0, status);
}

MeasureUnit MeasureUnit::getItem() {
    return MeasureUnit(3, 0);
}

MeasureUnit *MeasureUnit::createKarat(UErrorCode &status) {
    return MeasureUnit::create(3, 1, status);
}

MeasureUnit MeasureUnit::getKarat() {
    return MeasureUnit(3, 1);
}

MeasureUnit *MeasureUnit::createMilligramOfglucosePerDeciliter(UErrorCode &status) {
    return MeasureUnit::create(3, 2, status);
}

MeasureUnit MeasureUnit::getMilligramOfglucosePerDeciliter() {
    return MeasureUnit(3, 2);
}

MeasureUnit *MeasureUnit::createMilligramPerDeciliter(UErrorCode &status) {
    return MeasureUnit::create(3, 3, status);
}

MeasureUnit MeasureUnit::getMilligramPerDeciliter() {
    return MeasureUnit(3, 3);
}

MeasureUnit *MeasureUnit::createMillimolePerLiter(UErrorCode &status) {
    return MeasureUnit::create(3, 4, status);
}

MeasureUnit MeasureUnit::getMillimolePerLiter() {
    return MeasureUnit(3, 4);
}

MeasureUnit *MeasureUnit::createMole(UErrorCode &status) {
    return MeasureUnit::create(3, 5, status);
}

MeasureUnit MeasureUnit::getMole() {
    return MeasureUnit(3, 5);
}

MeasureUnit *MeasureUnit::createPercent(UErrorCode &status) {
    return MeasureUnit::create(3, 6, status);
}

MeasureUnit MeasureUnit::getPercent() {
    return MeasureUnit(3, 6);
}

MeasureUnit *MeasureUnit::createPermille(UErrorCode &status) {
    return MeasureUnit::create(3, 7, status);
}

MeasureUnit MeasureUnit::getPermille() {
    return MeasureUnit(3, 7);
}

MeasureUnit *MeasureUnit::createPartPerMillion(UErrorCode &status) {
    return MeasureUnit::create(3, 8, status);
}

MeasureUnit MeasureUnit::getPartPerMillion() {
    return MeasureUnit(3, 8);
}

MeasureUnit *MeasureUnit::createPermyriad(UErrorCode &status) {
    return MeasureUnit::create(3, 9, status);
}

MeasureUnit MeasureUnit::getPermyriad() {
    return MeasureUnit(3, 9);
}

MeasureUnit *MeasureUnit::createLiterPer100Kilometers(UErrorCode &status) {
    return MeasureUnit::create(4, 0, status);
}

MeasureUnit MeasureUnit::getLiterPer100Kilometers() {
    return MeasureUnit(4, 0);
}

MeasureUnit *MeasureUnit::createLiterPerKilometer(UErrorCode &status) {
    return MeasureUnit::create(4, 1, status);
}

MeasureUnit MeasureUnit::getLiterPerKilometer() {
    return MeasureUnit(4, 1);
}

MeasureUnit *MeasureUnit::createMilePerGallon(UErrorCode &status) {
    return MeasureUnit::create(4, 2, status);
}

MeasureUnit MeasureUnit::getMilePerGallon() {
    return MeasureUnit(4, 2);
}

MeasureUnit *MeasureUnit::createMilePerGallonImperial(UErrorCode &status) {
    return MeasureUnit::create(4, 3, status);
}

MeasureUnit MeasureUnit::getMilePerGallonImperial() {
    return MeasureUnit(4, 3);
}

MeasureUnit *MeasureUnit::createBit(UErrorCode &status) {
    return MeasureUnit::create(6, 0, status);
}

MeasureUnit MeasureUnit::getBit() {
    return MeasureUnit(6, 0);
}

MeasureUnit *MeasureUnit::createByte(UErrorCode &status) {
    return MeasureUnit::create(6, 1, status);
}

MeasureUnit MeasureUnit::getByte() {
    return MeasureUnit(6, 1);
}

MeasureUnit *MeasureUnit::createGigabit(UErrorCode &status) {
    return MeasureUnit::create(6, 2, status);
}

MeasureUnit MeasureUnit::getGigabit() {
    return MeasureUnit(6, 2);
}

MeasureUnit *MeasureUnit::createGigabyte(UErrorCode &status) {
    return MeasureUnit::create(6, 3, status);
}

MeasureUnit MeasureUnit::getGigabyte() {
    return MeasureUnit(6, 3);
}

MeasureUnit *MeasureUnit::createKilobit(UErrorCode &status) {
    return MeasureUnit::create(6, 4, status);
}

MeasureUnit MeasureUnit::getKilobit() {
    return MeasureUnit(6, 4);
}

MeasureUnit *MeasureUnit::createKilobyte(UErrorCode &status) {
    return MeasureUnit::create(6, 5, status);
}

MeasureUnit MeasureUnit::getKilobyte() {
    return MeasureUnit(6, 5);
}

MeasureUnit *MeasureUnit::createMegabit(UErrorCode &status) {
    return MeasureUnit::create(6, 6, status);
}

MeasureUnit MeasureUnit::getMegabit() {
    return MeasureUnit(6, 6);
}

MeasureUnit *MeasureUnit::createMegabyte(UErrorCode &status) {
    return MeasureUnit::create(6, 7, status);
}

MeasureUnit MeasureUnit::getMegabyte() {
    return MeasureUnit(6, 7);
}

MeasureUnit *MeasureUnit::createPetabyte(UErrorCode &status) {
    return MeasureUnit::create(6, 8, status);
}

MeasureUnit MeasureUnit::getPetabyte() {
    return MeasureUnit(6, 8);
}

MeasureUnit *MeasureUnit::createTerabit(UErrorCode &status) {
    return MeasureUnit::create(6, 9, status);
}

MeasureUnit MeasureUnit::getTerabit() {
    return MeasureUnit(6, 9);
}

MeasureUnit *MeasureUnit::createTerabyte(UErrorCode &status) {
    return MeasureUnit::create(6, 10, status);
}

MeasureUnit MeasureUnit::getTerabyte() {
    return MeasureUnit(6, 10);
}

MeasureUnit *MeasureUnit::createCentury(UErrorCode &status) {
    return MeasureUnit::create(7, 0, status);
}

MeasureUnit MeasureUnit::getCentury() {
    return MeasureUnit(7, 0);
}

MeasureUnit *MeasureUnit::createDay(UErrorCode &status) {
    return MeasureUnit::create(7, 1, status);
}

MeasureUnit MeasureUnit::getDay() {
    return MeasureUnit(7, 1);
}

MeasureUnit *MeasureUnit::createDayPerson(UErrorCode &status) {
    return MeasureUnit::create(7, 2, status);
}

MeasureUnit MeasureUnit::getDayPerson() {
    return MeasureUnit(7, 2);
}

MeasureUnit *MeasureUnit::createDecade(UErrorCode &status) {
    return MeasureUnit::create(7, 3, status);
}

MeasureUnit MeasureUnit::getDecade() {
    return MeasureUnit(7, 3);
}

MeasureUnit *MeasureUnit::createHour(UErrorCode &status) {
    return MeasureUnit::create(7, 4, status);
}

MeasureUnit MeasureUnit::getHour() {
    return MeasureUnit(7, 4);
}

MeasureUnit *MeasureUnit::createMicrosecond(UErrorCode &status) {
    return MeasureUnit::create(7, 5, status);
}

MeasureUnit MeasureUnit::getMicrosecond() {
    return MeasureUnit(7, 5);
}

MeasureUnit *MeasureUnit::createMillisecond(UErrorCode &status) {
    return MeasureUnit::create(7, 6, status);
}

MeasureUnit MeasureUnit::getMillisecond() {
    return MeasureUnit(7, 6);
}

MeasureUnit *MeasureUnit::createMinute(UErrorCode &status) {
    return MeasureUnit::create(7, 7, status);
}

MeasureUnit MeasureUnit::getMinute() {
    return MeasureUnit(7, 7);
}

MeasureUnit *MeasureUnit::createMonth(UErrorCode &status) {
    return MeasureUnit::create(7, 8, status);
}

MeasureUnit MeasureUnit::getMonth() {
    return MeasureUnit(7, 8);
}

MeasureUnit *MeasureUnit::createMonthPerson(UErrorCode &status) {
    return MeasureUnit::create(7, 9, status);
}

MeasureUnit MeasureUnit::getMonthPerson() {
    return MeasureUnit(7, 9);
}

MeasureUnit *MeasureUnit::createNanosecond(UErrorCode &status) {
    return MeasureUnit::create(7, 10, status);
}

MeasureUnit MeasureUnit::getNanosecond() {
    return MeasureUnit(7, 10);
}

MeasureUnit *MeasureUnit::createQuarter(UErrorCode &status) {
    return MeasureUnit::create(7, 11, status);
}

MeasureUnit MeasureUnit::getQuarter() {
    return MeasureUnit(7, 11);
}

MeasureUnit *MeasureUnit::createSecond(UErrorCode &status) {
    return MeasureUnit::create(7, 12, status);
}

MeasureUnit MeasureUnit::getSecond() {
    return MeasureUnit(7, 12);
}

MeasureUnit *MeasureUnit::createWeek(UErrorCode &status) {
    return MeasureUnit::create(7, 13, status);
}

MeasureUnit MeasureUnit::getWeek() {
    return MeasureUnit(7, 13);
}

MeasureUnit *MeasureUnit::createWeekPerson(UErrorCode &status) {
    return MeasureUnit::create(7, 14, status);
}

MeasureUnit MeasureUnit::getWeekPerson() {
    return MeasureUnit(7, 14);
}

MeasureUnit *MeasureUnit::createYear(UErrorCode &status) {
    return MeasureUnit::create(7, 15, status);
}

MeasureUnit MeasureUnit::getYear() {
    return MeasureUnit(7, 15);
}

MeasureUnit *MeasureUnit::createYearPerson(UErrorCode &status) {
    return MeasureUnit::create(7, 16, status);
}

MeasureUnit MeasureUnit::getYearPerson() {
    return MeasureUnit(7, 16);
}

MeasureUnit *MeasureUnit::createAmpere(UErrorCode &status) {
    return MeasureUnit::create(8, 0, status);
}

MeasureUnit MeasureUnit::getAmpere() {
    return MeasureUnit(8, 0);
}

MeasureUnit *MeasureUnit::createMilliampere(UErrorCode &status) {
    return MeasureUnit::create(8, 1, status);
}

MeasureUnit MeasureUnit::getMilliampere() {
    return MeasureUnit(8, 1);
}

MeasureUnit *MeasureUnit::createOhm(UErrorCode &status) {
    return MeasureUnit::create(8, 2, status);
}

MeasureUnit MeasureUnit::getOhm() {
    return MeasureUnit(8, 2);
}

MeasureUnit *MeasureUnit::createVolt(UErrorCode &status) {
    return MeasureUnit::create(8, 3, status);
}

MeasureUnit MeasureUnit::getVolt() {
    return MeasureUnit(8, 3);
}

MeasureUnit *MeasureUnit::createBritishThermalUnit(UErrorCode &status) {
    return MeasureUnit::create(9, 0, status);
}

MeasureUnit MeasureUnit::getBritishThermalUnit() {
    return MeasureUnit(9, 0);
}

MeasureUnit *MeasureUnit::createCalorie(UErrorCode &status) {
    return MeasureUnit::create(9, 1, status);
}

MeasureUnit MeasureUnit::getCalorie() {
    return MeasureUnit(9, 1);
}

MeasureUnit *MeasureUnit::createElectronvolt(UErrorCode &status) {
    return MeasureUnit::create(9, 2, status);
}

MeasureUnit MeasureUnit::getElectronvolt() {
    return MeasureUnit(9, 2);
}

MeasureUnit *MeasureUnit::createFoodcalorie(UErrorCode &status) {
    return MeasureUnit::create(9, 3, status);
}

MeasureUnit MeasureUnit::getFoodcalorie() {
    return MeasureUnit(9, 3);
}

MeasureUnit *MeasureUnit::createJoule(UErrorCode &status) {
    return MeasureUnit::create(9, 4, status);
}

MeasureUnit MeasureUnit::getJoule() {
    return MeasureUnit(9, 4);
}

MeasureUnit *MeasureUnit::createKilocalorie(UErrorCode &status) {
    return MeasureUnit::create(9, 5, status);
}

MeasureUnit MeasureUnit::getKilocalorie() {
    return MeasureUnit(9, 5);
}

MeasureUnit *MeasureUnit::createKilojoule(UErrorCode &status) {
    return MeasureUnit::create(9, 6, status);
}

MeasureUnit MeasureUnit::getKilojoule() {
    return MeasureUnit(9, 6);
}

MeasureUnit *MeasureUnit::createKilowattHour(UErrorCode &status) {
    return MeasureUnit::create(9, 7, status);
}

MeasureUnit MeasureUnit::getKilowattHour() {
    return MeasureUnit(9, 7);
}

MeasureUnit *MeasureUnit::createThermUs(UErrorCode &status) {
    return MeasureUnit::create(9, 8, status);
}

MeasureUnit MeasureUnit::getThermUs() {
    return MeasureUnit(9, 8);
}

MeasureUnit *MeasureUnit::createKilowattHourPer100Kilometer(UErrorCode &status) {
    return MeasureUnit::create(10, 0, status);
}

MeasureUnit MeasureUnit::getKilowattHourPer100Kilometer() {
    return MeasureUnit(10, 0);
}

MeasureUnit *MeasureUnit::createNewton(UErrorCode &status) {
    return MeasureUnit::create(10, 1, status);
}

MeasureUnit MeasureUnit::getNewton() {
    return MeasureUnit(10, 1);
}

MeasureUnit *MeasureUnit::createPoundForce(UErrorCode &status) {
    return MeasureUnit::create(10, 2, status);
}

MeasureUnit MeasureUnit::getPoundForce() {
    return MeasureUnit(10, 2);
}

MeasureUnit *MeasureUnit::createGigahertz(UErrorCode &status) {
    return MeasureUnit::create(11, 0, status);
}

MeasureUnit MeasureUnit::getGigahertz() {
    return MeasureUnit(11, 0);
}

MeasureUnit *MeasureUnit::createHertz(UErrorCode &status) {
    return MeasureUnit::create(11, 1, status);
}

MeasureUnit MeasureUnit::getHertz() {
    return MeasureUnit(11, 1);
}

MeasureUnit *MeasureUnit::createKilohertz(UErrorCode &status) {
    return MeasureUnit::create(11, 2, status);
}

MeasureUnit MeasureUnit::getKilohertz() {
    return MeasureUnit(11, 2);
}

MeasureUnit *MeasureUnit::createMegahertz(UErrorCode &status) {
    return MeasureUnit::create(11, 3, status);
}

MeasureUnit MeasureUnit::getMegahertz() {
    return MeasureUnit(11, 3);
}

MeasureUnit *MeasureUnit::createDot(UErrorCode &status) {
    return MeasureUnit::create(12, 0, status);
}

MeasureUnit MeasureUnit::getDot() {
    return MeasureUnit(12, 0);
}

MeasureUnit *MeasureUnit::createDotPerCentimeter(UErrorCode &status) {
    return MeasureUnit::create(12, 1, status);
}

MeasureUnit MeasureUnit::getDotPerCentimeter() {
    return MeasureUnit(12, 1);
}

MeasureUnit *MeasureUnit::createDotPerInch(UErrorCode &status) {
    return MeasureUnit::create(12, 2, status);
}

MeasureUnit MeasureUnit::getDotPerInch() {
    return MeasureUnit(12, 2);
}

MeasureUnit *MeasureUnit::createEm(UErrorCode &status) {
    return MeasureUnit::create(12, 3, status);
}

MeasureUnit MeasureUnit::getEm() {
    return MeasureUnit(12, 3);
}

MeasureUnit *MeasureUnit::createMegapixel(UErrorCode &status) {
    return MeasureUnit::create(12, 4, status);
}

MeasureUnit MeasureUnit::getMegapixel() {
    return MeasureUnit(12, 4);
}

MeasureUnit *MeasureUnit::createPixel(UErrorCode &status) {
    return MeasureUnit::create(12, 5, status);
}

MeasureUnit MeasureUnit::getPixel() {
    return MeasureUnit(12, 5);
}

MeasureUnit *MeasureUnit::createPixelPerCentimeter(UErrorCode &status) {
    return MeasureUnit::create(12, 6, status);
}

MeasureUnit MeasureUnit::getPixelPerCentimeter() {
    return MeasureUnit(12, 6);
}

MeasureUnit *MeasureUnit::createPixelPerInch(UErrorCode &status) {
    return MeasureUnit::create(12, 7, status);
}

MeasureUnit MeasureUnit::getPixelPerInch() {
    return MeasureUnit(12, 7);
}

MeasureUnit *MeasureUnit::createAstronomicalUnit(UErrorCode &status) {
    return MeasureUnit::create(13, 0, status);
}

MeasureUnit MeasureUnit::getAstronomicalUnit() {
    return MeasureUnit(13, 0);
}

MeasureUnit *MeasureUnit::createCentimeter(UErrorCode &status) {
    return MeasureUnit::create(13, 1, status);
}

MeasureUnit MeasureUnit::getCentimeter() {
    return MeasureUnit(13, 1);
}

MeasureUnit *MeasureUnit::createDecimeter(UErrorCode &status) {
    return MeasureUnit::create(13, 2, status);
}

MeasureUnit MeasureUnit::getDecimeter() {
    return MeasureUnit(13, 2);
}

MeasureUnit *MeasureUnit::createEarthRadius(UErrorCode &status) {
    return MeasureUnit::create(13, 3, status);
}

MeasureUnit MeasureUnit::getEarthRadius() {
    return MeasureUnit(13, 3);
}

MeasureUnit *MeasureUnit::createFathom(UErrorCode &status) {
    return MeasureUnit::create(13, 4, status);
}

MeasureUnit MeasureUnit::getFathom() {
    return MeasureUnit(13, 4);
}

MeasureUnit *MeasureUnit::createFoot(UErrorCode &status) {
    return MeasureUnit::create(13, 5, status);
}

MeasureUnit MeasureUnit::getFoot() {
    return MeasureUnit(13, 5);
}

MeasureUnit *MeasureUnit::createFurlong(UErrorCode &status) {
    return MeasureUnit::create(13, 6, status);
}

MeasureUnit MeasureUnit::getFurlong() {
    return MeasureUnit(13, 6);
}

MeasureUnit *MeasureUnit::createInch(UErrorCode &status) {
    return MeasureUnit::create(13, 7, status);
}

MeasureUnit MeasureUnit::getInch() {
    return MeasureUnit(13, 7);
}

MeasureUnit *MeasureUnit::createKilometer(UErrorCode &status) {
    return MeasureUnit::create(13, 8, status);
}

MeasureUnit MeasureUnit::getKilometer() {
    return MeasureUnit(13, 8);
}

MeasureUnit *MeasureUnit::createLightYear(UErrorCode &status) {
    return MeasureUnit::create(13, 9, status);
}

MeasureUnit MeasureUnit::getLightYear() {
    return MeasureUnit(13, 9);
}

MeasureUnit *MeasureUnit::createMeter(UErrorCode &status) {
    return MeasureUnit::create(13, 10, status);
}

MeasureUnit MeasureUnit::getMeter() {
    return MeasureUnit(13, 10);
}

MeasureUnit *MeasureUnit::createMicrometer(UErrorCode &status) {
    return MeasureUnit::create(13, 11, status);
}

MeasureUnit MeasureUnit::getMicrometer() {
    return MeasureUnit(13, 11);
}

MeasureUnit *MeasureUnit::createMile(UErrorCode &status) {
    return MeasureUnit::create(13, 12, status);
}

MeasureUnit MeasureUnit::getMile() {
    return MeasureUnit(13, 12);
}

MeasureUnit *MeasureUnit::createMileScandinavian(UErrorCode &status) {
    return MeasureUnit::create(13, 13, status);
}

MeasureUnit MeasureUnit::getMileScandinavian() {
    return MeasureUnit(13, 13);
}

MeasureUnit *MeasureUnit::createMillimeter(UErrorCode &status) {
    return MeasureUnit::create(13, 14, status);
}

MeasureUnit MeasureUnit::getMillimeter() {
    return MeasureUnit(13, 14);
}

MeasureUnit *MeasureUnit::createNanometer(UErrorCode &status) {
    return MeasureUnit::create(13, 15, status);
}

MeasureUnit MeasureUnit::getNanometer() {
    return MeasureUnit(13, 15);
}

MeasureUnit *MeasureUnit::createNauticalMile(UErrorCode &status) {
    return MeasureUnit::create(13, 16, status);
}

MeasureUnit MeasureUnit::getNauticalMile() {
    return MeasureUnit(13, 16);
}

MeasureUnit *MeasureUnit::createParsec(UErrorCode &status) {
    return MeasureUnit::create(13, 17, status);
}

MeasureUnit MeasureUnit::getParsec() {
    return MeasureUnit(13, 17);
}

MeasureUnit *MeasureUnit::createPicometer(UErrorCode &status) {
    return MeasureUnit::create(13, 18, status);
}

MeasureUnit MeasureUnit::getPicometer() {
    return MeasureUnit(13, 18);
}

MeasureUnit *MeasureUnit::createPoint(UErrorCode &status) {
    return MeasureUnit::create(13, 19, status);
}

MeasureUnit MeasureUnit::getPoint() {
    return MeasureUnit(13, 19);
}

MeasureUnit *MeasureUnit::createSolarRadius(UErrorCode &status) {
    return MeasureUnit::create(13, 20, status);
}

MeasureUnit MeasureUnit::getSolarRadius() {
    return MeasureUnit(13, 20);
}

MeasureUnit *MeasureUnit::createYard(UErrorCode &status) {
    return MeasureUnit::create(13, 21, status);
}

MeasureUnit MeasureUnit::getYard() {
    return MeasureUnit(13, 21);
}

MeasureUnit *MeasureUnit::createCandela(UErrorCode &status) {
    return MeasureUnit::create(14, 0, status);
}

MeasureUnit MeasureUnit::getCandela() {
    return MeasureUnit(14, 0);
}

MeasureUnit *MeasureUnit::createLumen(UErrorCode &status) {
    return MeasureUnit::create(14, 1, status);
}

MeasureUnit MeasureUnit::getLumen() {
    return MeasureUnit(14, 1);
}

MeasureUnit *MeasureUnit::createLux(UErrorCode &status) {
    return MeasureUnit::create(14, 2, status);
}

MeasureUnit MeasureUnit::getLux() {
    return MeasureUnit(14, 2);
}

MeasureUnit *MeasureUnit::createSolarLuminosity(UErrorCode &status) {
    return MeasureUnit::create(14, 3, status);
}

MeasureUnit MeasureUnit::getSolarLuminosity() {
    return MeasureUnit(14, 3);
}

MeasureUnit *MeasureUnit::createCarat(UErrorCode &status) {
    return MeasureUnit::create(15, 0, status);
}

MeasureUnit MeasureUnit::getCarat() {
    return MeasureUnit(15, 0);
}

MeasureUnit *MeasureUnit::createDalton(UErrorCode &status) {
    return MeasureUnit::create(15, 1, status);
}

MeasureUnit MeasureUnit::getDalton() {
    return MeasureUnit(15, 1);
}

MeasureUnit *MeasureUnit::createEarthMass(UErrorCode &status) {
    return MeasureUnit::create(15, 2, status);
}

MeasureUnit MeasureUnit::getEarthMass() {
    return MeasureUnit(15, 2);
}

MeasureUnit *MeasureUnit::createGrain(UErrorCode &status) {
    return MeasureUnit::create(15, 3, status);
}

MeasureUnit MeasureUnit::getGrain() {
    return MeasureUnit(15, 3);
}

MeasureUnit *MeasureUnit::createGram(UErrorCode &status) {
    return MeasureUnit::create(15, 4, status);
}

MeasureUnit MeasureUnit::getGram() {
    return MeasureUnit(15, 4);
}

MeasureUnit *MeasureUnit::createKilogram(UErrorCode &status) {
    return MeasureUnit::create(15, 5, status);
}

MeasureUnit MeasureUnit::getKilogram() {
    return MeasureUnit(15, 5);
}

MeasureUnit *MeasureUnit::createMetricTon(UErrorCode &status) {
    return MeasureUnit::create(15, 14, status);
}

MeasureUnit MeasureUnit::getMetricTon() {
    return MeasureUnit(15, 14);
}

MeasureUnit *MeasureUnit::createMicrogram(UErrorCode &status) {
    return MeasureUnit::create(15, 6, status);
}

MeasureUnit MeasureUnit::getMicrogram() {
    return MeasureUnit(15, 6);
}

MeasureUnit *MeasureUnit::createMilligram(UErrorCode &status) {
    return MeasureUnit::create(15, 7, status);
}

MeasureUnit MeasureUnit::getMilligram() {
    return MeasureUnit(15, 7);
}

MeasureUnit *MeasureUnit::createOunce(UErrorCode &status) {
    return MeasureUnit::create(15, 8, status);
}

MeasureUnit MeasureUnit::getOunce() {
    return MeasureUnit(15, 8);
}

MeasureUnit *MeasureUnit::createOunceTroy(UErrorCode &status) {
    return MeasureUnit::create(15, 9, status);
}

MeasureUnit MeasureUnit::getOunceTroy() {
    return MeasureUnit(15, 9);
}

MeasureUnit *MeasureUnit::createPound(UErrorCode &status) {
    return MeasureUnit::create(15, 10, status);
}

MeasureUnit MeasureUnit::getPound() {
    return MeasureUnit(15, 10);
}

MeasureUnit *MeasureUnit::createSolarMass(UErrorCode &status) {
    return MeasureUnit::create(15, 11, status);
}

MeasureUnit MeasureUnit::getSolarMass() {
    return MeasureUnit(15, 11);
}

MeasureUnit *MeasureUnit::createStone(UErrorCode &status) {
    return MeasureUnit::create(15, 12, status);
}

MeasureUnit MeasureUnit::getStone() {
    return MeasureUnit(15, 12);
}

MeasureUnit *MeasureUnit::createTon(UErrorCode &status) {
    return MeasureUnit::create(15, 13, status);
}

MeasureUnit MeasureUnit::getTon() {
    return MeasureUnit(15, 13);
}

MeasureUnit *MeasureUnit::createTonne(UErrorCode &status) {
    return MeasureUnit::create(15, 14, status);
}

MeasureUnit MeasureUnit::getTonne() {
    return MeasureUnit(15, 14);
}

MeasureUnit *MeasureUnit::createGigawatt(UErrorCode &status) {
    return MeasureUnit::create(17, 0, status);
}

MeasureUnit MeasureUnit::getGigawatt() {
    return MeasureUnit(17, 0);
}

MeasureUnit *MeasureUnit::createHorsepower(UErrorCode &status) {
    return MeasureUnit::create(17, 1, status);
}

MeasureUnit MeasureUnit::getHorsepower() {
    return MeasureUnit(17, 1);
}

MeasureUnit *MeasureUnit::createKilowatt(UErrorCode &status) {
    return MeasureUnit::create(17, 2, status);
}

MeasureUnit MeasureUnit::getKilowatt() {
    return MeasureUnit(17, 2);
}

MeasureUnit *MeasureUnit::createMegawatt(UErrorCode &status) {
    return MeasureUnit::create(17, 3, status);
}

MeasureUnit MeasureUnit::getMegawatt() {
    return MeasureUnit(17, 3);
}

MeasureUnit *MeasureUnit::createMilliwatt(UErrorCode &status) {
    return MeasureUnit::create(17, 4, status);
}

MeasureUnit MeasureUnit::getMilliwatt() {
    return MeasureUnit(17, 4);
}

MeasureUnit *MeasureUnit::createWatt(UErrorCode &status) {
    return MeasureUnit::create(17, 5, status);
}

MeasureUnit MeasureUnit::getWatt() {
    return MeasureUnit(17, 5);
}

MeasureUnit *MeasureUnit::createAtmosphere(UErrorCode &status) {
    return MeasureUnit::create(18, 0, status);
}

MeasureUnit MeasureUnit::getAtmosphere() {
    return MeasureUnit(18, 0);
}

MeasureUnit *MeasureUnit::createBar(UErrorCode &status) {
    return MeasureUnit::create(18, 1, status);
}

MeasureUnit MeasureUnit::getBar() {
    return MeasureUnit(18, 1);
}

MeasureUnit *MeasureUnit::createGasolineEnergyDensity(UErrorCode &status) {
    return MeasureUnit::create(18, 2, status);
}

MeasureUnit MeasureUnit::getGasolineEnergyDensity() {
    return MeasureUnit(18, 2);
}

MeasureUnit *MeasureUnit::createHectopascal(UErrorCode &status) {
    return MeasureUnit::create(18, 3, status);
}

MeasureUnit MeasureUnit::getHectopascal() {
    return MeasureUnit(18, 3);
}

MeasureUnit *MeasureUnit::createInchHg(UErrorCode &status) {
    return MeasureUnit::create(18, 4, status);
}

MeasureUnit MeasureUnit::getInchHg() {
    return MeasureUnit(18, 4);
}

MeasureUnit *MeasureUnit::createKilopascal(UErrorCode &status) {
    return MeasureUnit::create(18, 5, status);
}

MeasureUnit MeasureUnit::getKilopascal() {
    return MeasureUnit(18, 5);
}

MeasureUnit *MeasureUnit::createMegapascal(UErrorCode &status) {
    return MeasureUnit::create(18, 6, status);
}

MeasureUnit MeasureUnit::getMegapascal() {
    return MeasureUnit(18, 6);
}

MeasureUnit *MeasureUnit::createMillibar(UErrorCode &status) {
    return MeasureUnit::create(18, 7, status);
}

MeasureUnit MeasureUnit::getMillibar() {
    return MeasureUnit(18, 7);
}

MeasureUnit *MeasureUnit::createMillimeterOfMercury(UErrorCode &status) {
    return MeasureUnit::create(18, 8, status);
}

MeasureUnit MeasureUnit::getMillimeterOfMercury() {
    return MeasureUnit(18, 8);
}

MeasureUnit *MeasureUnit::createPascal(UErrorCode &status) {
    return MeasureUnit::create(18, 9, status);
}

MeasureUnit MeasureUnit::getPascal() {
    return MeasureUnit(18, 9);
}

MeasureUnit *MeasureUnit::createPoundPerSquareInch(UErrorCode &status) {
    return MeasureUnit::create(18, 10, status);
}

MeasureUnit MeasureUnit::getPoundPerSquareInch() {
    return MeasureUnit(18, 10);
}

MeasureUnit *MeasureUnit::createBeaufort(UErrorCode &status) {
    return MeasureUnit::create(19, 0, status);
}

MeasureUnit MeasureUnit::getBeaufort() {
    return MeasureUnit(19, 0);
}

MeasureUnit *MeasureUnit::createKilometerPerHour(UErrorCode &status) {
    return MeasureUnit::create(19, 1, status);
}

MeasureUnit MeasureUnit::getKilometerPerHour() {
    return MeasureUnit(19, 1);
}

MeasureUnit *MeasureUnit::createKnot(UErrorCode &status) {
    return MeasureUnit::create(19, 2, status);
}

MeasureUnit MeasureUnit::getKnot() {
    return MeasureUnit(19, 2);
}

MeasureUnit *MeasureUnit::createMeterPerSecond(UErrorCode &status) {
    return MeasureUnit::create(19, 3, status);
}

MeasureUnit MeasureUnit::getMeterPerSecond() {
    return MeasureUnit(19, 3);
}

MeasureUnit *MeasureUnit::createMilePerHour(UErrorCode &status) {
    return MeasureUnit::create(19, 4, status);
}

MeasureUnit MeasureUnit::getMilePerHour() {
    return MeasureUnit(19, 4);
}

MeasureUnit *MeasureUnit::createCelsius(UErrorCode &status) {
    return MeasureUnit::create(20, 0, status);
}

MeasureUnit MeasureUnit::getCelsius() {
    return MeasureUnit(20, 0);
}

MeasureUnit *MeasureUnit::createFahrenheit(UErrorCode &status) {
    return MeasureUnit::create(20, 1, status);
}

MeasureUnit MeasureUnit::getFahrenheit() {
    return MeasureUnit(20, 1);
}

MeasureUnit *MeasureUnit::createGenericTemperature(UErrorCode &status) {
    return MeasureUnit::create(20, 2, status);
}

MeasureUnit MeasureUnit::getGenericTemperature() {
    return MeasureUnit(20, 2);
}

MeasureUnit *MeasureUnit::createKelvin(UErrorCode &status) {
    return MeasureUnit::create(20, 3, status);
}

MeasureUnit MeasureUnit::getKelvin() {
    return MeasureUnit(20, 3);
}

MeasureUnit *MeasureUnit::createNewtonMeter(UErrorCode &status) {
    return MeasureUnit::create(21, 0, status);
}

MeasureUnit MeasureUnit::getNewtonMeter() {
    return MeasureUnit(21, 0);
}

MeasureUnit *MeasureUnit::createPoundFoot(UErrorCode &status) {
    return MeasureUnit::create(21, 1, status);
}

MeasureUnit MeasureUnit::getPoundFoot() {
    return MeasureUnit(21, 1);
}

MeasureUnit *MeasureUnit::createAcreFoot(UErrorCode &status) {
    return MeasureUnit::create(22, 0, status);
}

MeasureUnit MeasureUnit::getAcreFoot() {
    return MeasureUnit(22, 0);
}

MeasureUnit *MeasureUnit::createBarrel(UErrorCode &status) {
    return MeasureUnit::create(22, 1, status);
}

MeasureUnit MeasureUnit::getBarrel() {
    return MeasureUnit(22, 1);
}

MeasureUnit *MeasureUnit::createBushel(UErrorCode &status) {
    return MeasureUnit::create(22, 2, status);
}

MeasureUnit MeasureUnit::getBushel() {
    return MeasureUnit(22, 2);
}

MeasureUnit *MeasureUnit::createCentiliter(UErrorCode &status) {
    return MeasureUnit::create(22, 3, status);
}

MeasureUnit MeasureUnit::getCentiliter() {
    return MeasureUnit(22, 3);
}

MeasureUnit *MeasureUnit::createCubicCentimeter(UErrorCode &status) {
    return MeasureUnit::create(22, 4, status);
}

MeasureUnit MeasureUnit::getCubicCentimeter() {
    return MeasureUnit(22, 4);
}

MeasureUnit *MeasureUnit::createCubicFoot(UErrorCode &status) {
    return MeasureUnit::create(22, 5, status);
}

MeasureUnit MeasureUnit::getCubicFoot() {
    return MeasureUnit(22, 5);
}

MeasureUnit *MeasureUnit::createCubicInch(UErrorCode &status) {
    return MeasureUnit::create(22, 6, status);
}

MeasureUnit MeasureUnit::getCubicInch() {
    return MeasureUnit(22, 6);
}

MeasureUnit *MeasureUnit::createCubicKilometer(UErrorCode &status) {
    return MeasureUnit::create(22, 7, status);
}

MeasureUnit MeasureUnit::getCubicKilometer() {
    return MeasureUnit(22, 7);
}

MeasureUnit *MeasureUnit::createCubicMeter(UErrorCode &status) {
    return MeasureUnit::create(22, 8, status);
}

MeasureUnit MeasureUnit::getCubicMeter() {
    return MeasureUnit(22, 8);
}

MeasureUnit *MeasureUnit::createCubicMile(UErrorCode &status) {
    return MeasureUnit::create(22, 9, status);
}

MeasureUnit MeasureUnit::getCubicMile() {
    return MeasureUnit(22, 9);
}

MeasureUnit *MeasureUnit::createCubicYard(UErrorCode &status) {
    return MeasureUnit::create(22, 10, status);
}

MeasureUnit MeasureUnit::getCubicYard() {
    return MeasureUnit(22, 10);
}

MeasureUnit *MeasureUnit::createCup(UErrorCode &status) {
    return MeasureUnit::create(22, 11, status);
}

MeasureUnit MeasureUnit::getCup() {
    return MeasureUnit(22, 11);
}

MeasureUnit *MeasureUnit::createCupMetric(UErrorCode &status) {
    return MeasureUnit::create(22, 12, status);
}

MeasureUnit MeasureUnit::getCupMetric() {
    return MeasureUnit(22, 12);
}

MeasureUnit *MeasureUnit::createDeciliter(UErrorCode &status) {
    return MeasureUnit::create(22, 13, status);
}

MeasureUnit MeasureUnit::getDeciliter() {
    return MeasureUnit(22, 13);
}

MeasureUnit *MeasureUnit::createDessertSpoon(UErrorCode &status) {
    return MeasureUnit::create(22, 14, status);
}

MeasureUnit MeasureUnit::getDessertSpoon() {
    return MeasureUnit(22, 14);
}

MeasureUnit *MeasureUnit::createDessertSpoonImperial(UErrorCode &status) {
    return MeasureUnit::create(22, 15, status);
}

MeasureUnit MeasureUnit::getDessertSpoonImperial() {
    return MeasureUnit(22, 15);
}

MeasureUnit *MeasureUnit::createDram(UErrorCode &status) {
    return MeasureUnit::create(22, 16, status);
}

MeasureUnit MeasureUnit::getDram() {
    return MeasureUnit(22, 16);
}

MeasureUnit *MeasureUnit::createDrop(UErrorCode &status) {
    return MeasureUnit::create(22, 17, status);
}

MeasureUnit MeasureUnit::getDrop() {
    return MeasureUnit(22, 17);
}

MeasureUnit *MeasureUnit::createFluidOunce(UErrorCode &status) {
    return MeasureUnit::create(22, 18, status);
}

MeasureUnit MeasureUnit::getFluidOunce() {
    return MeasureUnit(22, 18);
}

MeasureUnit *MeasureUnit::createFluidOunceImperial(UErrorCode &status) {
    return MeasureUnit::create(22, 19, status);
}

MeasureUnit MeasureUnit::getFluidOunceImperial() {
    return MeasureUnit(22, 19);
}

MeasureUnit *MeasureUnit::createGallon(UErrorCode &status) {
    return MeasureUnit::create(22, 20, status);
}

MeasureUnit MeasureUnit::getGallon() {
    return MeasureUnit(22, 20);
}

MeasureUnit *MeasureUnit::createGallonImperial(UErrorCode &status) {
    return MeasureUnit::create(22, 21, status);
}

MeasureUnit MeasureUnit::getGallonImperial() {
    return MeasureUnit(22, 21);
}

MeasureUnit *MeasureUnit::createHectoliter(UErrorCode &status) {
    return MeasureUnit::create(22, 22, status);
}

MeasureUnit MeasureUnit::getHectoliter() {
    return MeasureUnit(22, 22);
}

MeasureUnit *MeasureUnit::createJigger(UErrorCode &status) {
    return MeasureUnit::create(22, 23, status);
}

MeasureUnit MeasureUnit::getJigger() {
    return MeasureUnit(22, 23);
}

MeasureUnit *MeasureUnit::createLiter(UErrorCode &status) {
    return MeasureUnit::create(22, 24, status);
}

MeasureUnit MeasureUnit::getLiter() {
    return MeasureUnit(22, 24);
}

MeasureUnit *MeasureUnit::createMegaliter(UErrorCode &status) {
    return MeasureUnit::create(22, 25, status);
}

MeasureUnit MeasureUnit::getMegaliter() {
    return MeasureUnit(22, 25);
}

MeasureUnit *MeasureUnit::createMilliliter(UErrorCode &status) {
    return MeasureUnit::create(22, 26, status);
}

MeasureUnit MeasureUnit::getMilliliter() {
    return MeasureUnit(22, 26);
}

MeasureUnit *MeasureUnit::createPinch(UErrorCode &status) {
    return MeasureUnit::create(22, 27, status);
}

MeasureUnit MeasureUnit::getPinch() {
    return MeasureUnit(22, 27);
}

MeasureUnit *MeasureUnit::createPint(UErrorCode &status) {
    return MeasureUnit::create(22, 28, status);
}

MeasureUnit MeasureUnit::getPint() {
    return MeasureUnit(22, 28);
}

MeasureUnit *MeasureUnit::createPintMetric(UErrorCode &status) {
    return MeasureUnit::create(22, 29, status);
}

MeasureUnit MeasureUnit::getPintMetric() {
    return MeasureUnit(22, 29);
}

MeasureUnit *MeasureUnit::createQuart(UErrorCode &status) {
    return MeasureUnit::create(22, 30, status);
}

MeasureUnit MeasureUnit::getQuart() {
    return MeasureUnit(22, 30);
}

MeasureUnit *MeasureUnit::createQuartImperial(UErrorCode &status) {
    return MeasureUnit::create(22, 31, status);
}

MeasureUnit MeasureUnit::getQuartImperial() {
    return MeasureUnit(22, 31);
}

MeasureUnit *MeasureUnit::createTablespoon(UErrorCode &status) {
    return MeasureUnit::create(22, 32, status);
}

MeasureUnit MeasureUnit::getTablespoon() {
    return MeasureUnit(22, 32);
}

MeasureUnit *MeasureUnit::createTeaspoon(UErrorCode &status) {
    return MeasureUnit::create(22, 33, status);
}

MeasureUnit MeasureUnit::getTeaspoon() {
    return MeasureUnit(22, 33);
}

// End generated code for measunit.cpp

static int32_t binarySearch(
        const char * const * array, int32_t start, int32_t end, StringPiece key) {
    while (start < end) {
        int32_t mid = (start + end) / 2;
        int32_t cmp = StringPiece(array[mid]).compare(key);
        if (cmp < 0) {
            start = mid + 1;
            continue;
        }
        if (cmp == 0) {
            return mid;
        }
        end = mid;
    }
    return -1;
}

MeasureUnit::MeasureUnit() : MeasureUnit(kBaseTypeIdx, kBaseSubTypeIdx) {
}

MeasureUnit::MeasureUnit(int32_t typeId, int32_t subTypeId)
        : fImpl(nullptr), fSubTypeId(subTypeId), fTypeId(typeId) {
}

MeasureUnit::MeasureUnit(const MeasureUnit &other)
        : fImpl(nullptr) {
    *this = other;
}

MeasureUnit::MeasureUnit(MeasureUnit &&other) noexcept
        : fImpl(other.fImpl),
        fSubTypeId(other.fSubTypeId),
        fTypeId(other.fTypeId) {
    other.fImpl = nullptr;
}

MeasureUnit::MeasureUnit(MeasureUnitImpl&& impl)
        : fImpl(nullptr), fSubTypeId(-1), fTypeId(-1) {
    if (!findBySubType(impl.identifier.toStringPiece(), this)) {
        fImpl = new MeasureUnitImpl(std::move(impl));
    }
}

MeasureUnit &MeasureUnit::operator=(const MeasureUnit &other) {
    if (this == &other) {
        return *this;
    }
    delete fImpl;
    if (other.fImpl) {
        ErrorCode localStatus;
        fImpl = new MeasureUnitImpl(other.fImpl->copy(localStatus));
        if (!fImpl || localStatus.isFailure()) {
            // Unrecoverable allocation error; set to the default unit
            *this = MeasureUnit();
            return *this;
        }
    } else {
        fImpl = nullptr;
    }
    fTypeId = other.fTypeId;
    fSubTypeId = other.fSubTypeId;
    return *this;
}

MeasureUnit &MeasureUnit::operator=(MeasureUnit &&other) noexcept {
    if (this == &other) {
        return *this;
    }
    delete fImpl;
    fImpl = other.fImpl;
    other.fImpl = nullptr;
    fTypeId = other.fTypeId;
    fSubTypeId = other.fSubTypeId;
    return *this;
}

MeasureUnit *MeasureUnit::clone() const {
    return new MeasureUnit(*this);
}

MeasureUnit::~MeasureUnit() {
    if (fImpl != nullptr) {
        delete fImpl;
        fImpl = nullptr;
    }
}

const char *MeasureUnit::getType() const {
    // We have a type & subtype only if fTypeId is present.
    if (fTypeId == -1) {
        return "";
    }
    return gTypes[fTypeId];
}

const char *MeasureUnit::getSubtype() const {
    // We have a type & subtype only if fTypeId is present.
    if (fTypeId == -1) {
        return "";
    }
    return getIdentifier();
}

const char *MeasureUnit::getIdentifier() const {
    return fImpl ? fImpl->identifier.data() : gSubTypes[getOffset()];
}

bool MeasureUnit::operator==(const UObject& other) const {
    if (this == &other) {  // Same object, equal
        return true;
    }
    if (typeid(*this) != typeid(other)) { // Different types, not equal
        return false;
    }
    const MeasureUnit &rhs = static_cast<const MeasureUnit&>(other);
    return uprv_strcmp(getIdentifier(), rhs.getIdentifier()) == 0;
}

int32_t MeasureUnit::getAvailable(
        MeasureUnit *dest,
        int32_t destCapacity,
        UErrorCode &errorCode) {
    if (U_FAILURE(errorCode)) {
        return 0;
    }
    if (destCapacity < UPRV_LENGTHOF(gSubTypes)) {
        errorCode = U_BUFFER_OVERFLOW_ERROR;
        return UPRV_LENGTHOF(gSubTypes);
    }
    int32_t idx = 0;
    for (int32_t typeIdx = 0; typeIdx < UPRV_LENGTHOF(gTypes); ++typeIdx) {
        int32_t len = gOffsets[typeIdx + 1] - gOffsets[typeIdx];
        for (int32_t subTypeIdx = 0; subTypeIdx < len; ++subTypeIdx) {
            dest[idx].setTo(typeIdx, subTypeIdx);
            ++idx;
        }
    }
    U_ASSERT(idx == UPRV_LENGTHOF(gSubTypes));
    return UPRV_LENGTHOF(gSubTypes);
}

int32_t MeasureUnit::getAvailable(
        const char *type,
        MeasureUnit *dest,
        int32_t destCapacity,
        UErrorCode &errorCode) {
    if (U_FAILURE(errorCode)) {
        return 0;
    }
    int32_t typeIdx = binarySearch(gTypes, 0, UPRV_LENGTHOF(gTypes), type);
    if (typeIdx == -1) {
        return 0;
    }
    int32_t len = gOffsets[typeIdx + 1] - gOffsets[typeIdx];
    if (destCapacity < len) {
        errorCode = U_BUFFER_OVERFLOW_ERROR;
        return len;
    }
    for (int subTypeIdx = 0; subTypeIdx < len; ++subTypeIdx) {
        dest[subTypeIdx].setTo(typeIdx, subTypeIdx);
    }
    return len;
}

StringEnumeration* MeasureUnit::getAvailableTypes(UErrorCode &errorCode) {
    UEnumeration *uenum = uenum_openCharStringsEnumeration(
            gTypes, UPRV_LENGTHOF(gTypes), &errorCode);
    if (U_FAILURE(errorCode)) {
        uenum_close(uenum);
        return nullptr;
    }
    StringEnumeration *result = new UStringEnumeration(uenum);
    if (result == nullptr) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        uenum_close(uenum);
        return nullptr;
    }
    return result;
}

bool MeasureUnit::findBySubType(StringPiece subType, MeasureUnit* output) {
    // Sanity checking kCurrencyOffset and final entry in gOffsets
    U_ASSERT(uprv_strcmp(gTypes[kCurrencyOffset], "currency") == 0);
    U_ASSERT(gOffsets[UPRV_LENGTHOF(gOffsets) - 1] == UPRV_LENGTHOF(gSubTypes));

    for (int32_t t = 0; t < UPRV_LENGTHOF(gOffsets) - 1; t++) {
        // Skip currency units
        if (t == kCurrencyOffset) {
            continue;
        }
        int32_t st = binarySearch(gSubTypes, gOffsets[t], gOffsets[t + 1], subType);
        if (st >= 0) {
            output->setTo(t, st - gOffsets[t]);
            return true;
        }
    }
    return false;
}

MeasureUnit *MeasureUnit::create(int typeId, int subTypeId, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    MeasureUnit *result = new MeasureUnit(typeId, subTypeId);
    if (result == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
    }
    return result;
}

void MeasureUnit::initTime(const char *timeId) {
    int32_t result = binarySearch(gTypes, 0, UPRV_LENGTHOF(gTypes), "duration");
    U_ASSERT(result != -1);
    fTypeId = result;
    result = binarySearch(gSubTypes, gOffsets[fTypeId], gOffsets[fTypeId + 1], timeId);
    U_ASSERT(result != -1);
    fSubTypeId = result - gOffsets[fTypeId];
}

void MeasureUnit::initCurrency(StringPiece isoCurrency) {
    int32_t result = binarySearch(gTypes, 0, UPRV_LENGTHOF(gTypes), "currency");
    U_ASSERT(result != -1);
    fTypeId = result;
    result = binarySearch(
            gSubTypes, gOffsets[fTypeId], gOffsets[fTypeId + 1], isoCurrency);
    if (result == -1) {
        fImpl = new MeasureUnitImpl(MeasureUnitImpl::forCurrencyCode(isoCurrency));
        if (fImpl) {
            fSubTypeId = -1;
            return;
        }
        // malloc error: fall back to the undefined currency
        result = binarySearch(
            gSubTypes, gOffsets[fTypeId], gOffsets[fTypeId + 1], kDefaultCurrency8);
        U_ASSERT(result != -1);
    }
    fSubTypeId = result - gOffsets[fTypeId];
}

void MeasureUnit::setTo(int32_t typeId, int32_t subTypeId) {
    fTypeId = typeId;
    fSubTypeId = subTypeId;
    if (fImpl != nullptr) {
        delete fImpl;
        fImpl = nullptr;
    }
}

int32_t MeasureUnit::getOffset() const {
    if (fTypeId < 0 || fSubTypeId < 0) {
        return -1;
    }
    return gOffsets[fTypeId] + fSubTypeId;
}

MeasureUnitImpl MeasureUnitImpl::copy(UErrorCode &status) const {
    MeasureUnitImpl result;
    result.complexity = complexity;
    result.identifier.append(identifier, status);
    for (int32_t i = 0; i < singleUnits.length(); i++) {
        SingleUnitImpl *item = result.singleUnits.emplaceBack(*singleUnits[i]);
        if (!item) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return result;
        }
    }
    return result;
}

U_NAMESPACE_END

#endif /* !UNCONFIG_NO_FORMATTING */
