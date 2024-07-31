// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*   Copyright (C) 1996-2015, International Business Machines
*   Corporation and others.  All Rights Reserved.
*******************************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/udat.h"

#include "unicode/uloc.h"
#include "unicode/datefmt.h"
#include "unicode/timezone.h"
#include "unicode/smpdtfmt.h"
#include "unicode/fieldpos.h"
#include "unicode/parsepos.h"
#include "unicode/calendar.h"
#include "unicode/numfmt.h"
#include "unicode/dtfmtsym.h"
#include "unicode/ustring.h"
#include "unicode/udisplaycontext.h"
#include "unicode/ufieldpositer.h"
#include "cpputils.h"
#include "reldtfmt.h"
#include "umutex.h"

U_NAMESPACE_USE

/**
 * Verify that fmt is a SimpleDateFormat. Invalid error if not.
 * @param fmt the UDateFormat, definitely a DateFormat, maybe something else
 * @param status error code, will be set to failure if there is a failure or the fmt is nullptr.
 */
static void verifyIsSimpleDateFormat(const UDateFormat* fmt, UErrorCode *status) {
   if(U_SUCCESS(*status) &&
       dynamic_cast<const SimpleDateFormat*>(reinterpret_cast<const DateFormat*>(fmt))==nullptr) {
       *status = U_ILLEGAL_ARGUMENT_ERROR;
   }
}

// This mirrors the correspondence between the
// SimpleDateFormat::fgPatternIndexToDateFormatField and
// SimpleDateFormat::fgPatternIndexToCalendarField arrays.
static UCalendarDateFields gDateFieldMapping[] = {
    UCAL_ERA,                  // UDAT_ERA_FIELD = 0
    UCAL_YEAR,                 // UDAT_YEAR_FIELD = 1
    UCAL_MONTH,                // UDAT_MONTH_FIELD = 2
    UCAL_DATE,                 // UDAT_DATE_FIELD = 3
    UCAL_HOUR_OF_DAY,          // UDAT_HOUR_OF_DAY1_FIELD = 4
    UCAL_HOUR_OF_DAY,          // UDAT_HOUR_OF_DAY0_FIELD = 5
    UCAL_MINUTE,               // UDAT_MINUTE_FIELD = 6
    UCAL_SECOND,               // UDAT_SECOND_FIELD = 7
    UCAL_MILLISECOND,          // UDAT_FRACTIONAL_SECOND_FIELD = 8
    UCAL_DAY_OF_WEEK,          // UDAT_DAY_OF_WEEK_FIELD = 9
    UCAL_DAY_OF_YEAR,          // UDAT_DAY_OF_YEAR_FIELD = 10
    UCAL_DAY_OF_WEEK_IN_MONTH, // UDAT_DAY_OF_WEEK_IN_MONTH_FIELD = 11
    UCAL_WEEK_OF_YEAR,         // UDAT_WEEK_OF_YEAR_FIELD = 12
    UCAL_WEEK_OF_MONTH,        // UDAT_WEEK_OF_MONTH_FIELD = 13
    UCAL_AM_PM,                // UDAT_AM_PM_FIELD = 14
    UCAL_HOUR,                 // UDAT_HOUR1_FIELD = 15
    UCAL_HOUR,                 // UDAT_HOUR0_FIELD = 16
    UCAL_ZONE_OFFSET,          // UDAT_TIMEZONE_FIELD = 17
    UCAL_YEAR_WOY,             // UDAT_YEAR_WOY_FIELD = 18
    UCAL_DOW_LOCAL,            // UDAT_DOW_LOCAL_FIELD = 19
    UCAL_EXTENDED_YEAR,        // UDAT_EXTENDED_YEAR_FIELD = 20
    UCAL_JULIAN_DAY,           // UDAT_JULIAN_DAY_FIELD = 21
    UCAL_MILLISECONDS_IN_DAY,  // UDAT_MILLISECONDS_IN_DAY_FIELD = 22
    UCAL_ZONE_OFFSET,          // UDAT_TIMEZONE_RFC_FIELD = 23 (also UCAL_DST_OFFSET)
    UCAL_ZONE_OFFSET,          // UDAT_TIMEZONE_GENERIC_FIELD = 24 (also UCAL_DST_OFFSET)
    UCAL_DOW_LOCAL,            // UDAT_STANDALONE_DAY_FIELD = 25
    UCAL_MONTH,                // UDAT_STANDALONE_MONTH_FIELD = 26
    UCAL_MONTH,                // UDAT_QUARTER_FIELD = 27
    UCAL_MONTH,                // UDAT_STANDALONE_QUARTER_FIELD = 28
    UCAL_ZONE_OFFSET,          // UDAT_TIMEZONE_SPECIAL_FIELD = 29 (also UCAL_DST_OFFSET)
    UCAL_YEAR,                 // UDAT_YEAR_NAME_FIELD = 30
    UCAL_ZONE_OFFSET,          // UDAT_TIMEZONE_LOCALIZED_GMT_OFFSET_FIELD = 31 (also UCAL_DST_OFFSET)
    UCAL_ZONE_OFFSET,          // UDAT_TIMEZONE_ISO_FIELD = 32 (also UCAL_DST_OFFSET)
    UCAL_ZONE_OFFSET,          // UDAT_TIMEZONE_ISO_LOCAL_FIELD = 33 (also UCAL_DST_OFFSET)
    UCAL_EXTENDED_YEAR,        // UDAT_RELATED_YEAR_FIELD = 34 (not an exact match)
    UCAL_FIELD_COUNT,          // UDAT_AM_PM_MIDNIGHT_NOON_FIELD=35 (no match)
    UCAL_FIELD_COUNT,          // UDAT_FLEXIBLE_DAY_PERIOD_FIELD=36 (no match)
    UCAL_FIELD_COUNT,          // UDAT_TIME_SEPARATOR_FIELD = 37 (no match)
                               // UDAT_FIELD_COUNT = 38 as of ICU 67
    // UCAL_IS_LEAP_MONTH is not the target of a mapping
};

U_CAPI UCalendarDateFields U_EXPORT2
udat_toCalendarDateField(UDateFormatField field) UPRV_NO_SANITIZE_UNDEFINED {
  static_assert(UDAT_FIELD_COUNT == UPRV_LENGTHOF(gDateFieldMapping),
    "UDateFormatField and gDateFieldMapping should have the same number of entries and be kept in sync.");
  return (field >= UDAT_ERA_FIELD && field < UPRV_LENGTHOF(gDateFieldMapping))? gDateFieldMapping[field]: UCAL_FIELD_COUNT;
}

/* For now- one opener. */
static UDateFormatOpener gOpener = nullptr;

U_CAPI void U_EXPORT2
udat_registerOpener(UDateFormatOpener opener, UErrorCode *status)
{
  if(U_FAILURE(*status)) return;
  umtx_lock(nullptr);
  if(gOpener==nullptr) {
    gOpener = opener;
  } else {
    *status = U_ILLEGAL_ARGUMENT_ERROR;
  }
  umtx_unlock(nullptr);
}

U_CAPI UDateFormatOpener U_EXPORT2
udat_unregisterOpener(UDateFormatOpener opener, UErrorCode *status)
{
  if(U_FAILURE(*status)) return nullptr;
  UDateFormatOpener oldOpener = nullptr;
  umtx_lock(nullptr);
  if(gOpener==nullptr || gOpener!=opener) {
    *status = U_ILLEGAL_ARGUMENT_ERROR;
  } else {
    oldOpener=gOpener;
    gOpener=nullptr;
  }
  umtx_unlock(nullptr);
  return oldOpener;
}



U_CAPI UDateFormat* U_EXPORT2
udat_open(UDateFormatStyle  timeStyle,
          UDateFormatStyle  dateStyle,
          const char        *locale,
          const char16_t    *tzID,
          int32_t           tzIDLength,
          const char16_t    *pattern,
          int32_t           patternLength,
          UErrorCode        *status)
{
    DateFormat *fmt;
    if(U_FAILURE(*status)) {
        return nullptr;
    }
    if(gOpener!=nullptr) { // if it's registered
      fmt = (DateFormat*) (*gOpener)(timeStyle,dateStyle,locale,tzID,tzIDLength,pattern,patternLength,status);
      if(fmt!=nullptr) {
        return (UDateFormat*)fmt;
      } // else fall through.
    }
    if(timeStyle != UDAT_PATTERN) {
        if (locale == nullptr) {
            fmt = DateFormat::createDateTimeInstance((DateFormat::EStyle)dateStyle,
                (DateFormat::EStyle)timeStyle);
        }
        else {
            fmt = DateFormat::createDateTimeInstance((DateFormat::EStyle)dateStyle,
                (DateFormat::EStyle)timeStyle,
                Locale(locale));
        }
    }
    else {
        UnicodeString pat((UBool)(patternLength == -1), pattern, patternLength);

        if (locale == nullptr) {
            fmt = new SimpleDateFormat(pat, *status);
        }
        else {
            fmt = new SimpleDateFormat(pat, Locale(locale), *status);
        }
    }

    if(fmt == nullptr) {
        *status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    if (U_FAILURE(*status)) {
        delete fmt;
        return nullptr;
    }

    if (tzID != nullptr) {
        TimeZone *zone = TimeZone::createTimeZone(UnicodeString((UBool)(tzIDLength == -1), tzID, tzIDLength));
        if (zone == nullptr) {
            *status = U_MEMORY_ALLOCATION_ERROR;
            delete fmt;
            return nullptr;
        }
        fmt->adoptTimeZone(zone);
    }

    return (UDateFormat*)fmt;
}


U_CAPI void U_EXPORT2
udat_close(UDateFormat* format)
{
    if (format == nullptr) return;
    delete (DateFormat*)format;
}

U_CAPI UDateFormat* U_EXPORT2
udat_clone(const UDateFormat *fmt,
       UErrorCode *status)
{
    if (U_FAILURE(*status)) return nullptr;

    Format *res = ((DateFormat*)fmt)->clone();

    if (res == nullptr) {
        *status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }

    return (UDateFormat*) res;
}

U_CAPI int32_t U_EXPORT2
udat_format(    const    UDateFormat*    format,
        UDate           dateToFormat,
        char16_t*          result,
        int32_t         resultLength,
        UFieldPosition* position,
        UErrorCode*     status)
{
    if(U_FAILURE(*status)) {
        return -1;
    }
    if (result == nullptr ? resultLength != 0 : resultLength < 0) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return -1;
    }

    UnicodeString res;
    if (result != nullptr) {
        // nullptr destination for pure preflighting: empty dummy string
        // otherwise, alias the destination buffer
        res.setTo(result, 0, resultLength);
    }

    FieldPosition fp;

    if (position != nullptr)
        fp.setField(position->field);

    ((DateFormat*)format)->format(dateToFormat, res, fp);

    if (position != nullptr) {
        position->beginIndex = fp.getBeginIndex();
        position->endIndex = fp.getEndIndex();
    }

    return res.extract(result, resultLength, *status);
}

U_CAPI int32_t U_EXPORT2
udat_formatCalendar(const UDateFormat*  format,
        UCalendar*      calendar,
        char16_t*          result,
        int32_t         resultLength,
        UFieldPosition* position,
        UErrorCode*     status)
{
    if(U_FAILURE(*status)) {
        return -1;
    }
    if (result == nullptr ? resultLength != 0 : resultLength < 0) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return -1;
    }

    UnicodeString res;
    if (result != nullptr) {
        // nullptr destination for pure preflighting: empty dummy string
        // otherwise, alias the destination buffer
        res.setTo(result, 0, resultLength);
    }

    FieldPosition fp;

    if (position != nullptr)
        fp.setField(position->field);

    ((DateFormat*)format)->format(*(Calendar*)calendar, res, fp);

    if (position != nullptr) {
        position->beginIndex = fp.getBeginIndex();
        position->endIndex = fp.getEndIndex();
    }

    return res.extract(result, resultLength, *status);
}

U_CAPI int32_t U_EXPORT2
udat_formatForFields(    const    UDateFormat*    format,
        UDate           dateToFormat,
        char16_t*          result,
        int32_t         resultLength,
        UFieldPositionIterator* fpositer,
        UErrorCode*     status)
{
    if(U_FAILURE(*status)) {
        return -1;
    }
    if (result == nullptr ? resultLength != 0 : resultLength < 0) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return -1;
    }

    UnicodeString res;
    if (result != nullptr) {
        // nullptr destination for pure preflighting: empty dummy string
        // otherwise, alias the destination buffer
        res.setTo(result, 0, resultLength);
    }

    ((DateFormat*)format)->format(dateToFormat, res, (FieldPositionIterator*)fpositer, *status);

    return res.extract(result, resultLength, *status);
}

U_CAPI int32_t U_EXPORT2
udat_formatCalendarForFields(const UDateFormat*  format,
        UCalendar*      calendar,
        char16_t*          result,
        int32_t         resultLength,
        UFieldPositionIterator* fpositer,
        UErrorCode*     status)
{
    if(U_FAILURE(*status)) {
        return -1;
    }
    if (result == nullptr ? resultLength != 0 : resultLength < 0) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return -1;
    }

    UnicodeString res;
    if (result != nullptr) {
        // nullptr destination for pure preflighting: empty dummy string
        // otherwise, alias the destination buffer
        res.setTo(result, 0, resultLength);
    }

    ((DateFormat*)format)->format(*(Calendar*)calendar, res, (FieldPositionIterator*)fpositer, *status);

    return res.extract(result, resultLength, *status);
}

U_CAPI UDate U_EXPORT2
udat_parse(    const    UDateFormat*        format,
        const    char16_t*          text,
        int32_t         textLength,
        int32_t         *parsePos,
        UErrorCode      *status)
{
    if(U_FAILURE(*status)) return (UDate)0;

    const UnicodeString src((UBool)(textLength == -1), text, textLength);
    ParsePosition pp;
    int32_t stackParsePos = 0;
    UDate res;

    if(parsePos == nullptr) {
        parsePos = &stackParsePos;
    }

    pp.setIndex(*parsePos);

    res = ((DateFormat*)format)->parse(src, pp);

    if(pp.getErrorIndex() == -1)
        *parsePos = pp.getIndex();
    else {
        *parsePos = pp.getErrorIndex();
        *status = U_PARSE_ERROR;
    }

    return res;
}

U_CAPI void U_EXPORT2
udat_parseCalendar(const    UDateFormat*    format,
                            UCalendar*      calendar,
                   const    char16_t*          text,
                            int32_t         textLength,
                            int32_t         *parsePos,
                            UErrorCode      *status)
{
    if(U_FAILURE(*status)) return;

    const UnicodeString src((UBool)(textLength == -1), text, textLength);
    ParsePosition pp;
    int32_t stackParsePos = 0;

    if(parsePos == nullptr) {
        parsePos = &stackParsePos;
    }

    pp.setIndex(*parsePos);

    ((DateFormat*)format)->parse(src, *(Calendar*)calendar, pp);

    if(pp.getErrorIndex() == -1)
        *parsePos = pp.getIndex();
    else {
        *parsePos = pp.getErrorIndex();
        *status = U_PARSE_ERROR;
    }
}

U_CAPI UBool U_EXPORT2
udat_isLenient(const UDateFormat* fmt)
{
    return ((DateFormat*)fmt)->isLenient();
}

U_CAPI void U_EXPORT2
udat_setLenient(    UDateFormat*    fmt,
            UBool          isLenient)
{
    ((DateFormat*)fmt)->setLenient(isLenient);
}

U_CAPI UBool U_EXPORT2
udat_getBooleanAttribute(const UDateFormat* fmt, 
                         UDateFormatBooleanAttribute attr, 
                         UErrorCode* status)
{
    if(U_FAILURE(*status)) return false;
    return ((DateFormat*)fmt)->getBooleanAttribute(attr, *status);
    //return false;
}

U_CAPI void U_EXPORT2
udat_setBooleanAttribute(UDateFormat *fmt, 
                         UDateFormatBooleanAttribute attr, 
                         UBool newValue, 
                         UErrorCode* status)
{
    if(U_FAILURE(*status)) return;
    ((DateFormat*)fmt)->setBooleanAttribute(attr, newValue, *status);
}

U_CAPI const UCalendar* U_EXPORT2
udat_getCalendar(const UDateFormat* fmt)
{
    return (const UCalendar*) ((DateFormat*)fmt)->getCalendar();
}

U_CAPI void U_EXPORT2
udat_setCalendar(UDateFormat*    fmt,
                 const   UCalendar*      calendarToSet)
{
    ((DateFormat*)fmt)->setCalendar(*((Calendar*)calendarToSet));
}

U_CAPI const UNumberFormat* U_EXPORT2 
udat_getNumberFormatForField(const UDateFormat* fmt, char16_t field)
{
    UErrorCode status = U_ZERO_ERROR;
    verifyIsSimpleDateFormat(fmt, &status);
    if (U_FAILURE(status)) return (const UNumberFormat*) ((DateFormat*)fmt)->getNumberFormat();
    return (const UNumberFormat*) ((SimpleDateFormat*)fmt)->getNumberFormatForField(field);
}

U_CAPI const UNumberFormat* U_EXPORT2
udat_getNumberFormat(const UDateFormat* fmt)
{
    return (const UNumberFormat*) ((DateFormat*)fmt)->getNumberFormat();
}

U_CAPI void U_EXPORT2 
udat_adoptNumberFormatForFields(           UDateFormat*    fmt,
                                    const  char16_t*          fields,
                                           UNumberFormat*  numberFormatToSet,
                                           UErrorCode*     status)
{
    verifyIsSimpleDateFormat(fmt, status);
    if (U_FAILURE(*status)) return;
    
    if (fields!=nullptr) {
        UnicodeString overrideFields(fields);
        ((SimpleDateFormat*)fmt)->adoptNumberFormat(overrideFields, (NumberFormat*)numberFormatToSet, *status);
    }
}

U_CAPI void U_EXPORT2
udat_setNumberFormat(UDateFormat*    fmt,
                     const   UNumberFormat*  numberFormatToSet)
{
    ((DateFormat*)fmt)->setNumberFormat(*((NumberFormat*)numberFormatToSet));
}

U_CAPI void U_EXPORT2
udat_adoptNumberFormat(      UDateFormat*    fmt,
                             UNumberFormat*  numberFormatToAdopt)
{
    ((DateFormat*)fmt)->adoptNumberFormat((NumberFormat*)numberFormatToAdopt);
}

U_CAPI const char* U_EXPORT2
udat_getAvailable(int32_t index)
{
    return uloc_getAvailable(index);
}

U_CAPI int32_t U_EXPORT2
udat_countAvailable()
{
    return uloc_countAvailable();
}

U_CAPI UDate U_EXPORT2
udat_get2DigitYearStart(    const   UDateFormat     *fmt,
                        UErrorCode      *status)
{
    verifyIsSimpleDateFormat(fmt, status);
    if(U_FAILURE(*status)) return (UDate)0;
    return ((SimpleDateFormat*)fmt)->get2DigitYearStart(*status);
}

U_CAPI void U_EXPORT2
udat_set2DigitYearStart(    UDateFormat     *fmt,
                        UDate           d,
                        UErrorCode      *status)
{
    verifyIsSimpleDateFormat(fmt, status);
    if(U_FAILURE(*status)) return;
    ((SimpleDateFormat*)fmt)->set2DigitYearStart(d, *status);
}

U_CAPI int32_t U_EXPORT2
udat_toPattern(    const   UDateFormat     *fmt,
        UBool          localized,
        char16_t        *result,
        int32_t         resultLength,
        UErrorCode      *status)
{
    if(U_FAILURE(*status)) {
        return -1;
    }
    if (result == nullptr ? resultLength != 0 : resultLength < 0) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return -1;
    }

    UnicodeString res;
    if (result != nullptr) {
        // nullptr destination for pure preflighting: empty dummy string
        // otherwise, alias the destination buffer
        res.setTo(result, 0, resultLength);
    }

    const DateFormat *df=reinterpret_cast<const DateFormat *>(fmt);
    const SimpleDateFormat *sdtfmt=dynamic_cast<const SimpleDateFormat *>(df);
    const RelativeDateFormat *reldtfmt;
    if (sdtfmt!=nullptr) {
        if(localized)
            sdtfmt->toLocalizedPattern(res, *status);
        else
            sdtfmt->toPattern(res);
    } else if (!localized && (reldtfmt=dynamic_cast<const RelativeDateFormat *>(df))!=nullptr) {
        reldtfmt->toPattern(res, *status);
    } else {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return -1;
    }

    return res.extract(result, resultLength, *status);
}

// TODO: should this take an UErrorCode?
// A: Yes. Of course.
U_CAPI void U_EXPORT2
udat_applyPattern(  UDateFormat     *format,
                    UBool          localized,
                    const   char16_t        *pattern,
                    int32_t         patternLength)
{
    const UnicodeString pat((UBool)(patternLength == -1), pattern, patternLength);
    UErrorCode status = U_ZERO_ERROR;

    verifyIsSimpleDateFormat(format, &status);
    if(U_FAILURE(status)) {
        return;
    }
    
    if(localized)
        ((SimpleDateFormat*)format)->applyLocalizedPattern(pat, status);
    else
        ((SimpleDateFormat*)format)->applyPattern(pat);
}

U_CAPI int32_t U_EXPORT2
udat_getSymbols(const   UDateFormat     *fmt,
                UDateFormatSymbolType   type,
                int32_t                 index,
                char16_t                *result,
                int32_t                 resultLength,
                UErrorCode              *status)
{
    const DateFormatSymbols *syms;
    const SimpleDateFormat* sdtfmt;
    const RelativeDateFormat* rdtfmt;
    if ((sdtfmt = dynamic_cast<const SimpleDateFormat*>(reinterpret_cast<const DateFormat*>(fmt))) != nullptr) {
        syms = sdtfmt->getDateFormatSymbols();
    } else if ((rdtfmt = dynamic_cast<const RelativeDateFormat*>(reinterpret_cast<const DateFormat*>(fmt))) != nullptr) {
        syms = rdtfmt->getDateFormatSymbols();
    } else {
        return -1;
    }
    int32_t count = 0;
    const UnicodeString *res = nullptr;

    switch(type) {
    case UDAT_ERAS:
        res = syms->getEras(count);
        break;

    case UDAT_ERA_NAMES:
        res = syms->getEraNames(count);
        break;

    case UDAT_MONTHS:
        res = syms->getMonths(count);
        break;

    case UDAT_SHORT_MONTHS:
        res = syms->getShortMonths(count);
        break;

    case UDAT_WEEKDAYS:
        res = syms->getWeekdays(count);
        break;

    case UDAT_SHORT_WEEKDAYS:
        res = syms->getShortWeekdays(count);
        break;

    case UDAT_AM_PMS:
        res = syms->getAmPmStrings(count);
        break;

    case UDAT_LOCALIZED_CHARS:
        {
            UnicodeString res1;
            if(!(result==nullptr && resultLength==0)) {
                // nullptr destination for pure preflighting: empty dummy string
                // otherwise, alias the destination buffer
                res1.setTo(result, 0, resultLength);
            }
            syms->getLocalPatternChars(res1);
            return res1.extract(result, resultLength, *status);
        }

    case UDAT_NARROW_MONTHS:
        res = syms->getMonths(count, DateFormatSymbols::FORMAT, DateFormatSymbols::NARROW);
        break;

    case UDAT_SHORTER_WEEKDAYS:
        res = syms->getWeekdays(count, DateFormatSymbols::FORMAT, DateFormatSymbols::SHORT);
        break;

    case UDAT_NARROW_WEEKDAYS:
        res = syms->getWeekdays(count, DateFormatSymbols::FORMAT, DateFormatSymbols::NARROW);
        break;

    case UDAT_STANDALONE_MONTHS:
        res = syms->getMonths(count, DateFormatSymbols::STANDALONE, DateFormatSymbols::WIDE);
        break;

    case UDAT_STANDALONE_SHORT_MONTHS:
        res = syms->getMonths(count, DateFormatSymbols::STANDALONE, DateFormatSymbols::ABBREVIATED);
        break;

    case UDAT_STANDALONE_NARROW_MONTHS:
        res = syms->getMonths(count, DateFormatSymbols::STANDALONE, DateFormatSymbols::NARROW);
        break;

    case UDAT_STANDALONE_WEEKDAYS:
        res = syms->getWeekdays(count, DateFormatSymbols::STANDALONE, DateFormatSymbols::WIDE);
        break;

    case UDAT_STANDALONE_SHORT_WEEKDAYS:
        res = syms->getWeekdays(count, DateFormatSymbols::STANDALONE, DateFormatSymbols::ABBREVIATED);
        break;

    case UDAT_STANDALONE_SHORTER_WEEKDAYS:
        res = syms->getWeekdays(count, DateFormatSymbols::STANDALONE, DateFormatSymbols::SHORT);
        break;

    case UDAT_STANDALONE_NARROW_WEEKDAYS:
        res = syms->getWeekdays(count, DateFormatSymbols::STANDALONE, DateFormatSymbols::NARROW);
        break;

    case UDAT_QUARTERS:
        res = syms->getQuarters(count, DateFormatSymbols::FORMAT, DateFormatSymbols::WIDE);
        break;

    case UDAT_SHORT_QUARTERS:
        res = syms->getQuarters(count, DateFormatSymbols::FORMAT, DateFormatSymbols::ABBREVIATED);
        break;

    case UDAT_NARROW_QUARTERS:
        res = syms->getQuarters(count, DateFormatSymbols::FORMAT, DateFormatSymbols::NARROW);
        break;
        
    case UDAT_STANDALONE_QUARTERS:
        res = syms->getQuarters(count, DateFormatSymbols::STANDALONE, DateFormatSymbols::WIDE);
        break;

    case UDAT_STANDALONE_SHORT_QUARTERS:
        res = syms->getQuarters(count, DateFormatSymbols::STANDALONE, DateFormatSymbols::ABBREVIATED);
        break;

    case UDAT_STANDALONE_NARROW_QUARTERS:
        res = syms->getQuarters(count, DateFormatSymbols::STANDALONE, DateFormatSymbols::NARROW);
        break;
        
    case UDAT_CYCLIC_YEARS_WIDE:
        res = syms->getYearNames(count, DateFormatSymbols::FORMAT, DateFormatSymbols::WIDE);
        break;

    case UDAT_CYCLIC_YEARS_ABBREVIATED:
        res = syms->getYearNames(count, DateFormatSymbols::FORMAT, DateFormatSymbols::ABBREVIATED);
        break;

    case UDAT_CYCLIC_YEARS_NARROW:
        res = syms->getYearNames(count, DateFormatSymbols::FORMAT, DateFormatSymbols::NARROW);
        break;

    case UDAT_ZODIAC_NAMES_WIDE:
        res = syms->getZodiacNames(count, DateFormatSymbols::FORMAT, DateFormatSymbols::WIDE);
        break;

    case UDAT_ZODIAC_NAMES_ABBREVIATED:
        res = syms->getZodiacNames(count, DateFormatSymbols::FORMAT, DateFormatSymbols::ABBREVIATED);
        break;

    case UDAT_ZODIAC_NAMES_NARROW:
        res = syms->getZodiacNames(count, DateFormatSymbols::FORMAT, DateFormatSymbols::NARROW);
        break;

    }

    if(index < count) {
        return res[index].extract(result, resultLength, *status);
    }
    return 0;
}

// TODO: also needs an errorCode.
U_CAPI int32_t U_EXPORT2
udat_countSymbols(    const    UDateFormat                *fmt,
            UDateFormatSymbolType    type)
{
    const DateFormatSymbols *syms;
    const SimpleDateFormat* sdtfmt;
    const RelativeDateFormat* rdtfmt;
    if ((sdtfmt = dynamic_cast<const SimpleDateFormat*>(reinterpret_cast<const DateFormat*>(fmt))) != nullptr) {
        syms = sdtfmt->getDateFormatSymbols();
    } else if ((rdtfmt = dynamic_cast<const RelativeDateFormat*>(reinterpret_cast<const DateFormat*>(fmt))) != nullptr) {
        syms = rdtfmt->getDateFormatSymbols();
    } else {
        return 0;
    }
    int32_t count = 0;

    switch(type) {
    case UDAT_ERAS:
        syms->getEras(count);
        break;

    case UDAT_MONTHS:
        syms->getMonths(count);
        break;

    case UDAT_SHORT_MONTHS:
        syms->getShortMonths(count);
        break;

    case UDAT_WEEKDAYS:
        syms->getWeekdays(count);
        break;

    case UDAT_SHORT_WEEKDAYS:
        syms->getShortWeekdays(count);
        break;

    case UDAT_AM_PMS:
        syms->getAmPmStrings(count);
        break;

    case UDAT_LOCALIZED_CHARS:
        count = 1;
        break;

    case UDAT_ERA_NAMES:
        syms->getEraNames(count);
        break;

    case UDAT_NARROW_MONTHS:
        syms->getMonths(count, DateFormatSymbols::FORMAT, DateFormatSymbols::NARROW);
        break;

    case UDAT_SHORTER_WEEKDAYS:
        syms->getWeekdays(count, DateFormatSymbols::FORMAT, DateFormatSymbols::SHORT);
        break;

    case UDAT_NARROW_WEEKDAYS:
        syms->getWeekdays(count, DateFormatSymbols::FORMAT, DateFormatSymbols::NARROW);
        break;

    case UDAT_STANDALONE_MONTHS:
        syms->getMonths(count, DateFormatSymbols::STANDALONE, DateFormatSymbols::WIDE);
        break;

    case UDAT_STANDALONE_SHORT_MONTHS:
        syms->getMonths(count, DateFormatSymbols::STANDALONE, DateFormatSymbols::ABBREVIATED);
        break;

    case UDAT_STANDALONE_NARROW_MONTHS:
        syms->getMonths(count, DateFormatSymbols::STANDALONE, DateFormatSymbols::NARROW);
        break;

    case UDAT_STANDALONE_WEEKDAYS:
        syms->getWeekdays(count, DateFormatSymbols::STANDALONE, DateFormatSymbols::WIDE);
        break;

    case UDAT_STANDALONE_SHORT_WEEKDAYS:
        syms->getWeekdays(count, DateFormatSymbols::STANDALONE, DateFormatSymbols::ABBREVIATED);
        break;

    case UDAT_STANDALONE_SHORTER_WEEKDAYS:
        syms->getWeekdays(count, DateFormatSymbols::STANDALONE, DateFormatSymbols::SHORT);
        break;

    case UDAT_STANDALONE_NARROW_WEEKDAYS:
        syms->getWeekdays(count, DateFormatSymbols::STANDALONE, DateFormatSymbols::NARROW);
        break;

    case UDAT_QUARTERS:
        syms->getQuarters(count, DateFormatSymbols::FORMAT, DateFormatSymbols::WIDE);
        break;

    case UDAT_SHORT_QUARTERS:
        syms->getQuarters(count, DateFormatSymbols::FORMAT, DateFormatSymbols::ABBREVIATED);
        break;

    case UDAT_NARROW_QUARTERS:
        syms->getQuarters(count, DateFormatSymbols::FORMAT, DateFormatSymbols::NARROW);
        break;
        
    case UDAT_STANDALONE_QUARTERS:
        syms->getQuarters(count, DateFormatSymbols::STANDALONE, DateFormatSymbols::WIDE);
        break;

    case UDAT_STANDALONE_SHORT_QUARTERS:
        syms->getQuarters(count, DateFormatSymbols::STANDALONE, DateFormatSymbols::ABBREVIATED);
        break;

    case UDAT_STANDALONE_NARROW_QUARTERS:
        syms->getQuarters(count, DateFormatSymbols::STANDALONE, DateFormatSymbols::NARROW);
        break;
        
    case UDAT_CYCLIC_YEARS_WIDE:
        syms->getYearNames(count, DateFormatSymbols::FORMAT, DateFormatSymbols::WIDE);
        break;

    case UDAT_CYCLIC_YEARS_ABBREVIATED:
        syms->getYearNames(count, DateFormatSymbols::FORMAT, DateFormatSymbols::ABBREVIATED);
        break;

    case UDAT_CYCLIC_YEARS_NARROW:
        syms->getYearNames(count, DateFormatSymbols::FORMAT, DateFormatSymbols::NARROW);
        break;

    case UDAT_ZODIAC_NAMES_WIDE:
        syms->getZodiacNames(count, DateFormatSymbols::FORMAT, DateFormatSymbols::WIDE);
        break;

    case UDAT_ZODIAC_NAMES_ABBREVIATED:
        syms->getZodiacNames(count, DateFormatSymbols::FORMAT, DateFormatSymbols::ABBREVIATED);
        break;

    case UDAT_ZODIAC_NAMES_NARROW:
        syms->getZodiacNames(count, DateFormatSymbols::FORMAT, DateFormatSymbols::NARROW);
        break;

    }

    return count;
}

U_NAMESPACE_BEGIN

/*
 * This DateFormatSymbolsSingleSetter class is a friend of DateFormatSymbols
 * solely for the purpose of avoiding to clone the array of strings
 * just to modify one of them and then setting all of them back.
 * For example, the old code looked like this:
 *  case UDAT_MONTHS:
 *    res = syms->getMonths(count);
 *    array = new UnicodeString[count];
 *    if(array == 0) {
 *      *status = U_MEMORY_ALLOCATION_ERROR;
 *      return;
 *    }
 *    uprv_arrayCopy(res, array, count);
 *    if(index < count)
 *      array[index] = val;
 *    syms->setMonths(array, count);
 *    break;
 *
 * Even worse, the old code actually cloned the entire DateFormatSymbols object,
 * cloned one value array, changed one value, and then made the SimpleDateFormat
 * replace its DateFormatSymbols object with the new one.
 *
 * markus 2002-oct-14
 */
class DateFormatSymbolsSingleSetter /* not : public UObject because all methods are static */ {
public:
    static void
        setSymbol(UnicodeString *array, int32_t count, int32_t index,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        if(array!=nullptr) {
            if(index>=count) {
                errorCode=U_INDEX_OUTOFBOUNDS_ERROR;
            } else if(value==nullptr) {
                errorCode=U_ILLEGAL_ARGUMENT_ERROR;
            } else {
                array[index].setTo(value, valueLength);
            }
        }
    }

    static void
        setEra(DateFormatSymbols *syms, int32_t index,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        setSymbol(syms->fEras, syms->fErasCount, index, value, valueLength, errorCode);
    }

    static void
        setEraName(DateFormatSymbols *syms, int32_t index,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        setSymbol(syms->fEraNames, syms->fEraNamesCount, index, value, valueLength, errorCode);
    }

    static void
        setMonth(DateFormatSymbols *syms, int32_t index,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        setSymbol(syms->fMonths, syms->fMonthsCount, index, value, valueLength, errorCode);
    }

    static void
        setShortMonth(DateFormatSymbols *syms, int32_t index,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        setSymbol(syms->fShortMonths, syms->fShortMonthsCount, index, value, valueLength, errorCode);
    }

    static void
        setNarrowMonth(DateFormatSymbols *syms, int32_t index,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        setSymbol(syms->fNarrowMonths, syms->fNarrowMonthsCount, index, value, valueLength, errorCode);
    }

    static void
        setStandaloneMonth(DateFormatSymbols *syms, int32_t index,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        setSymbol(syms->fStandaloneMonths, syms->fStandaloneMonthsCount, index, value, valueLength, errorCode);
    }

    static void
        setStandaloneShortMonth(DateFormatSymbols *syms, int32_t index,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        setSymbol(syms->fStandaloneShortMonths, syms->fStandaloneShortMonthsCount, index, value, valueLength, errorCode);
    }

    static void
        setStandaloneNarrowMonth(DateFormatSymbols *syms, int32_t index,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        setSymbol(syms->fStandaloneNarrowMonths, syms->fStandaloneNarrowMonthsCount, index, value, valueLength, errorCode);
    }

    static void
        setWeekday(DateFormatSymbols *syms, int32_t index,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        setSymbol(syms->fWeekdays, syms->fWeekdaysCount, index, value, valueLength, errorCode);
    }

    static void
        setShortWeekday(DateFormatSymbols *syms, int32_t index,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        setSymbol(syms->fShortWeekdays, syms->fShortWeekdaysCount, index, value, valueLength, errorCode);
    }

    static void
        setShorterWeekday(DateFormatSymbols *syms, int32_t index,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        setSymbol(syms->fShorterWeekdays, syms->fShorterWeekdaysCount, index, value, valueLength, errorCode);
    }

    static void
        setNarrowWeekday(DateFormatSymbols *syms, int32_t index,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        setSymbol(syms->fNarrowWeekdays, syms->fNarrowWeekdaysCount, index, value, valueLength, errorCode);
    }

    static void
        setStandaloneWeekday(DateFormatSymbols *syms, int32_t index,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        setSymbol(syms->fStandaloneWeekdays, syms->fStandaloneWeekdaysCount, index, value, valueLength, errorCode);
    }

    static void
        setStandaloneShortWeekday(DateFormatSymbols *syms, int32_t index,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        setSymbol(syms->fStandaloneShortWeekdays, syms->fStandaloneShortWeekdaysCount, index, value, valueLength, errorCode);
    }

    static void
        setStandaloneShorterWeekday(DateFormatSymbols *syms, int32_t index,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        setSymbol(syms->fStandaloneShorterWeekdays, syms->fStandaloneShorterWeekdaysCount, index, value, valueLength, errorCode);
    }

    static void
        setStandaloneNarrowWeekday(DateFormatSymbols *syms, int32_t index,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        setSymbol(syms->fStandaloneNarrowWeekdays, syms->fStandaloneNarrowWeekdaysCount, index, value, valueLength, errorCode);
    }

    static void
        setQuarter(DateFormatSymbols *syms, int32_t index,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        setSymbol(syms->fQuarters, syms->fQuartersCount, index, value, valueLength, errorCode);
    }

    static void
        setShortQuarter(DateFormatSymbols *syms, int32_t index,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        setSymbol(syms->fShortQuarters, syms->fShortQuartersCount, index, value, valueLength, errorCode);
    }

    static void
        setNarrowQuarter(DateFormatSymbols *syms, int32_t index,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        setSymbol(syms->fNarrowQuarters, syms->fNarrowQuartersCount, index, value, valueLength, errorCode);
    }
    
    static void
        setStandaloneQuarter(DateFormatSymbols *syms, int32_t index,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        setSymbol(syms->fStandaloneQuarters, syms->fStandaloneQuartersCount, index, value, valueLength, errorCode);
    }

    static void
        setStandaloneShortQuarter(DateFormatSymbols *syms, int32_t index,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        setSymbol(syms->fStandaloneShortQuarters, syms->fStandaloneShortQuartersCount, index, value, valueLength, errorCode);
    }

    static void
        setStandaloneNarrowQuarter(DateFormatSymbols *syms, int32_t index,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        setSymbol(syms->fStandaloneNarrowQuarters, syms->fStandaloneNarrowQuartersCount, index, value, valueLength, errorCode);
    }
    
    static void
        setShortYearNames(DateFormatSymbols *syms, int32_t index,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        setSymbol(syms->fShortYearNames, syms->fShortYearNamesCount, index, value, valueLength, errorCode);
    }

    static void
        setShortZodiacNames(DateFormatSymbols *syms, int32_t index,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        setSymbol(syms->fShortZodiacNames, syms->fShortZodiacNamesCount, index, value, valueLength, errorCode);
    }

    static void
        setAmPm(DateFormatSymbols *syms, int32_t index,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        setSymbol(syms->fAmPms, syms->fAmPmsCount, index, value, valueLength, errorCode);
    }

    static void
        setLocalPatternChars(DateFormatSymbols *syms,
        const char16_t *value, int32_t valueLength, UErrorCode &errorCode)
    {
        setSymbol(&syms->fLocalPatternChars, 1, 0, value, valueLength, errorCode);
    }
};

U_NAMESPACE_END

U_CAPI void U_EXPORT2
udat_setSymbols(    UDateFormat             *format,
            UDateFormatSymbolType   type,
            int32_t                 index,
            char16_t                *value,
            int32_t                 valueLength,
            UErrorCode              *status)
{
    verifyIsSimpleDateFormat(format, status);
    if(U_FAILURE(*status)) return;

    DateFormatSymbols *syms = (DateFormatSymbols *)((SimpleDateFormat *)format)->getDateFormatSymbols();

    switch(type) {
    case UDAT_ERAS:
        DateFormatSymbolsSingleSetter::setEra(syms, index, value, valueLength, *status);
        break;

    case UDAT_ERA_NAMES:
        DateFormatSymbolsSingleSetter::setEraName(syms, index, value, valueLength, *status);
        break;

    case UDAT_MONTHS:
        DateFormatSymbolsSingleSetter::setMonth(syms, index, value, valueLength, *status);
        break;

    case UDAT_SHORT_MONTHS:
        DateFormatSymbolsSingleSetter::setShortMonth(syms, index, value, valueLength, *status);
        break;

    case UDAT_NARROW_MONTHS:
        DateFormatSymbolsSingleSetter::setNarrowMonth(syms, index, value, valueLength, *status);
        break;

    case UDAT_STANDALONE_MONTHS:
        DateFormatSymbolsSingleSetter::setStandaloneMonth(syms, index, value, valueLength, *status);
        break;

    case UDAT_STANDALONE_SHORT_MONTHS:
        DateFormatSymbolsSingleSetter::setStandaloneShortMonth(syms, index, value, valueLength, *status);
        break;

    case UDAT_STANDALONE_NARROW_MONTHS:
        DateFormatSymbolsSingleSetter::setStandaloneNarrowMonth(syms, index, value, valueLength, *status);
        break;

    case UDAT_WEEKDAYS:
        DateFormatSymbolsSingleSetter::setWeekday(syms, index, value, valueLength, *status);
        break;

    case UDAT_SHORT_WEEKDAYS:
        DateFormatSymbolsSingleSetter::setShortWeekday(syms, index, value, valueLength, *status);
        break;

    case UDAT_SHORTER_WEEKDAYS:
        DateFormatSymbolsSingleSetter::setShorterWeekday(syms, index, value, valueLength, *status);
        break;

    case UDAT_NARROW_WEEKDAYS:
        DateFormatSymbolsSingleSetter::setNarrowWeekday(syms, index, value, valueLength, *status);
        break;

    case UDAT_STANDALONE_WEEKDAYS:
        DateFormatSymbolsSingleSetter::setStandaloneWeekday(syms, index, value, valueLength, *status);
        break;

    case UDAT_STANDALONE_SHORT_WEEKDAYS:
        DateFormatSymbolsSingleSetter::setStandaloneShortWeekday(syms, index, value, valueLength, *status);
        break;

    case UDAT_STANDALONE_SHORTER_WEEKDAYS:
        DateFormatSymbolsSingleSetter::setStandaloneShorterWeekday(syms, index, value, valueLength, *status);
        break;

    case UDAT_STANDALONE_NARROW_WEEKDAYS:
        DateFormatSymbolsSingleSetter::setStandaloneNarrowWeekday(syms, index, value, valueLength, *status);
        break;

    case UDAT_QUARTERS:
        DateFormatSymbolsSingleSetter::setQuarter(syms, index, value, valueLength, *status);
        break;

    case UDAT_SHORT_QUARTERS:
        DateFormatSymbolsSingleSetter::setShortQuarter(syms, index, value, valueLength, *status);
        break;

    case UDAT_NARROW_QUARTERS:
        DateFormatSymbolsSingleSetter::setNarrowQuarter(syms, index, value, valueLength, *status);
        break;
        
    case UDAT_STANDALONE_QUARTERS:
        DateFormatSymbolsSingleSetter::setStandaloneQuarter(syms, index, value, valueLength, *status);
        break;

    case UDAT_STANDALONE_SHORT_QUARTERS:
        DateFormatSymbolsSingleSetter::setStandaloneShortQuarter(syms, index, value, valueLength, *status);
        break;

    case UDAT_STANDALONE_NARROW_QUARTERS:
        DateFormatSymbolsSingleSetter::setStandaloneNarrowQuarter(syms, index, value, valueLength, *status);
        break;
        
    case UDAT_CYCLIC_YEARS_ABBREVIATED:
        DateFormatSymbolsSingleSetter::setShortYearNames(syms, index, value, valueLength, *status);
        break;

    case UDAT_ZODIAC_NAMES_ABBREVIATED:
        DateFormatSymbolsSingleSetter::setShortZodiacNames(syms, index, value, valueLength, *status);
        break;

    case UDAT_AM_PMS:
        DateFormatSymbolsSingleSetter::setAmPm(syms, index, value, valueLength, *status);
        break;

    case UDAT_LOCALIZED_CHARS:
        DateFormatSymbolsSingleSetter::setLocalPatternChars(syms, value, valueLength, *status);
        break;

    default:
        *status = U_UNSUPPORTED_ERROR;
        break;
        
    }
}

U_CAPI const char* U_EXPORT2
udat_getLocaleByType(const UDateFormat *fmt,
                     ULocDataLocaleType type,
                     UErrorCode* status)
{
    if (fmt == nullptr) {
        if (U_SUCCESS(*status)) {
            *status = U_ILLEGAL_ARGUMENT_ERROR;
        }
        return nullptr;
    }
    return ((Format*)fmt)->getLocaleID(type, *status);
}

U_CAPI void U_EXPORT2
udat_setContext(UDateFormat* fmt, UDisplayContext value, UErrorCode* status)
{
    if (U_FAILURE(*status)) {
        return;
    }
    ((DateFormat*)fmt)->setContext(value, *status);
}

U_CAPI UDisplayContext U_EXPORT2
udat_getContext(const UDateFormat* fmt, UDisplayContextType type, UErrorCode* status)
{
    if (U_FAILURE(*status)) {
        return (UDisplayContext)0;
    }
    return ((const DateFormat*)fmt)->getContext(type, *status);
}


/**
 * Verify that fmt is a RelativeDateFormat. Invalid error if not.
 * @param fmt the UDateFormat, definitely a DateFormat, maybe something else
 * @param status error code, will be set to failure if there is a failure or the fmt is nullptr.
 */
static void verifyIsRelativeDateFormat(const UDateFormat* fmt, UErrorCode *status) {
   if(U_SUCCESS(*status) &&
       dynamic_cast<const RelativeDateFormat*>(reinterpret_cast<const DateFormat*>(fmt))==nullptr) {
       *status = U_ILLEGAL_ARGUMENT_ERROR;
   }
}


U_CAPI int32_t U_EXPORT2 
udat_toPatternRelativeDate(const UDateFormat *fmt,
                           char16_t          *result,
                           int32_t           resultLength,
                           UErrorCode        *status)
{
    verifyIsRelativeDateFormat(fmt, status);
    if(U_FAILURE(*status)) {
        return -1;
    }
    if (result == nullptr ? resultLength != 0 : resultLength < 0) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return -1;
    }

    UnicodeString datePattern;
    if (result != nullptr) {
        // nullptr destination for pure preflighting: empty dummy string
        // otherwise, alias the destination buffer
        datePattern.setTo(result, 0, resultLength);
    }
    ((RelativeDateFormat*)fmt)->toPatternDate(datePattern, *status);
    return datePattern.extract(result, resultLength, *status);
}

U_CAPI int32_t U_EXPORT2 
udat_toPatternRelativeTime(const UDateFormat *fmt,
                           char16_t          *result,
                           int32_t           resultLength,
                           UErrorCode        *status)
{
    verifyIsRelativeDateFormat(fmt, status);
    if(U_FAILURE(*status)) {
        return -1;
    }
    if (result == nullptr ? resultLength != 0 : resultLength < 0) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return -1;
    }

    UnicodeString timePattern;
    if (result != nullptr) {
        // nullptr destination for pure preflighting: empty dummy string
        // otherwise, alias the destination buffer
        timePattern.setTo(result, 0, resultLength);
    }
    ((RelativeDateFormat*)fmt)->toPatternTime(timePattern, *status);
    return timePattern.extract(result, resultLength, *status);
}

U_CAPI void U_EXPORT2 
udat_applyPatternRelative(UDateFormat *format,
                          const char16_t *datePattern,
                          int32_t     datePatternLength,
                          const char16_t *timePattern,
                          int32_t     timePatternLength,
                          UErrorCode  *status)
{
    verifyIsRelativeDateFormat(format, status);
    if(U_FAILURE(*status)) return;
    const UnicodeString datePat((UBool)(datePatternLength == -1), datePattern, datePatternLength);
    const UnicodeString timePat((UBool)(timePatternLength == -1), timePattern, timePatternLength);
    ((RelativeDateFormat*)format)->applyPatterns(datePat, timePat, *status);
}

#endif /* #if !UCONFIG_NO_FORMATTING */
