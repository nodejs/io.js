// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
*   Copyright (C) 2008-2016, International Business Machines
*   Corporation and others.  All Rights Reserved.
**********************************************************************
*/

#include "unicode/utypes.h"
#include "unicode/uspoof.h"
#include "unicode/uchar.h"
#include "unicode/uniset.h"
#include "unicode/utf16.h"
#include "utrie2.h"
#include "cmemory.h"
#include "cstring.h"
#include "scriptset.h"
#include "umutex.h"
#include "udataswp.h"
#include "uassert.h"
#include "ucln_in.h"
#include "uspoof_impl.h"

#if !UCONFIG_NO_NORMALIZATION


U_NAMESPACE_BEGIN

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(SpoofImpl)

SpoofImpl::SpoofImpl(SpoofData *data, UErrorCode& status) {
    construct(status);
    fSpoofData = data;
}

SpoofImpl::SpoofImpl(UErrorCode& status) {
    construct(status);

    // TODO: Call this method where it is actually needed, instead of in the
    // constructor, to allow for lazy data loading.  See #12696.
    fSpoofData = SpoofData::getDefault(status);
}

SpoofImpl::SpoofImpl() {
    UErrorCode status = U_ZERO_ERROR;
    construct(status);

    // TODO: Call this method where it is actually needed, instead of in the
    // constructor, to allow for lazy data loading.  See #12696.
    fSpoofData = SpoofData::getDefault(status);
}

void SpoofImpl::construct(UErrorCode& status) {
    fChecks = USPOOF_ALL_CHECKS;
    fSpoofData = nullptr;
    fAllowedCharsSet = nullptr;
    fAllowedLocales = nullptr;
    fRestrictionLevel = USPOOF_HIGHLY_RESTRICTIVE;

    if (U_FAILURE(status)) { return; }

    UnicodeSet *allowedCharsSet = new UnicodeSet(0, 0x10ffff);
    fAllowedCharsSet = allowedCharsSet;
    fAllowedLocales  = uprv_strdup("");
    if (fAllowedCharsSet == nullptr || fAllowedLocales == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    allowedCharsSet->freeze();
}


// Copy Constructor, used by the user level clone() function.
SpoofImpl::SpoofImpl(const SpoofImpl &src, UErrorCode &status)  :
        fChecks(USPOOF_ALL_CHECKS), fSpoofData(nullptr), fAllowedCharsSet(nullptr) , 
        fAllowedLocales(nullptr) {
    if (U_FAILURE(status)) {
        return;
    }
    fChecks = src.fChecks;
    if (src.fSpoofData != nullptr) {
        fSpoofData = src.fSpoofData->addReference();
    }
    fAllowedCharsSet = src.fAllowedCharsSet->clone();
    fAllowedLocales = uprv_strdup(src.fAllowedLocales);
    if (fAllowedCharsSet == nullptr || fAllowedLocales == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
    }
    fRestrictionLevel = src.fRestrictionLevel;
}

SpoofImpl::~SpoofImpl() {
    if (fSpoofData != nullptr) {
        fSpoofData->removeReference();   // Will delete if refCount goes to zero.
    }
    delete fAllowedCharsSet;
    uprv_free((void *)fAllowedLocales);
}

//  Cast this instance as a USpoofChecker for the C API.
USpoofChecker *SpoofImpl::asUSpoofChecker() {
    return exportForC();
}

//
//  Incoming parameter check on Status and the SpoofChecker object
//    received from the C API.
//
const SpoofImpl *SpoofImpl::validateThis(const USpoofChecker *sc, UErrorCode &status) {
    const auto* This = validate(sc, status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    if (This->fSpoofData != nullptr && !This->fSpoofData->validateDataVersion(status)) {
        return nullptr;
    }
    return This;
}

SpoofImpl *SpoofImpl::validateThis(USpoofChecker *sc, UErrorCode &status) {
    return const_cast<SpoofImpl *>
        (SpoofImpl::validateThis(const_cast<const USpoofChecker *>(sc), status));
}


void SpoofImpl::setAllowedLocales(const char *localesList, UErrorCode &status) {
    UnicodeSet    allowedChars;
    UnicodeSet    *tmpSet = nullptr;
    const char    *locStart = localesList;
    const char    *locEnd = nullptr;
    const char    *localesListEnd = localesList + uprv_strlen(localesList);
    int32_t        localeListCount = 0;   // Number of locales provided by caller.

    // Loop runs once per locale from the localesList, a comma separated list of locales.
    do {
        locEnd = uprv_strchr(locStart, ',');
        if (locEnd == nullptr) {
            locEnd = localesListEnd;
        }
        while (*locStart == ' ') {
            locStart++;
        }
        const char *trimmedEnd = locEnd-1;
        while (trimmedEnd > locStart && *trimmedEnd == ' ') {
            trimmedEnd--;
        }
        if (trimmedEnd <= locStart) {
            break;
        }
        const char* locale = uprv_strndup(locStart, static_cast<int32_t>(trimmedEnd + 1 - locStart));
        localeListCount++;

        // We have one locale from the locales list.
        // Add the script chars for this locale to the accumulating set of allowed chars.
        // If the locale is no good, we will be notified back via status.
        addScriptChars(locale, &allowedChars, status);
        uprv_free((void *)locale);
        if (U_FAILURE(status)) {
            break;
        }
        locStart = locEnd + 1;
    } while (locStart < localesListEnd);

    // If our caller provided an empty list of locales, we disable the allowed characters checking
    if (localeListCount == 0) {
        uprv_free((void *)fAllowedLocales);
        fAllowedLocales = uprv_strdup("");
        tmpSet = new UnicodeSet(0, 0x10ffff);
        if (fAllowedLocales == nullptr || tmpSet == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return;
        } 
        tmpSet->freeze();
        delete fAllowedCharsSet;
        fAllowedCharsSet = tmpSet;
        fChecks &= ~USPOOF_CHAR_LIMIT;
        return;
    }

        
    // Add all common and inherited characters to the set of allowed chars.
    UnicodeSet tempSet;
    tempSet.applyIntPropertyValue(UCHAR_SCRIPT, USCRIPT_COMMON, status);
    allowedChars.addAll(tempSet);
    tempSet.applyIntPropertyValue(UCHAR_SCRIPT, USCRIPT_INHERITED, status);
    allowedChars.addAll(tempSet);
    
    // If anything went wrong, we bail out without changing
    // the state of the spoof checker.
    if (U_FAILURE(status)) {
        return;
    }

    // Store the updated spoof checker state.
    tmpSet = allowedChars.clone();
    const char *tmpLocalesList = uprv_strdup(localesList);
    if (tmpSet == nullptr || tmpLocalesList == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    uprv_free((void *)fAllowedLocales);
    fAllowedLocales = tmpLocalesList;
    tmpSet->freeze();
    delete fAllowedCharsSet;
    fAllowedCharsSet = tmpSet;
    fChecks |= USPOOF_CHAR_LIMIT;
}


const char * SpoofImpl::getAllowedLocales(UErrorCode &/*status*/) {
    return fAllowedLocales;
}


// Given a locale (a language), add all the characters from all of the scripts used with that language
// to the allowedChars UnicodeSet

void SpoofImpl::addScriptChars(const char *locale, UnicodeSet *allowedChars, UErrorCode &status) {
    UScriptCode scripts[30];

    int32_t numScripts = uscript_getCode(locale, scripts, UPRV_LENGTHOF(scripts), &status);
    if (U_FAILURE(status)) {
        return;
    }
    if (status == U_USING_DEFAULT_WARNING) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    UnicodeSet tmpSet;
    int32_t    i;
    for (i=0; i<numScripts; i++) {
        tmpSet.applyIntPropertyValue(UCHAR_SCRIPT, scripts[i], status);
        allowedChars->addAll(tmpSet);
    }
}

// Computes the augmented script set for a code point, according to UTS 39 section 5.1.
void SpoofImpl::getAugmentedScriptSet(UChar32 codePoint, ScriptSet& result, UErrorCode& status) {
    result.resetAll();
    result.setScriptExtensions(codePoint, status);
    if (U_FAILURE(status)) { return; }

    // Section 5.1 step 1
    if (result.test(USCRIPT_HAN, status)) {
        result.set(USCRIPT_HAN_WITH_BOPOMOFO, status);
        result.set(USCRIPT_JAPANESE, status);
        result.set(USCRIPT_KOREAN, status);
    }
    if (result.test(USCRIPT_HIRAGANA, status)) {
        result.set(USCRIPT_JAPANESE, status);
    }
    if (result.test(USCRIPT_KATAKANA, status)) {
        result.set(USCRIPT_JAPANESE, status);
    }
    if (result.test(USCRIPT_HANGUL, status)) {
        result.set(USCRIPT_KOREAN, status);
    }
    if (result.test(USCRIPT_BOPOMOFO, status)) {
        result.set(USCRIPT_HAN_WITH_BOPOMOFO, status);
    }

    // Section 5.1 step 2
    if (result.test(USCRIPT_COMMON, status) || result.test(USCRIPT_INHERITED, status)) {
        result.setAll();
    }
}

// Computes the resolved script set for a string, according to UTS 39 section 5.1.
void SpoofImpl::getResolvedScriptSet(const UnicodeString& input, ScriptSet& result, UErrorCode& status) const {
    getResolvedScriptSetWithout(input, USCRIPT_CODE_LIMIT, result, status);
}

// Computes the resolved script set for a string, omitting characters having the specified script.
// If USCRIPT_CODE_LIMIT is passed as the second argument, all characters are included.
void SpoofImpl::getResolvedScriptSetWithout(const UnicodeString& input, UScriptCode script, ScriptSet& result, UErrorCode& status) const {
    result.setAll();

    ScriptSet temp;
    UChar32 codePoint;
    for (int32_t i = 0; i < input.length(); i += U16_LENGTH(codePoint)) {
        codePoint = input.char32At(i);

        // Compute the augmented script set for the character
        getAugmentedScriptSet(codePoint, temp, status);
        if (U_FAILURE(status)) { return; }

        // Intersect the augmented script set with the resolved script set, but only if the character doesn't
        // have the script specified in the function call
        if (script == USCRIPT_CODE_LIMIT || !temp.test(script, status)) {
            result.intersect(temp);
        }
    }
}

// Computes the set of numerics for a string, according to UTS 39 section 5.3.
void SpoofImpl::getNumerics(const UnicodeString& input, UnicodeSet& result, UErrorCode& /*status*/) const {
    result.clear();

    UChar32 codePoint;
    for (int32_t i = 0; i < input.length(); i += U16_LENGTH(codePoint)) {
        codePoint = input.char32At(i);

        // Store a representative character for each kind of decimal digit
        if (u_charType(codePoint) == U_DECIMAL_DIGIT_NUMBER) {
            // Store the zero character as a representative for comparison.
            // Unicode guarantees it is codePoint - value
            result.add(codePoint - static_cast<UChar32>(u_getNumericValue(codePoint)));
        }
    }
}

// Computes the restriction level of a string, according to UTS 39 section 5.2.
URestrictionLevel SpoofImpl::getRestrictionLevel(const UnicodeString& input, UErrorCode& status) const {
    // Section 5.2 step 1:
    if (!fAllowedCharsSet->containsAll(input)) {
        return USPOOF_UNRESTRICTIVE;
    }

    // Section 5.2 step 2
    // Java use a static UnicodeSet for this test.  In C++, avoid the static variable
    // and just do a simple for loop.
    UBool allASCII = true;
    for (int32_t i=0, length=input.length(); i<length; i++) {
        if (input.charAt(i) > 0x7f) {
            allASCII = false;
            break;
        }
    }
    if (allASCII) {
        return USPOOF_ASCII;
    }

    // Section 5.2 steps 3:
    ScriptSet resolvedScriptSet;
    getResolvedScriptSet(input, resolvedScriptSet, status);
    if (U_FAILURE(status)) { return USPOOF_UNRESTRICTIVE; }

    // Section 5.2 step 4:
    if (!resolvedScriptSet.isEmpty()) {
        return USPOOF_SINGLE_SCRIPT_RESTRICTIVE;
    }

    // Section 5.2 step 5:
    ScriptSet resolvedNoLatn;
    getResolvedScriptSetWithout(input, USCRIPT_LATIN, resolvedNoLatn, status);
    if (U_FAILURE(status)) { return USPOOF_UNRESTRICTIVE; }

    // Section 5.2 step 6:
    if (resolvedNoLatn.test(USCRIPT_HAN_WITH_BOPOMOFO, status)
            || resolvedNoLatn.test(USCRIPT_JAPANESE, status)
            || resolvedNoLatn.test(USCRIPT_KOREAN, status)) {
        return USPOOF_HIGHLY_RESTRICTIVE;
    }

    // Section 5.2 step 7:
    if (!resolvedNoLatn.isEmpty()
            && !resolvedNoLatn.test(USCRIPT_CYRILLIC, status)
            && !resolvedNoLatn.test(USCRIPT_GREEK, status)
            && !resolvedNoLatn.test(USCRIPT_CHEROKEE, status)) {
        return USPOOF_MODERATELY_RESTRICTIVE;
    }

    // Section 5.2 step 8:
    return USPOOF_MINIMALLY_RESTRICTIVE;
}

int32_t SpoofImpl::findHiddenOverlay(const UnicodeString& input, UErrorCode&) const {
    bool sawLeadCharacter = false;
    for (int32_t i=0; i<input.length();) {
        UChar32 cp = input.char32At(i);
        if (sawLeadCharacter && cp == 0x0307) {
            return i;
        }
        uint8_t combiningClass = u_getCombiningClass(cp);
        // Skip over characters except for those with combining class 0 (non-combining characters) or with
        // combining class 230 (same class as U+0307)
        U_ASSERT(u_getCombiningClass(0x0307) == 230);
        if (combiningClass == 0 || combiningClass == 230) {
            sawLeadCharacter = isIllegalCombiningDotLeadCharacter(cp);
        }
        i += U16_LENGTH(cp);
    }
    return -1;
}

static inline bool isIllegalCombiningDotLeadCharacterNoLookup(UChar32 cp) {
    return cp == u'i' || cp == u'j' || cp == u'ı' || cp == u'ȷ' || cp == u'l' ||
           u_hasBinaryProperty(cp, UCHAR_SOFT_DOTTED);
}

bool SpoofImpl::isIllegalCombiningDotLeadCharacter(UChar32 cp) const {
    if (isIllegalCombiningDotLeadCharacterNoLookup(cp)) {
        return true;
    }
    UnicodeString skelStr;
    fSpoofData->confusableLookup(cp, skelStr);
    UChar32 finalCp = skelStr.char32At(skelStr.moveIndex32(skelStr.length(), -1));
    if (finalCp != cp && isIllegalCombiningDotLeadCharacterNoLookup(finalCp)) {
        return true;
    }
    return false;
}



// Convert a text format hex number.  Utility function used by builder code.  Static.
// Input: char16_t *string text.  Output: a UChar32
// Input has been pre-checked, and will have no non-hex chars.
// The number must fall in the code point range of 0..0x10ffff
// Static Function.
UChar32 SpoofImpl::ScanHex(const char16_t *s, int32_t start, int32_t limit, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return 0;
    }
    U_ASSERT(limit-start > 0);
    uint32_t val = 0;
    int i;
    for (i=start; i<limit; i++) {
        int digitVal = s[i] - 0x30;
        if (digitVal>9) {
            digitVal = 0xa + (s[i] - 0x41);  // Upper Case 'A'
        }
        if (digitVal>15) {
            digitVal = 0xa + (s[i] - 0x61);  // Lower Case 'a'
        }
        U_ASSERT(digitVal <= 0xf);
        val <<= 4;
        val += digitVal;
    }
    if (val > 0x10ffff) {
        status = U_PARSE_ERROR;
        val = 0;
    }
    return static_cast<UChar32>(val);
}


//-----------------------------------------
//
//   class CheckResult Implementation
//
//-----------------------------------------

CheckResult::CheckResult() {
    clear();
}

USpoofCheckResult* CheckResult::asUSpoofCheckResult() {
    return exportForC();
}

//
//  Incoming parameter check on Status and the CheckResult object
//    received from the C API.
//
const CheckResult* CheckResult::validateThis(const USpoofCheckResult *ptr, UErrorCode &status) {
    return validate(ptr, status);
}

CheckResult* CheckResult::validateThis(USpoofCheckResult *ptr, UErrorCode &status) {
    return validate(ptr, status);
}

void CheckResult::clear() {
    fChecks = 0;
    fNumerics.clear();
    fRestrictionLevel = USPOOF_UNDEFINED_RESTRICTIVE;
}

int32_t CheckResult::toCombinedBitmask(int32_t enabledChecks) {
    if ((enabledChecks & USPOOF_AUX_INFO) != 0 && fRestrictionLevel != USPOOF_UNDEFINED_RESTRICTIVE) {
        return fChecks | fRestrictionLevel;
    } else {
        return fChecks;
    }
}

CheckResult::~CheckResult() {
}

//----------------------------------------------------------------------------------------------
//
//   class SpoofData Implementation
//
//----------------------------------------------------------------------------------------------


UBool SpoofData::validateDataVersion(UErrorCode &status) const {
    if (U_FAILURE(status) ||
        fRawData == nullptr ||
        fRawData->fMagic != USPOOF_MAGIC ||
        fRawData->fFormatVersion[0] != USPOOF_CONFUSABLE_DATA_FORMAT_VERSION ||
        fRawData->fFormatVersion[1] != 0 ||
        fRawData->fFormatVersion[2] != 0 ||
        fRawData->fFormatVersion[3] != 0) {
            status = U_INVALID_FORMAT_ERROR;
            return false;
    }
    return true;
}

static UBool U_CALLCONV
spoofDataIsAcceptable(void *context,
                        const char * /* type */, const char * /*name*/,
                        const UDataInfo *pInfo) {
    if(
        pInfo->size >= 20 &&
        pInfo->isBigEndian == U_IS_BIG_ENDIAN &&
        pInfo->charsetFamily == U_CHARSET_FAMILY &&
        pInfo->dataFormat[0] == 0x43 &&  // dataFormat="Cfu "
        pInfo->dataFormat[1] == 0x66 &&
        pInfo->dataFormat[2] == 0x75 &&
        pInfo->dataFormat[3] == 0x20 &&
        pInfo->formatVersion[0] == USPOOF_CONFUSABLE_DATA_FORMAT_VERSION
    ) {
        UVersionInfo *version = static_cast<UVersionInfo *>(context);
        if(version != nullptr) {
            uprv_memcpy(version, pInfo->dataVersion, 4);
        }
        return true;
    } else {
        return false;
    }
}

//  Methods for the loading of the default confusables data file.  The confusable
//  data is loaded only when it is needed.
//
//  SpoofData::getDefault() - Return the default confusables data, and call the
//                            initOnce() if it is not available.  Adds a reference
//                            to the SpoofData that the caller is responsible for
//                            decrementing when they are done with the data.
//
//  uspoof_loadDefaultData - Called once, from initOnce().  The resulting SpoofData
//                           is shared by all spoof checkers using the default data.
//
//  uspoof_cleanupDefaultData - Called during cleanup.
//

static UInitOnce gSpoofInitDefaultOnce {};
static SpoofData* gDefaultSpoofData;

static UBool U_CALLCONV
uspoof_cleanupDefaultData() {
    if (gDefaultSpoofData) {
        // Will delete, assuming all user-level spoof checkers were closed.
        gDefaultSpoofData->removeReference();
        gDefaultSpoofData = nullptr;
        gSpoofInitDefaultOnce.reset();
    }
    return true;
}

static void U_CALLCONV uspoof_loadDefaultData(UErrorCode& status) {
    UDataMemory *udm = udata_openChoice(nullptr, "cfu", "confusables",
                                        spoofDataIsAcceptable, 
                                        nullptr,       // context, would receive dataVersion if supplied.
                                        &status);
    if (U_FAILURE(status)) { return; }
    gDefaultSpoofData = new SpoofData(udm, status);
    if (U_FAILURE(status)) {
        delete gDefaultSpoofData;
        gDefaultSpoofData = nullptr;
        return;
    }
    if (gDefaultSpoofData == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    ucln_i18n_registerCleanup(UCLN_I18N_SPOOFDATA, uspoof_cleanupDefaultData);
}

SpoofData* SpoofData::getDefault(UErrorCode& status) {
    umtx_initOnce(gSpoofInitDefaultOnce, &uspoof_loadDefaultData, status);
    if (U_FAILURE(status)) { return nullptr; }
    gDefaultSpoofData->addReference();
    return gDefaultSpoofData;
}



SpoofData::SpoofData(UDataMemory *udm, UErrorCode &status)
{
    reset();
    if (U_FAILURE(status)) {
        return;
    }
    fUDM = udm;
    // fRawData is non-const because it may be constructed by the data builder.
    fRawData = reinterpret_cast<SpoofDataHeader *>(
            const_cast<void *>(udata_getMemory(udm)));
    validateDataVersion(status);
    initPtrs(status);
}


SpoofData::SpoofData(const void *data, int32_t length, UErrorCode &status)
{
    reset();
    if (U_FAILURE(status)) {
        return;
    }
    if (static_cast<size_t>(length) < sizeof(SpoofDataHeader)) {
        status = U_INVALID_FORMAT_ERROR;
        return;
    }
    if (data == nullptr) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    void *ncData = const_cast<void *>(data);
    fRawData = static_cast<SpoofDataHeader *>(ncData);
    if (length < fRawData->fLength) {
        status = U_INVALID_FORMAT_ERROR;
        return;
    }
    validateDataVersion(status);
    initPtrs(status);
}


// Spoof Data constructor for use from data builder.
//   Initializes a new, empty data area that will be populated later.
SpoofData::SpoofData(UErrorCode &status) {
    reset();
    if (U_FAILURE(status)) {
        return;
    }
    fDataOwned = true;

    // The spoof header should already be sized to be a multiple of 16 bytes.
    // Just in case it's not, round it up.
    uint32_t initialSize = (sizeof(SpoofDataHeader) + 15) & ~15;
    U_ASSERT(initialSize == sizeof(SpoofDataHeader));
    
    fRawData = static_cast<SpoofDataHeader *>(uprv_malloc(initialSize));
    fMemLimit = initialSize;
    if (fRawData == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    uprv_memset(fRawData, 0, initialSize);

    fRawData->fMagic = USPOOF_MAGIC;
    fRawData->fFormatVersion[0] = USPOOF_CONFUSABLE_DATA_FORMAT_VERSION;
    fRawData->fFormatVersion[1] = 0;
    fRawData->fFormatVersion[2] = 0;
    fRawData->fFormatVersion[3] = 0;
    initPtrs(status);
}

// reset() - initialize all fields.
//           Should be updated if any new fields are added.
//           Called by constructors to put things in a known initial state.
void SpoofData::reset() {
   fRawData = nullptr;
   fDataOwned = false;
   fUDM      = nullptr;
   fMemLimit = 0;
   fRefCount = 1;
   fCFUKeys = nullptr;
   fCFUValues = nullptr;
   fCFUStrings = nullptr;
}


//  SpoofData::initPtrs()
//            Initialize the pointers to the various sections of the raw data.
//
//            This function is used both during the Trie building process (multiple
//            times, as the individual data sections are added), and
//            during the opening of a Spoof Checker from prebuilt data.
//
//            The pointers for non-existent data sections (identified by an offset of 0)
//            are set to nullptr.
//
//            Note:  During building the data, adding each new data section
//            reallocs the raw data area, which likely relocates it, which
//            in turn requires reinitializing all of the pointers into it, hence
//            multiple calls to this function during building.
//
void SpoofData::initPtrs(UErrorCode &status) {
    fCFUKeys = nullptr;
    fCFUValues = nullptr;
    fCFUStrings = nullptr;
    if (U_FAILURE(status)) {
        return;
    }
    if (fRawData->fCFUKeys != 0) {
        fCFUKeys = reinterpret_cast<int32_t*>(reinterpret_cast<char*>(fRawData) + fRawData->fCFUKeys);
    }
    if (fRawData->fCFUStringIndex != 0) {
        fCFUValues = reinterpret_cast<uint16_t*>(reinterpret_cast<char*>(fRawData) + fRawData->fCFUStringIndex);
    }
    if (fRawData->fCFUStringTable != 0) {
        fCFUStrings = reinterpret_cast<char16_t*>(reinterpret_cast<char*>(fRawData) + fRawData->fCFUStringTable);
    }
}


SpoofData::~SpoofData() {
    if (fDataOwned) {
        uprv_free(fRawData);
    }
    fRawData = nullptr;
    if (fUDM != nullptr) {
        udata_close(fUDM);
    }
    fUDM = nullptr;
}


void SpoofData::removeReference() {
    if (umtx_atomic_dec(&fRefCount) == 0) {
        delete this;
    }
}


SpoofData *SpoofData::addReference() {
    umtx_atomic_inc(&fRefCount);
    return this;
}


void *SpoofData::reserveSpace(int32_t numBytes,  UErrorCode &status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    if (!fDataOwned) {
        UPRV_UNREACHABLE_EXIT;
    }

    numBytes = (numBytes + 15) & ~15;   // Round up to a multiple of 16
    uint32_t returnOffset = fMemLimit;
    fMemLimit += numBytes;
    fRawData = static_cast<SpoofDataHeader *>(uprv_realloc(fRawData, fMemLimit));
    fRawData->fLength = fMemLimit;
    uprv_memset((char *)fRawData + returnOffset, 0, numBytes);
    initPtrs(status);
    return reinterpret_cast<char*>(fRawData) + returnOffset;
}

int32_t SpoofData::serialize(void *buf, int32_t capacity, UErrorCode &status) const {
    int32_t dataSize = fRawData->fLength;
    if (capacity < dataSize) {
        status = U_BUFFER_OVERFLOW_ERROR;
        return dataSize;
    }
    uprv_memcpy(buf, fRawData, dataSize);
    return dataSize;
}

int32_t SpoofData::size() const {
    return fRawData->fLength;
}

//-------------------------------
//
// Front-end APIs for SpoofData
//
//-------------------------------

int32_t SpoofData::confusableLookup(UChar32 inChar, UnicodeString &dest) const {
    // Perform a binary search.
    // [lo, hi), i.e lo is inclusive, hi is exclusive.
    // The result after the loop will be in lo.
    int32_t lo = 0;
    int32_t hi = length();
    do {
        int32_t mid = (lo + hi) / 2;
        if (codePointAt(mid) > inChar) {
            hi = mid;
        } else if (codePointAt(mid) < inChar) {
            lo = mid;
        } else {
            // Found result.  Break early.
            lo = mid;
            break;
        }
    } while (hi - lo > 1);

    // Did we find an entry?  If not, the char maps to itself.
    if (codePointAt(lo) != inChar) {
        dest.append(inChar);
        return 1;
    }

    // Add the element to the string builder and return.
    return appendValueTo(lo, dest);
}

int32_t SpoofData::length() const {
    return fRawData->fCFUKeysSize;
}

UChar32 SpoofData::codePointAt(int32_t index) const {
    return ConfusableDataUtils::keyToCodePoint(fCFUKeys[index]);
}

int32_t SpoofData::appendValueTo(int32_t index, UnicodeString& dest) const {
    int32_t stringLength = ConfusableDataUtils::keyToLength(fCFUKeys[index]);

    // Value is either a char (for strings of length 1) or
    // an index into the string table (for longer strings)
    uint16_t value = fCFUValues[index];
    if (stringLength == 1) {
        dest.append(static_cast<char16_t>(value));
    } else {
        dest.append(fCFUStrings + value, stringLength);
    }

    return stringLength;
}


U_NAMESPACE_END

U_NAMESPACE_USE

//-----------------------------------------------------------------------------
//
//  uspoof_swap   -  byte swap and char encoding swap of spoof data
//
//-----------------------------------------------------------------------------
U_CAPI int32_t U_EXPORT2
uspoof_swap(const UDataSwapper *ds, const void *inData, int32_t length, void *outData,
           UErrorCode *status) {

    if (status == nullptr || U_FAILURE(*status)) {
        return 0;
    }
    if(ds==nullptr || inData==nullptr || length<-1 || (length>0 && outData==nullptr)) {
        *status=U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }

    //
    //  Check that the data header is for spoof data.
    //    (Header contents are defined in gencfu.cpp)
    //
    const UDataInfo *pInfo = (const UDataInfo *)((const char *)inData+4);
    if(!(  pInfo->dataFormat[0]==0x43 &&   /* dataFormat="Cfu " */
           pInfo->dataFormat[1]==0x66 &&
           pInfo->dataFormat[2]==0x75 &&
           pInfo->dataFormat[3]==0x20 &&
           pInfo->formatVersion[0]==USPOOF_CONFUSABLE_DATA_FORMAT_VERSION &&
           pInfo->formatVersion[1]==0 &&
           pInfo->formatVersion[2]==0 &&
           pInfo->formatVersion[3]==0  )) {
        udata_printError(ds, "uspoof_swap(): data format %02x.%02x.%02x.%02x "
                             "(format version %02x %02x %02x %02x) is not recognized\n",
                         pInfo->dataFormat[0], pInfo->dataFormat[1],
                         pInfo->dataFormat[2], pInfo->dataFormat[3],
                         pInfo->formatVersion[0], pInfo->formatVersion[1],
                         pInfo->formatVersion[2], pInfo->formatVersion[3]);
        *status=U_UNSUPPORTED_ERROR;
        return 0;
    }

    //
    // Swap the data header.  (This is the generic ICU Data Header, not the uspoof Specific
    //                         header).  This swap also conveniently gets us
    //                         the size of the ICU d.h., which lets us locate the start
    //                         of the uspoof specific data.
    //
    int32_t headerSize=udata_swapDataHeader(ds, inData, length, outData, status);


    //
    // Get the Spoof Data Header, and check that it appears to be OK.
    //
    //
    const uint8_t   *inBytes =(const uint8_t *)inData+headerSize;
    SpoofDataHeader *spoofDH = (SpoofDataHeader *)inBytes;
    if (ds->readUInt32(spoofDH->fMagic)   != USPOOF_MAGIC ||
        ds->readUInt32(spoofDH->fLength)  <  sizeof(SpoofDataHeader)) 
    {
        udata_printError(ds, "uspoof_swap(): Spoof Data header is invalid.\n");
        *status=U_UNSUPPORTED_ERROR;
        return 0;
    }

    //
    // Prefight operation?  Just return the size
    //
    int32_t spoofDataLength = ds->readUInt32(spoofDH->fLength);
    int32_t totalSize = headerSize + spoofDataLength;
    if (length < 0) {
        return totalSize;
    }

    //
    // Check that length passed in is consistent with length from Spoof data header.
    //
    if (length < totalSize) {
        udata_printError(ds, "uspoof_swap(): too few bytes (%d after ICU Data header) for spoof data.\n",
                            spoofDataLength);
        *status=U_INDEX_OUTOFBOUNDS_ERROR;
        return 0;
        }


    //
    // Swap the Data.  Do the data itself first, then the Spoof Data Header, because
    //                 we need to reference the header to locate the data, and an
    //                 inplace swap of the header leaves it unusable.
    //
    uint8_t          *outBytes = (uint8_t *)outData + headerSize;
    SpoofDataHeader  *outputDH = (SpoofDataHeader *)outBytes;

    int32_t   sectionStart;
    int32_t   sectionLength;

    //
    // If not swapping in place, zero out the output buffer before starting.
    //    Gaps may exist between the individual sections, and these must be zeroed in
    //    the output buffer.  The simplest way to do that is to just zero the whole thing.
    //
    if (inBytes != outBytes) {
        uprv_memset(outBytes, 0, spoofDataLength);
    }

    // Confusables Keys Section   (fCFUKeys)
    sectionStart  = ds->readUInt32(spoofDH->fCFUKeys);
    sectionLength = ds->readUInt32(spoofDH->fCFUKeysSize) * 4;
    ds->swapArray32(ds, inBytes+sectionStart, sectionLength, outBytes+sectionStart, status);

    // String Index Section
    sectionStart  = ds->readUInt32(spoofDH->fCFUStringIndex);
    sectionLength = ds->readUInt32(spoofDH->fCFUStringIndexSize) * 2;
    ds->swapArray16(ds, inBytes+sectionStart, sectionLength, outBytes+sectionStart, status);

    // String Table Section
    sectionStart  = ds->readUInt32(spoofDH->fCFUStringTable);
    sectionLength = ds->readUInt32(spoofDH->fCFUStringTableLen) * 2;
    ds->swapArray16(ds, inBytes+sectionStart, sectionLength, outBytes+sectionStart, status);

    // And, last, swap the header itself.
    //   int32_t   fMagic             // swap this
    //   uint8_t   fFormatVersion[4]  // Do not swap this, just copy
    //   int32_t   fLength and all the rest       // Swap the rest, all is 32 bit stuff.
    //
    uint32_t magic = ds->readUInt32(spoofDH->fMagic);
    ds->writeUInt32((uint32_t *)&outputDH->fMagic, magic);

    if (inBytes != outBytes) {
        uprv_memcpy(outputDH->fFormatVersion, spoofDH->fFormatVersion, sizeof(spoofDH->fFormatVersion));
    }
    // swap starting at fLength
    ds->swapArray32(ds, &spoofDH->fLength, sizeof(SpoofDataHeader)-8 /* minus magic and fFormatVersion[4] */, &outputDH->fLength, status);

    return totalSize;
}

#endif


