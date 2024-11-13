// © 2018 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include <utility>

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include <stdlib.h>
#include "unicode/ucal.h"
#include "unicode/ures.h"
#include "unicode/ustring.h"
#include "unicode/timezone.h"
#include "cmemory.h"
#include "cstring.h"
#include "erarules.h"
#include "gregoimp.h"
#include "uassert.h"

U_NAMESPACE_BEGIN

static const int32_t MAX_ENCODED_START_YEAR = 32767;
static const int32_t MIN_ENCODED_START_YEAR = -32768;
static const int32_t MIN_ENCODED_START = -2147483391;   // encodeDate(MIN_ENCODED_START_YEAR, 1, 1, ...);

static const int32_t YEAR_MASK = 0xFFFF0000;
static const int32_t MONTH_MASK = 0x0000FF00;
static const int32_t DAY_MASK = 0x000000FF;

static const int32_t MAX_INT32 = 0x7FFFFFFF;
static const int32_t MIN_INT32 = 0xFFFFFFFF;

static const char16_t VAL_FALSE[] = {0x66, 0x61, 0x6c, 0x73, 0x65};    // "false"
static const char16_t VAL_FALSE_LEN = 5;

static UBool isSet(int startDate) {
    return startDate != 0;
}

static UBool isValidRuleStartDate(int32_t year, int32_t month, int32_t day) {
    return year >= MIN_ENCODED_START_YEAR && year <= MAX_ENCODED_START_YEAR
            && month >= 1 && month <= 12 && day >=1 && day <= 31;
}

/**
 * Encode year/month/date to a single integer.
 * year is high 16 bits (-32768 to 32767), month is
 * next 8 bits and day of month is last 8 bits.
 *
 * @param year  year
 * @param month month (1-base)
 * @param day   day of month
 * @return  an encoded date.
 */
static int32_t encodeDate(int32_t year, int32_t month, int32_t day) {
    return static_cast<int32_t>(static_cast<uint32_t>(year) << 16) | month << 8 | day;
}

static void decodeDate(int32_t encodedDate, int32_t (&fields)[3]) {
    if (encodedDate == MIN_ENCODED_START) {
        fields[0] = MIN_INT32;
        fields[1] = 1;
        fields[2] = 1;
    } else {
        fields[0] = (encodedDate & YEAR_MASK) >> 16;
        fields[1] = (encodedDate & MONTH_MASK) >> 8;
        fields[2] = encodedDate & DAY_MASK;
    }
}

/**
 * Compare an encoded date with another date specified by year/month/day.
 * @param encoded   An encoded date
 * @param year      Year of another date
 * @param month     Month of another date
 * @param day       Day of another date
 * @return -1 when encoded date is earlier, 0 when two dates are same,
 *          and 1 when encoded date is later.
 */
static int32_t compareEncodedDateWithYMD(int encoded, int year, int month, int day) {
    if (year < MIN_ENCODED_START_YEAR) {
        if (encoded == MIN_ENCODED_START) {
            if (year > MIN_INT32 || month > 1 || day > 1) {
                return -1;
            }
            return 0;
        } else {
            return 1;
        }
    } else if (year > MAX_ENCODED_START_YEAR) {
        return -1;
    } else {
        int tmp = encodeDate(year, month, day);
        if (encoded < tmp) {
            return -1;
        } else if (encoded == tmp) {
            return 0;
        } else {
            return 1;
        }
    }
}

EraRules::EraRules(LocalMemory<int32_t>& eraStartDates, int32_t numEras)
    : numEras(numEras) {
    startDates = std::move(eraStartDates);
    initCurrentEra();
}

EraRules::~EraRules() {
}

EraRules* EraRules::createInstance(const char *calType, UBool includeTentativeEra, UErrorCode& status) {
    if(U_FAILURE(status)) {
        return nullptr;
    }
    LocalUResourceBundlePointer rb(ures_openDirect(nullptr, "supplementalData", &status));
    ures_getByKey(rb.getAlias(), "calendarData", rb.getAlias(), &status);
    ures_getByKey(rb.getAlias(), calType, rb.getAlias(), &status);
    ures_getByKey(rb.getAlias(), "eras", rb.getAlias(), &status);

    if (U_FAILURE(status)) {
        return nullptr;
    }

    int32_t numEras = ures_getSize(rb.getAlias());
    int32_t firstTentativeIdx = MAX_INT32;

    LocalMemory<int32_t> startDates(static_cast<int32_t *>(uprv_malloc(numEras * sizeof(int32_t))));
    if (startDates.isNull()) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    uprv_memset(startDates.getAlias(), 0 , numEras * sizeof(int32_t));

    while (ures_hasNext(rb.getAlias())) {
        LocalUResourceBundlePointer eraRuleRes(ures_getNextResource(rb.getAlias(), nullptr, &status));
        if (U_FAILURE(status)) {
            return nullptr;
        }
        const char *eraIdxStr = ures_getKey(eraRuleRes.getAlias());
        char *endp;
        int32_t eraIdx = static_cast<int32_t>(strtol(eraIdxStr, &endp, 10));
        if (static_cast<size_t>(endp - eraIdxStr) != uprv_strlen(eraIdxStr)) {
            status = U_INVALID_FORMAT_ERROR;
            return nullptr;
        }
        if (eraIdx < 0 || eraIdx >= numEras) {
            status = U_INVALID_FORMAT_ERROR;
            return nullptr;
        }
        if (isSet(startDates[eraIdx])) {
            // start date of the index was already set
            status = U_INVALID_FORMAT_ERROR;
            return nullptr;
        }

        UBool hasName = true;
        UBool hasEnd = true;
        int32_t len;
        while (ures_hasNext(eraRuleRes.getAlias())) {
            LocalUResourceBundlePointer res(ures_getNextResource(eraRuleRes.getAlias(), nullptr, &status));
            if (U_FAILURE(status)) {
                return nullptr;
            }
            const char *key = ures_getKey(res.getAlias());
            if (uprv_strcmp(key, "start") == 0) {
                const int32_t *fields = ures_getIntVector(res.getAlias(), &len, &status);
                if (U_FAILURE(status)) {
                    return nullptr;
                }
                if (len != 3 || !isValidRuleStartDate(fields[0], fields[1], fields[2])) {
                    status = U_INVALID_FORMAT_ERROR;
                    return nullptr;
                }
                startDates[eraIdx] = encodeDate(fields[0], fields[1], fields[2]);
            } else if (uprv_strcmp(key, "named") == 0) {
                const char16_t *val = ures_getString(res.getAlias(), &len, &status);
                if (u_strncmp(val, VAL_FALSE, VAL_FALSE_LEN) == 0) {
                    hasName = false;
                }
            } else if (uprv_strcmp(key, "end") == 0) {
                hasEnd = true;
            }
        }

        if (isSet(startDates[eraIdx])) {
            if (hasEnd) {
                // This implementation assumes either start or end is available, not both.
                // For now, just ignore the end rule.
            }
        } else {
            if (hasEnd) {
                if (eraIdx != 0) {
                    // This implementation does not support end only rule for eras other than
                    // the first one.
                    status = U_INVALID_FORMAT_ERROR;
                    return nullptr;
                }
                U_ASSERT(eraIdx == 0);
                startDates[eraIdx] = MIN_ENCODED_START;
            } else {
                status = U_INVALID_FORMAT_ERROR;
                return nullptr;
            }
        }

        if (hasName) {
            if (eraIdx >= firstTentativeIdx) {
                status = U_INVALID_FORMAT_ERROR;
                return nullptr;
            }
        } else {
            if (eraIdx < firstTentativeIdx) {
                firstTentativeIdx = eraIdx;
            }
        }
    }

    EraRules *result;
    if (firstTentativeIdx < MAX_INT32 && !includeTentativeEra) {
        result = new EraRules(startDates, firstTentativeIdx);
    } else {
        result = new EraRules(startDates, numEras);
    }

    if (result == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
    }
    return result;
}

void EraRules::getStartDate(int32_t eraIdx, int32_t (&fields)[3], UErrorCode& status) const {
    if(U_FAILURE(status)) {
        return;
    }
    if (eraIdx < 0 || eraIdx >= numEras) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    decodeDate(startDates[eraIdx], fields);
}

int32_t EraRules::getStartYear(int32_t eraIdx, UErrorCode& status) const {
    int year = MAX_INT32;   // bogus value
    if(U_FAILURE(status)) {
        return year;
    }
    if (eraIdx < 0 || eraIdx >= numEras) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return year;
    }
    int fields[3];
    decodeDate(startDates[eraIdx], fields);
    year = fields[0];

    return year;
}

int32_t EraRules::getEraIndex(int32_t year, int32_t month, int32_t day, UErrorCode& status) const {
    if(U_FAILURE(status)) {
        return -1;
    }

    if (month < 1 || month > 12 || day < 1 || day > 31) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return -1;
    }
    int32_t high = numEras; // last index + 1
    int32_t low;

    // Short circuit for recent years.  Most modern computations will
    // occur in the last few eras.
    if (compareEncodedDateWithYMD(startDates[getCurrentEraIndex()], year, month, day) <= 0) {
        low = getCurrentEraIndex();
    } else {
        low = 0;
    }

    // Do binary search
    while (low < high - 1) {
        int i = (low + high) / 2;
        if (compareEncodedDateWithYMD(startDates[i], year, month, day) <= 0) {
            low = i;
        } else {
            high = i;
        }
    }
    return low;
}

void EraRules::initCurrentEra() {
    // Compute local wall time in millis using ICU's default time zone.
    UErrorCode ec = U_ZERO_ERROR;
    UDate localMillis = ucal_getNow();

    int32_t rawOffset, dstOffset;
    TimeZone* zone = TimeZone::createDefault();
    // If we failed to create the default time zone, we are in a bad state and don't
    // really have many options. Carry on using UTC millis as a fallback.
    if (zone != nullptr) {
        zone->getOffset(localMillis, false, rawOffset, dstOffset, ec);
        delete zone;
        localMillis += (rawOffset + dstOffset);
    }

    int year, month0, dom, dow, doy, mid;
    Grego::timeToFields(localMillis, year, month0, dom, dow, doy, mid, ec);
    if (U_FAILURE(ec)) return;
    int currentEncodedDate = encodeDate(year, month0 + 1 /* changes to 1-base */, dom);
    int eraIdx = numEras - 1;
    while (eraIdx > 0) {
        if (currentEncodedDate >= startDates[eraIdx]) {
            break;
        }
        eraIdx--;
    }
    // Note: current era could be before the first era.
    // In this case, this implementation returns the first era index (0).
    currentEra = eraIdx;
}

U_NAMESPACE_END
#endif /* #if !UCONFIG_NO_FORMATTING */


