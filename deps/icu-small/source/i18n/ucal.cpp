// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*   Copyright (C) 1996-2016, International Business Machines
*   Corporation and others.  All Rights Reserved.
*******************************************************************************
*/

#include "utypeinfo.h"  // for 'typeid' to work

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/ucal.h"
#include "unicode/uloc.h"
#include "unicode/calendar.h"
#include "unicode/timezone.h"
#include "unicode/gregocal.h"
#include "unicode/simpletz.h"
#include "unicode/ustring.h"
#include "unicode/strenum.h"
#include "unicode/localpointer.h"
#include "charstr.h"
#include "cmemory.h"
#include "cstring.h"
#include "iso8601cal.h"
#include "ustrenum.h"
#include "uenumimp.h"
#include "ulist.h"
#include "ulocimp.h"

U_NAMESPACE_USE

static TimeZone*
_createTimeZone(const char16_t* zoneID, int32_t len, UErrorCode* ec) {
    TimeZone* zone = nullptr;
    if (ec != nullptr && U_SUCCESS(*ec)) {
        // Note that if zoneID is invalid, we get back GMT. This odd
        // behavior is by design and goes back to the JDK. The only
        // failure we will see is a memory allocation failure.
        int32_t l = (len<0 ? u_strlen(zoneID) : len);
        UnicodeString zoneStrID;
        zoneStrID.setTo((UBool)(len < 0), zoneID, l); /* temporary read-only alias */
        zone = TimeZone::createTimeZone(zoneStrID);
        if (zone == nullptr) {
            *ec = U_MEMORY_ALLOCATION_ERROR;
        }
    }
    return zone;
}

U_CAPI UEnumeration* U_EXPORT2
ucal_openTimeZoneIDEnumeration(USystemTimeZoneType zoneType, const char* region,
                                const int32_t* rawOffset, UErrorCode* ec) {
    return uenum_openFromStringEnumeration(TimeZone::createTimeZoneIDEnumeration(
        zoneType, region, rawOffset, *ec), ec);
}

U_CAPI UEnumeration* U_EXPORT2
ucal_openTimeZones(UErrorCode* ec) {
    return ucal_openTimeZoneIDEnumeration(UCAL_ZONE_TYPE_ANY, nullptr, nullptr, ec);
}

U_CAPI UEnumeration* U_EXPORT2
ucal_openCountryTimeZones(const char* country, UErrorCode* ec) {
    return ucal_openTimeZoneIDEnumeration(UCAL_ZONE_TYPE_ANY, country, nullptr, ec);
}

U_CAPI int32_t U_EXPORT2
ucal_getDefaultTimeZone(char16_t* result, int32_t resultCapacity, UErrorCode* ec) {
    int32_t len = 0;
    if (ec != nullptr && U_SUCCESS(*ec)) {
        TimeZone* zone = TimeZone::createDefault();
        if (zone == nullptr) {
            *ec = U_MEMORY_ALLOCATION_ERROR;
        } else {
            UnicodeString id;
            zone->getID(id);
            delete zone;
            len = id.extract(result, resultCapacity, *ec);
        }
    }
    return len;
}

U_CAPI void U_EXPORT2
ucal_setDefaultTimeZone(const char16_t* zoneID, UErrorCode* ec) {
    TimeZone* zone = _createTimeZone(zoneID, -1, ec);
    if (zone != nullptr) {
        TimeZone::adoptDefault(zone);
    }
}

U_CAPI int32_t U_EXPORT2
ucal_getHostTimeZone(char16_t* result, int32_t resultCapacity, UErrorCode* ec) {
    int32_t len = 0;
    if (ec != nullptr && U_SUCCESS(*ec)) {
        TimeZone *zone = TimeZone::detectHostTimeZone();
        if (zone == nullptr) {
            *ec = U_MEMORY_ALLOCATION_ERROR;
        } else {
            UnicodeString id;
            zone->getID(id);
            delete zone;
            len = id.extract(result, resultCapacity, *ec);
        }
    }
    return len;
}

U_CAPI int32_t U_EXPORT2
ucal_getDSTSavings(const char16_t* zoneID, UErrorCode* ec) {
    int32_t result = 0;
    TimeZone* zone = _createTimeZone(zoneID, -1, ec);
    if (U_SUCCESS(*ec)) {
        SimpleTimeZone* stz = dynamic_cast<SimpleTimeZone*>(zone);
        if (stz != nullptr) {
            result = stz->getDSTSavings();
        } else {
            // Since there is no getDSTSavings on TimeZone, we use a
            // heuristic: Starting with the current time, march
            // forwards for one year, looking for DST savings.
            // Stepping by weeks is sufficient.
            UDate d = Calendar::getNow();
            for (int32_t i=0; i<53; ++i, d+=U_MILLIS_PER_DAY*7.0) {
                int32_t raw, dst;
                zone->getOffset(d, false, raw, dst, *ec);
                if (U_FAILURE(*ec)) {
                    break;
                } else if (dst != 0) {
                    result = dst;
                    break;
                }
            }
        }
    }
    delete zone;
    return result;
}

U_CAPI UDate  U_EXPORT2
ucal_getNow()
{

  return Calendar::getNow();
}

#define ULOC_LOCALE_IDENTIFIER_CAPACITY (ULOC_FULLNAME_CAPACITY + 1 + ULOC_KEYWORD_AND_VALUES_CAPACITY)

U_CAPI UCalendar*  U_EXPORT2
ucal_open(  const char16_t*  zoneID,
            int32_t       len,
            const char*   locale,
            UCalendarType caltype,
            UErrorCode*   status)
{
  if (U_FAILURE(*status)) {
      return nullptr;
  }
  
  LocalPointer<TimeZone> zone( (zoneID==nullptr) ? TimeZone::createDefault() 
      : _createTimeZone(zoneID, len, status), *status);

  if (U_FAILURE(*status)) {
      return nullptr;
  }

  if ( caltype == UCAL_GREGORIAN ) {
      if ( locale == nullptr ) {
          locale = uloc_getDefault();
      }
      CharString localeBuf(locale, *status);
      ulocimp_setKeywordValue("calendar", "gregorian", localeBuf, *status);
      if (U_FAILURE(*status)) {
          return nullptr;
      }
      return (UCalendar*)Calendar::createInstance(zone.orphan(), Locale(localeBuf.data()), *status);
  }
  return (UCalendar*)Calendar::createInstance(zone.orphan(), Locale(locale), *status);
}

U_CAPI void U_EXPORT2
ucal_close(UCalendar *cal)
{
    if (cal != nullptr) {
        delete (Calendar*) cal;
    }
}

U_CAPI UCalendar* U_EXPORT2 
ucal_clone(const UCalendar* cal,
           UErrorCode*      status)
{
  if (U_FAILURE(*status)) return nullptr;

  Calendar* res = ((Calendar*)cal)->clone();

  if (res == nullptr) {
    *status = U_MEMORY_ALLOCATION_ERROR;
    return nullptr;
  }

  return (UCalendar*) res;
}

U_CAPI void  U_EXPORT2
ucal_setTimeZone(    UCalendar*      cal,
            const    char16_t*            zoneID,
            int32_t        len,
            UErrorCode *status)
{

  if(U_FAILURE(*status))
    return;

  TimeZone* zone = (zoneID==nullptr) ? TimeZone::createDefault()
      : _createTimeZone(zoneID, len, status);

  if (zone != nullptr) {
      ((Calendar*)cal)->adoptTimeZone(zone);
  }
}

U_CAPI int32_t U_EXPORT2
ucal_getTimeZoneID(const UCalendar *cal,
                   char16_t *result,
                   int32_t resultLength,
                   UErrorCode *status)
{
    if (U_FAILURE(*status)) {
        return 0;
    }
    const TimeZone& tz = ((Calendar*)cal)->getTimeZone();
    UnicodeString id;
    tz.getID(id);
    return id.extract(result, resultLength, *status);
}

U_CAPI int32_t U_EXPORT2
ucal_getTimeZoneDisplayName(const     UCalendar*                 cal,
                    UCalendarDisplayNameType     type,
                    const char             *locale,
                    char16_t*                  result,
                    int32_t                 resultLength,
                    UErrorCode*             status)
{

    if(U_FAILURE(*status)) return -1;

    const TimeZone& tz = ((Calendar*)cal)->getTimeZone();
    UnicodeString id;
    if (!(result == nullptr && resultLength == 0)) {
        // Null destination for pure preflighting: empty dummy string
        // otherwise, alias the destination buffer
        id.setTo(result, 0, resultLength);
    }

    switch(type) {
  case UCAL_STANDARD:
      tz.getDisplayName(false, TimeZone::LONG, Locale(locale), id);
      break;

  case UCAL_SHORT_STANDARD:
      tz.getDisplayName(false, TimeZone::SHORT, Locale(locale), id);
      break;

  case UCAL_DST:
      tz.getDisplayName(true, TimeZone::LONG, Locale(locale), id);
      break;

  case UCAL_SHORT_DST:
      tz.getDisplayName(true, TimeZone::SHORT, Locale(locale), id);
      break;
    }

    return id.extract(result, resultLength, *status);
}

U_CAPI UBool  U_EXPORT2
ucal_inDaylightTime(    const    UCalendar*      cal, 
                    UErrorCode*     status )
{

    if(U_FAILURE(*status)) return (UBool) -1;
    return ((Calendar*)cal)->inDaylightTime(*status);
}

U_CAPI void U_EXPORT2
ucal_setGregorianChange(UCalendar *cal, UDate date, UErrorCode *pErrorCode) {
    if(U_FAILURE(*pErrorCode)) {
        return;
    }
    Calendar *cpp_cal = (Calendar *)cal;
    GregorianCalendar *gregocal = dynamic_cast<GregorianCalendar *>(cpp_cal);
    // Not if(gregocal == nullptr) {
    // because we really want to work only with a GregorianCalendar, not with
    // its subclasses like BuddhistCalendar.
    if (cpp_cal == nullptr) {
        // We normally don't check "this" pointers for nullptr, but this here avoids
        // compiler-generated exception-throwing code in case cal == nullptr.
        *pErrorCode = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    if(typeid(*cpp_cal) != typeid(GregorianCalendar) &&
       typeid(*cpp_cal) != typeid(ISO8601Calendar)) {
        *pErrorCode = U_UNSUPPORTED_ERROR;
        return;
    }
    gregocal->setGregorianChange(date, *pErrorCode);
}

U_CAPI UDate U_EXPORT2
ucal_getGregorianChange(const UCalendar *cal, UErrorCode *pErrorCode) {
    if(U_FAILURE(*pErrorCode)) {
        return (UDate)0;
    }
    const Calendar *cpp_cal = (const Calendar *)cal;
    const GregorianCalendar *gregocal = dynamic_cast<const GregorianCalendar *>(cpp_cal);
    // Not if(gregocal == nullptr) {
    // see comments in ucal_setGregorianChange().
    if (cpp_cal == nullptr) {
        // We normally don't check "this" pointers for nullptr, but this here avoids
        // compiler-generated exception-throwing code in case cal == nullptr.
        *pErrorCode = U_ILLEGAL_ARGUMENT_ERROR;
        return (UDate)0;
    }
    if(typeid(*cpp_cal) != typeid(GregorianCalendar) &&
       typeid(*cpp_cal) != typeid(ISO8601Calendar)) {
        *pErrorCode = U_UNSUPPORTED_ERROR;
        return (UDate)0;
    }
    return gregocal->getGregorianChange();
}

U_CAPI int32_t U_EXPORT2
ucal_getAttribute(    const    UCalendar*              cal,
                  UCalendarAttribute      attr) UPRV_NO_SANITIZE_UNDEFINED {
    switch(attr) {
  case UCAL_LENIENT:
      return ((Calendar*)cal)->isLenient();

  case UCAL_FIRST_DAY_OF_WEEK:
      return ((Calendar*)cal)->getFirstDayOfWeek();

  case UCAL_MINIMAL_DAYS_IN_FIRST_WEEK:
      return ((Calendar*)cal)->getMinimalDaysInFirstWeek();

  case UCAL_REPEATED_WALL_TIME:
      return ((Calendar*)cal)->getRepeatedWallTimeOption();

  case UCAL_SKIPPED_WALL_TIME:
      return ((Calendar*)cal)->getSkippedWallTimeOption();

  default:
      break;
    }
    return -1;
}

U_CAPI void U_EXPORT2
ucal_setAttribute(      UCalendar*              cal,
                  UCalendarAttribute      attr,
                  int32_t                 newValue)
{

    switch(attr) {
  case UCAL_LENIENT:
      ((Calendar*)cal)->setLenient((UBool)newValue);
      break;

  case UCAL_FIRST_DAY_OF_WEEK:
      ((Calendar*)cal)->setFirstDayOfWeek((UCalendarDaysOfWeek)newValue);
      break;

  case UCAL_MINIMAL_DAYS_IN_FIRST_WEEK:
      ((Calendar*)cal)->setMinimalDaysInFirstWeek((uint8_t)newValue);
      break;

  case UCAL_REPEATED_WALL_TIME:
      ((Calendar*)cal)->setRepeatedWallTimeOption((UCalendarWallTimeOption)newValue);
      break;

  case UCAL_SKIPPED_WALL_TIME:
      ((Calendar*)cal)->setSkippedWallTimeOption((UCalendarWallTimeOption)newValue);
      break;
    }
}

U_CAPI const char* U_EXPORT2
ucal_getAvailable(int32_t index)
{

    return uloc_getAvailable(index);
}

U_CAPI int32_t U_EXPORT2
ucal_countAvailable()
{

    return uloc_countAvailable();
}

U_CAPI UDate  U_EXPORT2
ucal_getMillis(    const    UCalendar*      cal,
               UErrorCode*     status)
{

    if(U_FAILURE(*status)) return (UDate) 0;

    return ((Calendar*)cal)->getTime(*status);
}

U_CAPI void  U_EXPORT2
ucal_setMillis(        UCalendar*      cal,
               UDate           dateTime,
               UErrorCode*     status )
{
    if(U_FAILURE(*status)) return;

    ((Calendar*)cal)->setTime(dateTime, *status);
}

// TBD: why does this take an UErrorCode?
U_CAPI void  U_EXPORT2
ucal_setDate(        UCalendar*        cal,
             int32_t            year,
             int32_t            month,
             int32_t            date,
             UErrorCode        *status)
{

    if(U_FAILURE(*status)) return;

    ((Calendar*)cal)->set(year, month, date);
}

// TBD: why does this take an UErrorCode?
U_CAPI void  U_EXPORT2
ucal_setDateTime(    UCalendar*        cal,
                 int32_t            year,
                 int32_t            month,
                 int32_t            date,
                 int32_t            hour,
                 int32_t            minute,
                 int32_t            second,
                 UErrorCode        *status)
{
    if(U_FAILURE(*status)) return;

    ((Calendar*)cal)->set(year, month, date, hour, minute, second);
}

U_CAPI UBool  U_EXPORT2
ucal_equivalentTo(    const UCalendar*      cal1,
                  const UCalendar*      cal2)
{

    return ((Calendar*)cal1)->isEquivalentTo(*((Calendar*)cal2));
}

U_CAPI void  U_EXPORT2
ucal_add(    UCalendar*                cal,
         UCalendarDateFields        field,
         int32_t                    amount,
         UErrorCode*                status) UPRV_NO_SANITIZE_UNDEFINED {
    if(U_FAILURE(*status)) return;
    if (field < 0 || UCAL_FIELD_COUNT <= field) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    ((Calendar*)cal)->add(field, amount, *status);
}

U_CAPI void  U_EXPORT2
ucal_roll(        UCalendar*            cal,
          UCalendarDateFields field,
          int32_t                amount,
          UErrorCode*            status) UPRV_NO_SANITIZE_UNDEFINED {
    if(U_FAILURE(*status)) return;
    if (field < 0 || UCAL_FIELD_COUNT <= field) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    ((Calendar*)cal)->roll(field, amount, *status);
}

U_CAPI int32_t  U_EXPORT2
ucal_get(    const    UCalendar*                cal,
         UCalendarDateFields        field,
         UErrorCode*                status ) UPRV_NO_SANITIZE_UNDEFINED {
    if(U_FAILURE(*status)) return -1;
    if (field < 0 || UCAL_FIELD_COUNT <= field) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return -1;
    }

    return ((Calendar*)cal)->get(field, *status);
}

U_CAPI void  U_EXPORT2
ucal_set(    UCalendar*                cal,
         UCalendarDateFields        field,
         int32_t                    value) UPRV_NO_SANITIZE_UNDEFINED {
    if (field < 0 || UCAL_FIELD_COUNT <= field) {
        return;
    }

    ((Calendar*)cal)->set(field, value);
}

U_CAPI UBool  U_EXPORT2
ucal_isSet(    const    UCalendar*                cal,
           UCalendarDateFields        field) UPRV_NO_SANITIZE_UNDEFINED {
    if (field < 0 || UCAL_FIELD_COUNT <= field) {
        return false;
    }

    return ((Calendar*)cal)->isSet(field);
}

U_CAPI void  U_EXPORT2
ucal_clearField(    UCalendar*            cal,
                UCalendarDateFields field) UPRV_NO_SANITIZE_UNDEFINED {
    if (field < 0 || UCAL_FIELD_COUNT <= field) {
        return;
    }

    ((Calendar*)cal)->clear(field);
}

U_CAPI void  U_EXPORT2
ucal_clear(UCalendar* calendar)
{

    ((Calendar*)calendar)->clear();
}

U_CAPI int32_t  U_EXPORT2
ucal_getLimit(    const    UCalendar*              cal,
              UCalendarDateFields     field,
              UCalendarLimitType      type,
              UErrorCode        *status) UPRV_NO_SANITIZE_UNDEFINED {
    if (status == nullptr || U_FAILURE(*status)) {
        return -1;
    }
    if (field < 0 || UCAL_FIELD_COUNT <= field) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return -1;
    }

    switch(type) {
  case UCAL_MINIMUM:
      return ((Calendar*)cal)->getMinimum(field);

  case UCAL_MAXIMUM:
      return ((Calendar*)cal)->getMaximum(field);

  case UCAL_GREATEST_MINIMUM:
      return ((Calendar*)cal)->getGreatestMinimum(field);

  case UCAL_LEAST_MAXIMUM:
      return ((Calendar*)cal)->getLeastMaximum(field);

  case UCAL_ACTUAL_MINIMUM:
      return ((Calendar*)cal)->getActualMinimum(field,
          *status);

  case UCAL_ACTUAL_MAXIMUM:
      return ((Calendar*)cal)->getActualMaximum(field,
          *status);

  default:
      break;
    }
    return -1;
}

U_CAPI const char * U_EXPORT2
ucal_getLocaleByType(const UCalendar *cal, ULocDataLocaleType type, UErrorCode* status) 
{
    if (cal == nullptr) {
        if (U_SUCCESS(*status)) {
            *status = U_ILLEGAL_ARGUMENT_ERROR;
        }
        return nullptr;
    }
    return ((Calendar*)cal)->getLocaleID(type, *status);
}

U_CAPI const char * U_EXPORT2
ucal_getTZDataVersion(UErrorCode* status)
{
    return TimeZone::getTZDataVersion(*status);
}

U_CAPI int32_t U_EXPORT2
ucal_getCanonicalTimeZoneID(const char16_t* id, int32_t len,
                            char16_t* result, int32_t resultCapacity, UBool *isSystemID, UErrorCode* status) {
    if (status == nullptr || U_FAILURE(*status)) {
        return 0;
    }
    if (isSystemID) {
        *isSystemID = false;
    }
    if (id == nullptr || len == 0 || result == nullptr || resultCapacity <= 0) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    int32_t reslen = 0;
    UnicodeString canonical;
    UBool systemID = false;
    TimeZone::getCanonicalID(UnicodeString(id, len), canonical, systemID, *status);
    if (U_SUCCESS(*status)) {
        if (isSystemID) {
            *isSystemID = systemID;
        }
        reslen = canonical.extract(result, resultCapacity, *status);
    }
    return reslen;
}

U_DRAFT int32_t U_EXPORT2
ucal_getIanaTimeZoneID(const char16_t* id, int32_t len,
                        char16_t* result, int32_t resultCapacity, UErrorCode* status)
{
    UnicodeString ianaID;
    TimeZone::getIanaID(UnicodeString(id, len), ianaID, *status);
    return ianaID.extract(result, resultCapacity, *status);
}


U_CAPI const char * U_EXPORT2
ucal_getType(const UCalendar *cal, UErrorCode* status)
{
    if (U_FAILURE(*status)) {
        return nullptr;
    }
    return ((Calendar*)cal)->getType();
}

U_CAPI UCalendarWeekdayType U_EXPORT2
ucal_getDayOfWeekType(const UCalendar *cal, UCalendarDaysOfWeek dayOfWeek, UErrorCode* status)
{
    if (U_FAILURE(*status)) {
        return UCAL_WEEKDAY;
    }
    return ((Calendar*)cal)->getDayOfWeekType(dayOfWeek, *status);
}

U_CAPI int32_t U_EXPORT2
ucal_getWeekendTransition(const UCalendar *cal, UCalendarDaysOfWeek dayOfWeek, UErrorCode *status)
{
    if (U_FAILURE(*status)) {
        return 0;
    }
    return ((Calendar*)cal)->getWeekendTransition(dayOfWeek, *status);
}

U_CAPI UBool U_EXPORT2
ucal_isWeekend(const UCalendar *cal, UDate date, UErrorCode *status)
{
    if (U_FAILURE(*status)) {
        return false;
    }
    return ((Calendar*)cal)->isWeekend(date, *status);
}

U_CAPI int32_t  U_EXPORT2
ucal_getFieldDifference(UCalendar* cal, UDate target,
                        UCalendarDateFields field,
                        UErrorCode* status )
{
    if (U_FAILURE(*status)) {
        return 0;
    }
    return ((Calendar*)cal)->fieldDifference(target, field, *status);
}


static const UEnumeration defaultKeywordValues = {
    nullptr,
    nullptr,
    ulist_close_keyword_values_iterator,
    ulist_count_keyword_values,
    uenum_unextDefault,
    ulist_next_keyword_value, 
    ulist_reset_keyword_values_iterator
};

static const char * const CAL_TYPES[] = {
        "gregorian",
        "japanese",
        "buddhist",
        "roc",
        "persian",
        "islamic-civil",
        "islamic",
        "hebrew",
        "chinese",
        "indian",
        "coptic",
        "ethiopic",
        "ethiopic-amete-alem",
        "iso8601",
        "dangi",
        "islamic-umalqura",
        "islamic-tbla",
        "islamic-rgsa",
        nullptr
};

U_CAPI UEnumeration* U_EXPORT2
ucal_getKeywordValuesForLocale(const char * /* key */, const char* locale, UBool commonlyUsed, UErrorCode *status) {
    // Resolve region
    CharString prefRegion = ulocimp_getRegionForSupplementalData(locale, true, *status);

    // Read preferred calendar values from supplementalData calendarPreference
    UResourceBundle *rb = ures_openDirect(nullptr, "supplementalData", status);
    ures_getByKey(rb, "calendarPreferenceData", rb, status);
    UResourceBundle *order = ures_getByKey(rb, prefRegion.data(), nullptr, status);
    if (*status == U_MISSING_RESOURCE_ERROR && rb != nullptr) {
        *status = U_ZERO_ERROR;
        order = ures_getByKey(rb, "001", nullptr, status);
    }

    // Create a list of calendar type strings
    UList *values = nullptr;
    if (U_SUCCESS(*status)) {
        values = ulist_createEmptyList(status);
        if (U_SUCCESS(*status)) {
            for (int i = 0; i < ures_getSize(order); i++) {
                int32_t len;
                const char16_t *type = ures_getStringByIndex(order, i, &len, status);
                char *caltype = (char*)uprv_malloc(len + 1);
                if (caltype == nullptr) {
                    *status = U_MEMORY_ALLOCATION_ERROR;
                    break;
                }
                u_UCharsToChars(type, caltype, len);
                *(caltype + len) = 0;

                ulist_addItemEndList(values, caltype, true, status);
                if (U_FAILURE(*status)) {
                    break;
                }
            }

            if (U_SUCCESS(*status) && !commonlyUsed) {
                // If not commonlyUsed, add other available values
                for (int32_t i = 0; CAL_TYPES[i] != nullptr; i++) {
                    if (!ulist_containsString(values, CAL_TYPES[i], (int32_t)uprv_strlen(CAL_TYPES[i]))) {
                        ulist_addItemEndList(values, CAL_TYPES[i], false, status);
                        if (U_FAILURE(*status)) {
                            break;
                        }
                    }
                }
            }
            if (U_FAILURE(*status)) {
                ulist_deleteList(values);
                values = nullptr;
            }
        }
    }

    ures_close(order);
    ures_close(rb);

    if (U_FAILURE(*status) || values == nullptr) {
        return nullptr;
    }

    // Create string enumeration
    UEnumeration *en = (UEnumeration*)uprv_malloc(sizeof(UEnumeration));
    if (en == nullptr) {
        *status = U_MEMORY_ALLOCATION_ERROR;
        ulist_deleteList(values);
        return nullptr;
    }
    ulist_resetList(values);
    memcpy(en, &defaultKeywordValues, sizeof(UEnumeration));
    en->context = values;
    return en;
}

U_CAPI UBool U_EXPORT2 
ucal_getTimeZoneTransitionDate(const UCalendar* cal, UTimeZoneTransitionType type,
                               UDate* transition, UErrorCode* status)
{
    if (U_FAILURE(*status)) {
        return false;
    }
    UDate base = ((Calendar*)cal)->getTime(*status);
    const TimeZone& tz = ((Calendar*)cal)->getTimeZone();
    const BasicTimeZone * btz = dynamic_cast<const BasicTimeZone *>(&tz);
    if (btz != nullptr && U_SUCCESS(*status)) {
        TimeZoneTransition tzt;
        UBool inclusive = (type == UCAL_TZ_TRANSITION_NEXT_INCLUSIVE || type == UCAL_TZ_TRANSITION_PREVIOUS_INCLUSIVE);
        UBool result = (type == UCAL_TZ_TRANSITION_NEXT || type == UCAL_TZ_TRANSITION_NEXT_INCLUSIVE)?
                        btz->getNextTransition(base, inclusive, tzt):
                        btz->getPreviousTransition(base, inclusive, tzt);
        if (result) {
            *transition = tzt.getTime();
            return true;
        }
    }
    return false;
}

U_CAPI int32_t U_EXPORT2
ucal_getWindowsTimeZoneID(const char16_t* id, int32_t len, char16_t* winid, int32_t winidCapacity, UErrorCode* status) {
    if (U_FAILURE(*status)) {
        return 0;
    }

    int32_t resultLen = 0;
    UnicodeString resultWinID;

    TimeZone::getWindowsID(UnicodeString(id, len), resultWinID, *status);
    if (U_SUCCESS(*status) && resultWinID.length() > 0) {
        resultLen = resultWinID.length();
        resultWinID.extract(winid, winidCapacity, *status);
    }

    return resultLen;
}

U_CAPI int32_t U_EXPORT2
ucal_getTimeZoneIDForWindowsID(const char16_t* winid, int32_t len, const char* region, char16_t* id, int32_t idCapacity, UErrorCode* status) {
    if (U_FAILURE(*status)) {
        return 0;
    }

    int32_t resultLen = 0;
    UnicodeString resultID;

    TimeZone::getIDForWindowsID(UnicodeString(winid, len), region, resultID, *status);
    if (U_SUCCESS(*status) && resultID.length() > 0) {
        resultLen = resultID.length();
        resultID.extract(id, idCapacity, *status);
    }

    return resultLen;
}

U_CAPI void U_EXPORT2 ucal_getTimeZoneOffsetFromLocal(
    const UCalendar* cal,
    UTimeZoneLocalOption nonExistingTimeOpt,
    UTimeZoneLocalOption duplicatedTimeOpt,
    int32_t* rawOffset, int32_t* dstOffset, UErrorCode* status)
{
    if (U_FAILURE(*status)) {
        return;
    }
    UDate date = ((Calendar*)cal)->getTime(*status);
    if (U_FAILURE(*status)) {
        return;
    }
    const TimeZone& tz = ((Calendar*)cal)->getTimeZone();
    const BasicTimeZone* btz = dynamic_cast<const BasicTimeZone *>(&tz);
    if (btz == nullptr) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    btz->getOffsetFromLocal(
        date, nonExistingTimeOpt, duplicatedTimeOpt,
        *rawOffset, *dstOffset, *status);
}

#endif /* #if !UCONFIG_NO_FORMATTING */
