// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
******************************************************************************
* Copyright (C) 2014-2016, International Business Machines Corporation and
* others. All Rights Reserved.
******************************************************************************
*
* File reldatefmt.cpp
******************************************************************************
*/

#include "unicode/reldatefmt.h"

#if !UCONFIG_NO_FORMATTING

#include <cmath>
#include <functional>
#include "unicode/calendar.h"
#include "unicode/datefmt.h"
#include "unicode/dtfmtsym.h"
#include "unicode/ucasemap.h"
#include "unicode/ureldatefmt.h"
#include "unicode/udisplaycontext.h"
#include "unicode/unum.h"
#include "unicode/localpointer.h"
#include "unicode/plurrule.h"
#include "unicode/simpleformatter.h"
#include "unicode/decimfmt.h"
#include "unicode/numfmt.h"
#include "unicode/brkiter.h"
#include "unicode/simpleformatter.h"
#include "uresimp.h"
#include "unicode/ures.h"
#include "cstring.h"
#include "ucln_in.h"
#include "mutex.h"
#include "charstr.h"
#include "uassert.h"
#include "quantityformatter.h"
#include "resource.h"
#include "sharedbreakiterator.h"
#include "sharedpluralrules.h"
#include "sharednumberformat.h"
#include "standardplural.h"
#include "unifiedcache.h"
#include "util.h"
#include "formatted_string_builder.h"
#include "number_utypes.h"
#include "number_modifiers.h"
#include "formattedval_impl.h"
#include "number_utils.h"

// Copied from uscript_props.cpp

U_NAMESPACE_BEGIN

// RelativeDateTimeFormatter specific data for a single locale
class RelativeDateTimeCacheData: public SharedObject {
public:
    RelativeDateTimeCacheData() : combinedDateAndTime(nullptr) {
        // Initialize the cache arrays
        for (int32_t style = 0; style < UDAT_STYLE_COUNT; ++style) {
            for (int32_t relUnit = 0; relUnit < UDAT_REL_UNIT_COUNT; ++relUnit) {
                for (int32_t pl = 0; pl < StandardPlural::COUNT; ++pl) {
                    relativeUnitsFormatters[style][relUnit][0][pl] = nullptr;
                    relativeUnitsFormatters[style][relUnit][1][pl] = nullptr;
                }
            }
        }
        for (int32_t i = 0; i < UDAT_STYLE_COUNT; ++i) {
          fallBackCache[i] = -1;
        }
    }
    virtual ~RelativeDateTimeCacheData();

    // no numbers: e.g Next Tuesday; Yesterday; etc.
    UnicodeString absoluteUnits[UDAT_STYLE_COUNT][UDAT_ABSOLUTE_UNIT_COUNT][UDAT_DIRECTION_COUNT];

    // SimpleFormatter pointers for relative unit format,
    // e.g., Next Tuesday; Yesterday; etc. For third index, 0
    // means past, e.g., 5 days ago; 1 means future, e.g., in 5 days.
    SimpleFormatter *relativeUnitsFormatters[UDAT_STYLE_COUNT]
        [UDAT_REL_UNIT_COUNT][2][StandardPlural::COUNT];

    const UnicodeString& getAbsoluteUnitString(int32_t fStyle,
                                               UDateAbsoluteUnit unit,
                                               UDateDirection direction) const;
    const SimpleFormatter* getRelativeUnitFormatter(int32_t fStyle,
                                                    UDateRelativeUnit unit,
                                                    int32_t pastFutureIndex,
                                                    int32_t pluralUnit) const;
    const SimpleFormatter* getRelativeDateTimeUnitFormatter(int32_t fStyle,
                                                    URelativeDateTimeUnit unit,
                                                    int32_t pastFutureIndex,
                                                    int32_t pluralUnit) const;

    const UnicodeString emptyString;

    // Mapping from source to target styles for alias fallback.
    int32_t fallBackCache[UDAT_STYLE_COUNT];

    void adoptCombinedDateAndTime(SimpleFormatter *fmtToAdopt) {
        delete combinedDateAndTime;
        combinedDateAndTime = fmtToAdopt;
    }
    const SimpleFormatter *getCombinedDateAndTime() const {
        return combinedDateAndTime;
    }

private:
    SimpleFormatter *combinedDateAndTime;
    RelativeDateTimeCacheData(const RelativeDateTimeCacheData &other);
    RelativeDateTimeCacheData& operator=(
            const RelativeDateTimeCacheData &other);
};

RelativeDateTimeCacheData::~RelativeDateTimeCacheData() {
    // clear out the cache arrays
    for (int32_t style = 0; style < UDAT_STYLE_COUNT; ++style) {
        for (int32_t relUnit = 0; relUnit < UDAT_REL_UNIT_COUNT; ++relUnit) {
            for (int32_t pl = 0; pl < StandardPlural::COUNT; ++pl) {
                delete relativeUnitsFormatters[style][relUnit][0][pl];
                delete relativeUnitsFormatters[style][relUnit][1][pl];
            }
        }
    }
    delete combinedDateAndTime;
}


// Use fallback cache for absolute units.
const UnicodeString& RelativeDateTimeCacheData::getAbsoluteUnitString(
        int32_t fStyle, UDateAbsoluteUnit unit, UDateDirection direction) const {
    int32_t style = fStyle;
    do {
        if (!absoluteUnits[style][unit][direction].isEmpty()) {
            return absoluteUnits[style][unit][direction];
        }
        style = fallBackCache[style];
    } while (style != -1);
    return emptyString;
}

 const SimpleFormatter* RelativeDateTimeCacheData::getRelativeUnitFormatter(
        int32_t fStyle,
        UDateRelativeUnit unit,
        int32_t pastFutureIndex,
        int32_t pluralUnit) const {
   URelativeDateTimeUnit rdtunit = UDAT_REL_UNIT_COUNT;
   switch (unit) {
       case UDAT_RELATIVE_YEARS:   rdtunit = UDAT_REL_UNIT_YEAR; break;
       case UDAT_RELATIVE_MONTHS:  rdtunit = UDAT_REL_UNIT_MONTH; break;
       case UDAT_RELATIVE_WEEKS:   rdtunit = UDAT_REL_UNIT_WEEK; break;
       case UDAT_RELATIVE_DAYS:    rdtunit = UDAT_REL_UNIT_DAY; break;
       case UDAT_RELATIVE_HOURS:   rdtunit = UDAT_REL_UNIT_HOUR; break;
       case UDAT_RELATIVE_MINUTES: rdtunit = UDAT_REL_UNIT_MINUTE; break;
       case UDAT_RELATIVE_SECONDS: rdtunit = UDAT_REL_UNIT_SECOND; break;
       default: // a unit that the above method does not handle
            return nullptr;
   }

   return getRelativeDateTimeUnitFormatter(fStyle, rdtunit, pastFutureIndex, pluralUnit);
 }

 // Use fallback cache for SimpleFormatter relativeUnits.
 const SimpleFormatter* RelativeDateTimeCacheData::getRelativeDateTimeUnitFormatter(
        int32_t fStyle,
        URelativeDateTimeUnit unit,
        int32_t pastFutureIndex,
        int32_t pluralUnit) const {
    while (true) {
        int32_t style = fStyle;
        do {
            if (relativeUnitsFormatters[style][unit][pastFutureIndex][pluralUnit] != nullptr) {
                return relativeUnitsFormatters[style][unit][pastFutureIndex][pluralUnit];
            }
            style = fallBackCache[style];
        } while (style != -1);

        if (pluralUnit == StandardPlural::OTHER) {
            break;
        }
        pluralUnit = StandardPlural::OTHER;
    }
    return nullptr;  // No formatter found.
 }

static UBool getStringByIndex(
        const UResourceBundle *resource,
        int32_t idx,
        UnicodeString &result,
        UErrorCode &status) {
    int32_t len = 0;
    const char16_t *resStr = ures_getStringByIndex(
            resource, idx, &len, &status);
    if (U_FAILURE(status)) {
        return false;
    }
    result.setTo(true, resStr, len);
    return true;
}

namespace {

/**
 * Sink for enumerating all of the measurement unit display names.
 *
 * More specific bundles (en_GB) are enumerated before their parents (en_001, en, root):
 * Only store a value if it is still missing, that is, it has not been overridden.
 */
struct RelDateTimeFmtDataSink : public ResourceSink {

    /**
     * Sink for patterns for relative dates and times. For example,
     * fields/relative/...
     */

    // Generic unit enum for storing Unit info.
    typedef enum RelAbsUnit {
        INVALID_UNIT = -1,
        SECOND,
        MINUTE,
        HOUR,
        DAY,
        WEEK,
        MONTH,
        QUARTER,
        YEAR,
        SUNDAY,
        MONDAY,
        TUESDAY,
        WEDNESDAY,
        THURSDAY,
        FRIDAY,
        SATURDAY
    } RelAbsUnit;

    static int32_t relUnitFromGeneric(RelAbsUnit genUnit) {
        // Converts the generic units to UDAT_RELATIVE version.
        switch (genUnit) {
            case SECOND:
                return UDAT_REL_UNIT_SECOND;
            case MINUTE:
                return UDAT_REL_UNIT_MINUTE;
            case HOUR:
                return UDAT_REL_UNIT_HOUR;
            case DAY:
                return UDAT_REL_UNIT_DAY;
            case WEEK:
                return UDAT_REL_UNIT_WEEK;
            case MONTH:
                return UDAT_REL_UNIT_MONTH;
            case QUARTER:
                return UDAT_REL_UNIT_QUARTER;
            case YEAR:
                return UDAT_REL_UNIT_YEAR;
            case SUNDAY:
                return UDAT_REL_UNIT_SUNDAY;
            case MONDAY:
                return UDAT_REL_UNIT_MONDAY;
            case TUESDAY:
                return UDAT_REL_UNIT_TUESDAY;
            case WEDNESDAY:
                return UDAT_REL_UNIT_WEDNESDAY;
            case THURSDAY:
                return UDAT_REL_UNIT_THURSDAY;
            case FRIDAY:
                return UDAT_REL_UNIT_FRIDAY;
            case SATURDAY:
                return UDAT_REL_UNIT_SATURDAY;
            default:
                return -1;
        }
    }

    static int32_t absUnitFromGeneric(RelAbsUnit genUnit) {
        // Converts the generic units to UDAT_RELATIVE version.
        switch (genUnit) {
            case DAY:
                return UDAT_ABSOLUTE_DAY;
            case WEEK:
                return UDAT_ABSOLUTE_WEEK;
            case MONTH:
                return UDAT_ABSOLUTE_MONTH;
            case QUARTER:
                return UDAT_ABSOLUTE_QUARTER;
            case YEAR:
                return UDAT_ABSOLUTE_YEAR;
            case SUNDAY:
                return UDAT_ABSOLUTE_SUNDAY;
            case MONDAY:
                return UDAT_ABSOLUTE_MONDAY;
            case TUESDAY:
                return UDAT_ABSOLUTE_TUESDAY;
            case WEDNESDAY:
                return UDAT_ABSOLUTE_WEDNESDAY;
            case THURSDAY:
                return UDAT_ABSOLUTE_THURSDAY;
            case FRIDAY:
                return UDAT_ABSOLUTE_FRIDAY;
            case SATURDAY:
                return UDAT_ABSOLUTE_SATURDAY;
            case HOUR:
                return UDAT_ABSOLUTE_HOUR;
            case MINUTE:
                return UDAT_ABSOLUTE_MINUTE;
            default:
                return -1;
        }
    }

    static int32_t keyToDirection(const char* key) {
        if (uprv_strcmp(key, "-2") == 0) {
            return UDAT_DIRECTION_LAST_2;
        }
        if (uprv_strcmp(key, "-1") == 0) {
            return UDAT_DIRECTION_LAST;
        }
        if (uprv_strcmp(key, "0") == 0) {
            return UDAT_DIRECTION_THIS;
        }
        if (uprv_strcmp(key, "1") == 0) {
            return UDAT_DIRECTION_NEXT;
        }
        if (uprv_strcmp(key, "2") == 0) {
            return UDAT_DIRECTION_NEXT_2;
        }
        return -1;
    }

    // Values kept between levels of parsing the CLDR data.
    int32_t pastFutureIndex;  // 0 == past or 1 ==  future
    UDateRelativeDateTimeFormatterStyle style;  // {LONG, SHORT, NARROW}
    RelAbsUnit genericUnit;

    RelativeDateTimeCacheData &outputData;

    // Constructor
    RelDateTimeFmtDataSink(RelativeDateTimeCacheData& cacheData)
        : outputData(cacheData) {
        // Clear cacheData.fallBackCache
        cacheData.fallBackCache[UDAT_STYLE_LONG] = -1;
        cacheData.fallBackCache[UDAT_STYLE_SHORT] = -1;
        cacheData.fallBackCache[UDAT_STYLE_NARROW] = -1;
    }

    ~RelDateTimeFmtDataSink();

    // Utility functions
    static UDateRelativeDateTimeFormatterStyle styleFromString(const char *s) {
        int32_t len = static_cast<int32_t>(uprv_strlen(s));
        if (len >= 7 && uprv_strcmp(s + len - 7, "-narrow") == 0) {
            return UDAT_STYLE_NARROW;
        }
        if (len >= 6 && uprv_strcmp(s + len - 6, "-short") == 0) {
            return UDAT_STYLE_SHORT;
        }
        return UDAT_STYLE_LONG;
    }

    static int32_t styleSuffixLength(UDateRelativeDateTimeFormatterStyle style) {
        switch (style) {
            case UDAT_STYLE_NARROW:
                return 7;
            case UDAT_STYLE_SHORT:
                return 6;
            default:
                return 0;
        }
    }

    // Utility functions
    static UDateRelativeDateTimeFormatterStyle styleFromAliasUnicodeString(UnicodeString s) {
        static const char16_t narrow[7] = {0x002D, 0x006E, 0x0061, 0x0072, 0x0072, 0x006F, 0x0077};
        static const char16_t sshort[6] = {0x002D, 0x0073, 0x0068, 0x006F, 0x0072, 0x0074,};
        if (s.endsWith(narrow, 7)) {
            return UDAT_STYLE_NARROW;
        }
        if (s.endsWith(sshort, 6)) {
            return UDAT_STYLE_SHORT;
        }
        return UDAT_STYLE_LONG;
    }

    static RelAbsUnit unitOrNegativeFromString(const char* keyword, int32_t length) {
        // Quick check from string to enum.
        switch (length) {
            case 3:
                if (uprv_strncmp(keyword, "day", length) == 0) {
                    return DAY;
                } else if (uprv_strncmp(keyword, "sun", length) == 0) {
                    return SUNDAY;
                } else if (uprv_strncmp(keyword, "mon", length) == 0) {
                    return MONDAY;
                } else if (uprv_strncmp(keyword, "tue", length) == 0) {
                    return TUESDAY;
                } else if (uprv_strncmp(keyword, "wed", length) == 0) {
                    return WEDNESDAY;
                } else if (uprv_strncmp(keyword, "thu", length) == 0) {
                    return THURSDAY;
                } else if (uprv_strncmp(keyword, "fri", length) == 0) {
                    return FRIDAY;
                } else if (uprv_strncmp(keyword, "sat", length) == 0) {
                    return SATURDAY;
                }
                break;
            case 4:
                if (uprv_strncmp(keyword, "hour", length) == 0) {
                    return HOUR;
                } else if (uprv_strncmp(keyword, "week", length) == 0) {
                    return WEEK;
                } else if (uprv_strncmp(keyword, "year", length) == 0) {
                    return YEAR;
                }
                break;
            case 5:
                if (uprv_strncmp(keyword, "month", length) == 0) {
                    return MONTH;
                }
                break;
            case 6:
                if (uprv_strncmp(keyword, "minute", length) == 0) {
                    return MINUTE;
                } else if (uprv_strncmp(keyword, "second", length) == 0) {
                    return SECOND;
                }
                break;
            case 7:
                if (uprv_strncmp(keyword, "quarter", length) == 0) {
                    return QUARTER;  // TODO: Check @provisional
                  }
                break;
            default:
                break;
        }
        return INVALID_UNIT;
    }

    void handlePlainDirection(ResourceValue &value, UErrorCode &errorCode) {
        // Handle Display Name for PLAIN direction for some units.
        if (U_FAILURE(errorCode)) { return; }

        int32_t absUnit = absUnitFromGeneric(genericUnit);
        if (absUnit < 0) {
          return;  // Not interesting.
        }

        // Store displayname if not set.
        if (outputData.absoluteUnits[style]
            [absUnit][UDAT_DIRECTION_PLAIN].isEmpty()) {
            outputData.absoluteUnits[style]
                [absUnit][UDAT_DIRECTION_PLAIN].fastCopyFrom(value.getUnicodeString(errorCode));
            return;
        }
    }

    void consumeTableRelative(const char *key, ResourceValue &value, UErrorCode &errorCode) {
        ResourceTable unitTypesTable = value.getTable(errorCode);
        if (U_FAILURE(errorCode)) { return; }

        for (int32_t i = 0; unitTypesTable.getKeyAndValue(i, key, value); ++i) {
            if (value.getType() == URES_STRING) {
                int32_t direction = keyToDirection(key);
                if (direction < 0) {
                  continue;
                }

                int32_t relUnitIndex = relUnitFromGeneric(genericUnit);
                if (relUnitIndex == UDAT_REL_UNIT_SECOND && uprv_strcmp(key, "0") == 0 &&
                    outputData.absoluteUnits[style][UDAT_ABSOLUTE_NOW][UDAT_DIRECTION_PLAIN].isEmpty()) {
                    // Handle "NOW"
                    outputData.absoluteUnits[style][UDAT_ABSOLUTE_NOW]
                        [UDAT_DIRECTION_PLAIN].fastCopyFrom(value.getUnicodeString(errorCode));
                }

                int32_t absUnitIndex = absUnitFromGeneric(genericUnit);
                if (absUnitIndex < 0) {
                    continue;
                }
                // Only reset if slot is empty.
                if (outputData.absoluteUnits[style][absUnitIndex][direction].isEmpty()) {
                    outputData.absoluteUnits[style][absUnitIndex]
                        [direction].fastCopyFrom(value.getUnicodeString(errorCode));
                }
            }
        }
    }

    void consumeTimeDetail(int32_t relUnitIndex,
                           const char *key, ResourceValue &value, UErrorCode &errorCode) {
        ResourceTable unitTypesTable = value.getTable(errorCode);
        if (U_FAILURE(errorCode)) { return; }

          for (int32_t i = 0; unitTypesTable.getKeyAndValue(i, key, value); ++i) {
            if (value.getType() == URES_STRING) {
                int32_t pluralIndex = StandardPlural::indexOrNegativeFromString(key);
                if (pluralIndex >= 0) {
                    SimpleFormatter **patterns =
                        outputData.relativeUnitsFormatters[style][relUnitIndex]
                        [pastFutureIndex];
                    // Only set if not already established.
                    if (patterns[pluralIndex] == nullptr) {
                        patterns[pluralIndex] = new SimpleFormatter(
                            value.getUnicodeString(errorCode), 0, 1, errorCode);
                        if (patterns[pluralIndex] == nullptr) {
                            errorCode = U_MEMORY_ALLOCATION_ERROR;
                        }
                    }
                }
            }
        }
    }

    void consumeTableRelativeTime(const char *key, ResourceValue &value, UErrorCode &errorCode) {
        ResourceTable relativeTimeTable = value.getTable(errorCode);
        if (U_FAILURE(errorCode)) { return; }

        int32_t relUnitIndex = relUnitFromGeneric(genericUnit);
        if (relUnitIndex < 0) {
            return;
        }
        for (int32_t i = 0; relativeTimeTable.getKeyAndValue(i, key, value); ++i) {
            if (uprv_strcmp(key, "past") == 0) {
                pastFutureIndex = 0;
            } else if (uprv_strcmp(key, "future") == 0) {
                pastFutureIndex = 1;
            } else {
                // Unknown key.
                continue;
            }
            consumeTimeDetail(relUnitIndex, key, value, errorCode);
        }
    }

    void consumeAlias(const char *key, const ResourceValue &value, UErrorCode &errorCode) {

        UDateRelativeDateTimeFormatterStyle sourceStyle = styleFromString(key);
        const UnicodeString valueStr = value.getAliasUnicodeString(errorCode);
        if (U_FAILURE(errorCode)) { return; }

        UDateRelativeDateTimeFormatterStyle targetStyle =
            styleFromAliasUnicodeString(valueStr);

        if (sourceStyle == targetStyle) {
            errorCode = U_INVALID_FORMAT_ERROR;
            return;
        }
        if (outputData.fallBackCache[sourceStyle] != -1 &&
            outputData.fallBackCache[sourceStyle] != targetStyle) {
            errorCode = U_INVALID_FORMAT_ERROR;
            return;
        }
        outputData.fallBackCache[sourceStyle] = targetStyle;
    }

    void consumeTimeUnit(const char *key, ResourceValue &value, UErrorCode &errorCode) {
        ResourceTable unitTypesTable = value.getTable(errorCode);
        if (U_FAILURE(errorCode)) { return; }

        for (int32_t i = 0; unitTypesTable.getKeyAndValue(i, key, value); ++i) {
            // Handle display name.
            if (uprv_strcmp(key, "dn") == 0 && value.getType() == URES_STRING) {
                handlePlainDirection(value, errorCode);
            }
            if (value.getType() == URES_TABLE) {
                if (uprv_strcmp(key, "relative") == 0) {
                    consumeTableRelative(key, value, errorCode);
                } else if (uprv_strcmp(key, "relativeTime") == 0) {
                    consumeTableRelativeTime(key, value, errorCode);
                }
            }
        }
    }

    virtual void put(const char *key, ResourceValue &value,
                     UBool /*noFallback*/, UErrorCode &errorCode) override {
        // Main entry point to sink
        ResourceTable table = value.getTable(errorCode);
        if (U_FAILURE(errorCode)) { return; }
        for (int32_t i = 0; table.getKeyAndValue(i, key, value); ++i) {
            if (value.getType() == URES_ALIAS) {
                consumeAlias(key, value, errorCode);
            } else {
                style = styleFromString(key);
                int32_t unitSize = static_cast<int32_t>(uprv_strlen(key)) - styleSuffixLength(style);
                genericUnit = unitOrNegativeFromString(key, unitSize);
                if (style >= 0 && genericUnit != INVALID_UNIT) {
                    consumeTimeUnit(key, value, errorCode);
                }
            }
        }
    }

};

// Virtual destructors must be defined out of line.
RelDateTimeFmtDataSink::~RelDateTimeFmtDataSink() {}
} // namespace

static const DateFormatSymbols::DtWidthType styleToDateFormatSymbolWidth[UDAT_STYLE_COUNT] = {
  DateFormatSymbols::WIDE, DateFormatSymbols::SHORT, DateFormatSymbols::NARROW
};

// Get days of weeks from the DateFormatSymbols class.
static void loadWeekdayNames(UnicodeString absoluteUnits[UDAT_STYLE_COUNT]
                                 [UDAT_ABSOLUTE_UNIT_COUNT][UDAT_DIRECTION_COUNT],
                             const char* localeId,
                             UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }
    Locale locale(localeId);
    DateFormatSymbols dfSym(locale, status);
    if (U_FAILURE(status)) {
        return;
    }
    for (int32_t style = 0; style < UDAT_STYLE_COUNT; ++style) {
        DateFormatSymbols::DtWidthType dtfmtWidth = styleToDateFormatSymbolWidth[style];
        int32_t count;
        const UnicodeString* weekdayNames =
            dfSym.getWeekdays(count, DateFormatSymbols::STANDALONE, dtfmtWidth);
        for (int32_t dayIndex = UDAT_ABSOLUTE_SUNDAY;
                dayIndex <= UDAT_ABSOLUTE_SATURDAY; ++ dayIndex) {
            int32_t dateSymbolIndex = (dayIndex - UDAT_ABSOLUTE_SUNDAY) + UCAL_SUNDAY;
            absoluteUnits[style][dayIndex][UDAT_DIRECTION_PLAIN].fastCopyFrom(
                weekdayNames[dateSymbolIndex]);
        }
    }
}

static UBool loadUnitData(
        const UResourceBundle *resource,
        RelativeDateTimeCacheData &cacheData,
        const char* localeId,
        UErrorCode &status) {

    RelDateTimeFmtDataSink sink(cacheData);

    ures_getAllItemsWithFallback(resource, "fields", sink, status);
    if (U_FAILURE(status)) {
        return false;
    }

    // Get the weekday names from DateFormatSymbols.
    loadWeekdayNames(cacheData.absoluteUnits, localeId, status);
    return U_SUCCESS(status);
}

static const int32_t cTypeBufMax = 32;

static UBool getDateTimePattern(
        Locale locale,
        const UResourceBundle *resource,
        UnicodeString &result,
        UErrorCode &status) {
    if (U_FAILURE(status)) {
        return false;
    }
    char cType[cTypeBufMax + 1];
    Calendar::getCalendarTypeFromLocale(locale, cType, cTypeBufMax, status);
    cType[cTypeBufMax] = 0;
    if (U_FAILURE(status) || cType[0] == 0) {
        status = U_ZERO_ERROR;
        uprv_strcpy(cType, "gregorian");
    }

    LocalUResourceBundlePointer topLevel;
    int32_t dateTimeFormatOffset = DateFormat::kMedium;
    CharString pathBuffer;
    // Currently, for compatibility with pre-CLDR-42 data, we default to the "atTime"
    // combining patterns. Depending on guidance in CLDR 42 spec and on DisplayOptions,
    // we may change this.
    pathBuffer.append("calendar/", status)
            .append(cType, status)
            .append("/DateTimePatterns%atTime", status);
    topLevel.adoptInstead(
            ures_getByKeyWithFallback(
                    resource, pathBuffer.data(), nullptr, &status));
    if (U_FAILURE(status) ||  ures_getSize(topLevel.getAlias()) < 4) {
        // Fall back to standard combining patterns
        status = U_ZERO_ERROR;
        dateTimeFormatOffset = DateFormat::kDateTime;
        pathBuffer.clear();
        pathBuffer.append("calendar/", status)
                .append(cType, status)
                .append("/DateTimePatterns", status);
        topLevel.adoptInstead(
                ures_getByKeyWithFallback(
                        resource, pathBuffer.data(), nullptr, &status));
    }
    if (U_FAILURE(status)) {
        return false;
    }
    if (dateTimeFormatOffset == DateFormat::kDateTime && ures_getSize(topLevel.getAlias()) <= DateFormat::kDateTime) {
        // Oops, size is too small to access the index that we want, fallback
        // to a hard-coded value.
        result = UNICODE_STRING_SIMPLE("{1} {0}");
        return true;
    }
    return getStringByIndex(topLevel.getAlias(), dateTimeFormatOffset, result, status);
}

template<>
const RelativeDateTimeCacheData *LocaleCacheKey<RelativeDateTimeCacheData>::createObject(const void * /*unused*/, UErrorCode &status) const {
    const char *localeId = fLoc.getName();
    LocalUResourceBundlePointer topLevel(ures_open(nullptr, localeId, &status));
    if (U_FAILURE(status)) {
        return nullptr;
    }
    LocalPointer<RelativeDateTimeCacheData> result(
            new RelativeDateTimeCacheData());
    if (result.isNull()) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    if (!loadUnitData(
            topLevel.getAlias(),
            *result,
            localeId,
            status)) {
        return nullptr;
    }
    UnicodeString dateTimePattern;
    if (!getDateTimePattern(fLoc, topLevel.getAlias(), dateTimePattern, status)) {
        return nullptr;
    }
    result->adoptCombinedDateAndTime(
            new SimpleFormatter(dateTimePattern, 2, 2, status));
    if (U_FAILURE(status)) {
        return nullptr;
    }
    result->addRef();
    return result.orphan();
}



static constexpr FormattedStringBuilder::Field kRDTNumericField
    = {UFIELD_CATEGORY_RELATIVE_DATETIME, UDAT_REL_NUMERIC_FIELD};

static constexpr FormattedStringBuilder::Field kRDTLiteralField
    = {UFIELD_CATEGORY_RELATIVE_DATETIME, UDAT_REL_LITERAL_FIELD};

class FormattedRelativeDateTimeData : public FormattedValueStringBuilderImpl {
public:
    FormattedRelativeDateTimeData() : FormattedValueStringBuilderImpl(kRDTNumericField) {}
    virtual ~FormattedRelativeDateTimeData();
};

FormattedRelativeDateTimeData::~FormattedRelativeDateTimeData() = default;


UPRV_FORMATTED_VALUE_SUBCLASS_AUTO_IMPL(FormattedRelativeDateTime)


RelativeDateTimeFormatter::RelativeDateTimeFormatter(UErrorCode& status) :
        fCache(nullptr),
        fNumberFormat(nullptr),
        fPluralRules(nullptr),
        fStyle(UDAT_STYLE_LONG),
        fContext(UDISPCTX_CAPITALIZATION_NONE),
        fOptBreakIterator(nullptr) {
    (void)fOptBreakIterator; // suppress unused field warning
    init(nullptr, nullptr, status);
}

RelativeDateTimeFormatter::RelativeDateTimeFormatter(
        const Locale& locale, UErrorCode& status) :
        fCache(nullptr),
        fNumberFormat(nullptr),
        fPluralRules(nullptr),
        fStyle(UDAT_STYLE_LONG),
        fContext(UDISPCTX_CAPITALIZATION_NONE),
        fOptBreakIterator(nullptr),
        fLocale(locale) {
    init(nullptr, nullptr, status);
}

RelativeDateTimeFormatter::RelativeDateTimeFormatter(
        const Locale& locale, NumberFormat *nfToAdopt, UErrorCode& status) :
        fCache(nullptr),
        fNumberFormat(nullptr),
        fPluralRules(nullptr),
        fStyle(UDAT_STYLE_LONG),
        fContext(UDISPCTX_CAPITALIZATION_NONE),
        fOptBreakIterator(nullptr),
        fLocale(locale) {
    init(nfToAdopt, nullptr, status);
}

RelativeDateTimeFormatter::RelativeDateTimeFormatter(
        const Locale& locale,
        NumberFormat *nfToAdopt,
        UDateRelativeDateTimeFormatterStyle styl,
        UDisplayContext capitalizationContext,
        UErrorCode& status) :
        fCache(nullptr),
        fNumberFormat(nullptr),
        fPluralRules(nullptr),
        fStyle(styl),
        fContext(capitalizationContext),
        fOptBreakIterator(nullptr),
        fLocale(locale) {
    if (U_FAILURE(status)) {
        return;
    }
    if (styl < 0 || UDAT_STYLE_COUNT <= styl) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    if ((capitalizationContext >> 8) != UDISPCTX_TYPE_CAPITALIZATION) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    if (capitalizationContext == UDISPCTX_CAPITALIZATION_FOR_BEGINNING_OF_SENTENCE) {
#if !UCONFIG_NO_BREAK_ITERATION
        BreakIterator *bi = BreakIterator::createSentenceInstance(locale, status);
        if (U_FAILURE(status)) {
            return;
        }
        init(nfToAdopt, bi, status);
#else
        status = U_UNSUPPORTED_ERROR;
        return;
#endif // !UCONFIG_NO_BREAK_ITERATION
    } else {
        init(nfToAdopt, nullptr, status);
    }
}

RelativeDateTimeFormatter::RelativeDateTimeFormatter(
        const RelativeDateTimeFormatter& other)
        : UObject(other),
          fCache(other.fCache),
          fNumberFormat(other.fNumberFormat),
          fPluralRules(other.fPluralRules),
          fStyle(other.fStyle),
          fContext(other.fContext),
          fOptBreakIterator(other.fOptBreakIterator),
          fLocale(other.fLocale) {
    fCache->addRef();
    fNumberFormat->addRef();
    fPluralRules->addRef();
#if !UCONFIG_NO_BREAK_ITERATION
    if (fOptBreakIterator != nullptr) {
      fOptBreakIterator->addRef();
    }
#endif // !UCONFIG_NO_BREAK_ITERATION
}

RelativeDateTimeFormatter& RelativeDateTimeFormatter::operator=(
        const RelativeDateTimeFormatter& other) {
    if (this != &other) {
        SharedObject::copyPtr(other.fCache, fCache);
        SharedObject::copyPtr(other.fNumberFormat, fNumberFormat);
        SharedObject::copyPtr(other.fPluralRules, fPluralRules);
#if !UCONFIG_NO_BREAK_ITERATION
        SharedObject::copyPtr(other.fOptBreakIterator, fOptBreakIterator);
#endif // !UCONFIG_NO_BREAK_ITERATION
        fStyle = other.fStyle;
        fContext = other.fContext;
        fLocale = other.fLocale;
    }
    return *this;
}

RelativeDateTimeFormatter::~RelativeDateTimeFormatter() {
    if (fCache != nullptr) {
        fCache->removeRef();
    }
    if (fNumberFormat != nullptr) {
        fNumberFormat->removeRef();
    }
    if (fPluralRules != nullptr) {
        fPluralRules->removeRef();
    }
#if !UCONFIG_NO_BREAK_ITERATION
    if (fOptBreakIterator != nullptr) {
        fOptBreakIterator->removeRef();
    }
#endif // !UCONFIG_NO_BREAK_ITERATION
}

const NumberFormat& RelativeDateTimeFormatter::getNumberFormat() const {
    return **fNumberFormat;
}

UDisplayContext RelativeDateTimeFormatter::getCapitalizationContext() const {
    return fContext;
}

UDateRelativeDateTimeFormatterStyle RelativeDateTimeFormatter::getFormatStyle() const {
    return fStyle;
}


// To reduce boilerplate code, we use a helper function that forwards variadic
// arguments to the formatImpl function.

template<typename F, typename... Args>
UnicodeString& RelativeDateTimeFormatter::doFormat(
        F callback,
        UnicodeString& appendTo,
        UErrorCode& status,
        Args... args) const {
    FormattedRelativeDateTimeData output;
    (this->*callback)(std::forward<Args>(args)..., output, status);
    if (U_FAILURE(status)) {
        return appendTo;
    }
    UnicodeString result = output.getStringRef().toUnicodeString();
    return appendTo.append(adjustForContext(result));
}

template<typename F, typename... Args>
FormattedRelativeDateTime RelativeDateTimeFormatter::doFormatToValue(
        F callback,
        UErrorCode& status,
        Args... args) const {
    if (!checkNoAdjustForContext(status)) {
        return FormattedRelativeDateTime(status);
    }
    LocalPointer<FormattedRelativeDateTimeData> output(
        new FormattedRelativeDateTimeData(), status);
    if (U_FAILURE(status)) {
        return FormattedRelativeDateTime(status);
    }
    (this->*callback)(std::forward<Args>(args)..., *output, status);
    output->getStringRef().writeTerminator(status);
    return FormattedRelativeDateTime(output.orphan());
}

UnicodeString& RelativeDateTimeFormatter::format(
        double quantity,
        UDateDirection direction,
        UDateRelativeUnit unit,
        UnicodeString& appendTo,
        UErrorCode& status) const {
    return doFormat(
        &RelativeDateTimeFormatter::formatImpl,
        appendTo,
        status,
        quantity,
        direction,
        unit);
}

FormattedRelativeDateTime RelativeDateTimeFormatter::formatToValue(
        double quantity,
        UDateDirection direction,
        UDateRelativeUnit unit,
        UErrorCode& status) const {
    return doFormatToValue(
        &RelativeDateTimeFormatter::formatImpl,
        status,
        quantity,
        direction,
        unit);
}

void RelativeDateTimeFormatter::formatImpl(
        double quantity,
        UDateDirection direction,
        UDateRelativeUnit unit,
        FormattedRelativeDateTimeData& output,
        UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return;
    }
    if (direction != UDAT_DIRECTION_LAST && direction != UDAT_DIRECTION_NEXT) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    int32_t bFuture = direction == UDAT_DIRECTION_NEXT ? 1 : 0;

    StandardPlural::Form pluralForm;
    QuantityFormatter::formatAndSelect(
        quantity,
        **fNumberFormat,
        **fPluralRules,
        output.getStringRef(),
        pluralForm,
        status);
    if (U_FAILURE(status)) {
        return;
    }

    const SimpleFormatter* formatter =
        fCache->getRelativeUnitFormatter(fStyle, unit, bFuture, pluralForm);
    if (formatter == nullptr) {
        // TODO: WARN - look at quantity formatter's action with an error.
        status = U_INVALID_FORMAT_ERROR;
        return;
    }

    number::impl::SimpleModifier modifier(*formatter, kRDTLiteralField, false);
    modifier.formatAsPrefixSuffix(
        output.getStringRef(), 0, output.getStringRef().length(), status);
}

UnicodeString& RelativeDateTimeFormatter::formatNumeric(
        double offset,
        URelativeDateTimeUnit unit,
        UnicodeString& appendTo,
        UErrorCode& status) const {
    return doFormat(
        &RelativeDateTimeFormatter::formatNumericImpl,
        appendTo,
        status,
        offset,
        unit);
}

FormattedRelativeDateTime RelativeDateTimeFormatter::formatNumericToValue(
        double offset,
        URelativeDateTimeUnit unit,
        UErrorCode& status) const {
    return doFormatToValue(
        &RelativeDateTimeFormatter::formatNumericImpl,
        status,
        offset,
        unit);
}

void RelativeDateTimeFormatter::formatNumericImpl(
        double offset,
        URelativeDateTimeUnit unit,
        FormattedRelativeDateTimeData& output,
        UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return;
    }
    if (unit < 0 || UDAT_REL_UNIT_COUNT <= unit) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    UDateDirection direction = UDAT_DIRECTION_NEXT;
    if (std::signbit(offset)) { // needed to handle -0.0
        direction = UDAT_DIRECTION_LAST;
        offset = -offset;
    }
    if (direction != UDAT_DIRECTION_LAST && direction != UDAT_DIRECTION_NEXT) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    int32_t bFuture = direction == UDAT_DIRECTION_NEXT ? 1 : 0;

    StandardPlural::Form pluralForm;
    QuantityFormatter::formatAndSelect(
        offset,
        **fNumberFormat,
        **fPluralRules,
        output.getStringRef(),
        pluralForm,
        status);
    if (U_FAILURE(status)) {
        return;
    }

    const SimpleFormatter* formatter =
        fCache->getRelativeDateTimeUnitFormatter(fStyle, unit, bFuture, pluralForm);
    if (formatter == nullptr) {
        // TODO: WARN - look at quantity formatter's action with an error.
        status = U_INVALID_FORMAT_ERROR;
        return;
    }

    number::impl::SimpleModifier modifier(*formatter, kRDTLiteralField, false);
    modifier.formatAsPrefixSuffix(
        output.getStringRef(), 0, output.getStringRef().length(), status);
}

UnicodeString& RelativeDateTimeFormatter::format(
        UDateDirection direction,
        UDateAbsoluteUnit unit,
        UnicodeString& appendTo,
        UErrorCode& status) const {
    return doFormat(
        &RelativeDateTimeFormatter::formatAbsoluteImpl,
        appendTo,
        status,
        direction,
        unit);
}

FormattedRelativeDateTime RelativeDateTimeFormatter::formatToValue(
        UDateDirection direction,
        UDateAbsoluteUnit unit,
        UErrorCode& status) const {
    return doFormatToValue(
        &RelativeDateTimeFormatter::formatAbsoluteImpl,
        status,
        direction,
        unit);
}

void RelativeDateTimeFormatter::formatAbsoluteImpl(
        UDateDirection direction,
        UDateAbsoluteUnit unit,
        FormattedRelativeDateTimeData& output,
        UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return;
    }
    if ((unit < 0 || UDAT_ABSOLUTE_UNIT_COUNT <= unit) ||
        (direction < 0 || UDAT_DIRECTION_COUNT <= direction) ||
        (unit == UDAT_ABSOLUTE_NOW && direction != UDAT_DIRECTION_PLAIN)) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    // Get string using fallback.
    output.getStringRef().append(
        fCache->getAbsoluteUnitString(fStyle, unit, direction),
        kRDTLiteralField,
        status);
}

UnicodeString& RelativeDateTimeFormatter::format(
        double offset,
        URelativeDateTimeUnit unit,
        UnicodeString& appendTo,
        UErrorCode& status) const {
    return doFormat(
        &RelativeDateTimeFormatter::formatRelativeImpl,
        appendTo,
        status,
        offset,
        unit);
}

FormattedRelativeDateTime RelativeDateTimeFormatter::formatToValue(
        double offset,
        URelativeDateTimeUnit unit,
        UErrorCode& status) const {
    return doFormatToValue(
        &RelativeDateTimeFormatter::formatRelativeImpl,
        status,
        offset,
        unit);
}

void RelativeDateTimeFormatter::formatRelativeImpl(
        double offset,
        URelativeDateTimeUnit unit,
        FormattedRelativeDateTimeData& output,
        UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return;
    }
    // TODO:
    // The full implementation of this depends on CLDR data that is not yet available,
    // see: http://unicode.org/cldr/trac/ticket/9165 Add more relative field data.
    // In the meantime do a quick bring-up by calling the old format method; this
    // leaves some holes (even for data that is currently available, such as quarter).
    // When the new CLDR data is available, update the data storage accordingly,
    // rewrite this to use it directly, and rewrite the old format method to call this
    // new one; that is covered by https://unicode-org.atlassian.net/browse/ICU-12171.
    UDateDirection direction = UDAT_DIRECTION_COUNT;
    if (offset > -2.1 && offset < 2.1) {
        // Allow a 1% epsilon, so offsets in -1.01..-0.99 map to LAST
        double offsetx100 = offset * 100.0;
        int32_t intoffset = (offsetx100 < 0)? (int32_t)(offsetx100-0.5) : (int32_t)(offsetx100+0.5);
        switch (intoffset) {
            case -200/*-2*/: direction = UDAT_DIRECTION_LAST_2; break;
            case -100/*-1*/: direction = UDAT_DIRECTION_LAST; break;
            case    0/* 0*/: direction = UDAT_DIRECTION_THIS; break;
            case  100/* 1*/: direction = UDAT_DIRECTION_NEXT; break;
            case  200/* 2*/: direction = UDAT_DIRECTION_NEXT_2; break;
            default: break;
    	}
    }
    UDateAbsoluteUnit absunit = UDAT_ABSOLUTE_UNIT_COUNT;
    switch (unit) {
        case UDAT_REL_UNIT_YEAR:    absunit = UDAT_ABSOLUTE_YEAR; break;
        case UDAT_REL_UNIT_QUARTER: absunit = UDAT_ABSOLUTE_QUARTER; break;
        case UDAT_REL_UNIT_MONTH:   absunit = UDAT_ABSOLUTE_MONTH; break;
        case UDAT_REL_UNIT_WEEK:    absunit = UDAT_ABSOLUTE_WEEK; break;
        case UDAT_REL_UNIT_DAY:     absunit = UDAT_ABSOLUTE_DAY; break;
        case UDAT_REL_UNIT_SECOND:
            if (direction == UDAT_DIRECTION_THIS) {
                absunit = UDAT_ABSOLUTE_NOW;
                direction = UDAT_DIRECTION_PLAIN;
            }
            break;
        case UDAT_REL_UNIT_SUNDAY:  absunit = UDAT_ABSOLUTE_SUNDAY; break;
        case UDAT_REL_UNIT_MONDAY:  absunit = UDAT_ABSOLUTE_MONDAY; break;
        case UDAT_REL_UNIT_TUESDAY:  absunit = UDAT_ABSOLUTE_TUESDAY; break;
        case UDAT_REL_UNIT_WEDNESDAY:  absunit = UDAT_ABSOLUTE_WEDNESDAY; break;
        case UDAT_REL_UNIT_THURSDAY:  absunit = UDAT_ABSOLUTE_THURSDAY; break;
        case UDAT_REL_UNIT_FRIDAY:  absunit = UDAT_ABSOLUTE_FRIDAY; break;
        case UDAT_REL_UNIT_SATURDAY:  absunit = UDAT_ABSOLUTE_SATURDAY; break;
        case UDAT_REL_UNIT_HOUR:  absunit = UDAT_ABSOLUTE_HOUR; break;
        case UDAT_REL_UNIT_MINUTE:  absunit = UDAT_ABSOLUTE_MINUTE; break;
        default: break;
    }
    if (direction != UDAT_DIRECTION_COUNT && absunit != UDAT_ABSOLUTE_UNIT_COUNT) {
        formatAbsoluteImpl(direction, absunit, output, status);
        if (output.getStringRef().length() != 0) {
            return;
        }
    }
    // otherwise fallback to formatNumeric
    formatNumericImpl(offset, unit, output, status);
}

UnicodeString& RelativeDateTimeFormatter::combineDateAndTime(
        const UnicodeString& relativeDateString, const UnicodeString& timeString,
        UnicodeString& appendTo, UErrorCode& status) const {
    return fCache->getCombinedDateAndTime()->format(
            timeString, relativeDateString, appendTo, status);
}

UnicodeString& RelativeDateTimeFormatter::adjustForContext(UnicodeString &str) const {
#if !UCONFIG_NO_BREAK_ITERATION
    if (fOptBreakIterator == nullptr
        || str.length() == 0 || !u_islower(str.char32At(0))) {
        return str;
    }

    // Must guarantee that one thread at a time accesses the shared break
    // iterator.
    static UMutex gBrkIterMutex;
    Mutex lock(&gBrkIterMutex);
    str.toTitle(
            fOptBreakIterator->get(),
            fLocale,
            U_TITLECASE_NO_LOWERCASE | U_TITLECASE_NO_BREAK_ADJUSTMENT);
#endif // !UCONFIG_NO_BREAK_ITERATION
    return str;
}

UBool RelativeDateTimeFormatter::checkNoAdjustForContext(UErrorCode& status) const {
#if !UCONFIG_NO_BREAK_ITERATION
    // This is unsupported because it's hard to keep fields in sync with title
    // casing. The code could be written and tested if there is demand.
    if (fOptBreakIterator != nullptr) {
        status = U_UNSUPPORTED_ERROR;
        return false;
    }
#else
    (void)status; // suppress unused argument warning
#endif // !UCONFIG_NO_BREAK_ITERATION
    return true;
}

void RelativeDateTimeFormatter::init(
        NumberFormat *nfToAdopt,
#if !UCONFIG_NO_BREAK_ITERATION
        BreakIterator *biToAdopt,
#else
        std::nullptr_t,
#endif // !UCONFIG_NO_BREAK_ITERATION
        UErrorCode &status) {
    LocalPointer<NumberFormat> nf(nfToAdopt);
#if !UCONFIG_NO_BREAK_ITERATION
    LocalPointer<BreakIterator> bi(biToAdopt);
#endif // !UCONFIG_NO_BREAK_ITERATION
    UnifiedCache::getByLocale(fLocale, fCache, status);
    if (U_FAILURE(status)) {
        return;
    }
    const SharedPluralRules *pr = PluralRules::createSharedInstance(
            fLocale, UPLURAL_TYPE_CARDINAL, status);
    if (U_FAILURE(status)) {
        return;
    }
    SharedObject::copyPtr(pr, fPluralRules);
    pr->removeRef();
    if (nf.isNull()) {
       const SharedNumberFormat *shared = NumberFormat::createSharedInstance(
               fLocale, UNUM_DECIMAL, status);
        if (U_FAILURE(status)) {
            return;
        }
        SharedObject::copyPtr(shared, fNumberFormat);
        shared->removeRef();
    } else {
        SharedNumberFormat *shared = new SharedNumberFormat(nf.getAlias());
        if (shared == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
        nf.orphan();
        SharedObject::copyPtr(shared, fNumberFormat);
    }
#if !UCONFIG_NO_BREAK_ITERATION
    if (bi.isNull()) {
        SharedObject::clearPtr(fOptBreakIterator);
    } else {
        SharedBreakIterator *shared = new SharedBreakIterator(bi.getAlias());
        if (shared == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
        bi.orphan();
        SharedObject::copyPtr(shared, fOptBreakIterator);
    }
#endif // !UCONFIG_NO_BREAK_ITERATION
}

U_NAMESPACE_END

// Plain C API

U_NAMESPACE_USE


// Magic number: "FRDT" (FormattedRelativeDateTime) in ASCII
UPRV_FORMATTED_VALUE_CAPI_AUTO_IMPL(
    FormattedRelativeDateTime,
    UFormattedRelativeDateTime,
    UFormattedRelativeDateTimeImpl,
    UFormattedRelativeDateTimeApiHelper,
    ureldatefmt,
    0x46524454)


U_CAPI URelativeDateTimeFormatter* U_EXPORT2
ureldatefmt_open( const char*          locale,
                  UNumberFormat*       nfToAdopt,
                  UDateRelativeDateTimeFormatterStyle width,
                  UDisplayContext      capitalizationContext,
                  UErrorCode*          status )
{
    if (U_FAILURE(*status)) {
        return nullptr;
    }
    LocalPointer<RelativeDateTimeFormatter> formatter(new RelativeDateTimeFormatter(Locale(locale),
                                                              (NumberFormat*)nfToAdopt, width,
                                                              capitalizationContext, *status), *status);
    if (U_FAILURE(*status)) {
        return nullptr;
    }
    return (URelativeDateTimeFormatter*)formatter.orphan();
}

U_CAPI void U_EXPORT2
ureldatefmt_close(URelativeDateTimeFormatter *reldatefmt)
{
    delete (RelativeDateTimeFormatter*)reldatefmt;
}

U_CAPI int32_t U_EXPORT2
ureldatefmt_formatNumeric( const URelativeDateTimeFormatter* reldatefmt,
                    double                offset,
                    URelativeDateTimeUnit unit,
                    char16_t*                result,
                    int32_t               resultCapacity,
                    UErrorCode*           status)
{
    if (U_FAILURE(*status)) {
        return 0;
    }
    if (result == nullptr ? resultCapacity != 0 : resultCapacity < 0) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    UnicodeString res;
    if (result != nullptr) {
        // nullptr destination for pure preflighting: empty dummy string
        // otherwise, alias the destination buffer (copied from udat_format)
        res.setTo(result, 0, resultCapacity);
    }
    ((RelativeDateTimeFormatter*)reldatefmt)->formatNumeric(offset, unit, res, *status);
    if (U_FAILURE(*status)) {
        return 0;
    }
    return res.extract(result, resultCapacity, *status);
}

U_CAPI void U_EXPORT2
ureldatefmt_formatNumericToResult(
        const URelativeDateTimeFormatter* reldatefmt,
        double                            offset,
        URelativeDateTimeUnit             unit,
        UFormattedRelativeDateTime*       result,
        UErrorCode*                       status) {
    if (U_FAILURE(*status)) {
        return;
    }
    const auto* fmt = reinterpret_cast<const RelativeDateTimeFormatter*>(reldatefmt);
    auto* resultImpl = UFormattedRelativeDateTimeApiHelper::validate(result, *status);
    resultImpl->fImpl = fmt->formatNumericToValue(offset, unit, *status);
}

U_CAPI int32_t U_EXPORT2
ureldatefmt_format( const URelativeDateTimeFormatter* reldatefmt,
                    double                offset,
                    URelativeDateTimeUnit unit,
                    char16_t*                result,
                    int32_t               resultCapacity,
                    UErrorCode*           status)
{
    if (U_FAILURE(*status)) {
        return 0;
    }
    if (result == nullptr ? resultCapacity != 0 : resultCapacity < 0) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    UnicodeString res;
    if (result != nullptr) {
        // nullptr destination for pure preflighting: empty dummy string
        // otherwise, alias the destination buffer (copied from udat_format)
        res.setTo(result, 0, resultCapacity);
    }
    ((RelativeDateTimeFormatter*)reldatefmt)->format(offset, unit, res, *status);
    if (U_FAILURE(*status)) {
        return 0;
    }
    return res.extract(result, resultCapacity, *status);
}

U_CAPI void U_EXPORT2
ureldatefmt_formatToResult(
        const URelativeDateTimeFormatter* reldatefmt,
        double                            offset,
        URelativeDateTimeUnit             unit,
        UFormattedRelativeDateTime*       result,
        UErrorCode*                       status) {
    if (U_FAILURE(*status)) {
        return;
    }
    const auto* fmt = reinterpret_cast<const RelativeDateTimeFormatter*>(reldatefmt);
    auto* resultImpl = UFormattedRelativeDateTimeApiHelper::validate(result, *status);
    resultImpl->fImpl = fmt->formatToValue(offset, unit, *status);
}

U_CAPI int32_t U_EXPORT2
ureldatefmt_combineDateAndTime( const URelativeDateTimeFormatter* reldatefmt,
                    const char16_t *     relativeDateString,
                    int32_t           relativeDateStringLen,
                    const char16_t *     timeString,
                    int32_t           timeStringLen,
                    char16_t*            result,
                    int32_t           resultCapacity,
                    UErrorCode*       status )
{
    if (U_FAILURE(*status)) {
        return 0;
    }
    if (result == nullptr ? resultCapacity != 0 : resultCapacity < 0 ||
            (relativeDateString == nullptr ? relativeDateStringLen != 0 : relativeDateStringLen < -1) ||
            (timeString == nullptr ? timeStringLen != 0 : timeStringLen < -1)) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    UnicodeString relDateStr((UBool)(relativeDateStringLen == -1), relativeDateString, relativeDateStringLen);
    UnicodeString timeStr((UBool)(timeStringLen == -1), timeString, timeStringLen);
    UnicodeString res(result, 0, resultCapacity);
    ((RelativeDateTimeFormatter*)reldatefmt)->combineDateAndTime(relDateStr, timeStr, res, *status);
    if (U_FAILURE(*status)) {
        return 0;
    }
    return res.extract(result, resultCapacity, *status);
}

#endif /* !UCONFIG_NO_FORMATTING */
