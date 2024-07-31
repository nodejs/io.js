// © 2019 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

// loclikelysubtags.cpp
// created: 2019may08 Markus W. Scherer

#include <utility>
#include "unicode/utypes.h"
#include "unicode/bytestrie.h"
#include "unicode/localpointer.h"
#include "unicode/locid.h"
#include "unicode/uobject.h"
#include "unicode/ures.h"
#include "unicode/uscript.h"
#include "charstr.h"
#include "cstring.h"
#include "loclikelysubtags.h"
#include "lsr.h"
#include "uassert.h"
#include "ucln_cmn.h"
#include "uhash.h"
#include "uinvchar.h"
#include "umutex.h"
#include "uniquecharstr.h"
#include "uresdata.h"
#include "uresimp.h"
#include "uvector.h"

U_NAMESPACE_BEGIN

namespace {

constexpr char PSEUDO_ACCENTS_PREFIX = '\'';  // -XA, -PSACCENT
constexpr char PSEUDO_BIDI_PREFIX = '+';  // -XB, -PSBIDI
constexpr char PSEUDO_CRACKED_PREFIX = ',';  // -XC, -PSCRACK

}  // namespace

LocaleDistanceData::LocaleDistanceData(LocaleDistanceData &&data) :
        distanceTrieBytes(data.distanceTrieBytes),
        regionToPartitions(data.regionToPartitions),
        partitions(data.partitions),
        paradigms(data.paradigms), paradigmsLength(data.paradigmsLength),
        distances(data.distances) {
    data.partitions = nullptr;
    data.paradigms = nullptr;
}

LocaleDistanceData::~LocaleDistanceData() {
    uprv_free(partitions);
    delete[] paradigms;
}

struct LikelySubtagsData {
    UResourceBundle *langInfoBundle = nullptr;
    UniqueCharStrings strings;
    CharStringMap languageAliases;
    CharStringMap regionAliases;
    const uint8_t *trieBytes = nullptr;
    LSR *lsrs = nullptr;
    int32_t lsrsLength = 0;

    LocaleDistanceData distanceData;

    LikelySubtagsData(UErrorCode &errorCode) : strings(errorCode) {}

    ~LikelySubtagsData() {
        ures_close(langInfoBundle);
        delete[] lsrs;
    }

    void load(UErrorCode &errorCode) {
        if (U_FAILURE(errorCode)) { return; }
        langInfoBundle = ures_openDirect(nullptr, "langInfo", &errorCode);
        if (U_FAILURE(errorCode)) { return; }
        StackUResourceBundle stackTempBundle;
        ResourceDataValue value;
        ures_getValueWithFallback(langInfoBundle, "likely", stackTempBundle.getAlias(),
                                  value, errorCode);
        ResourceTable likelyTable = value.getTable(errorCode);
        if (U_FAILURE(errorCode)) { return; }

        // Read all strings in the resource bundle and convert them to invariant char *.
        LocalMemory<int32_t> languageIndexes, regionIndexes, lsrSubtagIndexes;
        int32_t languagesLength = 0, regionsLength = 0, lsrSubtagsLength = 0;
        ResourceArray m49Array;
        if (likelyTable.findValue("m49", value)) {
            m49Array = value.getArray(errorCode);
        } else {
            errorCode = U_MISSING_RESOURCE_ERROR;
            return;
        }
        if (!readStrings(likelyTable, "languageAliases", value,
                         languageIndexes, languagesLength, errorCode) ||
                !readStrings(likelyTable, "regionAliases", value,
                             regionIndexes, regionsLength, errorCode) ||
                !readLSREncodedStrings(likelyTable, "lsrnum", value, m49Array,
                             lsrSubtagIndexes,lsrSubtagsLength, errorCode)) {
            return;
        }
        if ((languagesLength & 1) != 0 ||
                (regionsLength & 1) != 0 ||
                (lsrSubtagsLength % 3) != 0) {
            errorCode = U_INVALID_FORMAT_ERROR;
            return;
        }
        if (lsrSubtagsLength == 0) {
            errorCode = U_MISSING_RESOURCE_ERROR;
            return;
        }

        if (!likelyTable.findValue("trie", value)) {
            errorCode = U_MISSING_RESOURCE_ERROR;
            return;
        }
        int32_t length;
        trieBytes = value.getBinary(length, errorCode);
        if (U_FAILURE(errorCode)) { return; }

        // Also read distance/matcher data if available,
        // to open & keep only one resource bundle pointer
        // and to use one single UniqueCharStrings.
        UErrorCode matchErrorCode = U_ZERO_ERROR;
        ures_getValueWithFallback(langInfoBundle, "match", stackTempBundle.getAlias(),
                                  value, matchErrorCode);
        LocalMemory<int32_t> partitionIndexes, paradigmSubtagIndexes;
        int32_t partitionsLength = 0, paradigmSubtagsLength = 0;
        if (U_SUCCESS(matchErrorCode)) {
            ResourceTable matchTable = value.getTable(errorCode);
            if (U_FAILURE(errorCode)) { return; }

            if (matchTable.findValue("trie", value)) {
                distanceData.distanceTrieBytes = value.getBinary(length, errorCode);
                if (U_FAILURE(errorCode)) { return; }
            }

            if (matchTable.findValue("regionToPartitions", value)) {
                distanceData.regionToPartitions = value.getBinary(length, errorCode);
                if (U_FAILURE(errorCode)) { return; }
                if (length < LSR::REGION_INDEX_LIMIT) {
                    errorCode = U_INVALID_FORMAT_ERROR;
                    return;
                }
            }

            if (!readStrings(matchTable, "partitions", value,
                             partitionIndexes, partitionsLength, errorCode) ||
                    !readLSREncodedStrings(matchTable, "paradigmnum", value, m49Array,
                                 paradigmSubtagIndexes, paradigmSubtagsLength, errorCode)) {
                return;
            }
            if ((paradigmSubtagsLength % 3) != 0) {
                errorCode = U_INVALID_FORMAT_ERROR;
                return;
            }

            if (matchTable.findValue("distances", value)) {
                distanceData.distances = value.getIntVector(length, errorCode);
                if (U_FAILURE(errorCode)) { return; }
                if (length < 4) {  // LocaleDistance IX_LIMIT
                    errorCode = U_INVALID_FORMAT_ERROR;
                    return;
                }
            }
        } else if (matchErrorCode == U_MISSING_RESOURCE_ERROR) {
            // ok for likely subtags
        } else {  // error other than missing resource
            errorCode = matchErrorCode;
            return;
        }

        // Fetch & store invariant-character versions of strings
        // only after we have collected and de-duplicated all of them.
        strings.freeze();

        languageAliases = CharStringMap(languagesLength / 2, errorCode);
        for (int32_t i = 0; i < languagesLength; i += 2) {
            languageAliases.put(strings.get(languageIndexes[i]),
                                strings.get(languageIndexes[i + 1]), errorCode);
        }

        regionAliases = CharStringMap(regionsLength / 2, errorCode);
        for (int32_t i = 0; i < regionsLength; i += 2) {
            regionAliases.put(strings.get(regionIndexes[i]),
                              strings.get(regionIndexes[i + 1]), errorCode);
        }
        if (U_FAILURE(errorCode)) { return; }

        lsrsLength = lsrSubtagsLength / 3;
        lsrs = new LSR[lsrsLength];
        if (lsrs == nullptr) {
            errorCode = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
        for (int32_t i = 0, j = 0; i < lsrSubtagsLength; i += 3, ++j) {
            lsrs[j] = LSR(strings.get(lsrSubtagIndexes[i]),
                          strings.get(lsrSubtagIndexes[i + 1]),
                          strings.get(lsrSubtagIndexes[i + 2]),
                          LSR::IMPLICIT_LSR);
        }

        if (partitionsLength > 0) {
            distanceData.partitions = static_cast<const char **>(
                uprv_malloc(partitionsLength * sizeof(const char *)));
            if (distanceData.partitions == nullptr) {
                errorCode = U_MEMORY_ALLOCATION_ERROR;
                return;
            }
            for (int32_t i = 0; i < partitionsLength; ++i) {
                distanceData.partitions[i] = strings.get(partitionIndexes[i]);
            }
        }

        if (paradigmSubtagsLength > 0) {
            distanceData.paradigmsLength = paradigmSubtagsLength / 3;
            LSR *paradigms = new LSR[distanceData.paradigmsLength];
            if (paradigms == nullptr) {
                errorCode = U_MEMORY_ALLOCATION_ERROR;
                return;
            }
            for (int32_t i = 0, j = 0; i < paradigmSubtagsLength; i += 3, ++j) {
                paradigms[j] = LSR(strings.get(paradigmSubtagIndexes[i]),
                                   strings.get(paradigmSubtagIndexes[i + 1]),
                                   strings.get(paradigmSubtagIndexes[i + 2]),
                                   LSR::DONT_CARE_FLAGS);
            }
            distanceData.paradigms = paradigms;
        }
    }

private:
    bool readStrings(const ResourceTable &table, const char *key, ResourceValue &value,
                     LocalMemory<int32_t> &indexes, int32_t &length, UErrorCode &errorCode) {
        if (U_FAILURE(errorCode)) { return false; }
        if (table.findValue(key, value)) {
            ResourceArray stringArray = value.getArray(errorCode);
            if (U_FAILURE(errorCode)) { return false; }
            length = stringArray.getSize();
            if (length == 0) { return true; }
            int32_t *rawIndexes = indexes.allocateInsteadAndCopy(length);
            if (rawIndexes == nullptr) {
                errorCode = U_MEMORY_ALLOCATION_ERROR;
                return false;
            }
            for (int i = 0; i < length; ++i) {
                if (stringArray.getValue(i, value)) {  // returns true because i < length
                    int32_t strLength = 0;
                    rawIndexes[i] = strings.add(value.getString(strLength, errorCode), errorCode);
                    if (U_FAILURE(errorCode)) { return false; }
                }
            }
        }
        return true;
    }
    UnicodeString toLanguage(int encoded) {
        if (encoded == 0) {
            return UNICODE_STRING_SIMPLE("");
        }
        if (encoded == 1) {
            return UNICODE_STRING_SIMPLE("skip");
        }
        encoded &= 0x00ffffff;
        encoded %= 27*27*27;
        char lang[3];
        lang[0] = 'a' + ((encoded % 27) - 1);
        lang[1] = 'a' + (((encoded / 27 ) % 27) - 1);
        if (encoded / (27 * 27) == 0) {
            return UnicodeString(lang, 2, US_INV);
        }
        lang[2] = 'a' + ((encoded / (27 * 27)) - 1);
        return UnicodeString(lang, 3, US_INV);
    }
    UnicodeString toScript(int encoded) {
        if (encoded == 0) {
            return UNICODE_STRING_SIMPLE("");
        }
        if (encoded == 1) {
            return UNICODE_STRING_SIMPLE("script");
        }
        encoded = (encoded >> 24) & 0x000000ff;
        const char* script = uscript_getShortName(static_cast<UScriptCode>(encoded));
        if (script == nullptr) {
            return UNICODE_STRING_SIMPLE("");
        }
        U_ASSERT(uprv_strlen(script) == 4);
        return UnicodeString(script, 4, US_INV);
    }
    UnicodeString m49IndexToCode(const ResourceArray &m49Array, ResourceValue &value, int index, UErrorCode &errorCode) {
        if (U_FAILURE(errorCode)) {
            return UNICODE_STRING_SIMPLE("");
        }
        if (m49Array.getValue(index, value)) {
            return value.getUnicodeString(errorCode);
        }
        // "m49" does not include the index.
        errorCode = U_MISSING_RESOURCE_ERROR;
        return UNICODE_STRING_SIMPLE("");
    }

    UnicodeString toRegion(const ResourceArray& m49Array, ResourceValue &value, int encoded, UErrorCode &errorCode) {
        if (U_FAILURE(errorCode) || encoded == 0 || encoded == 1) {
            return UNICODE_STRING_SIMPLE("");
        }
        encoded &= 0x00ffffff;
        encoded /= 27 * 27 * 27;
        encoded %= 27 * 27;
        if (encoded < 27) {
            // Selected M49 code index, find the code from "m49" resource.
            return  m49IndexToCode(m49Array, value, encoded, errorCode);
        }
        char region[2];
        region[0] = 'A' + ((encoded % 27) - 1);
        region[1] = 'A' + (((encoded / 27) % 27) - 1);
        return UnicodeString(region, 2, US_INV);
    }

    bool readLSREncodedStrings(const ResourceTable &table, const char* key, ResourceValue &value, const ResourceArray& m49Array,
                     LocalMemory<int32_t> &indexes, int32_t &length, UErrorCode &errorCode) {
        if (U_FAILURE(errorCode)) { return false; }
        if (table.findValue(key, value)) {
            const int32_t* vectors = value.getIntVector(length, errorCode);
            if (U_FAILURE(errorCode)) { return false; }
            if (length == 0) { return true; }
            int32_t *rawIndexes = indexes.allocateInsteadAndCopy(length * 3);
            if (rawIndexes == nullptr) {
                errorCode = U_MEMORY_ALLOCATION_ERROR;
                return false;
            }
            for (int i = 0; i < length; ++i) {
                rawIndexes[i*3] = strings.addByValue(toLanguage(vectors[i]), errorCode);
                rawIndexes[i*3+1] = strings.addByValue(toScript(vectors[i]), errorCode);
                rawIndexes[i*3+2] = strings.addByValue(
                    toRegion(m49Array, value, vectors[i], errorCode), errorCode);
                if (U_FAILURE(errorCode)) { return false; }
            }
            length *= 3;
        }
        return true;
    }
};

namespace {

LikelySubtags *gLikelySubtags = nullptr;
UVector *gMacroregions = nullptr;
UInitOnce gInitOnce {};

UBool U_CALLCONV cleanup() {
    delete gLikelySubtags;
    gLikelySubtags = nullptr;
    delete gMacroregions;
    gMacroregions = nullptr;
    gInitOnce.reset();
    return true;
}

constexpr const char16_t* MACROREGION_HARDCODE[] = {
    u"001~3",
    u"005",
    u"009",
    u"011",
    u"013~5",
    u"017~9",
    u"021",
    u"029",
    u"030",
    u"034~5",
    u"039",
    u"053~4",
    u"057",
    u"061",
    u"142~3",
    u"145",
    u"150~1",
    u"154~5",
    u"202",
    u"419",
    u"EU",
    u"EZ",
    u"QO",
    u"UN",
};

constexpr char16_t RANGE_MARKER = 0x7E; /* '~' */
void processMacroregionRange(const UnicodeString& regionName, UVector* newMacroRegions, UErrorCode& status) {
    if (U_FAILURE(status)) { return; }
    int32_t rangeMarkerLocation = regionName.indexOf(RANGE_MARKER);
    char16_t buf[6];
    regionName.extract(buf,6,status);
    if ( rangeMarkerLocation > 0 ) {
        char16_t endRange = regionName.charAt(rangeMarkerLocation+1);
        buf[rangeMarkerLocation] = 0;
        while ( buf[rangeMarkerLocation-1] <= endRange && U_SUCCESS(status)) {
            LocalPointer<UnicodeString> newRegion(new UnicodeString(buf), status);
            newMacroRegions->adoptElement(newRegion.orphan(),status);
            buf[rangeMarkerLocation-1]++;
        }
    } else {
        LocalPointer<UnicodeString> newRegion(new UnicodeString(regionName), status);
        newMacroRegions->adoptElement(newRegion.orphan(),status);
    }
}

#if U_DEBUG
UVector* loadMacroregions(UErrorCode &status) {
    if (U_FAILURE(status)) { return nullptr; }
    LocalPointer<UVector> newMacroRegions(new UVector(uprv_deleteUObject, uhash_compareUnicodeString, status), status);

    LocalUResourceBundlePointer supplementalData(ures_openDirect(nullptr,"supplementalData",&status));
    LocalUResourceBundlePointer idValidity(ures_getByKey(supplementalData.getAlias(),"idValidity",nullptr,&status));
    LocalUResourceBundlePointer regionList(ures_getByKey(idValidity.getAlias(),"region",nullptr,&status));
    LocalUResourceBundlePointer regionMacro(ures_getByKey(regionList.getAlias(),"macroregion",nullptr,&status));

    if (U_FAILURE(status)) {
        return nullptr;
    }

    while (ures_hasNext(regionMacro.getAlias())) {
        UnicodeString regionName = ures_getNextUnicodeString(regionMacro.getAlias(),nullptr,&status);
        processMacroregionRange(regionName, newMacroRegions.getAlias(), status);
        if (U_FAILURE(status)) {
            return nullptr;
        }
    }

    return newMacroRegions.orphan();
}
#endif // U_DEBUG

UVector* getStaticMacroregions(UErrorCode &status) {
    if (U_FAILURE(status)) { return nullptr; }
    LocalPointer<UVector> newMacroRegions(new UVector(uprv_deleteUObject, uhash_compareUnicodeString, status), status);

    if (U_FAILURE(status)) {
        return nullptr;
    }

    for (const auto *region : MACROREGION_HARDCODE) {
        UnicodeString regionName(region);
        processMacroregionRange(regionName, newMacroRegions.getAlias(), status);
        if (U_FAILURE(status)) {
            return nullptr;
        }
    }

    return newMacroRegions.orphan();
}

}  // namespace

void U_CALLCONV LikelySubtags::initLikelySubtags(UErrorCode &errorCode) {
    // This function is invoked only via umtx_initOnce().
    U_ASSERT(gLikelySubtags == nullptr);
    LikelySubtagsData data(errorCode);
    data.load(errorCode);
    if (U_FAILURE(errorCode)) { return; }
    gLikelySubtags = new LikelySubtags(data);
    gMacroregions = getStaticMacroregions(errorCode);
#if U_DEBUG
    auto macroregionsFromData = loadMacroregions(errorCode);
    U_ASSERT((*gMacroregions) == (*macroregionsFromData));
    delete macroregionsFromData;
#endif
    if (U_FAILURE(errorCode) || gLikelySubtags == nullptr || gMacroregions == nullptr) {
        delete gLikelySubtags;
        delete gMacroregions;
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        return;
    }

    ucln_common_registerCleanup(UCLN_COMMON_LIKELY_SUBTAGS, cleanup);
}

const LikelySubtags *LikelySubtags::getSingleton(UErrorCode &errorCode) {
    if (U_FAILURE(errorCode)) { return nullptr; }
    umtx_initOnce(gInitOnce, &LikelySubtags::initLikelySubtags, errorCode);
    return gLikelySubtags;
}

LikelySubtags::LikelySubtags(LikelySubtagsData &data) :
        langInfoBundle(data.langInfoBundle),
        strings(data.strings.orphanCharStrings()),
        languageAliases(std::move(data.languageAliases)),
        regionAliases(std::move(data.regionAliases)),
        trie(data.trieBytes),
        lsrs(data.lsrs),
#if U_DEBUG
        lsrsLength(data.lsrsLength),
#endif // U_DEBUG
        distanceData(std::move(data.distanceData)) {
    data.langInfoBundle = nullptr;
    data.lsrs = nullptr;

    // Cache the result of looking up language="und" encoded as "*", and "und-Zzzz" ("**").
    UStringTrieResult result = trie.next(u'*');
    U_ASSERT(USTRINGTRIE_HAS_NEXT(result));
    trieUndState = trie.getState64();
    result = trie.next(u'*');
    U_ASSERT(USTRINGTRIE_HAS_NEXT(result));
    trieUndZzzzState = trie.getState64();
    result = trie.next(u'*');
    U_ASSERT(USTRINGTRIE_HAS_VALUE(result));
    defaultLsrIndex = trie.getValue();
    trie.reset();

    for (char16_t c = u'a'; c <= u'z'; ++c) {
        result = trie.next(c);
        if (result == USTRINGTRIE_NO_VALUE) {
            trieFirstLetterStates[c - u'a'] = trie.getState64();
        }
        trie.reset();
    }
}

LikelySubtags::~LikelySubtags() {
    ures_close(langInfoBundle);
    delete strings;
    delete[] lsrs;
}

LSR LikelySubtags::makeMaximizedLsrFrom(const Locale &locale,
                                         bool returnInputIfUnmatch,
                                         UErrorCode &errorCode) const {
    if (U_FAILURE(errorCode)) { return {}; }
    if (locale.isBogus()) {
        errorCode = U_ILLEGAL_ARGUMENT_ERROR;
        return {};
    }
    const char *name = locale.getName();
    if (uprv_isAtSign(name[0]) && name[1] == 'x' && name[2] == '=') {  // name.startsWith("@x=")
        // Private use language tag x-subtag-subtag... which CLDR changes to
        // und-x-subtag-subtag...
        return LSR(name, "", "", LSR::EXPLICIT_LSR);
    }
    LSR max = makeMaximizedLsr(locale.getLanguage(), locale.getScript(), locale.getCountry(),
                            locale.getVariant(), returnInputIfUnmatch, errorCode);

    if (uprv_strlen(max.language) == 0 &&
        uprv_strlen(max.script) == 0 &&
        uprv_strlen(max.region) == 0) {
        // No match. ICU API mandate us to
        // If the provided ULocale instance is already in the maximal form, or
        // there is no data available available for maximization, it will be
        // returned.
        return LSR(locale.getLanguage(), locale.getScript(), locale.getCountry(), LSR::EXPLICIT_LSR, errorCode);
    }
    return max;
}

namespace {

const char *getCanonical(const CharStringMap &aliases, const char *alias) {
    const char *canonical = aliases.get(alias);
    return canonical == nullptr ? alias : canonical;
}

}  // namespace

LSR LikelySubtags::makeMaximizedLsr(const char *language, const char *script, const char *region,
                                     const char *variant,
                                     bool returnInputIfUnmatch,
                                     UErrorCode &errorCode) const {
    if (U_FAILURE(errorCode)) { return {}; }
    // Handle pseudolocales like en-XA, ar-XB, fr-PSCRACK.
    // They should match only themselves,
    // not other locales with what looks like the same language and script subtags.
    char c1;
    if (region[0] == 'X' && (c1 = region[1]) != 0 && region[2] == 0) {
        switch (c1) {
        case 'A':
            if (returnInputIfUnmatch) {
                return LSR(language, script, region, LSR::EXPLICIT_LSR);
            }
            return LSR(PSEUDO_ACCENTS_PREFIX, language, script, region,
                       LSR::EXPLICIT_LSR, errorCode);
        case 'B':
            if (returnInputIfUnmatch) {
                return LSR(language, script, region, LSR::EXPLICIT_LSR);
            }
            return LSR(PSEUDO_BIDI_PREFIX, language, script, region,
                       LSR::EXPLICIT_LSR, errorCode);
        case 'C':
            if (returnInputIfUnmatch) {
                return LSR(language, script, region, LSR::EXPLICIT_LSR);
            }
            return LSR(PSEUDO_CRACKED_PREFIX, language, script, region,
                       LSR::EXPLICIT_LSR, errorCode);
        default:  // normal locale
            break;
        }
    }

    if (variant[0] == 'P' && variant[1] == 'S') {
        int32_t lsrFlags = *region == 0 ?
            LSR::EXPLICIT_LANGUAGE | LSR::EXPLICIT_SCRIPT : LSR::EXPLICIT_LSR;
        if (uprv_strcmp(variant, "PSACCENT") == 0) {
            return LSR(PSEUDO_ACCENTS_PREFIX, language, script,
                       *region == 0 ? "XA" : region, lsrFlags, errorCode);
        } else if (uprv_strcmp(variant, "PSBIDI") == 0) {
            return LSR(PSEUDO_BIDI_PREFIX, language, script,
                       *region == 0 ? "XB" : region, lsrFlags, errorCode);
        } else if (uprv_strcmp(variant, "PSCRACK") == 0) {
            return LSR(PSEUDO_CRACKED_PREFIX, language, script,
                       *region == 0 ? "XC" : region, lsrFlags, errorCode);
        }
        // else normal locale
    }

    language = getCanonical(languageAliases, language);
    // (We have no script mappings.)
    region = getCanonical(regionAliases, region);
    return maximize(language, script, region, returnInputIfUnmatch, errorCode);
}

LSR LikelySubtags::maximize(const char *language, const char *script, const char *region,
                             bool returnInputIfUnmatch,
                             UErrorCode &errorCode) const {
    if (U_FAILURE(errorCode)) { return {}; }
    return maximize({language, (int32_t)uprv_strlen(language)},
                    {script, (int32_t)uprv_strlen(script)},
                    {region, (int32_t)uprv_strlen(region)},
                    returnInputIfUnmatch,
                    errorCode);
}

bool LikelySubtags::isMacroregion(StringPiece& region, UErrorCode& errorCode) const {
    if (U_FAILURE(errorCode)) { return false; }
    // In Java, we use Region class. In C++, since Region is under i18n,
    // we read the same data used by Region into gMacroregions avoid dependency
    // from common to i18n/region.cpp
    umtx_initOnce(gInitOnce, &LikelySubtags::initLikelySubtags, errorCode);
    if (U_FAILURE(errorCode)) { return false; }
    UnicodeString str(UnicodeString::fromUTF8(region));
    return gMacroregions->contains((void *)&str);
}

LSR LikelySubtags::maximize(StringPiece language, StringPiece script, StringPiece region,
                             bool returnInputIfUnmatch,
                             UErrorCode &errorCode) const {
    if (U_FAILURE(errorCode)) { return {}; }
    if (language.compare("und") == 0) {
        language = "";
    }
    if (script.compare("Zzzz") == 0) {
        script = "";
    }
    if (region.compare("ZZ") == 0) {
        region = "";
    }
    if (!script.empty() && !region.empty() && !language.empty()) {
        return LSR(language, script, region, LSR::EXPLICIT_LSR, errorCode);  // already maximized
    }
    bool retainLanguage = false;
    bool retainScript = false;
    bool retainRegion = false;

    BytesTrie iter(trie);
    uint64_t state;
    int32_t value;
    // Small optimization: Array lookup for first language letter.
    int32_t c0;
    if (0 <= (c0 = uprv_lowerOrdinal(language.data()[0])) && c0 <= 25 &&
            language.length() >= 2 &&
            (state = trieFirstLetterStates[c0]) != 0) {
        value = trieNext(iter.resetToState64(state), language, 1);
    } else {
        value = trieNext(iter, language, 0);
    }
    bool matchLanguage = (value >= 0);
    bool matchScript = false;
    if (value >= 0) {
        retainLanguage = !language.empty();
        state = iter.getState64();
    } else {
        retainLanguage = true;
        iter.resetToState64(trieUndState);  // "und" ("*")
        state = 0;
    }

    if (value >= 0 && !script.empty()) {
        matchScript = true;
    }
    if (value > 0) {
        // Intermediate or final value from just language.
        if (value == SKIP_SCRIPT) {
            value = 0;
        }
        retainScript = !script.empty();
    } else {
        value = trieNext(iter, script, 0);
        if (value >= 0) {
            retainScript = !script.empty();
            state = iter.getState64();
        } else {
            retainScript = true;
            if (state == 0) {
                iter.resetToState64(trieUndZzzzState);  // "und-Zzzz" ("**")
            } else {
                iter.resetToState64(state);
                value = trieNext(iter, "", 0);
                U_ASSERT(value >= 0);
                state = iter.getState64();
            }
        }
    }

    bool matchRegion = false;
    if (value > 0) {
        // Final value from just language or language+script.
        retainRegion = !region.empty();
    } else {
        value = trieNext(iter, region, 0);
        if (value >= 0) {
            if (!region.empty() && !isMacroregion(region, errorCode)) {
                retainRegion = true;
                matchRegion = true;
            }
        } else {
            retainRegion = true;
            if (state == 0) {
                value = defaultLsrIndex;
            } else {
                iter.resetToState64(state);
                value = trieNext(iter, "", 0);
                U_ASSERT(value > 0);
            }
        }
    }
    U_ASSERT(value < lsrsLength);
    const LSR &matched = lsrs[value];

    if (returnInputIfUnmatch &&
        (!(matchLanguage || matchScript || (matchRegion && language.empty())))) {
      return LSR("", "", "", LSR::EXPLICIT_LSR, errorCode);  // no matching.
    }
    if (language.empty()) {
        language = StringPiece("und");
    }

    if (!(retainLanguage || retainScript || retainRegion)) {
        // Quickly return a copy of the lookup-result LSR
        // without new allocation of the subtags.
        return LSR(matched.language, matched.script, matched.region, matched.flags);
    }
    if (!retainLanguage) {
        language = matched.language;
    }
    if (!retainScript) {
        script = matched.script;
    }
    if (!retainRegion) {
        region = matched.region;
    }
    int32_t retainMask = (retainLanguage ? 4 : 0) + (retainScript ? 2 : 0) + (retainRegion ? 1 : 0);
    // retainOldMask flags = LSR explicit-subtag flags
    return LSR(language, script, region, retainMask, errorCode);
}

int32_t LikelySubtags::compareLikely(const LSR &lsr, const LSR &other, int32_t likelyInfo) const {
    // If likelyInfo >= 0:
    // likelyInfo bit 1 is set if the previous comparison with lsr
    // was for equal language and script.
    // Otherwise the scripts differed.
    if (uprv_strcmp(lsr.language, other.language) != 0) {
        return 0xfffffffc;  // negative, lsr not better than other
    }
    if (uprv_strcmp(lsr.script, other.script) != 0) {
        int32_t index;
        if (likelyInfo >= 0 && (likelyInfo & 2) == 0) {
            index = likelyInfo >> 2;
        } else {
            index = getLikelyIndex(lsr.language, "");
            likelyInfo = index << 2;
        }
        const LSR &likely = lsrs[index];
        if (uprv_strcmp(lsr.script, likely.script) == 0) {
            return likelyInfo | 1;
        } else {
            return likelyInfo & ~1;
        }
    }
    if (uprv_strcmp(lsr.region, other.region) != 0) {
        int32_t index;
        if (likelyInfo >= 0 && (likelyInfo & 2) != 0) {
            index = likelyInfo >> 2;
        } else {
            index = getLikelyIndex(lsr.language, lsr.region);
            likelyInfo = (index << 2) | 2;
        }
        const LSR &likely = lsrs[index];
        if (uprv_strcmp(lsr.region, likely.region) == 0) {
            return likelyInfo | 1;
        } else {
            return likelyInfo & ~1;
        }
    }
    return likelyInfo & ~1;  // lsr not better than other
}

// Subset of maximize().
int32_t LikelySubtags::getLikelyIndex(const char *language, const char *script) const {
    if (uprv_strcmp(language, "und") == 0) {
        language = "";
    }
    if (uprv_strcmp(script, "Zzzz") == 0) {
        script = "";
    }

    BytesTrie iter(trie);
    uint64_t state;
    int32_t value;
    // Small optimization: Array lookup for first language letter.
    int32_t c0;
    if (0 <= (c0 = uprv_lowerOrdinal(language[0])) && c0 <= 25 &&
            language[1] != 0 &&  // language.length() >= 2
            (state = trieFirstLetterStates[c0]) != 0) {
        value = trieNext(iter.resetToState64(state), language, 1);
    } else {
        value = trieNext(iter, language, 0);
    }
    if (value >= 0) {
        state = iter.getState64();
    } else {
        iter.resetToState64(trieUndState);  // "und" ("*")
        state = 0;
    }

    if (value > 0) {
        // Intermediate or final value from just language.
        if (value == SKIP_SCRIPT) {
            value = 0;
        }
    } else {
        value = trieNext(iter, script, 0);
        if (value >= 0) {
            state = iter.getState64();
        } else {
            if (state == 0) {
                iter.resetToState64(trieUndZzzzState);  // "und-Zzzz" ("**")
            } else {
                iter.resetToState64(state);
                value = trieNext(iter, "", 0);
                U_ASSERT(value >= 0);
                state = iter.getState64();
            }
        }
    }

    if (value > 0) {
        // Final value from just language or language+script.
    } else {
        value = trieNext(iter, "", 0);
        U_ASSERT(value > 0);
    }
    U_ASSERT(value < lsrsLength);
    return value;
}

int32_t LikelySubtags::trieNext(BytesTrie &iter, const char *s, int32_t i) {
    UStringTrieResult result;
    uint8_t c;
    if ((c = s[i]) == 0) {
        result = iter.next(u'*');
    } else {
        for (;;) {
            c = uprv_invCharToAscii(c);
            // EBCDIC: If s[i] is not an invariant character,
            // then c is now 0 and will simply not match anything, which is harmless.
            uint8_t next = s[++i];
            if (next != 0) {
                if (!USTRINGTRIE_HAS_NEXT(iter.next(c))) {
                    return -1;
                }
            } else {
                // last character of this subtag
                result = iter.next(c | 0x80);
                break;
            }
            c = next;
        }
    }
    switch (result) {
    case USTRINGTRIE_NO_MATCH: return -1;
    case USTRINGTRIE_NO_VALUE: return 0;
    case USTRINGTRIE_INTERMEDIATE_VALUE:
        U_ASSERT(iter.getValue() == SKIP_SCRIPT);
        return SKIP_SCRIPT;
    case USTRINGTRIE_FINAL_VALUE: return iter.getValue();
    default: return -1;
    }
}
int32_t LikelySubtags::trieNext(BytesTrie &iter, StringPiece s, int32_t i) {
    UStringTrieResult result;
    uint8_t c;
    if (s.length() == i) {
        result = iter.next(u'*');
    } else {
        c = s.data()[i];
        for (;;) {
            c = uprv_invCharToAscii(c);
            // EBCDIC: If s[i] is not an invariant character,
            // then c is now 0 and will simply not match anything, which is harmless.
            if (i+1 != s.length()) {
                if (!USTRINGTRIE_HAS_NEXT(iter.next(c))) {
                    return -1;
                }
                c = s.data()[++i];
            } else {
                // last character of this subtag
                result = iter.next(c | 0x80);
                break;
            }
        }
    }
    switch (result) {
    case USTRINGTRIE_NO_MATCH: return -1;
    case USTRINGTRIE_NO_VALUE: return 0;
    case USTRINGTRIE_INTERMEDIATE_VALUE:
        U_ASSERT(iter.getValue() == SKIP_SCRIPT);
        return SKIP_SCRIPT;
    case USTRINGTRIE_FINAL_VALUE: return iter.getValue();
    default: return -1;
    }
}

LSR LikelySubtags::minimizeSubtags(StringPiece language, StringPiece script,
                                    StringPiece region,
                                    bool favorScript,
                                    UErrorCode &errorCode) const {
    if (U_FAILURE(errorCode)) { return {}; }
    LSR max = maximize(language, script, region, true, errorCode);
    if (U_FAILURE(errorCode)) { return {}; }
    // If no match, return it.
    if (uprv_strlen(max.language) == 0 &&
        uprv_strlen(max.script) == 0 &&
        uprv_strlen(max.region) == 0) {
        // No match. ICU API mandate us to
        // "If this Locale is already in the minimal form, or not valid, or
        // there is no data available for minimization, the Locale will be
        // unchanged."
        return LSR(language, script, region, LSR::EXPLICIT_LSR, errorCode);
    }
    // try language
    LSR test = maximize(max.language, "", "", true, errorCode);
    if (U_FAILURE(errorCode)) { return {}; }
    if (test.isEquivalentTo(max)) {
        return LSR(max.language, "", "", LSR::DONT_CARE_FLAGS, errorCode);
    }

    if (!favorScript) {
        // favor Region
        // try language and region
        test = maximize(max.language, "", max.region, true, errorCode);
        if (U_FAILURE(errorCode)) { return {}; }
        if (test.isEquivalentTo(max)) {
            return LSR(max.language, "", max.region, LSR::DONT_CARE_FLAGS, errorCode);
        }
    }
    // try language and script
    test = maximize(max.language, max.script, "", true, errorCode);
    if (U_FAILURE(errorCode)) { return {}; }
    if (test.isEquivalentTo(max)) {
        return LSR(max.language, max.script, "", LSR::DONT_CARE_FLAGS, errorCode);
    }
    if (favorScript) {
        // try language and region
        test = maximize(max.language, "", max.region, true, errorCode);
        if (U_FAILURE(errorCode)) { return {}; }
        if (test.isEquivalentTo(max)) {
            return LSR(max.language, "", max.region, LSR::DONT_CARE_FLAGS, errorCode);
        }
    }
    return LSR(max.language, max.script, max.region, LSR::DONT_CARE_FLAGS, errorCode);
}

U_NAMESPACE_END
