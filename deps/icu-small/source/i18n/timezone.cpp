// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 1997-2016, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*
* File TIMEZONE.CPP
*
* Modification History:
*
*   Date        Name        Description
*   12/05/96    clhuang     Creation.
*   04/21/97    aliu        General clean-up and bug fixing.
*   05/08/97    aliu        Fixed Hashtable code per code review.
*   07/09/97    helena      Changed createInstance to createDefault.
*   07/29/97    aliu        Updated with all-new list of 96 UNIX-derived
*                           TimeZones.  Changed mechanism to load from static
*                           array rather than resource bundle.
*   07/07/1998  srl         Bugfixes from the Java side: UTC GMT CAT NST
*                           Added getDisplayName API
*                           going to add custom parsing.
*
*                           ISSUES:
*                               - should getDisplayName cache something?
*                               - should custom time zones be cached? [probably]
*  08/10/98     stephen     Brought getDisplayName() API in-line w/ conventions
*  08/19/98     stephen     Changed createTimeZone() to never return 0
*  09/02/98     stephen     Added getOffset(monthLen) and hasSameRules()
*  09/15/98     stephen     Added getStaticClassID()
*  02/22/99     stephen     Removed character literals for EBCDIC safety
*  05/04/99     stephen     Changed initDefault() for Mutex issues
*  07/12/99     helena      HPUX 11 CC Port.
*  12/03/99     aliu        Moved data out of static table into icudata.dll.
*                           Substantial rewrite of zone lookup, default zone, and
*                           available IDs code.  Misc. cleanup.
*********************************************************************************/

#include "utypeinfo.h"  // for 'typeid' to work

#include "unicode/utypes.h"
#include "unicode/ustring.h"
#include "uassert.h"
#include "uinvchar.h"
#include "ustr_imp.h"
#include "util.h"

#ifdef U_DEBUG_TZ
# include <stdio.h>
# include "uresimp.h" // for debugging

static void debug_tz_loc(const char *f, int32_t l)
{
  fprintf(stderr, "%s:%d: ", f, l);
}

static void debug_tz_msg(const char *pat, ...)
{
  va_list ap;
  va_start(ap, pat);
  vfprintf(stderr, pat, ap);
  fflush(stderr);
}
static char gStrBuf[256];
#define U_DEBUG_TZ_STR(x) u_austrncpy(gStrBuf,x,sizeof(gStrBuf)-1)
// must use double parens, i.e.:  U_DEBUG_TZ_MSG(("four is: %d",4));
#define U_DEBUG_TZ_MSG(x) {debug_tz_loc(__FILE__,__LINE__);debug_tz_msg x;}
#else
#define U_DEBUG_TZ_MSG(x)
#endif

#if !UCONFIG_NO_FORMATTING

#include "unicode/simpletz.h"
#include "unicode/calendar.h"
#include "unicode/gregocal.h"
#include "unicode/ures.h"
#include "unicode/tzfmt.h"
#include "gregoimp.h"
#include "uresimp.h" // struct UResourceBundle
#include "olsontz.h"
#include "mutex.h"
#include "unicode/udata.h"
#include "ucln_in.h"
#include "cstring.h"
#include "cmemory.h"
#include "unicode/strenum.h"
#include "uassert.h"
#include "zonemeta.h"

#define kZONEINFO "zoneinfo64"
#define kREGIONS  "Regions"
#define kZONES    "Zones"
#define kRULES    "Rules"
#define kNAMES    "Names"
#define kTZVERSION  "TZVersion"
#define kLINKS    "links"
#define kMAX_CUSTOM_HOUR    23
#define kMAX_CUSTOM_MIN     59
#define kMAX_CUSTOM_SEC     59
#define MINUS 0x002D
#define PLUS 0x002B
#define ZERO_DIGIT 0x0030
#define COLON 0x003A

// Static data and constants

static const char16_t      WORLD[] = {0x30, 0x30, 0x31, 0x00}; /* "001" */

static const char16_t      GMT_ID[] = {0x47, 0x4D, 0x54, 0x00}; /* "GMT" */
static const char16_t      UNKNOWN_ZONE_ID[] = {0x45, 0x74, 0x63, 0x2F, 0x55, 0x6E, 0x6B, 0x6E, 0x6F, 0x77, 0x6E, 0x00}; /* "Etc/Unknown" */
static const int32_t       GMT_ID_LENGTH = 3;
static const int32_t       UNKNOWN_ZONE_ID_LENGTH = 11;

static icu::TimeZone* DEFAULT_ZONE = nullptr;
static icu::UInitOnce gDefaultZoneInitOnce {};

alignas(icu::SimpleTimeZone)
static char gRawGMT[sizeof(icu::SimpleTimeZone)];

alignas(icu::SimpleTimeZone)
static char gRawUNKNOWN[sizeof(icu::SimpleTimeZone)];

static icu::UInitOnce gStaticZonesInitOnce {};
static UBool gStaticZonesInitialized = false; // Whether the static zones are initialized and ready to use.

static char TZDATA_VERSION[16];
static icu::UInitOnce gTZDataVersionInitOnce {};

static int32_t* MAP_SYSTEM_ZONES = nullptr;
static int32_t* MAP_CANONICAL_SYSTEM_ZONES = nullptr;
static int32_t* MAP_CANONICAL_SYSTEM_LOCATION_ZONES = nullptr;

static int32_t LEN_SYSTEM_ZONES = 0;
static int32_t LEN_CANONICAL_SYSTEM_ZONES = 0;
static int32_t LEN_CANONICAL_SYSTEM_LOCATION_ZONES = 0;

static icu::UInitOnce gSystemZonesInitOnce {};
static icu::UInitOnce gCanonicalZonesInitOnce {};
static icu::UInitOnce gCanonicalLocationZonesInitOnce {};

U_CDECL_BEGIN
static UBool U_CALLCONV timeZone_cleanup()
{
    U_NAMESPACE_USE
    delete DEFAULT_ZONE;
    DEFAULT_ZONE = nullptr;
    gDefaultZoneInitOnce.reset();

    if (gStaticZonesInitialized) {
        reinterpret_cast<SimpleTimeZone*>(gRawGMT)->~SimpleTimeZone();
        reinterpret_cast<SimpleTimeZone*>(gRawUNKNOWN)->~SimpleTimeZone();
        gStaticZonesInitialized = false;
        gStaticZonesInitOnce.reset();
    }

    uprv_memset(TZDATA_VERSION, 0, sizeof(TZDATA_VERSION));
    gTZDataVersionInitOnce.reset();

    LEN_SYSTEM_ZONES = 0;
    uprv_free(MAP_SYSTEM_ZONES);
    MAP_SYSTEM_ZONES = nullptr;
    gSystemZonesInitOnce.reset();

    LEN_CANONICAL_SYSTEM_ZONES = 0;
    uprv_free(MAP_CANONICAL_SYSTEM_ZONES);
    MAP_CANONICAL_SYSTEM_ZONES = nullptr;
    gCanonicalZonesInitOnce.reset();

    LEN_CANONICAL_SYSTEM_LOCATION_ZONES = 0;
    uprv_free(MAP_CANONICAL_SYSTEM_LOCATION_ZONES);
    MAP_CANONICAL_SYSTEM_LOCATION_ZONES = nullptr;
    gCanonicalLocationZonesInitOnce.reset();

    return true;
}
U_CDECL_END

U_NAMESPACE_BEGIN

static int32_t findInStringArray(UResourceBundle* array, const UnicodeString& id, UErrorCode &status)
{
    UnicodeString copy;
    const char16_t *u;
    int32_t len;

    int32_t start = 0;
    int32_t limit = ures_getSize(array);
    int32_t mid;
    int32_t lastMid = INT32_MAX;
    if(U_FAILURE(status) || (limit < 1)) {
        return -1;
    }
    U_DEBUG_TZ_MSG(("fisa: Looking for %s, between %d and %d\n", U_DEBUG_TZ_STR(UnicodeString(id).getTerminatedBuffer()), start, limit));

    for (;;) {
        mid = (int32_t)((start + limit) / 2);
        if (lastMid == mid) {   /* Have we moved? */
            break;  /* We haven't moved, and it wasn't found. */
        }
        lastMid = mid;
        u = ures_getStringByIndex(array, mid, &len, &status);
        if (U_FAILURE(status)) {
            break;
        }
        U_DEBUG_TZ_MSG(("tz: compare to %s, %d .. [%d] .. %d\n", U_DEBUG_TZ_STR(u), start, mid, limit));
        copy.setTo(true, u, len);
        int r = id.compare(copy);
        if(r==0) {
            U_DEBUG_TZ_MSG(("fisa: found at %d\n", mid));
            return mid;
        } else if(r<0) {
            limit = mid;
        } else {
            start = mid;
        }
    }
    U_DEBUG_TZ_MSG(("fisa: not found\n"));
    return -1;
}

/**
 * Fetch a specific zone by name.  Replaces the getByKey call.
 * @param top Top timezone resource
 * @param id Time zone ID
 * @param oldbundle Bundle for reuse (or nullptr).   see 'ures_open()'
 * @return the zone's bundle if found, or undefined if error.  Reuses oldbundle.
 */
static UResourceBundle* getZoneByName(const UResourceBundle* top, const UnicodeString& id, UResourceBundle *oldbundle, UErrorCode& status) {
    // load the Rules object
    UResourceBundle *tmp = ures_getByKey(top, kNAMES, nullptr, &status);

    // search for the string
    int32_t idx = findInStringArray(tmp, id, status);

    if((idx == -1) && U_SUCCESS(status)) {
        // not found
        status = U_MISSING_RESOURCE_ERROR;
        //ures_close(oldbundle);
        //oldbundle = nullptr;
    } else {
        U_DEBUG_TZ_MSG(("gzbn: oldbundle= size %d, type %d, %s\n", ures_getSize(tmp), ures_getType(tmp), u_errorName(status)));
        tmp = ures_getByKey(top, kZONES, tmp, &status); // get Zones object from top
        U_DEBUG_TZ_MSG(("gzbn: loaded ZONES, size %d, type %d, path %s %s\n", ures_getSize(tmp), ures_getType(tmp), ures_getPath(tmp), u_errorName(status)));
        oldbundle = ures_getByIndex(tmp, idx, oldbundle, &status); // get nth Zone object
        U_DEBUG_TZ_MSG(("gzbn: loaded z#%d, size %d, type %d, path %s, %s\n", idx, ures_getSize(oldbundle), ures_getType(oldbundle), ures_getPath(oldbundle),  u_errorName(status)));
    }
    ures_close(tmp);
    if(U_FAILURE(status)) {
        //ures_close(oldbundle);
        return nullptr;
    } else {
        return oldbundle;
    }
}


UResourceBundle* TimeZone::loadRule(const UResourceBundle* top, const UnicodeString& ruleid, UResourceBundle* oldbundle, UErrorCode& status) {
    char key[64];
    ruleid.extract(0, sizeof(key)-1, key, (int32_t)sizeof(key)-1, US_INV);
    U_DEBUG_TZ_MSG(("loadRule(%s)\n", key));
    UResourceBundle *r = ures_getByKey(top, kRULES, oldbundle, &status);
    U_DEBUG_TZ_MSG(("loadRule(%s) -> kRULES [%s]\n", key, u_errorName(status)));
    r = ures_getByKey(r, key, r, &status);
    U_DEBUG_TZ_MSG(("loadRule(%s) -> item [%s]\n", key, u_errorName(status)));
    return r;
}

/**
 * Given an ID, open the appropriate resource for the given time zone.
 * Dereference aliases if necessary.
 * @param id zone id
 * @param res resource, which must be ready for use (initialized but not open)
 * @param ec input-output error code
 * @return top-level resource bundle
 */
static UResourceBundle* openOlsonResource(const UnicodeString& id,
                                          UResourceBundle& res,
                                          UErrorCode& ec)
{
#ifdef U_DEBUG_TZ
    char buf[128];
    id.extract(0, sizeof(buf)-1, buf, sizeof(buf), "");
#endif
    UResourceBundle *top = ures_openDirect(nullptr, kZONEINFO, &ec);
    U_DEBUG_TZ_MSG(("pre: res sz=%d\n", ures_getSize(&res)));
    /* &res = */ getZoneByName(top, id, &res, ec);
    // Dereference if this is an alias.  Docs say result should be 1
    // but it is 0 in 2.8 (?).
    U_DEBUG_TZ_MSG(("Loading zone '%s' (%s, size %d) - %s\n", buf, ures_getKey((UResourceBundle*)&res), ures_getSize(&res), u_errorName(ec)));
    if (ures_getType(&res) == URES_INT) {
        int32_t deref = ures_getInt(&res, &ec) + 0;
        U_DEBUG_TZ_MSG(("getInt: %s - type is %d\n", u_errorName(ec), ures_getType(&res)));
        UResourceBundle *ares = ures_getByKey(top, kZONES, nullptr, &ec); // dereference Zones section
        ures_getByIndex(ares, deref, &res, &ec);
        ures_close(ares);
        U_DEBUG_TZ_MSG(("alias to #%d (%s) - %s\n", deref, "??", u_errorName(ec)));
    } else {
        U_DEBUG_TZ_MSG(("not an alias - size %d\n", ures_getSize(&res)));
    }
    U_DEBUG_TZ_MSG(("%s - final status is %s\n", buf, u_errorName(ec)));
    return top;
}

// -------------------------------------

namespace {

void U_CALLCONV initStaticTimeZones() {
    // Initialize _GMT independently of other static data; it should
    // be valid even if we can't load the time zone UDataMemory.
    ucln_i18n_registerCleanup(UCLN_I18N_TIMEZONE, timeZone_cleanup);

    // new can't fail below, as we use placement new into statically allocated space.
    new(gRawGMT) SimpleTimeZone(0, UnicodeString(true, GMT_ID, GMT_ID_LENGTH));
    new(gRawUNKNOWN) SimpleTimeZone(0, UnicodeString(true, UNKNOWN_ZONE_ID, UNKNOWN_ZONE_ID_LENGTH));

    gStaticZonesInitialized = true;
}

}  // anonymous namespace

const TimeZone& U_EXPORT2
TimeZone::getUnknown()
{
    umtx_initOnce(gStaticZonesInitOnce, &initStaticTimeZones);
    return *reinterpret_cast<SimpleTimeZone*>(gRawUNKNOWN);
}

const TimeZone* U_EXPORT2
TimeZone::getGMT()
{
    umtx_initOnce(gStaticZonesInitOnce, &initStaticTimeZones);
    return reinterpret_cast<SimpleTimeZone*>(gRawGMT);
}

// *****************************************************************************
// class TimeZone
// *****************************************************************************

UOBJECT_DEFINE_ABSTRACT_RTTI_IMPLEMENTATION(TimeZone)

TimeZone::TimeZone()
    :   UObject(), fID()
{
}

// -------------------------------------

TimeZone::TimeZone(const UnicodeString &id)
    :   UObject(), fID(id)
{
}

// -------------------------------------

TimeZone::~TimeZone()
{
}

// -------------------------------------

TimeZone::TimeZone(const TimeZone &source)
    :   UObject(source), fID(source.fID)
{
}

// -------------------------------------

TimeZone &
TimeZone::operator=(const TimeZone &right)
{
    if (this != &right) fID = right.fID;
    return *this;
}

// -------------------------------------

bool
TimeZone::operator==(const TimeZone& that) const
{
    return typeid(*this) == typeid(that) &&
        fID == that.fID;
}

// -------------------------------------

namespace {
TimeZone*
createSystemTimeZone(const UnicodeString& id, UErrorCode& ec) {
    if (U_FAILURE(ec)) {
        return nullptr;
    }
    TimeZone* z = nullptr;
    StackUResourceBundle res;
    U_DEBUG_TZ_MSG(("pre-err=%s\n", u_errorName(ec)));
    UResourceBundle *top = openOlsonResource(id, res.ref(), ec);
    U_DEBUG_TZ_MSG(("post-err=%s\n", u_errorName(ec)));
    if (U_SUCCESS(ec)) {
        z = new OlsonTimeZone(top, res.getAlias(), id, ec);
        if (z == nullptr) {
            ec = U_MEMORY_ALLOCATION_ERROR;
            U_DEBUG_TZ_MSG(("cstz: olson time zone failed to initialize - err %s\n", u_errorName(ec)));
        }
    }
    ures_close(top);
    if (U_FAILURE(ec)) {
        U_DEBUG_TZ_MSG(("cstz: failed to create, err %s\n", u_errorName(ec)));
        delete z;
        z = nullptr;
    }
    return z;
}

/**
 * Lookup the given name in our system zone table.  If found,
 * instantiate a new zone of that name and return it.  If not
 * found, return 0.
 */
TimeZone*
createSystemTimeZone(const UnicodeString& id) {
    UErrorCode ec = U_ZERO_ERROR;
    return createSystemTimeZone(id, ec);
}

}

TimeZone* U_EXPORT2
TimeZone::createTimeZone(const UnicodeString& ID)
{
    /* We first try to lookup the zone ID in our system list.  If this
     * fails, we try to parse it as a custom string GMT[+-]hh:mm.  If
     * all else fails, we return GMT, which is probably not what the
     * user wants, but at least is a functioning TimeZone object.
     *
     * We cannot return nullptr, because that would break compatibility
     * with the JDK.
     */
    TimeZone* result = createSystemTimeZone(ID);

    if (result == nullptr) {
        U_DEBUG_TZ_MSG(("failed to load system time zone with id - falling to custom"));
        result = createCustomTimeZone(ID);
    }
    if (result == nullptr) {
        U_DEBUG_TZ_MSG(("failed to load time zone with id - falling to Etc/Unknown(GMT)"));
        const TimeZone& unknown = getUnknown();
        // Unknown zone uses statically allocated memory, so creation of it can never fail due to OOM.
        result = unknown.clone();
    }
    return result;
}

// -------------------------------------

TimeZone* U_EXPORT2
TimeZone::detectHostTimeZone()
{
    // We access system timezone data through uprv_tzset(), uprv_tzname(), and others,
    // which have platform specific implementations in putil.cpp
    int32_t rawOffset = 0;
    const char *hostID;
    UBool hostDetectionSucceeded = true;

    // First, try to create a system timezone, based
    // on the string ID in tzname[0].

    uprv_tzset(); // Initialize tz... system data

    uprv_tzname_clear_cache();

    // Get the timezone ID from the host.  This function should do
    // any required host-specific remapping; e.g., on Windows this
    // function maps the Windows Time Zone name to an ICU timezone ID.
    hostID = uprv_tzname(0);

    // Invert sign because UNIX semantics are backwards
    rawOffset = uprv_timezone() * -U_MILLIS_PER_SECOND;

    TimeZone* hostZone = nullptr;

    UnicodeString hostStrID(hostID, -1, US_INV);

    if (hostStrID.length() == 0) {
        // The host time zone detection (or remapping) above has failed and
        // we have no name at all. Fallback to using the Unknown zone.
        hostStrID = UnicodeString(true, UNKNOWN_ZONE_ID, UNKNOWN_ZONE_ID_LENGTH);
        hostDetectionSucceeded = false;
    }

    hostZone = createSystemTimeZone(hostStrID);

#if U_PLATFORM_USES_ONLY_WIN32_API
    // hostID points to a heap-allocated location on Windows.
    uprv_free(const_cast<char *>(hostID));
#endif

    int32_t hostIDLen = hostStrID.length();
    if (hostZone != nullptr && rawOffset != hostZone->getRawOffset()
        && (3 <= hostIDLen && hostIDLen <= 4))
    {
        // Uh oh. This probably wasn't a good id.
        // It was probably an ambiguous abbreviation
        delete hostZone;
        hostZone = nullptr;
    }

    // Construct a fixed standard zone with the host's ID
    // and raw offset.
    if (hostZone == nullptr && hostDetectionSucceeded) {
        hostZone = new SimpleTimeZone(rawOffset, hostStrID);
    }

    // If we _still_ don't have a time zone, use the Unknown zone.
    //
    // Note: This is extremely unlikely situation. If
    // new SimpleTimeZone(...) above fails, the following
    // code may also fail.
    if (hostZone == nullptr) {
        // Unknown zone uses static allocated memory, so it must always exist.
        // However, clone() allocates memory and can fail.
        hostZone = TimeZone::getUnknown().clone();
    }

    return hostZone;
}

// -------------------------------------

static UMutex gDefaultZoneMutex;

/**
 * Initialize DEFAULT_ZONE from the system default time zone.  
 * Upon return, DEFAULT_ZONE will not be nullptr, unless operator new()
 * returns nullptr.
 */
static void U_CALLCONV initDefault()
{
    ucln_i18n_registerCleanup(UCLN_I18N_TIMEZONE, timeZone_cleanup);

    Mutex lock(&gDefaultZoneMutex);
    // If setDefault() has already been called we can skip getting the
    // default zone information from the system.
    if (DEFAULT_ZONE != nullptr) {
        return;
    }
    
    // NOTE:  this code is safely single threaded, being only
    // run via umtx_initOnce().
    //
    // Some of the locale/timezone OS functions may not be thread safe,
    //
    // The operating system might actually use ICU to implement timezones.
    // So we may have ICU calling ICU here, like on AIX.
    // There shouldn't be a problem with this; initOnce does not hold a mutex
    // while the init function is being run.

    // The code detecting the host time zone was separated from this
    // and implemented as TimeZone::detectHostTimeZone()

    TimeZone *default_zone = TimeZone::detectHostTimeZone();

    U_ASSERT(DEFAULT_ZONE == nullptr);

    DEFAULT_ZONE = default_zone;
}

// -------------------------------------

TimeZone* U_EXPORT2
TimeZone::createDefault()
{
    umtx_initOnce(gDefaultZoneInitOnce, initDefault);
    {
        Mutex lock(&gDefaultZoneMutex);
        return (DEFAULT_ZONE != nullptr) ? DEFAULT_ZONE->clone() : nullptr;
    }
}

// -------------------------------------

TimeZone* U_EXPORT2
TimeZone::forLocaleOrDefault(const Locale& locale)
{
    char buffer[ULOC_KEYWORDS_CAPACITY] = "";
    UErrorCode localStatus = U_ZERO_ERROR;
    int32_t count = locale.getKeywordValue("timezone", buffer, sizeof(buffer), localStatus);
    if (U_FAILURE(localStatus) || localStatus == U_STRING_NOT_TERMINATED_WARNING) {
        // the "timezone" keyword exceeds ULOC_KEYWORDS_CAPACITY; ignore and use default.
        count = 0;
    }
    if (count > 0) {
        return TimeZone::createTimeZone(UnicodeString(buffer, count, US_INV));
    }
    return TimeZone::createDefault();
}

// -------------------------------------

void U_EXPORT2
TimeZone::adoptDefault(TimeZone* zone)
{
    if (zone != nullptr)
    {
        {
            Mutex lock(&gDefaultZoneMutex);
            TimeZone *old = DEFAULT_ZONE;
            DEFAULT_ZONE = zone;
            delete old;
        }
        ucln_i18n_registerCleanup(UCLN_I18N_TIMEZONE, timeZone_cleanup);
    }
}
// -------------------------------------

void U_EXPORT2
TimeZone::setDefault(const TimeZone& zone)
{
    adoptDefault(zone.clone());
}

//----------------------------------------------------------------------


static void U_CALLCONV initMap(USystemTimeZoneType type, UErrorCode& ec) {
    ucln_i18n_registerCleanup(UCLN_I18N_TIMEZONE, timeZone_cleanup);

    UResourceBundle *res = ures_openDirect(nullptr, kZONEINFO, &ec);
    res = ures_getByKey(res, kNAMES, res, &ec); // dereference Zones section
    if (U_SUCCESS(ec)) {
        int32_t size = ures_getSize(res);
        int32_t *m = (int32_t *)uprv_malloc(size * sizeof(int32_t));
        if (m == nullptr) {
            ec = U_MEMORY_ALLOCATION_ERROR;
        } else {
            int32_t numEntries = 0;
            for (int32_t i = 0; i < size; i++) {
                UnicodeString id = ures_getUnicodeStringByIndex(res, i, &ec);
                if (U_FAILURE(ec)) {
                    break;
                }
                if (0 == id.compare(UNKNOWN_ZONE_ID, UNKNOWN_ZONE_ID_LENGTH)) {
                    // exclude Etc/Unknown
                    continue;
                }
                if (type == UCAL_ZONE_TYPE_CANONICAL || type == UCAL_ZONE_TYPE_CANONICAL_LOCATION) {
                    UnicodeString canonicalID;
                    ZoneMeta::getCanonicalCLDRID(id, canonicalID, ec);
                    if (U_FAILURE(ec)) {
                        break;
                    }
                    if (canonicalID != id) {
                        // exclude aliases
                        continue;
                    }
                }
                if (type == UCAL_ZONE_TYPE_CANONICAL_LOCATION) {
                    const char16_t *region = TimeZone::getRegion(id, ec);
                    if (U_FAILURE(ec)) {
                        break;
                    }
                    if (u_strcmp(region, WORLD) == 0) {
                       // exclude non-location ("001")
                        continue;
                    }
                }
                m[numEntries++] = i;
            }
            if (U_SUCCESS(ec)) {
                int32_t *tmp = m;
                m = (int32_t *)uprv_realloc(tmp, numEntries * sizeof(int32_t));
                if (m == nullptr) {
                    // realloc failed.. use the original one even it has unused
                    // area at the end
                    m = tmp;
                }

                switch(type) {
                case UCAL_ZONE_TYPE_ANY:
                    U_ASSERT(MAP_SYSTEM_ZONES == nullptr);
                    MAP_SYSTEM_ZONES = m;
                    LEN_SYSTEM_ZONES = numEntries;
                    break;
                case UCAL_ZONE_TYPE_CANONICAL:
                    U_ASSERT(MAP_CANONICAL_SYSTEM_ZONES == nullptr);
                    MAP_CANONICAL_SYSTEM_ZONES = m;
                    LEN_CANONICAL_SYSTEM_ZONES = numEntries;
                    break;
                case UCAL_ZONE_TYPE_CANONICAL_LOCATION:
                    U_ASSERT(MAP_CANONICAL_SYSTEM_LOCATION_ZONES == nullptr);
                    MAP_CANONICAL_SYSTEM_LOCATION_ZONES = m;
                    LEN_CANONICAL_SYSTEM_LOCATION_ZONES = numEntries;
                    break;
                }
            }
        }
    }
    ures_close(res);
}


/**
 * This is the default implementation for subclasses that do not
 * override this method.  This implementation calls through to the
 * 8-argument getOffset() method after suitable computations, and
 * correctly adjusts GMT millis to local millis when necessary.
 */
void TimeZone::getOffset(UDate date, UBool local, int32_t& rawOffset,
                         int32_t& dstOffset, UErrorCode& ec) const {
    if (U_FAILURE(ec)) {
        return;
    }

    rawOffset = getRawOffset();
    if (!local) {
        date += rawOffset; // now in local standard millis
    }

    // When local == true, date might not be in local standard
    // millis.  getOffset taking 7 parameters used here assume
    // the given time in day is local standard time.
    // At STD->DST transition, there is a range of time which
    // does not exist.  When 'date' is in this time range
    // (and local == true), this method interprets the specified
    // local time as DST.  At DST->STD transition, there is a
    // range of time which occurs twice.  In this case, this
    // method interprets the specified local time as STD.
    // To support the behavior above, we need to call getOffset
    // (with 7 args) twice when local == true and DST is
    // detected in the initial call.
    for (int32_t pass=0; ; ++pass) {
        int32_t year, month, dom, dow, millis;
        double day = ClockMath::floorDivide(date, U_MILLIS_PER_DAY, &millis);

        // out of the range
        if (day < INT32_MIN || day > INT32_MAX) {
            ec = U_ILLEGAL_ARGUMENT_ERROR;
            return;
        }
        Grego::dayToFields(day, year, month, dom, dow);

        dstOffset = getOffset(GregorianCalendar::AD, year, month, dom,
                              (uint8_t) dow, millis,
                              Grego::monthLength(year, month),
                              ec) - rawOffset;

        // Recompute if local==true, dstOffset!=0.
        if (pass!=0 || !local || dstOffset == 0) {
            break;
        }
        // adjust to local standard millis
        date -= dstOffset;
    }
}

// -------------------------------------

// New available IDs API as of ICU 2.4.  Uses StringEnumeration API.

class TZEnumeration : public StringEnumeration {
private:

    // Map into to zones.  Our results are zone[map[i]] for
    // i=0..len-1, where zone[i] is the i-th Olson zone.  If map==nullptr
    // then our results are zone[i] for i=0..len-1.  Len will be zero
    // if the zone data could not be loaded.
    int32_t* map;
    int32_t* localMap;
    int32_t  len;
    int32_t  pos;

    TZEnumeration(int32_t* mapData, int32_t mapLen, UBool adoptMapData) : pos(0) {
        map = mapData;
        localMap = adoptMapData ? mapData : nullptr;
        len = mapLen;
    }

    UBool getID(int32_t i, UErrorCode& ec) {
        int32_t idLen = 0;
        const char16_t* id = nullptr;
        UResourceBundle *top = ures_openDirect(nullptr, kZONEINFO, &ec);
        top = ures_getByKey(top, kNAMES, top, &ec); // dereference Zones section
        id = ures_getStringByIndex(top, i, &idLen, &ec);
        if(U_FAILURE(ec)) {
            unistr.truncate(0);
        }
        else {
            unistr.fastCopyFrom(UnicodeString(true, id, idLen));
        }
        ures_close(top);
        return U_SUCCESS(ec);
    }

    static int32_t* getMap(USystemTimeZoneType type, int32_t& len, UErrorCode& ec) {
        len = 0;
        if (U_FAILURE(ec)) {
            return nullptr;
        }
        int32_t* m = nullptr;
        switch (type) {
        case UCAL_ZONE_TYPE_ANY:
            umtx_initOnce(gSystemZonesInitOnce, &initMap, type, ec);
            m = MAP_SYSTEM_ZONES;
            len = LEN_SYSTEM_ZONES;
            break;
        case UCAL_ZONE_TYPE_CANONICAL:
            umtx_initOnce(gCanonicalZonesInitOnce, &initMap, type, ec);
            m = MAP_CANONICAL_SYSTEM_ZONES;
            len = LEN_CANONICAL_SYSTEM_ZONES;
            break;
        case UCAL_ZONE_TYPE_CANONICAL_LOCATION:
            umtx_initOnce(gCanonicalLocationZonesInitOnce, &initMap, type, ec);
            m = MAP_CANONICAL_SYSTEM_LOCATION_ZONES;
            len = LEN_CANONICAL_SYSTEM_LOCATION_ZONES;
            break;
        default:
            ec = U_ILLEGAL_ARGUMENT_ERROR;
            m = nullptr;
            len = 0;
            break;
        }
        return m;
    }

public:

#define DEFAULT_FILTERED_MAP_SIZE 8
#define MAP_INCREMENT_SIZE 8

    static TZEnumeration* create(USystemTimeZoneType type, const char* region, const int32_t* rawOffset, UErrorCode& ec) {
        if (U_FAILURE(ec)) {
            return nullptr;
        }

        int32_t baseLen;
        int32_t *baseMap = getMap(type, baseLen, ec);

        if (U_FAILURE(ec)) {
            return nullptr;
        }

        // If any additional conditions are available,
        // create instance local map filtered by the conditions.

        int32_t *filteredMap = nullptr;
        int32_t numEntries = 0;

        if (region != nullptr || rawOffset != nullptr) {
            int32_t filteredMapSize = DEFAULT_FILTERED_MAP_SIZE;
            filteredMap = (int32_t *)uprv_malloc(filteredMapSize * sizeof(int32_t));
            if (filteredMap == nullptr) {
                ec = U_MEMORY_ALLOCATION_ERROR;
                return nullptr;
            }

            // Walk through the base map
            UResourceBundle *res = ures_openDirect(nullptr, kZONEINFO, &ec);
            res = ures_getByKey(res, kNAMES, res, &ec); // dereference Zones section
            for (int32_t i = 0; i < baseLen; i++) {
                int32_t zidx = baseMap[i];
                UnicodeString id = ures_getUnicodeStringByIndex(res, zidx, &ec);
                if (U_FAILURE(ec)) {
                    break;
                }
                if (region != nullptr) {
                    // Filter by region
                    char tzregion[4]; // max 3 letters + null term
                    TimeZone::getRegion(id, tzregion, sizeof(tzregion), ec);
                    if (U_FAILURE(ec)) {
                        break;
                    }
                    if (uprv_stricmp(tzregion, region) != 0) {
                        // region does not match
                        continue;
                    }
                }
                if (rawOffset != nullptr) {
                    // Filter by raw offset
                    // Note: This is VERY inefficient
                    TimeZone *z = createSystemTimeZone(id, ec);
                    if (U_FAILURE(ec)) {
                        break;
                    }
                    int32_t tzoffset = z->getRawOffset();
                    delete z;

                    if (tzoffset != *rawOffset) {
                        continue;
                    }
                }

                if (filteredMapSize <= numEntries) {
                    filteredMapSize += MAP_INCREMENT_SIZE;
                    int32_t *tmp = (int32_t *)uprv_realloc(filteredMap, filteredMapSize * sizeof(int32_t));
                    if (tmp == nullptr) {
                        ec = U_MEMORY_ALLOCATION_ERROR;
                        break;
                    } else {
                        filteredMap = tmp;
                    }
                }

                filteredMap[numEntries++] = zidx;
            }

            if (U_FAILURE(ec)) {
                uprv_free(filteredMap);
                filteredMap = nullptr;
            }

            ures_close(res);
        }

        TZEnumeration *result = nullptr;
        if (U_SUCCESS(ec)) {
            // Finally, create a new enumeration instance
            if (filteredMap == nullptr) {
                result = new TZEnumeration(baseMap, baseLen, false);
            } else {
                result = new TZEnumeration(filteredMap, numEntries, true);
                filteredMap = nullptr;
            }
            if (result == nullptr) {
                ec = U_MEMORY_ALLOCATION_ERROR;
            }
        }

        if (filteredMap != nullptr) {
            uprv_free(filteredMap);
        }

        return result;
    }

    TZEnumeration(const TZEnumeration &other) : StringEnumeration(), map(nullptr), localMap(nullptr), len(0), pos(0) {
        if (other.localMap != nullptr) {
            localMap = (int32_t *)uprv_malloc(other.len * sizeof(int32_t));
            if (localMap != nullptr) {
                len = other.len;
                uprv_memcpy(localMap, other.localMap, len * sizeof(int32_t));
                pos = other.pos;
                map = localMap;
            } else {
                len = 0;
                pos = 0;
                map = nullptr;
            }
        } else {
            map = other.map;
            localMap = nullptr;
            len = other.len;
            pos = other.pos;
        }
    }

    virtual ~TZEnumeration();

    virtual StringEnumeration *clone() const override {
        return new TZEnumeration(*this);
    }

    virtual int32_t count(UErrorCode& status) const override {
        return U_FAILURE(status) ? 0 : len;
    }

    virtual const UnicodeString* snext(UErrorCode& status) override {
        if (U_SUCCESS(status) && map != nullptr && pos < len) {
            getID(map[pos], status);
            ++pos;
            return &unistr;
        }
        return nullptr;
    }

    virtual void reset(UErrorCode& /*status*/) override {
        pos = 0;
    }

public:
    static UClassID U_EXPORT2 getStaticClassID();
    virtual UClassID getDynamicClassID() const override;
};

TZEnumeration::~TZEnumeration() {
    if (localMap != nullptr) {
        uprv_free(localMap);
    }
}

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(TZEnumeration)

StringEnumeration* U_EXPORT2
TimeZone::createTimeZoneIDEnumeration(
            USystemTimeZoneType zoneType,
            const char* region,
            const int32_t* rawOffset,
            UErrorCode& ec) {
    return TZEnumeration::create(zoneType, region, rawOffset, ec);
}

StringEnumeration* U_EXPORT2
TimeZone::createEnumeration(UErrorCode& status) {
    return TZEnumeration::create(UCAL_ZONE_TYPE_ANY, nullptr, nullptr, status);
}

StringEnumeration* U_EXPORT2
TimeZone::createEnumerationForRawOffset(int32_t rawOffset, UErrorCode& status) {
    return TZEnumeration::create(UCAL_ZONE_TYPE_ANY, nullptr, &rawOffset, status);
}

StringEnumeration* U_EXPORT2
TimeZone::createEnumerationForRegion(const char* region, UErrorCode& status) {
    return TZEnumeration::create(UCAL_ZONE_TYPE_ANY, region, nullptr, status);
}

//
// Next 3 methods are equivalent to above, but ignores UErrorCode.
// These methods were deprecated in ICU 70.

StringEnumeration* U_EXPORT2
TimeZone::createEnumeration() {
    UErrorCode ec = U_ZERO_ERROR;
    return createEnumeration(ec);
}

StringEnumeration* U_EXPORT2
TimeZone::createEnumeration(int32_t rawOffset) {
    UErrorCode ec = U_ZERO_ERROR;
    return createEnumerationForRawOffset(rawOffset, ec);
}

StringEnumeration* U_EXPORT2
TimeZone::createEnumeration(const char* region) {
    UErrorCode ec = U_ZERO_ERROR;
    return createEnumerationForRegion(region, ec);
}

// ---------------------------------------

int32_t U_EXPORT2
TimeZone::countEquivalentIDs(const UnicodeString& id) {
    int32_t result = 0;
    UErrorCode ec = U_ZERO_ERROR;
    StackUResourceBundle res;
    U_DEBUG_TZ_MSG(("countEquivalentIDs..\n"));
    UResourceBundle *top = openOlsonResource(id, res.ref(), ec);
    if (U_SUCCESS(ec)) {
        StackUResourceBundle r;
        ures_getByKey(res.getAlias(), kLINKS, r.getAlias(), &ec);
        ures_getIntVector(r.getAlias(), &result, &ec);
    }
    ures_close(top);
    return result;
}

// ---------------------------------------

const UnicodeString U_EXPORT2
TimeZone::getEquivalentID(const UnicodeString& id, int32_t index) {
    U_DEBUG_TZ_MSG(("gEI(%d)\n", index));
    UnicodeString result;
    UErrorCode ec = U_ZERO_ERROR;
    StackUResourceBundle res;
    UResourceBundle *top = openOlsonResource(id, res.ref(), ec);
    int32_t zone = -1;
    if (U_SUCCESS(ec)) {
        StackUResourceBundle r;
        int32_t size;
        ures_getByKey(res.getAlias(), kLINKS, r.getAlias(), &ec);
        const int32_t *v = ures_getIntVector(r.getAlias(), &size, &ec);
        if (U_SUCCESS(ec)) {
            if (index >= 0 && index < size) {
                zone = v[index];
            }
        }
    }
    if (zone >= 0) {
        UResourceBundle *ares = ures_getByKey(top, kNAMES, nullptr, &ec); // dereference Zones section
        if (U_SUCCESS(ec)) {
            int32_t idLen = 0;
            const char16_t* id2 = ures_getStringByIndex(ares, zone, &idLen, &ec);
            result.fastCopyFrom(UnicodeString(true, id2, idLen));
            U_DEBUG_TZ_MSG(("gei(%d) -> %d, len%d, %s\n", index, zone, result.length(), u_errorName(ec)));
        }
        ures_close(ares);
    }
    ures_close(top);
#if defined(U_DEBUG_TZ)
    if(result.length() ==0) {
      U_DEBUG_TZ_MSG(("equiv [__, #%d] -> 0 (%s)\n", index, u_errorName(ec)));
    }
#endif
    return result;
}

// ---------------------------------------

// These methods are used by ZoneMeta class only.

const char16_t*
TimeZone::findID(const UnicodeString& id) {
    const char16_t *result = nullptr;
    UErrorCode ec = U_ZERO_ERROR;
    UResourceBundle *rb = ures_openDirect(nullptr, kZONEINFO, &ec);

    // resolve zone index by name
    UResourceBundle *names = ures_getByKey(rb, kNAMES, nullptr, &ec);
    int32_t idx = findInStringArray(names, id, ec);
    result = ures_getStringByIndex(names, idx, nullptr, &ec);
    if (U_FAILURE(ec)) {
        result = nullptr;
    }
    ures_close(names);
    ures_close(rb);
    return result;
}


const char16_t*
TimeZone::dereferOlsonLink(const UnicodeString& id) {
    const char16_t *result = nullptr;
    UErrorCode ec = U_ZERO_ERROR;
    UResourceBundle *rb = ures_openDirect(nullptr, kZONEINFO, &ec);

    // resolve zone index by name
    UResourceBundle *names = ures_getByKey(rb, kNAMES, nullptr, &ec);
    int32_t idx = findInStringArray(names, id, ec);
    result = ures_getStringByIndex(names, idx, nullptr, &ec);

    // open the zone bundle by index
    ures_getByKey(rb, kZONES, rb, &ec);
    ures_getByIndex(rb, idx, rb, &ec); 

    if (U_SUCCESS(ec)) {
        if (ures_getType(rb) == URES_INT) {
            // this is a link - dereference the link
            int32_t deref = ures_getInt(rb, &ec);
            const char16_t* tmp = ures_getStringByIndex(names, deref, nullptr, &ec);
            if (U_SUCCESS(ec)) {
                result = tmp;
            }
        }
    }

    ures_close(names);
    ures_close(rb);

    return result;
}

const char16_t*
TimeZone::getRegion(const UnicodeString& id) {
    UErrorCode status = U_ZERO_ERROR;
    return getRegion(id, status);
}

const char16_t*
TimeZone::getRegion(const UnicodeString& id, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    const char16_t *result = nullptr;
    UResourceBundle *rb = ures_openDirect(nullptr, kZONEINFO, &status);

    // resolve zone index by name
    UResourceBundle *res = ures_getByKey(rb, kNAMES, nullptr, &status);
    int32_t idx = findInStringArray(res, id, status);

    // get region mapping
    ures_getByKey(rb, kREGIONS, res, &status);
    const char16_t *tmp = ures_getStringByIndex(res, idx, nullptr, &status);
    if (U_SUCCESS(status)) {
        result = tmp;
    }

    ures_close(res);
    ures_close(rb);

    return result;
}


// ---------------------------------------
int32_t
TimeZone::getRegion(const UnicodeString& id, char *region, int32_t capacity, UErrorCode& status)
{
    int32_t resultLen = 0;
    *region = 0;
    if (U_FAILURE(status)) {
        return 0;
    }

    const char16_t *uregion = nullptr;
    // "Etc/Unknown" is not a system zone ID,
    // but in the zone data
    if (id.compare(UNKNOWN_ZONE_ID, UNKNOWN_ZONE_ID_LENGTH) != 0) {
        uregion = getRegion(id);
    }
    if (uregion == nullptr) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    resultLen = u_strlen(uregion);
    // A region code is represented by invariant characters
    u_UCharsToChars(uregion, region, uprv_min(resultLen, capacity));

    if (capacity < resultLen) {
        status = U_BUFFER_OVERFLOW_ERROR;
        return resultLen;
    }

    return u_terminateChars(region, capacity, resultLen, &status);
}

// ---------------------------------------


UnicodeString&
TimeZone::getDisplayName(UnicodeString& result) const
{
    return getDisplayName(false,LONG,Locale::getDefault(), result);
}

UnicodeString&
TimeZone::getDisplayName(const Locale& locale, UnicodeString& result) const
{
    return getDisplayName(false, LONG, locale, result);
}

UnicodeString&
TimeZone::getDisplayName(UBool inDaylight, EDisplayType style, UnicodeString& result)  const
{
    return getDisplayName(inDaylight,style, Locale::getDefault(), result);
}
//--------------------------------------
int32_t
TimeZone::getDSTSavings()const {
    if (useDaylightTime()) {
        return 3600000;
    }
    return 0;
}
//---------------------------------------
UnicodeString&
TimeZone::getDisplayName(UBool inDaylight, EDisplayType style, const Locale& locale, UnicodeString& result) const
{
    UErrorCode status = U_ZERO_ERROR;
    UDate date = Calendar::getNow();
    UTimeZoneFormatTimeType timeType = UTZFMT_TIME_TYPE_UNKNOWN;
    int32_t offset;

    if (style == GENERIC_LOCATION || style == LONG_GENERIC || style == SHORT_GENERIC) {
        LocalPointer<TimeZoneFormat> tzfmt(TimeZoneFormat::createInstance(locale, status));
        if (U_FAILURE(status)) {
            result.remove();
            return result;
        }
        // Generic format
        switch (style) {
        case GENERIC_LOCATION:
            tzfmt->format(UTZFMT_STYLE_GENERIC_LOCATION, *this, date, result, &timeType);
            break;
        case LONG_GENERIC:
            tzfmt->format(UTZFMT_STYLE_GENERIC_LONG, *this, date, result, &timeType);
            break;
        case SHORT_GENERIC:
            tzfmt->format(UTZFMT_STYLE_GENERIC_SHORT, *this, date, result, &timeType);
            break;
        default:
            UPRV_UNREACHABLE_EXIT;
        }
        // Generic format many use Localized GMT as the final fallback.
        // When Localized GMT format is used, the result might not be
        // appropriate for the requested daylight value.
        if ((inDaylight && timeType == UTZFMT_TIME_TYPE_STANDARD) || (!inDaylight && timeType == UTZFMT_TIME_TYPE_DAYLIGHT)) {
            offset = inDaylight ? getRawOffset() + getDSTSavings() : getRawOffset();
            if (style == SHORT_GENERIC) {
                tzfmt->formatOffsetShortLocalizedGMT(offset, result, status);
            } else {
                tzfmt->formatOffsetLocalizedGMT(offset, result, status);
            }
        }
    } else if (style == LONG_GMT || style == SHORT_GMT) {
        LocalPointer<TimeZoneFormat> tzfmt(TimeZoneFormat::createInstance(locale, status));
        if (U_FAILURE(status)) {
            result.remove();
            return result;
        }
        offset = inDaylight && useDaylightTime() ? getRawOffset() + getDSTSavings() : getRawOffset();
        switch (style) {
        case LONG_GMT:
            tzfmt->formatOffsetLocalizedGMT(offset, result, status);
            break;
        case SHORT_GMT:
            tzfmt->formatOffsetISO8601Basic(offset, false, false, false, result, status);
            break;
        default:
            UPRV_UNREACHABLE_EXIT;
        }

    } else {
        U_ASSERT(style == LONG || style == SHORT || style == SHORT_COMMONLY_USED);
        UTimeZoneNameType nameType = UTZNM_UNKNOWN;
        switch (style) {
        case LONG:
            nameType = inDaylight ? UTZNM_LONG_DAYLIGHT : UTZNM_LONG_STANDARD;
            break;
        case SHORT:
        case SHORT_COMMONLY_USED:
            nameType = inDaylight ? UTZNM_SHORT_DAYLIGHT : UTZNM_SHORT_STANDARD;
            break;
        default:
            UPRV_UNREACHABLE_EXIT;
        }
        LocalPointer<TimeZoneNames> tznames(TimeZoneNames::createInstance(locale, status));
        if (U_FAILURE(status)) {
            result.remove();
            return result;
        }
        UnicodeString canonicalID(ZoneMeta::getCanonicalCLDRID(*this));
        tznames->getDisplayName(canonicalID, nameType, date, result);
        if (result.isEmpty()) {
            // Fallback to localized GMT
            LocalPointer<TimeZoneFormat> tzfmt(TimeZoneFormat::createInstance(locale, status));
            offset = inDaylight && useDaylightTime() ? getRawOffset() + getDSTSavings() : getRawOffset();
            if (style == LONG) {
                tzfmt->formatOffsetLocalizedGMT(offset, result, status);
            } else {
                tzfmt->formatOffsetShortLocalizedGMT(offset, result, status);
            }
        }
    }
    if (U_FAILURE(status)) {
        result.remove();
    }
    return  result;
}

/**
 * Parse a custom time zone identifier and return a corresponding zone.
 * @param id a string of the form GMT[+-]hh:mm, GMT[+-]hhmm, or
 * GMT[+-]hh.
 * @return a newly created SimpleTimeZone with the given offset and
 * no Daylight Savings Time, or null if the id cannot be parsed.
*/
TimeZone*
TimeZone::createCustomTimeZone(const UnicodeString& id)
{
    int32_t sign, hour, min, sec;
    if (parseCustomID(id, sign, hour, min, sec)) {
        UnicodeString customID;
        formatCustomID(hour, min, sec, (sign < 0), customID);
        int32_t offset = sign * ((hour * 60 + min) * 60 + sec) * 1000;
        return new SimpleTimeZone(offset, customID);
    }
    return nullptr;
}

UnicodeString&
TimeZone::getCustomID(const UnicodeString& id, UnicodeString& normalized, UErrorCode& status) {
    normalized.remove();
    if (U_FAILURE(status)) {
        return normalized;
    }
    int32_t sign, hour, min, sec;
    if (parseCustomID(id, sign, hour, min, sec)) {
        formatCustomID(hour, min, sec, (sign < 0), normalized);
    } else {
        status = U_ILLEGAL_ARGUMENT_ERROR;
    }
    return normalized;
}

UBool
TimeZone::parseCustomID(const UnicodeString& id, int32_t& sign,
                        int32_t& hour, int32_t& min, int32_t& sec) {
    if (id.length() < GMT_ID_LENGTH) {
      return false;
    }
    if (0 != u_strncasecmp(id.getBuffer(), GMT_ID, GMT_ID_LENGTH, 0)) {
        return false;
    }
    sign = 1;
    hour = 0;
    min = 0;
    sec = 0;

    if (id[GMT_ID_LENGTH] == MINUS /*'-'*/) {
        sign = -1;
    } else if (id[GMT_ID_LENGTH] != PLUS /*'+'*/) {
        return false;
    }

    int32_t start = GMT_ID_LENGTH + 1;
    int32_t pos = start;
    hour = ICU_Utility::parseNumber(id, pos, 10);
    if (pos == id.length()) {
        // Handle the following cases
        // HHmmss
        // Hmmss
        // HHmm
        // Hmm
        // HH
        // H

        // Get all digits
        // Should be 1 to 6 digits.
        int32_t length = pos - start;
        switch (length) {
            case 1:  // H
            case 2:  // HH
                // already set to hour
                break;
            case 3:  // Hmm
            case 4:  // HHmm
                min = hour % 100;
                hour /= 100;
                break;
            case 5:  // Hmmss
            case 6:  // HHmmss
                sec = hour % 100;
                min = (hour/100) % 100;
                hour /= 10000;
                break;
            default:
                // invalid range
                return false;
        }
    } else {
        // Handle the following cases
        // HH:mm:ss
        // H:mm:ss
        // HH:mm
        // H:mm
        if (pos - start < 1 || pos - start > 2 || id[pos] != COLON) {
            return false;
        }
        pos++; // skip : after H or HH
        if (id.length() == pos) {
            return false;
        }
        start = pos;
        min = ICU_Utility::parseNumber(id, pos, 10);
        if (pos - start != 2) {
            return false;
        }
        if (id.length() > pos) {
            if (id[pos] != COLON) {
                return false;
            }
            pos++; // skip : after mm
            start = pos;
            sec = ICU_Utility::parseNumber(id, pos, 10);
            if (pos - start != 2 || id.length() > pos) {
                return false;
            }
        }
    }
    if (hour > kMAX_CUSTOM_HOUR ||
        min > kMAX_CUSTOM_MIN ||
        sec > kMAX_CUSTOM_SEC) {
        return false;
    }
    return true;
}

UnicodeString&
TimeZone::formatCustomID(int32_t hour, int32_t min, int32_t sec,
                         UBool negative, UnicodeString& id) {
    // Create time zone ID - GMT[+|-]hhmm[ss]
    id.setTo(GMT_ID, GMT_ID_LENGTH);
    if (hour | min | sec) {
        if (negative) {
            id += (char16_t)MINUS;
        } else {
            id += (char16_t)PLUS;
        }

        if (hour < 10) {
            id += (char16_t)ZERO_DIGIT;
        } else {
            id += (char16_t)(ZERO_DIGIT + hour/10);
        }
        id += (char16_t)(ZERO_DIGIT + hour%10);
        id += (char16_t)COLON;
        if (min < 10) {
            id += (char16_t)ZERO_DIGIT;
        } else {
            id += (char16_t)(ZERO_DIGIT + min/10);
        }
        id += (char16_t)(ZERO_DIGIT + min%10);

        if (sec) {
            id += (char16_t)COLON;
            if (sec < 10) {
                id += (char16_t)ZERO_DIGIT;
            } else {
                id += (char16_t)(ZERO_DIGIT + sec/10);
            }
            id += (char16_t)(ZERO_DIGIT + sec%10);
        }
    }
    return id;
}


UBool
TimeZone::hasSameRules(const TimeZone& other) const
{
    return (getRawOffset() == other.getRawOffset() &&
            useDaylightTime() == other.useDaylightTime());
}

static void U_CALLCONV initTZDataVersion(UErrorCode &status) {
    ucln_i18n_registerCleanup(UCLN_I18N_TIMEZONE, timeZone_cleanup);
    int32_t len = 0;
    StackUResourceBundle bundle;
    ures_openDirectFillIn(bundle.getAlias(), nullptr, kZONEINFO, &status);
    const char16_t *tzver = ures_getStringByKey(bundle.getAlias(), kTZVERSION, &len, &status);

    if (U_SUCCESS(status)) {
        if (len >= (int32_t)sizeof(TZDATA_VERSION)) {
            // Ensure that there is always space for a trailing nul in TZDATA_VERSION
            len = sizeof(TZDATA_VERSION) - 1;
        }
        u_UCharsToChars(tzver, TZDATA_VERSION, len);
    }
}

const char*
TimeZone::getTZDataVersion(UErrorCode& status)
{
    umtx_initOnce(gTZDataVersionInitOnce, &initTZDataVersion, status);
    return (const char*)TZDATA_VERSION;
}

UnicodeString&
TimeZone::getCanonicalID(const UnicodeString& id, UnicodeString& canonicalID, UErrorCode& status)
{
    UBool isSystemID = false;
    return getCanonicalID(id, canonicalID, isSystemID, status);
}

UnicodeString&
TimeZone::getCanonicalID(const UnicodeString& id, UnicodeString& canonicalID, UBool& isSystemID,
                         UErrorCode& status)
{
    canonicalID.remove();
    isSystemID = false;
    if (U_FAILURE(status)) {
        return canonicalID;
    }
    if (id.compare(UNKNOWN_ZONE_ID, UNKNOWN_ZONE_ID_LENGTH) == 0) {
        // special case - Etc/Unknown is a canonical ID, but not system ID
        canonicalID.fastCopyFrom(id);
        isSystemID = false;
    } else {
        ZoneMeta::getCanonicalCLDRID(id, canonicalID, status);
        if (U_SUCCESS(status)) {
            isSystemID = true;
        } else {
            // Not a system ID
            status = U_ZERO_ERROR;
            getCustomID(id, canonicalID, status);
        }
    }
    return canonicalID;
}

UnicodeString&
TimeZone::getIanaID(const UnicodeString& id, UnicodeString& ianaID, UErrorCode& status)
{
    ianaID.remove();
    if (U_FAILURE(status)) {
        return ianaID;
    }
    if (id.compare(ConstChar16Ptr(UNKNOWN_ZONE_ID), UNKNOWN_ZONE_ID_LENGTH) == 0) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        ianaID.setToBogus();
    } else {
        ZoneMeta::getIanaID(id, ianaID, status);
    }
    return ianaID;
}

UnicodeString&
TimeZone::getWindowsID(const UnicodeString& id, UnicodeString& winid, UErrorCode& status) {
    winid.remove();
    if (U_FAILURE(status)) {
        return winid;
    }

    // canonicalize the input ID
    UnicodeString canonicalID;
    UBool isSystemID = false;

    getCanonicalID(id, canonicalID, isSystemID, status);
    if (U_FAILURE(status) || !isSystemID) {
        // mapping data is only applicable to tz database IDs
        if (status == U_ILLEGAL_ARGUMENT_ERROR) {
            // getWindowsID() sets an empty string where
            // getCanonicalID() sets a U_ILLEGAL_ARGUMENT_ERROR.
            status = U_ZERO_ERROR;
        }
        return winid;
    }

    UResourceBundle *mapTimezones = ures_openDirect(nullptr, "windowsZones", &status);
    ures_getByKey(mapTimezones, "mapTimezones", mapTimezones, &status);

    if (U_FAILURE(status)) {
        return winid;
    }

    UResourceBundle *winzone = nullptr;
    UBool found = false;
    while (ures_hasNext(mapTimezones) && !found) {
        winzone = ures_getNextResource(mapTimezones, winzone, &status);
        if (U_FAILURE(status)) {
            break;
        }
        if (ures_getType(winzone) != URES_TABLE) {
            continue;
        }
        UResourceBundle *regionalData = nullptr;
        while (ures_hasNext(winzone) && !found) {
            regionalData = ures_getNextResource(winzone, regionalData, &status);
            if (U_FAILURE(status)) {
                break;
            }
            if (ures_getType(regionalData) != URES_STRING) {
                continue;
            }
            int32_t len;
            const char16_t *tzids = ures_getString(regionalData, &len, &status);
            if (U_FAILURE(status)) {
                break;
            }

            const char16_t *start = tzids;
            UBool hasNext = true;
            while (hasNext) {
                const char16_t *end = u_strchr(start, (char16_t)0x20);
                if (end == nullptr) {
                    end = tzids + len;
                    hasNext = false;
                }
                if (canonicalID.compare(start, static_cast<int32_t>(end - start)) == 0) {
                    winid = UnicodeString(ures_getKey(winzone), -1 , US_INV);
                    found = true;
                    break;
                }
                start = end + 1;
            }
        }
        ures_close(regionalData);
    }
    ures_close(winzone);
    ures_close(mapTimezones);

    return winid;
}

#define MAX_WINDOWS_ID_SIZE 128

UnicodeString&
TimeZone::getIDForWindowsID(const UnicodeString& winid, const char* region, UnicodeString& id, UErrorCode& status) {
    id.remove();
    if (U_FAILURE(status)) {
        return id;
    }

    UResourceBundle *zones = ures_openDirect(nullptr, "windowsZones", &status);
    ures_getByKey(zones, "mapTimezones", zones, &status);
    if (U_FAILURE(status)) {
        ures_close(zones);
        return id;
    }

    UErrorCode tmperr = U_ZERO_ERROR;
    char winidKey[MAX_WINDOWS_ID_SIZE];
    int32_t winKeyLen = winid.extract(0, winid.length(), winidKey, sizeof(winidKey) - 1, US_INV);

    if (winKeyLen == 0 || winKeyLen >= (int32_t)sizeof(winidKey)) {
        ures_close(zones);
        return id;
    }
    winidKey[winKeyLen] = 0;

    ures_getByKey(zones, winidKey, zones, &tmperr); // use tmperr, because windows mapping might not
                                                    // be available by design
    if (U_FAILURE(tmperr)) {
        ures_close(zones);
        return id;
    }

    const char16_t *tzid = nullptr;
    int32_t len = 0;
    UBool gotID = false;
    if (region) {
        const char16_t *tzids = ures_getStringByKey(zones, region, &len, &tmperr); // use tmperr, because
                                                                                // regional mapping is optional
        if (U_SUCCESS(tmperr)) {
            // first ID delimited by space is the default one
            const char16_t *end = u_strchr(tzids, (char16_t)0x20);
            if (end == nullptr) {
                id.setTo(tzids, -1);
            } else {
                id.setTo(tzids, static_cast<int32_t>(end - tzids));
            }
            gotID = true;
        }
    }

    if (!gotID) {
        tzid = ures_getStringByKey(zones, "001", &len, &status);    // using status, because "001" must be
                                                                // available at this point
        if (U_SUCCESS(status)) {
            id.setTo(tzid, len);
        }
    }

    ures_close(zones);
    return id;
}


U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */

//eof
