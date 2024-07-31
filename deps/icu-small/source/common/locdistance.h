// © 2019 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

// locdistance.h
// created: 2019may08 Markus W. Scherer

#ifndef __LOCDISTANCE_H__
#define __LOCDISTANCE_H__

#include "unicode/utypes.h"
#include "unicode/bytestrie.h"
#include "unicode/localematcher.h"
#include "unicode/locid.h"
#include "unicode/uobject.h"
#include "lsr.h"

U_NAMESPACE_BEGIN

struct LocaleDistanceData;

/**
 * Offline-built data for LocaleMatcher.
 * Mostly but not only the data for mapping locales to their maximized forms.
 */
class LocaleDistance final : public UMemory {
public:
    static const LocaleDistance *getSingleton(UErrorCode &errorCode);

    static int32_t shiftDistance(int32_t distance) {
        return distance << DISTANCE_SHIFT;
    }

    static int32_t getShiftedDistance(int32_t indexAndDistance) {
        return indexAndDistance & DISTANCE_MASK;
    }

    static double getDistanceDouble(int32_t indexAndDistance) {
        double shiftedDistance = getShiftedDistance(indexAndDistance);
        return shiftedDistance / (1 << DISTANCE_SHIFT);
    }

    static int32_t getDistanceFloor(int32_t indexAndDistance) {
        return (indexAndDistance & DISTANCE_MASK) >> DISTANCE_SHIFT;
    }

    static int32_t getIndex(int32_t indexAndDistance) {
        // assert indexAndDistance >= 0;
        return indexAndDistance >> INDEX_SHIFT;
    }

    /**
     * Finds the supported LSR with the smallest distance from the desired one.
     * Equivalent LSR subtags must be normalized into a canonical form.
     *
     * <p>Returns the index of the lowest-distance supported LSR in the high bits
     * (negative if none has a distance below the threshold),
     * and its distance (0..ABOVE_THRESHOLD) in the low bits.
     */
    int32_t getBestIndexAndDistance(const LSR &desired,
                                    const LSR **supportedLSRs, int32_t supportedLSRsLength,
                                    int32_t shiftedThreshold,
                                    ULocMatchFavorSubtag favorSubtag,
                                    ULocMatchDirection direction) const;

    bool isParadigmLSR(const LSR &lsr) const;

    int32_t getDefaultScriptDistance() const {
        return defaultScriptDistance;
    }

    int32_t getDefaultDemotionPerDesiredLocale() const {
        return defaultDemotionPerDesiredLocale;
    }

private:
    // The distance is shifted left to gain some fraction bits.
    static constexpr int32_t DISTANCE_SHIFT = 3;
    static constexpr int32_t DISTANCE_FRACTION_MASK = 7;
    // 7 bits for 0..100
    static constexpr int32_t DISTANCE_INT_SHIFT = 7;
    static constexpr int32_t INDEX_SHIFT = DISTANCE_INT_SHIFT + DISTANCE_SHIFT;
    static constexpr int32_t DISTANCE_MASK = 0x3ff;
    // tic constexpr int32_t MAX_INDEX = 0x1fffff;  // avoids sign bit
    static constexpr int32_t INDEX_NEG_1 = 0xfffffc00;

    LocaleDistance(const LocaleDistanceData &data, const LikelySubtags &likely);
    LocaleDistance(const LocaleDistance &other) = delete;
    LocaleDistance &operator=(const LocaleDistance &other) = delete;

    static void initLocaleDistance(UErrorCode &errorCode);

    bool isMatch(const LSR &desired, const LSR &supported,
                 int32_t shiftedThreshold, ULocMatchFavorSubtag favorSubtag) const {
        const LSR *pSupp = &supported;
        return getBestIndexAndDistance(
            desired, &pSupp, 1,
            shiftedThreshold, favorSubtag, ULOCMATCH_DIRECTION_WITH_ONE_WAY) >= 0;
    }

    static int32_t getDesSuppScriptDistance(BytesTrie &iter, uint64_t startState,
                                            const char *desired, const char *supported);

    static int32_t getRegionPartitionsDistance(
        BytesTrie &iter, uint64_t startState,
        const char *desiredPartitions, const char *supportedPartitions,
        int32_t threshold);

    static int32_t getFallbackRegionDistance(BytesTrie &iter, uint64_t startState);

    static int32_t trieNext(BytesTrie &iter, const char *s, bool wantValue);

    const char *partitionsForRegion(const LSR &lsr) const {
        // ill-formed region -> one non-matching string
        int32_t pIndex = regionToPartitionsIndex[lsr.regionIndex];
        return partitionArrays[pIndex];
    }

    int32_t getDefaultRegionDistance() const {
        return defaultRegionDistance;
    }

    const LikelySubtags &likelySubtags;

    // The trie maps each dlang+slang+dscript+sscript+dregion+sregion
    // (encoded in ASCII with bit 7 set on the last character of each subtag) to a distance.
    // There is also a trie value for each subsequence of whole subtags.
    // One '*' is used for a (desired, supported) pair of "und", "Zzzz"/"", or "ZZ"/"".
    BytesTrie trie;

    /**
     * Maps each region to zero or more single-character partitions.
     */
    const uint8_t *regionToPartitionsIndex;
    const char **partitionArrays;

    /**
     * Used to get the paradigm region for a cluster, if there is one.
     */
    const LSR *paradigmLSRs;
    int32_t paradigmLSRsLength;

    int32_t defaultLanguageDistance;
    int32_t defaultScriptDistance;
    int32_t defaultRegionDistance;
    int32_t minRegionDistance;
    int32_t defaultDemotionPerDesiredLocale;
};

U_NAMESPACE_END

#endif  // __LOCDISTANCE_H__
