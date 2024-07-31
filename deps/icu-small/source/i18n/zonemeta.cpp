// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2007-2014, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "zonemeta.h"

#include "unicode/timezone.h"
#include "unicode/ustring.h"
#include "unicode/putil.h"
#include "unicode/simpletz.h"
#include "unicode/strenum.h"
#include "umutex.h"
#include "uvector.h"
#include "cmemory.h"
#include "gregoimp.h"
#include "cstring.h"
#include "ucln_in.h"
#include "uassert.h"
#include "uresimp.h"
#include "uhash.h"
#include "olsontz.h"
#include "uinvchar.h"

static icu::UMutex gZoneMetaLock;

// CLDR Canonical ID mapping table
static UHashtable *gCanonicalIDCache = nullptr;
static icu::UInitOnce gCanonicalIDCacheInitOnce {};

// Metazone mapping table
static UHashtable *gOlsonToMeta = nullptr;
static icu::UInitOnce gOlsonToMetaInitOnce {};

// Available metazone IDs vector and table
static icu::UVector *gMetaZoneIDs = nullptr;
static UHashtable *gMetaZoneIDTable = nullptr;
static icu::UInitOnce gMetaZoneIDsInitOnce {};

// Country info vectors
static icu::UVector *gSingleZoneCountries = nullptr;
static icu::UVector *gMultiZonesCountries = nullptr;
static icu::UInitOnce gCountryInfoVectorsInitOnce {};

U_CDECL_BEGIN

/**
 * Cleanup callback func
 */
static UBool U_CALLCONV zoneMeta_cleanup()
{
    if (gCanonicalIDCache != nullptr) {
        uhash_close(gCanonicalIDCache);
        gCanonicalIDCache = nullptr;
    }
    gCanonicalIDCacheInitOnce.reset();

    if (gOlsonToMeta != nullptr) {
        uhash_close(gOlsonToMeta);
        gOlsonToMeta = nullptr;
    }
    gOlsonToMetaInitOnce.reset();

    if (gMetaZoneIDTable != nullptr) {
        uhash_close(gMetaZoneIDTable);
        gMetaZoneIDTable = nullptr;
    }
    // delete after closing gMetaZoneIDTable, because it holds
    // value objects held by the hashtable
    delete gMetaZoneIDs;
    gMetaZoneIDs = nullptr;
    gMetaZoneIDsInitOnce.reset();

    delete gSingleZoneCountries;
    gSingleZoneCountries = nullptr;
    delete gMultiZonesCountries;
    gMultiZonesCountries = nullptr;
    gCountryInfoVectorsInitOnce.reset();

    return true;
}

/**
 * Deleter for char16_t* string
 */
static void U_CALLCONV
deleteUCharString(void *obj) {
    char16_t *entry = (char16_t*)obj;
    uprv_free(entry);
}

/**
 * Deleter for OlsonToMetaMappingEntry
 */
static void U_CALLCONV
deleteOlsonToMetaMappingEntry(void *obj) {
    icu::OlsonToMetaMappingEntry *entry = (icu::OlsonToMetaMappingEntry*)obj;
    delete entry;
}

U_CDECL_END

U_NAMESPACE_BEGIN

#define ZID_KEY_MAX 128

static const char gMetaZones[]          = "metaZones";
static const char gMetazoneInfo[]       = "metazoneInfo";
static const char gMapTimezonesTag[]    = "mapTimezones";

static const char gKeyTypeData[]        = "keyTypeData";
static const char gTypeAliasTag[]       = "typeAlias";
static const char gTypeMapTag[]         = "typeMap";
static const char gTimezoneTag[]        = "timezone";
static const char gIanaMapTag[]         = "ianaMap";

static const char gPrimaryZonesTag[]    = "primaryZones";

static const char gWorldTag[]           = "001";

static const char16_t gWorld[] = {0x30, 0x30, 0x31, 0x00}; // "001"

static const char16_t gDefaultFrom[] = {0x31, 0x39, 0x37, 0x30, 0x2D, 0x30, 0x31, 0x2D, 0x30, 0x31,
                                     0x20, 0x30, 0x30, 0x3A, 0x30, 0x30, 0x00}; // "1970-01-01 00:00"
static const char16_t gDefaultTo[]   = {0x39, 0x39, 0x39, 0x39, 0x2D, 0x31, 0x32, 0x2D, 0x33, 0x31,
                                     0x20, 0x32, 0x33, 0x3A, 0x35, 0x39, 0x00}; // "9999-12-31 23:59"

static const char16_t gCustomTzPrefix[]    = {0x47, 0x4D, 0x54, 0};    // "GMT"

#define ASCII_DIGIT(c) (((c)>=0x30 && (c)<=0x39) ? (c)-0x30 : -1)

/*
 * Convert a date string used by metazone mappings to UDate.
 * The format used by CLDR metazone mapping is "yyyy-MM-dd HH:mm".
 */
static UDate
parseDate (const char16_t *text, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return 0;
    }
    int32_t len = u_strlen(text);
    if (len != 16 && len != 10) {
        // It must be yyyy-MM-dd HH:mm (length 16) or yyyy-MM-dd (length 10)
        status = U_INVALID_FORMAT_ERROR;
        return 0;
    }

    int32_t year = 0, month = 0, day = 0, hour = 0, min = 0, n;
    int32_t idx;

    // "yyyy" (0 - 3)
    for (idx = 0; idx <= 3 && U_SUCCESS(status); idx++) {
        n = ASCII_DIGIT((int32_t)text[idx]);
        if (n >= 0) {
            year = 10*year + n;
        } else {
            status = U_INVALID_FORMAT_ERROR;
        }
    }
    // "MM" (5 - 6)
    for (idx = 5; idx <= 6 && U_SUCCESS(status); idx++) {
        n = ASCII_DIGIT((int32_t)text[idx]);
        if (n >= 0) {
            month = 10*month + n;
        } else {
            status = U_INVALID_FORMAT_ERROR;
        }
    }
    // "dd" (8 - 9)
    for (idx = 8; idx <= 9 && U_SUCCESS(status); idx++) {
        n = ASCII_DIGIT((int32_t)text[idx]);
        if (n >= 0) {
            day = 10*day + n;
        } else {
            status = U_INVALID_FORMAT_ERROR;
        }
    }
    if (len == 16) {
        // "HH" (11 - 12)
        for (idx = 11; idx <= 12 && U_SUCCESS(status); idx++) {
            n = ASCII_DIGIT((int32_t)text[idx]);
            if (n >= 0) {
                hour = 10*hour + n;
            } else {
                status = U_INVALID_FORMAT_ERROR;
            }
        }
        // "mm" (14 - 15)
        for (idx = 14; idx <= 15 && U_SUCCESS(status); idx++) {
            n = ASCII_DIGIT((int32_t)text[idx]);
            if (n >= 0) {
                min = 10*min + n;
            } else {
                status = U_INVALID_FORMAT_ERROR;
            }
        }
    }

    if (U_SUCCESS(status)) {
        UDate date = Grego::fieldsToDay(year, month - 1, day) * U_MILLIS_PER_DAY
            + hour * U_MILLIS_PER_HOUR + min * U_MILLIS_PER_MINUTE;
        return date;
    }
    return 0;
}

static void U_CALLCONV initCanonicalIDCache(UErrorCode &status) {
    gCanonicalIDCache = uhash_open(uhash_hashUChars, uhash_compareUChars, nullptr, &status);
    if (gCanonicalIDCache == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
    }
    if (U_FAILURE(status)) {
        gCanonicalIDCache = nullptr;
    }
    // No key/value deleters - keys/values are from a resource bundle
    ucln_i18n_registerCleanup(UCLN_I18N_ZONEMETA, zoneMeta_cleanup);
}


const char16_t* U_EXPORT2
ZoneMeta::getCanonicalCLDRID(const UnicodeString &tzid, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }

    if (tzid.isBogus() || tzid.length() > ZID_KEY_MAX) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }

    // Checking the cached results
    umtx_initOnce(gCanonicalIDCacheInitOnce, &initCanonicalIDCache, status);
    if (U_FAILURE(status)) {
        return nullptr;
    }

    const char16_t *canonicalID = nullptr;

    UErrorCode tmpStatus = U_ZERO_ERROR;
    char16_t utzid[ZID_KEY_MAX + 1];
    tzid.extract(utzid, ZID_KEY_MAX + 1, tmpStatus);
    U_ASSERT(tmpStatus == U_ZERO_ERROR);    // we checked the length of tzid already

    if (!uprv_isInvariantUString(utzid, -1)) {
        // All of known tz IDs are only containing ASCII invariant characters.
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }

    // Check if it was already cached
    umtx_lock(&gZoneMetaLock);
    {
        canonicalID = (const char16_t *)uhash_get(gCanonicalIDCache, utzid);
    }
    umtx_unlock(&gZoneMetaLock);

    if (canonicalID != nullptr) {
        return canonicalID;
    }

    // If not, resolve CLDR canonical ID with resource data
    UBool isInputCanonical = false;
    char id[ZID_KEY_MAX + 1];
    tzid.extract(0, 0x7fffffff, id, UPRV_LENGTHOF(id), US_INV);

    // replace '/' with ':'
    char *p = id;
    while (*p++) {
        if (*p == '/') {
            *p = ':';
        }
    }

    UResourceBundle *top = ures_openDirect(nullptr, gKeyTypeData, &tmpStatus);
    UResourceBundle *rb = ures_getByKey(top, gTypeMapTag, nullptr, &tmpStatus);
    ures_getByKey(rb, gTimezoneTag, rb, &tmpStatus);
    ures_getByKey(rb, id, rb, &tmpStatus);
    if (U_SUCCESS(tmpStatus)) {
        // type entry (canonical) found
        // the input is the canonical ID. resolve to const char16_t*
        canonicalID = TimeZone::findID(tzid);
        isInputCanonical = true;
    }

    if (canonicalID == nullptr) {
        // If a map element not found, then look for an alias
        tmpStatus = U_ZERO_ERROR;
        ures_getByKey(top, gTypeAliasTag, rb, &tmpStatus);
        ures_getByKey(rb, gTimezoneTag, rb, &tmpStatus);
        const char16_t *canonical = ures_getStringByKey(rb,id,nullptr,&tmpStatus);
        if (U_SUCCESS(tmpStatus)) {
            // canonical map found
            canonicalID = canonical;
        }

        if (canonicalID == nullptr) {
            // Dereference the input ID using the tz data
            const char16_t *derefer = TimeZone::dereferOlsonLink(tzid);
            if (derefer == nullptr) {
                status = U_ILLEGAL_ARGUMENT_ERROR;
            } else {
                int32_t len = u_strlen(derefer);
                u_UCharsToChars(derefer,id,len);
                id[len] = (char) 0; // Make sure it is null terminated.

                // replace '/' with ':'
                char *q = id;
                while (*q++) {
                    if (*q == '/') {
                        *q = ':';
                    }
                }

                // If a dereference turned something up then look for an alias.
                // rb still points to the alias table, so we don't have to go looking
                // for it.
                tmpStatus = U_ZERO_ERROR;
                canonical = ures_getStringByKey(rb,id,nullptr,&tmpStatus);
                if (U_SUCCESS(tmpStatus)) {
                    // canonical map for the dereferenced ID found
                    canonicalID = canonical;
                } else {
                    canonicalID = derefer;
                    isInputCanonical = true;
                }
            }
        }
    }
    ures_close(rb);
    ures_close(top);

    if (U_SUCCESS(status)) {
        U_ASSERT(canonicalID != nullptr);  // canocanilD must be non-nullptr here

        // Put the resolved canonical ID to the cache
        umtx_lock(&gZoneMetaLock);
        {
            const char16_t* idInCache = (const char16_t *)uhash_get(gCanonicalIDCache, utzid);
            if (idInCache == nullptr) {
                const char16_t* key = ZoneMeta::findTimeZoneID(tzid);
                U_ASSERT(key != nullptr);
                if (key != nullptr) {
                    idInCache = (const char16_t *)uhash_put(gCanonicalIDCache, (void *)key, (void *)canonicalID, &status);
                    U_ASSERT(idInCache == nullptr);
                }
            }
            if (U_SUCCESS(status) && isInputCanonical) {
                // Also put canonical ID itself into the cache if not exist
                const char16_t *canonicalInCache = (const char16_t*)uhash_get(gCanonicalIDCache, canonicalID);
                if (canonicalInCache == nullptr) {
                    canonicalInCache = (const char16_t *)uhash_put(gCanonicalIDCache, (void *)canonicalID, (void *)canonicalID, &status);
                    U_ASSERT(canonicalInCache == nullptr);
                }
            }
        }
        umtx_unlock(&gZoneMetaLock);
    }

    return canonicalID;
}

UnicodeString& U_EXPORT2
ZoneMeta::getCanonicalCLDRID(const UnicodeString &tzid, UnicodeString &systemID, UErrorCode& status) {
    const char16_t *canonicalID = getCanonicalCLDRID(tzid, status);
    if (U_FAILURE(status) || canonicalID == nullptr) {
        systemID.setToBogus();
        return systemID;
    }
    systemID.setTo(true, canonicalID, -1);
    return systemID;
}

const char16_t* U_EXPORT2
ZoneMeta::getCanonicalCLDRID(const TimeZone& tz) {
    if (dynamic_cast<const OlsonTimeZone *>(&tz) != nullptr) {
        // short cut for OlsonTimeZone
        const OlsonTimeZone *otz = (const OlsonTimeZone*)&tz;
        return otz->getCanonicalID();
    }
    UErrorCode status = U_ZERO_ERROR;
    UnicodeString tzID;
    return getCanonicalCLDRID(tz.getID(tzID), status);
}

UnicodeString& U_EXPORT2
ZoneMeta::getIanaID(const UnicodeString& tzid, UnicodeString& ianaID, UErrorCode& status) {
    // First, get CLDR canonical ID
    const char16_t *canonicalID = getCanonicalCLDRID(tzid, status);
    if (U_FAILURE(status) || canonicalID == nullptr) {
        ianaID.setToBogus();
        return ianaID;
    }
    // Find IANA mapping if any.
    UErrorCode tmpStatus = U_ZERO_ERROR;
    UnicodeString tmpKey(canonicalID);
    tmpKey.findAndReplace(UnicodeString("/"), UnicodeString(":"));
    char keyBuf[ZID_KEY_MAX + 1];
    /* int32_t keyLen = */ tmpKey.extract(0, tmpKey.length(), keyBuf, sizeof(keyBuf), US_INV);

    StackUResourceBundle r;
    ures_openDirectFillIn(r.getAlias(), nullptr, gKeyTypeData, &tmpStatus);
    ures_getByKey(r.getAlias(), gIanaMapTag, r.getAlias(), &tmpStatus);
    ures_getByKey(r.getAlias(), gTimezoneTag, r.getAlias(), &tmpStatus);
    int32_t tmpLen = 0;
    const char16_t* tmpIana = ures_getStringByKey(r.getAlias(), keyBuf, &tmpLen, &tmpStatus);
    if (U_SUCCESS(tmpStatus)) {
        ianaID.setTo(true, tmpIana, -1);
    } else {
        ianaID.setTo(true, canonicalID, -1);
    }
    return ianaID;
}

static void U_CALLCONV countryInfoVectorsInit(UErrorCode &status) {
    // Create empty vectors
    // No deleters for these UVectors, it's a reference to a resource bundle string.
    gSingleZoneCountries = new UVector(nullptr, uhash_compareUChars, status);
    if (gSingleZoneCountries == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
    }
    gMultiZonesCountries = new UVector(nullptr, uhash_compareUChars, status);
    if (gMultiZonesCountries == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
    }

    if (U_FAILURE(status)) {
        delete gSingleZoneCountries;
        delete gMultiZonesCountries;
        gSingleZoneCountries = nullptr;
        gMultiZonesCountries  = nullptr;
    }
    ucln_i18n_registerCleanup(UCLN_I18N_ZONEMETA, zoneMeta_cleanup);
}


UnicodeString& U_EXPORT2
ZoneMeta::getCanonicalCountry(const UnicodeString &tzid, UnicodeString &country, UBool *isPrimary /* = nullptr */) {
    if (isPrimary != nullptr) {
        *isPrimary = false;
    }

    const char16_t *region = TimeZone::getRegion(tzid);
    if (region != nullptr && u_strcmp(gWorld, region) != 0) {
        country.setTo(region, -1);
    } else {
        country.setToBogus();
        return country;
    }

    if (isPrimary != nullptr) {
        char regionBuf[] = {0, 0, 0};

        // Checking the cached results
        UErrorCode status = U_ZERO_ERROR;
        umtx_initOnce(gCountryInfoVectorsInitOnce, &countryInfoVectorsInit, status);
        if (U_FAILURE(status)) {
            return country;
        }

        // Check if it was already cached
        UBool cached = false;
        UBool singleZone = false;
        umtx_lock(&gZoneMetaLock);
        {
            singleZone = cached = gSingleZoneCountries->contains((void*)region);
            if (!cached) {
                cached = gMultiZonesCountries->contains((void*)region);
            }
        }
        umtx_unlock(&gZoneMetaLock);

        if (!cached) {
            // We need to go through all zones associated with the region.
            // This is relatively heavy operation.

            U_ASSERT(u_strlen(region) == 2);

            u_UCharsToChars(region, regionBuf, 2);

            StringEnumeration *ids = TimeZone::createTimeZoneIDEnumeration(UCAL_ZONE_TYPE_CANONICAL_LOCATION, regionBuf, nullptr, status);
            int32_t idsLen = ids->count(status);
            if (U_SUCCESS(status) && idsLen == 1) {
                // only the single zone is available for the region
                singleZone = true;
            }
            delete ids;

            // Cache the result
            umtx_lock(&gZoneMetaLock);
            {
                UErrorCode ec = U_ZERO_ERROR;
                if (singleZone) {
                    if (!gSingleZoneCountries->contains((void*)region)) {
                        gSingleZoneCountries->addElement((void*)region, ec);
                    }
                } else {
                    if (!gMultiZonesCountries->contains((void*)region)) {
                        gMultiZonesCountries->addElement((void*)region, ec);
                    }
                }
            }
            umtx_unlock(&gZoneMetaLock);
        }

        if (singleZone) {
            *isPrimary = true;
        } else {
            // Note: We may cache the primary zone map in future.

            // Even a country has multiple zones, one of them might be
            // dominant and treated as a primary zone
            int32_t idLen = 0;
            if (regionBuf[0] == 0) {
                u_UCharsToChars(region, regionBuf, 2);
            }

            UResourceBundle *rb = ures_openDirect(nullptr, gMetaZones, &status);
            ures_getByKey(rb, gPrimaryZonesTag, rb, &status);
            const char16_t *primaryZone = ures_getStringByKey(rb, regionBuf, &idLen, &status);
            if (U_SUCCESS(status)) {
                if (tzid.compare(primaryZone, idLen) == 0) {
                    *isPrimary = true;
                } else {
                    // The given ID might not be a canonical ID
                    UnicodeString canonicalID;
                    TimeZone::getCanonicalID(tzid, canonicalID, status);
                    if (U_SUCCESS(status) && canonicalID.compare(primaryZone, idLen) == 0) {
                        *isPrimary = true;
                    }
                }
            }
            ures_close(rb);
        }
    }

    return country;
}

UnicodeString& U_EXPORT2
ZoneMeta::getMetazoneID(const UnicodeString &tzid, UDate date, UnicodeString &result) {
    UBool isSet = false;
    const UVector *mappings = getMetazoneMappings(tzid);
    if (mappings != nullptr) {
        for (int32_t i = 0; i < mappings->size(); i++) {
            OlsonToMetaMappingEntry *mzm = (OlsonToMetaMappingEntry*)mappings->elementAt(i);
            if (mzm->from <= date && mzm->to > date) {
                result.setTo(mzm->mzid, -1);
                isSet = true;
                break;
            }
        }
    }
    if (!isSet) {
        result.setToBogus();
    }
    return result;
}

static void U_CALLCONV olsonToMetaInit(UErrorCode &status) {
    U_ASSERT(gOlsonToMeta == nullptr);
    ucln_i18n_registerCleanup(UCLN_I18N_ZONEMETA, zoneMeta_cleanup);
    gOlsonToMeta = uhash_open(uhash_hashUChars, uhash_compareUChars, nullptr, &status);
    if (U_FAILURE(status)) {
        gOlsonToMeta = nullptr;
    } else {
        uhash_setKeyDeleter(gOlsonToMeta, deleteUCharString);
        uhash_setValueDeleter(gOlsonToMeta, uprv_deleteUObject);
    }
}


const UVector* U_EXPORT2
ZoneMeta::getMetazoneMappings(const UnicodeString &tzid) {
    UErrorCode status = U_ZERO_ERROR;
    char16_t tzidUChars[ZID_KEY_MAX + 1];
    tzid.extract(tzidUChars, ZID_KEY_MAX + 1, status);
    if (U_FAILURE(status) || status == U_STRING_NOT_TERMINATED_WARNING) {
        return nullptr;
    }

    umtx_initOnce(gOlsonToMetaInitOnce, &olsonToMetaInit, status);
    if (U_FAILURE(status)) {
        return nullptr;
    }

    // get the mapping from cache
    const UVector *result = nullptr;

    umtx_lock(&gZoneMetaLock);
    {
        result = (UVector*) uhash_get(gOlsonToMeta, tzidUChars);
    }
    umtx_unlock(&gZoneMetaLock);

    if (result != nullptr) {
        return result;
    }

    // miss the cache - create new one
    UVector *tmpResult = createMetazoneMappings(tzid);
    if (tmpResult == nullptr) {
        // not available
        return nullptr;
    }

    // put the new one into the cache
    umtx_lock(&gZoneMetaLock);
    {
        // make sure it's already created
        result = (UVector*) uhash_get(gOlsonToMeta, tzidUChars);
        if (result == nullptr) {
            // add the one just created
            int32_t tzidLen = tzid.length() + 1;
            char16_t *key = (char16_t*)uprv_malloc(tzidLen * sizeof(char16_t));
            if (key == nullptr) {
                // memory allocation error..  just return nullptr
                result = nullptr;
                delete tmpResult;
            } else {
                tzid.extract(key, tzidLen, status);
                uhash_put(gOlsonToMeta, key, tmpResult, &status);
                if (U_FAILURE(status)) {
                    // delete the mapping
                    result = nullptr;
                    delete tmpResult;
                } else {
                    result = tmpResult;
                }
            }
        } else {
            // another thread already put the one
            delete tmpResult;
        }
    }
    umtx_unlock(&gZoneMetaLock);

    return result;
}

UVector*
ZoneMeta::createMetazoneMappings(const UnicodeString &tzid) {
    LocalPointer <UVector> mzMappings;
    UErrorCode status = U_ZERO_ERROR;

    UnicodeString canonicalID;
    UResourceBundle *rb = ures_openDirect(nullptr, gMetaZones, &status);
    ures_getByKey(rb, gMetazoneInfo, rb, &status);
    getCanonicalCLDRID(tzid, canonicalID, status);

    if (U_SUCCESS(status)) {
        char tzKey[ZID_KEY_MAX + 1];
        int32_t tzKeyLen = canonicalID.extract(0, canonicalID.length(), tzKey, sizeof(tzKey), US_INV);
        tzKey[tzKeyLen] = 0;

        // tzid keys are using ':' as separators
        char *p = tzKey;
        while (*p) {
            if (*p == '/') {
                *p = ':';
            }
            p++;
        }

        ures_getByKey(rb, tzKey, rb, &status);

        if (U_SUCCESS(status)) {
            UResourceBundle *mz = nullptr;
            while (ures_hasNext(rb)) {
                mz = ures_getNextResource(rb, mz, &status);

                const char16_t *mz_name = ures_getStringByIndex(mz, 0, nullptr, &status);
                const char16_t *mz_from = gDefaultFrom;
                const char16_t *mz_to = gDefaultTo;

                if (ures_getSize(mz) == 3) {
                    mz_from = ures_getStringByIndex(mz, 1, nullptr, &status);
                    mz_to   = ures_getStringByIndex(mz, 2, nullptr, &status);
                }

                if(U_FAILURE(status)){
                    status = U_ZERO_ERROR;
                    continue;
                }
                // We do not want to use SimpleDateformat to parse boundary dates,
                // because this code could be triggered by the initialization code
                // used by SimpleDateFormat.
                UDate from = parseDate(mz_from, status);
                UDate to = parseDate(mz_to, status);
                if (U_FAILURE(status)) {
                    status = U_ZERO_ERROR;
                    continue;
                }

                LocalPointer<OlsonToMetaMappingEntry> entry(new OlsonToMetaMappingEntry, status);
                if (U_FAILURE(status)) {
                    break;
                }
                entry->mzid = mz_name;
                entry->from = from;
                entry->to = to;

                if (mzMappings.isNull()) {
                    mzMappings.adoptInsteadAndCheckErrorCode(
                        new UVector(deleteOlsonToMetaMappingEntry, nullptr, status), status);
                    if (U_FAILURE(status)) {
                        break;
                    }
                }

                mzMappings->adoptElement(entry.orphan(), status);
                if (U_FAILURE(status)) {
                    break;
                }
            }
            ures_close(mz);
        }
    }
    ures_close(rb);
    return U_SUCCESS(status) ? mzMappings.orphan() : nullptr;
}

UnicodeString& U_EXPORT2
ZoneMeta::getZoneIdByMetazone(const UnicodeString &mzid, const UnicodeString &region, UnicodeString &result) {
    UErrorCode status = U_ZERO_ERROR;
    const char16_t *tzid = nullptr;
    int32_t tzidLen = 0;
    char keyBuf[ZID_KEY_MAX + 1];
    int32_t keyLen = 0;

    if (mzid.isBogus() || mzid.length() > ZID_KEY_MAX) {
        result.setToBogus();
        return result;
    }

    keyLen = mzid.extract(0, mzid.length(), keyBuf, ZID_KEY_MAX + 1, US_INV);
    keyBuf[keyLen] = 0;

    UResourceBundle *rb = ures_openDirect(nullptr, gMetaZones, &status);
    ures_getByKey(rb, gMapTimezonesTag, rb, &status);
    ures_getByKey(rb, keyBuf, rb, &status);

    if (U_SUCCESS(status)) {
        // check region mapping
        if (region.length() == 2 || region.length() == 3) {
            keyLen = region.extract(0, region.length(), keyBuf, ZID_KEY_MAX + 1, US_INV);
            keyBuf[keyLen] = 0;
            tzid = ures_getStringByKey(rb, keyBuf, &tzidLen, &status);
            if (status == U_MISSING_RESOURCE_ERROR) {
                status = U_ZERO_ERROR;
            }
        }
        if (U_SUCCESS(status) && tzid == nullptr) {
            // try "001"
            tzid = ures_getStringByKey(rb, gWorldTag, &tzidLen, &status);
        }
    }
    ures_close(rb);

    if (tzid == nullptr) {
        result.setToBogus();
    } else {
        result.setTo(tzid, tzidLen);
    }

    return result;
}

static void U_CALLCONV initAvailableMetaZoneIDs () {
    U_ASSERT(gMetaZoneIDs == nullptr);
    U_ASSERT(gMetaZoneIDTable == nullptr);
    ucln_i18n_registerCleanup(UCLN_I18N_ZONEMETA, zoneMeta_cleanup);

    UErrorCode status = U_ZERO_ERROR;
    gMetaZoneIDTable = uhash_open(uhash_hashUnicodeString, uhash_compareUnicodeString, nullptr, &status);
    if (U_FAILURE(status) || gMetaZoneIDTable == nullptr) {
        gMetaZoneIDTable = nullptr;
        return;
    }
    uhash_setKeyDeleter(gMetaZoneIDTable, uprv_deleteUObject);
    // No valueDeleter, because the vector maintain the value objects
    gMetaZoneIDs = new UVector(nullptr, uhash_compareUChars, status);
    if (U_FAILURE(status) || gMetaZoneIDs == nullptr) {
        delete gMetaZoneIDs;
        gMetaZoneIDs = nullptr;
        uhash_close(gMetaZoneIDTable);
        gMetaZoneIDTable = nullptr;
        return;
    }
    gMetaZoneIDs->setDeleter(uprv_free);

    UResourceBundle *rb = ures_openDirect(nullptr, gMetaZones, &status);
    UResourceBundle *bundle = ures_getByKey(rb, gMapTimezonesTag, nullptr, &status);
    StackUResourceBundle res;
    while (U_SUCCESS(status) && ures_hasNext(bundle)) {
        ures_getNextResource(bundle, res.getAlias(), &status);
        if (U_FAILURE(status)) {
            break;
        }
        const char *mzID = ures_getKey(res.getAlias());
        int32_t len = static_cast<int32_t>(uprv_strlen(mzID));
        LocalMemory<char16_t> uMzID((char16_t*)uprv_malloc(sizeof(char16_t) * (len + 1)));
        if (uMzID.isNull()) {
            status = U_MEMORY_ALLOCATION_ERROR;
            break;
        }
        u_charsToUChars(mzID, uMzID.getAlias(), len);
        uMzID[len] = 0;
        LocalPointer<UnicodeString> usMzID(new UnicodeString(uMzID.getAlias()), status);
        if (U_FAILURE(status)) {
            break;
        }
        if (uhash_get(gMetaZoneIDTable, usMzID.getAlias()) == nullptr) {
            // Note: gMetaZoneIDTable adopts its keys, but not its values.
            //       gMetaZoneIDs adopts its values.
            uhash_put(gMetaZoneIDTable, usMzID.orphan(), uMzID.getAlias(), &status);
            gMetaZoneIDs->adoptElement(uMzID.orphan(), status);
        }
    }
    ures_close(bundle);
    ures_close(rb);

    if (U_FAILURE(status)) {
        uhash_close(gMetaZoneIDTable);
        delete gMetaZoneIDs;
        gMetaZoneIDTable = nullptr;
        gMetaZoneIDs = nullptr;
    }
}

const UVector*
ZoneMeta::getAvailableMetazoneIDs() {
    umtx_initOnce(gMetaZoneIDsInitOnce, &initAvailableMetaZoneIDs);
    return gMetaZoneIDs;
}

const char16_t*
ZoneMeta::findMetaZoneID(const UnicodeString& mzid) {
    umtx_initOnce(gMetaZoneIDsInitOnce, &initAvailableMetaZoneIDs);
    if (gMetaZoneIDTable == nullptr) {
        return nullptr;
    }
    return (const char16_t*)uhash_get(gMetaZoneIDTable, &mzid);
}

const char16_t*
ZoneMeta::findTimeZoneID(const UnicodeString& tzid) {
    return TimeZone::findID(tzid);
}


TimeZone*
ZoneMeta::createCustomTimeZone(int32_t offset) {
    UBool negative = false;
    int32_t tmp = offset;
    if (offset < 0) {
        negative = true;
        tmp = -offset;
    }
    uint8_t hour, min, sec;

    tmp /= 1000;
    sec = static_cast<uint8_t>(tmp % 60);
    tmp /= 60;
    min = static_cast<uint8_t>(tmp % 60);
    hour = static_cast<uint8_t>(tmp / 60);

    UnicodeString zid;
    formatCustomID(hour, min, sec, negative, zid);
    return new SimpleTimeZone(offset, zid);
}

UnicodeString&
ZoneMeta::formatCustomID(uint8_t hour, uint8_t min, uint8_t sec, UBool negative, UnicodeString& id) {
    // Create normalized time zone ID - GMT[+|-]HH:mm[:ss]
    id.setTo(gCustomTzPrefix, -1);
    if (hour != 0 || min != 0) {
        if (negative) {
          id.append((char16_t)0x2D);    // '-'
        } else {
          id.append((char16_t)0x2B);    // '+'
        }
        // Always use US-ASCII digits
        id.append((char16_t)(0x30 + (hour%100)/10));
        id.append((char16_t)(0x30 + (hour%10)));
        id.append((char16_t)0x3A);    // ':'
        id.append((char16_t)(0x30 + (min%100)/10));
        id.append((char16_t)(0x30 + (min%10)));
        if (sec != 0) {
          id.append((char16_t)0x3A);    // ':'
          id.append((char16_t)(0x30 + (sec%100)/10));
          id.append((char16_t)(0x30 + (sec%10)));
        }
    }
    return id;
}

const char16_t*
ZoneMeta::getShortID(const TimeZone& tz) {
    const char16_t* canonicalID = nullptr;
    if (dynamic_cast<const OlsonTimeZone *>(&tz) != nullptr) {
        // short cut for OlsonTimeZone
        const OlsonTimeZone *otz = (const OlsonTimeZone*)&tz;
        canonicalID = otz->getCanonicalID();
    }
    if (canonicalID == nullptr) {
        return nullptr;
    }
    return getShortIDFromCanonical(canonicalID);
}

const char16_t*
ZoneMeta::getShortID(const UnicodeString& id) {
    UErrorCode status = U_ZERO_ERROR;
    const char16_t* canonicalID = ZoneMeta::getCanonicalCLDRID(id, status);
    if (U_FAILURE(status) || canonicalID == nullptr) {
        return nullptr;
    }
    return ZoneMeta::getShortIDFromCanonical(canonicalID);
}

const char16_t*
ZoneMeta::getShortIDFromCanonical(const char16_t* canonicalID) {
    const char16_t* shortID = nullptr;
    int32_t len = u_strlen(canonicalID);
    char tzidKey[ZID_KEY_MAX + 1];

    u_UCharsToChars(canonicalID, tzidKey, len);
    tzidKey[len] = (char) 0; // Make sure it is null terminated.

    // replace '/' with ':'
    char *p = tzidKey;
    while (*p++) {
        if (*p == '/') {
            *p = ':';
        }
    }

    UErrorCode status = U_ZERO_ERROR;
    UResourceBundle *rb = ures_openDirect(nullptr, gKeyTypeData, &status);
    ures_getByKey(rb, gTypeMapTag, rb, &status);
    ures_getByKey(rb, gTimezoneTag, rb, &status);
    shortID = ures_getStringByKey(rb, tzidKey, nullptr, &status);
    ures_close(rb);

    return shortID;
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */
