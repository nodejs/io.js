// © 2019 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

// loclikelysubtags.h
// created: 2019may08 Markus W. Scherer

#ifndef __LOCLIKELYSUBTAGS_H__
#define __LOCLIKELYSUBTAGS_H__

#include <utility>
#include "unicode/utypes.h"
#include "unicode/bytestrie.h"
#include "unicode/locid.h"
#include "unicode/stringpiece.h"
#include "unicode/uobject.h"
#include "unicode/ures.h"
#include "charstrmap.h"
#include "lsr.h"

U_NAMESPACE_BEGIN

struct LikelySubtagsData;

struct LocaleDistanceData {
    LocaleDistanceData() = default;
    LocaleDistanceData(LocaleDistanceData &&data);
    ~LocaleDistanceData();

    const uint8_t *distanceTrieBytes = nullptr;
    const uint8_t *regionToPartitions = nullptr;
    const char **partitions = nullptr;
    const LSR *paradigms = nullptr;
    int32_t paradigmsLength = 0;
    const int32_t *distances = nullptr;

private:
    LocaleDistanceData &operator=(const LocaleDistanceData &) = delete;
};

class LikelySubtags final : public UMemory {
public:
    ~LikelySubtags();

    static constexpr int32_t SKIP_SCRIPT = 1;

    // VisibleForTesting
    static const LikelySubtags *getSingleton(UErrorCode &errorCode);

    // VisibleForTesting
    LSR makeMaximizedLsrFrom(const Locale &locale,
                             bool returnInputIfUnmatch,
                             UErrorCode &errorCode) const;

    /**
     * Tests whether lsr is "more likely" than other.
     * For example, fr-Latn-FR is more likely than fr-Latn-CH because
     * FR is the default region for fr-Latn.
     *
     * The likelyInfo caches lookup information between calls.
     * The return value is an updated likelyInfo value,
     * with bit 0 set if lsr is "more likely".
     * The initial value of likelyInfo must be negative.
     */
    int32_t compareLikely(const LSR &lsr, const LSR &other, int32_t likelyInfo) const;

    LSR minimizeSubtags(StringPiece language, StringPiece script, StringPiece region,
                        bool favorScript,
                        UErrorCode &errorCode) const;

    // visible for LocaleDistance
    const LocaleDistanceData &getDistanceData() const { return distanceData; }

private:
    LikelySubtags(LikelySubtagsData &data);
    LikelySubtags(const LikelySubtags &other) = delete;
    LikelySubtags &operator=(const LikelySubtags &other) = delete;

    static void initLikelySubtags(UErrorCode &errorCode);

    LSR makeMaximizedLsr(const char *language, const char *script, const char *region,
                         const char *variant,
                         bool returnInputIfUnmatch,
                         UErrorCode &errorCode) const;

    /**
     * Raw access to addLikelySubtags. Input must be in canonical format, eg "en", not "eng" or "EN".
     */
    LSR maximize(const char *language, const char *script, const char *region,
                 bool returnInputIfUnmatch,
                 UErrorCode &errorCode) const;
    LSR maximize(StringPiece language, StringPiece script, StringPiece region,
                 bool returnInputIfUnmatch,
                 UErrorCode &errorCode) const;

    int32_t getLikelyIndex(const char *language, const char *script) const;
    bool isMacroregion(StringPiece& region, UErrorCode &errorCode) const;

    static int32_t trieNext(BytesTrie &iter, const char *s, int32_t i);
    static int32_t trieNext(BytesTrie &iter, StringPiece s, int32_t i);

    UResourceBundle *langInfoBundle;
    // We could store the strings by value, except that if there were few enough strings,
    // moving the contents could copy it to a different array,
    // invalidating the pointers stored in the maps.
    CharString *strings;
    CharStringMap languageAliases;
    CharStringMap regionAliases;

    // The trie maps each lang+script+region (encoded in ASCII) to an index into lsrs.
    // There is also a trie value for each intermediate lang and lang+script.
    // '*' is used instead of "und", "Zzzz"/"" and "ZZ"/"".
    BytesTrie trie;
    uint64_t trieUndState;
    uint64_t trieUndZzzzState;
    int32_t defaultLsrIndex;
    uint64_t trieFirstLetterStates[26];
    const LSR *lsrs;
#if U_DEBUG
    int32_t lsrsLength;
#endif

    // distance/matcher data: see comment in LikelySubtagsData::load()
    LocaleDistanceData distanceData;
};

U_NAMESPACE_END

#endif  // __LOCLIKELYSUBTAGS_H__
