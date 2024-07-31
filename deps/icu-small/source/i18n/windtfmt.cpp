// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
********************************************************************************
*   Copyright (C) 2005-2016, International Business Machines
*   Corporation and others.  All Rights Reserved.
********************************************************************************
*
* File WINDTFMT.CPP
*
********************************************************************************
*/

#include "unicode/utypes.h"

#if U_PLATFORM_USES_ONLY_WIN32_API

#if !UCONFIG_NO_FORMATTING

#include "unicode/ures.h"
#include "unicode/format.h"
#include "unicode/fmtable.h"
#include "unicode/datefmt.h"
#include "unicode/simpleformatter.h"
#include "unicode/calendar.h"
#include "unicode/gregocal.h"
#include "unicode/locid.h"
#include "unicode/unistr.h"
#include "unicode/ustring.h"
#include "unicode/timezone.h"
#include "unicode/utmscale.h"

#include "charstr.h"
#include "cmemory.h"
#include "ulocimp.h"
#include "uresimp.h"
#include "windtfmt.h"
#include "wintzimpl.h"

#ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
#endif
#   define VC_EXTRALEAN
#   define NOUSER
#   define NOSERVICE
#   define NOIME
#   define NOMCX
#include <windows.h>

U_NAMESPACE_BEGIN

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(Win32DateFormat)

#define NEW_ARRAY(type,count) (type *) uprv_malloc((count) * sizeof(type))
#define DELETE_ARRAY(array) uprv_free((void *) (array))

#define STACK_BUFFER_SIZE 64

UnicodeString* Win32DateFormat::getTimeDateFormat(const Calendar *cal, const Locale *locale, UErrorCode &status) const
{
    UnicodeString *result = nullptr;
    const char *type = cal->getType();
    const char *base = locale->getBaseName();
    UResourceBundle *topBundle = ures_open((char *) 0, base, &status);
    UResourceBundle *calBundle = ures_getByKey(topBundle, "calendar", nullptr, &status);
    UResourceBundle *typBundle = ures_getByKeyWithFallback(calBundle, type, nullptr, &status);
    UResourceBundle *patBundle = ures_getByKeyWithFallback(typBundle, "DateTimePatterns", nullptr, &status);

    if (status == U_MISSING_RESOURCE_ERROR) {
        status = U_ZERO_ERROR;
        typBundle = ures_getByKeyWithFallback(calBundle, "gregorian", typBundle, &status);
        patBundle = ures_getByKeyWithFallback(typBundle, "DateTimePatterns", patBundle, &status);
    }

    if (U_FAILURE(status)) {
        static const char16_t defaultPattern[] = {0x007B, 0x0031, 0x007D, 0x0020, 0x007B, 0x0030, 0x007D, 0x0000}; // "{1} {0}"
        return new UnicodeString(defaultPattern, UPRV_LENGTHOF(defaultPattern));
    }

    int32_t resStrLen = 0;
    int32_t glueIndex = DateFormat::kDateTime;
    int32_t patSize = ures_getSize(patBundle);
    if (patSize >= (DateFormat::kDateTimeOffset + DateFormat::kShort + 1)) {
        // Get proper date time format
        glueIndex = (int32_t)(DateFormat::kDateTimeOffset + (fDateStyle - DateFormat::kDateOffset));
    }
    const char16_t *resStr = ures_getStringByIndex(patBundle, glueIndex, &resStrLen, &status);

    result = new UnicodeString(true, resStr, resStrLen);

    ures_close(patBundle);
    ures_close(typBundle);
    ures_close(calBundle);
    ures_close(topBundle);

    return result;
}

// TODO: This is copied in both winnmfmt.cpp and windtfmt.cpp, but really should
// be factored out into a common helper for both.
static UErrorCode GetEquivalentWindowsLocaleName(const Locale& locale, UnicodeString** buffer)
{
    UErrorCode status = U_ZERO_ERROR;

    // Convert from names like "en_CA" and "de_DE@collation=phonebook" to "en-CA" and "de-DE-u-co-phonebk".
    CharString asciiBCP47Tag = ulocimp_toLanguageTag(locale.getName(), false, status);

    if (U_SUCCESS(status))
    {
        // Need it to be UTF-16, not 8-bit
        // TODO: This seems like a good thing for a helper
        wchar_t bcp47Tag[LOCALE_NAME_MAX_LENGTH] = {};
        int32_t i;
        for (i = 0; i < UPRV_LENGTHOF(bcp47Tag); i++)
        {
            if (asciiBCP47Tag[i] == '\0')
            {
                break;
            }
            else
            {
                // normally just copy the character
                bcp47Tag[i] = static_cast<wchar_t>(asciiBCP47Tag[i]);
            }
        }

        // Ensure it's null terminated
        if (i < (UPRV_LENGTHOF(bcp47Tag) - 1))
        {
            bcp47Tag[i] = L'\0';
        }
        else
        {
            // Ran out of room.
            bcp47Tag[UPRV_LENGTHOF(bcp47Tag) - 1] = L'\0';
        }


        wchar_t windowsLocaleName[LOCALE_NAME_MAX_LENGTH] = {};

        // Note: On Windows versions below 10, there is no support for locale name aliases.
        // This means that it will fail for locales where ICU has a completely different
        // name (like ku vs ckb), and it will also not work for alternate sort locale
        // names like "de-DE-u-co-phonebk".
        
        // TODO: We could add some sort of exception table for cases like ku vs ckb.

        int length = ResolveLocaleName(bcp47Tag, windowsLocaleName, UPRV_LENGTHOF(windowsLocaleName));

        if (length > 0)
        {
            *buffer = new UnicodeString(windowsLocaleName);
        }
        else
        {
            status = U_UNSUPPORTED_ERROR;
        }
    }
    return status;
}

// TODO: Range-check timeStyle, dateStyle
Win32DateFormat::Win32DateFormat(DateFormat::EStyle timeStyle, DateFormat::EStyle dateStyle, const Locale &locale, UErrorCode &status)
  : DateFormat(), fDateTimeMsg(nullptr), fTimeStyle(timeStyle), fDateStyle(dateStyle), fLocale(locale), fZoneID(), fWindowsLocaleName(nullptr)
{
    if (U_SUCCESS(status)) {
        GetEquivalentWindowsLocaleName(locale, &fWindowsLocaleName);
        // Note: In the previous code, it would look up the LCID for the locale, and if
        // the locale was not recognized then it would get an LCID of 0, which is a
        // synonym for LOCALE_USER_DEFAULT on Windows.
        // If the above method fails, then fWindowsLocaleName will remain as nullptr, and 
        // then we will pass nullptr to API GetLocaleInfoEx, which is the same as passing
        // LOCALE_USER_DEFAULT.

        fTZI = NEW_ARRAY(TIME_ZONE_INFORMATION, 1);
        uprv_memset(fTZI, 0, sizeof(TIME_ZONE_INFORMATION));
        adoptCalendar(Calendar::createInstance(locale, status));
    }
}

Win32DateFormat::Win32DateFormat(const Win32DateFormat &other)
  : DateFormat(other)
{
    *this = other;
}

Win32DateFormat::~Win32DateFormat()
{
//    delete fCalendar;
    uprv_free(fTZI);
    delete fDateTimeMsg;
    delete fWindowsLocaleName;
}

Win32DateFormat &Win32DateFormat::operator=(const Win32DateFormat &other)
{
    if (this == &other) { return *this; }  // self-assignment: no-op
    // The following handles fCalendar
    DateFormat::operator=(other);

//    delete fCalendar;

    this->fDateTimeMsg = other.fDateTimeMsg == nullptr ? nullptr : new UnicodeString(*other.fDateTimeMsg);
    this->fTimeStyle   = other.fTimeStyle;
    this->fDateStyle   = other.fDateStyle;
    this->fLocale      = other.fLocale;
//    this->fCalendar    = other.fCalendar->clone();
    this->fZoneID      = other.fZoneID;

    this->fTZI = NEW_ARRAY(TIME_ZONE_INFORMATION, 1);
    *this->fTZI = *other.fTZI;

    this->fWindowsLocaleName = other.fWindowsLocaleName == nullptr ? nullptr : new UnicodeString(*other.fWindowsLocaleName);

    return *this;
}

Win32DateFormat *Win32DateFormat::clone() const
{
    return new Win32DateFormat(*this);
}

// TODO: Is just ignoring pos the right thing?
UnicodeString &Win32DateFormat::format(Calendar &cal, UnicodeString &appendTo, FieldPosition & /* pos */) const
{
    FILETIME ft;
    SYSTEMTIME st_gmt;
    SYSTEMTIME st_local;
    TIME_ZONE_INFORMATION tzi = *fTZI;
    UErrorCode status = U_ZERO_ERROR;
    const TimeZone &tz = cal.getTimeZone();
    int64_t uct, uft;

    setTimeZoneInfo(&tzi, tz);

    uct = utmscale_fromInt64((int64_t) cal.getTime(status), UDTS_ICU4C_TIME, &status);
    uft = utmscale_toInt64(uct, UDTS_WINDOWS_FILE_TIME, &status);

    ft.dwLowDateTime =  (DWORD) (uft & 0xFFFFFFFF);
    ft.dwHighDateTime = (DWORD) ((uft >> 32) & 0xFFFFFFFF);

    FileTimeToSystemTime(&ft, &st_gmt);
    SystemTimeToTzSpecificLocalTime(&tzi, &st_gmt, &st_local);


    if (fDateStyle != DateFormat::kNone && fTimeStyle != DateFormat::kNone) {
        UnicodeString date;
        UnicodeString time;
        UnicodeString *pattern = fDateTimeMsg;

        formatDate(&st_local, date);
        formatTime(&st_local, time);

        if (strcmp(fCalendar->getType(), cal.getType()) != 0) {
            pattern = getTimeDateFormat(&cal, &fLocale, status);
        }

        SimpleFormatter(*pattern, 2, 2, status).format(time, date, appendTo, status);
    } else if (fDateStyle != DateFormat::kNone) {
        formatDate(&st_local, appendTo);
    } else if (fTimeStyle != DateFormat::kNone) {
        formatTime(&st_local, appendTo);
    }

    return appendTo;
}

void Win32DateFormat::parse(const UnicodeString& /* text */, Calendar& /* cal */, ParsePosition& pos) const
{
    pos.setErrorIndex(pos.getIndex());
}

void Win32DateFormat::adoptCalendar(Calendar *newCalendar)
{
    if (fCalendar == nullptr || strcmp(fCalendar->getType(), newCalendar->getType()) != 0) {
        UErrorCode status = U_ZERO_ERROR;

        if (fDateStyle != DateFormat::kNone && fTimeStyle != DateFormat::kNone) {
            delete fDateTimeMsg;
            fDateTimeMsg = getTimeDateFormat(newCalendar, &fLocale, status);
        }
    }

    delete fCalendar;
    fCalendar = newCalendar;

    fZoneID = setTimeZoneInfo(fTZI, fCalendar->getTimeZone());
}

void Win32DateFormat::setCalendar(const Calendar &newCalendar)
{
    adoptCalendar(newCalendar.clone());
}

void Win32DateFormat::adoptTimeZone(TimeZone *zoneToAdopt)
{
    fZoneID = setTimeZoneInfo(fTZI, *zoneToAdopt);
    fCalendar->adoptTimeZone(zoneToAdopt);
}

void Win32DateFormat::setTimeZone(const TimeZone& zone)
{
    fZoneID = setTimeZoneInfo(fTZI, zone);
    fCalendar->setTimeZone(zone);
}

static const DWORD dfFlags[] = {DATE_LONGDATE, DATE_LONGDATE, DATE_SHORTDATE, DATE_SHORTDATE};

void Win32DateFormat::formatDate(const SYSTEMTIME *st, UnicodeString &appendTo) const
{
    int result=0;
    wchar_t stackBuffer[STACK_BUFFER_SIZE];
    wchar_t *buffer = stackBuffer;
    const wchar_t *localeName = nullptr;

    if (fWindowsLocaleName != nullptr)
    {
        localeName = reinterpret_cast<const wchar_t*>(toOldUCharPtr(fWindowsLocaleName->getTerminatedBuffer()));
    }

    result = GetDateFormatEx(localeName, dfFlags[fDateStyle - kDateOffset], st, nullptr, buffer, STACK_BUFFER_SIZE, nullptr);

    if (result == 0) {
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
            int newLength = GetDateFormatEx(localeName, dfFlags[fDateStyle - kDateOffset], st, nullptr, nullptr, 0, nullptr);

            buffer = NEW_ARRAY(wchar_t, newLength);

            GetDateFormatEx(localeName, dfFlags[fDateStyle - kDateOffset], st, nullptr, buffer, newLength, nullptr);
        }
    }

    appendTo.append((const char16_t *)buffer, (int32_t) wcslen(buffer));

    if (buffer != stackBuffer) {
        DELETE_ARRAY(buffer);
    }
}

static const DWORD tfFlags[] = {0, 0, 0, TIME_NOSECONDS};

void Win32DateFormat::formatTime(const SYSTEMTIME *st, UnicodeString &appendTo) const
{
    int result;
    wchar_t stackBuffer[STACK_BUFFER_SIZE];
    wchar_t *buffer = stackBuffer;
    const wchar_t *localeName = nullptr;

    if (fWindowsLocaleName != nullptr)
    {
        localeName = reinterpret_cast<const wchar_t*>(toOldUCharPtr(fWindowsLocaleName->getTerminatedBuffer()));
    }

    result = GetTimeFormatEx(localeName, tfFlags[fTimeStyle], st, nullptr, buffer, STACK_BUFFER_SIZE);

    if (result == 0) {
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
            int newLength = GetTimeFormatEx(localeName, tfFlags[fTimeStyle], st, nullptr, nullptr, 0);

            buffer = NEW_ARRAY(wchar_t, newLength);

            GetTimeFormatEx(localeName, tfFlags[fTimeStyle], st, nullptr, buffer, newLength);
        }
    }

    appendTo.append((const char16_t *)buffer, (int32_t) wcslen(buffer));

    if (buffer != stackBuffer) {
        DELETE_ARRAY(buffer);
    }
}

UnicodeString Win32DateFormat::setTimeZoneInfo(TIME_ZONE_INFORMATION *tzi, const TimeZone &zone) const
{
    UnicodeString zoneID;

    zone.getID(zoneID);

    if (zoneID.compare(fZoneID) != 0) {
        UnicodeString icuid;

        zone.getID(icuid);
        if (! uprv_getWindowsTimeZoneInfo(tzi, icuid.getBuffer(), icuid.length())) {
            UBool found = false;
            int32_t ec = TimeZone::countEquivalentIDs(icuid);

            for (int z = 0; z < ec; z += 1) {
                UnicodeString equiv = TimeZone::getEquivalentID(icuid, z);

                found = uprv_getWindowsTimeZoneInfo(tzi, equiv.getBuffer(), equiv.length());
                if (found) {
                    break;
                }
            }

            if (! found) {
                GetTimeZoneInformation(tzi);
            }
        }
    }

    return zoneID;
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */

#endif // U_PLATFORM_USES_ONLY_WIN32_API
