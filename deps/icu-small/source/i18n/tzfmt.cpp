// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2011-2015, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/calendar.h"
#include "unicode/tzfmt.h"
#include "unicode/numsys.h"
#include "unicode/strenum.h"
#include "unicode/uchar.h"
#include "unicode/udat.h"
#include "unicode/ustring.h"
#include "unicode/utf16.h"
#include "charstr.h"
#include "tzgnames.h"
#include "cmemory.h"
#include "cstring.h"
#include "putilimp.h"
#include "uassert.h"
#include "ucln_in.h"
#include "ulocimp.h"
#include "umutex.h"
#include "uresimp.h"
#include "ureslocs.h"
#include "uvector.h"
#include "zonemeta.h"
#include "tznames_impl.h"   // TextTrieMap
#include "patternprops.h"

U_NAMESPACE_BEGIN

// Bit flags used by the parse method.
// The order must match UTimeZoneFormatStyle enum.
#define ISO_Z_STYLE_FLAG 0x0080
#define ISO_LOCAL_STYLE_FLAG 0x0100
static const int16_t STYLE_PARSE_FLAGS[] = {
    0x0001, // UTZFMT_STYLE_GENERIC_LOCATION,
    0x0002, // UTZFMT_STYLE_GENERIC_LONG,
    0x0004, // UTZFMT_STYLE_GENERIC_SHORT,
    0x0008, // UTZFMT_STYLE_SPECIFIC_LONG,
    0x0010, // UTZFMT_STYLE_SPECIFIC_SHORT,
    0x0020, // UTZFMT_STYLE_LOCALIZED_GMT,
    0x0040, // UTZFMT_STYLE_LOCALIZED_GMT_SHORT,
    ISO_Z_STYLE_FLAG,       // UTZFMT_STYLE_ISO_BASIC_SHORT,
    ISO_LOCAL_STYLE_FLAG,   // UTZFMT_STYLE_ISO_BASIC_LOCAL_SHORT,
    ISO_Z_STYLE_FLAG,       // UTZFMT_STYLE_ISO_BASIC_FIXED,
    ISO_LOCAL_STYLE_FLAG,   // UTZFMT_STYLE_ISO_BASIC_LOCAL_FIXED,
    ISO_Z_STYLE_FLAG,       // UTZFMT_STYLE_ISO_BASIC_FULL,
    ISO_LOCAL_STYLE_FLAG,   // UTZFMT_STYLE_ISO_BASIC_LOCAL_FULL,
    ISO_Z_STYLE_FLAG,       // UTZFMT_STYLE_ISO_EXTENDED_FIXED,
    ISO_LOCAL_STYLE_FLAG,   // UTZFMT_STYLE_ISO_EXTENDED_LOCAL_FIXED,
    ISO_Z_STYLE_FLAG,       // UTZFMT_STYLE_ISO_EXTENDED_FULL,
    ISO_LOCAL_STYLE_FLAG,   // UTZFMT_STYLE_ISO_EXTENDED_LOCAL_FULL,
    0x0200, // UTZFMT_STYLE_ZONE_ID,
    0x0400, // UTZFMT_STYLE_ZONE_ID_SHORT,
    0x0800  // UTZFMT_STYLE_EXEMPLAR_LOCATION
};

static const char gZoneStringsTag[] = "zoneStrings";
static const char gGmtFormatTag[]= "gmtFormat";
static const char gGmtZeroFormatTag[] = "gmtZeroFormat";
static const char gHourFormatTag[]= "hourFormat";

static const char16_t TZID_GMT[] = {0x0045, 0x0074, 0x0063, 0x002F, 0x0047, 0x004D, 0x0054, 0};    // Etc/GMT
static const char16_t UNKNOWN_ZONE_ID[] = {
    0x0045, 0x0074, 0x0063, 0x002F, 0x0055, 0x006E, 0x006B, 0x006E, 0x006F, 0x0077, 0x006E, 0}; // Etc/Unknown
static const char16_t UNKNOWN_SHORT_ZONE_ID[] = {0x0075, 0x006E, 0x006B, 0};   // unk
static const char16_t UNKNOWN_LOCATION[] = {0x0055, 0x006E, 0x006B, 0x006E, 0x006F, 0x0077, 0x006E, 0};    // Unknown

static const char16_t DEFAULT_GMT_PATTERN[] = {0x0047, 0x004D, 0x0054, 0x007B, 0x0030, 0x007D, 0}; // GMT{0}
//static const char16_t DEFAULT_GMT_ZERO[] = {0x0047, 0x004D, 0x0054, 0}; // GMT
static const char16_t DEFAULT_GMT_POSITIVE_HM[] = {0x002B, 0x0048, 0x003A, 0x006D, 0x006D, 0}; // +H:mm
static const char16_t DEFAULT_GMT_POSITIVE_HMS[] = {0x002B, 0x0048, 0x003A, 0x006D, 0x006D, 0x003A, 0x0073, 0x0073, 0}; // +H:mm:ss
static const char16_t DEFAULT_GMT_NEGATIVE_HM[] = {0x002D, 0x0048, 0x003A, 0x006D, 0x006D, 0}; // -H:mm
static const char16_t DEFAULT_GMT_NEGATIVE_HMS[] = {0x002D, 0x0048, 0x003A, 0x006D, 0x006D, 0x003A, 0x0073, 0x0073, 0}; // -H:mm:ss
static const char16_t DEFAULT_GMT_POSITIVE_H[] = {0x002B, 0x0048, 0}; // +H
static const char16_t DEFAULT_GMT_NEGATIVE_H[] = {0x002D, 0x0048, 0}; // -H

static const UChar32 DEFAULT_GMT_DIGITS[] = {
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034,
    0x0035, 0x0036, 0x0037, 0x0038, 0x0039
};

static const char16_t DEFAULT_GMT_OFFSET_SEP = 0x003A; // ':'

static const char16_t ARG0[] = {0x007B, 0x0030, 0x007D};   // "{0}"
static const int32_t ARG0_LEN = 3;

static const char16_t DEFAULT_GMT_OFFSET_MINUTE_PATTERN[] = {0x006D, 0x006D, 0};   // "mm"
static const char16_t DEFAULT_GMT_OFFSET_SECOND_PATTERN[] = {0x0073, 0x0073, 0};   // "ss"

static const char16_t ALT_GMT_STRINGS[][4] = {
    {0x0047, 0x004D, 0x0054, 0},    // GMT
    {0x0055, 0x0054, 0x0043, 0},    // UTC
    {0x0055, 0x0054, 0, 0},         // UT
    {0, 0, 0, 0}
};

// Order of GMT offset pattern parsing, *_HMS must be evaluated first
// because *_HM is most likely a substring of *_HMS 
static const int32_t PARSE_GMT_OFFSET_TYPES[] = {
    UTZFMT_PAT_POSITIVE_HMS,
    UTZFMT_PAT_NEGATIVE_HMS,
    UTZFMT_PAT_POSITIVE_HM,
    UTZFMT_PAT_NEGATIVE_HM,
    UTZFMT_PAT_POSITIVE_H,
    UTZFMT_PAT_NEGATIVE_H,
    -1
};

static const char16_t SINGLEQUOTE  = 0x0027;
static const char16_t PLUS         = 0x002B;
static const char16_t MINUS        = 0x002D;
static const char16_t ISO8601_UTC  = 0x005A;   // 'Z'
static const char16_t ISO8601_SEP  = 0x003A;   // ':'

static const int32_t MILLIS_PER_HOUR = 60 * 60 * 1000;
static const int32_t MILLIS_PER_MINUTE = 60 * 1000;
static const int32_t MILLIS_PER_SECOND = 1000;

// Maximum offset (exclusive) in millisecond supported by offset formats
static int32_t MAX_OFFSET = 24 * MILLIS_PER_HOUR;

// Maximum values for GMT offset fields
static const int32_t MAX_OFFSET_HOUR = 23;
static const int32_t MAX_OFFSET_MINUTE = 59;
static const int32_t MAX_OFFSET_SECOND = 59;

static const int32_t UNKNOWN_OFFSET = 0x7FFFFFFF;

static const int32_t ALL_SIMPLE_NAME_TYPES = UTZNM_LONG_STANDARD | UTZNM_LONG_DAYLIGHT | UTZNM_SHORT_STANDARD | UTZNM_SHORT_DAYLIGHT | UTZNM_EXEMPLAR_LOCATION;
static const int32_t ALL_GENERIC_NAME_TYPES = UTZGNM_LOCATION | UTZGNM_LONG | UTZGNM_SHORT;

#define DIGIT_VAL(c) (0x0030 <= (c) && (c) <= 0x0039 ? (c) - 0x0030 : -1)
#define MAX_OFFSET_DIGITS 6

// Time Zone ID/Short ID trie
static TextTrieMap *gZoneIdTrie = nullptr;
static icu::UInitOnce gZoneIdTrieInitOnce {};

static TextTrieMap *gShortZoneIdTrie = nullptr;
static icu::UInitOnce gShortZoneIdTrieInitOnce {};

static UMutex gLock;

U_CDECL_BEGIN
/**
 * Cleanup callback func
 */
static UBool U_CALLCONV tzfmt_cleanup()
{
    delete gZoneIdTrie;
    gZoneIdTrie = nullptr;
    gZoneIdTrieInitOnce.reset();

    delete gShortZoneIdTrie;
    gShortZoneIdTrie = nullptr;
    gShortZoneIdTrieInitOnce.reset();

    return true;
}
U_CDECL_END

// ------------------------------------------------------------------
// GMTOffsetField
//
// This class represents a localized GMT offset pattern
// item and used by TimeZoneFormat
// ------------------------------------------------------------------
class GMTOffsetField : public UMemory {
public:
    enum FieldType {
        TEXT = 0,
        HOUR = 1,
        MINUTE = 2,
        SECOND = 4
    };

    virtual ~GMTOffsetField();

    static GMTOffsetField* createText(const UnicodeString& text, UErrorCode& status);
    static GMTOffsetField* createTimeField(FieldType type, uint8_t width, UErrorCode& status);
    static UBool isValid(FieldType type, int32_t width);
    static FieldType getTypeByLetter(char16_t ch);

    FieldType getType() const;
    uint8_t getWidth() const;
    const char16_t* getPatternText() const;

private:
    char16_t* fText;
    FieldType fType;
    uint8_t fWidth;

    GMTOffsetField();
};

GMTOffsetField::GMTOffsetField()
: fText(nullptr), fType(TEXT), fWidth(0) {
}

GMTOffsetField::~GMTOffsetField() {
    if (fText) {
        uprv_free(fText);
    }
}

GMTOffsetField*
GMTOffsetField::createText(const UnicodeString& text, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    GMTOffsetField* result = new GMTOffsetField();
    if (result == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }

    int32_t len = text.length();
    result->fText = (char16_t*)uprv_malloc((len + 1) * sizeof(char16_t));
    if (result->fText == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        delete result;
        return nullptr;
    }
    u_strncpy(result->fText, text.getBuffer(), len);
    result->fText[len] = 0;
    result->fType = TEXT;

    return result;
}

GMTOffsetField*
GMTOffsetField::createTimeField(FieldType type, uint8_t width, UErrorCode& status) {
    U_ASSERT(type != TEXT);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    GMTOffsetField* result = new GMTOffsetField();
    if (result == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }

    result->fType = type;
    result->fWidth = width;

    return result;
}

UBool
GMTOffsetField::isValid(FieldType type, int32_t width) {
    switch (type) {
    case HOUR:
        return (width == 1 || width == 2);
    case MINUTE:
    case SECOND:
        return (width == 2);
    default:
        UPRV_UNREACHABLE_EXIT;
    }
    return (width > 0);
}

GMTOffsetField::FieldType
GMTOffsetField::getTypeByLetter(char16_t ch) {
    if (ch == 0x0048 /* H */) {
        return HOUR;
    } else if (ch == 0x006D /* m */) {
        return MINUTE;
    } else if (ch == 0x0073 /* s */) {
        return SECOND;
    }
    return TEXT;
}

inline GMTOffsetField::FieldType
GMTOffsetField::getType() const {
     return fType;
 }

inline uint8_t
GMTOffsetField::getWidth() const {
    return fWidth;
}
 
inline const char16_t*
GMTOffsetField::getPatternText() const {
    return fText;
}


U_CDECL_BEGIN
static void U_CALLCONV
deleteGMTOffsetField(void *obj) {
    delete static_cast<GMTOffsetField *>(obj);
}
U_CDECL_END


// ------------------------------------------------------------------
// TimeZoneFormat
// ------------------------------------------------------------------
UOBJECT_DEFINE_RTTI_IMPLEMENTATION(TimeZoneFormat)

TimeZoneFormat::TimeZoneFormat(const Locale& locale, UErrorCode& status) 
: fLocale(locale), fTimeZoneNames(nullptr), fTimeZoneGenericNames(nullptr),
  fDefParseOptionFlags(0), fTZDBTimeZoneNames(nullptr) {

    for (int32_t i = 0; i < UTZFMT_PAT_COUNT; i++) {
        fGMTOffsetPatternItems[i] = nullptr;
    }

    const char* region = fLocale.getCountry();
    int32_t regionLen = static_cast<int32_t>(uprv_strlen(region));
    if (regionLen == 0) {
        UErrorCode tempStatus = U_ZERO_ERROR;
        CharString loc = ulocimp_addLikelySubtags(fLocale.getName(), tempStatus);

        regionLen = uloc_getCountry(loc.data(), fTargetRegion, sizeof(fTargetRegion), &tempStatus);
        if (U_SUCCESS(tempStatus)) {
            fTargetRegion[regionLen] = 0;
        } else {
            fTargetRegion[0] = 0;
        }
    } else if (regionLen < (int32_t)sizeof(fTargetRegion)) {
        uprv_strcpy(fTargetRegion, region);
    } else {
        fTargetRegion[0] = 0;
    }

    fTimeZoneNames = TimeZoneNames::createInstance(locale, status);
    // fTimeZoneGenericNames is lazily instantiated
    if (U_FAILURE(status)) {
        return;
    }

    const char16_t* gmtPattern = nullptr;
    const char16_t* hourFormats = nullptr;

    UResourceBundle *zoneBundle = ures_open(U_ICUDATA_ZONE, locale.getName(), &status);
    UResourceBundle *zoneStringsArray = ures_getByKeyWithFallback(zoneBundle, gZoneStringsTag, nullptr, &status);
    if (U_SUCCESS(status)) {
        const char16_t* resStr;
        int32_t len;
        resStr = ures_getStringByKeyWithFallback(zoneStringsArray, gGmtFormatTag, &len, &status);
        if (len > 0) {
            gmtPattern = resStr;
        }
        resStr = ures_getStringByKeyWithFallback(zoneStringsArray, gGmtZeroFormatTag, &len, &status);
        if (len > 0) {
            fGMTZeroFormat.setTo(true, resStr, len);
        }
        resStr = ures_getStringByKeyWithFallback(zoneStringsArray, gHourFormatTag, &len, &status);
        if (len > 0) {
            hourFormats = resStr;
        }
        ures_close(zoneStringsArray);
        ures_close(zoneBundle);
    }

    if (gmtPattern == nullptr) {
        gmtPattern = DEFAULT_GMT_PATTERN;
    }
    initGMTPattern(UnicodeString(true, gmtPattern, -1), status);

    UBool useDefaultOffsetPatterns = true;
    if (hourFormats) {
        char16_t *sep = u_strchr(hourFormats, (char16_t)0x003B /* ';' */);
        if (sep != nullptr) {
            UErrorCode tmpStatus = U_ZERO_ERROR;
            fGMTOffsetPatterns[UTZFMT_PAT_POSITIVE_HM].setTo(false, hourFormats, (int32_t)(sep - hourFormats));
            fGMTOffsetPatterns[UTZFMT_PAT_NEGATIVE_HM].setTo(true, sep + 1, -1);
            expandOffsetPattern(fGMTOffsetPatterns[UTZFMT_PAT_POSITIVE_HM], fGMTOffsetPatterns[UTZFMT_PAT_POSITIVE_HMS], tmpStatus);
            expandOffsetPattern(fGMTOffsetPatterns[UTZFMT_PAT_NEGATIVE_HM], fGMTOffsetPatterns[UTZFMT_PAT_NEGATIVE_HMS], tmpStatus);
            truncateOffsetPattern(fGMTOffsetPatterns[UTZFMT_PAT_POSITIVE_HM], fGMTOffsetPatterns[UTZFMT_PAT_POSITIVE_H], tmpStatus);
            truncateOffsetPattern(fGMTOffsetPatterns[UTZFMT_PAT_NEGATIVE_HM], fGMTOffsetPatterns[UTZFMT_PAT_NEGATIVE_H], tmpStatus);
            if (U_SUCCESS(tmpStatus)) {
                useDefaultOffsetPatterns = false;
            }
        }
    }
    if (useDefaultOffsetPatterns) {
        fGMTOffsetPatterns[UTZFMT_PAT_POSITIVE_H].setTo(true, DEFAULT_GMT_POSITIVE_H, -1);
        fGMTOffsetPatterns[UTZFMT_PAT_POSITIVE_HM].setTo(true, DEFAULT_GMT_POSITIVE_HM, -1);
        fGMTOffsetPatterns[UTZFMT_PAT_POSITIVE_HMS].setTo(true, DEFAULT_GMT_POSITIVE_HMS, -1);
        fGMTOffsetPatterns[UTZFMT_PAT_NEGATIVE_H].setTo(true, DEFAULT_GMT_NEGATIVE_H, -1);
        fGMTOffsetPatterns[UTZFMT_PAT_NEGATIVE_HM].setTo(true, DEFAULT_GMT_NEGATIVE_HM, -1);
        fGMTOffsetPatterns[UTZFMT_PAT_NEGATIVE_HMS].setTo(true, DEFAULT_GMT_NEGATIVE_HMS, -1);
    }
    initGMTOffsetPatterns(status);

    NumberingSystem* ns = NumberingSystem::createInstance(locale, status);
    UBool useDefDigits = true;
    if (ns && !ns->isAlgorithmic()) {
        UnicodeString digits = ns->getDescription();
        useDefDigits = !toCodePoints(digits, fGMTOffsetDigits, 10);
    }
    if (useDefDigits) {
        uprv_memcpy(fGMTOffsetDigits, DEFAULT_GMT_DIGITS, sizeof(UChar32) * 10);
    }
    delete ns;
}

TimeZoneFormat::TimeZoneFormat(const TimeZoneFormat& other)
: Format(other), fTimeZoneNames(nullptr), fTimeZoneGenericNames(nullptr),
  fTZDBTimeZoneNames(nullptr) {

    for (int32_t i = 0; i < UTZFMT_PAT_COUNT; i++) {
        fGMTOffsetPatternItems[i] = nullptr;
    }
    *this = other;
}


TimeZoneFormat::~TimeZoneFormat() {
    delete fTimeZoneNames;
    delete fTimeZoneGenericNames;
    delete fTZDBTimeZoneNames;
    for (int32_t i = 0; i < UTZFMT_PAT_COUNT; i++) {
        delete fGMTOffsetPatternItems[i];
    }
}

TimeZoneFormat&
TimeZoneFormat::operator=(const TimeZoneFormat& other) {
    if (this == &other) {
        return *this;
    }

    delete fTimeZoneNames;
    delete fTimeZoneGenericNames;
    fTimeZoneGenericNames = nullptr;
    delete fTZDBTimeZoneNames;
    fTZDBTimeZoneNames = nullptr;

    fLocale = other.fLocale;
    uprv_memcpy(fTargetRegion, other.fTargetRegion, sizeof(fTargetRegion));

    fTimeZoneNames = other.fTimeZoneNames->clone();
    if (other.fTimeZoneGenericNames) {
        // TODO: this test has dubious thread safety.
        fTimeZoneGenericNames = other.fTimeZoneGenericNames->clone();
    }

    fGMTPattern = other.fGMTPattern;
    fGMTPatternPrefix = other.fGMTPatternPrefix;
    fGMTPatternSuffix = other.fGMTPatternSuffix;

    UErrorCode status = U_ZERO_ERROR;
    for (int32_t i = 0; i < UTZFMT_PAT_COUNT; i++) {
        fGMTOffsetPatterns[i] = other.fGMTOffsetPatterns[i];
        delete fGMTOffsetPatternItems[i];
        fGMTOffsetPatternItems[i] = nullptr;
    }
    initGMTOffsetPatterns(status);
    U_ASSERT(U_SUCCESS(status));

    fGMTZeroFormat = other.fGMTZeroFormat;

    uprv_memcpy(fGMTOffsetDigits, other.fGMTOffsetDigits, sizeof(fGMTOffsetDigits));

    fDefParseOptionFlags = other.fDefParseOptionFlags;

    return *this;
}


bool
TimeZoneFormat::operator==(const Format& other) const {
    TimeZoneFormat* tzfmt = (TimeZoneFormat*)&other;

    bool isEqual =
            fLocale == tzfmt->fLocale
            && fGMTPattern == tzfmt->fGMTPattern
            && fGMTZeroFormat == tzfmt->fGMTZeroFormat
            && *fTimeZoneNames == *tzfmt->fTimeZoneNames;

    for (int32_t i = 0; i < UTZFMT_PAT_COUNT && isEqual; i++) {
        isEqual = fGMTOffsetPatterns[i] == tzfmt->fGMTOffsetPatterns[i];
    }
    for (int32_t i = 0; i < 10 && isEqual; i++) {
        isEqual = fGMTOffsetDigits[i] == tzfmt->fGMTOffsetDigits[i];
    }
    // TODO
    // Check fTimeZoneGenericNames. For now,
    // if fTimeZoneNames is same, fTimeZoneGenericNames should
    // be also equivalent.
    return isEqual;
}

TimeZoneFormat*
TimeZoneFormat::clone() const {
    return new TimeZoneFormat(*this);
}

TimeZoneFormat* U_EXPORT2
TimeZoneFormat::createInstance(const Locale& locale, UErrorCode& status) {
    TimeZoneFormat* tzfmt = new TimeZoneFormat(locale, status);
    if (U_SUCCESS(status)) {
        return tzfmt;
    }
    delete tzfmt;
    return nullptr;
}

// ------------------------------------------------------------------
// Setter and Getter

const TimeZoneNames*
TimeZoneFormat::getTimeZoneNames() const {
    return (const TimeZoneNames*)fTimeZoneNames;
}

void
TimeZoneFormat::adoptTimeZoneNames(TimeZoneNames *tznames) {
    delete fTimeZoneNames;
    fTimeZoneNames = tznames;

    // TODO - We should also update fTimeZoneGenericNames
}

void
TimeZoneFormat::setTimeZoneNames(const TimeZoneNames &tznames) {
    delete fTimeZoneNames;
    fTimeZoneNames = tznames.clone();

    // TODO - We should also update fTimeZoneGenericNames
}

void
TimeZoneFormat::setDefaultParseOptions(uint32_t flags) {
    fDefParseOptionFlags = flags;
}

uint32_t
TimeZoneFormat::getDefaultParseOptions() const {
    return fDefParseOptionFlags;
}


UnicodeString& 
TimeZoneFormat::getGMTPattern(UnicodeString& pattern) const {
    return pattern.setTo(fGMTPattern);
}

void
TimeZoneFormat::setGMTPattern(const UnicodeString& pattern, UErrorCode& status) {
    initGMTPattern(pattern, status);
}

UnicodeString&
TimeZoneFormat::getGMTOffsetPattern(UTimeZoneFormatGMTOffsetPatternType type, UnicodeString& pattern) const {
    return pattern.setTo(fGMTOffsetPatterns[type]);
}

void
TimeZoneFormat::setGMTOffsetPattern(UTimeZoneFormatGMTOffsetPatternType type, const UnicodeString& pattern, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }
    if (pattern == fGMTOffsetPatterns[type]) {
        // No need to reset
        return;
    }

    OffsetFields required = FIELDS_HM;
    switch (type) {
    case UTZFMT_PAT_POSITIVE_H:
    case UTZFMT_PAT_NEGATIVE_H:
        required = FIELDS_H;
        break;
    case UTZFMT_PAT_POSITIVE_HM:
    case UTZFMT_PAT_NEGATIVE_HM:
        required = FIELDS_HM;
        break;
    case UTZFMT_PAT_POSITIVE_HMS:
    case UTZFMT_PAT_NEGATIVE_HMS:
        required = FIELDS_HMS;
        break;
    default:
        UPRV_UNREACHABLE_EXIT;
    }

    UVector* patternItems = parseOffsetPattern(pattern, required, status);
    if (patternItems == nullptr) {
        return;
    }

    fGMTOffsetPatterns[type].setTo(pattern);
    delete fGMTOffsetPatternItems[type];
    fGMTOffsetPatternItems[type] = patternItems;
    checkAbuttingHoursAndMinutes();
}

UnicodeString&
TimeZoneFormat::getGMTOffsetDigits(UnicodeString& digits) const {
    digits.remove();
    for (int32_t i = 0; i < 10; i++) {
        digits.append(fGMTOffsetDigits[i]);
    }
    return digits;
}

void
TimeZoneFormat::setGMTOffsetDigits(const UnicodeString& digits, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }
    UChar32 digitArray[10];
    if (!toCodePoints(digits, digitArray, 10)) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    uprv_memcpy(fGMTOffsetDigits, digitArray, sizeof(UChar32)*10);
}

UnicodeString&
TimeZoneFormat::getGMTZeroFormat(UnicodeString& gmtZeroFormat) const {
    return gmtZeroFormat.setTo(fGMTZeroFormat);
}

void
TimeZoneFormat::setGMTZeroFormat(const UnicodeString& gmtZeroFormat, UErrorCode& status) {
    if (U_SUCCESS(status)) {
        if (gmtZeroFormat.isEmpty()) {
            status = U_ILLEGAL_ARGUMENT_ERROR;
        } else if (gmtZeroFormat != fGMTZeroFormat) {
            fGMTZeroFormat.setTo(gmtZeroFormat);
        }
    }
}

// ------------------------------------------------------------------
// Format and Parse

UnicodeString&
TimeZoneFormat::format(UTimeZoneFormatStyle style, const TimeZone& tz, UDate date,
        UnicodeString& name, UTimeZoneFormatTimeType* timeType /* = nullptr */) const {
    if (timeType) {
        *timeType = UTZFMT_TIME_TYPE_UNKNOWN;
    }

    UBool noOffsetFormatFallback = false;

    switch (style) {
    case UTZFMT_STYLE_GENERIC_LOCATION:
        formatGeneric(tz, UTZGNM_LOCATION, date, name);
        break;
    case UTZFMT_STYLE_GENERIC_LONG:
        formatGeneric(tz, UTZGNM_LONG, date, name);
        break;
    case UTZFMT_STYLE_GENERIC_SHORT:
        formatGeneric(tz, UTZGNM_SHORT, date, name);
        break;
    case UTZFMT_STYLE_SPECIFIC_LONG:
        formatSpecific(tz, UTZNM_LONG_STANDARD, UTZNM_LONG_DAYLIGHT, date, name, timeType);
        break;
    case UTZFMT_STYLE_SPECIFIC_SHORT:
        formatSpecific(tz, UTZNM_SHORT_STANDARD, UTZNM_SHORT_DAYLIGHT, date, name, timeType);
        break;

    case UTZFMT_STYLE_ZONE_ID:
        tz.getID(name);
        noOffsetFormatFallback = true;
        break;
    case UTZFMT_STYLE_ZONE_ID_SHORT:
        {
            const char16_t* shortID = ZoneMeta::getShortID(tz);
            if (shortID == nullptr) {
                shortID = UNKNOWN_SHORT_ZONE_ID;
            }
            name.setTo(shortID, -1);
        }
        noOffsetFormatFallback = true;
        break;

    case UTZFMT_STYLE_EXEMPLAR_LOCATION:
        formatExemplarLocation(tz, name);
        noOffsetFormatFallback = true;
        break;

    default:
        // will be handled below
        break;
    }

    if (name.isEmpty() && !noOffsetFormatFallback) {
        UErrorCode status = U_ZERO_ERROR;
        int32_t rawOffset, dstOffset;
        tz.getOffset(date, false, rawOffset, dstOffset, status);
        int32_t offset = rawOffset + dstOffset;
        if (U_SUCCESS(status)) {
            switch (style) {
            case UTZFMT_STYLE_GENERIC_LOCATION:
            case UTZFMT_STYLE_GENERIC_LONG:
            case UTZFMT_STYLE_SPECIFIC_LONG:
            case UTZFMT_STYLE_LOCALIZED_GMT:
                formatOffsetLocalizedGMT(offset, name, status);
                break;

            case UTZFMT_STYLE_GENERIC_SHORT:
            case UTZFMT_STYLE_SPECIFIC_SHORT:
            case UTZFMT_STYLE_LOCALIZED_GMT_SHORT:
                formatOffsetShortLocalizedGMT(offset, name, status);
                break;

            case UTZFMT_STYLE_ISO_BASIC_SHORT:
                formatOffsetISO8601Basic(offset, true, true, true, name, status);
                break;

            case UTZFMT_STYLE_ISO_BASIC_LOCAL_SHORT:
                formatOffsetISO8601Basic(offset, false, true, true, name, status);
                break;

            case UTZFMT_STYLE_ISO_BASIC_FIXED:
                formatOffsetISO8601Basic(offset, true, false, true, name, status);
                break;

            case UTZFMT_STYLE_ISO_BASIC_LOCAL_FIXED:
                formatOffsetISO8601Basic(offset, false, false, true, name, status);
                break;

            case UTZFMT_STYLE_ISO_EXTENDED_FIXED:
                formatOffsetISO8601Extended(offset, true, false, true, name, status);
                break;

            case UTZFMT_STYLE_ISO_EXTENDED_LOCAL_FIXED:
                formatOffsetISO8601Extended(offset, false, false, true, name, status);
                break;

            case UTZFMT_STYLE_ISO_BASIC_FULL:
                formatOffsetISO8601Basic(offset, true, false, false, name, status);
                break;

            case UTZFMT_STYLE_ISO_BASIC_LOCAL_FULL:
                formatOffsetISO8601Basic(offset, false, false, false, name, status);
                break;

            case UTZFMT_STYLE_ISO_EXTENDED_FULL:
                formatOffsetISO8601Extended(offset, true, false, false, name, status);
                break;

            case UTZFMT_STYLE_ISO_EXTENDED_LOCAL_FULL:
                formatOffsetISO8601Extended(offset, false, false, false, name, status);
                break;

            default:
              // UTZFMT_STYLE_ZONE_ID, UTZFMT_STYLE_ZONE_ID_SHORT, UTZFMT_STYLE_EXEMPLAR_LOCATION
              break;
            }

            if (timeType) {
                *timeType = (dstOffset != 0) ? UTZFMT_TIME_TYPE_DAYLIGHT : UTZFMT_TIME_TYPE_STANDARD;
            }
        }
    }

    return name;
}

UnicodeString&
TimeZoneFormat::format(const Formattable& obj, UnicodeString& appendTo,
        FieldPosition& pos, UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return appendTo;
    }
    UDate date = Calendar::getNow();
    if (obj.getType() == Formattable::kObject) {
        const UObject* formatObj = obj.getObject();
        const TimeZone* tz = dynamic_cast<const TimeZone*>(formatObj);
        if (tz == nullptr) {
            const Calendar* cal = dynamic_cast<const Calendar*>(formatObj);
            if (cal != nullptr) {
                tz = &cal->getTimeZone();
                date = cal->getTime(status);
            }
        }
        if (tz != nullptr) {
            int32_t rawOffset, dstOffset;
            tz->getOffset(date, false, rawOffset, dstOffset, status);
            char16_t buf[ZONE_NAME_U16_MAX];
            UnicodeString result(buf, 0, UPRV_LENGTHOF(buf));
            formatOffsetLocalizedGMT(rawOffset + dstOffset, result, status);
            if (U_SUCCESS(status)) {
                appendTo.append(result);
                if (pos.getField() == UDAT_TIMEZONE_FIELD) {
                    pos.setBeginIndex(0);
                    pos.setEndIndex(result.length());
                }
            }
        }
    }
    return appendTo;
}

TimeZone*
TimeZoneFormat::parse(UTimeZoneFormatStyle style, const UnicodeString& text, ParsePosition& pos,
        UTimeZoneFormatTimeType* timeType /*= nullptr*/) const {
    return parse(style, text, pos, getDefaultParseOptions(), timeType);
}

TimeZone*
TimeZoneFormat::parse(UTimeZoneFormatStyle style, const UnicodeString& text, ParsePosition& pos,
        int32_t parseOptions, UTimeZoneFormatTimeType* timeType /* = nullptr */) const {
    if (timeType) {
        *timeType = UTZFMT_TIME_TYPE_UNKNOWN;
    }

    int32_t startIdx = pos.getIndex();
    int32_t maxPos = text.length();
    int32_t offset;

    // Styles using localized GMT format as fallback
    UBool fallbackLocalizedGMT = 
        (style == UTZFMT_STYLE_SPECIFIC_LONG || style == UTZFMT_STYLE_GENERIC_LONG || style == UTZFMT_STYLE_GENERIC_LOCATION);
    UBool fallbackShortLocalizedGMT =
        (style == UTZFMT_STYLE_SPECIFIC_SHORT || style == UTZFMT_STYLE_GENERIC_SHORT);

    int32_t evaluated = 0;  // bit flags representing already evaluated styles
    ParsePosition tmpPos(startIdx);

    int32_t parsedOffset = UNKNOWN_OFFSET;  // stores successfully parsed offset for later use
    int32_t parsedPos = -1;                 // stores successfully parsed offset position for later use

    // Try localized GMT format first if necessary
    if (fallbackLocalizedGMT || fallbackShortLocalizedGMT) {
        UBool hasDigitOffset = false;
        offset = parseOffsetLocalizedGMT(text, tmpPos, fallbackShortLocalizedGMT, &hasDigitOffset);
        if (tmpPos.getErrorIndex() == -1) {
            // Even when the input text was successfully parsed as a localized GMT format text,
            // we may still need to evaluate the specified style if -
            //   1) GMT zero format was used, and
            //   2) The input text was not completely processed
            if (tmpPos.getIndex() == maxPos || hasDigitOffset) {
                pos.setIndex(tmpPos.getIndex());
                return createTimeZoneForOffset(offset);
            }
            parsedOffset = offset;
            parsedPos = tmpPos.getIndex();
        }
        // Note: For now, no distinction between long/short localized GMT format in the parser.
        // This might be changed in future.
        // evaluated |= (fallbackLocalizedGMT ? STYLE_PARSE_FLAGS[UTZFMT_STYLE_LOCALIZED_GMT] : STYLE_PARSE_FLAGS[UTZFMT_STYLE_LOCALIZED_GMT_SHORT]);
        evaluated |= STYLE_PARSE_FLAGS[UTZFMT_STYLE_LOCALIZED_GMT] | STYLE_PARSE_FLAGS[UTZFMT_STYLE_LOCALIZED_GMT_SHORT];
    }

    UErrorCode status = U_ZERO_ERROR;
    char16_t tzIDBuf[32];
    UnicodeString tzID(tzIDBuf, 0, UPRV_LENGTHOF(tzIDBuf));

    UBool parseTZDBAbbrev = ((parseOptions & UTZFMT_PARSE_OPTION_TZ_DATABASE_ABBREVIATIONS) != 0);

    // Try the specified style
    switch (style) {
    case UTZFMT_STYLE_LOCALIZED_GMT:
        {
            tmpPos.setIndex(startIdx);
            tmpPos.setErrorIndex(-1);

            offset = parseOffsetLocalizedGMT(text, tmpPos);
            if (tmpPos.getErrorIndex() == -1) {
                pos.setIndex(tmpPos.getIndex());
                return createTimeZoneForOffset(offset);
            }

            // Note: For now, no distinction between long/short localized GMT format in the parser.
            // This might be changed in future.
            evaluated |= STYLE_PARSE_FLAGS[UTZFMT_STYLE_LOCALIZED_GMT_SHORT];

            break;
        }
    case UTZFMT_STYLE_LOCALIZED_GMT_SHORT:
        {
            tmpPos.setIndex(startIdx);
            tmpPos.setErrorIndex(-1);

            offset = parseOffsetShortLocalizedGMT(text, tmpPos);
            if (tmpPos.getErrorIndex() == -1) {
                pos.setIndex(tmpPos.getIndex());
                return createTimeZoneForOffset(offset);
            }

            // Note: For now, no distinction between long/short localized GMT format in the parser.
            // This might be changed in future.
            evaluated |= STYLE_PARSE_FLAGS[UTZFMT_STYLE_LOCALIZED_GMT];

            break;
        }
    case UTZFMT_STYLE_ISO_BASIC_SHORT:
    case UTZFMT_STYLE_ISO_BASIC_FIXED:
    case UTZFMT_STYLE_ISO_BASIC_FULL:
    case UTZFMT_STYLE_ISO_EXTENDED_FIXED:
    case UTZFMT_STYLE_ISO_EXTENDED_FULL:
        {
            tmpPos.setIndex(startIdx);
            tmpPos.setErrorIndex(-1);

            offset = parseOffsetISO8601(text, tmpPos);
            if (tmpPos.getErrorIndex() == -1) {
                pos.setIndex(tmpPos.getIndex());
                return createTimeZoneForOffset(offset);
            }

            break;
        }

    case UTZFMT_STYLE_ISO_BASIC_LOCAL_SHORT:
    case UTZFMT_STYLE_ISO_BASIC_LOCAL_FIXED:
    case UTZFMT_STYLE_ISO_BASIC_LOCAL_FULL:
    case UTZFMT_STYLE_ISO_EXTENDED_LOCAL_FIXED:
    case UTZFMT_STYLE_ISO_EXTENDED_LOCAL_FULL:
        {
            tmpPos.setIndex(startIdx);
            tmpPos.setErrorIndex(-1);

            // Exclude the case of UTC Indicator "Z" here
            UBool hasDigitOffset = false;
            offset = parseOffsetISO8601(text, tmpPos, false, &hasDigitOffset);
            if (tmpPos.getErrorIndex() == -1 && hasDigitOffset) {
                pos.setIndex(tmpPos.getIndex());
                return createTimeZoneForOffset(offset);
            }

            break;
        }

    case UTZFMT_STYLE_SPECIFIC_LONG:
    case UTZFMT_STYLE_SPECIFIC_SHORT:
        {
            // Specific styles
            int32_t nameTypes = 0;
            if (style == UTZFMT_STYLE_SPECIFIC_LONG) {
                nameTypes = (UTZNM_LONG_STANDARD | UTZNM_LONG_DAYLIGHT);
            } else {
                U_ASSERT(style == UTZFMT_STYLE_SPECIFIC_SHORT);
                nameTypes = (UTZNM_SHORT_STANDARD | UTZNM_SHORT_DAYLIGHT);
            }
            LocalPointer<TimeZoneNames::MatchInfoCollection> specificMatches(fTimeZoneNames->find(text, startIdx, nameTypes, status));
            if (U_FAILURE(status)) {
                pos.setErrorIndex(startIdx);
                return nullptr;
            }
            if (!specificMatches.isNull()) {
                int32_t matchIdx = -1;
                int32_t matchPos = -1;
                for (int32_t i = 0; i < specificMatches->size(); i++) {
                    matchPos  = startIdx + specificMatches->getMatchLengthAt(i);
                    if (matchPos > parsedPos) {
                        matchIdx = i;
                        parsedPos = matchPos;
                    }
                }
                if (matchIdx >= 0) {
                    if (timeType) {
                        *timeType = getTimeType(specificMatches->getNameTypeAt(matchIdx));
                    }
                    pos.setIndex(matchPos);
                    getTimeZoneID(specificMatches.getAlias(), matchIdx, tzID);
                    U_ASSERT(!tzID.isEmpty());
                    return TimeZone::createTimeZone(tzID);
                }
            }

            if (parseTZDBAbbrev && style == UTZFMT_STYLE_SPECIFIC_SHORT) {
                U_ASSERT((nameTypes & UTZNM_SHORT_STANDARD) != 0);
                U_ASSERT((nameTypes & UTZNM_SHORT_DAYLIGHT) != 0);

                const TZDBTimeZoneNames *tzdbTimeZoneNames = getTZDBTimeZoneNames(status);
                if (U_SUCCESS(status)) {
                    LocalPointer<TimeZoneNames::MatchInfoCollection> tzdbNameMatches(
                        tzdbTimeZoneNames->find(text, startIdx, nameTypes, status));
                    if (U_FAILURE(status)) {
                        pos.setErrorIndex(startIdx);
                        return nullptr;
                    }
                    if (!tzdbNameMatches.isNull()) {
                        int32_t matchIdx = -1;
                        int32_t matchPos = -1;
                        for (int32_t i = 0; i < tzdbNameMatches->size(); i++) {
                            matchPos = startIdx + tzdbNameMatches->getMatchLengthAt(i);
                            if (matchPos > parsedPos) {
                                matchIdx = i;
                                parsedPos = matchPos;
                            }
                        }
                        if (matchIdx >= 0) {
                            if (timeType) {
                                *timeType = getTimeType(tzdbNameMatches->getNameTypeAt(matchIdx));
                            }
                            pos.setIndex(matchPos);
                            getTimeZoneID(tzdbNameMatches.getAlias(), matchIdx, tzID);
                            U_ASSERT(!tzID.isEmpty());
                            return TimeZone::createTimeZone(tzID);
                        }
                    }
                }
            }
            break;
        }
    case UTZFMT_STYLE_GENERIC_LONG:
    case UTZFMT_STYLE_GENERIC_SHORT:
    case UTZFMT_STYLE_GENERIC_LOCATION:
        {
            int32_t genericNameTypes = 0;
            switch (style) {
            case UTZFMT_STYLE_GENERIC_LOCATION:
                genericNameTypes = UTZGNM_LOCATION;
                break;

            case UTZFMT_STYLE_GENERIC_LONG:
                genericNameTypes = UTZGNM_LONG | UTZGNM_LOCATION;
                break;

            case UTZFMT_STYLE_GENERIC_SHORT:
                genericNameTypes = UTZGNM_SHORT | UTZGNM_LOCATION;
                break;

            default:
                UPRV_UNREACHABLE_EXIT;
            }

            int32_t len = 0;
            UTimeZoneFormatTimeType tt = UTZFMT_TIME_TYPE_UNKNOWN;
            const TimeZoneGenericNames *gnames = getTimeZoneGenericNames(status);
            if (U_SUCCESS(status)) {
                len = gnames->findBestMatch(text, startIdx, genericNameTypes, tzID, tt, status);
            }
            if (U_FAILURE(status)) {
                pos.setErrorIndex(startIdx);
                return nullptr;
            }
            if (len > 0) {
                // Found a match
                if (timeType) {
                    *timeType = tt;
                }
                pos.setIndex(startIdx + len);
                U_ASSERT(!tzID.isEmpty());
                return TimeZone::createTimeZone(tzID);
            }

            break;
        }
    case UTZFMT_STYLE_ZONE_ID:
        {
            tmpPos.setIndex(startIdx);
            tmpPos.setErrorIndex(-1);

            parseZoneID(text, tmpPos, tzID);
            if (tmpPos.getErrorIndex() == -1) {
                pos.setIndex(tmpPos.getIndex());
                return TimeZone::createTimeZone(tzID);
            }
            break;
        }
    case UTZFMT_STYLE_ZONE_ID_SHORT:
        {
            tmpPos.setIndex(startIdx);
            tmpPos.setErrorIndex(-1);

            parseShortZoneID(text, tmpPos, tzID);
            if (tmpPos.getErrorIndex() == -1) {
                pos.setIndex(tmpPos.getIndex());
                return TimeZone::createTimeZone(tzID);
            }
            break;
        }
    case UTZFMT_STYLE_EXEMPLAR_LOCATION:
        {
            tmpPos.setIndex(startIdx);
            tmpPos.setErrorIndex(-1);

            parseExemplarLocation(text, tmpPos, tzID);
            if (tmpPos.getErrorIndex() == -1) {
                pos.setIndex(tmpPos.getIndex());
                return TimeZone::createTimeZone(tzID);
            }
            break;
        }
    }
    evaluated |= STYLE_PARSE_FLAGS[style];


    if (parsedPos > startIdx) {
        // When the specified style is one of SPECIFIC_XXX or GENERIC_XXX, we tried to parse the input
        // as localized GMT format earlier. If parsedOffset is positive, it means it was successfully
        // parsed as localized GMT format, but offset digits were not detected (more specifically, GMT
        // zero format). Then, it tried to find a match within the set of display names, but could not
        // find a match. At this point, we can safely assume the input text contains the localized
        // GMT format.
        U_ASSERT(parsedOffset != UNKNOWN_OFFSET);
        pos.setIndex(parsedPos);
        return createTimeZoneForOffset(parsedOffset);
    }

    // Failed to parse the input text as the time zone format in the specified style.
    // Check the longest match among other styles below.
    char16_t parsedIDBuf[32];
    UnicodeString parsedID(parsedIDBuf, 0, UPRV_LENGTHOF(parsedIDBuf));
    UTimeZoneFormatTimeType parsedTimeType = UTZFMT_TIME_TYPE_UNKNOWN;

    U_ASSERT(parsedPos < 0);
    U_ASSERT(parsedOffset == UNKNOWN_OFFSET);

    // ISO 8601
    if (parsedPos < maxPos &&
        ((evaluated & ISO_Z_STYLE_FLAG) == 0 || (evaluated & ISO_LOCAL_STYLE_FLAG) == 0)) {
        tmpPos.setIndex(startIdx);
        tmpPos.setErrorIndex(-1);

        UBool hasDigitOffset = false;
        offset = parseOffsetISO8601(text, tmpPos, false, &hasDigitOffset);
        if (tmpPos.getErrorIndex() == -1) {
            if (tmpPos.getIndex() == maxPos || hasDigitOffset) {
                pos.setIndex(tmpPos.getIndex());
                return createTimeZoneForOffset(offset);
            }
            // Note: When ISO 8601 format contains offset digits, it should not
            // collide with other formats. However, ISO 8601 UTC format "Z" (single letter)
            // may collide with other names. In this case, we need to evaluate other names.
            if (parsedPos < tmpPos.getIndex()) {
                parsedOffset = offset;
                parsedID.setToBogus();
                parsedTimeType = UTZFMT_TIME_TYPE_UNKNOWN;
                parsedPos = tmpPos.getIndex();
                U_ASSERT(parsedPos == startIdx + 1);    // only when "Z" is used
            }
        }
    }

    // Localized GMT format
    if (parsedPos < maxPos &&
        (evaluated & STYLE_PARSE_FLAGS[UTZFMT_STYLE_LOCALIZED_GMT]) == 0) {
        tmpPos.setIndex(startIdx);
        tmpPos.setErrorIndex(-1);

        UBool hasDigitOffset = false;
        offset = parseOffsetLocalizedGMT(text, tmpPos, false, &hasDigitOffset);
        if (tmpPos.getErrorIndex() == -1) {
            if (tmpPos.getIndex() == maxPos || hasDigitOffset) {
                pos.setIndex(tmpPos.getIndex());
                return createTimeZoneForOffset(offset);
            }
            // Evaluate other names - see the comment earlier in this method.
            if (parsedPos < tmpPos.getIndex()) {
                parsedOffset = offset;
                parsedID.setToBogus();
                parsedTimeType = UTZFMT_TIME_TYPE_UNKNOWN;
                parsedPos = tmpPos.getIndex();
            }
        }
    }

    if (parsedPos < maxPos &&
        (evaluated & STYLE_PARSE_FLAGS[UTZFMT_STYLE_LOCALIZED_GMT_SHORT]) == 0) {
        tmpPos.setIndex(startIdx);
        tmpPos.setErrorIndex(-1);

        UBool hasDigitOffset = false;
        offset = parseOffsetLocalizedGMT(text, tmpPos, true, &hasDigitOffset);
        if (tmpPos.getErrorIndex() == -1) {
            if (tmpPos.getIndex() == maxPos || hasDigitOffset) {
                pos.setIndex(tmpPos.getIndex());
                return createTimeZoneForOffset(offset);
            }
            // Evaluate other names - see the comment earlier in this method.
            if (parsedPos < tmpPos.getIndex()) {
                parsedOffset = offset;
                parsedID.setToBogus();
                parsedTimeType = UTZFMT_TIME_TYPE_UNKNOWN;
                parsedPos = tmpPos.getIndex();
            }
        }
    }

    // When ParseOption.ALL_STYLES is available, we also try to look all possible display names and IDs.
    // For example, when style is GENERIC_LONG, "EST" (SPECIFIC_SHORT) is never
    // used for America/New_York. With parseAllStyles true, this code parses "EST"
    // as America/New_York.

    // Note: Adding all possible names into the trie used by the implementation is quite heavy operation,
    // which we want to avoid normally (note that we cache the trie, so this is applicable to the
    // first time only as long as the cache does not expire).

    if (parseOptions & UTZFMT_PARSE_OPTION_ALL_STYLES) {
        // Try all specific names and exemplar location names
        if (parsedPos < maxPos) {
            LocalPointer<TimeZoneNames::MatchInfoCollection> specificMatches(fTimeZoneNames->find(text, startIdx, ALL_SIMPLE_NAME_TYPES, status));
            if (U_FAILURE(status)) {
                pos.setErrorIndex(startIdx);
                return nullptr;
            }
            int32_t specificMatchIdx = -1;
            int32_t matchPos = -1;
            if (!specificMatches.isNull()) {
                for (int32_t i = 0; i < specificMatches->size(); i++) {
                    if (startIdx + specificMatches->getMatchLengthAt(i) > matchPos) {
                        specificMatchIdx = i;
                        matchPos = startIdx + specificMatches->getMatchLengthAt(i);
                    }
                }
            }
            if (parsedPos < matchPos) {
                U_ASSERT(specificMatchIdx >= 0);
                parsedPos = matchPos;
                getTimeZoneID(specificMatches.getAlias(), specificMatchIdx, parsedID);
                parsedTimeType = getTimeType(specificMatches->getNameTypeAt(specificMatchIdx));
                parsedOffset = UNKNOWN_OFFSET;
            }
        }
        if (parseTZDBAbbrev && parsedPos < maxPos && (evaluated & STYLE_PARSE_FLAGS[UTZFMT_STYLE_SPECIFIC_SHORT]) == 0) {
            const TZDBTimeZoneNames *tzdbTimeZoneNames = getTZDBTimeZoneNames(status);
            if (U_SUCCESS(status)) {
                LocalPointer<TimeZoneNames::MatchInfoCollection> tzdbNameMatches(
                    tzdbTimeZoneNames->find(text, startIdx, ALL_SIMPLE_NAME_TYPES, status));
                if (U_FAILURE(status)) {
                    pos.setErrorIndex(startIdx);
                    return nullptr;
                }
                int32_t tzdbNameMatchIdx = -1;
                int32_t matchPos = -1;
                if (!tzdbNameMatches.isNull()) {
                    for (int32_t i = 0; i < tzdbNameMatches->size(); i++) {
                        if (startIdx + tzdbNameMatches->getMatchLengthAt(i) > matchPos) {
                            tzdbNameMatchIdx = i;
                            matchPos = startIdx + tzdbNameMatches->getMatchLengthAt(i);
                        }
                    }
                }
                if (parsedPos < matchPos) {
                    U_ASSERT(tzdbNameMatchIdx >= 0);
                    parsedPos = matchPos;
                    getTimeZoneID(tzdbNameMatches.getAlias(), tzdbNameMatchIdx, parsedID);
                    parsedTimeType = getTimeType(tzdbNameMatches->getNameTypeAt(tzdbNameMatchIdx));
                    parsedOffset = UNKNOWN_OFFSET;
                }
            }
        }
        // Try generic names
        if (parsedPos < maxPos) {
            int32_t genMatchLen = -1;
            UTimeZoneFormatTimeType tt = UTZFMT_TIME_TYPE_UNKNOWN;

            const TimeZoneGenericNames *gnames = getTimeZoneGenericNames(status);
            if (U_SUCCESS(status)) {
                genMatchLen = gnames->findBestMatch(text, startIdx, ALL_GENERIC_NAME_TYPES, tzID, tt, status);
            }
            if (U_FAILURE(status)) {
                pos.setErrorIndex(startIdx);
                return nullptr;
            }

            if (genMatchLen > 0 && parsedPos < startIdx + genMatchLen) {
                parsedPos = startIdx + genMatchLen;
                parsedID.setTo(tzID);
                parsedTimeType = tt;
                parsedOffset = UNKNOWN_OFFSET;
            }
        }

        // Try time zone ID
        if (parsedPos < maxPos && (evaluated & STYLE_PARSE_FLAGS[UTZFMT_STYLE_ZONE_ID]) == 0) {
            tmpPos.setIndex(startIdx);
            tmpPos.setErrorIndex(-1);

            parseZoneID(text, tmpPos, tzID);
            if (tmpPos.getErrorIndex() == -1 && parsedPos < tmpPos.getIndex()) {
                parsedPos = tmpPos.getIndex();
                parsedID.setTo(tzID);
                parsedTimeType = UTZFMT_TIME_TYPE_UNKNOWN;
                parsedOffset = UNKNOWN_OFFSET;
            }
        }
        // Try short time zone ID
        if (parsedPos < maxPos && (evaluated & STYLE_PARSE_FLAGS[UTZFMT_STYLE_ZONE_ID]) == 0) {
            tmpPos.setIndex(startIdx);
            tmpPos.setErrorIndex(-1);

            parseShortZoneID(text, tmpPos, tzID);
            if (tmpPos.getErrorIndex() == -1 && parsedPos < tmpPos.getIndex()) {
                parsedPos = tmpPos.getIndex();
                parsedID.setTo(tzID);
                parsedTimeType = UTZFMT_TIME_TYPE_UNKNOWN;
                parsedOffset = UNKNOWN_OFFSET;
            }
        }
    }

    if (parsedPos > startIdx) {
        // Parsed successfully
        TimeZone* parsedTZ;
        if (parsedID.length() > 0) {
            parsedTZ = TimeZone::createTimeZone(parsedID);
        } else {
            U_ASSERT(parsedOffset != UNKNOWN_OFFSET);
            parsedTZ = createTimeZoneForOffset(parsedOffset);
        }
        if (timeType) {
            *timeType = parsedTimeType;
        }
        pos.setIndex(parsedPos);
        return parsedTZ;
    }

    pos.setErrorIndex(startIdx);
    return nullptr;
}

void
TimeZoneFormat::parseObject(const UnicodeString& source, Formattable& result,
        ParsePosition& parse_pos) const {
    result.adoptObject(parse(UTZFMT_STYLE_GENERIC_LOCATION, source, parse_pos, UTZFMT_PARSE_OPTION_ALL_STYLES));
}


// ------------------------------------------------------------------
// Private zone name format/parse implementation

UnicodeString&
TimeZoneFormat::formatGeneric(const TimeZone& tz, int32_t genType, UDate date, UnicodeString& name) const {
    UErrorCode status = U_ZERO_ERROR;
    const TimeZoneGenericNames* gnames = getTimeZoneGenericNames(status);
    if (U_FAILURE(status)) {
        name.setToBogus();
        return name;
    }

    if (genType == UTZGNM_LOCATION) {
        const char16_t* canonicalID = ZoneMeta::getCanonicalCLDRID(tz);
        if (canonicalID == nullptr) {
            name.setToBogus();
            return name;
        }
        return gnames->getGenericLocationName(UnicodeString(true, canonicalID, -1), name);
    }
    return gnames->getDisplayName(tz, (UTimeZoneGenericNameType)genType, date, name);
}

UnicodeString&
TimeZoneFormat::formatSpecific(const TimeZone& tz, UTimeZoneNameType stdType, UTimeZoneNameType dstType,
        UDate date, UnicodeString& name, UTimeZoneFormatTimeType *timeType) const {
    if (fTimeZoneNames == nullptr) {
        name.setToBogus();
        return name;
    }

    UErrorCode status = U_ZERO_ERROR;
    UBool isDaylight = tz.inDaylightTime(date, status);
    const char16_t* canonicalID = ZoneMeta::getCanonicalCLDRID(tz);

    if (U_FAILURE(status) || canonicalID == nullptr) {
        name.setToBogus();
        return name;
    }

    if (isDaylight) {
        fTimeZoneNames->getDisplayName(UnicodeString(true, canonicalID, -1), dstType, date, name);
    } else {
        fTimeZoneNames->getDisplayName(UnicodeString(true, canonicalID, -1), stdType, date, name);
    }

    if (timeType && !name.isEmpty()) {
        *timeType = isDaylight ? UTZFMT_TIME_TYPE_DAYLIGHT : UTZFMT_TIME_TYPE_STANDARD;
    }
    return name;
}

const TimeZoneGenericNames*
TimeZoneFormat::getTimeZoneGenericNames(UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return nullptr;
    }

    umtx_lock(&gLock);
    if (fTimeZoneGenericNames == nullptr) {
        TimeZoneFormat *nonConstThis = const_cast<TimeZoneFormat *>(this);
        nonConstThis->fTimeZoneGenericNames = TimeZoneGenericNames::createInstance(fLocale, status);
    }
    umtx_unlock(&gLock);

    return fTimeZoneGenericNames;
}

const TZDBTimeZoneNames*
TimeZoneFormat::getTZDBTimeZoneNames(UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return nullptr;
    }

    umtx_lock(&gLock);
    if (fTZDBTimeZoneNames == nullptr) {
        TZDBTimeZoneNames *tzdbNames = new TZDBTimeZoneNames(fLocale);
        if (tzdbNames == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
        } else {
            TimeZoneFormat *nonConstThis = const_cast<TimeZoneFormat *>(this);
            nonConstThis->fTZDBTimeZoneNames = tzdbNames;
        }
    }
    umtx_unlock(&gLock);

    return fTZDBTimeZoneNames;
}

UnicodeString&
TimeZoneFormat::formatExemplarLocation(const TimeZone& tz, UnicodeString& name) const {
    char16_t locationBuf[ZONE_NAME_U16_MAX];
    UnicodeString location(locationBuf, 0, UPRV_LENGTHOF(locationBuf));
    const char16_t* canonicalID = ZoneMeta::getCanonicalCLDRID(tz);

    if (canonicalID) {
        fTimeZoneNames->getExemplarLocationName(UnicodeString(true, canonicalID, -1), location);
    }
    if (location.length() > 0) {
        name.setTo(location);
    } else {
        // Use "unknown" location
        fTimeZoneNames->getExemplarLocationName(UnicodeString(true, UNKNOWN_ZONE_ID, -1), location);
        if (location.length() > 0) {
            name.setTo(location);
        } else {
            // last resort
            name.setTo(UNKNOWN_LOCATION, -1);
        }
    }
    return name;
}


// ------------------------------------------------------------------
// Zone offset format and parse

UnicodeString&
TimeZoneFormat::formatOffsetISO8601Basic(int32_t offset, UBool useUtcIndicator, UBool isShort, UBool ignoreSeconds,
        UnicodeString& result, UErrorCode& status) const {
    return formatOffsetISO8601(offset, true, useUtcIndicator, isShort, ignoreSeconds, result, status);
}

UnicodeString&
TimeZoneFormat::formatOffsetISO8601Extended(int32_t offset, UBool useUtcIndicator, UBool isShort, UBool ignoreSeconds,
        UnicodeString& result, UErrorCode& status) const {
    return formatOffsetISO8601(offset, false, useUtcIndicator, isShort, ignoreSeconds, result, status);
}

UnicodeString&
TimeZoneFormat::formatOffsetLocalizedGMT(int32_t offset, UnicodeString& result, UErrorCode& status) const {
    return formatOffsetLocalizedGMT(offset, false, result, status);
}

UnicodeString&
TimeZoneFormat::formatOffsetShortLocalizedGMT(int32_t offset, UnicodeString& result, UErrorCode& status) const {
    return formatOffsetLocalizedGMT(offset, true, result, status);
}

int32_t
TimeZoneFormat::parseOffsetISO8601(const UnicodeString& text, ParsePosition& pos) const {
    return parseOffsetISO8601(text, pos, false);
}

int32_t
TimeZoneFormat::parseOffsetLocalizedGMT(const UnicodeString& text, ParsePosition& pos) const {
    return parseOffsetLocalizedGMT(text, pos, false, nullptr);
}

int32_t
TimeZoneFormat::parseOffsetShortLocalizedGMT(const UnicodeString& text, ParsePosition& pos) const {
    return parseOffsetLocalizedGMT(text, pos, true, nullptr);
}

// ------------------------------------------------------------------
// Private zone offset format/parse implementation

UnicodeString&
TimeZoneFormat::formatOffsetISO8601(int32_t offset, UBool isBasic, UBool useUtcIndicator,
        UBool isShort, UBool ignoreSeconds, UnicodeString& result, UErrorCode& status) const {
    if (U_FAILURE(status)) {
        result.setToBogus();
        return result;
    }
    int32_t absOffset = offset < 0 ? -offset : offset;
    if (useUtcIndicator && (absOffset < MILLIS_PER_SECOND || (ignoreSeconds && absOffset < MILLIS_PER_MINUTE))) {
        result.setTo(ISO8601_UTC);
        return result;
    }

    OffsetFields minFields = isShort ? FIELDS_H : FIELDS_HM;
    OffsetFields maxFields = ignoreSeconds ? FIELDS_HM : FIELDS_HMS;
    char16_t sep = isBasic ? 0 : ISO8601_SEP;

    // Note: FIELDS_HMS as maxFields is a CLDR/ICU extension. ISO 8601 specification does
    // not support seconds field.

    if (absOffset >= MAX_OFFSET) {
        result.setToBogus();
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return result;
    }

    int fields[3];
    fields[0] = absOffset / MILLIS_PER_HOUR;
    absOffset = absOffset % MILLIS_PER_HOUR;
    fields[1] = absOffset / MILLIS_PER_MINUTE;
    absOffset = absOffset % MILLIS_PER_MINUTE;
    fields[2] = absOffset / MILLIS_PER_SECOND;

    U_ASSERT(fields[0] >= 0 && fields[0] <= MAX_OFFSET_HOUR);
    U_ASSERT(fields[1] >= 0 && fields[1] <= MAX_OFFSET_MINUTE);
    U_ASSERT(fields[2] >= 0 && fields[2] <= MAX_OFFSET_SECOND);

    int32_t lastIdx = maxFields;
    while (lastIdx > minFields) {
        if (fields[lastIdx] != 0) {
            break;
        }
        lastIdx--;
    }

    char16_t sign = PLUS;
    if (offset < 0) {
        // if all output fields are 0s, do not use negative sign
        for (int32_t idx = 0; idx <= lastIdx; idx++) {
            if (fields[idx] != 0) {
                sign = MINUS;
                break;
            }
        }
    }
    result.setTo(sign);

    for (int32_t idx = 0; idx <= lastIdx; idx++) {
        if (sep && idx != 0) {
            result.append(sep);
        }
        result.append((char16_t)(0x0030 + fields[idx]/10));
        result.append((char16_t)(0x0030 + fields[idx]%10));
    }

    return result;
}

UnicodeString&
TimeZoneFormat::formatOffsetLocalizedGMT(int32_t offset, UBool isShort, UnicodeString& result, UErrorCode& status) const {
    if (U_FAILURE(status)) {
        result.setToBogus();
        return result;
    }
    if (offset <= -MAX_OFFSET || offset >= MAX_OFFSET) {
        result.setToBogus();
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return result;
    }

    if (offset == 0) {
        result.setTo(fGMTZeroFormat);
        return result;
    }

    UBool positive = true;
    if (offset < 0) {
        offset = -offset;
        positive = false;
    }

    int32_t offsetH = offset / MILLIS_PER_HOUR;
    offset = offset % MILLIS_PER_HOUR;
    int32_t offsetM = offset / MILLIS_PER_MINUTE;
    offset = offset % MILLIS_PER_MINUTE;
    int32_t offsetS = offset / MILLIS_PER_SECOND;

    U_ASSERT(offsetH <= MAX_OFFSET_HOUR && offsetM <= MAX_OFFSET_MINUTE && offsetS <= MAX_OFFSET_SECOND);

    const UVector* offsetPatternItems = nullptr;
    if (positive) {
        if (offsetS != 0) {
            offsetPatternItems = fGMTOffsetPatternItems[UTZFMT_PAT_POSITIVE_HMS];
        } else if (offsetM != 0 || !isShort) {
            offsetPatternItems = fGMTOffsetPatternItems[UTZFMT_PAT_POSITIVE_HM];
        } else {
            offsetPatternItems = fGMTOffsetPatternItems[UTZFMT_PAT_POSITIVE_H];
        }
    } else {
        if (offsetS != 0) {
            offsetPatternItems = fGMTOffsetPatternItems[UTZFMT_PAT_NEGATIVE_HMS];
        } else if (offsetM != 0 || !isShort) {
            offsetPatternItems = fGMTOffsetPatternItems[UTZFMT_PAT_NEGATIVE_HM];
        } else {
            offsetPatternItems = fGMTOffsetPatternItems[UTZFMT_PAT_NEGATIVE_H];
        }
    }

    U_ASSERT(offsetPatternItems != nullptr);

    // Building the GMT format string
    result.setTo(fGMTPatternPrefix);

    for (int32_t i = 0; i < offsetPatternItems->size(); i++) {
        const GMTOffsetField* item = (GMTOffsetField*)offsetPatternItems->elementAt(i);
        GMTOffsetField::FieldType type = item->getType();

        switch (type) {
        case GMTOffsetField::TEXT:
            result.append(item->getPatternText(), -1);
            break;

        case GMTOffsetField::HOUR:
            appendOffsetDigits(result, offsetH, (isShort ? 1 : 2));
            break;

        case GMTOffsetField::MINUTE:
            appendOffsetDigits(result, offsetM, 2);
            break;

        case GMTOffsetField::SECOND:
            appendOffsetDigits(result, offsetS, 2);
            break;
        }
    }

    result.append(fGMTPatternSuffix);
    return result;
}

int32_t
TimeZoneFormat::parseOffsetISO8601(const UnicodeString& text, ParsePosition& pos, UBool extendedOnly, UBool* hasDigitOffset /* = nullptr */) const {
    if (hasDigitOffset) {
        *hasDigitOffset = false;
    }
    int32_t start = pos.getIndex();
    if (start >= text.length()) {
        pos.setErrorIndex(start);
        return 0;
    }

    char16_t firstChar = text.charAt(start);
    if (firstChar == ISO8601_UTC || firstChar == (char16_t)(ISO8601_UTC + 0x20)) {
        // "Z" (or "z") - indicates UTC
        pos.setIndex(start + 1);
        return 0;
    }

    int32_t sign = 1;
    if (firstChar == PLUS) {
        sign = 1;
    } else if (firstChar == MINUS) {
        sign = -1;
    } else {
        // Not an ISO 8601 offset string
        pos.setErrorIndex(start);
        return 0;
    }
    ParsePosition posOffset(start + 1);
    int32_t offset = parseAsciiOffsetFields(text, posOffset, ISO8601_SEP, FIELDS_H, FIELDS_HMS);
    if (posOffset.getErrorIndex() == -1 && !extendedOnly && (posOffset.getIndex() - start <= 3)) {
        // If the text is successfully parsed as extended format with the options above, it can be also parsed
        // as basic format. For example, "0230" can be parsed as offset 2:00 (only first digits are valid for
        // extended format), but it can be parsed as offset 2:30 with basic format. We use longer result.
        ParsePosition posBasic(start + 1);
        int32_t tmpOffset = parseAbuttingAsciiOffsetFields(text, posBasic, FIELDS_H, FIELDS_HMS, false);
        if (posBasic.getErrorIndex() == -1 && posBasic.getIndex() > posOffset.getIndex()) {
            offset = tmpOffset;
            posOffset.setIndex(posBasic.getIndex());
        }
    }

    if (posOffset.getErrorIndex() != -1) {
        pos.setErrorIndex(start);
        return 0;
    }

    pos.setIndex(posOffset.getIndex());
    if (hasDigitOffset) {
        *hasDigitOffset = true;
    }
    return sign * offset;
}

int32_t
TimeZoneFormat::parseOffsetLocalizedGMT(const UnicodeString& text, ParsePosition& pos, UBool isShort, UBool* hasDigitOffset) const {
    int32_t start = pos.getIndex();
    int32_t offset = 0;
    int32_t parsedLength = 0;

    if (hasDigitOffset) {
        *hasDigitOffset = false;
    }

    offset = parseOffsetLocalizedGMTPattern(text, start, isShort, parsedLength);

    // For now, parseOffsetLocalizedGMTPattern handles both long and short
    // formats, no matter isShort is true or false. This might be changed in future
    // when strict parsing is necessary, or different set of patterns are used for
    // short/long formats.
#if 0
    if (parsedLength == 0) {
        offset = parseOffsetLocalizedGMTPattern(text, start, !isShort, parsedLength);
    }
#endif

    if (parsedLength > 0) {
        if (hasDigitOffset) {
            *hasDigitOffset = true;
        }
        pos.setIndex(start + parsedLength);
        return offset;
    }

    // Try the default patterns
    offset = parseOffsetDefaultLocalizedGMT(text, start, parsedLength);
    if (parsedLength > 0) {
        if (hasDigitOffset) {
            *hasDigitOffset = true;
        }
        pos.setIndex(start + parsedLength);
        return offset;
    }

    // Check if this is a GMT zero format
    if (text.caseCompare(start, fGMTZeroFormat.length(), fGMTZeroFormat, 0) == 0) {
        pos.setIndex(start + fGMTZeroFormat.length());
        return 0;
    }

    // Check if this is a default GMT zero format
    for (int32_t i = 0; ALT_GMT_STRINGS[i][0] != 0; i++) {
        const char16_t* defGMTZero = ALT_GMT_STRINGS[i];
        int32_t defGMTZeroLen = u_strlen(defGMTZero);
        if (text.caseCompare(start, defGMTZeroLen, defGMTZero, 0) == 0) {
            pos.setIndex(start + defGMTZeroLen);
            return 0;
        }
    }

    // Nothing matched
    pos.setErrorIndex(start);
    return 0;
}

int32_t
TimeZoneFormat::parseOffsetLocalizedGMTPattern(const UnicodeString& text, int32_t start, UBool /*isShort*/, int32_t& parsedLen) const {
    int32_t idx = start;
    int32_t offset = 0;
    UBool parsed = false;

    do {
        // Prefix part
        int32_t len = fGMTPatternPrefix.length();
        if (len > 0 && text.caseCompare(idx, len, fGMTPatternPrefix, 0) != 0) {
            // prefix match failed
            break;
        }
        idx += len;

        // Offset part
        offset = parseOffsetFields(text, idx, false, len);
        if (len == 0) {
            // offset field match failed
            break;
        }
        idx += len;

        len = fGMTPatternSuffix.length();
        if (len > 0 && text.caseCompare(idx, len, fGMTPatternSuffix, 0) != 0) {
            // no suffix match
            break;
        }
        idx += len;
        parsed = true;
    } while (false);

    parsedLen = parsed ? idx - start : 0;
    return offset;
}

int32_t
TimeZoneFormat::parseOffsetFields(const UnicodeString& text, int32_t start, UBool /*isShort*/, int32_t& parsedLen) const {
    int32_t outLen = 0;
    int32_t offset = 0;
    int32_t sign = 1;

    parsedLen = 0;

    int32_t offsetH, offsetM, offsetS;
    offsetH = offsetM = offsetS = 0;

    for (int32_t patidx = 0; PARSE_GMT_OFFSET_TYPES[patidx] >= 0; patidx++) {
        int32_t gmtPatType = PARSE_GMT_OFFSET_TYPES[patidx];
        UVector* items = fGMTOffsetPatternItems[gmtPatType];
        U_ASSERT(items != nullptr);

        outLen = parseOffsetFieldsWithPattern(text, start, items, false, offsetH, offsetM, offsetS);
        if (outLen > 0) {
            sign = (gmtPatType == UTZFMT_PAT_POSITIVE_H || gmtPatType == UTZFMT_PAT_POSITIVE_HM || gmtPatType == UTZFMT_PAT_POSITIVE_HMS) ?
                1 : -1;
            break;
        }
    }

    if (outLen > 0 && fAbuttingOffsetHoursAndMinutes) {
        // When hours field is sabutting minutes field,
        // the parse result above may not be appropriate.
        // For example, "01020" is parsed as 01:02: above,
        // but it should be parsed as 00:10:20.
        int32_t tmpLen = 0;
        int32_t tmpSign = 1;
        int32_t tmpH = 0;
        int32_t tmpM = 0;
        int32_t tmpS = 0;

        for (int32_t patidx = 0; PARSE_GMT_OFFSET_TYPES[patidx] >= 0; patidx++) {
            int32_t gmtPatType = PARSE_GMT_OFFSET_TYPES[patidx];
            UVector* items = fGMTOffsetPatternItems[gmtPatType];
            U_ASSERT(items != nullptr);

            // forcing parse to use single hour digit
            tmpLen = parseOffsetFieldsWithPattern(text, start, items, true, tmpH, tmpM, tmpS);
            if (tmpLen > 0) {
                tmpSign = (gmtPatType == UTZFMT_PAT_POSITIVE_H || gmtPatType == UTZFMT_PAT_POSITIVE_HM || gmtPatType == UTZFMT_PAT_POSITIVE_HMS) ?
                    1 : -1;
                break;
            }
        }
        if (tmpLen > outLen) {
            // Better parse result with single hour digit
            outLen = tmpLen;
            sign = tmpSign;
            offsetH = tmpH;
            offsetM = tmpM;
            offsetS = tmpS;
        }
    }

    if (outLen > 0) {
        offset = ((((offsetH * 60) + offsetM) * 60) + offsetS) * 1000 * sign;
        parsedLen = outLen;
    }

    return offset;
}

int32_t
TimeZoneFormat::parseOffsetFieldsWithPattern(const UnicodeString& text, int32_t start,
        UVector* patternItems, UBool forceSingleHourDigit, int32_t& hour, int32_t& min, int32_t& sec) const {
    UBool failed = false;
    int32_t offsetH, offsetM, offsetS;
    offsetH = offsetM = offsetS = 0;
    int32_t idx = start;

    for (int32_t i = 0; i < patternItems->size(); i++) {
        int32_t len = 0;
        const GMTOffsetField* field = (const GMTOffsetField*)patternItems->elementAt(i);
        GMTOffsetField::FieldType fieldType = field->getType();
        if (fieldType == GMTOffsetField::TEXT) {
            const char16_t* patStr = field->getPatternText();
            len = u_strlen(patStr);
            if (i == 0) {
                // When TimeZoneFormat parse() is called from SimpleDateFormat,
                // leading space characters might be truncated. If the first pattern text
                // starts with such character (e.g. Bidi control), then we need to
                // skip the leading space characters.
                if (idx < text.length() && !PatternProps::isWhiteSpace(text.char32At(idx))) {
                    while (len > 0) {
                        UChar32 ch;
                        int32_t chLen;
                        U16_GET(patStr, 0, 0, len, ch);
                        if (PatternProps::isWhiteSpace(ch)) {
                            chLen = U16_LENGTH(ch);
                            len -= chLen;
                            patStr += chLen;
                        }
                        else {
                            break;
                        }
                    }
                }
            }
            if (text.caseCompare(idx, len, patStr, 0) != 0) {
                failed = true;
                break;
            }
            idx += len;
        } else {
            if (fieldType == GMTOffsetField::HOUR) {
                uint8_t maxDigits = forceSingleHourDigit ? 1 : 2;
                offsetH = parseOffsetFieldWithLocalizedDigits(text, idx, 1, maxDigits, 0, MAX_OFFSET_HOUR, len);
            } else if (fieldType == GMTOffsetField::MINUTE) {
                offsetM = parseOffsetFieldWithLocalizedDigits(text, idx, 2, 2, 0, MAX_OFFSET_MINUTE, len);
            } else if (fieldType == GMTOffsetField::SECOND) {
                offsetS = parseOffsetFieldWithLocalizedDigits(text, idx, 2, 2, 0, MAX_OFFSET_SECOND, len);
            }

            if (len == 0) {
                failed = true;
                break;
            }
            idx += len;
        }
    }

    if (failed) {
        hour = min = sec = 0;
        return 0;
    }

    hour = offsetH;
    min = offsetM;
    sec = offsetS;

    return idx - start;
}

int32_t
TimeZoneFormat::parseAbuttingOffsetFields(const UnicodeString& text, int32_t start, int32_t& parsedLen) const {
    int32_t digits[MAX_OFFSET_DIGITS];
    int32_t parsed[MAX_OFFSET_DIGITS];  // accumulative offsets

    // Parse digits into int[]
    int32_t idx = start;
    int32_t len = 0;
    int32_t numDigits = 0;
    for (int32_t i = 0; i < MAX_OFFSET_DIGITS; i++) {
        digits[i] = parseSingleLocalizedDigit(text, idx, len);
        if (digits[i] < 0) {
            break;
        }
        idx += len;
        parsed[i] = idx - start;
        numDigits++;
    }

    if (numDigits == 0) {
        parsedLen = 0;
        return 0;
    }

    int32_t offset = 0;
    while (numDigits > 0) {
        int32_t hour = 0;
        int32_t min = 0;
        int32_t sec = 0;

        U_ASSERT(numDigits > 0 && numDigits <= MAX_OFFSET_DIGITS);
        switch (numDigits) {
        case 1: // H
            hour = digits[0];
            break;
        case 2: // HH
            hour = digits[0] * 10 + digits[1];
            break;
        case 3: // Hmm
            hour = digits[0];
            min = digits[1] * 10 + digits[2];
            break;
        case 4: // HHmm
            hour = digits[0] * 10 + digits[1];
            min = digits[2] * 10 + digits[3];
            break;
        case 5: // Hmmss
            hour = digits[0];
            min = digits[1] * 10 + digits[2];
            sec = digits[3] * 10 + digits[4];
            break;
        case 6: // HHmmss
            hour = digits[0] * 10 + digits[1];
            min = digits[2] * 10 + digits[3];
            sec = digits[4] * 10 + digits[5];
            break;
        }
        if (hour <= MAX_OFFSET_HOUR && min <= MAX_OFFSET_MINUTE && sec <= MAX_OFFSET_SECOND) {
            // found a valid combination
            offset = hour * MILLIS_PER_HOUR + min * MILLIS_PER_MINUTE + sec * MILLIS_PER_SECOND;
            parsedLen = parsed[numDigits - 1];
            break;
        }
        numDigits--;
    }
    return offset;
}

int32_t
TimeZoneFormat::parseOffsetDefaultLocalizedGMT(const UnicodeString& text, int start, int32_t& parsedLen) const {
    int32_t idx = start;
    int32_t offset = 0;
    int32_t parsed = 0;

    do {
        // check global default GMT alternatives
        int32_t gmtLen = 0;

        for (int32_t i = 0; ALT_GMT_STRINGS[i][0] != 0; i++) {
            const char16_t* gmt = ALT_GMT_STRINGS[i];
            int32_t len = u_strlen(gmt);
            if (text.caseCompare(start, len, gmt, 0) == 0) {
                gmtLen = len;
                break;
            }
        }
        if (gmtLen == 0) {
            break;
        }
        idx += gmtLen;

        // offset needs a sign char and a digit at minimum
        if (idx + 1 >= text.length()) {
            break;
        }

        // parse sign
        int32_t sign = 1;
        char16_t c = text.charAt(idx);
        if (c == PLUS) {
            sign = 1;
        } else if (c == MINUS) {
            sign = -1;
        } else {
            break;
        }
        idx++;

        // offset part
        // try the default pattern with the separator first
        int32_t lenWithSep = 0;
        int32_t offsetWithSep = parseDefaultOffsetFields(text, idx, DEFAULT_GMT_OFFSET_SEP, lenWithSep);
        if (lenWithSep == text.length() - idx) {
            // maximum match
            offset = offsetWithSep * sign;
            idx += lenWithSep;
        } else {
            // try abutting field pattern
            int32_t lenAbut = 0;
            int32_t offsetAbut = parseAbuttingOffsetFields(text, idx, lenAbut);

            if (lenWithSep > lenAbut) {
                offset = offsetWithSep * sign;
                idx += lenWithSep;
            } else {
                offset = offsetAbut * sign;
                idx += lenAbut;
            }
        }
        parsed = idx - start;
    } while (false);

    parsedLen = parsed;
    return offset;
}

int32_t
TimeZoneFormat::parseDefaultOffsetFields(const UnicodeString& text, int32_t start, char16_t separator, int32_t& parsedLen) const {
    int32_t max = text.length();
    int32_t idx = start;
    int32_t len = 0;
    int32_t hour = 0, min = 0, sec = 0;

    parsedLen = 0;

    do {
        hour = parseOffsetFieldWithLocalizedDigits(text, idx, 1, 2, 0, MAX_OFFSET_HOUR, len);
        if (len == 0) {
            break;
        }
        idx += len;

        if (idx + 1 < max && text.charAt(idx) == separator) {
            min = parseOffsetFieldWithLocalizedDigits(text, idx + 1, 2, 2, 0, MAX_OFFSET_MINUTE, len);
            if (len == 0) {
                break;
            }
            idx += (1 + len);

            if (idx + 1 < max && text.charAt(idx) == separator) {
                sec = parseOffsetFieldWithLocalizedDigits(text, idx + 1, 2, 2, 0, MAX_OFFSET_SECOND, len);
                if (len == 0) {
                    break;
                }
                idx += (1 + len);
            }
        }
    } while (false);

    if (idx == start) {
        return 0;
    }

    parsedLen = idx - start;
    return hour * MILLIS_PER_HOUR + min * MILLIS_PER_MINUTE + sec * MILLIS_PER_SECOND;
}

int32_t
TimeZoneFormat::parseOffsetFieldWithLocalizedDigits(const UnicodeString& text, int32_t start, uint8_t minDigits, uint8_t maxDigits, uint16_t minVal, uint16_t maxVal, int32_t& parsedLen) const {
    parsedLen = 0;

    int32_t decVal = 0;
    int32_t numDigits = 0;
    int32_t idx = start;
    int32_t digitLen = 0;

    while (idx < text.length() && numDigits < maxDigits) {
        int32_t digit = parseSingleLocalizedDigit(text, idx, digitLen);
        if (digit < 0) {
            break;
        }
        int32_t tmpVal = decVal * 10 + digit;
        if (tmpVal > maxVal) {
            break;
        }
        decVal = tmpVal;
        numDigits++;
        idx += digitLen;
    }

    // Note: maxVal is checked in the while loop
    if (numDigits < minDigits || decVal < minVal) {
        decVal = -1;
        numDigits = 0;
    } else {
        parsedLen = idx - start;
    }

    return decVal;
}

int32_t
TimeZoneFormat::parseSingleLocalizedDigit(const UnicodeString& text, int32_t start, int32_t& len) const {
    int32_t digit = -1;
    len = 0;
    if (start < text.length()) {
        UChar32 cp = text.char32At(start);

        // First, try digits configured for this instance
        for (int32_t i = 0; i < 10; i++) {
            if (cp == fGMTOffsetDigits[i]) {
                digit = i;
                break;
            }
        }
        // If failed, check if this is a Unicode digit
        if (digit < 0) {
            int32_t tmp = u_charDigitValue(cp);
            digit = (tmp >= 0 && tmp <= 9) ? tmp : -1;
        }

        if (digit >= 0) {
            int32_t next = text.moveIndex32(start, 1);
            len = next - start;
        }
    }
    return digit;
}

UnicodeString&
TimeZoneFormat::formatOffsetWithAsciiDigits(int32_t offset, char16_t sep, OffsetFields minFields, OffsetFields maxFields, UnicodeString& result) {
    U_ASSERT(maxFields >= minFields);
    U_ASSERT(offset > -MAX_OFFSET && offset < MAX_OFFSET);

    char16_t sign = PLUS;
    if (offset < 0) {
        sign = MINUS;
        offset = -offset;
    }
    result.setTo(sign);

    int fields[3];
    fields[0] = offset / MILLIS_PER_HOUR;
    offset = offset % MILLIS_PER_HOUR;
    fields[1] = offset / MILLIS_PER_MINUTE;
    offset = offset % MILLIS_PER_MINUTE;
    fields[2] = offset / MILLIS_PER_SECOND;

    U_ASSERT(fields[0] >= 0 && fields[0] <= MAX_OFFSET_HOUR);
    U_ASSERT(fields[1] >= 0 && fields[1] <= MAX_OFFSET_MINUTE);
    U_ASSERT(fields[2] >= 0 && fields[2] <= MAX_OFFSET_SECOND);

    int32_t lastIdx = maxFields;
    while (lastIdx > minFields) {
        if (fields[lastIdx] != 0) {
            break;
        }
        lastIdx--;
    }

    for (int32_t idx = 0; idx <= lastIdx; idx++) {
        if (sep && idx != 0) {
            result.append(sep);
        }
        result.append((char16_t)(0x0030 + fields[idx]/10));
        result.append((char16_t)(0x0030 + fields[idx]%10));
    }

    return result;
}

int32_t
TimeZoneFormat::parseAbuttingAsciiOffsetFields(const UnicodeString& text, ParsePosition& pos, OffsetFields minFields, OffsetFields maxFields, UBool fixedHourWidth) {
    int32_t start = pos.getIndex();

    int32_t minDigits = 2 * (minFields + 1) - (fixedHourWidth ? 0 : 1);
    int32_t maxDigits = 2 * (maxFields + 1);

    U_ASSERT(maxDigits <= MAX_OFFSET_DIGITS);

    int32_t digits[MAX_OFFSET_DIGITS] = {};
    int32_t numDigits = 0;
    int32_t idx = start;
    while (numDigits < maxDigits && idx < text.length()) {
        char16_t uch = text.charAt(idx);
        int32_t digit = DIGIT_VAL(uch);
        if (digit < 0) {
            break;
        }
        digits[numDigits] = digit;
        numDigits++;
        idx++;
    }

    if (fixedHourWidth && (numDigits & 1)) {
        // Fixed digits, so the number of digits must be even number. Truncating.
        numDigits--;
    }

    if (numDigits < minDigits) {
        pos.setErrorIndex(start);
        return 0;
    }

    int32_t hour = 0, min = 0, sec = 0;
    UBool bParsed = false;
    while (numDigits >= minDigits) {
        switch (numDigits) {
        case 1: //H
            hour = digits[0];
            break;
        case 2: //HH
            hour = digits[0] * 10 + digits[1];
            break;
        case 3: //Hmm
            hour = digits[0];
            min = digits[1] * 10 + digits[2];
            break;
        case 4: //HHmm
            hour = digits[0] * 10 + digits[1];
            min = digits[2] * 10 + digits[3];
            break;
        case 5: //Hmmss
            hour = digits[0];
            min = digits[1] * 10 + digits[2];
            sec = digits[3] * 10 + digits[4];
            break;
        case 6: //HHmmss
            hour = digits[0] * 10 + digits[1];
            min = digits[2] * 10 + digits[3];
            sec = digits[4] * 10 + digits[5];
            break;
        }

        if (hour <= MAX_OFFSET_HOUR && min <= MAX_OFFSET_MINUTE && sec <= MAX_OFFSET_SECOND) {
            // Successfully parsed
            bParsed = true;
            break;
        }

        // Truncating
        numDigits -= (fixedHourWidth ? 2 : 1);
        hour = min = sec = 0;
    }

    if (!bParsed) {
        pos.setErrorIndex(start);
        return 0;
    }
    pos.setIndex(start + numDigits);
    return ((((hour * 60) + min) * 60) + sec) * 1000;
}

int32_t
TimeZoneFormat::parseAsciiOffsetFields(const UnicodeString& text, ParsePosition& pos, char16_t sep, OffsetFields minFields, OffsetFields maxFields) {
    int32_t start = pos.getIndex();
    int32_t fieldVal[] = {0, 0, 0};
    int32_t fieldLen[] = {0, -1, -1};
    for (int32_t idx = start, fieldIdx = 0; idx < text.length() && fieldIdx <= maxFields; idx++) {
        char16_t c = text.charAt(idx);
        if (c == sep) {
            if (fieldIdx == 0) {
                if (fieldLen[0] == 0) {
                    // no hours field
                    break;
                }
                // 1 digit hour, move to next field
            } else {
                if (fieldLen[fieldIdx] != -1) {
                    // premature minute or seconds field
                    break;
                }
                fieldLen[fieldIdx] = 0;
            }
            continue;
        } else if (fieldLen[fieldIdx] == -1) {
            // no separator after 2 digit field
            break;
        }
        int32_t digit = DIGIT_VAL(c);
        if (digit < 0) {
            // not a digit
            break;
        }
        fieldVal[fieldIdx] = fieldVal[fieldIdx] * 10 + digit;
        fieldLen[fieldIdx]++;
        if (fieldLen[fieldIdx] >= 2) {
            // parsed 2 digits, move to next field
            fieldIdx++;
        }
    }

    int32_t offset = 0;
    int32_t parsedLen = 0;
    int32_t parsedFields = -1;
    do {
        // hour
        if (fieldLen[0] == 0) {
            break;
        }
        if (fieldVal[0] > MAX_OFFSET_HOUR) {
            offset = (fieldVal[0] / 10) * MILLIS_PER_HOUR;
            parsedFields = FIELDS_H;
            parsedLen = 1;
            break;
        }
        offset = fieldVal[0] * MILLIS_PER_HOUR;
        parsedLen = fieldLen[0];
        parsedFields = FIELDS_H;

        // minute
        if (fieldLen[1] != 2 || fieldVal[1] > MAX_OFFSET_MINUTE) {
            break;
        }
        offset += fieldVal[1] * MILLIS_PER_MINUTE;
        parsedLen += (1 + fieldLen[1]);
        parsedFields = FIELDS_HM;

        // second
        if (fieldLen[2] != 2 || fieldVal[2] > MAX_OFFSET_SECOND) {
            break;
        }
        offset += fieldVal[2] * MILLIS_PER_SECOND;
        parsedLen += (1 + fieldLen[2]);
        parsedFields = FIELDS_HMS;
    } while (false);

    if (parsedFields < minFields) {
        pos.setErrorIndex(start);
        return 0;
    }

    pos.setIndex(start + parsedLen);
    return offset;
}

void
TimeZoneFormat::appendOffsetDigits(UnicodeString& buf, int32_t n, uint8_t minDigits) const {
    U_ASSERT(n >= 0 && n < 60);
    int32_t numDigits = n >= 10 ? 2 : 1;
    for (int32_t i = 0; i < minDigits - numDigits; i++) {
        buf.append(fGMTOffsetDigits[0]);
    }
    if (numDigits == 2) {
        buf.append(fGMTOffsetDigits[n / 10]);
    }
    buf.append(fGMTOffsetDigits[n % 10]);
}

// ------------------------------------------------------------------
// Private misc
void
TimeZoneFormat::initGMTPattern(const UnicodeString& gmtPattern, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }
    // This implementation not perfect, but sufficient practically.
    int32_t idx = gmtPattern.indexOf(ARG0, ARG0_LEN, 0);
    if (idx < 0) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    fGMTPattern.setTo(gmtPattern);
    unquote(gmtPattern.tempSubString(0, idx), fGMTPatternPrefix);
    unquote(gmtPattern.tempSubString(idx + ARG0_LEN), fGMTPatternSuffix);
}

UnicodeString&
TimeZoneFormat::unquote(const UnicodeString& pattern, UnicodeString& result) {
    if (pattern.indexOf(SINGLEQUOTE) < 0) {
        result.setTo(pattern);
        return result;
    }
    result.remove();
    UBool isPrevQuote = false;
    UBool inQuote = false;
    for (int32_t i = 0; i < pattern.length(); i++) {
        char16_t c = pattern.charAt(i);
        if (c == SINGLEQUOTE) {
            if (isPrevQuote) {
                result.append(c);
                isPrevQuote = false;
            } else {
                isPrevQuote = true;
            }
            inQuote = !inQuote;
        } else {
            isPrevQuote = false;
            result.append(c);
        }
    }
    return result;
}

UVector*
TimeZoneFormat::parseOffsetPattern(const UnicodeString& pattern, OffsetFields required, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    UVector* result = new UVector(deleteGMTOffsetField, nullptr, status);
    if (result == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }

    int32_t checkBits = 0;
    UBool isPrevQuote = false;
    UBool inQuote = false;
    char16_t textBuf[32];
    UnicodeString text(textBuf, 0, UPRV_LENGTHOF(textBuf));
    GMTOffsetField::FieldType itemType = GMTOffsetField::TEXT;
    int32_t itemLength = 1;

    for (int32_t i = 0; i < pattern.length(); i++) {
        char16_t ch = pattern.charAt(i);
        if (ch == SINGLEQUOTE) {
            if (isPrevQuote) {
                text.append(SINGLEQUOTE);
                isPrevQuote = false;
            } else {
                isPrevQuote = true;
                if (itemType != GMTOffsetField::TEXT) {
                    if (GMTOffsetField::isValid(itemType, itemLength)) {
                        GMTOffsetField* fld = GMTOffsetField::createTimeField(itemType, static_cast<uint8_t>(itemLength), status);
                        result->adoptElement(fld, status);
                        if (U_FAILURE(status)) {
                            break;
                        }
                    } else {
                        status = U_ILLEGAL_ARGUMENT_ERROR;
                        break;
                    }
                    itemType = GMTOffsetField::TEXT;
                }
            }
            inQuote = !inQuote;
        } else {
            isPrevQuote = false;
            if (inQuote) {
                text.append(ch);
            } else {
                GMTOffsetField::FieldType tmpType = GMTOffsetField::getTypeByLetter(ch);
                if (tmpType != GMTOffsetField::TEXT) {
                    // an offset time pattern character
                    if (tmpType == itemType) {
                        itemLength++;
                    } else {
                        if (itemType == GMTOffsetField::TEXT) {
                            if (text.length() > 0) {
                                GMTOffsetField* textfld = GMTOffsetField::createText(text, status);
                                result->adoptElement(textfld, status);
                                if (U_FAILURE(status)) {
                                    break;
                                }
                                text.remove();
                            }
                        } else {
                            if (GMTOffsetField::isValid(itemType, itemLength)) {
                                GMTOffsetField* fld = GMTOffsetField::createTimeField(itemType, static_cast<uint8_t>(itemLength), status);
                                result->adoptElement(fld, status);
                                if (U_FAILURE(status)) {
                                    break;
                                }
                            } else {
                                status = U_ILLEGAL_ARGUMENT_ERROR;
                                break;
                            }
                        }
                        itemType = tmpType;
                        itemLength = 1;
                        checkBits |= tmpType;
                    }
                } else {
                    // a string literal
                    if (itemType != GMTOffsetField::TEXT) {
                        if (GMTOffsetField::isValid(itemType, itemLength)) {
                            GMTOffsetField* fld = GMTOffsetField::createTimeField(itemType, static_cast<uint8_t>(itemLength), status);
                            result->adoptElement(fld, status);
                            if (U_FAILURE(status)) {
                                break;
                            }
                        } else {
                            status = U_ILLEGAL_ARGUMENT_ERROR;
                            break;
                        }
                        itemType = GMTOffsetField::TEXT;
                    }
                    text.append(ch);
                }
            }
        }
    }
    // handle last item
    if (U_SUCCESS(status)) {
        if (itemType == GMTOffsetField::TEXT) {
            if (text.length() > 0) {
                GMTOffsetField* tfld = GMTOffsetField::createText(text, status);
                result->adoptElement(tfld, status);
            }
        } else {
            if (GMTOffsetField::isValid(itemType, itemLength)) {
                GMTOffsetField* fld = GMTOffsetField::createTimeField(itemType, static_cast<uint8_t>(itemLength), status);
                result->adoptElement(fld, status);
            } else {
                status = U_ILLEGAL_ARGUMENT_ERROR;
            }
        }

        // Check all required fields are set
        if (U_SUCCESS(status)) {
            int32_t reqBits = 0;
            switch (required) {
            case FIELDS_H:
                reqBits = GMTOffsetField::HOUR;
                break;
            case FIELDS_HM:
                reqBits = GMTOffsetField::HOUR | GMTOffsetField::MINUTE;
                break;
            case FIELDS_HMS:
                reqBits = GMTOffsetField::HOUR | GMTOffsetField::MINUTE | GMTOffsetField::SECOND;
                break;
            }
            if (checkBits == reqBits) {
                // all required fields are set, no extra fields
                return result;
            }
        }
    }

    // error
    delete result;
    return nullptr;
}

UnicodeString&
TimeZoneFormat::expandOffsetPattern(const UnicodeString& offsetHM, UnicodeString& result, UErrorCode& status) {
    result.setToBogus();
    if (U_FAILURE(status)) {
        return result;
    }
    U_ASSERT(u_strlen(DEFAULT_GMT_OFFSET_MINUTE_PATTERN) == 2);

    int32_t idx_mm = offsetHM.indexOf(DEFAULT_GMT_OFFSET_MINUTE_PATTERN, 2, 0);
    if (idx_mm < 0) {
        // Bad time zone hour pattern data
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return result;
    }

    UnicodeString sep;
    int32_t idx_H = offsetHM.tempSubString(0, idx_mm).lastIndexOf((char16_t)0x0048 /* H */);
    if (idx_H >= 0) {
        sep = offsetHM.tempSubString(idx_H + 1, idx_mm - (idx_H + 1));
    }
    result.setTo(offsetHM.tempSubString(0, idx_mm + 2));
    result.append(sep);
    result.append(DEFAULT_GMT_OFFSET_SECOND_PATTERN, -1);
    result.append(offsetHM.tempSubString(idx_mm + 2));
    return result;
}

UnicodeString&
TimeZoneFormat::truncateOffsetPattern(const UnicodeString& offsetHM, UnicodeString& result, UErrorCode& status) {
    result.setToBogus();
    if (U_FAILURE(status)) {
        return result;
    }
    U_ASSERT(u_strlen(DEFAULT_GMT_OFFSET_MINUTE_PATTERN) == 2);

    int32_t idx_mm = offsetHM.indexOf(DEFAULT_GMT_OFFSET_MINUTE_PATTERN, 2, 0);
    if (idx_mm < 0) {
        // Bad time zone hour pattern data
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return result;
    }
    char16_t HH[] = {0x0048, 0x0048};
    int32_t idx_HH = offsetHM.tempSubString(0, idx_mm).lastIndexOf(HH, 2, 0);
    if (idx_HH >= 0) {
        return result.setTo(offsetHM.tempSubString(0, idx_HH + 2));
    }
    int32_t idx_H = offsetHM.tempSubString(0, idx_mm).lastIndexOf((char16_t)0x0048, 0);
    if (idx_H >= 0) {
        return result.setTo(offsetHM.tempSubString(0, idx_H + 1));
    }
    // Bad time zone hour pattern data
    status = U_ILLEGAL_ARGUMENT_ERROR;
    return result;
}

void
TimeZoneFormat::initGMTOffsetPatterns(UErrorCode& status) {
    for (int32_t type = 0; type < UTZFMT_PAT_COUNT; type++) {
        switch (type) {
        case UTZFMT_PAT_POSITIVE_H:
        case UTZFMT_PAT_NEGATIVE_H:
            fGMTOffsetPatternItems[type] = parseOffsetPattern(fGMTOffsetPatterns[type], FIELDS_H, status);
            break;
        case UTZFMT_PAT_POSITIVE_HM:
        case UTZFMT_PAT_NEGATIVE_HM:
            fGMTOffsetPatternItems[type] = parseOffsetPattern(fGMTOffsetPatterns[type], FIELDS_HM, status);
            break;
        case UTZFMT_PAT_POSITIVE_HMS:
        case UTZFMT_PAT_NEGATIVE_HMS:
            fGMTOffsetPatternItems[type] = parseOffsetPattern(fGMTOffsetPatterns[type], FIELDS_HMS, status);
            break;
        }
    }
    if (U_FAILURE(status)) {
        return;
    }
    checkAbuttingHoursAndMinutes();
}

void
TimeZoneFormat::checkAbuttingHoursAndMinutes() {
    fAbuttingOffsetHoursAndMinutes= false;
    for (int32_t type = 0; type < UTZFMT_PAT_COUNT; type++) {
        UBool afterH = false;
        UVector *items = fGMTOffsetPatternItems[type];
        for (int32_t i = 0; i < items->size(); i++) {
            const GMTOffsetField* item = (GMTOffsetField*)items->elementAt(i);
            GMTOffsetField::FieldType fieldType = item->getType();
            if (fieldType != GMTOffsetField::TEXT) {
                if (afterH) {
                    fAbuttingOffsetHoursAndMinutes = true;
                    break;
                } else if (fieldType == GMTOffsetField::HOUR) {
                    afterH = true;
                }
            } else if (afterH) {
                break;
            }
        }
        if (fAbuttingOffsetHoursAndMinutes) {
            break;
        }
    }
}

UBool
TimeZoneFormat::toCodePoints(const UnicodeString& str, UChar32* codeArray, int32_t size) {
    int32_t count = str.countChar32();
    if (count != size) {
        return false;
    }

    for (int32_t idx = 0, start = 0; idx < size; idx++) {
        codeArray[idx] = str.char32At(start);
        start = str.moveIndex32(start, 1);
    }

    return true;
}

TimeZone*
TimeZoneFormat::createTimeZoneForOffset(int32_t offset) const {
    if (offset == 0) {
        // when offset is 0, we should use "Etc/GMT"
        return TimeZone::createTimeZone(UnicodeString(true, TZID_GMT, -1));
    }
    return ZoneMeta::createCustomTimeZone(offset);
}

UTimeZoneFormatTimeType
TimeZoneFormat::getTimeType(UTimeZoneNameType nameType) {
    switch (nameType) {
    case UTZNM_LONG_STANDARD:
    case UTZNM_SHORT_STANDARD:
        return UTZFMT_TIME_TYPE_STANDARD;

    case UTZNM_LONG_DAYLIGHT:
    case UTZNM_SHORT_DAYLIGHT:
        return UTZFMT_TIME_TYPE_DAYLIGHT;

    default:
        return UTZFMT_TIME_TYPE_UNKNOWN;
    }
}

UnicodeString&
TimeZoneFormat::getTimeZoneID(const TimeZoneNames::MatchInfoCollection* matches, int32_t idx, UnicodeString& tzID) const {
    if (!matches->getTimeZoneIDAt(idx, tzID)) {
        char16_t mzIDBuf[32];
        UnicodeString mzID(mzIDBuf, 0, UPRV_LENGTHOF(mzIDBuf));
        if (matches->getMetaZoneIDAt(idx, mzID)) {
            fTimeZoneNames->getReferenceZoneID(mzID, fTargetRegion, tzID);
        }
    }
    return tzID;
}


class ZoneIdMatchHandler : public TextTrieMapSearchResultHandler {
public:
    ZoneIdMatchHandler();
    virtual ~ZoneIdMatchHandler();

    UBool handleMatch(int32_t matchLength, const CharacterNode *node, UErrorCode &status) override;
    const char16_t* getID();
    int32_t getMatchLen();
private:
    int32_t fLen;
    const char16_t* fID;
};

ZoneIdMatchHandler::ZoneIdMatchHandler() 
: fLen(0), fID(nullptr) {
}

ZoneIdMatchHandler::~ZoneIdMatchHandler() {
}

UBool
ZoneIdMatchHandler::handleMatch(int32_t matchLength, const CharacterNode *node, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return false;
    }
    if (node->hasValues()) {
        const char16_t* id = (const char16_t*)node->getValue(0);
        if (id != nullptr) {
            if (fLen < matchLength) {
                fID = id;
                fLen = matchLength;
            }
        }
    }
    return true;
}

const char16_t*
ZoneIdMatchHandler::getID() {
    return fID;
}

int32_t
ZoneIdMatchHandler::getMatchLen() {
    return fLen;
}


static void U_CALLCONV initZoneIdTrie(UErrorCode &status) {
    U_ASSERT(gZoneIdTrie == nullptr);
    ucln_i18n_registerCleanup(UCLN_I18N_TIMEZONEFORMAT, tzfmt_cleanup);
    gZoneIdTrie = new TextTrieMap(true, nullptr);    // No deleter, because values are pooled by ZoneMeta
    if (gZoneIdTrie == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    StringEnumeration *tzenum = TimeZone::createEnumeration(status);
    if (U_SUCCESS(status)) {
        const UnicodeString *id;
        while ((id = tzenum->snext(status)) != nullptr) {
            const char16_t* uid = ZoneMeta::findTimeZoneID(*id);
            if (uid) {
                gZoneIdTrie->put(uid, const_cast<char16_t *>(uid), status);
            }
        }
        delete tzenum;
    }
}


UnicodeString&
TimeZoneFormat::parseZoneID(const UnicodeString& text, ParsePosition& pos, UnicodeString& tzID) const {
    UErrorCode status = U_ZERO_ERROR;
    umtx_initOnce(gZoneIdTrieInitOnce, &initZoneIdTrie, status);

    int32_t start = pos.getIndex();
    int32_t len = 0;
    tzID.setToBogus();

    if (U_SUCCESS(status)) {
        LocalPointer<ZoneIdMatchHandler> handler(new ZoneIdMatchHandler());
        gZoneIdTrie->search(text, start, handler.getAlias(), status); 
        len = handler->getMatchLen();
        if (len > 0) {
            tzID.setTo(handler->getID(), -1);
        }
    }

    if (len > 0) {
        pos.setIndex(start + len);
    } else {
        pos.setErrorIndex(start);
    }

    return tzID;
}

static void U_CALLCONV initShortZoneIdTrie(UErrorCode &status) {
    U_ASSERT(gShortZoneIdTrie == nullptr);
    ucln_i18n_registerCleanup(UCLN_I18N_TIMEZONEFORMAT, tzfmt_cleanup);
    StringEnumeration *tzenum = TimeZone::createTimeZoneIDEnumeration(UCAL_ZONE_TYPE_CANONICAL, nullptr, nullptr, status);
    if (U_SUCCESS(status)) {
        gShortZoneIdTrie = new TextTrieMap(true, nullptr);    // No deleter, because values are pooled by ZoneMeta
        if (gShortZoneIdTrie == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
        } else {
            const UnicodeString *id;
            while ((id = tzenum->snext(status)) != nullptr) {
                const char16_t* uID = ZoneMeta::findTimeZoneID(*id);
                const char16_t* shortID = ZoneMeta::getShortID(*id);
                if (shortID && uID) {
                    gShortZoneIdTrie->put(shortID, const_cast<char16_t *>(uID), status);
                }
            }
        }
    }
    delete tzenum;
}


UnicodeString&
TimeZoneFormat::parseShortZoneID(const UnicodeString& text, ParsePosition& pos, UnicodeString& tzID) const {
    UErrorCode status = U_ZERO_ERROR;
    umtx_initOnce(gShortZoneIdTrieInitOnce, &initShortZoneIdTrie, status);

    int32_t start = pos.getIndex();
    int32_t len = 0;
    tzID.setToBogus();

    if (U_SUCCESS(status)) {
        LocalPointer<ZoneIdMatchHandler> handler(new ZoneIdMatchHandler());
        gShortZoneIdTrie->search(text, start, handler.getAlias(), status); 
        len = handler->getMatchLen();
        if (len > 0) {
            tzID.setTo(handler->getID(), -1);
        }
    }

    if (len > 0) {
        pos.setIndex(start + len);
    } else {
        pos.setErrorIndex(start);
    }

    return tzID;
}


UnicodeString&
TimeZoneFormat::parseExemplarLocation(const UnicodeString& text, ParsePosition& pos, UnicodeString& tzID) const {
    int32_t startIdx = pos.getIndex();
    int32_t parsedPos = -1;
    tzID.setToBogus();

    UErrorCode status = U_ZERO_ERROR;
    LocalPointer<TimeZoneNames::MatchInfoCollection> exemplarMatches(fTimeZoneNames->find(text, startIdx, UTZNM_EXEMPLAR_LOCATION, status));
    if (U_FAILURE(status)) {
        pos.setErrorIndex(startIdx);
        return tzID;
    }
    int32_t matchIdx = -1;
    if (!exemplarMatches.isNull()) {
        for (int32_t i = 0; i < exemplarMatches->size(); i++) {
            if (startIdx + exemplarMatches->getMatchLengthAt(i) > parsedPos) {
                matchIdx = i;
                parsedPos = startIdx + exemplarMatches->getMatchLengthAt(i);
            }
        }
        if (parsedPos > 0) {
            pos.setIndex(parsedPos);
            getTimeZoneID(exemplarMatches.getAlias(), matchIdx, tzID);
        }
    }

    if (tzID.length() == 0) {
        pos.setErrorIndex(startIdx);
    }

    return tzID;
}

U_NAMESPACE_END

#endif
