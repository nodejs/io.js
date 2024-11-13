// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2007-2016, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*/

#include "utypeinfo.h"  // for 'typeid' to work

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/vtzone.h"
#include "unicode/rbtz.h"
#include "unicode/ucal.h"
#include "unicode/ures.h"
#include "cmemory.h"
#include "uvector.h"
#include "gregoimp.h"
#include "uassert.h"

U_NAMESPACE_BEGIN

// Smybol characters used by RFC2445 VTIMEZONE
static const char16_t COLON = 0x3A; /* : */
static const char16_t SEMICOLON = 0x3B; /* ; */
static const char16_t EQUALS_SIGN = 0x3D; /* = */
static const char16_t COMMA = 0x2C; /* , */
static const char16_t PLUS = 0x2B; /* + */
static const char16_t MINUS = 0x2D; /* - */

// RFC2445 VTIMEZONE tokens
static const char16_t ICAL_BEGIN_VTIMEZONE[] = {0x42, 0x45, 0x47, 0x49, 0x4E, 0x3A, 0x56, 0x54, 0x49, 0x4D, 0x45, 0x5A, 0x4F, 0x4E, 0x45, 0}; /* "BEGIN:VTIMEZONE" */
static const char16_t ICAL_END_VTIMEZONE[] = {0x45, 0x4E, 0x44, 0x3A, 0x56, 0x54, 0x49, 0x4D, 0x45, 0x5A, 0x4F, 0x4E, 0x45, 0}; /* "END:VTIMEZONE" */
static const char16_t ICAL_BEGIN[] = {0x42, 0x45, 0x47, 0x49, 0x4E, 0}; /* "BEGIN" */
static const char16_t ICAL_END[] = {0x45, 0x4E, 0x44, 0}; /* "END" */
static const char16_t ICAL_VTIMEZONE[] = {0x56, 0x54, 0x49, 0x4D, 0x45, 0x5A, 0x4F, 0x4E, 0x45, 0}; /* "VTIMEZONE" */
static const char16_t ICAL_TZID[] = {0x54, 0x5A, 0x49, 0x44, 0}; /* "TZID" */
static const char16_t ICAL_STANDARD[] = {0x53, 0x54, 0x41, 0x4E, 0x44, 0x41, 0x52, 0x44, 0}; /* "STANDARD" */
static const char16_t ICAL_DAYLIGHT[] = {0x44, 0x41, 0x59, 0x4C, 0x49, 0x47, 0x48, 0x54, 0}; /* "DAYLIGHT" */
static const char16_t ICAL_DTSTART[] = {0x44, 0x54, 0x53, 0x54, 0x41, 0x52, 0x54, 0}; /* "DTSTART" */
static const char16_t ICAL_TZOFFSETFROM[] = {0x54, 0x5A, 0x4F, 0x46, 0x46, 0x53, 0x45, 0x54, 0x46, 0x52, 0x4F, 0x4D, 0}; /* "TZOFFSETFROM" */
static const char16_t ICAL_TZOFFSETTO[] = {0x54, 0x5A, 0x4F, 0x46, 0x46, 0x53, 0x45, 0x54, 0x54, 0x4F, 0}; /* "TZOFFSETTO" */
static const char16_t ICAL_RDATE[] = {0x52, 0x44, 0x41, 0x54, 0x45, 0}; /* "RDATE" */
static const char16_t ICAL_RRULE[] = {0x52, 0x52, 0x55, 0x4C, 0x45, 0}; /* "RRULE" */
static const char16_t ICAL_TZNAME[] = {0x54, 0x5A, 0x4E, 0x41, 0x4D, 0x45, 0}; /* "TZNAME" */
static const char16_t ICAL_TZURL[] = {0x54, 0x5A, 0x55, 0x52, 0x4C, 0}; /* "TZURL" */
static const char16_t ICAL_LASTMOD[] = {0x4C, 0x41, 0x53, 0x54, 0x2D, 0x4D, 0x4F, 0x44, 0x49, 0x46, 0x49, 0x45, 0x44, 0}; /* "LAST-MODIFIED" */

static const char16_t ICAL_FREQ[] = {0x46, 0x52, 0x45, 0x51, 0}; /* "FREQ" */
static const char16_t ICAL_UNTIL[] = {0x55, 0x4E, 0x54, 0x49, 0x4C, 0}; /* "UNTIL" */
static const char16_t ICAL_YEARLY[] = {0x59, 0x45, 0x41, 0x52, 0x4C, 0x59, 0}; /* "YEARLY" */
static const char16_t ICAL_BYMONTH[] = {0x42, 0x59, 0x4D, 0x4F, 0x4E, 0x54, 0x48, 0}; /* "BYMONTH" */
static const char16_t ICAL_BYDAY[] = {0x42, 0x59, 0x44, 0x41, 0x59, 0}; /* "BYDAY" */
static const char16_t ICAL_BYMONTHDAY[] = {0x42, 0x59, 0x4D, 0x4F, 0x4E, 0x54, 0x48, 0x44, 0x41, 0x59, 0}; /* "BYMONTHDAY" */

static const char16_t ICAL_NEWLINE[] = {0x0D, 0x0A, 0}; /* CRLF */

static const char16_t ICAL_DOW_NAMES[7][3] = {
    {0x53, 0x55, 0}, /* "SU" */
    {0x4D, 0x4F, 0}, /* "MO" */
    {0x54, 0x55, 0}, /* "TU" */
    {0x57, 0x45, 0}, /* "WE" */
    {0x54, 0x48, 0}, /* "TH" */
    {0x46, 0x52, 0}, /* "FR" */
    {0x53, 0x41, 0}  /* "SA" */};

// Month length for non-leap year
static const int32_t MONTHLENGTH[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

// ICU custom property
static const char16_t ICU_TZINFO_PROP[] = {0x58, 0x2D, 0x54, 0x5A, 0x49, 0x4E, 0x46, 0x4F, 0x3A, 0}; /* "X-TZINFO:" */
static const char16_t ICU_TZINFO_PARTIAL[] = {0x2F, 0x50, 0x61, 0x72, 0x74, 0x69, 0x61, 0x6C, 0x40, 0}; /* "/Partial@" */
static const char16_t ICU_TZINFO_SIMPLE[] = {0x2F, 0x53, 0x69, 0x6D, 0x70, 0x6C, 0x65, 0x40, 0}; /* "/Simple@" */


/*
 * Simple fixed digit ASCII number to integer converter
 */
static int32_t parseAsciiDigits(const UnicodeString& str, int32_t start, int32_t length, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return 0;
    }
    if (length <= 0 || str.length() < start || (start + length) > str.length()) {
        status = U_INVALID_FORMAT_ERROR;
        return 0;
    }
    int32_t sign = 1;
    if (str.charAt(start) == PLUS) {
        start++;
        length--;
    } else if (str.charAt(start) == MINUS) {
        sign = -1;
        start++;
        length--;
    }
    int32_t num = 0;
    for (int32_t i = 0; i < length; i++) {
        int32_t digit = str.charAt(start + i) - 0x0030;
        if (digit < 0 || digit > 9) {
            status = U_INVALID_FORMAT_ERROR;
            return 0;
        }
        num = 10 * num + digit;
    }
    return sign * num;    
}

static UnicodeString& appendAsciiDigits(int32_t number, uint8_t length, UnicodeString& str) {
    UBool negative = false;
    int32_t digits[10]; // max int32_t is 10 decimal digits
    int32_t i;

    if (number < 0) {
        negative = true;
        number *= -1;
    }

    length = length > 10 ? 10 : length;
    if (length == 0) {
        // variable length
        i = 0;
        do {
            digits[i++] = number % 10;
            number /= 10;
        } while (number != 0);
        length = static_cast<uint8_t>(i);
    } else {
        // fixed digits
        for (i = 0; i < length; i++) {
           digits[i] = number % 10;
           number /= 10;
        }
    }
    if (negative) {
        str.append(MINUS);
    }
    for (i = length - 1; i >= 0; i--) {
        str.append(static_cast<char16_t>(digits[i] + 0x0030));
    }
    return str;
}

static UnicodeString& appendMillis(UDate date, UnicodeString& str) {
    UBool negative = false;
    int32_t digits[20]; // max int64_t is 20 decimal digits
    int32_t i;
    int64_t number;

    if (date < MIN_MILLIS) {
        number = static_cast<int64_t>(MIN_MILLIS);
    } else if (date > MAX_MILLIS) {
        number = static_cast<int64_t>(MAX_MILLIS);
    } else {
        number = static_cast<int64_t>(date);
    }
    if (number < 0) {
        negative = true;
        number *= -1;
    }
    i = 0;
    do {
        digits[i++] = static_cast<int32_t>(number % 10);
        number /= 10;
    } while (number != 0);

    if (negative) {
        str.append(MINUS);
    }
    i--;
    while (i >= 0) {
        str.append(static_cast<char16_t>(digits[i--] + 0x0030));
    }
    return str;
}

/*
 * Convert date/time to RFC2445 Date-Time form #1 DATE WITH LOCAL TIME
 */
static UnicodeString& getDateTimeString(UDate time, UnicodeString& str, UErrorCode& status) {
    if (U_FAILURE(status)) {return str;}
    int32_t year, month, dom, dow, doy, mid;
    Grego::timeToFields(time, year, month, dom, dow, doy, mid, status);
    if (U_FAILURE(status)) {return str;}

    str.remove();
    appendAsciiDigits(year, 4, str);
    appendAsciiDigits(month + 1, 2, str);
    appendAsciiDigits(dom, 2, str);
    str.append(static_cast<char16_t>(0x0054) /*'T'*/);

    int32_t t = mid;
    int32_t hour = t / U_MILLIS_PER_HOUR;
    t %= U_MILLIS_PER_HOUR;
    int32_t min = t / U_MILLIS_PER_MINUTE;
    t %= U_MILLIS_PER_MINUTE;
    int32_t sec = t / U_MILLIS_PER_SECOND;

    appendAsciiDigits(hour, 2, str);
    appendAsciiDigits(min, 2, str);
    appendAsciiDigits(sec, 2, str);
    return str;
}

/*
 * Convert date/time to RFC2445 Date-Time form #2 DATE WITH UTC TIME
 */
static UnicodeString& getUTCDateTimeString(UDate time, UnicodeString& str, UErrorCode& status) {
    getDateTimeString(time, str, status);
    str.append(static_cast<char16_t>(0x005A) /*'Z'*/);
    return str;
}

/*
 * Parse RFC2445 Date-Time form #1 DATE WITH LOCAL TIME and
 * #2 DATE WITH UTC TIME
 */
static UDate parseDateTimeString(const UnicodeString& str, int32_t offset, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return 0.0;
    }

    int32_t year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0;
    UBool isUTC = false;
    UBool isValid = false;
    do {
        int length = str.length();
        if (length != 15 && length != 16) {
            // FORM#1 15 characters, such as "20060317T142115"
            // FORM#2 16 characters, such as "20060317T142115Z"
            break;
        }
        if (str.charAt(8) != 0x0054) {
            // character "T" must be used for separating date and time
            break;
        }
        if (length == 16) {
            if (str.charAt(15) != 0x005A) {
                // invalid format
                break;
            }
            isUTC = true;
        }

        year = parseAsciiDigits(str, 0, 4, status);
        month = parseAsciiDigits(str, 4, 2, status) - 1;  // 0-based
        day = parseAsciiDigits(str, 6, 2, status);
        hour = parseAsciiDigits(str, 9, 2, status);
        min = parseAsciiDigits(str, 11, 2, status);
        sec = parseAsciiDigits(str, 13, 2, status);

        if (U_FAILURE(status)) {
            break;
        }

        // check valid range
        int32_t maxDayOfMonth = Grego::monthLength(year, month);
        if (year < 0 || month < 0 || month > 11 || day < 1 || day > maxDayOfMonth ||
                hour < 0 || hour >= 24 || min < 0 || min >= 60 || sec < 0 || sec >= 60) {
            break;
        }

        isValid = true;
    } while(false);

    if (!isValid) {
        status = U_INVALID_FORMAT_ERROR;
        return 0.0;
    }
    // Calculate the time
    UDate time = Grego::fieldsToDay(year, month, day) * U_MILLIS_PER_DAY;
    time += (hour * U_MILLIS_PER_HOUR + min * U_MILLIS_PER_MINUTE + sec * U_MILLIS_PER_SECOND);
    if (!isUTC) {
        time -= offset;
    }
    return time;
}

/*
 * Convert RFC2445 utc-offset string to milliseconds
 */
static int32_t offsetStrToMillis(const UnicodeString& str, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return 0;
    }

    UBool isValid = false;
    int32_t sign = 0, hour = 0, min = 0, sec = 0;

    do {
        int length = str.length();
        if (length != 5 && length != 7) {
            // utf-offset must be 5 or 7 characters
            break;
        }
        // sign
        char16_t s = str.charAt(0);
        if (s == PLUS) {
            sign = 1;
        } else if (s == MINUS) {
            sign = -1;
        } else {
            // utf-offset must start with "+" or "-"
            break;
        }
        hour = parseAsciiDigits(str, 1, 2, status);
        min = parseAsciiDigits(str, 3, 2, status);
        if (length == 7) {
            sec = parseAsciiDigits(str, 5, 2, status);
        }
        if (U_FAILURE(status)) {
            break;
        }
        isValid = true;
    } while(false);

    if (!isValid) {
        status = U_INVALID_FORMAT_ERROR;
        return 0;
    }
    int32_t millis = sign * ((hour * 60 + min) * 60 + sec) * 1000;
    return millis;
}

/*
 * Convert milliseconds to RFC2445 utc-offset string
 */
static void millisToOffset(int32_t millis, UnicodeString& str) {
    str.remove();
    if (millis >= 0) {
        str.append(PLUS);
    } else {
        str.append(MINUS);
        millis = -millis;
    }
    int32_t hour, min, sec;
    int32_t t = millis / 1000;

    sec = t % 60;
    t = (t - sec) / 60;
    min = t % 60;
    hour = t / 60;

    appendAsciiDigits(hour, 2, str);
    appendAsciiDigits(min, 2, str);
    appendAsciiDigits(sec, 2, str);
}

/*
 * Create a default TZNAME from TZID
 */
static void getDefaultTZName(const UnicodeString &tzid, UBool isDST, UnicodeString& zonename) {
    zonename = tzid;
    if (isDST) {
        zonename += UNICODE_STRING_SIMPLE("(DST)");
    } else {
        zonename += UNICODE_STRING_SIMPLE("(STD)");
    }
}

/*
 * Parse individual RRULE
 * 
 * On return -
 * 
 * month    calculated by BYMONTH-1, or -1 when not found
 * dow      day of week in BYDAY, or 0 when not found
 * wim      day of week ordinal number in BYDAY, or 0 when not found
 * dom      an array of day of month
 * domCount number of available days in dom (domCount is specifying the size of dom on input)
 * until    time defined by UNTIL attribute or MIN_MILLIS if not available
 */
static void parseRRULE(const UnicodeString& rrule, int32_t& month, int32_t& dow, int32_t& wim,
                       int32_t* dom, int32_t& domCount, UDate& until, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }
    int32_t numDom = 0;

    month = -1;
    dow = 0;
    wim = 0;
    until = MIN_MILLIS;

    UBool yearly = false;
    //UBool parseError = false;

    int32_t prop_start = 0;
    int32_t prop_end;
    UnicodeString prop, attr, value;
    UBool nextProp = true;

    while (nextProp) {
        prop_end = rrule.indexOf(SEMICOLON, prop_start);
        if (prop_end == -1) {
            prop.setTo(rrule, prop_start);
            nextProp = false;
        } else {
            prop.setTo(rrule, prop_start, prop_end - prop_start);
            prop_start = prop_end + 1;
        }
        int32_t eql = prop.indexOf(EQUALS_SIGN);
        if (eql != -1) {
            attr.setTo(prop, 0, eql);
            value.setTo(prop, eql + 1);
        } else {
            goto rruleParseError;
        }

        if (attr.compare(ICAL_FREQ, -1) == 0) {
            // only support YEARLY frequency type
            if (value.compare(ICAL_YEARLY, -1) == 0) {
                yearly = true;
            } else {
                goto rruleParseError;
            }
        } else if (attr.compare(ICAL_UNTIL, -1) == 0) {
            // ISO8601 UTC format, for example, "20060315T020000Z"
            until = parseDateTimeString(value, 0, status);
            if (U_FAILURE(status)) {
                goto rruleParseError;
            }
        } else if (attr.compare(ICAL_BYMONTH, -1) == 0) {
            // Note: BYMONTH may contain multiple months, but only single month make sense for
            // VTIMEZONE property.
            if (value.length() > 2) {
                goto rruleParseError;
            }
            month = parseAsciiDigits(value, 0, value.length(), status) - 1;
            if (U_FAILURE(status) || month < 0 || month >= 12) {
                goto rruleParseError;
            }
        } else if (attr.compare(ICAL_BYDAY, -1) == 0) {
            // Note: BYDAY may contain multiple day of week separated by comma.  It is unlikely used for
            // VTIMEZONE property.  We do not support the case.

            // 2-letter format is used just for representing a day of week, for example, "SU" for Sunday
            // 3 or 4-letter format is used for represeinging Nth day of week, for example, "-1SA" for last Saturday
            int32_t length = value.length();
            if (length < 2 || length > 4) {
                goto rruleParseError;
            }
            if (length > 2) {
                // Nth day of week
                int32_t sign = 1;
                if (value.charAt(0) == PLUS) {
                    sign = 1;
                } else if (value.charAt(0) == MINUS) {
                    sign = -1;
                } else if (length == 4) {
                    goto rruleParseError;
                }
                int32_t n = parseAsciiDigits(value, length - 3, 1, status);
                if (U_FAILURE(status) || n == 0 || n > 4) {
                    goto rruleParseError;
                }
                wim = n * sign;
                value.remove(0, length - 2);
            }
            int32_t wday;
            for (wday = 0; wday < 7; wday++) {
                if (value.compare(ICAL_DOW_NAMES[wday], 2) == 0) {
                    break;
                }
            }
            if (wday < 7) {
                // Sunday(1) - Saturday(7)
                dow = wday + 1;
            } else {
                goto rruleParseError;
            }
        } else if (attr.compare(ICAL_BYMONTHDAY, -1) == 0) {
            // Note: BYMONTHDAY may contain multiple days delimited by comma
            //
            // A value of BYMONTHDAY could be negative, for example, -1 means
            // the last day in a month
            int32_t dom_idx = 0;
            int32_t dom_start = 0;
            int32_t dom_end;
            UBool nextDOM = true;
            while (nextDOM) {
                dom_end = value.indexOf(COMMA, dom_start);
                if (dom_end == -1) {
                    dom_end = value.length();
                    nextDOM = false;
                }
                if (dom_idx < domCount) {
                    dom[dom_idx] = parseAsciiDigits(value, dom_start, dom_end - dom_start, status);
                    if (U_FAILURE(status)) {
                        goto rruleParseError;
                    }
                    dom_idx++;
                } else {
                    status = U_BUFFER_OVERFLOW_ERROR;
                    goto rruleParseError;
                }
                dom_start = dom_end + 1;
            }
            numDom = dom_idx;
        }
    }
    if (!yearly) {
        // FREQ=YEARLY must be set
        goto rruleParseError;
    }
    // Set actual number of parsed DOM (ICAL_BYMONTHDAY)
    domCount = numDom;
    return;

rruleParseError:
    if (U_SUCCESS(status)) {
        // Set error status
        status = U_INVALID_FORMAT_ERROR;
    }
}

static TimeZoneRule* createRuleByRRULE(const UnicodeString& zonename, int rawOffset, int dstSavings, UDate start,
                                       UVector* dates, int fromOffset, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    if (dates == nullptr || dates->size() == 0) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }

    int32_t i, j;
    DateTimeRule *adtr = nullptr;

    // Parse the first rule
    UnicodeString rrule = *static_cast<UnicodeString*>(dates->elementAt(0));
    int32_t month, dayOfWeek, nthDayOfWeek, dayOfMonth = 0;
    int32_t days[7];
    int32_t daysCount = UPRV_LENGTHOF(days);
    UDate until;

    parseRRULE(rrule, month, dayOfWeek, nthDayOfWeek, days, daysCount, until, status);
    if (U_FAILURE(status)) {
        return nullptr;
    }

    if (dates->size() == 1) {
        // No more rules
        if (daysCount > 1) {
            // Multiple BYMONTHDAY values
            if (daysCount != 7 || month == -1 || dayOfWeek == 0) {
                // Only support the rule using 7 continuous days
                // BYMONTH and BYDAY must be set at the same time
                goto unsupportedRRule;
            }
            int32_t firstDay = 31; // max possible number of dates in a month
            for (i = 0; i < 7; i++) {
                // Resolve negative day numbers.  A negative day number should
                // not be used in February, but if we see such case, we use 28
                // as the base.
                if (days[i] < 0) {
                    days[i] = MONTHLENGTH[month] + days[i] + 1;
                }
                if (days[i] < firstDay) {
                    firstDay = days[i];
                }
            }
            // Make sure days are continuous
            for (i = 1; i < 7; i++) {
                UBool found = false;
                for (j = 0; j < 7; j++) {
                    if (days[j] == firstDay + i) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    // days are not continuous
                    goto unsupportedRRule;
                }
            }
            // Use DOW_GEQ_DOM rule with firstDay as the start date
            dayOfMonth = firstDay;
        }
    } else {
        // Check if BYMONTH + BYMONTHDAY + BYDAY rule with multiple RRULE lines.
        // Otherwise, not supported.
        if (month == -1 || dayOfWeek == 0 || daysCount == 0) {
            // This is not the case
            goto unsupportedRRule;
        }
        // Parse the rest of rules if number of rules is not exceeding 7.
        // We can only support 7 continuous days starting from a day of month.
        if (dates->size() > 7) {
            goto unsupportedRRule;
        }

        // Note: To check valid date range across multiple rule is a little
        // bit complicated.  For now, this code is not doing strict range
        // checking across month boundary

        int32_t earliestMonth = month;
        int32_t earliestDay = 31;
        for (i = 0; i < daysCount; i++) {
            int32_t dom = days[i];
            dom = dom > 0 ? dom : MONTHLENGTH[month] + dom + 1;
            earliestDay = dom < earliestDay ? dom : earliestDay;
        }

        int32_t anotherMonth = -1;
        for (i = 1; i < dates->size(); i++) {
            rrule = *static_cast<UnicodeString*>(dates->elementAt(i));
            UDate tmp_until;
            int32_t tmp_month, tmp_dayOfWeek, tmp_nthDayOfWeek;
            int32_t tmp_days[7];
            int32_t tmp_daysCount = UPRV_LENGTHOF(tmp_days);
            parseRRULE(rrule, tmp_month, tmp_dayOfWeek, tmp_nthDayOfWeek, tmp_days, tmp_daysCount, tmp_until, status);
            if (U_FAILURE(status)) {
                return nullptr;
            }
            // If UNTIL is newer than previous one, use the one
            if (tmp_until > until) {
                until = tmp_until;
            }
            
            // Check if BYMONTH + BYMONTHDAY + BYDAY rule
            if (tmp_month == -1 || tmp_dayOfWeek == 0 || tmp_daysCount == 0) {
                goto unsupportedRRule;
            }
            // Count number of BYMONTHDAY
            if (daysCount + tmp_daysCount > 7) {
                // We cannot support BYMONTHDAY more than 7
                goto unsupportedRRule;
            }
            // Check if the same BYDAY is used.  Otherwise, we cannot
            // support the rule
            if (tmp_dayOfWeek != dayOfWeek) {
                goto unsupportedRRule;
            }
            // Check if the month is same or right next to the primary month
            if (tmp_month != month) {
                if (anotherMonth == -1) {
                    int32_t diff = tmp_month - month;
                    if (diff == -11 || diff == -1) {
                        // Previous month
                        anotherMonth = tmp_month;
                        earliestMonth = anotherMonth;
                        // Reset earliest day
                        earliestDay = 31;
                    } else if (diff == 11 || diff == 1) {
                        // Next month
                        anotherMonth = tmp_month;
                    } else {
                        // The day range cannot exceed more than 2 months
                        goto unsupportedRRule;
                    }
                } else if (tmp_month != month && tmp_month != anotherMonth) {
                    // The day range cannot exceed more than 2 months
                    goto unsupportedRRule;
                }
            }
            // If earlier month, go through days to find the earliest day
            if (tmp_month == earliestMonth) {
                for (j = 0; j < tmp_daysCount; j++) {
                    tmp_days[j] = tmp_days[j] > 0 ? tmp_days[j] : MONTHLENGTH[tmp_month] + tmp_days[j] + 1;
                    earliestDay = tmp_days[j] < earliestDay ? tmp_days[j] : earliestDay;
                }
            }
            daysCount += tmp_daysCount;
        }
        if (daysCount != 7) {
            // Number of BYMONTHDAY entries must be 7
            goto unsupportedRRule;
        }
        month = earliestMonth;
        dayOfMonth = earliestDay;
    }

    // Calculate start/end year and missing fields
    int32_t startYear, startMonth, startDOM, startDOW, startDOY, startMID;
    Grego::timeToFields(start + fromOffset, startYear, startMonth, startDOM,
        startDOW, startDOY, startMID, status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    if (month == -1) {
        // If BYMONTH is not set, use the month of DTSTART
        month = startMonth;
    }
    if (dayOfWeek == 0 && nthDayOfWeek == 0 && dayOfMonth == 0) {
        // If only YEARLY is set, use the day of DTSTART as BYMONTHDAY
        dayOfMonth = startDOM;
    }

    int32_t endYear;
    if (until != MIN_MILLIS) {
        int32_t endMonth, endDOM, endDOW, endDOY, endMID;
        Grego::timeToFields(until, endYear, endMonth, endDOM, endDOW, endDOY, endMID, status);
        if (U_FAILURE(status)) return nullptr;
    } else {
        endYear = AnnualTimeZoneRule::MAX_YEAR;
    }

    // Create the AnnualDateTimeRule
    if (dayOfWeek == 0 && nthDayOfWeek == 0 && dayOfMonth != 0) {
        // Day in month rule, for example, 15th day in the month
        adtr = new DateTimeRule(month, dayOfMonth, startMID, DateTimeRule::WALL_TIME);
    } else if (dayOfWeek != 0 && nthDayOfWeek != 0 && dayOfMonth == 0) {
        // Nth day of week rule, for example, last Sunday
        adtr = new DateTimeRule(month, nthDayOfWeek, dayOfWeek, startMID, DateTimeRule::WALL_TIME);
    } else if (dayOfWeek != 0 && nthDayOfWeek == 0 && dayOfMonth != 0) {
        // First day of week after day of month rule, for example,
        // first Sunday after 15th day in the month
        adtr = new DateTimeRule(month, dayOfMonth, dayOfWeek, true, startMID, DateTimeRule::WALL_TIME);
    }
    if (adtr == nullptr) {
        goto unsupportedRRule;
    }
    return new AnnualTimeZoneRule(zonename, rawOffset, dstSavings, adtr, startYear, endYear);

unsupportedRRule:
    status = U_INVALID_STATE_ERROR;
    return nullptr;
}

/*
 * Create a TimeZoneRule by the RDATE definition
 */
static TimeZoneRule* createRuleByRDATE(const UnicodeString& zonename, int32_t rawOffset, int32_t dstSavings,
                                       UDate start, UVector* dates, int32_t fromOffset, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    TimeArrayTimeZoneRule *retVal = nullptr;
    if (dates == nullptr || dates->size() == 0) {
        // When no RDATE line is provided, use start (DTSTART)
        // as the transition time
        retVal = new TimeArrayTimeZoneRule(zonename, rawOffset, dstSavings, &start, 1, DateTimeRule::UTC_TIME);
    } else {
        // Create an array of transition times
        int32_t size = dates->size();
        UDate* times = static_cast<UDate*>(uprv_malloc(sizeof(UDate) * size));
        if (times == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return nullptr;
        }
        for (int32_t i = 0; i < size; i++) {
            UnicodeString* datestr = static_cast<UnicodeString*>(dates->elementAt(i));
            times[i] = parseDateTimeString(*datestr, fromOffset, status);
            if (U_FAILURE(status)) {
                uprv_free(times);
                return nullptr;
            }
        }
        retVal = new TimeArrayTimeZoneRule(zonename, rawOffset, dstSavings, times, size, DateTimeRule::UTC_TIME);
        uprv_free(times);
    }
    if (retVal == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
    }
    return retVal;
}

/*
 * Check if the DOW rule specified by month, weekInMonth and dayOfWeek is equivalent
 * to the DateTimerule.
 */
static UBool isEquivalentDateRule(int32_t month, int32_t weekInMonth, int32_t dayOfWeek, const DateTimeRule *dtrule) {
    if (month != dtrule->getRuleMonth() || dayOfWeek != dtrule->getRuleDayOfWeek()) {
        return false;
    }
    if (dtrule->getTimeRuleType() != DateTimeRule::WALL_TIME) {
        // Do not try to do more intelligent comparison for now.
        return false;
    }
    if (dtrule->getDateRuleType() == DateTimeRule::DOW
            && dtrule->getRuleWeekInMonth() == weekInMonth) {
        return true;
    }
    int32_t ruleDOM = dtrule->getRuleDayOfMonth();
    if (dtrule->getDateRuleType() == DateTimeRule::DOW_GEQ_DOM) {
        if (ruleDOM%7 == 1 && (ruleDOM + 6)/7 == weekInMonth) {
            return true;
        }
        if (month != UCAL_FEBRUARY && (MONTHLENGTH[month] - ruleDOM)%7 == 6
                && weekInMonth == -1*((MONTHLENGTH[month]-ruleDOM+1)/7)) {
            return true;
        }
    }
    if (dtrule->getDateRuleType() == DateTimeRule::DOW_LEQ_DOM) {
        if (ruleDOM%7 == 0 && ruleDOM/7 == weekInMonth) {
            return true;
        }
        if (month != UCAL_FEBRUARY && (MONTHLENGTH[month] - ruleDOM)%7 == 0
                && weekInMonth == -1*((MONTHLENGTH[month] - ruleDOM)/7 + 1)) {
            return true;
        }
    }
    return false;
}

/*
 * Convert the rule to its equivalent rule using WALL_TIME mode.
 * This function returns nullptr when the specified DateTimeRule is already
 * using WALL_TIME mode.
 */
static DateTimeRule *toWallTimeRule(const DateTimeRule *rule, int32_t rawOffset, int32_t dstSavings, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    if (rule->getTimeRuleType() == DateTimeRule::WALL_TIME) {
        return nullptr;
    }
    int32_t wallt = rule->getRuleMillisInDay();
    if (rule->getTimeRuleType() == DateTimeRule::UTC_TIME) {
        wallt += (rawOffset + dstSavings);
    } else if (rule->getTimeRuleType() == DateTimeRule::STANDARD_TIME) {
        wallt += dstSavings;
    }

    int32_t month = -1, dom = 0, dow = 0;
    DateTimeRule::DateRuleType dtype;
    int32_t dshift = 0;
    if (wallt < 0) {
        dshift = -1;
        wallt += U_MILLIS_PER_DAY;
    } else if (wallt >= U_MILLIS_PER_DAY) {
        dshift = 1;
        wallt -= U_MILLIS_PER_DAY;
    }

    month = rule->getRuleMonth();
    dom = rule->getRuleDayOfMonth();
    dow = rule->getRuleDayOfWeek();
    dtype = rule->getDateRuleType();

    if (dshift != 0) {
        if (dtype == DateTimeRule::DOW) {
            // Convert to DOW_GEW_DOM or DOW_LEQ_DOM rule first
            int32_t wim = rule->getRuleWeekInMonth();
            if (wim > 0) {
                dtype = DateTimeRule::DOW_GEQ_DOM;
                dom = 7 * (wim - 1) + 1;
            } else {
                dtype = DateTimeRule::DOW_LEQ_DOM;
                dom = MONTHLENGTH[month] + 7 * (wim + 1);
            }
        }
        // Shift one day before or after
        dom += dshift;
        if (dom == 0) {
            month--;
            month = month < UCAL_JANUARY ? UCAL_DECEMBER : month;
            dom = MONTHLENGTH[month];
        } else if (dom > MONTHLENGTH[month]) {
            month++;
            month = month > UCAL_DECEMBER ? UCAL_JANUARY : month;
            dom = 1;
        }
        if (dtype != DateTimeRule::DOM) {
            // Adjust day of week
            dow += dshift;
            if (dow < UCAL_SUNDAY) {
                dow = UCAL_SATURDAY;
            } else if (dow > UCAL_SATURDAY) {
                dow = UCAL_SUNDAY;
            }
        }
    }
    // Create a new rule
    DateTimeRule *modifiedRule = nullptr;
    if (dtype == DateTimeRule::DOM) {
        modifiedRule = new DateTimeRule(month, dom, wallt, DateTimeRule::WALL_TIME);
    } else {
        modifiedRule = new DateTimeRule(month, dom, dow, (dtype == DateTimeRule::DOW_GEQ_DOM), wallt, DateTimeRule::WALL_TIME);
    }
    if (modifiedRule == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
    }
    return modifiedRule;
}

/*
 * Minimum implementations of stream writer/reader, writing/reading
 * UnicodeString.  For now, we do not want to introduce the dependency
 * on the ICU I/O stream in this module.  But we want to keep the code
 * equivalent to the ICU4J implementation, which utilizes java.io.Writer/
 * Reader.
 */
class VTZWriter {
public:
    VTZWriter(UnicodeString& out);
    ~VTZWriter();

    void write(const UnicodeString& str);
    void write(char16_t ch);
    void write(const char16_t* str);
    //void write(const char16_t* str, int32_t length);
private:
    UnicodeString* out;
};

VTZWriter::VTZWriter(UnicodeString& output) {
    out = &output;
}

VTZWriter::~VTZWriter() {
}

void
VTZWriter::write(const UnicodeString& str) {
    out->append(str);
}

void
VTZWriter::write(char16_t ch) {
    out->append(ch);
}

void
VTZWriter::write(const char16_t* str) {
    out->append(str, -1);
}

/*
void
VTZWriter::write(const char16_t* str, int32_t length) {
    out->append(str, length);
}
*/

class VTZReader {
public:
    VTZReader(const UnicodeString& input);
    ~VTZReader();

    char16_t read();
private:
    const UnicodeString* in;
    int32_t index;
};

VTZReader::VTZReader(const UnicodeString& input) {
    in = &input;
    index = 0;
}

VTZReader::~VTZReader() {
}

char16_t
VTZReader::read() {
    char16_t ch = 0xFFFF;
    if (index < in->length()) {
        ch = in->charAt(index);
    }
    index++;
    return ch;
}


UOBJECT_DEFINE_RTTI_IMPLEMENTATION(VTimeZone)

VTimeZone::VTimeZone()
:   BasicTimeZone(), tz(nullptr), vtzlines(nullptr),
    lastmod(MAX_MILLIS) {
}

VTimeZone::VTimeZone(const VTimeZone& source)
:   BasicTimeZone(source), tz(nullptr), vtzlines(nullptr),
    tzurl(source.tzurl), lastmod(source.lastmod),
    olsonzid(source.olsonzid), icutzver(source.icutzver) {
    if (source.tz != nullptr) {
        tz = source.tz->clone();
    }
    if (source.vtzlines != nullptr) {
        UErrorCode status = U_ZERO_ERROR;
        int32_t size = source.vtzlines->size();
        LocalPointer<UVector> lpVtzLines(
            new UVector(uprv_deleteUObject, uhash_compareUnicodeString, size, status), status);
        if (U_FAILURE(status)) {
            return;
        }
        for (int32_t i = 0; i < size; i++) {
            UnicodeString* line = static_cast<UnicodeString*>(source.vtzlines->elementAt(i))->clone();
            lpVtzLines->adoptElement(line, status);
            if (U_FAILURE(status) || line == nullptr) {
                return;
            }
        }
        vtzlines = lpVtzLines.orphan();
    }
}

VTimeZone::~VTimeZone() {
    delete tz;
    delete vtzlines;
}

VTimeZone&
VTimeZone::operator=(const VTimeZone& right) {
    if (this == &right) {
        return *this;
    }
    if (*this != right) {
        BasicTimeZone::operator=(right);
        if (tz != nullptr) {
            delete tz;
            tz = nullptr;
        }
        if (right.tz != nullptr) {
            tz = right.tz->clone();
        }
        if (vtzlines != nullptr) {
            delete vtzlines;
            vtzlines = nullptr;
        }
        if (right.vtzlines != nullptr) {
            UErrorCode status = U_ZERO_ERROR;
            int32_t size = right.vtzlines->size();
            LocalPointer<UVector> lpVtzLines(
                new UVector(uprv_deleteUObject, uhash_compareUnicodeString, size, status), status);
            if (U_SUCCESS(status)) {
                for (int32_t i = 0; i < size; i++) {
                    LocalPointer<UnicodeString> line(
                        static_cast<UnicodeString*>(right.vtzlines->elementAt(i))->clone(), status);
                    lpVtzLines->adoptElement(line.orphan(), status);
                    if (U_FAILURE(status)) {
                        break;
                    }
                }
                if (U_SUCCESS(status)) {
                    vtzlines = lpVtzLines.orphan();
                }
            }
        }
        tzurl = right.tzurl;
        lastmod = right.lastmod;
        olsonzid = right.olsonzid;
        icutzver = right.icutzver;
    }
    return *this;
}

bool
VTimeZone::operator==(const TimeZone& that) const {
    if (this == &that) {
        return true;
    }
    if (typeid(*this) != typeid(that) || !BasicTimeZone::operator==(that)) {
        return false;
    }
    VTimeZone *vtz = (VTimeZone*)&that;
    if (*tz == *(vtz->tz)
        && tzurl == vtz->tzurl
        && lastmod == vtz->lastmod
        /* && olsonzid = that.olsonzid */
        /* && icutzver = that.icutzver */) {
        return true;
    }
    return false;
}

bool
VTimeZone::operator!=(const TimeZone& that) const {
    return !operator==(that);
}

VTimeZone*
VTimeZone::createVTimeZoneByID(const UnicodeString& ID) {
    VTimeZone *vtz = new VTimeZone();
    if (vtz == nullptr) {
        return nullptr;
    }
    vtz->tz = (BasicTimeZone*)TimeZone::createTimeZone(ID);
    vtz->tz->getID(vtz->olsonzid);

    // Set ICU tzdata version
    UErrorCode status = U_ZERO_ERROR;
    UResourceBundle *bundle = nullptr;
    const char16_t* versionStr = nullptr;
    int32_t len = 0;
    bundle = ures_openDirect(nullptr, "zoneinfo64", &status);
    versionStr = ures_getStringByKey(bundle, "TZVersion", &len, &status);
    if (U_SUCCESS(status)) {
        vtz->icutzver.setTo(versionStr, len);
    }
    ures_close(bundle);
    return vtz;
}

VTimeZone*
VTimeZone::createVTimeZoneFromBasicTimeZone(const BasicTimeZone& basic_time_zone, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    VTimeZone *vtz = new VTimeZone();
    if (vtz == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    vtz->tz = basic_time_zone.clone();
    if (vtz->tz == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        delete vtz;
        return nullptr;
    }
    vtz->tz->getID(vtz->olsonzid);

    // Set ICU tzdata version
    UResourceBundle *bundle = nullptr;
    const char16_t* versionStr = nullptr;
    int32_t len = 0;
    bundle = ures_openDirect(nullptr, "zoneinfo64", &status);
    versionStr = ures_getStringByKey(bundle, "TZVersion", &len, &status);
    if (U_SUCCESS(status)) {
        vtz->icutzver.setTo(versionStr, len);
    }
    ures_close(bundle);
    return vtz;
}

VTimeZone*
VTimeZone::createVTimeZone(const UnicodeString& vtzdata, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    VTZReader reader(vtzdata);
    VTimeZone *vtz = new VTimeZone();
    if (vtz == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    vtz->load(reader, status);
    if (U_FAILURE(status)) {
        delete vtz;
        return nullptr;
    }
    return vtz;
}

UBool
VTimeZone::getTZURL(UnicodeString& url) const {
    if (tzurl.length() > 0) {
        url = tzurl;
        return true;
    }
    return false;
}

void
VTimeZone::setTZURL(const UnicodeString& url) {
    tzurl = url;
}

UBool
VTimeZone::getLastModified(UDate& lastModified) const {
    if (lastmod != MAX_MILLIS) {
        lastModified = lastmod;
        return true;
    }
    return false;
}

void
VTimeZone::setLastModified(UDate lastModified) {
    lastmod = lastModified;
}

void
VTimeZone::write(UnicodeString& result, UErrorCode& status) const {
    result.remove();
    VTZWriter writer(result);
    write(writer, status);
}

void
VTimeZone::write(UDate start, UnicodeString& result, UErrorCode& status) const {
    result.remove();
    VTZWriter writer(result);
    write(start, writer, status);
}

void
VTimeZone::writeSimple(UDate time, UnicodeString& result, UErrorCode& status) const {
    result.remove();
    VTZWriter writer(result);
    writeSimple(time, writer, status);
}

VTimeZone*
VTimeZone::clone() const {
    return new VTimeZone(*this);
}

int32_t
VTimeZone::getOffset(uint8_t era, int32_t year, int32_t month, int32_t day,
                     uint8_t dayOfWeek, int32_t millis, UErrorCode& status) const {
    return tz->getOffset(era, year, month, day, dayOfWeek, millis, status);
}

int32_t
VTimeZone::getOffset(uint8_t era, int32_t year, int32_t month, int32_t day,
                     uint8_t dayOfWeek, int32_t millis,
                     int32_t monthLength, UErrorCode& status) const {
    return tz->getOffset(era, year, month, day, dayOfWeek, millis, monthLength, status);
}

void
VTimeZone::getOffset(UDate date, UBool local, int32_t& rawOffset,
                     int32_t& dstOffset, UErrorCode& status) const {
    return tz->getOffset(date, local, rawOffset, dstOffset, status);
}

void VTimeZone::getOffsetFromLocal(UDate date, UTimeZoneLocalOption nonExistingTimeOpt,
                                   UTimeZoneLocalOption duplicatedTimeOpt,
                                   int32_t& rawOffset, int32_t& dstOffset, UErrorCode& status) const {
    tz->getOffsetFromLocal(date, nonExistingTimeOpt, duplicatedTimeOpt, rawOffset, dstOffset, status);
}

void
VTimeZone::setRawOffset(int32_t offsetMillis) {
    tz->setRawOffset(offsetMillis);
}

int32_t
VTimeZone::getRawOffset() const {
    return tz->getRawOffset();
}

UBool
VTimeZone::useDaylightTime() const {
    return tz->useDaylightTime();
}

UBool
VTimeZone::inDaylightTime(UDate date, UErrorCode& status) const {
    return tz->inDaylightTime(date, status);
}

UBool
VTimeZone::hasSameRules(const TimeZone& other) const {
    return tz->hasSameRules(other);
}

UBool
VTimeZone::getNextTransition(UDate base, UBool inclusive, TimeZoneTransition& result) const {
    return tz->getNextTransition(base, inclusive, result);
}

UBool
VTimeZone::getPreviousTransition(UDate base, UBool inclusive, TimeZoneTransition& result) const {
    return tz->getPreviousTransition(base, inclusive, result);
}

int32_t
VTimeZone::countTransitionRules(UErrorCode& status) const {
    return tz->countTransitionRules(status);
}

void
VTimeZone::getTimeZoneRules(const InitialTimeZoneRule*& initial,
                            const TimeZoneRule* trsrules[], int32_t& trscount,
                            UErrorCode& status) const {
    tz->getTimeZoneRules(initial, trsrules, trscount, status);
}

void
VTimeZone::load(VTZReader& reader, UErrorCode& status) {
    U_ASSERT(vtzlines == nullptr);
    LocalPointer<UVector> lpVtzLines(
        new UVector(uprv_deleteUObject, uhash_compareUnicodeString, DEFAULT_VTIMEZONE_LINES, status), status);
    if (U_FAILURE(status)) {
        return;
    }
    UBool eol = false;
    UBool start = false;
    UBool success = false;
    UnicodeString line;

    while (true) {
        char16_t ch = reader.read();
        if (ch == 0xFFFF) {
            // end of file
            if (start && line.startsWith(ICAL_END_VTIMEZONE, -1)) {
                LocalPointer<UnicodeString> element(new UnicodeString(line), status);
                lpVtzLines->adoptElement(element.orphan(), status);
                if (U_FAILURE(status)) {
                    return;
                }
                success = true;
            }
            break;
        }
        if (ch == 0x000D) {
            // CR, must be followed by LF according to the definition in RFC2445
            continue;
        }
        if (eol) {
            if (ch != 0x0009 && ch != 0x0020) {
                // NOT followed by TAB/SP -> new line
                if (start) {
                    if (line.length() > 0) {
                        LocalPointer<UnicodeString> element(new UnicodeString(line), status);
                        lpVtzLines->adoptElement(element.orphan(), status);
                        if (U_FAILURE(status)) {
                            return;
                        }
                    }
                }
                line.remove();
                if (ch != 0x000A) {
                    line.append(ch);
                }
            }
            eol = false;
        } else {
            if (ch == 0x000A) {
                // LF
                eol = true;
                if (start) {
                    if (line.startsWith(ICAL_END_VTIMEZONE, -1)) {
                        LocalPointer<UnicodeString> element(new UnicodeString(line), status);
                        lpVtzLines->adoptElement(element.orphan(), status);
                        if (U_FAILURE(status)) {
                            return;
                        }
                        success = true;
                        break;
                    }
                } else {
                    if (line.startsWith(ICAL_BEGIN_VTIMEZONE, -1)) {
                        LocalPointer<UnicodeString> element(new UnicodeString(line), status);
                        lpVtzLines->adoptElement(element.orphan(), status);
                        if (U_FAILURE(status)) {
                            return;
                        }
                        line.remove();
                        start = true;
                        eol = false;
                    }
                }
            } else {
                line.append(ch);
            }
        }
    }
    if (!success) {
        if (U_SUCCESS(status)) {
            status = U_INVALID_STATE_ERROR;
        }
        return;
    }
    vtzlines = lpVtzLines.orphan();
    parse(status);
}

// parser state
#define INI 0   // Initial state
#define VTZ 1   // In VTIMEZONE
#define TZI 2   // In STANDARD or DAYLIGHT

#define DEF_DSTSAVINGS (60*60*1000)
#define DEF_TZSTARTTIME (0.0)

void
VTimeZone::parse(UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }
    if (vtzlines == nullptr || vtzlines->size() == 0) {
        status = U_INVALID_STATE_ERROR;
        return;
    }

    // timezone ID
    UnicodeString tzid;

    int32_t state = INI;
    int32_t n = 0;
    UBool dst = false;      // current zone type
    UnicodeString from;     // current zone from offset
    UnicodeString to;       // current zone offset
    UnicodeString zonename;   // current zone name
    UnicodeString dtstart;  // current zone starts
    UBool isRRULE = false;  // true if the rule is described by RRULE
    int32_t initialRawOffset = 0;   // initial offset
    int32_t initialDSTSavings = 0;  // initial offset
    UDate firstStart = MAX_MILLIS;  // the earliest rule start time
    UnicodeString name;     // RFC2445 prop name
    UnicodeString value;    // RFC2445 prop value

    int32_t finalRuleIdx = -1;
    int32_t finalRuleCount = 0;

    // Set the deleter on rules to remove TimeZoneRule vectors to avoid memory leaks due to unowned TimeZoneRules.
    UVector rules(uprv_deleteUObject, nullptr, status);
    
    // list of RDATE or RRULE strings
    UVector dates(uprv_deleteUObject, uhash_compareUnicodeString, status);
    if (U_FAILURE(status)) {
        return;
    }
    
    for (n = 0; n < vtzlines->size(); n++) {
        UnicodeString* line = static_cast<UnicodeString*>(vtzlines->elementAt(n));
        int32_t valueSep = line->indexOf(COLON);
        if (valueSep < 0) {
            continue;
        }
        name.setTo(*line, 0, valueSep);
        value.setTo(*line, valueSep + 1);

        switch (state) {
        case INI:
            if (name.compare(ICAL_BEGIN, -1) == 0
                && value.compare(ICAL_VTIMEZONE, -1) == 0) {
                state = VTZ;
            }
            break;

        case VTZ:
            if (name.compare(ICAL_TZID, -1) == 0) {
                tzid = value;
            } else if (name.compare(ICAL_TZURL, -1) == 0) {
                tzurl = value;
            } else if (name.compare(ICAL_LASTMOD, -1) == 0) {
                // Always in 'Z' format, so the offset argument for the parse method
                // can be any value.
                lastmod = parseDateTimeString(value, 0, status);
                if (U_FAILURE(status)) {
                    return;
                }
            } else if (name.compare(ICAL_BEGIN, -1) == 0) {
                UBool isDST = (value.compare(ICAL_DAYLIGHT, -1) == 0);
                if (value.compare(ICAL_STANDARD, -1) == 0 || isDST) {
                    // tzid must be ready at this point
                    if (tzid.length() == 0) {
                        return;
                    }
                    // initialize current zone properties
                    if (dates.size() != 0) {
                        dates.removeAllElements();
                    }
                    isRRULE = false;
                    from.remove();
                    to.remove();
                    zonename.remove();
                    dst = isDST;
                    state = TZI;
                } else {
                    // BEGIN property other than STANDARD/DAYLIGHT
                    // must not be there.
                    return;
                }
            } else if (name.compare(ICAL_END, -1) == 0) {
                break;
            }
            break;
        case TZI:
            if (name.compare(ICAL_DTSTART, -1) == 0) {
                dtstart = value;
            } else if (name.compare(ICAL_TZNAME, -1) == 0) {
                zonename = value;
            } else if (name.compare(ICAL_TZOFFSETFROM, -1) == 0) {
                from = value;
            } else if (name.compare(ICAL_TZOFFSETTO, -1) == 0) {
                to = value;
            } else if (name.compare(ICAL_RDATE, -1) == 0) {
                // RDATE mixed with RRULE is not supported
                if (isRRULE) {
                    return;
                }
                // RDATE value may contain multiple date delimited
                // by comma
                UBool nextDate = true;
                int32_t dstart = 0;
                LocalPointer<UnicodeString> dstr;
                while (nextDate) {
                    int32_t dend = value.indexOf(COMMA, dstart);
                    if (dend == -1) {
                        dstr.adoptInsteadAndCheckErrorCode(new UnicodeString(value, dstart), status);
                        nextDate = false;
                    } else {
                        dstr.adoptInsteadAndCheckErrorCode(new UnicodeString(value, dstart, dend - dstart), status);
                    }
                    dates.adoptElement(dstr.orphan(), status);
                    if (U_FAILURE(status)) {
                        return;
                    }
                    dstart = dend + 1;
                }
            } else if (name.compare(ICAL_RRULE, -1) == 0) {
                // RRULE mixed with RDATE is not supported
                if (!isRRULE && dates.size() != 0) {
                    return;
                }
                isRRULE = true;
                LocalPointer<UnicodeString> element(new UnicodeString(value), status);
                dates.adoptElement(element.orphan(), status);
                if (U_FAILURE(status)) {
                    return;
                }
            } else if (name.compare(ICAL_END, -1) == 0) {
                // Mandatory properties
                if (dtstart.length() == 0 || from.length() == 0 || to.length() == 0) {
                    return;
                }
                // if zonename is not available, create one from tzid
                if (zonename.length() == 0) {
                    getDefaultTZName(tzid, dst, zonename);
                }

                // create a time zone rule
                LocalPointer<TimeZoneRule> rule;
                int32_t fromOffset = 0;
                int32_t toOffset = 0;
                int32_t rawOffset = 0;
                int32_t dstSavings = 0;
                UDate start = 0;

                // Parse TZOFFSETFROM/TZOFFSETTO
                fromOffset = offsetStrToMillis(from, status);
                toOffset = offsetStrToMillis(to, status);
                if (U_FAILURE(status)) {
                    return;
                }

                if (dst) {
                    // If daylight, use the previous offset as rawoffset if positive
                    if (toOffset - fromOffset > 0) {
                        rawOffset = fromOffset;
                        dstSavings = toOffset - fromOffset;
                    } else {
                        // This is rare case..  just use 1 hour DST savings
                        rawOffset = toOffset - DEF_DSTSAVINGS;
                        dstSavings = DEF_DSTSAVINGS;                                
                    }
                } else {
                    rawOffset = toOffset;
                    dstSavings = 0;
                }

                // start time
                start = parseDateTimeString(dtstart, fromOffset, status);
                if (U_FAILURE(status)) {
                    return;
                }

                // Create the rule
                UDate actualStart = MAX_MILLIS;
                if (isRRULE) {
                    rule.adoptInsteadAndCheckErrorCode(
                        createRuleByRRULE(zonename, rawOffset, dstSavings, start, &dates, fromOffset, status), status);
                } else {
                    rule.adoptInsteadAndCheckErrorCode(
                        createRuleByRDATE(zonename, rawOffset, dstSavings, start, &dates, fromOffset, status), status);
                }
                if (U_FAILURE(status)) {
                    return;
                } else {
                    UBool startAvail = rule->getFirstStart(fromOffset, 0, actualStart);
                    if (startAvail && actualStart < firstStart) {
                        // save from offset information for the earliest rule
                        firstStart = actualStart;
                        // If this is STD, assume the time before this transition
                        // is DST when the difference is 1 hour.  This might not be
                        // accurate, but VTIMEZONE data does not have such info.
                        if (dstSavings > 0) {
                            initialRawOffset = fromOffset;
                            initialDSTSavings = 0;
                        } else {
                            if (fromOffset - toOffset == DEF_DSTSAVINGS) {
                                initialRawOffset = fromOffset - DEF_DSTSAVINGS;
                                initialDSTSavings = DEF_DSTSAVINGS;
                            } else {
                                initialRawOffset = fromOffset;
                                initialDSTSavings = 0;
                            }
                        }
                    }
                }
                rules.adoptElement(rule.orphan(), status);
                if (U_FAILURE(status)) {
                    return;
                }
                state = VTZ;
            }
            break;
        }
    }
    // Must have at least one rule
    if (rules.size() == 0) {
        return;
    }

    // Create a initial rule
    getDefaultTZName(tzid, false, zonename);
    LocalPointer<InitialTimeZoneRule> initialRule(
        new InitialTimeZoneRule(zonename, initialRawOffset, initialDSTSavings), status);
    if (U_FAILURE(status)) {
        return;
    }

    // Finally, create the RuleBasedTimeZone
    // C++ awkwardness on memory allocation failure: the constructor wont be run, meaning
    // that initialRule wont be adopted/deleted, as it normally would be.
    LocalPointer<RuleBasedTimeZone> rbtz(
        new RuleBasedTimeZone(tzid, initialRule.getAlias()), status);
    if (U_SUCCESS(status)) {
        initialRule.orphan();
    } else {
        return;
    }

    for (n = 0; n < rules.size(); n++) {
        TimeZoneRule* r = static_cast<TimeZoneRule*>(rules.elementAt(n));
        AnnualTimeZoneRule *atzrule = dynamic_cast<AnnualTimeZoneRule *>(r);
        if (atzrule != nullptr) {
            if (atzrule->getEndYear() == AnnualTimeZoneRule::MAX_YEAR) {
                finalRuleCount++;
                finalRuleIdx = n;
            }
        }
    }
    if (finalRuleCount > 2) {
        // Too many final rules
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    if (finalRuleCount == 1) {
        if (rules.size() == 1) {
            // Only one final rule, only governs the initial rule,
            // which is already initialized, thus, we do not need to
            // add this transition rule
            rules.removeAllElements();
        } else {
            // Normalize the final rule
            AnnualTimeZoneRule* finalRule = static_cast<AnnualTimeZoneRule*>(rules.elementAt(finalRuleIdx));
            int32_t tmpRaw = finalRule->getRawOffset();
            int32_t tmpDST = finalRule->getDSTSavings();

            // Find the last non-final rule
            UDate finalStart, start;
            finalRule->getFirstStart(initialRawOffset, initialDSTSavings, finalStart);
            start = finalStart;
            for (n = 0; n < rules.size(); n++) {
                if (finalRuleIdx == n) {
                    continue;
                }
                TimeZoneRule* r = static_cast<TimeZoneRule*>(rules.elementAt(n));
                UDate lastStart;
                r->getFinalStart(tmpRaw, tmpDST, lastStart);
                if (lastStart > start) {
                    finalRule->getNextStart(lastStart,
                        r->getRawOffset(),
                        r->getDSTSavings(),
                        false,
                        start);
                }
            }

            LocalPointer<TimeZoneRule> newRule;
            UnicodeString tznam;
            if (start == finalStart) {
                // Transform this into a single transition
                newRule.adoptInsteadAndCheckErrorCode(
                    new TimeArrayTimeZoneRule(
                            finalRule->getName(tznam),
                            finalRule->getRawOffset(),
                            finalRule->getDSTSavings(),
                            &finalStart,
                            1,
                            DateTimeRule::UTC_TIME),
                    status);
            } else {
                // Update the end year
                int32_t y, m, d, dow, doy, mid;
                Grego::timeToFields(start, y, m, d, dow, doy, mid, status);
                if (U_FAILURE(status)) return;
                newRule.adoptInsteadAndCheckErrorCode(
                    new AnnualTimeZoneRule(
                            finalRule->getName(tznam),
                            finalRule->getRawOffset(),
                            finalRule->getDSTSavings(),
                            *(finalRule->getRule()),
                            finalRule->getStartYear(),
                            y),
                    status);
            }
            if (U_FAILURE(status)) {
                return;
            }
            rules.removeElementAt(finalRuleIdx);
            rules.adoptElement(newRule.orphan(), status);
            if (U_FAILURE(status)) {
                return;
            }
        }
    }

    while (!rules.isEmpty()) {
        TimeZoneRule* tzr = static_cast<TimeZoneRule*>(rules.orphanElementAt(0));
        rbtz->addTransitionRule(tzr, status);
        if (U_FAILURE(status)) {
            return;
        }
    }
    rbtz->complete(status);
    if (U_FAILURE(status)) {
        return;
    }

    tz = rbtz.orphan();
    setID(tzid);
}

void
VTimeZone::write(VTZWriter& writer, UErrorCode& status) const {
    if (U_FAILURE(status)) return;
    if (vtzlines != nullptr) {
        for (int32_t i = 0; i < vtzlines->size(); i++) {
            UnicodeString* line = static_cast<UnicodeString*>(vtzlines->elementAt(i));
            if (line->startsWith(ICAL_TZURL, -1)
                && line->charAt(u_strlen(ICAL_TZURL)) == COLON) {
                writer.write(ICAL_TZURL);
                writer.write(COLON);
                writer.write(tzurl);
                writer.write(ICAL_NEWLINE);
            } else if (line->startsWith(ICAL_LASTMOD, -1)
                && line->charAt(u_strlen(ICAL_LASTMOD)) == COLON) {
                UnicodeString utcString;
                writer.write(ICAL_LASTMOD);
                writer.write(COLON);
                writer.write(getUTCDateTimeString(lastmod, utcString, status));
                if (U_FAILURE(status)) return;
                writer.write(ICAL_NEWLINE);
            } else {
                writer.write(*line);
                writer.write(ICAL_NEWLINE);
            }
        }
    } else {
        UnicodeString icutzprop;
        UVector customProps(nullptr, uhash_compareUnicodeString, status);
        if (olsonzid.length() > 0 && icutzver.length() > 0) {
            icutzprop.append(olsonzid);
            icutzprop.append(u'[');
            icutzprop.append(icutzver);
            icutzprop.append(u']');
            customProps.addElement(&icutzprop, status);
        }
        writeZone(writer, *tz, &customProps, status);
    }
}

void
VTimeZone::write(UDate start, VTZWriter& writer, UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return;
    }
    InitialTimeZoneRule *initial = nullptr;
    UVector *transitionRules = nullptr;
    UVector customProps(uprv_deleteUObject, uhash_compareUnicodeString, status);
    UnicodeString tzid;

    // Extract rules applicable to dates after the start time
    getTimeZoneRulesAfter(start, initial, transitionRules, status);
    LocalPointer<InitialTimeZoneRule> lpInitial(initial);
    LocalPointer<UVector> lpTransitionRules(transitionRules);
    if (U_FAILURE(status)) {
        return;
    }

    // Create a RuleBasedTimeZone with the subset rule
    getID(tzid);
    RuleBasedTimeZone rbtz(tzid, lpInitial.orphan());
    if (lpTransitionRules.isValid()) {
        U_ASSERT(transitionRules->hasDeleter());  // Assumed for U_FAILURE early return, below.
        while (!lpTransitionRules->isEmpty()) {
            TimeZoneRule* tr = static_cast<TimeZoneRule*>(lpTransitionRules->orphanElementAt(0));
            rbtz.addTransitionRule(tr, status);
            if (U_FAILURE(status)) {
                return;
            }
        }
    }
    rbtz.complete(status);
    if (U_FAILURE(status)) {
        return;
    }

    if (olsonzid.length() > 0 && icutzver.length() > 0) {
        UnicodeString *icutzprop = new UnicodeString(ICU_TZINFO_PROP);
        if (icutzprop == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
        icutzprop->append(olsonzid);
        icutzprop->append(static_cast<char16_t>(0x005B)/*'['*/);
        icutzprop->append(icutzver);
        icutzprop->append(ICU_TZINFO_PARTIAL, -1);
        appendMillis(start, *icutzprop);
        icutzprop->append(static_cast<char16_t>(0x005D)/*']'*/);
        customProps.adoptElement(icutzprop, status);
        if (U_FAILURE(status)) {
            return;
        }
    }
    writeZone(writer, rbtz, &customProps, status);
}

void
VTimeZone::writeSimple(UDate time, VTZWriter& writer, UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return;
    }

    UVector customProps(uprv_deleteUObject, uhash_compareUnicodeString, status);
    UnicodeString tzid;

    // Extract simple rules
    InitialTimeZoneRule *initial = nullptr;
    AnnualTimeZoneRule *std = nullptr, *dst = nullptr;
    getSimpleRulesNear(time, initial, std, dst, status);
    LocalPointer<InitialTimeZoneRule> lpInitial(initial);
    LocalPointer<AnnualTimeZoneRule> lpStd(std);
    LocalPointer<AnnualTimeZoneRule> lpDst(dst);
    if (U_SUCCESS(status)) {
        // Create a RuleBasedTimeZone with the subset rule
        getID(tzid);
        RuleBasedTimeZone rbtz(tzid, lpInitial.orphan());
        if (lpStd.isValid() && lpDst.isValid()) {
            rbtz.addTransitionRule(lpStd.orphan(), status);
            rbtz.addTransitionRule(lpDst.orphan(), status);
        }
        if (U_FAILURE(status)) {
            return;
        }

        if (olsonzid.length() > 0 && icutzver.length() > 0) {
            LocalPointer<UnicodeString> icutzprop(new UnicodeString(ICU_TZINFO_PROP), status);
            if (U_FAILURE(status)) {
               return;
            }
            icutzprop->append(olsonzid);
            icutzprop->append(static_cast<char16_t>(0x005B)/*'['*/);
            icutzprop->append(icutzver);
            icutzprop->append(ICU_TZINFO_SIMPLE, -1);
            appendMillis(time, *icutzprop);
            icutzprop->append(static_cast<char16_t>(0x005D)/*']'*/);
            customProps.adoptElement(icutzprop.orphan(), status);
        }
        writeZone(writer, rbtz, &customProps, status);
    }
}

void
VTimeZone::writeZone(VTZWriter& w, BasicTimeZone& basictz,
                     UVector* customProps, UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return;
    }
    writeHeaders(w, status);
    if (U_FAILURE(status)) {
        return;
    }

    if (customProps != nullptr) {
        for (int32_t i = 0; i < customProps->size(); i++) {
            UnicodeString* custprop = static_cast<UnicodeString*>(customProps->elementAt(i));
            w.write(*custprop);
            w.write(ICAL_NEWLINE);
        }
    }

    UDate t = MIN_MILLIS;
    UnicodeString dstName;
    int32_t dstFromOffset = 0;
    int32_t dstFromDSTSavings = 0;
    int32_t dstToOffset = 0;
    int32_t dstStartYear = 0;
    int32_t dstMonth = 0;
    int32_t dstDayOfWeek = 0;
    int32_t dstWeekInMonth = 0;
    int32_t dstMillisInDay = 0;
    UDate dstStartTime = 0.0;
    UDate dstUntilTime = 0.0;
    int32_t dstCount = 0;
    AnnualTimeZoneRule *finalDstRule = nullptr;

    UnicodeString stdName;
    int32_t stdFromOffset = 0;
    int32_t stdFromDSTSavings = 0;
    int32_t stdToOffset = 0;
    int32_t stdStartYear = 0;
    int32_t stdMonth = 0;
    int32_t stdDayOfWeek = 0;
    int32_t stdWeekInMonth = 0;
    int32_t stdMillisInDay = 0;
    UDate stdStartTime = 0.0;
    UDate stdUntilTime = 0.0;
    int32_t stdCount = 0;
    AnnualTimeZoneRule *finalStdRule = nullptr;

    int32_t year, month, dom, dow, doy, mid;
    UBool hasTransitions = false;
    TimeZoneTransition tzt;
    UBool tztAvail;
    UnicodeString name;
    UBool isDst;

    // Going through all transitions
    while (true) {
        tztAvail = basictz.getNextTransition(t, false, tzt);
        if (!tztAvail) {
            break;
        }
        hasTransitions = true;
        t = tzt.getTime();
        tzt.getTo()->getName(name);
        isDst = (tzt.getTo()->getDSTSavings() != 0);
        int32_t fromOffset = tzt.getFrom()->getRawOffset() + tzt.getFrom()->getDSTSavings();
        int32_t fromDSTSavings = tzt.getFrom()->getDSTSavings();
        int32_t toOffset = tzt.getTo()->getRawOffset() + tzt.getTo()->getDSTSavings();
        Grego::timeToFields(tzt.getTime() + fromOffset, year, month, dom, dow, doy, mid, status);
        if (U_FAILURE(status)) return;
        int32_t weekInMonth = Grego::dayOfWeekInMonth(year, month, dom);
        UBool sameRule = false;
        const AnnualTimeZoneRule *atzrule;
        if (isDst) {
            if (finalDstRule == nullptr
                && (atzrule = dynamic_cast<const AnnualTimeZoneRule *>(tzt.getTo())) != nullptr
                && atzrule->getEndYear() == AnnualTimeZoneRule::MAX_YEAR
            ) {
                finalDstRule = atzrule->clone();
            }
            if (dstCount > 0) {
                if (year == dstStartYear + dstCount
                        && name.compare(dstName) == 0
                        && dstFromOffset == fromOffset
                        && dstToOffset == toOffset
                        && dstMonth == month
                        && dstDayOfWeek == dow
                        && dstWeekInMonth == weekInMonth
                        && dstMillisInDay == mid) {
                    // Update until time
                    dstUntilTime = t;
                    dstCount++;
                    sameRule = true;
                }
                if (!sameRule) {
                    if (dstCount == 1) {
                        writeZonePropsByTime(w, true, dstName, dstFromOffset, dstToOffset, dstStartTime,
                                true, status);
                    } else {
                        writeZonePropsByDOW(w, true, dstName, dstFromOffset, dstToOffset,
                                dstMonth, dstWeekInMonth, dstDayOfWeek, dstStartTime, dstUntilTime, status);
                    }
                    if (U_FAILURE(status)) {
                        goto cleanupWriteZone;
                    }
                }
            } 
            if (!sameRule) {
                // Reset this DST information
                dstName = name;
                dstFromOffset = fromOffset;
                dstFromDSTSavings = fromDSTSavings;
                dstToOffset = toOffset;
                dstStartYear = year;
                dstMonth = month;
                dstDayOfWeek = dow;
                dstWeekInMonth = weekInMonth;
                dstMillisInDay = mid;
                dstStartTime = dstUntilTime = t;
                dstCount = 1;
            }
            if (finalStdRule != nullptr && finalDstRule != nullptr) {
                break;
            }
        } else {
            if (finalStdRule == nullptr
                && (atzrule = dynamic_cast<const AnnualTimeZoneRule *>(tzt.getTo())) != nullptr
                && atzrule->getEndYear() == AnnualTimeZoneRule::MAX_YEAR
            ) {
                finalStdRule = atzrule->clone();
            }
            if (stdCount > 0) {
                if (year == stdStartYear + stdCount
                        && name.compare(stdName) == 0
                        && stdFromOffset == fromOffset
                        && stdToOffset == toOffset
                        && stdMonth == month
                        && stdDayOfWeek == dow
                        && stdWeekInMonth == weekInMonth
                        && stdMillisInDay == mid) {
                    // Update until time
                    stdUntilTime = t;
                    stdCount++;
                    sameRule = true;
                }
                if (!sameRule) {
                    if (stdCount == 1) {
                        writeZonePropsByTime(w, false, stdName, stdFromOffset, stdToOffset, stdStartTime,
                                true, status);
                    } else {
                        writeZonePropsByDOW(w, false, stdName, stdFromOffset, stdToOffset,
                                stdMonth, stdWeekInMonth, stdDayOfWeek, stdStartTime, stdUntilTime, status);
                    }
                    if (U_FAILURE(status)) {
                        goto cleanupWriteZone;
                    }
                }
            }
            if (!sameRule) {
                // Reset this STD information
                stdName = name;
                stdFromOffset = fromOffset;
                stdFromDSTSavings = fromDSTSavings;
                stdToOffset = toOffset;
                stdStartYear = year;
                stdMonth = month;
                stdDayOfWeek = dow;
                stdWeekInMonth = weekInMonth;
                stdMillisInDay = mid;
                stdStartTime = stdUntilTime = t;
                stdCount = 1;
            }
            if (finalStdRule != nullptr && finalDstRule != nullptr) {
                break;
            }
        }
    }
    if (!hasTransitions) {
        // No transition - put a single non transition RDATE
        int32_t raw, dst, offset;
        basictz.getOffset(0.0/*any time*/, false, raw, dst, status);
        if (U_FAILURE(status)) {
            goto cleanupWriteZone;
        }
        offset = raw + dst;
        isDst = (dst != 0);
        UnicodeString tzid;
        basictz.getID(tzid);
        getDefaultTZName(tzid, isDst, name);        
        writeZonePropsByTime(w, isDst, name,
                offset, offset, DEF_TZSTARTTIME - offset, false, status);    
        if (U_FAILURE(status)) {
            goto cleanupWriteZone;
        }
    } else {
        if (dstCount > 0) {
            if (finalDstRule == nullptr) {
                if (dstCount == 1) {
                    writeZonePropsByTime(w, true, dstName, dstFromOffset, dstToOffset, dstStartTime,
                            true, status);
                } else {
                    writeZonePropsByDOW(w, true, dstName, dstFromOffset, dstToOffset,
                            dstMonth, dstWeekInMonth, dstDayOfWeek, dstStartTime, dstUntilTime, status);
                }
                if (U_FAILURE(status)) {
                    goto cleanupWriteZone;
                }
            } else {
                if (dstCount == 1) {
                    writeFinalRule(w, true, finalDstRule,
                            dstFromOffset - dstFromDSTSavings, dstFromDSTSavings, dstStartTime, status);
                } else {
                    // Use a single rule if possible
                    if (isEquivalentDateRule(dstMonth, dstWeekInMonth, dstDayOfWeek, finalDstRule->getRule())) {
                        writeZonePropsByDOW(w, true, dstName, dstFromOffset, dstToOffset,
                                dstMonth, dstWeekInMonth, dstDayOfWeek, dstStartTime, MAX_MILLIS, status);
                    } else {
                        // Not equivalent rule - write out two different rules
                        writeZonePropsByDOW(w, true, dstName, dstFromOffset, dstToOffset,
                                dstMonth, dstWeekInMonth, dstDayOfWeek, dstStartTime, dstUntilTime, status);
                        if (U_FAILURE(status)) {
                            goto cleanupWriteZone;
                        }
                        UDate nextStart;
                        UBool nextStartAvail = finalDstRule->getNextStart(dstUntilTime, dstFromOffset - dstFromDSTSavings, dstFromDSTSavings, false, nextStart);
                        U_ASSERT(nextStartAvail);
                        if (nextStartAvail) {
                            writeFinalRule(w, true, finalDstRule,
                                    dstFromOffset - dstFromDSTSavings, dstFromDSTSavings, nextStart, status);
                        }
                    }
                }
                if (U_FAILURE(status)) {
                    goto cleanupWriteZone;
                }
            }
        }
        if (stdCount > 0) {
            if (finalStdRule == nullptr) {
                if (stdCount == 1) {
                    writeZonePropsByTime(w, false, stdName, stdFromOffset, stdToOffset, stdStartTime,
                            true, status);
                } else {
                    writeZonePropsByDOW(w, false, stdName, stdFromOffset, stdToOffset,
                            stdMonth, stdWeekInMonth, stdDayOfWeek, stdStartTime, stdUntilTime, status);
                }
                if (U_FAILURE(status)) {
                    goto cleanupWriteZone;
                }
            } else {
                if (stdCount == 1) {
                    writeFinalRule(w, false, finalStdRule,
                            stdFromOffset - stdFromDSTSavings, stdFromDSTSavings, stdStartTime, status);
                } else {
                    // Use a single rule if possible
                    if (isEquivalentDateRule(stdMonth, stdWeekInMonth, stdDayOfWeek, finalStdRule->getRule())) {
                        writeZonePropsByDOW(w, false, stdName, stdFromOffset, stdToOffset,
                                stdMonth, stdWeekInMonth, stdDayOfWeek, stdStartTime, MAX_MILLIS, status);
                    } else {
                        // Not equivalent rule - write out two different rules
                        writeZonePropsByDOW(w, false, stdName, stdFromOffset, stdToOffset,
                                stdMonth, stdWeekInMonth, stdDayOfWeek, stdStartTime, stdUntilTime, status);
                        if (U_FAILURE(status)) {
                            goto cleanupWriteZone;
                        }
                        UDate nextStart;
                        UBool nextStartAvail = finalStdRule->getNextStart(stdUntilTime, stdFromOffset - stdFromDSTSavings, stdFromDSTSavings, false, nextStart);
                        U_ASSERT(nextStartAvail);
                        if (nextStartAvail) {
                            writeFinalRule(w, false, finalStdRule,
                                    stdFromOffset - stdFromDSTSavings, stdFromDSTSavings, nextStart, status);
                        }
                    }
                }
                if (U_FAILURE(status)) {
                    goto cleanupWriteZone;
                }
            }
        }            
    }
    writeFooter(w, status);

cleanupWriteZone:

    delete finalStdRule;
    delete finalDstRule;
}

void
VTimeZone::writeHeaders(VTZWriter& writer, UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return;
    }
    UnicodeString tzid;
    tz->getID(tzid);

    writer.write(ICAL_BEGIN);
    writer.write(COLON);
    writer.write(ICAL_VTIMEZONE);
    writer.write(ICAL_NEWLINE);
    writer.write(ICAL_TZID);
    writer.write(COLON);
    writer.write(tzid);
    writer.write(ICAL_NEWLINE);
    if (tzurl.length() != 0) {
        writer.write(ICAL_TZURL);
        writer.write(COLON);
        writer.write(tzurl);
        writer.write(ICAL_NEWLINE);
    }
    if (lastmod != MAX_MILLIS) {
        UnicodeString lastmodStr;
        writer.write(ICAL_LASTMOD);
        writer.write(COLON);
        writer.write(getUTCDateTimeString(lastmod, lastmodStr, status));
        writer.write(ICAL_NEWLINE);
    }
}

/*
 * Write the closing section of the VTIMEZONE definition block
 */
void
VTimeZone::writeFooter(VTZWriter& writer, UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return;
    }
    writer.write(ICAL_END);
    writer.write(COLON);
    writer.write(ICAL_VTIMEZONE);
    writer.write(ICAL_NEWLINE);
}

/*
 * Write a single start time
 */
void
VTimeZone::writeZonePropsByTime(VTZWriter& writer, UBool isDst, const UnicodeString& zonename,
                                int32_t fromOffset, int32_t toOffset, UDate time, UBool withRDATE,
                                UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return;
    }
    beginZoneProps(writer, isDst, zonename, fromOffset, toOffset, time, status);
    if (U_FAILURE(status)) {
        return;
    }
    if (withRDATE) {
        writer.write(ICAL_RDATE);
        writer.write(COLON);
        UnicodeString timestr;
        writer.write(getDateTimeString(time + fromOffset, timestr, status));
        writer.write(ICAL_NEWLINE);
        if (U_FAILURE(status)) {
            return;
        }
    }
    endZoneProps(writer, isDst, status);
    if (U_FAILURE(status)) {
        return;
    }
}

/*
 * Write start times defined by a DOM rule using VTIMEZONE RRULE
 */
void
VTimeZone::writeZonePropsByDOM(VTZWriter& writer, UBool isDst, const UnicodeString& zonename,
                               int32_t fromOffset, int32_t toOffset,
                               int32_t month, int32_t dayOfMonth, UDate startTime, UDate untilTime,
                               UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return;
    }
    beginZoneProps(writer, isDst, zonename, fromOffset, toOffset, startTime, status);
    if (U_FAILURE(status)) {
        return;
    }
    beginRRULE(writer, month, status);
    if (U_FAILURE(status)) {
        return;
    }
    writer.write(ICAL_BYMONTHDAY);
    writer.write(EQUALS_SIGN);
    UnicodeString dstr;
    appendAsciiDigits(dayOfMonth, 0, dstr);
    writer.write(dstr);
    if (untilTime != MAX_MILLIS) {
        appendUNTIL(writer, getDateTimeString(untilTime + fromOffset, dstr, status), status);
        if (U_FAILURE(status)) {
            return;
        }
    }
    writer.write(ICAL_NEWLINE);
    endZoneProps(writer, isDst, status);
}

/*
 * Write start times defined by a DOW rule using VTIMEZONE RRULE
 */
void
VTimeZone::writeZonePropsByDOW(VTZWriter& writer, UBool isDst, const UnicodeString& zonename,
                               int32_t fromOffset, int32_t toOffset,
                               int32_t month, int32_t weekInMonth, int32_t dayOfWeek,
                               UDate startTime, UDate untilTime, UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return;
    }
    beginZoneProps(writer, isDst, zonename, fromOffset, toOffset, startTime, status);
    if (U_FAILURE(status)) {
        return;
    }
    beginRRULE(writer, month, status);
    if (U_FAILURE(status)) {
        return;
    }
    writer.write(ICAL_BYDAY);
    writer.write(EQUALS_SIGN);
    UnicodeString dstr;
    appendAsciiDigits(weekInMonth, 0, dstr);
    writer.write(dstr);    // -4, -3, -2, -1, 1, 2, 3, 4
    writer.write(ICAL_DOW_NAMES[dayOfWeek - 1]);    // SU, MO, TU...

    if (untilTime != MAX_MILLIS) {
        appendUNTIL(writer, getDateTimeString(untilTime + fromOffset, dstr, status), status);
        if (U_FAILURE(status)) {
            return;
        }
    }
    writer.write(ICAL_NEWLINE);
    endZoneProps(writer, isDst, status);
}

/*
 * Write start times defined by a DOW_GEQ_DOM rule using VTIMEZONE RRULE
 */
void
VTimeZone::writeZonePropsByDOW_GEQ_DOM(VTZWriter& writer, UBool isDst, const UnicodeString& zonename,
                                       int32_t fromOffset, int32_t toOffset,
                                       int32_t month, int32_t dayOfMonth, int32_t dayOfWeek,
                                       UDate startTime, UDate untilTime, UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return;
    }
    // Check if this rule can be converted to DOW rule
    if (dayOfMonth%7 == 1) {
        // Can be represented by DOW rule
        writeZonePropsByDOW(writer, isDst, zonename, fromOffset, toOffset,
                month, (dayOfMonth + 6)/7, dayOfWeek, startTime, untilTime, status);
        if (U_FAILURE(status)) {
            return;
        }
    } else if (month != UCAL_FEBRUARY && (MONTHLENGTH[month] - dayOfMonth)%7 == 6) {
        // Can be represented by DOW rule with negative week number
        writeZonePropsByDOW(writer, isDst, zonename, fromOffset, toOffset,
                month, -1*((MONTHLENGTH[month] - dayOfMonth + 1)/7), dayOfWeek, startTime, untilTime, status);
        if (U_FAILURE(status)) {
            return;
        }
    } else {
        // Otherwise, use BYMONTHDAY to include all possible dates
        beginZoneProps(writer, isDst, zonename, fromOffset, toOffset, startTime, status);
        if (U_FAILURE(status)) {
            return;
        }
        // Check if all days are in the same month
        int32_t startDay = dayOfMonth;
        int32_t currentMonthDays = 7;
    
        if (dayOfMonth <= 0) {
            // The start day is in previous month
            int32_t prevMonthDays = 1 - dayOfMonth;
            currentMonthDays -= prevMonthDays;

            int32_t prevMonth = (month - 1) < 0 ? 11 : month - 1;

            // Note: When a rule is separated into two, UNTIL attribute needs to be
            // calculated for each of them.  For now, we skip this, because we basically use this method
            // only for final rules, which does not have the UNTIL attribute
            writeZonePropsByDOW_GEQ_DOM_sub(writer, prevMonth, -prevMonthDays, dayOfWeek, prevMonthDays,
                MAX_MILLIS /* Do not use UNTIL */, fromOffset, status);
            if (U_FAILURE(status)) {
                return;
            }

            // Start from 1 for the rest
            startDay = 1;
        } else if (dayOfMonth + 6 > MONTHLENGTH[month]) {
            // Note: This code does not actually work well in February.  For now, days in month in
            // non-leap year.
            int32_t nextMonthDays = dayOfMonth + 6 - MONTHLENGTH[month];
            currentMonthDays -= nextMonthDays;

            int32_t nextMonth = (month + 1) > 11 ? 0 : month + 1;
            
            writeZonePropsByDOW_GEQ_DOM_sub(writer, nextMonth, 1, dayOfWeek, nextMonthDays,
                MAX_MILLIS /* Do not use UNTIL */, fromOffset, status);
            if (U_FAILURE(status)) {
                return;
            }
        }
        writeZonePropsByDOW_GEQ_DOM_sub(writer, month, startDay, dayOfWeek, currentMonthDays,
            untilTime, fromOffset, status);
        if (U_FAILURE(status)) {
            return;
        }
        endZoneProps(writer, isDst, status);
    }
}

/*
 * Called from writeZonePropsByDOW_GEQ_DOM
 */
void
VTimeZone::writeZonePropsByDOW_GEQ_DOM_sub(VTZWriter& writer, int32_t month, int32_t dayOfMonth,
                                           int32_t dayOfWeek, int32_t numDays,
                                           UDate untilTime, int32_t fromOffset, UErrorCode& status) const {

    if (U_FAILURE(status)) {
        return;
    }
    int32_t startDayNum = dayOfMonth;
    UBool isFeb = (month == UCAL_FEBRUARY);
    if (dayOfMonth < 0 && !isFeb) {
        // Use positive number if possible
        startDayNum = MONTHLENGTH[month] + dayOfMonth + 1;
    }
    beginRRULE(writer, month, status);
    if (U_FAILURE(status)) {
        return;
    }
    writer.write(ICAL_BYDAY);
    writer.write(EQUALS_SIGN);
    writer.write(ICAL_DOW_NAMES[dayOfWeek - 1]);    // SU, MO, TU...
    writer.write(SEMICOLON);
    writer.write(ICAL_BYMONTHDAY);
    writer.write(EQUALS_SIGN);

    UnicodeString dstr;
    appendAsciiDigits(startDayNum, 0, dstr);
    writer.write(dstr);
    for (int32_t i = 1; i < numDays; i++) {
        writer.write(COMMA);
        dstr.remove();
        appendAsciiDigits(startDayNum + i, 0, dstr);
        writer.write(dstr);
    }

    if (untilTime != MAX_MILLIS) {
        appendUNTIL(writer, getDateTimeString(untilTime + fromOffset, dstr, status), status);
        if (U_FAILURE(status)) {
            return;
        }
    }
    writer.write(ICAL_NEWLINE);
}

/*
 * Write start times defined by a DOW_LEQ_DOM rule using VTIMEZONE RRULE
 */
void
VTimeZone::writeZonePropsByDOW_LEQ_DOM(VTZWriter& writer, UBool isDst, const UnicodeString& zonename,
                                       int32_t fromOffset, int32_t toOffset,
                                       int32_t month, int32_t dayOfMonth, int32_t dayOfWeek,
                                       UDate startTime, UDate untilTime, UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return;
    }
    // Check if this rule can be converted to DOW rule
    if (dayOfMonth%7 == 0) {
        // Can be represented by DOW rule
        writeZonePropsByDOW(writer, isDst, zonename, fromOffset, toOffset,
                month, dayOfMonth/7, dayOfWeek, startTime, untilTime, status);
    } else if (month != UCAL_FEBRUARY && (MONTHLENGTH[month] - dayOfMonth)%7 == 0){
        // Can be represented by DOW rule with negative week number
        writeZonePropsByDOW(writer, isDst, zonename, fromOffset, toOffset,
                month, -1*((MONTHLENGTH[month] - dayOfMonth)/7 + 1), dayOfWeek, startTime, untilTime, status);
    } else if (month == UCAL_FEBRUARY && dayOfMonth == 29) {
        // Special case for February
        writeZonePropsByDOW(writer, isDst, zonename, fromOffset, toOffset,
                UCAL_FEBRUARY, -1, dayOfWeek, startTime, untilTime, status);
    } else {
        // Otherwise, convert this to DOW_GEQ_DOM rule
        writeZonePropsByDOW_GEQ_DOM(writer, isDst, zonename, fromOffset, toOffset,
                month, dayOfMonth - 6, dayOfWeek, startTime, untilTime, status);
    }
}

/*
 * Write the final time zone rule using RRULE, with no UNTIL attribute
 */
void
VTimeZone::writeFinalRule(VTZWriter& writer, UBool isDst, const AnnualTimeZoneRule* rule,
                          int32_t fromRawOffset, int32_t fromDSTSavings,
                          UDate startTime, UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return;
    }
    UBool modifiedRule = true;
    const DateTimeRule *dtrule = toWallTimeRule(rule->getRule(), fromRawOffset, fromDSTSavings, status);
    if (U_FAILURE(status)) {
        return;
    }
    if (dtrule == nullptr) {
        modifiedRule = false;
        dtrule = rule->getRule();
    }

    // If the rule's mills in a day is out of range, adjust start time.
    // Olson tzdata supports 24:00 of a day, but VTIMEZONE does not.
    // See ticket#7008/#7518

    int32_t timeInDay = dtrule->getRuleMillisInDay();
    if (timeInDay < 0) {
        startTime = startTime + (0 - timeInDay);
    } else if (timeInDay >= U_MILLIS_PER_DAY) {
        startTime = startTime - (timeInDay - (U_MILLIS_PER_DAY - 1));
    }

    int32_t toOffset = rule->getRawOffset() + rule->getDSTSavings();
    UnicodeString name;
    rule->getName(name);
    switch (dtrule->getDateRuleType()) {
    case DateTimeRule::DOM:
        writeZonePropsByDOM(writer, isDst, name, fromRawOffset + fromDSTSavings, toOffset,
                dtrule->getRuleMonth(), dtrule->getRuleDayOfMonth(), startTime, MAX_MILLIS, status);
        break;
    case DateTimeRule::DOW:
        writeZonePropsByDOW(writer, isDst, name, fromRawOffset + fromDSTSavings, toOffset,
                dtrule->getRuleMonth(), dtrule->getRuleWeekInMonth(), dtrule->getRuleDayOfWeek(), startTime, MAX_MILLIS, status);
        break;
    case DateTimeRule::DOW_GEQ_DOM:
        writeZonePropsByDOW_GEQ_DOM(writer, isDst, name, fromRawOffset + fromDSTSavings, toOffset,
                dtrule->getRuleMonth(), dtrule->getRuleDayOfMonth(), dtrule->getRuleDayOfWeek(), startTime, MAX_MILLIS, status);
        break;
    case DateTimeRule::DOW_LEQ_DOM:
        writeZonePropsByDOW_LEQ_DOM(writer, isDst, name, fromRawOffset + fromDSTSavings, toOffset,
                dtrule->getRuleMonth(), dtrule->getRuleDayOfMonth(), dtrule->getRuleDayOfWeek(), startTime, MAX_MILLIS, status);
        break;
    }
    if (modifiedRule) {
        delete dtrule;
    }
}

/*
 * Write the opening section of zone properties
 */
void
VTimeZone::beginZoneProps(VTZWriter& writer, UBool isDst, const UnicodeString& zonename,
                          int32_t fromOffset, int32_t toOffset, UDate startTime, UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return;
    }
    writer.write(ICAL_BEGIN);
    writer.write(COLON);
    if (isDst) {
        writer.write(ICAL_DAYLIGHT);
    } else {
        writer.write(ICAL_STANDARD);
    }
    writer.write(ICAL_NEWLINE);

    UnicodeString dstr;

    // TZOFFSETTO
    writer.write(ICAL_TZOFFSETTO);
    writer.write(COLON);
    millisToOffset(toOffset, dstr);
    writer.write(dstr);
    writer.write(ICAL_NEWLINE);

    // TZOFFSETFROM
    writer.write(ICAL_TZOFFSETFROM);
    writer.write(COLON);
    millisToOffset(fromOffset, dstr);
    writer.write(dstr);
    writer.write(ICAL_NEWLINE);

    // TZNAME
    writer.write(ICAL_TZNAME);
    writer.write(COLON);
    writer.write(zonename);
    writer.write(ICAL_NEWLINE);
    
    // DTSTART
    writer.write(ICAL_DTSTART);
    writer.write(COLON);
    writer.write(getDateTimeString(startTime + fromOffset, dstr, status));
    if (U_FAILURE(status)) {
        return;
    }
    writer.write(ICAL_NEWLINE);        
}

/*
 * Writes the closing section of zone properties
 */
void
VTimeZone::endZoneProps(VTZWriter& writer, UBool isDst, UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return;
    }
    // END:STANDARD or END:DAYLIGHT
    writer.write(ICAL_END);
    writer.write(COLON);
    if (isDst) {
        writer.write(ICAL_DAYLIGHT);
    } else {
        writer.write(ICAL_STANDARD);
    }
    writer.write(ICAL_NEWLINE);
}

/*
 * Write the beginning part of RRULE line
 */
void
VTimeZone::beginRRULE(VTZWriter& writer, int32_t month, UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return;
    }
    UnicodeString dstr;
    writer.write(ICAL_RRULE);
    writer.write(COLON);
    writer.write(ICAL_FREQ);
    writer.write(EQUALS_SIGN);
    writer.write(ICAL_YEARLY);
    writer.write(SEMICOLON);
    writer.write(ICAL_BYMONTH);
    writer.write(EQUALS_SIGN);
    appendAsciiDigits(month + 1, 0, dstr);
    writer.write(dstr);
    writer.write(SEMICOLON);
}

/*
 * Append the UNTIL attribute after RRULE line
 */
void
VTimeZone::appendUNTIL(VTZWriter& writer, const UnicodeString& until,  UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return;
    }
    if (until.length() > 0) {
        writer.write(SEMICOLON);
        writer.write(ICAL_UNTIL);
        writer.write(EQUALS_SIGN);
        writer.write(until);
    }
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */

//eof
