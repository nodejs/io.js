// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 1997-2015, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*
* File brkiter.cpp
*
* Modification History:
*
*   Date        Name        Description
*   02/18/97    aliu        Converted from OpenClass.  Added DONE.
*   01/13/2000  helena      Added UErrorCode parameter to createXXXInstance methods.
*****************************************************************************************
*/

// *****************************************************************************
// This file was generated from the java source file BreakIterator.java
// *****************************************************************************

#include "unicode/utypes.h"

#if !UCONFIG_NO_BREAK_ITERATION

#include "unicode/rbbi.h"
#include "unicode/brkiter.h"
#include "unicode/udata.h"
#include "unicode/uloc.h"
#include "unicode/ures.h"
#include "unicode/ustring.h"
#include "unicode/filteredbrk.h"
#include "bytesinkutil.h"
#include "ucln_cmn.h"
#include "cstring.h"
#include "umutex.h"
#include "servloc.h"
#include "locbased.h"
#include "uresimp.h"
#include "uassert.h"
#include "ubrkimpl.h"
#include "utracimp.h"
#include "charstr.h"

// *****************************************************************************
// class BreakIterator
// This class implements methods for finding the location of boundaries in text.
// Instances of BreakIterator maintain a current position and scan over text
// returning the index of characters where boundaries occur.
// *****************************************************************************

U_NAMESPACE_BEGIN

// -------------------------------------

BreakIterator*
BreakIterator::buildInstance(const Locale& loc, const char *type, UErrorCode &status)
{
    char fnbuff[256];
    char ext[4]={'\0'};
    CharString actualLocale;
    int32_t size;
    const char16_t* brkfname = nullptr;
    UResourceBundle brkRulesStack;
    UResourceBundle brkNameStack;
    UResourceBundle *brkRules = &brkRulesStack;
    UResourceBundle *brkName  = &brkNameStack;
    RuleBasedBreakIterator *result = nullptr;

    if (U_FAILURE(status))
        return nullptr;

    ures_initStackObject(brkRules);
    ures_initStackObject(brkName);

    // Get the locale
    UResourceBundle *b = ures_openNoDefault(U_ICUDATA_BRKITR, loc.getName(), &status);

    // Get the "boundaries" array.
    if (U_SUCCESS(status)) {
        brkRules = ures_getByKeyWithFallback(b, "boundaries", brkRules, &status);
        // Get the string object naming the rules file
        brkName = ures_getByKeyWithFallback(brkRules, type, brkName, &status);
        // Get the actual string
        brkfname = ures_getString(brkName, &size, &status);
        U_ASSERT((size_t)size<sizeof(fnbuff));
        if (static_cast<size_t>(size) >= sizeof(fnbuff)) {
            size=0;
            if (U_SUCCESS(status)) {
                status = U_BUFFER_OVERFLOW_ERROR;
            }
        }

        // Use the string if we found it
        if (U_SUCCESS(status) && brkfname) {
            actualLocale.append(ures_getLocaleInternal(brkName, &status), -1, status);

            char16_t* extStart=u_strchr(brkfname, 0x002e);
            int len = 0;
            if (extStart != nullptr){
                len = static_cast<int>(extStart - brkfname);
                u_UCharsToChars(extStart+1, ext, sizeof(ext)); // nul terminates the buff
                u_UCharsToChars(brkfname, fnbuff, len);
            }
            fnbuff[len]=0; // nul terminate
        }
    }

    ures_close(brkRules);
    ures_close(brkName);

    UDataMemory* file = udata_open(U_ICUDATA_BRKITR, ext, fnbuff, &status);
    if (U_FAILURE(status)) {
        ures_close(b);
        return nullptr;
    }

    // Create a RuleBasedBreakIterator
    result = new RuleBasedBreakIterator(file, uprv_strstr(type, "phrase") != nullptr, status);

    // If there is a result, set the valid locale and actual locale, and the kind
    if (U_SUCCESS(status) && result != nullptr) {
        U_LOCALE_BASED(locBased, *(BreakIterator*)result);

        locBased.setLocaleIDs(ures_getLocaleByType(b, ULOC_VALID_LOCALE, &status), 
                              actualLocale.data());
        uprv_strncpy(result->requestLocale, loc.getName(), ULOC_FULLNAME_CAPACITY);
        result->requestLocale[ULOC_FULLNAME_CAPACITY-1] = 0; // always terminate
    }

    ures_close(b);

    if (U_FAILURE(status) && result != nullptr) {  // Sometimes redundant check, but simple
        delete result;
        return nullptr;
    }

    if (result == nullptr) {
        udata_close(file);
        if (U_SUCCESS(status)) {
            status = U_MEMORY_ALLOCATION_ERROR;
        }
    }

    return result;
}

// Creates a break iterator for word breaks.
BreakIterator* U_EXPORT2
BreakIterator::createWordInstance(const Locale& key, UErrorCode& status)
{
    return createInstance(key, UBRK_WORD, status);
}

// -------------------------------------

// Creates a break iterator  for line breaks.
BreakIterator* U_EXPORT2
BreakIterator::createLineInstance(const Locale& key, UErrorCode& status)
{
    return createInstance(key, UBRK_LINE, status);
}

// -------------------------------------

// Creates a break iterator  for character breaks.
BreakIterator* U_EXPORT2
BreakIterator::createCharacterInstance(const Locale& key, UErrorCode& status)
{
    return createInstance(key, UBRK_CHARACTER, status);
}

// -------------------------------------

// Creates a break iterator  for sentence breaks.
BreakIterator* U_EXPORT2
BreakIterator::createSentenceInstance(const Locale& key, UErrorCode& status)
{
    return createInstance(key, UBRK_SENTENCE, status);
}

// -------------------------------------

// Creates a break iterator for title casing breaks.
BreakIterator* U_EXPORT2
BreakIterator::createTitleInstance(const Locale& key, UErrorCode& status)
{
    return createInstance(key, UBRK_TITLE, status);
}

// -------------------------------------

// Gets all the available locales that has localized text boundary data.
const Locale* U_EXPORT2
BreakIterator::getAvailableLocales(int32_t& count)
{
    return Locale::getAvailableLocales(count);
}

// ------------------------------------------
//
// Constructors, destructor and assignment operator
//
//-------------------------------------------

BreakIterator::BreakIterator()
{
    *validLocale = *actualLocale = *requestLocale = 0;
}

BreakIterator::BreakIterator(const BreakIterator &other) : UObject(other) {
    uprv_strncpy(actualLocale, other.actualLocale, sizeof(actualLocale));
    uprv_strncpy(validLocale, other.validLocale, sizeof(validLocale));
    uprv_strncpy(requestLocale, other.requestLocale, sizeof(requestLocale));
}

BreakIterator &BreakIterator::operator =(const BreakIterator &other) {
    if (this != &other) {
        uprv_strncpy(actualLocale, other.actualLocale, sizeof(actualLocale));
        uprv_strncpy(validLocale, other.validLocale, sizeof(validLocale));
        uprv_strncpy(requestLocale, other.requestLocale, sizeof(requestLocale));
    }
    return *this;
}

BreakIterator::~BreakIterator()
{
}

// ------------------------------------------
//
// Registration
//
//-------------------------------------------
#if !UCONFIG_NO_SERVICE

// -------------------------------------

class ICUBreakIteratorFactory : public ICUResourceBundleFactory {
public:
    virtual ~ICUBreakIteratorFactory();
protected:
    virtual UObject* handleCreate(const Locale& loc, int32_t kind, const ICUService* /*service*/, UErrorCode& status) const override {
        return BreakIterator::makeInstance(loc, kind, status);
    }
};

ICUBreakIteratorFactory::~ICUBreakIteratorFactory() {}

// -------------------------------------

class ICUBreakIteratorService : public ICULocaleService {
public:
    ICUBreakIteratorService()
        : ICULocaleService(UNICODE_STRING("Break Iterator", 14))
    {
        UErrorCode status = U_ZERO_ERROR;
        registerFactory(new ICUBreakIteratorFactory(), status);
    }

    virtual ~ICUBreakIteratorService();

    virtual UObject* cloneInstance(UObject* instance) const override {
        return ((BreakIterator*)instance)->clone();
    }

    virtual UObject* handleDefault(const ICUServiceKey& key, UnicodeString* /*actualID*/, UErrorCode& status) const override {
        LocaleKey& lkey = static_cast<LocaleKey&>(const_cast<ICUServiceKey&>(key));
        int32_t kind = lkey.kind();
        Locale loc;
        lkey.currentLocale(loc);
        return BreakIterator::makeInstance(loc, kind, status);
    }

    virtual UBool isDefault() const override {
        return countFactories() == 1;
    }
};

ICUBreakIteratorService::~ICUBreakIteratorService() {}

// -------------------------------------

// defined in ucln_cmn.h
U_NAMESPACE_END

static icu::UInitOnce gInitOnceBrkiter {};
static icu::ICULocaleService* gService = nullptr;



/**
 * Release all static memory held by breakiterator.
 */
U_CDECL_BEGIN
static UBool U_CALLCONV breakiterator_cleanup() {
#if !UCONFIG_NO_SERVICE
    if (gService) {
        delete gService;
        gService = nullptr;
    }
    gInitOnceBrkiter.reset();
#endif
    return true;
}
U_CDECL_END
U_NAMESPACE_BEGIN

static void U_CALLCONV 
initService() {
    gService = new ICUBreakIteratorService();
    ucln_common_registerCleanup(UCLN_COMMON_BREAKITERATOR, breakiterator_cleanup);
}

static ICULocaleService*
getService()
{
    umtx_initOnce(gInitOnceBrkiter, &initService);
    return gService;
}


// -------------------------------------

static inline UBool
hasService()
{
    return !gInitOnceBrkiter.isReset() && getService() != nullptr;
}

// -------------------------------------

URegistryKey U_EXPORT2
BreakIterator::registerInstance(BreakIterator* toAdopt, const Locale& locale, UBreakIteratorType kind, UErrorCode& status)
{
    ICULocaleService *service = getService();
    if (service == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    return service->registerInstance(toAdopt, locale, kind, status);
}

// -------------------------------------

UBool U_EXPORT2
BreakIterator::unregister(URegistryKey key, UErrorCode& status)
{
    if (U_SUCCESS(status)) {
        if (hasService()) {
            return gService->unregister(key, status);
        }
        status = U_MEMORY_ALLOCATION_ERROR;
    }
    return false;
}

// -------------------------------------

StringEnumeration* U_EXPORT2
BreakIterator::getAvailableLocales()
{
    ICULocaleService *service = getService();
    if (service == nullptr) {
        return nullptr;
    }
    return service->getAvailableLocales();
}
#endif /* UCONFIG_NO_SERVICE */

// -------------------------------------

BreakIterator*
BreakIterator::createInstance(const Locale& loc, int32_t kind, UErrorCode& status)
{
    if (U_FAILURE(status)) {
        return nullptr;
    }

#if !UCONFIG_NO_SERVICE
    if (hasService()) {
        Locale actualLoc("");
        BreakIterator *result = (BreakIterator*)gService->get(loc, kind, &actualLoc, status);
        // TODO: The way the service code works in ICU 2.8 is that if
        // there is a real registered break iterator, the actualLoc
        // will be populated, but if the handleDefault path is taken
        // (because nothing is registered that can handle the
        // requested locale) then the actualLoc comes back empty.  In
        // that case, the returned object already has its actual/valid
        // locale data populated (by makeInstance, which is what
        // handleDefault calls), so we don't touch it.  YES, A COMMENT
        // THIS LONG is a sign of bad code -- so the action item is to
        // revisit this in ICU 3.0 and clean it up/fix it/remove it.
        if (U_SUCCESS(status) && (result != nullptr) && *actualLoc.getName() != 0) {
            U_LOCALE_BASED(locBased, *result);
            locBased.setLocaleIDs(actualLoc.getName(), actualLoc.getName());
        }
        return result;
    }
    else
#endif
    {
        return makeInstance(loc, kind, status);
    }
}

// -------------------------------------
enum { kKeyValueLenMax = 32 };

BreakIterator*
BreakIterator::makeInstance(const Locale& loc, int32_t kind, UErrorCode& status)
{

    if (U_FAILURE(status)) {
        return nullptr;
    }

    BreakIterator *result = nullptr;
    switch (kind) {
    case UBRK_CHARACTER:
        {
            UTRACE_ENTRY(UTRACE_UBRK_CREATE_CHARACTER);
            result = BreakIterator::buildInstance(loc, "grapheme", status);
            UTRACE_EXIT_STATUS(status);
        }
        break;
    case UBRK_WORD:
        {
            UTRACE_ENTRY(UTRACE_UBRK_CREATE_WORD);
            result = BreakIterator::buildInstance(loc, "word", status);
            UTRACE_EXIT_STATUS(status);
        }
        break;
    case UBRK_LINE:
        {
            char lb_lw[kKeyValueLenMax];
            UTRACE_ENTRY(UTRACE_UBRK_CREATE_LINE);
            uprv_strcpy(lb_lw, "line");
            UErrorCode kvStatus = U_ZERO_ERROR;
            auto value = loc.getKeywordValue<CharString>("lb", kvStatus);
            if (U_SUCCESS(kvStatus) && (value == "strict" || value == "normal" || value == "loose")) {
                uprv_strcat(lb_lw, "_");
                uprv_strcat(lb_lw, value.data());
            }
            // lw=phrase is only supported in Japanese and Korean
            if (uprv_strcmp(loc.getLanguage(), "ja") == 0 || uprv_strcmp(loc.getLanguage(), "ko") == 0) {
                value = loc.getKeywordValue<CharString>("lw", kvStatus);
                if (U_SUCCESS(kvStatus) && value == "phrase") {
                    uprv_strcat(lb_lw, "_");
                    uprv_strcat(lb_lw, value.data());
                }
            }
            result = BreakIterator::buildInstance(loc, lb_lw, status);

            UTRACE_DATA1(UTRACE_INFO, "lb_lw=%s", lb_lw);
            UTRACE_EXIT_STATUS(status);
        }
        break;
    case UBRK_SENTENCE:
        {
            UTRACE_ENTRY(UTRACE_UBRK_CREATE_SENTENCE);
            result = BreakIterator::buildInstance(loc, "sentence", status);
#if !UCONFIG_NO_FILTERED_BREAK_ITERATION
            char ssKeyValue[kKeyValueLenMax] = {0};
            UErrorCode kvStatus = U_ZERO_ERROR;
            int32_t kLen = loc.getKeywordValue("ss", ssKeyValue, kKeyValueLenMax, kvStatus);
            if (U_SUCCESS(kvStatus) && kLen > 0 && uprv_strcmp(ssKeyValue,"standard")==0) {
                FilteredBreakIteratorBuilder* fbiBuilder = FilteredBreakIteratorBuilder::createInstance(loc, kvStatus);
                if (U_SUCCESS(kvStatus)) {
                    result = fbiBuilder->build(result, status);
                    delete fbiBuilder;
                }
            }
#endif
            UTRACE_EXIT_STATUS(status);
        }
        break;
    case UBRK_TITLE:
        {
            UTRACE_ENTRY(UTRACE_UBRK_CREATE_TITLE);
            result = BreakIterator::buildInstance(loc, "title", status);
            UTRACE_EXIT_STATUS(status);
        }
        break;
    default:
        status = U_ILLEGAL_ARGUMENT_ERROR;
    }

    if (U_FAILURE(status)) {
        return nullptr;
    }

    return result;
}

Locale
BreakIterator::getLocale(ULocDataLocaleType type, UErrorCode& status) const {
    if (type == ULOC_REQUESTED_LOCALE) {
        return {requestLocale};
    }
    U_LOCALE_BASED(locBased, *this);
    return locBased.getLocale(type, status);
}

const char *
BreakIterator::getLocaleID(ULocDataLocaleType type, UErrorCode& status) const {
    if (type == ULOC_REQUESTED_LOCALE) {
        return requestLocale;
    }
    U_LOCALE_BASED(locBased, *this);
    return locBased.getLocaleID(type, status);
}


// This implementation of getRuleStatus is a do-nothing stub, here to
// provide a default implementation for any derived BreakIterator classes that
// do not implement it themselves.
int32_t BreakIterator::getRuleStatus() const {
    return 0;
}

// This implementation of getRuleStatusVec is a do-nothing stub, here to
// provide a default implementation for any derived BreakIterator classes that
// do not implement it themselves.
int32_t BreakIterator::getRuleStatusVec(int32_t *fillInVec, int32_t capacity, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return 0;
    }
    if (capacity < 1) {
        status = U_BUFFER_OVERFLOW_ERROR;
        return 1;
    }
    *fillInVec = 0;
    return 1;
}

BreakIterator::BreakIterator (const Locale& valid, const Locale& actual) {
  U_LOCALE_BASED(locBased, (*this));
  locBased.setLocaleIDs(valid, actual);
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_BREAK_ITERATION */

//eof
