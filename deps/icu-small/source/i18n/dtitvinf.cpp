// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*******************************************************************************
* Copyright (C) 2008-2016, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*
* File DTITVINF.CPP
*
*******************************************************************************
*/

#include "unicode/dtitvinf.h"


#if !UCONFIG_NO_FORMATTING

//TODO: define it in compiler time
//#define DTITVINF_DEBUG 1


#ifdef DTITVINF_DEBUG
#include <iostream>
#endif

#include "cmemory.h"
#include "cstring.h"
#include "unicode/msgfmt.h"
#include "unicode/uloc.h"
#include "unicode/ures.h"
#include "dtitv_impl.h"
#include "charstr.h"
#include "hash.h"
#include "gregoimp.h"
#include "uresimp.h"
#include "hash.h"
#include "gregoimp.h"
#include "ulocimp.h"
#include "uresimp.h"


U_NAMESPACE_BEGIN


#ifdef DTITVINF_DEBUG
#define PRINTMESG(msg) UPRV_BLOCK_MACRO_BEGIN { \
    std::cout << "(" << __FILE__ << ":" << __LINE__ << ") " << msg << "\n"; \
} UPRV_BLOCK_MACRO_END
#endif

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(DateIntervalInfo)

static const char gCalendarTag[]="calendar";
static const char gGregorianTag[]="gregorian";
static const char gIntervalDateTimePatternTag[]="intervalFormats";
static const char gFallbackPatternTag[]="fallback";

// {0}
static const char16_t gFirstPattern[] = {LEFT_CURLY_BRACKET, DIGIT_ZERO, RIGHT_CURLY_BRACKET};
// {1}
static const char16_t gSecondPattern[] = {LEFT_CURLY_BRACKET, DIGIT_ONE, RIGHT_CURLY_BRACKET};

// default fall-back
static const char16_t gDefaultFallbackPattern[] = {LEFT_CURLY_BRACKET, DIGIT_ZERO, RIGHT_CURLY_BRACKET, SPACE, EN_DASH, SPACE, LEFT_CURLY_BRACKET, DIGIT_ONE, RIGHT_CURLY_BRACKET, 0};

DateIntervalInfo::DateIntervalInfo(UErrorCode& status)
:   fFallbackIntervalPattern(gDefaultFallbackPattern),
    fFirstDateInPtnIsLaterDate(false),
    fIntervalPatterns(nullptr)
{
    fIntervalPatterns = initHash(status);
}



DateIntervalInfo::DateIntervalInfo(const Locale& locale, UErrorCode& status)
:   fFallbackIntervalPattern(gDefaultFallbackPattern),
    fFirstDateInPtnIsLaterDate(false),
    fIntervalPatterns(nullptr)
{
    initializeData(locale, status);
}



void
DateIntervalInfo::setIntervalPattern(const UnicodeString& skeleton,
                                     UCalendarDateFields lrgDiffCalUnit,
                                     const UnicodeString& intervalPattern,
                                     UErrorCode& status) {

    if ( lrgDiffCalUnit == UCAL_HOUR_OF_DAY ) {
        setIntervalPatternInternally(skeleton, UCAL_AM_PM, intervalPattern, status);
        setIntervalPatternInternally(skeleton, UCAL_HOUR, intervalPattern, status);
    } else if ( lrgDiffCalUnit == UCAL_DAY_OF_MONTH ||
                lrgDiffCalUnit == UCAL_DAY_OF_WEEK ) {
        setIntervalPatternInternally(skeleton, UCAL_DATE, intervalPattern, status);
    } else {
        setIntervalPatternInternally(skeleton, lrgDiffCalUnit, intervalPattern, status);
    }
}


void
DateIntervalInfo::setFallbackIntervalPattern(
                                    const UnicodeString& fallbackPattern,
                                    UErrorCode& status) {
    if ( U_FAILURE(status) ) {
        return;
    }
    int32_t firstPatternIndex = fallbackPattern.indexOf(gFirstPattern,
                        UPRV_LENGTHOF(gFirstPattern), 0);
    int32_t secondPatternIndex = fallbackPattern.indexOf(gSecondPattern,
                        UPRV_LENGTHOF(gSecondPattern), 0);
    if ( firstPatternIndex == -1 || secondPatternIndex == -1 ) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    if ( firstPatternIndex > secondPatternIndex ) {
        fFirstDateInPtnIsLaterDate = true;
    }
    fFallbackIntervalPattern = fallbackPattern;
}



DateIntervalInfo::DateIntervalInfo(const DateIntervalInfo& dtitvinf)
:   UObject(dtitvinf),
    fIntervalPatterns(nullptr)
{
    *this = dtitvinf;
}



DateIntervalInfo&
DateIntervalInfo::operator=(const DateIntervalInfo& dtitvinf) {
    if ( this == &dtitvinf ) {
        return *this;
    }

    UErrorCode status = U_ZERO_ERROR;
    deleteHash(fIntervalPatterns);
    fIntervalPatterns = initHash(status);
    copyHash(dtitvinf.fIntervalPatterns, fIntervalPatterns, status);
    if ( U_FAILURE(status) ) {
        return *this;
    }

    fFallbackIntervalPattern = dtitvinf.fFallbackIntervalPattern;
    fFirstDateInPtnIsLaterDate = dtitvinf.fFirstDateInPtnIsLaterDate;
    return *this;
}


DateIntervalInfo*
DateIntervalInfo::clone() const {
    return new DateIntervalInfo(*this);
}


DateIntervalInfo::~DateIntervalInfo() {
    deleteHash(fIntervalPatterns);
    fIntervalPatterns = nullptr;
}


bool
DateIntervalInfo::operator==(const DateIntervalInfo& other) const {
    bool equal = (
      fFallbackIntervalPattern == other.fFallbackIntervalPattern &&
      fFirstDateInPtnIsLaterDate == other.fFirstDateInPtnIsLaterDate );

    if ( equal ) {
        equal = fIntervalPatterns->equals(*(other.fIntervalPatterns));
    }

    return equal;
}


UnicodeString&
DateIntervalInfo::getIntervalPattern(const UnicodeString& skeleton,
                                     UCalendarDateFields field,
                                     UnicodeString& result,
                                     UErrorCode& status) const {
    if ( U_FAILURE(status) ) {
        return result;
    }

    const UnicodeString* patternsOfOneSkeleton = (UnicodeString*) fIntervalPatterns->get(skeleton);
    if ( patternsOfOneSkeleton != nullptr ) {
        IntervalPatternIndex index = calendarFieldToIntervalIndex(field, status);
        if ( U_FAILURE(status) ) {
            return result;
        }
        const UnicodeString& intervalPattern =  patternsOfOneSkeleton[index];
        if ( !intervalPattern.isEmpty() ) {
            result = intervalPattern;
        }
    }
    return result;
}


UBool
DateIntervalInfo::getDefaultOrder() const {
    return fFirstDateInPtnIsLaterDate;
}


UnicodeString&
DateIntervalInfo::getFallbackIntervalPattern(UnicodeString& result) const {
    result = fFallbackIntervalPattern;
    return result;
}

#define ULOC_LOCALE_IDENTIFIER_CAPACITY (ULOC_FULLNAME_CAPACITY + 1 + ULOC_KEYWORD_AND_VALUES_CAPACITY)


static const int32_t PATH_PREFIX_LENGTH = 17;
static const char16_t PATH_PREFIX[] = {SOLIDUS, CAP_L, CAP_O, CAP_C, CAP_A, CAP_L, CAP_E, SOLIDUS,
                                    LOW_C, LOW_A, LOW_L, LOW_E, LOW_N, LOW_D, LOW_A, LOW_R, SOLIDUS};
static const int32_t PATH_SUFFIX_LENGTH = 16;
static const char16_t PATH_SUFFIX[] = {SOLIDUS, LOW_I, LOW_N, LOW_T, LOW_E, LOW_R, LOW_V, LOW_A,
                                    LOW_L, CAP_F, LOW_O, LOW_R, LOW_M, LOW_A, LOW_T, LOW_S};

/**
 * Sink for enumerating all of the date interval skeletons.
 */
struct DateIntervalInfo::DateIntervalSink : public ResourceSink {

    // Output data
    DateIntervalInfo &dateIntervalInfo;

    // Next calendar type
    UnicodeString nextCalendarType;

    DateIntervalSink(DateIntervalInfo &diInfo, const char *currentCalendarType)
            : dateIntervalInfo(diInfo), nextCalendarType(currentCalendarType, -1, US_INV) { }
    virtual ~DateIntervalSink();

    virtual void put(const char *key, ResourceValue &value, UBool /*noFallback*/, UErrorCode &errorCode) override {
        if (U_FAILURE(errorCode)) { return; }

        // Iterate over all the calendar entries and only pick the 'intervalFormats' table.
        ResourceTable dateIntervalData = value.getTable(errorCode);
        if (U_FAILURE(errorCode)) { return; }
        for (int32_t i = 0; dateIntervalData.getKeyAndValue(i, key, value); i++) {
            if (uprv_strcmp(key, gIntervalDateTimePatternTag) != 0) {
                continue;
            }

            // Handle aliases and tables. Ignore the rest.
            if (value.getType() == URES_ALIAS) {
                // Get the calendar type for the alias path.
                const UnicodeString &aliasPath = value.getAliasUnicodeString(errorCode);
                if (U_FAILURE(errorCode)) { return; }

                nextCalendarType.remove();
                getCalendarTypeFromPath(aliasPath, nextCalendarType, errorCode);

                if (U_FAILURE(errorCode)) {
                    resetNextCalendarType();
                }
                break;

            } else if (value.getType() == URES_TABLE) {
                // Iterate over all the skeletons in the 'intervalFormat' table.
                ResourceTable skeletonData = value.getTable(errorCode);
                if (U_FAILURE(errorCode)) { return; }
                for (int32_t j = 0; skeletonData.getKeyAndValue(j, key, value); j++) {
                    if (value.getType() == URES_TABLE) {
                        // Process the skeleton
                        processSkeletonTable(key, value, errorCode);
                        if (U_FAILURE(errorCode)) { return; }
                    }
                }
                break;
            }
        }
    }

    /**
     * Processes the patterns for a skeleton table
     */
    void processSkeletonTable(const char *key, ResourceValue &value, UErrorCode &errorCode) {
        if (U_FAILURE(errorCode)) { return; }

        // Iterate over all the patterns in the current skeleton table
        const char *currentSkeleton = key;
        ResourceTable patternData = value.getTable(errorCode);
        if (U_FAILURE(errorCode)) { return; }
        for (int32_t k = 0; patternData.getKeyAndValue(k, key, value); k++) {
            if (value.getType() == URES_STRING) {
                // Process the key
                UCalendarDateFields calendarField = validateAndProcessPatternLetter(key);

                // If the calendar field has a valid value
                if (calendarField < UCAL_FIELD_COUNT) {
                    // Set the interval pattern
                    setIntervalPatternIfAbsent(currentSkeleton, calendarField, value, errorCode);
                    if (U_FAILURE(errorCode)) { return; }
                }
            }
        }
    }

    /**
     * Extracts the calendar type from the path.
     */
    static void getCalendarTypeFromPath(const UnicodeString &path, UnicodeString &calendarType,
                                        UErrorCode &errorCode) {
        if (U_FAILURE(errorCode)) { return; }

        if (!path.startsWith(PATH_PREFIX, PATH_PREFIX_LENGTH) || !path.endsWith(PATH_SUFFIX, PATH_SUFFIX_LENGTH)) {
            errorCode = U_INVALID_FORMAT_ERROR;
            return;
        }

        path.extractBetween(PATH_PREFIX_LENGTH, path.length() - PATH_SUFFIX_LENGTH, calendarType);
    }

    /**
     * Validates and processes the pattern letter
     */
    UCalendarDateFields validateAndProcessPatternLetter(const char *patternLetter) {
        // Check that patternLetter is just one letter
        char c0;
        if ((c0 = patternLetter[0]) != 0 && patternLetter[1] == 0) {
            // Check that the pattern letter is accepted
            if (c0 == 'G') {
                return UCAL_ERA;
            } else if (c0 == 'y') {
                return UCAL_YEAR;
            } else if (c0 == 'M') {
                return UCAL_MONTH;
            } else if (c0 == 'd') {
                return UCAL_DATE;
            } else if (c0 == 'a') {
                return UCAL_AM_PM;
            } else if (c0 == 'B') {
                // TODO: Using AM/PM as a proxy for flexible day period isn't really correct, but it's close
                return UCAL_AM_PM;
            } else if (c0 == 'h' || c0 == 'H') {
                return UCAL_HOUR;
            } else if (c0 == 'm') {
                return UCAL_MINUTE;
            }// TODO(ticket:12190): Why icu4c doesn't accept the calendar field "s" but icu4j does?
        }
        return UCAL_FIELD_COUNT;
    }

    /**
     * Stores the interval pattern for the current skeleton in the internal data structure
     * if it's not present.
     */
    void setIntervalPatternIfAbsent(const char *currentSkeleton, UCalendarDateFields lrgDiffCalUnit,
                                    const ResourceValue &value, UErrorCode &errorCode) {
        // Check if the pattern has already been stored on the data structure
        IntervalPatternIndex index =
            dateIntervalInfo.calendarFieldToIntervalIndex(lrgDiffCalUnit, errorCode);
        if (U_FAILURE(errorCode)) { return; }

        UnicodeString skeleton(currentSkeleton, -1, US_INV);
        UnicodeString* patternsOfOneSkeleton =
            (UnicodeString*)(dateIntervalInfo.fIntervalPatterns->get(skeleton));

        if (patternsOfOneSkeleton == nullptr || patternsOfOneSkeleton[index].isEmpty()) {
            UnicodeString pattern = value.getUnicodeString(errorCode);
            dateIntervalInfo.setIntervalPatternInternally(skeleton, lrgDiffCalUnit,
                                                          pattern, errorCode);
        }
    }

    const UnicodeString &getNextCalendarType() {
        return nextCalendarType;
    }

    void resetNextCalendarType() {
        nextCalendarType.setToBogus();
    }
};

// Virtual destructors must be defined out of line.
DateIntervalInfo::DateIntervalSink::~DateIntervalSink() {}



void
DateIntervalInfo::initializeData(const Locale& locale, UErrorCode& status)
{
    fIntervalPatterns = initHash(status);
    if (U_FAILURE(status)) {
      return;
    }
    const char *locName = locale.getName();

    // Get the correct calendar type
    const char * calendarTypeToUse = gGregorianTag; // initial default
    char         localeWithCalendarKey[ULOC_LOCALE_IDENTIFIER_CAPACITY];
    // obtain a locale that always has the calendar key value that should be used
    (void)ures_getFunctionalEquivalent(localeWithCalendarKey, ULOC_LOCALE_IDENTIFIER_CAPACITY, nullptr,
                                     "calendar", "calendar", locName, nullptr, false, &status);
    localeWithCalendarKey[ULOC_LOCALE_IDENTIFIER_CAPACITY-1] = 0; // ensure null termination
    // now get the calendar key value from that locale
    CharString calendarType = ulocimp_getKeywordValue(localeWithCalendarKey, "calendar", status);
    if (U_SUCCESS(status)) {
        calendarTypeToUse = calendarType.data();
    }
    status = U_ZERO_ERROR;

    // Instantiate the resource bundles
    UResourceBundle *rb, *calBundle;
    rb = ures_open(nullptr, locName, &status);
    if (U_FAILURE(status)) {
        return;
    }
    calBundle = ures_getByKeyWithFallback(rb, gCalendarTag, nullptr, &status);


    if (U_SUCCESS(status)) {
        UResourceBundle *calTypeBundle, *itvDtPtnResource;

        // Get the fallback pattern
        const char16_t* resStr = nullptr;
        int32_t resStrLen = 0;
        calTypeBundle = ures_getByKeyWithFallback(calBundle, calendarTypeToUse, nullptr, &status);
        itvDtPtnResource = ures_getByKeyWithFallback(calTypeBundle,
                                                     gIntervalDateTimePatternTag, nullptr, &status);
        // TODO(ICU-20400): After the fixing, we should find the "fallback" from
        // the rb directly by the path "calendar/${calendar}/intervalFormats/fallback".
        if ( U_SUCCESS(status) ) {
            resStr = ures_getStringByKeyWithFallback(itvDtPtnResource, gFallbackPatternTag,
                                                     &resStrLen, &status);
        }

        if ( U_SUCCESS(status) && (resStr != nullptr)) {
            UnicodeString pattern = UnicodeString(true, resStr, resStrLen);
            setFallbackIntervalPattern(pattern, status);
        }
        ures_close(itvDtPtnResource);
        ures_close(calTypeBundle);


        // Instantiate the sink
        DateIntervalSink sink(*this, calendarTypeToUse);
        const UnicodeString &calendarTypeToUseUString = sink.getNextCalendarType();

        // Already loaded calendar types
        Hashtable loadedCalendarTypes(false, status);

        if (U_SUCCESS(status)) {
            while (!calendarTypeToUseUString.isBogus()) {
                // Set an error when a loop is detected
                if (loadedCalendarTypes.geti(calendarTypeToUseUString) == 1) {
                    status = U_INVALID_FORMAT_ERROR;
                    break;
                }

                // Register the calendar type to avoid loops
                loadedCalendarTypes.puti(calendarTypeToUseUString, 1, status);
                if (U_FAILURE(status)) { break; }

                // Get the calendar string
                CharString calTypeBuffer;
                calTypeBuffer.appendInvariantChars(calendarTypeToUseUString, status);
                if (U_FAILURE(status)) { break; }
                const char *calType = calTypeBuffer.data();

                // Reset the next calendar type to load.
                sink.resetNextCalendarType();

                // Get all resources for this calendar type
                ures_getAllItemsWithFallback(calBundle, calType, sink, status);
            }
        }
    }

    // Close the opened resource bundles
    ures_close(calBundle);
    ures_close(rb);
}

void
DateIntervalInfo::setIntervalPatternInternally(const UnicodeString& skeleton,
                                      UCalendarDateFields lrgDiffCalUnit,
                                      const UnicodeString& intervalPattern,
                                      UErrorCode& status) {
    IntervalPatternIndex index = calendarFieldToIntervalIndex(lrgDiffCalUnit,status);
    if ( U_FAILURE(status) ) {
        return;
    }
    UnicodeString* patternsOfOneSkeleton = (UnicodeString*)(fIntervalPatterns->get(skeleton));
    UBool emptyHash = false;
    if ( patternsOfOneSkeleton == nullptr ) {
        patternsOfOneSkeleton = new UnicodeString[kIPI_MAX_INDEX];
        if (patternsOfOneSkeleton == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
        emptyHash = true;
    }

    patternsOfOneSkeleton[index] = intervalPattern;
    if ( emptyHash ) {
        fIntervalPatterns->put(skeleton, patternsOfOneSkeleton, status);
    }
}



void
DateIntervalInfo::parseSkeleton(const UnicodeString& skeleton,
                                int32_t* skeletonFieldWidth) {
    const int8_t PATTERN_CHAR_BASE = 0x41;
    int32_t i;
    for ( i = 0; i < skeleton.length(); ++i ) {
        // it is an ASCII char in skeleton
        int8_t ch = (int8_t)skeleton.charAt(i);
        ++skeletonFieldWidth[ch - PATTERN_CHAR_BASE];
    }
}



UBool
DateIntervalInfo::stringNumeric(int32_t fieldWidth, int32_t anotherFieldWidth,
                                char patternLetter) {
    if ( patternLetter == 'M' ) {
        if ( (fieldWidth <= 2 && anotherFieldWidth > 2) ||
             (fieldWidth > 2 && anotherFieldWidth <= 2 )) {
            return true;
        }
    }
    return false;
}



const UnicodeString*
DateIntervalInfo::getBestSkeleton(const UnicodeString& skeleton,
                                  int8_t& bestMatchDistanceInfo) const {
#ifdef DTITVINF_DEBUG
    char result[1000];
    char result_1[1000];
    char mesg[2000];
    skeleton.extract(0,  skeleton.length(), result, "UTF-8");
    snprintf(mesg, sizeof(mesg), "in getBestSkeleton: skeleton: %s; \n", result);
    PRINTMESG(mesg)
#endif


    int32_t inputSkeletonFieldWidth[] =
    {
    //       A   B   C   D   E   F   G   H   I   J   K   L   M   N   O
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    //   P   Q   R   S   T   U   V   W   X   Y   Z
         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0, 0,  0, 0, 0,
    //       a   b   c   d   e   f   g   h   i   j   k   l   m   n   o
         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    //   p   q   r   s   t   u   v   w   x   y   z
         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0
    };

    int32_t skeletonFieldWidth[] =
    {
    //       A   B   C   D   E   F   G   H   I   J   K   L   M   N   O
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    //   P   Q   R   S   T   U   V   W   X   Y   Z
         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0, 0,  0, 0, 0,
    //       a   b   c   d   e   f   g   h   i   j   k   l   m   n   o
         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    //   p   q   r   s   t   u   v   w   x   y   z
         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0
    };

    const int32_t DIFFERENT_FIELD = 0x1000;
    const int32_t STRING_NUMERIC_DIFFERENCE = 0x100;
    const int32_t BASE = 0x41;

    // hack for certain alternate characters
    // resource bundles only have time skeletons containing 'v', 'h', and 'H'
    // but not time skeletons containing 'z', 'K', or 'k'
    // the skeleton may also include 'a' or 'b', which never occur in the resource bundles, so strip them out too
    UBool replacedAlternateChars = false;
    const UnicodeString* inputSkeleton = &skeleton;
    UnicodeString copySkeleton;
    if ( skeleton.indexOf(LOW_Z) != -1 || skeleton.indexOf(LOW_K) != -1 || skeleton.indexOf(CAP_K) != -1 || skeleton.indexOf(LOW_A) != -1 || skeleton.indexOf(LOW_B) != -1 ) {
        copySkeleton = skeleton;
        copySkeleton.findAndReplace(UnicodeString(LOW_Z), UnicodeString(LOW_V));
        copySkeleton.findAndReplace(UnicodeString(LOW_K), UnicodeString(CAP_H));
        copySkeleton.findAndReplace(UnicodeString(CAP_K), UnicodeString(LOW_H));
        copySkeleton.findAndReplace(UnicodeString(LOW_A), UnicodeString());
        copySkeleton.findAndReplace(UnicodeString(LOW_B), UnicodeString());
        inputSkeleton = &copySkeleton;
        replacedAlternateChars = true;
    }

    parseSkeleton(*inputSkeleton, inputSkeletonFieldWidth);
    int32_t bestDistance = MAX_POSITIVE_INT;
    const UnicodeString* bestSkeleton = nullptr;

    // 0 means exact the same skeletons;
    // 1 means having the same field, but with different length,
    // 2 means only z/v, h/K, or H/k differs
    // -1 means having different field.
    bestMatchDistanceInfo = 0;
    int8_t fieldLength = UPRV_LENGTHOF(skeletonFieldWidth);

    int32_t pos = UHASH_FIRST;
    const UHashElement* elem = nullptr;
    while ( (elem = fIntervalPatterns->nextElement(pos)) != nullptr ) {
        const UHashTok keyTok = elem->key;
        UnicodeString* newSkeleton = (UnicodeString*)keyTok.pointer;
#ifdef DTITVINF_DEBUG
    skeleton->extract(0,  skeleton->length(), result, "UTF-8");
    snprintf(mesg, sizeof(mesg), "available skeletons: skeleton: %s; \n", result);
    PRINTMESG(mesg)
#endif

        // clear skeleton field width
        int8_t i;
        for ( i = 0; i < fieldLength; ++i ) {
            skeletonFieldWidth[i] = 0;
        }
        parseSkeleton(*newSkeleton, skeletonFieldWidth);
        // calculate distance
        int32_t distance = 0;
        int8_t fieldDifference = 1;
        for ( i = 0; i < fieldLength; ++i ) {
            int32_t inputFieldWidth = inputSkeletonFieldWidth[i];
            int32_t fieldWidth = skeletonFieldWidth[i];
            if ( inputFieldWidth == fieldWidth ) {
                continue;
            }
            if ( inputFieldWidth == 0 ) {
                fieldDifference = -1;
                distance += DIFFERENT_FIELD;
            } else if ( fieldWidth == 0 ) {
                fieldDifference = -1;
                distance += DIFFERENT_FIELD;
            } else if (stringNumeric(inputFieldWidth, fieldWidth,
                                     (char)(i+BASE) ) ) {
                distance += STRING_NUMERIC_DIFFERENCE;
            } else {
                distance += (inputFieldWidth > fieldWidth) ?
                            (inputFieldWidth - fieldWidth) :
                            (fieldWidth - inputFieldWidth);
            }
        }
        if ( distance < bestDistance ) {
            bestSkeleton = newSkeleton;
            bestDistance = distance;
            bestMatchDistanceInfo = fieldDifference;
        }
        if ( distance == 0 ) {
            bestMatchDistanceInfo = 0;
            break;
        }
    }
    if ( replacedAlternateChars && bestMatchDistanceInfo != -1 ) {
        bestMatchDistanceInfo = 2;
    }
    return bestSkeleton;
}



DateIntervalInfo::IntervalPatternIndex
DateIntervalInfo::calendarFieldToIntervalIndex(UCalendarDateFields field,
                                               UErrorCode& status) {
    if ( U_FAILURE(status) ) {
        return kIPI_MAX_INDEX;
    }
    IntervalPatternIndex index = kIPI_MAX_INDEX;
    switch ( field ) {
      case UCAL_ERA:
        index = kIPI_ERA;
        break;
      case UCAL_YEAR:
        index = kIPI_YEAR;
        break;
      case UCAL_MONTH:
        index = kIPI_MONTH;
        break;
      case UCAL_DATE:
      case UCAL_DAY_OF_WEEK:
      //case UCAL_DAY_OF_MONTH:
        index = kIPI_DATE;
        break;
      case UCAL_AM_PM:
        index = kIPI_AM_PM;
        break;
      case UCAL_HOUR:
      case UCAL_HOUR_OF_DAY:
        index = kIPI_HOUR;
        break;
      case UCAL_MINUTE:
        index = kIPI_MINUTE;
        break;
      case UCAL_SECOND:
        index = kIPI_SECOND;
        break;
      case UCAL_MILLISECOND:
        index = kIPI_MILLISECOND;
        break;
      default:
        status = U_ILLEGAL_ARGUMENT_ERROR;
    }
    return index;
}



void
DateIntervalInfo::deleteHash(Hashtable* hTable)
{
    if ( hTable == nullptr ) {
        return;
    }
    int32_t pos = UHASH_FIRST;
    const UHashElement* element = nullptr;
    while ( (element = hTable->nextElement(pos)) != nullptr ) {
        const UHashTok valueTok = element->value;
        const UnicodeString* value = (UnicodeString*)valueTok.pointer;
        delete[] value;
    }
    delete fIntervalPatterns;
}


U_CDECL_BEGIN

/**
 * set hash table value comparator
 *
 * @param val1  one value in comparison
 * @param val2  the other value in comparison
 * @return      true if 2 values are the same, false otherwise
 */
static UBool U_CALLCONV dtitvinfHashTableValueComparator(UHashTok val1, UHashTok val2);

static UBool
U_CALLCONV dtitvinfHashTableValueComparator(UHashTok val1, UHashTok val2) {
    const UnicodeString* pattern1 = (UnicodeString*)val1.pointer;
    const UnicodeString* pattern2 = (UnicodeString*)val2.pointer;
    UBool ret = true;
    int8_t i;
    for ( i = 0; i < DateIntervalInfo::kMaxIntervalPatternIndex && ret ; ++i ) {
        ret = (pattern1[i] == pattern2[i]);
    }
    return ret;
}

U_CDECL_END


Hashtable*
DateIntervalInfo::initHash(UErrorCode& status) {
    if ( U_FAILURE(status) ) {
        return nullptr;
    }
    Hashtable* hTable;
    if ( (hTable = new Hashtable(false, status)) == nullptr ) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    if ( U_FAILURE(status) ) {
        delete hTable;
        return nullptr;
    }
    hTable->setValueComparator(dtitvinfHashTableValueComparator);
    return hTable;
}


void
DateIntervalInfo::copyHash(const Hashtable* source,
                           Hashtable* target,
                           UErrorCode& status) {
    if ( U_FAILURE(status) ) {
        return;
    }
    int32_t pos = UHASH_FIRST;
    const UHashElement* element = nullptr;
    if ( source ) {
        while ( (element = source->nextElement(pos)) != nullptr ) {
            const UHashTok keyTok = element->key;
            const UnicodeString* key = (UnicodeString*)keyTok.pointer;
            const UHashTok valueTok = element->value;
            const UnicodeString* value = (UnicodeString*)valueTok.pointer;
            UnicodeString* copy = new UnicodeString[kIPI_MAX_INDEX];
            if (copy == nullptr) {
                status = U_MEMORY_ALLOCATION_ERROR;
                return;
            }
            int8_t i;
            for ( i = 0; i < kIPI_MAX_INDEX; ++i ) {
                copy[i] = value[i];
            }
            target->put(UnicodeString(*key), copy, status);
            if ( U_FAILURE(status) ) {
                return;
            }
        }
    }
}


U_NAMESPACE_END

#endif
