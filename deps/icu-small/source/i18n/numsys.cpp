// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2010-2015, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*
*
* File NUMSYS.CPP
*
* Modification History:*
*   Date        Name        Description
*
********************************************************************************
*/

#include "unicode/utypes.h"
#include "unicode/localpointer.h"
#include "unicode/uchar.h"
#include "unicode/unistr.h"
#include "unicode/ures.h"
#include "unicode/ustring.h"
#include "unicode/uloc.h"
#include "unicode/schriter.h"
#include "unicode/numsys.h"
#include "cstring.h"
#include "uassert.h"
#include "ucln_in.h"
#include "umutex.h"
#include "uresimp.h"
#include "numsys_impl.h"

#if !UCONFIG_NO_FORMATTING

U_NAMESPACE_BEGIN

// Useful constants

#define DEFAULT_DIGITS UNICODE_STRING_SIMPLE("0123456789")
static const char gNumberingSystems[] = "numberingSystems";
static const char gNumberElements[] = "NumberElements";
static const char gDefault[] = "default";
static const char gNative[] = "native";
static const char gTraditional[] = "traditional";
static const char gFinance[] = "finance";
static const char gDesc[] = "desc";
static const char gRadix[] = "radix";
static const char gAlgorithmic[] = "algorithmic";
static const char gLatn[] = "latn";


UOBJECT_DEFINE_RTTI_IMPLEMENTATION(NumberingSystem)
UOBJECT_DEFINE_RTTI_IMPLEMENTATION(NumsysNameEnumeration)

    /**
     * Default Constructor.
     *
     * @draft ICU 4.2
     */

NumberingSystem::NumberingSystem() {
     radix = 10;
     algorithmic = false;
     UnicodeString defaultDigits = DEFAULT_DIGITS;
     desc.setTo(defaultDigits);
     uprv_strcpy(name,gLatn);
}

    /**
     * Copy constructor.
     * @draft ICU 4.2
     */

NumberingSystem::NumberingSystem(const NumberingSystem& other) 
:  UObject(other) {
    *this=other;
}

NumberingSystem* U_EXPORT2
NumberingSystem::createInstance(int32_t radix_in, UBool isAlgorithmic_in, const UnicodeString & desc_in, UErrorCode &status) {

    if (U_FAILURE(status)) {
        return nullptr;
    }

    if ( radix_in < 2 ) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }

    if ( !isAlgorithmic_in ) {
       if ( desc_in.countChar32() != radix_in ) {
           status = U_ILLEGAL_ARGUMENT_ERROR;
           return nullptr;
       }
    }

    LocalPointer<NumberingSystem> ns(new NumberingSystem(), status);
    if (U_FAILURE(status)) {
        return nullptr;
    }

    ns->setRadix(radix_in);
    ns->setDesc(desc_in);
    ns->setAlgorithmic(isAlgorithmic_in);
    ns->setName(nullptr);

    return ns.orphan();
}

NumberingSystem* U_EXPORT2
NumberingSystem::createInstance(const Locale & inLocale, UErrorCode& status) {

    if (U_FAILURE(status)) {
        return nullptr;
    }

    UBool nsResolved = true;
    UBool usingFallback = false;
    char buffer[ULOC_KEYWORDS_CAPACITY] = "";
    int32_t count = inLocale.getKeywordValue("numbers", buffer, sizeof(buffer), status);
    if (U_FAILURE(status) || status == U_STRING_NOT_TERMINATED_WARNING) {
        // the "numbers" keyword exceeds ULOC_KEYWORDS_CAPACITY; ignore and use default.
        count = 0;
        status = U_ZERO_ERROR;
    }
    if ( count > 0 ) { // @numbers keyword was specified in the locale
        U_ASSERT(count < ULOC_KEYWORDS_CAPACITY);
        buffer[count] = '\0'; // Make sure it is null terminated.
        if ( !uprv_strcmp(buffer,gDefault) || !uprv_strcmp(buffer,gNative) || 
             !uprv_strcmp(buffer,gTraditional) || !uprv_strcmp(buffer,gFinance)) {
            nsResolved = false;
        }
    } else {
        uprv_strcpy(buffer, gDefault);
        nsResolved = false;
    }

    if (!nsResolved) { // Resolve the numbering system ( default, native, traditional or finance ) into a "real" numbering system
        UErrorCode localStatus = U_ZERO_ERROR;
        LocalUResourceBundlePointer resource(ures_open(nullptr, inLocale.getName(), &localStatus));
        LocalUResourceBundlePointer numberElementsRes(ures_getByKey(resource.getAlias(), gNumberElements, nullptr, &localStatus));
        // Don't stomp on the catastrophic failure of OOM.
        if (localStatus == U_MEMORY_ALLOCATION_ERROR) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return nullptr;
        }
        while (!nsResolved) {
            localStatus = U_ZERO_ERROR;
            count = 0;
            const char16_t *nsName = ures_getStringByKeyWithFallback(numberElementsRes.getAlias(), buffer, &count, &localStatus);
            // Don't stomp on the catastrophic failure of OOM.
            if (localStatus == U_MEMORY_ALLOCATION_ERROR) {
                status = U_MEMORY_ALLOCATION_ERROR;
                return nullptr;
            }
            if ( count > 0 && count < ULOC_KEYWORDS_CAPACITY ) { // numbering system found
                u_UCharsToChars(nsName, buffer, count);
                buffer[count] = '\0'; // Make sure it is null terminated.
                nsResolved = true;
            } 

            if (!nsResolved) { // Fallback behavior per TR35 - traditional falls back to native, finance and native fall back to default
                if (!uprv_strcmp(buffer,gNative) || !uprv_strcmp(buffer,gFinance)) { 
                    uprv_strcpy(buffer,gDefault);
                } else if (!uprv_strcmp(buffer,gTraditional)) {
                    uprv_strcpy(buffer,gNative);
                } else { // If we get here we couldn't find even the default numbering system
                    usingFallback = true;
                    nsResolved = true;
                }
            }
        }
    }

    if (usingFallback) {
        status = U_USING_FALLBACK_WARNING;
        NumberingSystem *ns = new NumberingSystem();
        if (ns == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
        }
        return ns;
    } else {
        return NumberingSystem::createInstanceByName(buffer, status);
    }
 }

NumberingSystem* U_EXPORT2
NumberingSystem::createInstance(UErrorCode& status) {
    return NumberingSystem::createInstance(Locale::getDefault(), status);
}

NumberingSystem* U_EXPORT2
NumberingSystem::createInstanceByName(const char *name, UErrorCode& status) {
    int32_t radix = 10;
    int32_t algorithmic = 0;

    LocalUResourceBundlePointer numberingSystemsInfo(ures_openDirect(nullptr, gNumberingSystems, &status));
    LocalUResourceBundlePointer nsCurrent(ures_getByKey(numberingSystemsInfo.getAlias(), gNumberingSystems, nullptr, &status));
    LocalUResourceBundlePointer nsTop(ures_getByKey(nsCurrent.getAlias(), name, nullptr, &status));

    UnicodeString nsd = ures_getUnicodeStringByKey(nsTop.getAlias(), gDesc, &status);

    ures_getByKey(nsTop.getAlias(), gRadix, nsCurrent.getAlias(), &status);
    radix = ures_getInt(nsCurrent.getAlias(), &status);

    ures_getByKey(nsTop.getAlias(), gAlgorithmic, nsCurrent.getAlias(), &status);
    algorithmic = ures_getInt(nsCurrent.getAlias(), &status);

    UBool isAlgorithmic = ( algorithmic == 1 );

    if (U_FAILURE(status)) {
        // Don't stomp on the catastrophic failure of OOM.
        if (status != U_MEMORY_ALLOCATION_ERROR) {
            status = U_UNSUPPORTED_ERROR;
        }
        return nullptr;
    }

    LocalPointer<NumberingSystem> ns(NumberingSystem::createInstance(radix, isAlgorithmic, nsd, status), status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    ns->setName(name);
    return ns.orphan();
}

    /**
     * Destructor.
     * @draft ICU 4.2
     */
NumberingSystem::~NumberingSystem() {
}

int32_t NumberingSystem::getRadix() const {
    return radix;
}

UnicodeString NumberingSystem::getDescription() const {
    return desc;
}

const char * NumberingSystem::getName() const {
    return name;
}

void NumberingSystem::setRadix(int32_t r) {
    radix = r;
}

void NumberingSystem::setAlgorithmic(UBool c) {
    algorithmic = c;
}

void NumberingSystem::setDesc(const UnicodeString &d) {
    desc.setTo(d);
}
void NumberingSystem::setName(const char *n) {
    if ( n == nullptr ) {
        name[0] = (char) 0;
    } else {
        uprv_strncpy(name,n,kInternalNumSysNameCapacity);
        name[kInternalNumSysNameCapacity] = '\0'; // Make sure it is null terminated.
    }
}
UBool NumberingSystem::isAlgorithmic() const {
    return ( algorithmic );
}

namespace {

UVector* gNumsysNames = nullptr;
UInitOnce gNumSysInitOnce {};

U_CFUNC UBool U_CALLCONV numSysCleanup() {
    delete gNumsysNames;
    gNumsysNames = nullptr;
    gNumSysInitOnce.reset();
    return true;
}

U_CFUNC void initNumsysNames(UErrorCode &status) {
    U_ASSERT(gNumsysNames == nullptr);
    ucln_i18n_registerCleanup(UCLN_I18N_NUMSYS, numSysCleanup);

    // TODO: Simple array of UnicodeString objects, based on length of table resource?
    LocalPointer<UVector> numsysNames(new UVector(uprv_deleteUObject, nullptr, status), status);
    if (U_FAILURE(status)) {
        return;
    }

    UErrorCode rbstatus = U_ZERO_ERROR;
    UResourceBundle *numberingSystemsInfo = ures_openDirect(nullptr, "numberingSystems", &rbstatus);
    numberingSystemsInfo =
            ures_getByKey(numberingSystemsInfo, "numberingSystems", numberingSystemsInfo, &rbstatus);
    if (U_FAILURE(rbstatus)) {
        // Don't stomp on the catastrophic failure of OOM.
        if (rbstatus == U_MEMORY_ALLOCATION_ERROR) {
            status = rbstatus;
        } else {
            status = U_MISSING_RESOURCE_ERROR;
        }
        ures_close(numberingSystemsInfo);
        return;
    }

    while ( ures_hasNext(numberingSystemsInfo) && U_SUCCESS(status) ) {
        LocalUResourceBundlePointer nsCurrent(ures_getNextResource(numberingSystemsInfo, nullptr, &rbstatus));
        if (rbstatus == U_MEMORY_ALLOCATION_ERROR) {
            status = rbstatus; // we want to report OOM failure back to the caller.
            break;
        }
        const char *nsName = ures_getKey(nsCurrent.getAlias());
        LocalPointer<UnicodeString> newElem(new UnicodeString(nsName, -1, US_INV), status);
        numsysNames->adoptElement(newElem.orphan(), status);
    }

    ures_close(numberingSystemsInfo);
    if (U_SUCCESS(status)) {
        gNumsysNames = numsysNames.orphan();
    }
}

}   // end anonymous namespace

StringEnumeration* NumberingSystem::getAvailableNames(UErrorCode &status) {
    umtx_initOnce(gNumSysInitOnce, &initNumsysNames, status);
    LocalPointer<StringEnumeration> result(new NumsysNameEnumeration(status), status);
    return result.orphan();
}

NumsysNameEnumeration::NumsysNameEnumeration(UErrorCode& status) : pos(0) {
    (void)status;
}

const UnicodeString*
NumsysNameEnumeration::snext(UErrorCode& status) {
    if (U_SUCCESS(status) && (gNumsysNames != nullptr) && (pos < gNumsysNames->size())) {
        return (const UnicodeString*)gNumsysNames->elementAt(pos++);
    }
    return nullptr;
}

void
NumsysNameEnumeration::reset(UErrorCode& /*status*/) {
    pos=0;
}

int32_t
NumsysNameEnumeration::count(UErrorCode& /*status*/) const {
    return (gNumsysNames==nullptr) ? 0 : gNumsysNames->size();
}

NumsysNameEnumeration::~NumsysNameEnumeration() {
}
U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */

//eof
