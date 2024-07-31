// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
********************************************************************************
*   Copyright (C) 2005-2016, International Business Machines
*   Corporation and others.  All Rights Reserved.
********************************************************************************
*
* File WINNMFMT.CPP
*
********************************************************************************
*/

#include "unicode/utypes.h"

#if U_PLATFORM_USES_ONLY_WIN32_API

#if !UCONFIG_NO_FORMATTING

#include "winnmfmt.h"

#include "unicode/format.h"
#include "unicode/numfmt.h"
#include "unicode/locid.h"
#include "unicode/ustring.h"

#include "charstr.h"
#include "cmemory.h"
#include "uassert.h"
#include "ulocimp.h"
#include "locmap.h"

#ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
#endif
#   define VC_EXTRALEAN
#   define NOUSER
#   define NOSERVICE
#   define NOIME
#   define NOMCX
#include <windows.h>
#include <stdio.h>

U_NAMESPACE_BEGIN

union FormatInfo
{
    NUMBERFMTW   number;
    CURRENCYFMTW currency;
};

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(Win32NumberFormat)

#define NEW_ARRAY(type,count) (type *) uprv_malloc((count) * sizeof(type))
#define DELETE_ARRAY(array) uprv_free((void *) (array))

#define STACK_BUFFER_SIZE 32

/*
 * Turns a string of the form "3;2;0" into the grouping UINT
 * needed for NUMBERFMT and CURRENCYFMT. If the string does not
 * end in ";0" then the return value should be multiplied by 10.
 * (e.g. "3" => 30, "3;2" => 320)
 */
static UINT getGrouping(const wchar_t *grouping)
{
    UINT g = 0;
    const wchar_t *s;

    for (s = grouping; *s != L'\0'; s += 1) {
        if (*s > L'0' && *s < L'9') {
            g = g * 10 + (*s - L'0');
        } else if (*s != L';') {
            break;
        }
    }

    if (*s != L'0') {
        g *= 10;
    }

    return g;
}

static void getNumberFormat(NUMBERFMTW *fmt, const wchar_t *windowsLocaleName)
{
    wchar_t buf[10];

    GetLocaleInfoEx(windowsLocaleName, LOCALE_RETURN_NUMBER|LOCALE_IDIGITS, (LPWSTR) &fmt->NumDigits, sizeof(UINT));
    GetLocaleInfoEx(windowsLocaleName, LOCALE_RETURN_NUMBER|LOCALE_ILZERO,  (LPWSTR) &fmt->LeadingZero, sizeof(UINT));

    GetLocaleInfoEx(windowsLocaleName, LOCALE_SGROUPING, (LPWSTR)buf, 10);
    fmt->Grouping = getGrouping(buf);

    fmt->lpDecimalSep = NEW_ARRAY(wchar_t, 6);
    GetLocaleInfoEx(windowsLocaleName, LOCALE_SDECIMAL,  fmt->lpDecimalSep,  6);

    fmt->lpThousandSep = NEW_ARRAY(wchar_t, 6);
    GetLocaleInfoEx(windowsLocaleName, LOCALE_STHOUSAND, fmt->lpThousandSep, 6);

    GetLocaleInfoEx(windowsLocaleName, LOCALE_RETURN_NUMBER|LOCALE_INEGNUMBER, (LPWSTR) &fmt->NegativeOrder, sizeof(UINT));
}

static void freeNumberFormat(NUMBERFMTW *fmt)
{
    if (fmt != nullptr) {
        DELETE_ARRAY(fmt->lpThousandSep);
        DELETE_ARRAY(fmt->lpDecimalSep);
    }
}

static void getCurrencyFormat(CURRENCYFMTW *fmt, const wchar_t *windowsLocaleName)
{
    wchar_t buf[10];

    GetLocaleInfoEx(windowsLocaleName, LOCALE_RETURN_NUMBER|LOCALE_ICURRDIGITS, (LPWSTR) &fmt->NumDigits, sizeof(UINT));
    GetLocaleInfoEx(windowsLocaleName, LOCALE_RETURN_NUMBER|LOCALE_ILZERO, (LPWSTR) &fmt->LeadingZero, sizeof(UINT));

    GetLocaleInfoEx(windowsLocaleName, LOCALE_SMONGROUPING, (LPWSTR)buf, sizeof(buf));
    fmt->Grouping = getGrouping(buf);

    fmt->lpDecimalSep = NEW_ARRAY(wchar_t, 6);
    GetLocaleInfoEx(windowsLocaleName, LOCALE_SMONDECIMALSEP,  fmt->lpDecimalSep,  6);

    fmt->lpThousandSep = NEW_ARRAY(wchar_t, 6);
    GetLocaleInfoEx(windowsLocaleName, LOCALE_SMONTHOUSANDSEP, fmt->lpThousandSep, 6);

    GetLocaleInfoEx(windowsLocaleName, LOCALE_RETURN_NUMBER|LOCALE_INEGCURR,  (LPWSTR) &fmt->NegativeOrder, sizeof(UINT));
    GetLocaleInfoEx(windowsLocaleName, LOCALE_RETURN_NUMBER|LOCALE_ICURRENCY, (LPWSTR) &fmt->PositiveOrder, sizeof(UINT));

    fmt->lpCurrencySymbol = NEW_ARRAY(wchar_t, 8);
    GetLocaleInfoEx(windowsLocaleName, LOCALE_SCURRENCY, (LPWSTR) fmt->lpCurrencySymbol, 8);
}

static void freeCurrencyFormat(CURRENCYFMTW *fmt)
{
    if (fmt != nullptr) {
        DELETE_ARRAY(fmt->lpCurrencySymbol);
        DELETE_ARRAY(fmt->lpThousandSep);
        DELETE_ARRAY(fmt->lpDecimalSep);
    }
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

Win32NumberFormat::Win32NumberFormat(const Locale &locale, UBool currency, UErrorCode &status)
  : NumberFormat(), fCurrency(currency), fFormatInfo(nullptr), fFractionDigitsSet(false), fWindowsLocaleName(nullptr)
{
    if (!U_FAILURE(status)) {
        fLCID = locale.getLCID();

        GetEquivalentWindowsLocaleName(locale, &fWindowsLocaleName);
        // Note: In the previous code, it would look up the LCID for the locale, and if
        // the locale was not recognized then it would get an LCID of 0, which is a
        // synonym for LOCALE_USER_DEFAULT on Windows.
        // If the above method fails, then fWindowsLocaleName will remain as nullptr, and 
        // then we will pass nullptr to API GetLocaleInfoEx, which is the same as passing
        // LOCALE_USER_DEFAULT.

        // Resolve actual locale to be used later
        UErrorCode tmpsts = U_ZERO_ERROR;
        char tmpLocID[ULOC_FULLNAME_CAPACITY];
        int32_t len = uloc_getLocaleForLCID(fLCID, tmpLocID, UPRV_LENGTHOF(tmpLocID) - 1, &tmpsts);
        if (U_SUCCESS(tmpsts)) {
            tmpLocID[len] = 0;
            fLocale = Locale((const char*)tmpLocID);
        }

        const wchar_t *localeName = nullptr;

        if (fWindowsLocaleName != nullptr)
        {
            localeName = reinterpret_cast<const wchar_t*>(toOldUCharPtr(fWindowsLocaleName->getTerminatedBuffer()));
        }

        fFormatInfo = (FormatInfo*)uprv_malloc(sizeof(FormatInfo));

        if (fCurrency) {
            getCurrencyFormat(&fFormatInfo->currency, localeName);
        } else {
            getNumberFormat(&fFormatInfo->number, localeName);
        }
    }
}

Win32NumberFormat::Win32NumberFormat(const Win32NumberFormat &other)
  : NumberFormat(other), fFormatInfo((FormatInfo*)uprv_malloc(sizeof(FormatInfo)))
{
    if (fFormatInfo != nullptr) {
        uprv_memset(fFormatInfo, 0, sizeof(*fFormatInfo));
    }
    *this = other;
}

Win32NumberFormat::~Win32NumberFormat()
{
    if (fFormatInfo != nullptr) {
        if (fCurrency) {
            freeCurrencyFormat(&fFormatInfo->currency);
        } else {
            freeNumberFormat(&fFormatInfo->number);
        }

        uprv_free(fFormatInfo);
    }
    delete fWindowsLocaleName;
}

Win32NumberFormat &Win32NumberFormat::operator=(const Win32NumberFormat &other)
{
    if (this == &other) { return *this; }  // self-assignment: no-op
    NumberFormat::operator=(other);

    this->fCurrency          = other.fCurrency;
    this->fLocale            = other.fLocale;
    this->fLCID              = other.fLCID;
    this->fFractionDigitsSet = other.fFractionDigitsSet;
    this->fWindowsLocaleName = other.fWindowsLocaleName == nullptr ? nullptr : new UnicodeString(*other.fWindowsLocaleName);
    
    const wchar_t *localeName = nullptr;

    if (fWindowsLocaleName != nullptr)
    {
        localeName = reinterpret_cast<const wchar_t*>(toOldUCharPtr(fWindowsLocaleName->getTerminatedBuffer()));
    }

    if (fCurrency) {
        freeCurrencyFormat(&fFormatInfo->currency);
        getCurrencyFormat(&fFormatInfo->currency, localeName);
    } else {
        freeNumberFormat(&fFormatInfo->number);
        getNumberFormat(&fFormatInfo->number, localeName);
    }

    return *this;
}

Win32NumberFormat *Win32NumberFormat::clone() const
{
    return new Win32NumberFormat(*this);
}

UnicodeString& Win32NumberFormat::format(double number, UnicodeString& appendTo, FieldPosition& /* pos */) const
{
    return format(getMaximumFractionDigits(), appendTo, L"%.16f", number);
}

UnicodeString& Win32NumberFormat::format(int32_t number, UnicodeString& appendTo, FieldPosition& /* pos */) const
{
    return format(getMinimumFractionDigits(), appendTo, L"%I32d", number);
}

UnicodeString& Win32NumberFormat::format(int64_t number, UnicodeString& appendTo, FieldPosition& /* pos */) const
{
    return format(getMinimumFractionDigits(), appendTo, L"%I64d", number);
}

void Win32NumberFormat::parse(const UnicodeString& text, Formattable& result, ParsePosition& parsePosition) const
{
    UErrorCode status = U_ZERO_ERROR;
    NumberFormat *nf = fCurrency? NumberFormat::createCurrencyInstance(fLocale, status) : NumberFormat::createInstance(fLocale, status);

    nf->parse(text, result, parsePosition);
    delete nf;
}
void Win32NumberFormat::setMaximumFractionDigits(int32_t newValue)
{
    fFractionDigitsSet = true;
    NumberFormat::setMaximumFractionDigits(newValue);
}

void Win32NumberFormat::setMinimumFractionDigits(int32_t newValue)
{
    fFractionDigitsSet = true;
    NumberFormat::setMinimumFractionDigits(newValue);
}

UnicodeString &Win32NumberFormat::format(int32_t numDigits, UnicodeString &appendTo, const wchar_t *fmt, ...) const
{
    wchar_t nStackBuffer[STACK_BUFFER_SIZE];
    wchar_t *nBuffer = nStackBuffer;
    va_list args;
    int result;

    nBuffer[0] = 0x0000;

    /* Due to the arguments causing a result to be <= 23 characters (+2 for nullptr and minus),
    we don't need to reallocate the buffer. */
    va_start(args, fmt);
    result = _vsnwprintf(nBuffer, STACK_BUFFER_SIZE, fmt, args);
    va_end(args);

    /* Just to make sure of the above statement, we add this assert */
    U_ASSERT(result >=0);
    // The following code is not used because _vscwprintf isn't available on MinGW at the moment.
    /*if (result < 0) {
        int newLength;

        va_start(args, fmt);
        newLength = _vscwprintf(fmt, args);
        va_end(args);

        nBuffer = NEW_ARRAY(char16_t, newLength + 1);

        va_start(args, fmt);
        result = _vsnwprintf(nBuffer, newLength + 1, fmt, args);
        va_end(args);
    }*/

    // vswprintf is sensitive to the locale set by setlocale. For some locales
    // it doesn't use "." as the decimal separator, which is what GetNumberFormatW
    // and GetCurrencyFormatW both expect to see.
    //
    // To fix this, we scan over the string and replace the first non-digits, except
    // for a leading "-", with a "."
    //
    // Note: (nBuffer[0] == L'-') will evaluate to 1 if there is a leading '-' in the
    // number, and 0 otherwise.
    for (wchar_t *p = &nBuffer[nBuffer[0] == L'-']; *p != L'\0'; p += 1) {
        if (*p < L'0' || *p > L'9') {
            *p = L'.';
            break;
        }
    }

    wchar_t stackBuffer[STACK_BUFFER_SIZE];
    wchar_t *buffer = stackBuffer;
    FormatInfo formatInfo;

    formatInfo = *fFormatInfo;
    buffer[0] = 0x0000;

    const wchar_t *localeName = nullptr;

    if (fWindowsLocaleName != nullptr)
    {
        localeName = reinterpret_cast<const wchar_t*>(toOldUCharPtr(fWindowsLocaleName->getTerminatedBuffer()));
    }

    if (fCurrency) {
        if (fFractionDigitsSet) {
            formatInfo.currency.NumDigits = (UINT) numDigits;
        }

        if (!isGroupingUsed()) {
            formatInfo.currency.Grouping = 0;
        }

        result = GetCurrencyFormatEx(localeName, 0, nBuffer, &formatInfo.currency, buffer, STACK_BUFFER_SIZE);

        if (result == 0) {
            DWORD lastError = GetLastError();

            if (lastError == ERROR_INSUFFICIENT_BUFFER) {
                int newLength = GetCurrencyFormatEx(localeName, 0, nBuffer, &formatInfo.currency, nullptr, 0);

                buffer = NEW_ARRAY(wchar_t, newLength);
                buffer[0] = 0x0000;
                GetCurrencyFormatEx(localeName, 0, nBuffer,  &formatInfo.currency, buffer, newLength);
            }
        }
    } else {
        if (fFractionDigitsSet) {
            formatInfo.number.NumDigits = (UINT) numDigits;
        }

        if (!isGroupingUsed()) {
            formatInfo.number.Grouping = 0;
        }

        result = GetNumberFormatEx(localeName, 0, nBuffer, &formatInfo.number, buffer, STACK_BUFFER_SIZE);

        if (result == 0) {
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                int newLength = GetNumberFormatEx(localeName, 0, nBuffer, &formatInfo.number, nullptr, 0);

                buffer = NEW_ARRAY(wchar_t, newLength);
                buffer[0] = 0x0000;
                GetNumberFormatEx(localeName, 0, nBuffer, &formatInfo.number, buffer, newLength);
            }
        }
    }

    appendTo.append((char16_t *)buffer, (int32_t) wcslen(buffer));

    if (buffer != stackBuffer) {
        DELETE_ARRAY(buffer);
    }

    /*if (nBuffer != nStackBuffer) {
        DELETE_ARRAY(nBuffer);
    }*/

    return appendTo;
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */

#endif // U_PLATFORM_USES_ONLY_WIN32_API
