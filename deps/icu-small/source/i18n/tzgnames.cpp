// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2011-2016, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "tzgnames.h"

#include "unicode/basictz.h"
#include "unicode/locdspnm.h"
#include "unicode/rbtz.h"
#include "unicode/simpleformatter.h"
#include "unicode/simpletz.h"
#include "unicode/strenum.h"
#include "unicode/vtzone.h"

#include "charstr.h"
#include "cmemory.h"
#include "cstring.h"
#include "mutex.h"
#include "uhash.h"
#include "uassert.h"
#include "umutex.h"
#include "ulocimp.h"
#include "uresimp.h"
#include "ureslocs.h"
#include "zonemeta.h"
#include "tznames_impl.h"
#include "olsontz.h"
#include "ucln_in.h"

U_NAMESPACE_BEGIN

#define ZID_KEY_MAX  128

static const char gZoneStrings[]                = "zoneStrings";

static const char gRegionFormatTag[]            = "regionFormat";
static const char gFallbackFormatTag[]          = "fallbackFormat";

static const char16_t gEmpty[]                     = {0x00};

static const char16_t gDefRegionPattern[]          = {0x7B, 0x30, 0x7D, 0x00}; // "{0}"
static const char16_t gDefFallbackPattern[]        = {0x7B, 0x31, 0x7D, 0x20, 0x28, 0x7B, 0x30, 0x7D, 0x29, 0x00}; // "{1} ({0})"

static const double kDstCheckRange = static_cast<double>(184) * U_MILLIS_PER_DAY;



U_CDECL_BEGIN

typedef struct PartialLocationKey {
    const char16_t* tzID;
    const char16_t* mzID;
    UBool isLong;
} PartialLocationKey;

/**
 * Hash function for partial location name hash key
 */
static int32_t U_CALLCONV
hashPartialLocationKey(const UHashTok key) {
    // <tzID>&<mzID>#[L|S]
    PartialLocationKey *p = (PartialLocationKey *)key.pointer;
    UnicodeString str(p->tzID);
    str.append((char16_t)0x26)
        .append(p->mzID, -1)
        .append((char16_t)0x23)
        .append((char16_t)(p->isLong ? 0x4C : 0x53));
    return str.hashCode();
}

/**
 * Comparer for partial location name hash key
 */
static UBool U_CALLCONV
comparePartialLocationKey(const UHashTok key1, const UHashTok key2) {
    PartialLocationKey *p1 = (PartialLocationKey *)key1.pointer;
    PartialLocationKey *p2 = (PartialLocationKey *)key2.pointer;

    if (p1 == p2) {
        return true;
    }
    if (p1 == nullptr || p2 == nullptr) {
        return false;
    }
    // We just check identity of tzID/mzID
    return (p1->tzID == p2->tzID && p1->mzID == p2->mzID && p1->isLong == p2->isLong);
}

/**
 * Deleter for GNameInfo
 */
static void U_CALLCONV
deleteGNameInfo(void *obj) {
    uprv_free(obj);
}

/**
 * GNameInfo stores zone name information in the local trie
 */
typedef struct GNameInfo {
    UTimeZoneGenericNameType    type;
    const char16_t*                tzID;
} ZNameInfo;

/**
 * GMatchInfo stores zone name match information used by find method
 */
typedef struct GMatchInfo {
    const GNameInfo*    gnameInfo;
    int32_t             matchLength;
    UTimeZoneFormatTimeType   timeType;
} ZMatchInfo;

U_CDECL_END

// ---------------------------------------------------
// The class stores time zone generic name match information
// ---------------------------------------------------
class TimeZoneGenericNameMatchInfo : public UMemory {
public:
    TimeZoneGenericNameMatchInfo(UVector* matches);
    ~TimeZoneGenericNameMatchInfo();

    int32_t size() const;
    UTimeZoneGenericNameType getGenericNameType(int32_t index) const;
    int32_t getMatchLength(int32_t index) const;
    UnicodeString& getTimeZoneID(int32_t index, UnicodeString& tzID) const;

private:
    UVector* fMatches;  // vector of MatchEntry
};

TimeZoneGenericNameMatchInfo::TimeZoneGenericNameMatchInfo(UVector* matches)
: fMatches(matches) {
}

TimeZoneGenericNameMatchInfo::~TimeZoneGenericNameMatchInfo() {
    delete fMatches;
}

int32_t
TimeZoneGenericNameMatchInfo::size() const {
    if (fMatches == nullptr) {
        return 0;
    }
    return fMatches->size();
}

UTimeZoneGenericNameType
TimeZoneGenericNameMatchInfo::getGenericNameType(int32_t index) const {
    GMatchInfo* minfo = static_cast<GMatchInfo*>(fMatches->elementAt(index));
    if (minfo != nullptr) {
        return static_cast<UTimeZoneGenericNameType>(minfo->gnameInfo->type);
    }
    return UTZGNM_UNKNOWN;
}

int32_t
TimeZoneGenericNameMatchInfo::getMatchLength(int32_t index) const {
    ZMatchInfo* minfo = static_cast<ZMatchInfo*>(fMatches->elementAt(index));
    if (minfo != nullptr) {
        return minfo->matchLength;
    }
    return -1;
}

UnicodeString&
TimeZoneGenericNameMatchInfo::getTimeZoneID(int32_t index, UnicodeString& tzID) const {
    GMatchInfo* minfo = static_cast<GMatchInfo*>(fMatches->elementAt(index));
    if (minfo != nullptr && minfo->gnameInfo->tzID != nullptr) {
        tzID.setTo(true, minfo->gnameInfo->tzID, -1);
    } else {
        tzID.setToBogus();
    }
    return tzID;
}

// ---------------------------------------------------
// GNameSearchHandler
// ---------------------------------------------------
class GNameSearchHandler : public TextTrieMapSearchResultHandler {
public:
    GNameSearchHandler(uint32_t types);
    virtual ~GNameSearchHandler();

    UBool handleMatch(int32_t matchLength, const CharacterNode *node, UErrorCode &status) override;
    UVector* getMatches(int32_t& maxMatchLen);

private:
    uint32_t fTypes;
    UVector* fResults;
    int32_t fMaxMatchLen;
};

GNameSearchHandler::GNameSearchHandler(uint32_t types)
: fTypes(types), fResults(nullptr), fMaxMatchLen(0) {
}

GNameSearchHandler::~GNameSearchHandler() {
    delete fResults;
}

UBool
GNameSearchHandler::handleMatch(int32_t matchLength, const CharacterNode *node, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return false;
    }
    if (node->hasValues()) {
        int32_t valuesCount = node->countValues();
        for (int32_t i = 0; i < valuesCount; i++) {
            GNameInfo *nameinfo = (ZNameInfo *)node->getValue(i);
            if (nameinfo == nullptr) {
                break;
            }
            if ((nameinfo->type & fTypes) != 0) {
                // matches a requested type
                if (fResults == nullptr) {
                    LocalPointer<UVector> lpResults(new UVector(uprv_free, nullptr, status), status);
                    if (U_FAILURE(status)) {
                        return false;
                    }
                    fResults = lpResults.orphan();
                }
                GMatchInfo* gmatch = static_cast<GMatchInfo*>(uprv_malloc(sizeof(GMatchInfo)));
                if (gmatch == nullptr) {
                    status = U_MEMORY_ALLOCATION_ERROR;
                    return false;
                }
                // add the match to the vector
                gmatch->gnameInfo = nameinfo;
                gmatch->matchLength = matchLength;
                gmatch->timeType = UTZFMT_TIME_TYPE_UNKNOWN;
                fResults->adoptElement(gmatch, status);
                if (U_FAILURE(status)) {
                    return false;
                }
                if (matchLength > fMaxMatchLen) {
                    fMaxMatchLen = matchLength;
                }
            }
        }
    }
    return true;
}

UVector*
GNameSearchHandler::getMatches(int32_t& maxMatchLen) {
    // give the ownership to the caller
    UVector *results = fResults;
    maxMatchLen = fMaxMatchLen;

    // reset
    fResults = nullptr;
    fMaxMatchLen = 0;
    return results;
}

static UMutex gLock;

class TZGNCore : public UMemory {
public:
    TZGNCore(const Locale& locale, UErrorCode& status);
    virtual ~TZGNCore();

    UnicodeString& getDisplayName(const TimeZone& tz, UTimeZoneGenericNameType type,
                        UDate date, UnicodeString& name) const;

    UnicodeString& getGenericLocationName(const UnicodeString& tzCanonicalID, UnicodeString& name) const;

    int32_t findBestMatch(const UnicodeString& text, int32_t start, uint32_t types,
        UnicodeString& tzID, UTimeZoneFormatTimeType& timeType, UErrorCode& status) const;

private:
    Locale fLocale;
    const TimeZoneNames* fTimeZoneNames;
    UHashtable* fLocationNamesMap;
    UHashtable* fPartialLocationNamesMap;

    SimpleFormatter fRegionFormat;
    SimpleFormatter fFallbackFormat;

    LocaleDisplayNames* fLocaleDisplayNames;
    ZNStringPool fStringPool;

    TextTrieMap fGNamesTrie;
    UBool fGNamesTrieFullyLoaded;

    CharString fTargetRegion;

    void initialize(const Locale& locale, UErrorCode& status);
    void cleanup();

    void loadStrings(const UnicodeString& tzCanonicalID);

    const char16_t* getGenericLocationName(const UnicodeString& tzCanonicalID);

    UnicodeString& formatGenericNonLocationName(const TimeZone& tz, UTimeZoneGenericNameType type,
                        UDate date, UnicodeString& name) const;

    UnicodeString& getPartialLocationName(const UnicodeString& tzCanonicalID,
                        const UnicodeString& mzID, UBool isLong, const UnicodeString& mzDisplayName,
                        UnicodeString& name) const;

    const char16_t* getPartialLocationName(const UnicodeString& tzCanonicalID,
                        const UnicodeString& mzID, UBool isLong, const UnicodeString& mzDisplayName);

    TimeZoneGenericNameMatchInfo* findLocal(const UnicodeString& text, int32_t start, uint32_t types, UErrorCode& status) const;

    TimeZoneNames::MatchInfoCollection* findTimeZoneNames(const UnicodeString& text, int32_t start, uint32_t types, UErrorCode& status) const;
};


// ---------------------------------------------------
// TZGNCore - core implementation of TimeZoneGenericNames
//
// TimeZoneGenericNames is parallel to TimeZoneNames,
// but handles run-time generated time zone names.
// This is the main part of this module.
// ---------------------------------------------------
TZGNCore::TZGNCore(const Locale& locale, UErrorCode& status)
: fLocale(locale),
  fTimeZoneNames(nullptr),
  fLocationNamesMap(nullptr),
  fPartialLocationNamesMap(nullptr),
  fLocaleDisplayNames(nullptr),
  fStringPool(status),
  fGNamesTrie(true, deleteGNameInfo),
  fGNamesTrieFullyLoaded(false),
  fTargetRegion() {
    initialize(locale, status);
}

TZGNCore::~TZGNCore() {
    cleanup();
}

void
TZGNCore::initialize(const Locale& locale, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }

    // TimeZoneNames
    fTimeZoneNames = TimeZoneNames::createInstance(locale, status);
    if (U_FAILURE(status)) {
        return;
    }

    // Initialize format patterns
    UnicodeString rpat(true, gDefRegionPattern, -1);
    UnicodeString fpat(true, gDefFallbackPattern, -1);

    UErrorCode tmpsts = U_ZERO_ERROR;   // OK with fallback warning..
    UResourceBundle *zoneStrings = ures_open(U_ICUDATA_ZONE, locale.getName(), &tmpsts);
    zoneStrings = ures_getByKeyWithFallback(zoneStrings, gZoneStrings, zoneStrings, &tmpsts);

    if (U_SUCCESS(tmpsts)) {
        const char16_t *regionPattern = ures_getStringByKeyWithFallback(zoneStrings, gRegionFormatTag, nullptr, &tmpsts);
        if (U_SUCCESS(tmpsts) && u_strlen(regionPattern) > 0) {
            rpat.setTo(regionPattern, -1);
        }
        tmpsts = U_ZERO_ERROR;
        const char16_t *fallbackPattern = ures_getStringByKeyWithFallback(zoneStrings, gFallbackFormatTag, nullptr, &tmpsts);
        if (U_SUCCESS(tmpsts) && u_strlen(fallbackPattern) > 0) {
            fpat.setTo(fallbackPattern, -1);
        }
    }
    ures_close(zoneStrings);

    fRegionFormat.applyPatternMinMaxArguments(rpat, 1, 1, status);
    fFallbackFormat.applyPatternMinMaxArguments(fpat, 2, 2, status);
    if (U_FAILURE(status)) {
        cleanup();
        return;
    }

    // locale display names
    fLocaleDisplayNames = LocaleDisplayNames::createInstance(locale);

    // hash table for names - no key/value deleters
    fLocationNamesMap = uhash_open(uhash_hashUChars, uhash_compareUChars, nullptr, &status);
    if (U_FAILURE(status)) {
        cleanup();
        return;
    }

    fPartialLocationNamesMap = uhash_open(hashPartialLocationKey, comparePartialLocationKey, nullptr, &status);
    if (U_FAILURE(status)) {
        cleanup();
        return;
    }
    uhash_setKeyDeleter(fPartialLocationNamesMap, uprv_free);
    // no value deleter

    // target region
    const char* region = fLocale.getCountry();
    int32_t regionLen = static_cast<int32_t>(uprv_strlen(region));
    if (regionLen == 0) {
        CharString loc = ulocimp_addLikelySubtags(fLocale.getName(), status);
        ulocimp_getSubtags(loc.data(), nullptr, nullptr, &fTargetRegion, nullptr, nullptr, status);
        if (U_FAILURE(status)) {
            cleanup();
            return;
        }
    } else {
        fTargetRegion.append(region, regionLen, status);
    }

    // preload generic names for the default zone
    TimeZone *tz = TimeZone::createDefault();
    const char16_t *tzID = ZoneMeta::getCanonicalCLDRID(*tz);
    if (tzID != nullptr) {
        loadStrings(UnicodeString(true, tzID, -1));
    }
    delete tz;
}

void
TZGNCore::cleanup() {
    delete fLocaleDisplayNames;
    delete fTimeZoneNames;

    uhash_close(fLocationNamesMap);
    uhash_close(fPartialLocationNamesMap);
}


UnicodeString&
TZGNCore::getDisplayName(const TimeZone& tz, UTimeZoneGenericNameType type, UDate date, UnicodeString& name) const {
    name.setToBogus();
    switch (type) {
    case UTZGNM_LOCATION:
        {
            const char16_t* tzCanonicalID = ZoneMeta::getCanonicalCLDRID(tz);
            if (tzCanonicalID != nullptr) {
                getGenericLocationName(UnicodeString(true, tzCanonicalID, -1), name);
            }
        }
        break;
    case UTZGNM_LONG:
    case UTZGNM_SHORT:
        formatGenericNonLocationName(tz, type, date, name);
        if (name.isEmpty()) {
            const char16_t* tzCanonicalID = ZoneMeta::getCanonicalCLDRID(tz);
            if (tzCanonicalID != nullptr) {
                getGenericLocationName(UnicodeString(true, tzCanonicalID, -1), name);
            }
        }
        break;
    default:
        break;
    }
    return name;
}

UnicodeString&
TZGNCore::getGenericLocationName(const UnicodeString& tzCanonicalID, UnicodeString& name) const {
    if (tzCanonicalID.isEmpty()) {
        name.setToBogus();
        return name;
    }

    const char16_t *locname = nullptr;
    TZGNCore *nonConstThis = const_cast<TZGNCore *>(this);
    umtx_lock(&gLock);
    {
        locname = nonConstThis->getGenericLocationName(tzCanonicalID);
    }
    umtx_unlock(&gLock);

    if (locname == nullptr) {
        name.setToBogus();
    } else {
        name.setTo(locname, u_strlen(locname));
    }

    return name;
}

/*
 * This method updates the cache and must be called with a lock
 */
const char16_t*
TZGNCore::getGenericLocationName(const UnicodeString& tzCanonicalID) {
    U_ASSERT(!tzCanonicalID.isEmpty());
    if (tzCanonicalID.length() > ZID_KEY_MAX) {
        return nullptr;
    }

    UErrorCode status = U_ZERO_ERROR;
    char16_t tzIDKey[ZID_KEY_MAX + 1];
    int32_t tzIDKeyLen = tzCanonicalID.extract(tzIDKey, ZID_KEY_MAX + 1, status);
    U_ASSERT(status == U_ZERO_ERROR);   // already checked length above
    tzIDKey[tzIDKeyLen] = 0;

    const char16_t* locname = static_cast<const char16_t*>(uhash_get(fLocationNamesMap, tzIDKey));

    if (locname != nullptr) {
        // gEmpty indicate the name is not available
        if (locname == gEmpty) {
            return nullptr;
        }
        return locname;
    }

    // Construct location name
    UnicodeString name;
    UnicodeString usCountryCode;
    UBool isPrimary = false;

    ZoneMeta::getCanonicalCountry(tzCanonicalID, usCountryCode, &isPrimary);

    if (!usCountryCode.isEmpty()) {
        if (isPrimary) {
            // If this is the primary zone in the country, use the country name.
            char countryCode[ULOC_COUNTRY_CAPACITY];
            U_ASSERT(usCountryCode.length() < ULOC_COUNTRY_CAPACITY);
            int32_t ccLen = usCountryCode.extract(0, usCountryCode.length(), countryCode, sizeof(countryCode), US_INV);
            countryCode[ccLen] = 0;

            UnicodeString country;
            fLocaleDisplayNames->regionDisplayName(countryCode, country);
            fRegionFormat.format(country, name, status);
        } else {
            // If this is not the primary zone in the country,
            // use the exemplar city name.

            // getExemplarLocationName should return non-empty string
            // if the time zone is associated with a region

            UnicodeString city;
            fTimeZoneNames->getExemplarLocationName(tzCanonicalID, city);
            fRegionFormat.format(city, name, status);
        }
        if (U_FAILURE(status)) {
            return nullptr;
        }
    }

    locname = name.isEmpty() ? nullptr : fStringPool.get(name, status);
    if (U_SUCCESS(status)) {
        // Cache the result
        const char16_t* cacheID = ZoneMeta::findTimeZoneID(tzCanonicalID);
        U_ASSERT(cacheID != nullptr);
        if (locname == nullptr) {
            // gEmpty to indicate - no location name available
            uhash_put(fLocationNamesMap, (void *)cacheID, (void *)gEmpty, &status);
        } else {
            uhash_put(fLocationNamesMap, (void *)cacheID, (void *)locname, &status);
            if (U_FAILURE(status)) {
                locname = nullptr;
            } else {
                // put the name info into the trie
                GNameInfo* nameinfo = static_cast<ZNameInfo*>(uprv_malloc(sizeof(GNameInfo)));
                if (nameinfo != nullptr) {
                    nameinfo->type = UTZGNM_LOCATION;
                    nameinfo->tzID = cacheID;
                    fGNamesTrie.put(locname, nameinfo, status);
                }
            }
        }
    }

    return locname;
}

UnicodeString&
TZGNCore::formatGenericNonLocationName(const TimeZone& tz, UTimeZoneGenericNameType type, UDate date, UnicodeString& name) const {
    U_ASSERT(type == UTZGNM_LONG || type == UTZGNM_SHORT);
    name.setToBogus();

    const char16_t* uID = ZoneMeta::getCanonicalCLDRID(tz);
    if (uID == nullptr) {
        return name;
    }

    UnicodeString tzID(true, uID, -1);

    // Try to get a name from time zone first
    UTimeZoneNameType nameType = (type == UTZGNM_LONG) ? UTZNM_LONG_GENERIC : UTZNM_SHORT_GENERIC;
    fTimeZoneNames->getTimeZoneDisplayName(tzID, nameType, name);

    if (!name.isEmpty()) {
        return name;
    }

    // Try meta zone
    char16_t mzIDBuf[32];
    UnicodeString mzID(mzIDBuf, 0, UPRV_LENGTHOF(mzIDBuf));
    fTimeZoneNames->getMetaZoneID(tzID, date, mzID);
    if (!mzID.isEmpty()) {
        UErrorCode status = U_ZERO_ERROR;
        UBool useStandard = false;
        int32_t raw, sav;
        char16_t tmpNameBuf[ZONE_NAME_U16_MAX];

        tz.getOffset(date, false, raw, sav, status);
        if (U_FAILURE(status)) {
            return name;
        }

        if (sav == 0) {
            useStandard = true;

            TimeZone *tmptz = tz.clone();
            // Check if the zone actually uses daylight saving time around the time
            BasicTimeZone *btz = nullptr;
            if (dynamic_cast<OlsonTimeZone *>(tmptz) != nullptr
                || dynamic_cast<SimpleTimeZone *>(tmptz) != nullptr
                || dynamic_cast<RuleBasedTimeZone *>(tmptz) != nullptr
                || dynamic_cast<VTimeZone *>(tmptz) != nullptr) {
                btz = (BasicTimeZone*)tmptz;
            }

            if (btz != nullptr) {
                TimeZoneTransition before;
                UBool beforTrs = btz->getPreviousTransition(date, true, before);
                if (beforTrs
                        && (date - before.getTime() < kDstCheckRange)
                        && before.getFrom()->getDSTSavings() != 0) {
                    useStandard = false;
                } else {
                    TimeZoneTransition after;
                    UBool afterTrs = btz->getNextTransition(date, false, after);
                    if (afterTrs
                            && (after.getTime() - date < kDstCheckRange)
                            && after.getTo()->getDSTSavings() != 0) {
                        useStandard = false;
                    }
                }
            } else {
                // If not BasicTimeZone... only if the instance is not an ICU's implementation.
                // We may get a wrong answer in edge case, but it should practically work OK.
                tmptz->getOffset(date - kDstCheckRange, false, raw, sav, status);
                if (sav != 0) {
                    useStandard = false;
                } else {
                    tmptz->getOffset(date + kDstCheckRange, false, raw, sav, status);
                    if (sav != 0){
                        useStandard = false;
                    }
                }
                if (U_FAILURE(status)) {
                    delete tmptz;
                    return name;
                }
            }
            delete tmptz;
        }
        if (useStandard) {
            UTimeZoneNameType stdNameType = (nameType == UTZNM_LONG_GENERIC)
                ? UTZNM_LONG_STANDARD : UTZNM_SHORT_STANDARD;
            UnicodeString stdName(tmpNameBuf, 0, UPRV_LENGTHOF(tmpNameBuf));
            fTimeZoneNames->getDisplayName(tzID, stdNameType, date, stdName);
            if (!stdName.isEmpty()) {
                name.setTo(stdName);

                // TODO: revisit this issue later
                // In CLDR, a same display name is used for both generic and standard
                // for some meta zones in some locales.  This looks like a data bugs.
                // For now, we check if the standard name is different from its generic
                // name below.
                char16_t genNameBuf[ZONE_NAME_U16_MAX];
                UnicodeString mzGenericName(genNameBuf, 0, UPRV_LENGTHOF(genNameBuf));
                fTimeZoneNames->getMetaZoneDisplayName(mzID, nameType, mzGenericName);
                if (stdName.caseCompare(mzGenericName, 0) == 0) {
                    name.setToBogus();
                }
            }
        }
        if (name.isEmpty()) {
            // Get a name from meta zone
            UnicodeString mzName(tmpNameBuf, 0, UPRV_LENGTHOF(tmpNameBuf));
            fTimeZoneNames->getMetaZoneDisplayName(mzID, nameType, mzName);
            if (!mzName.isEmpty()) {
                // Check if we need to use a partial location format.
                // This check is done by comparing offset with the meta zone's
                // golden zone at the given date.
                char16_t idBuf[32];
                UnicodeString goldenID(idBuf, 0, UPRV_LENGTHOF(idBuf));
                fTimeZoneNames->getReferenceZoneID(mzID, fTargetRegion.data(), goldenID);
                if (!goldenID.isEmpty() && goldenID != tzID) {
                    TimeZone *goldenZone = TimeZone::createTimeZone(goldenID);
                    int32_t raw1, sav1;

                    // Check offset in the golden zone with wall time.
                    // With getOffset(date, false, offsets1),
                    // you may get incorrect results because of time overlap at DST->STD
                    // transition.
                    goldenZone->getOffset(date + raw + sav, true, raw1, sav1, status);
                    delete goldenZone;
                    if (U_SUCCESS(status)) {
                        if (raw != raw1 || sav != sav1) {
                            // Now we need to use a partial location format
                            getPartialLocationName(tzID, mzID, (nameType == UTZNM_LONG_GENERIC), mzName, name);
                        } else {
                            name.setTo(mzName);
                        }
                    }
                } else {
                    name.setTo(mzName);
                }
            }
        }
    }
    return name;
}

UnicodeString&
TZGNCore::getPartialLocationName(const UnicodeString& tzCanonicalID,
                        const UnicodeString& mzID, UBool isLong, const UnicodeString& mzDisplayName,
                        UnicodeString& name) const {
    name.setToBogus();
    if (tzCanonicalID.isEmpty() || mzID.isEmpty() || mzDisplayName.isEmpty()) {
        return name;
    }

    const char16_t *uplname = nullptr;
    TZGNCore *nonConstThis = const_cast<TZGNCore *>(this);
    umtx_lock(&gLock);
    {
        uplname = nonConstThis->getPartialLocationName(tzCanonicalID, mzID, isLong, mzDisplayName);
    }
    umtx_unlock(&gLock);

    if (uplname == nullptr) {
        name.setToBogus();
    } else {
        name.setTo(true, uplname, -1);
    }
    return name;
}

/*
 * This method updates the cache and must be called with a lock
 */
const char16_t*
TZGNCore::getPartialLocationName(const UnicodeString& tzCanonicalID,
                        const UnicodeString& mzID, UBool isLong, const UnicodeString& mzDisplayName) {
    U_ASSERT(!tzCanonicalID.isEmpty());
    U_ASSERT(!mzID.isEmpty());
    U_ASSERT(!mzDisplayName.isEmpty());

    PartialLocationKey key;
    key.tzID = ZoneMeta::findTimeZoneID(tzCanonicalID);
    key.mzID = ZoneMeta::findMetaZoneID(mzID);
    key.isLong = isLong;
    U_ASSERT(key.tzID != nullptr && key.mzID != nullptr);

    const char16_t* uplname = static_cast<const char16_t*>(uhash_get(fPartialLocationNamesMap, &key));
    if (uplname != nullptr) {
        return uplname;
    }

    UnicodeString location;
    UnicodeString usCountryCode;
    ZoneMeta::getCanonicalCountry(tzCanonicalID, usCountryCode);
    if (!usCountryCode.isEmpty()) {
        char countryCode[ULOC_COUNTRY_CAPACITY];
        U_ASSERT(usCountryCode.length() < ULOC_COUNTRY_CAPACITY);
        int32_t ccLen = usCountryCode.extract(0, usCountryCode.length(), countryCode, sizeof(countryCode), US_INV);
        countryCode[ccLen] = 0;

        UnicodeString regionalGolden;
        fTimeZoneNames->getReferenceZoneID(mzID, countryCode, regionalGolden);
        if (tzCanonicalID == regionalGolden) {
            // Use country name
            fLocaleDisplayNames->regionDisplayName(countryCode, location);
        } else {
            // Otherwise, use exemplar city name
            fTimeZoneNames->getExemplarLocationName(tzCanonicalID, location);
        }
    } else {
        fTimeZoneNames->getExemplarLocationName(tzCanonicalID, location);
        if (location.isEmpty()) {
            // This could happen when the time zone is not associated with a country,
            // and its ID is not hierarchical, for example, CST6CDT.
            // We use the canonical ID itself as the location for this case.
            location.setTo(tzCanonicalID);
        }
    }

    UErrorCode status = U_ZERO_ERROR;
    UnicodeString name;
    fFallbackFormat.format(location, mzDisplayName, name, status);
    if (U_FAILURE(status)) {
        return nullptr;
    }

    uplname = fStringPool.get(name, status);
    if (U_SUCCESS(status)) {
        // Add the name to cache
        PartialLocationKey* cacheKey = static_cast<PartialLocationKey*>(uprv_malloc(sizeof(PartialLocationKey)));
        if (cacheKey != nullptr) {
            cacheKey->tzID = key.tzID;
            cacheKey->mzID = key.mzID;
            cacheKey->isLong = key.isLong;
            uhash_put(fPartialLocationNamesMap, (void *)cacheKey, (void *)uplname, &status);
            if (U_FAILURE(status)) {
                uprv_free(cacheKey);
            } else {
                // put the name to the local trie as well
                GNameInfo* nameinfo = static_cast<ZNameInfo*>(uprv_malloc(sizeof(GNameInfo)));
                if (nameinfo != nullptr) {
                    nameinfo->type = isLong ? UTZGNM_LONG : UTZGNM_SHORT;
                    nameinfo->tzID = key.tzID;
                    fGNamesTrie.put(uplname, nameinfo, status);
                }
            }
        }
    }
    return uplname;
}

/*
 * This method updates the cache and must be called with a lock,
 * except initializer.
 */
void
TZGNCore::loadStrings(const UnicodeString& tzCanonicalID) {
    // load the generic location name
    getGenericLocationName(tzCanonicalID);

    // partial location names
    UErrorCode status = U_ZERO_ERROR;

    const UnicodeString *mzID;
    UnicodeString goldenID;
    UnicodeString mzGenName;
    UTimeZoneNameType genNonLocTypes[] = {
        UTZNM_LONG_GENERIC, UTZNM_SHORT_GENERIC,
        UTZNM_UNKNOWN /*terminator*/
    };

    StringEnumeration *mzIDs = fTimeZoneNames->getAvailableMetaZoneIDs(tzCanonicalID, status);
    while ((mzID = mzIDs->snext(status)) != nullptr) {
        if (U_FAILURE(status)) {
            break;
        }
        // if this time zone is not the golden zone of the meta zone,
        // partial location name (such as "PT (Los Angeles)") might be
        // available.
        fTimeZoneNames->getReferenceZoneID(*mzID, fTargetRegion.data(), goldenID);
        if (tzCanonicalID != goldenID) {
            for (int32_t i = 0; genNonLocTypes[i] != UTZNM_UNKNOWN; i++) {
                fTimeZoneNames->getMetaZoneDisplayName(*mzID, genNonLocTypes[i], mzGenName);
                if (!mzGenName.isEmpty()) {
                    // getPartialLocationName formats a name and put it into the trie
                    getPartialLocationName(tzCanonicalID, *mzID,
                        (genNonLocTypes[i] == UTZNM_LONG_GENERIC), mzGenName);
                }
            }
        }
    }
    delete mzIDs;
}

int32_t
TZGNCore::findBestMatch(const UnicodeString& text, int32_t start, uint32_t types,
        UnicodeString& tzID, UTimeZoneFormatTimeType& timeType, UErrorCode& status) const {
    timeType = UTZFMT_TIME_TYPE_UNKNOWN;
    tzID.setToBogus();

    if (U_FAILURE(status)) {
        return 0;
    }

    // Find matches in the TimeZoneNames first
    TimeZoneNames::MatchInfoCollection *tznamesMatches = findTimeZoneNames(text, start, types, status);
    if (U_FAILURE(status)) {
        return 0;
    }

    int32_t bestMatchLen = 0;
    UTimeZoneFormatTimeType bestMatchTimeType = UTZFMT_TIME_TYPE_UNKNOWN;
    UnicodeString bestMatchTzID;
    // UBool isLongStandard = false;   // workaround - see the comments below
    UBool isStandard = false;       // TODO: Temporary hack (on hack) for short standard name/location name conflict (found in zh_Hant), should be removed after CLDR 21m1 integration

    if (tznamesMatches != nullptr) {
        UnicodeString mzID;
        for (int32_t i = 0; i < tznamesMatches->size(); i++) {
            int32_t len = tznamesMatches->getMatchLengthAt(i);
            if (len > bestMatchLen) {
                bestMatchLen = len;
                if (!tznamesMatches->getTimeZoneIDAt(i, bestMatchTzID)) {
                    // name for a meta zone
                    if (tznamesMatches->getMetaZoneIDAt(i, mzID)) {
                        fTimeZoneNames->getReferenceZoneID(mzID, fTargetRegion.data(), bestMatchTzID);
                    }
                }
                UTimeZoneNameType nameType = tznamesMatches->getNameTypeAt(i);
                if (U_FAILURE(status)) {
                    break;
                }
                switch (nameType) {
                case UTZNM_LONG_STANDARD:
                    // isLongStandard = true;
                case UTZNM_SHORT_STANDARD:  // this one is never used for generic, but just in case
                    isStandard = true;      // TODO: Remove this later, see the comments above.
                    bestMatchTimeType = UTZFMT_TIME_TYPE_STANDARD;
                    break;
                case UTZNM_LONG_DAYLIGHT:
                case UTZNM_SHORT_DAYLIGHT: // this one is never used for generic, but just in case
                    bestMatchTimeType = UTZFMT_TIME_TYPE_DAYLIGHT;
                    break;
                default:
                    bestMatchTimeType = UTZFMT_TIME_TYPE_UNKNOWN;
                }
            }
        }
        delete tznamesMatches;
        if (U_FAILURE(status)) {
            return 0;
        }

        if (bestMatchLen == (text.length() - start)) {
            // Full match

            //tzID.setTo(bestMatchTzID);
            //timeType = bestMatchTimeType;
            //return bestMatchLen;

            // TODO Some time zone uses a same name for the long standard name
            // and the location name. When the match is a long standard name,
            // then we need to check if the name is same with the location name.
            // This is probably a data error or a design bug.
/*
            if (!isLongStandard) {
                tzID.setTo(bestMatchTzID);
                timeType = bestMatchTimeType;
                return bestMatchLen;
            }
*/
            // TODO The deprecation of commonlyUsed flag introduced the name
            // conflict not only for long standard names, but short standard names too.
            // These short names (found in zh_Hant) should be gone once we clean
            // up CLDR time zone display name data. Once the short name conflict
            // problem (with location name) is resolved, we should change the condition
            // below back to the original one above. -Yoshito (2011-09-14)
            if (!isStandard) {
                tzID.setTo(bestMatchTzID);
                timeType = bestMatchTimeType;
                return bestMatchLen;
            }
        }
    }

    // Find matches in the local trie
    TimeZoneGenericNameMatchInfo *localMatches = findLocal(text, start, types, status);
    if (U_FAILURE(status)) {
        return 0;
    }
    if (localMatches != nullptr) {
        for (int32_t i = 0; i < localMatches->size(); i++) {
            int32_t len = localMatches->getMatchLength(i);

            // TODO See the above TODO. We use len >= bestMatchLen
            // because of the long standard/location name collision
            // problem. If it is also a location name, carrying
            // timeType = UTZFMT_TIME_TYPE_STANDARD will cause a
            // problem in SimpleDateFormat
            if (len >= bestMatchLen) {
                bestMatchLen = localMatches->getMatchLength(i);
                bestMatchTimeType = UTZFMT_TIME_TYPE_UNKNOWN;   // because generic
                localMatches->getTimeZoneID(i, bestMatchTzID);
            }
        }
        delete localMatches;
    }

    if (bestMatchLen > 0) {
        timeType = bestMatchTimeType;
        tzID.setTo(bestMatchTzID);
    }
    return bestMatchLen;
}

TimeZoneGenericNameMatchInfo*
TZGNCore::findLocal(const UnicodeString& text, int32_t start, uint32_t types, UErrorCode& status) const {
    GNameSearchHandler handler(types);

    TZGNCore *nonConstThis = const_cast<TZGNCore *>(this);

    umtx_lock(&gLock);
    {
        fGNamesTrie.search(text, start, (TextTrieMapSearchResultHandler *)&handler, status);
    }
    umtx_unlock(&gLock);

    if (U_FAILURE(status)) {
        return nullptr;
    }

    TimeZoneGenericNameMatchInfo *gmatchInfo = nullptr;

    int32_t maxLen = 0;
    UVector *results = handler.getMatches(maxLen);
    if (results != nullptr && ((maxLen == (text.length() - start)) || fGNamesTrieFullyLoaded)) {
        // perfect match
        gmatchInfo = new TimeZoneGenericNameMatchInfo(results);
        if (gmatchInfo == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            delete results;
            return nullptr;
        }
        return gmatchInfo;
    }

    delete results;

    // All names are not yet loaded into the local trie.
    // Load all available names into the trie. This could be very heavy.
    umtx_lock(&gLock);
    {
        if (!fGNamesTrieFullyLoaded) {
            StringEnumeration *tzIDs = TimeZone::createTimeZoneIDEnumeration(UCAL_ZONE_TYPE_CANONICAL, nullptr, nullptr, status);
            if (U_SUCCESS(status)) {
                const UnicodeString *tzID;
                while ((tzID = tzIDs->snext(status)) != nullptr) {
                    if (U_FAILURE(status)) {
                        break;
                    }
                    nonConstThis->loadStrings(*tzID);
                }
            }
            delete tzIDs;

            if (U_SUCCESS(status)) {
                nonConstThis->fGNamesTrieFullyLoaded = true;
            }
        }
    }
    umtx_unlock(&gLock);

    if (U_FAILURE(status)) {
        return nullptr;
    }

    umtx_lock(&gLock);
    {
        // now try it again
        fGNamesTrie.search(text, start, (TextTrieMapSearchResultHandler *)&handler, status);
    }
    umtx_unlock(&gLock);

    results = handler.getMatches(maxLen);
    if (results != nullptr && maxLen > 0) {
        gmatchInfo = new TimeZoneGenericNameMatchInfo(results);
        if (gmatchInfo == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            delete results;
            return nullptr;
        }
    }

    return gmatchInfo;
}

TimeZoneNames::MatchInfoCollection*
TZGNCore::findTimeZoneNames(const UnicodeString& text, int32_t start, uint32_t types, UErrorCode& status) const {
    // Check if the target name typs is really in the TimeZoneNames
    uint32_t nameTypes = 0;
    if (types & UTZGNM_LONG) {
        nameTypes |= (UTZNM_LONG_GENERIC | UTZNM_LONG_STANDARD);
    }
    if (types & UTZGNM_SHORT) {
        nameTypes |= (UTZNM_SHORT_GENERIC | UTZNM_SHORT_STANDARD);
    }

    if (types) {
        // Find matches in the TimeZoneNames
        return fTimeZoneNames->find(text, start, nameTypes, status);
    }

    return nullptr;
}

typedef struct TZGNCoreRef {
    TZGNCore*       obj;
    int32_t         refCount;
    double          lastAccess;
} TZGNCoreRef;

// TZGNCore object cache handling
static UMutex gTZGNLock;
static UHashtable *gTZGNCoreCache = nullptr;
static UBool gTZGNCoreCacheInitialized = false;

// Access count - incremented every time up to SWEEP_INTERVAL,
// then reset to 0
static int32_t gAccessCount = 0;

// Interval for calling the cache sweep function - every 100 times
#define SWEEP_INTERVAL 100

// Cache expiration in millisecond. When a cached entry is no
// longer referenced and exceeding this threshold since last
// access time, then the cache entry will be deleted by the sweep
// function. For now, 3 minutes.
#define CACHE_EXPIRATION 180000.0

U_CDECL_BEGIN
/**
 * Cleanup callback func
 */
static UBool U_CALLCONV tzgnCore_cleanup()
{
    if (gTZGNCoreCache != nullptr) {
        uhash_close(gTZGNCoreCache);
        gTZGNCoreCache = nullptr;
    }
    gTZGNCoreCacheInitialized = false;
    return true;
}

/**
 * Deleter for TZGNCoreRef
 */
static void U_CALLCONV
deleteTZGNCoreRef(void *obj) {
    icu::TZGNCoreRef *entry = (icu::TZGNCoreRef*)obj;
    delete (icu::TZGNCore*) entry->obj;
    uprv_free(entry);
}
U_CDECL_END

/**
 * Function used for removing unreferrenced cache entries exceeding
 * the expiration time. This function must be called with in the mutex
 * block.
 */
static void sweepCache() {
    int32_t pos = UHASH_FIRST;
    const UHashElement* elem;
    double now = static_cast<double>(uprv_getUTCtime());

    while ((elem = uhash_nextElement(gTZGNCoreCache, &pos)) != nullptr) {
        TZGNCoreRef* entry = static_cast<TZGNCoreRef*>(elem->value.pointer);
        if (entry->refCount <= 0 && (now - entry->lastAccess) > CACHE_EXPIRATION) {
            // delete this entry
            uhash_removeElement(gTZGNCoreCache, elem);
        }
    }
}

TimeZoneGenericNames::TimeZoneGenericNames()
: fRef(nullptr) {
}

TimeZoneGenericNames::~TimeZoneGenericNames() {
    umtx_lock(&gTZGNLock);
    {
        U_ASSERT(fRef->refCount > 0);
        // Just decrement the reference count
        fRef->refCount--;
    }
    umtx_unlock(&gTZGNLock);
}

TimeZoneGenericNames*
TimeZoneGenericNames::createInstance(const Locale& locale, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    TimeZoneGenericNames* instance = new TimeZoneGenericNames();
    if (instance == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }

    TZGNCoreRef *cacheEntry = nullptr;
    {
        Mutex lock(&gTZGNLock);

        if (!gTZGNCoreCacheInitialized) {
            // Create empty hashtable
            gTZGNCoreCache = uhash_open(uhash_hashChars, uhash_compareChars, nullptr, &status);
            if (U_SUCCESS(status)) {
                uhash_setKeyDeleter(gTZGNCoreCache, uprv_free);
                uhash_setValueDeleter(gTZGNCoreCache, deleteTZGNCoreRef);
                gTZGNCoreCacheInitialized = true;
                ucln_i18n_registerCleanup(UCLN_I18N_TIMEZONEGENERICNAMES, tzgnCore_cleanup);
            }
        }
        if (U_FAILURE(status)) {
            delete instance;
            return nullptr;
        }

        // Check the cache, if not available, create new one and cache
        const char *key = locale.getName();
        cacheEntry = static_cast<TZGNCoreRef*>(uhash_get(gTZGNCoreCache, key));
        if (cacheEntry == nullptr) {
            TZGNCore *tzgnCore = nullptr;
            char *newKey = nullptr;

            tzgnCore = new TZGNCore(locale, status);
            if (tzgnCore == nullptr) {
                status = U_MEMORY_ALLOCATION_ERROR;
            }
            if (U_SUCCESS(status)) {
                newKey = static_cast<char*>(uprv_malloc(uprv_strlen(key) + 1));
                if (newKey == nullptr) {
                    status = U_MEMORY_ALLOCATION_ERROR;
                } else {
                    uprv_strcpy(newKey, key);
                }
            }
            if (U_SUCCESS(status)) {
                cacheEntry = static_cast<TZGNCoreRef*>(uprv_malloc(sizeof(TZGNCoreRef)));
                if (cacheEntry == nullptr) {
                    status = U_MEMORY_ALLOCATION_ERROR;
                } else {
                    cacheEntry->obj = tzgnCore;
                    cacheEntry->refCount = 1;
                    cacheEntry->lastAccess = static_cast<double>(uprv_getUTCtime());

                    uhash_put(gTZGNCoreCache, newKey, cacheEntry, &status);
                }
            }
            if (U_FAILURE(status)) {
                delete tzgnCore;
                if (newKey != nullptr) {
                    uprv_free(newKey);
                }
                if (cacheEntry != nullptr) {
                    uprv_free(cacheEntry);
                }
                cacheEntry = nullptr;
            }
        } else {
            // Update the reference count
            cacheEntry->refCount++;
            cacheEntry->lastAccess = static_cast<double>(uprv_getUTCtime());
        }
        gAccessCount++;
        if (gAccessCount >= SWEEP_INTERVAL) {
            // sweep
            sweepCache();
            gAccessCount = 0;
        }
    }  // End of mutex locked block

    if (cacheEntry == nullptr) {
        delete instance;
        return nullptr;
    }

    instance->fRef = cacheEntry;
    return instance;
}

bool
TimeZoneGenericNames::operator==(const TimeZoneGenericNames& other) const {
    // Just compare if the other object also use the same
    // ref entry
    return fRef == other.fRef;
}

TimeZoneGenericNames*
TimeZoneGenericNames::clone() const {
    TimeZoneGenericNames* other = new TimeZoneGenericNames();
    if (other) {
        umtx_lock(&gTZGNLock);
        {
            // Just increments the reference count
            fRef->refCount++;
            other->fRef = fRef;
        }
        umtx_unlock(&gTZGNLock);
    }
    return other;
}

UnicodeString&
TimeZoneGenericNames::getDisplayName(const TimeZone& tz, UTimeZoneGenericNameType type,
                        UDate date, UnicodeString& name) const {
    return fRef->obj->getDisplayName(tz, type, date, name);
}

UnicodeString&
TimeZoneGenericNames::getGenericLocationName(const UnicodeString& tzCanonicalID, UnicodeString& name) const {
    return fRef->obj->getGenericLocationName(tzCanonicalID, name);
}

int32_t
TimeZoneGenericNames::findBestMatch(const UnicodeString& text, int32_t start, uint32_t types,
        UnicodeString& tzID, UTimeZoneFormatTimeType& timeType, UErrorCode& status) const {
    return fRef->obj->findBestMatch(text, start, types, tzID, timeType, status);
}

U_NAMESPACE_END
#endif
