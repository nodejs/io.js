// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
 ******************************************************************************
 * Copyright (C) 2013, International Business Machines Corporation
 * and others. All Rights Reserved.
 ******************************************************************************
 *
 * File DANGICAL.CPP
 *****************************************************************************
 */

#include "chnsecal.h"
#include "dangical.h"

#if !UCONFIG_NO_FORMATTING

#include "astro.h" // CalendarCache
#include "gregoimp.h" // Math
#include "uassert.h"
#include "ucln_in.h"
#include "umutex.h"
#include "unicode/rbtz.h"
#include "unicode/tzrule.h"

// --- The cache --
// Lazy Creation & Access synchronized by class CalendarCache with a mutex.
static icu::CalendarCache *gWinterSolsticeCache = nullptr;
static icu::CalendarCache *gNewYearCache = nullptr;

// gAstronomerTimeZone
static icu::TimeZone *gAstronomerTimeZone = nullptr;
static icu::UInitOnce gAstronomerTimeZoneInitOnce {};

/**
 * The start year of the Korean traditional calendar (Dan-gi) is the inaugural
 * year of Dan-gun (BC 2333).
 */
static const int32_t DANGI_EPOCH_YEAR = -2332; // Gregorian year

U_CDECL_BEGIN
static UBool calendar_dangi_cleanup() {
    if (gWinterSolsticeCache) {
        delete gWinterSolsticeCache;
        gWinterSolsticeCache = nullptr;
    }
    if (gNewYearCache) {
        delete gNewYearCache;
        gNewYearCache = nullptr;
    }

    if (gAstronomerTimeZone) {
        delete gAstronomerTimeZone;
        gAstronomerTimeZone = nullptr;
    }
    gAstronomerTimeZoneInitOnce.reset();
    return true;
}
U_CDECL_END

U_NAMESPACE_BEGIN

// Implementation of the DangiCalendar class

//-------------------------------------------------------------------------
// Constructors...
//-------------------------------------------------------------------------

const TimeZone* getAstronomerTimeZone(UErrorCode &status);

DangiCalendar::DangiCalendar(const Locale& aLocale, UErrorCode& success)
:   ChineseCalendar(aLocale, success)
{
}

DangiCalendar::DangiCalendar (const DangiCalendar& other) 
: ChineseCalendar(other)
{
}

DangiCalendar::~DangiCalendar()
{
}

DangiCalendar*
DangiCalendar::clone() const
{
    return new DangiCalendar(*this);
}

const char *DangiCalendar::getType() const { 
    return "dangi";
}

/**
 * The time zone used for performing astronomical computations for
 * Dangi calendar. In Korea various timezones have been used historically 
 * (cf. http://www.math.snu.ac.kr/~kye/others/lunar.html): 
 *  
 *            - 1908/04/01: GMT+8 
 * 1908/04/01 - 1911/12/31: GMT+8.5 
 * 1912/01/01 - 1954/03/20: GMT+9 
 * 1954/03/21 - 1961/08/09: GMT+8.5 
 * 1961/08/10 -           : GMT+9 
 *  
 * Note that, in 1908-1911, the government did not apply the timezone change 
 * but used GMT+8. In addition, 1954-1961's timezone change does not affect 
 * the lunar date calculation. Therefore, the following simpler rule works: 
 *   
 * -1911: GMT+8 
 * 1912-: GMT+9 
 *  
 * Unfortunately, our astronomer's approximation doesn't agree with the 
 * references (http://www.math.snu.ac.kr/~kye/others/lunar.html and 
 * http://astro.kasi.re.kr/Life/ConvertSolarLunarForm.aspx?MenuID=115) 
 * in 1897/7/30. So the following ad hoc fix is used here: 
 *  
 *     -1896: GMT+8 
 *      1897: GMT+7 
 * 1898-1911: GMT+8 
 * 1912-    : GMT+9 
 */
static void U_CALLCONV initAstronomerTimeZone(UErrorCode &status) {
    U_ASSERT(gAstronomerTimeZone == nullptr);
    const UDate millis1897[] = { static_cast<UDate>((1897 - 1970) * 365 * kOneDay) }; // some days of error is not a problem here
    const UDate millis1898[] = { static_cast<UDate>((1898 - 1970) * 365 * kOneDay) }; // some days of error is not a problem here
    const UDate millis1912[] = { static_cast<UDate>((1912 - 1970) * 365 * kOneDay) }; // this doesn't create an issue for 1911/12/20
    LocalPointer<InitialTimeZoneRule> initialTimeZone(new InitialTimeZoneRule(
        UnicodeString(u"GMT+8"), 8*kOneHour, 0), status);

    LocalPointer<TimeZoneRule> rule1897(new TimeArrayTimeZoneRule(
        UnicodeString(u"Korean 1897"), 7*kOneHour, 0, millis1897, 1, DateTimeRule::STANDARD_TIME), status);

    LocalPointer<TimeZoneRule> rule1898to1911(new TimeArrayTimeZoneRule(
        UnicodeString(u"Korean 1898-1911"), 8*kOneHour, 0, millis1898, 1, DateTimeRule::STANDARD_TIME), status);

    LocalPointer<TimeZoneRule> ruleFrom1912(new TimeArrayTimeZoneRule(
        UnicodeString(u"Korean 1912-"), 9*kOneHour, 0, millis1912, 1, DateTimeRule::STANDARD_TIME), status);

    LocalPointer<RuleBasedTimeZone> zone(new RuleBasedTimeZone(
        UnicodeString(u"KOREA_ZONE"), initialTimeZone.orphan()), status); // adopts initialTimeZone

    if (U_FAILURE(status)) {
        return;
    }
    zone->addTransitionRule(rule1897.orphan(), status); // adopts rule1897
    zone->addTransitionRule(rule1898to1911.orphan(), status);
    zone->addTransitionRule(ruleFrom1912.orphan(), status);
    zone->complete(status);
    if (U_SUCCESS(status)) {
        gAstronomerTimeZone = zone.orphan();
    }
    ucln_i18n_registerCleanup(UCLN_I18N_DANGI_CALENDAR, calendar_dangi_cleanup);
}

const TimeZone* getAstronomerTimeZone(UErrorCode &status) {
    umtx_initOnce(gAstronomerTimeZoneInitOnce, &initAstronomerTimeZone, status);
    return gAstronomerTimeZone;
}

constexpr uint32_t kDangiRelatedYearDiff = -2333;

int32_t DangiCalendar::getRelatedYear(UErrorCode &status) const
{
    int32_t year = get(UCAL_EXTENDED_YEAR, status);
    if (U_FAILURE(status)) {
        return 0;
    }
    if (uprv_add32_overflow(year, kDangiRelatedYearDiff, &year)) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    return year;
}

void DangiCalendar::setRelatedYear(int32_t year)
{
    // set extended year
    set(UCAL_EXTENDED_YEAR, year - kDangiRelatedYearDiff);
}

ChineseCalendar::Setting DangiCalendar::getSetting(UErrorCode& status) const {
  return { DANGI_EPOCH_YEAR,
    getAstronomerTimeZone(status),
    &gWinterSolsticeCache, &gNewYearCache
  };
}

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(DangiCalendar)

U_NAMESPACE_END

#endif

