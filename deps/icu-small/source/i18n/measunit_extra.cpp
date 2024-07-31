// © 2020 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

// Extra functions for MeasureUnit not needed for all clients.
// Separate .o file so that it can be removed for modularity.

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

// Allow implicit conversion from char16_t* to UnicodeString for this file:
// Helpful in toString methods and elsewhere.
#define UNISTR_FROM_STRING_EXPLICIT

#include "charstr.h"
#include "cmemory.h"
#include "cstring.h"
#include "measunit_impl.h"
#include "resource.h"
#include "uarrsort.h"
#include "uassert.h"
#include "ucln_in.h"
#include "umutex.h"
#include "unicode/bytestrie.h"
#include "unicode/bytestriebuilder.h"
#include "unicode/localpointer.h"
#include "unicode/stringpiece.h"
#include "unicode/stringtriebuilder.h"
#include "unicode/ures.h"
#include "unicode/ustringtrie.h"
#include "uresimp.h"
#include "util.h"
#include <cstdlib>

U_NAMESPACE_BEGIN


namespace {

// TODO: Propose a new error code for this?
constexpr UErrorCode kUnitIdentifierSyntaxError = U_ILLEGAL_ARGUMENT_ERROR;

// Trie value offset for SI or binary prefixes. This is big enough to ensure we only
// insert positive integers into the trie.
constexpr int32_t kPrefixOffset = 64;
static_assert(kPrefixOffset + UMEASURE_PREFIX_INTERNAL_MIN_BIN > 0,
              "kPrefixOffset is too small for minimum UMeasurePrefix value");
static_assert(kPrefixOffset + UMEASURE_PREFIX_INTERNAL_MIN_SI > 0,
              "kPrefixOffset is too small for minimum UMeasurePrefix value");

// Trie value offset for compound parts, e.g. "-per-", "-", "-and-".
constexpr int32_t kCompoundPartOffset = 128;
static_assert(kCompoundPartOffset > kPrefixOffset + UMEASURE_PREFIX_INTERNAL_MAX_BIN,
              "Ambiguous token values: prefix tokens are overlapping with CompoundPart tokens");
static_assert(kCompoundPartOffset > kPrefixOffset + UMEASURE_PREFIX_INTERNAL_MAX_SI,
              "Ambiguous token values: prefix tokens are overlapping with CompoundPart tokens");

enum CompoundPart {
    // Represents "-per-"
    COMPOUND_PART_PER = kCompoundPartOffset,
    // Represents "-"
    COMPOUND_PART_TIMES,
    // Represents "-and-"
    COMPOUND_PART_AND,
};

// Trie value offset for "per-".
constexpr int32_t kInitialCompoundPartOffset = 192;

enum InitialCompoundPart {
    // Represents "per-", the only compound part that can appear at the start of
    // an identifier.
    INITIAL_COMPOUND_PART_PER = kInitialCompoundPartOffset,
};

// Trie value offset for powers like "square-", "cubic-", "pow2-" etc.
constexpr int32_t kPowerPartOffset = 256;

enum PowerPart {
    POWER_PART_P2 = kPowerPartOffset + 2,
    POWER_PART_P3,
    POWER_PART_P4,
    POWER_PART_P5,
    POWER_PART_P6,
    POWER_PART_P7,
    POWER_PART_P8,
    POWER_PART_P9,
    POWER_PART_P10,
    POWER_PART_P11,
    POWER_PART_P12,
    POWER_PART_P13,
    POWER_PART_P14,
    POWER_PART_P15,
};

// Trie value offset for simple units, e.g. "gram", "nautical-mile",
// "fluid-ounce-imperial".
constexpr int32_t kSimpleUnitOffset = 512;

const struct UnitPrefixStrings {
    const char* const string;
    UMeasurePrefix value;
} gUnitPrefixStrings[] = {
    // SI prefixes
    { "quetta", UMEASURE_PREFIX_QUETTA },
    { "ronna", UMEASURE_PREFIX_RONNA },
    { "yotta", UMEASURE_PREFIX_YOTTA },
    { "zetta", UMEASURE_PREFIX_ZETTA },
    { "exa", UMEASURE_PREFIX_EXA },
    { "peta", UMEASURE_PREFIX_PETA },
    { "tera", UMEASURE_PREFIX_TERA },
    { "giga", UMEASURE_PREFIX_GIGA },
    { "mega", UMEASURE_PREFIX_MEGA },
    { "kilo", UMEASURE_PREFIX_KILO },
    { "hecto", UMEASURE_PREFIX_HECTO },
    { "deka", UMEASURE_PREFIX_DEKA },
    { "deci", UMEASURE_PREFIX_DECI },
    { "centi", UMEASURE_PREFIX_CENTI },
    { "milli", UMEASURE_PREFIX_MILLI },
    { "micro", UMEASURE_PREFIX_MICRO },
    { "nano", UMEASURE_PREFIX_NANO },
    { "pico", UMEASURE_PREFIX_PICO },
    { "femto", UMEASURE_PREFIX_FEMTO },
    { "atto", UMEASURE_PREFIX_ATTO },
    { "zepto", UMEASURE_PREFIX_ZEPTO },
    { "yocto", UMEASURE_PREFIX_YOCTO },
    { "ronto", UMEASURE_PREFIX_RONTO },
    { "quecto", UMEASURE_PREFIX_QUECTO },
    // Binary prefixes
    { "yobi", UMEASURE_PREFIX_YOBI },
    { "zebi", UMEASURE_PREFIX_ZEBI },
    { "exbi", UMEASURE_PREFIX_EXBI },
    { "pebi", UMEASURE_PREFIX_PEBI },
    { "tebi", UMEASURE_PREFIX_TEBI },
    { "gibi", UMEASURE_PREFIX_GIBI },
    { "mebi", UMEASURE_PREFIX_MEBI },
    { "kibi", UMEASURE_PREFIX_KIBI },
};

/**
 * A ResourceSink that collects simple unit identifiers from the keys of the
 * convertUnits table into an array, and adds these values to a TrieBuilder,
 * with associated values being their index into this array plus a specified
 * offset.
 *
 * Example code:
 *
 *     UErrorCode status = U_ZERO_ERROR;
 *     BytesTrieBuilder b(status);
 *     int32_t ARR_SIZE = 200;
 *     const char *unitIdentifiers[ARR_SIZE];
 *     int32_t *unitCategories[ARR_SIZE];
 *     SimpleUnitIdentifiersSink identifierSink(gSerializedUnitCategoriesTrie, unitIdentifiers,
 *                                              unitCategories, ARR_SIZE, b, kTrieValueOffset);
 *     LocalUResourceBundlePointer unitsBundle(ures_openDirect(nullptr, "units", &status));
 *     ures_getAllItemsWithFallback(unitsBundle.getAlias(), "convertUnits", identifierSink, status);
 */
class SimpleUnitIdentifiersSink : public icu::ResourceSink {
  public:
    /**
     * Constructor.
     * @param quantitiesTrieData The data for constructing a quantitiesTrie,
     *     which maps from a simple unit identifier to an index into the
     *     gCategories array.
     * @param out Array of char* to which pointers to the simple unit
     *     identifiers will be saved. (Does not take ownership.)
     * @param outCategories Array of int32_t to which category indexes will be
     *     saved: this corresponds to simple unit IDs saved to `out`, mapping
     *     from the ID to the value produced by the quantitiesTrie (which is an
     *     index into the gCategories array).
     * @param outSize The size of `out` and `outCategories`.
     * @param trieBuilder The trie builder to which the simple unit identifier
     *     should be added. The trie builder must outlive this resource sink.
     * @param trieValueOffset This is added to the index of the identifier in
     *     the `out` array, before adding to `trieBuilder` as the value
     *     associated with the identifier.
     */
    explicit SimpleUnitIdentifiersSink(StringPiece quantitiesTrieData, const char **out,
                                       int32_t *outCategories, int32_t outSize,
                                       BytesTrieBuilder &trieBuilder, int32_t trieValueOffset)
        : outArray(out), outCategories(outCategories), outSize(outSize), trieBuilder(trieBuilder),
          trieValueOffset(trieValueOffset), quantitiesTrieData(quantitiesTrieData), outIndex(0) {}

    /**
     * Adds the table keys found in value to the output vector.
     * @param key The key of the resource passed to `value`: the second
     *     parameter of the ures_getAllItemsWithFallback() call.
     * @param value Should be a ResourceTable value, if
     *     ures_getAllItemsWithFallback() was called correctly for this sink.
     * @param noFallback Ignored.
     * @param status The standard ICU error code output parameter.
     */
    void put(const char * /*key*/, ResourceValue &value, UBool /*noFallback*/, UErrorCode &status) override {
        ResourceTable table = value.getTable(status);
        if (U_FAILURE(status)) return;

        if (outIndex + table.getSize() > outSize) {
            status = U_INDEX_OUTOFBOUNDS_ERROR;
            return;
        }

        BytesTrie quantitiesTrie(quantitiesTrieData.data());

        // Collect keys from the table resource.
        const char *simpleUnitID;
        for (int32_t i = 0; table.getKeyAndValue(i, simpleUnitID, value); ++i) {
            U_ASSERT(i < table.getSize());
            U_ASSERT(outIndex < outSize);
            if (uprv_strcmp(simpleUnitID, "kilogram") == 0) {
                // For parsing, we use "gram", the prefixless metric mass unit. We
                // thus ignore the SI Base Unit of Mass: it exists due to being the
                // mass conversion target unit, but not needed for MeasureUnit
                // parsing.
                continue;
            }
            outArray[outIndex] = simpleUnitID;
            trieBuilder.add(simpleUnitID, trieValueOffset + outIndex, status);

            // Find the base target unit for this simple unit
            ResourceTable table = value.getTable(status);
            if (U_FAILURE(status)) { return; }
            if (!table.findValue("target", value)) {
                status = U_INVALID_FORMAT_ERROR;
                break;
            }
            int32_t len;
            const char16_t* uTarget = value.getString(len, status);
            CharString target;
            target.appendInvariantChars(uTarget, len, status);
            if (U_FAILURE(status)) { return; }
            quantitiesTrie.reset();
            UStringTrieResult result = quantitiesTrie.next(target.data(), target.length());
            if (!USTRINGTRIE_HAS_VALUE(result)) {
                status = U_INVALID_FORMAT_ERROR;
                break;
            }
            outCategories[outIndex] = quantitiesTrie.getValue();

            outIndex++;
        }
    }

  private:
    const char **outArray;
    int32_t *outCategories;
    int32_t outSize;
    BytesTrieBuilder &trieBuilder;
    int32_t trieValueOffset;

    StringPiece quantitiesTrieData;

    int32_t outIndex;
};

/**
 * A ResourceSink that collects information from `unitQuantities` in the `units`
 * resource to provide key->value lookups from base unit to category, as well as
 * preserving ordering information for these categories. See `units.txt`.
 *
 * For example: "kilogram" -> "mass", "meter-per-second" -> "speed".
 *
 * In C++ unitQuantity values are collected in order into a char16_t* array, while
 * unitQuantity keys are added added to a TrieBuilder, with associated values
 * being the index into the aforementioned char16_t* array.
 */
class CategoriesSink : public icu::ResourceSink {
  public:
    /**
     * Constructor.
     * @param out Array of char16_t* to which unitQuantity values will be saved.
     *     The pointers returned  not owned: they point directly at the resource
     *     strings in static memory.
     * @param outSize The size of the `out` array.
     * @param trieBuilder The trie builder to which the keys (base units) of
     *     each unitQuantity will be added, each with value being the offset
     *     into `out`.
     */
    explicit CategoriesSink(const char16_t **out, int32_t &outSize, BytesTrieBuilder &trieBuilder)
        : outQuantitiesArray(out), outSize(outSize), trieBuilder(trieBuilder), outIndex(0) {}

    void put(const char * /*key*/, ResourceValue &value, UBool /*noFallback*/, UErrorCode &status) override {
        ResourceArray array = value.getArray(status);
        if (U_FAILURE(status)) {
            return;
        }

        if (outIndex + array.getSize() > outSize) {
            status = U_INDEX_OUTOFBOUNDS_ERROR;
            return;
        }

        for (int32_t i = 0; array.getValue(i, value); ++i) {
            U_ASSERT(outIndex < outSize);
            ResourceTable table = value.getTable(status);
            if (U_FAILURE(status)) {
                return;
            }
            if (table.getSize() != 1) {
                status = U_INVALID_FORMAT_ERROR;
                return;
            }
            const char *key;
            table.getKeyAndValue(0, key, value);
            int32_t uTmpLen;
            outQuantitiesArray[outIndex] = value.getString(uTmpLen, status);
            trieBuilder.add(key, outIndex, status);
            outIndex++;
        }
    }

  private:
    const char16_t **outQuantitiesArray;
    int32_t &outSize;
    BytesTrieBuilder &trieBuilder;

    int32_t outIndex;
};

icu::UInitOnce gUnitExtrasInitOnce {};

// Array of simple unit IDs.
//
// The array memory itself is owned by this pointer, but the individual char* in
// that array point at static memory. (Note that these char* are also returned
// by SingleUnitImpl::getSimpleUnitID().)
const char **gSimpleUnits = nullptr;

// Maps from the value associated with each simple unit ID to an index into the
// gCategories array.
int32_t *gSimpleUnitCategories = nullptr;

char *gSerializedUnitExtrasStemTrie = nullptr;

// Array of char16_t* pointing at the unit categories (aka "quantities", aka
// "types"), as found in the `unitQuantities` resource. The array memory itself
// is owned by this pointer, but the individual char16_t* in that array point at
// static memory.
const char16_t **gCategories = nullptr;
// Number of items in `gCategories`.
int32_t gCategoriesCount = 0;
// Serialized BytesTrie for mapping from base units to indices into gCategories.
char *gSerializedUnitCategoriesTrie = nullptr;

UBool U_CALLCONV cleanupUnitExtras() {
    uprv_free(gSerializedUnitCategoriesTrie);
    gSerializedUnitCategoriesTrie = nullptr;
    uprv_free(gCategories);
    gCategories = nullptr;
    uprv_free(gSerializedUnitExtrasStemTrie);
    gSerializedUnitExtrasStemTrie = nullptr;
    uprv_free(gSimpleUnitCategories);
    gSimpleUnitCategories = nullptr;
    uprv_free(gSimpleUnits);
    gSimpleUnits = nullptr;
    gUnitExtrasInitOnce.reset();
    return true;
}

void U_CALLCONV initUnitExtras(UErrorCode& status) {
    ucln_i18n_registerCleanup(UCLN_I18N_UNIT_EXTRAS, cleanupUnitExtras);
    LocalUResourceBundlePointer unitsBundle(ures_openDirect(nullptr, "units", &status));

    // Collect unitQuantities information into gSerializedUnitCategoriesTrie and gCategories.
    const char *CATEGORY_TABLE_NAME = "unitQuantities";
    LocalUResourceBundlePointer unitQuantities(
        ures_getByKey(unitsBundle.getAlias(), CATEGORY_TABLE_NAME, nullptr, &status));
    if (U_FAILURE(status)) { return; }
    gCategoriesCount = unitQuantities.getAlias()->fSize;
    size_t quantitiesMallocSize = sizeof(char16_t *) * gCategoriesCount;
    gCategories = static_cast<const char16_t **>(uprv_malloc(quantitiesMallocSize));
    if (gCategories == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    uprv_memset(gCategories, 0, quantitiesMallocSize);
    BytesTrieBuilder quantitiesBuilder(status);
    CategoriesSink categoriesSink(gCategories, gCategoriesCount, quantitiesBuilder);
    ures_getAllItemsWithFallback(unitsBundle.getAlias(), CATEGORY_TABLE_NAME, categoriesSink, status);
    StringPiece resultQuantities = quantitiesBuilder.buildStringPiece(USTRINGTRIE_BUILD_FAST, status);
    if (U_FAILURE(status)) { return; }
    // Copy the result into the global constant pointer
    size_t numBytesQuantities = resultQuantities.length();
    gSerializedUnitCategoriesTrie = static_cast<char *>(uprv_malloc(numBytesQuantities));
    if (gSerializedUnitCategoriesTrie == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    uprv_memcpy(gSerializedUnitCategoriesTrie, resultQuantities.data(), numBytesQuantities);

    // Build the BytesTrie that Parser needs for parsing unit identifiers.

    BytesTrieBuilder b(status);
    if (U_FAILURE(status)) { return; }

    // Add SI and binary prefixes
    for (const auto& unitPrefixInfo : gUnitPrefixStrings) {
        b.add(unitPrefixInfo.string, unitPrefixInfo.value + kPrefixOffset, status);
    }
    if (U_FAILURE(status)) { return; }

    // Add syntax parts (compound, power prefixes)
    b.add("-per-", COMPOUND_PART_PER, status);
    b.add("-", COMPOUND_PART_TIMES, status);
    b.add("-and-", COMPOUND_PART_AND, status);
    b.add("per-", INITIAL_COMPOUND_PART_PER, status);
    b.add("square-", POWER_PART_P2, status);
    b.add("cubic-", POWER_PART_P3, status);
    b.add("pow2-", POWER_PART_P2, status);
    b.add("pow3-", POWER_PART_P3, status);
    b.add("pow4-", POWER_PART_P4, status);
    b.add("pow5-", POWER_PART_P5, status);
    b.add("pow6-", POWER_PART_P6, status);
    b.add("pow7-", POWER_PART_P7, status);
    b.add("pow8-", POWER_PART_P8, status);
    b.add("pow9-", POWER_PART_P9, status);
    b.add("pow10-", POWER_PART_P10, status);
    b.add("pow11-", POWER_PART_P11, status);
    b.add("pow12-", POWER_PART_P12, status);
    b.add("pow13-", POWER_PART_P13, status);
    b.add("pow14-", POWER_PART_P14, status);
    b.add("pow15-", POWER_PART_P15, status);
    if (U_FAILURE(status)) { return; }

    // Add sanctioned simple units by offset: simple units all have entries in
    // units/convertUnits resources.
    LocalUResourceBundlePointer convertUnits(
        ures_getByKey(unitsBundle.getAlias(), "convertUnits", nullptr, &status));
    if (U_FAILURE(status)) { return; }

    // Allocate enough space: with identifierSink below skipping kilogram, we're
    // probably allocating one more than needed.
    int32_t simpleUnitsCount = convertUnits.getAlias()->fSize;
    int32_t arrayMallocSize = sizeof(char *) * simpleUnitsCount;
    gSimpleUnits = static_cast<const char **>(uprv_malloc(arrayMallocSize));
    if (gSimpleUnits == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    uprv_memset(gSimpleUnits, 0, arrayMallocSize);
    arrayMallocSize = sizeof(int32_t) * simpleUnitsCount;
    gSimpleUnitCategories = static_cast<int32_t *>(uprv_malloc(arrayMallocSize));
    if (gSimpleUnitCategories == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    uprv_memset(gSimpleUnitCategories, 0, arrayMallocSize);

    // Populate gSimpleUnits and build the associated trie.
    SimpleUnitIdentifiersSink identifierSink(resultQuantities, gSimpleUnits, gSimpleUnitCategories,
                                             simpleUnitsCount, b, kSimpleUnitOffset);
    ures_getAllItemsWithFallback(unitsBundle.getAlias(), "convertUnits", identifierSink, status);

    // Build the CharsTrie
    // TODO: Use SLOW or FAST here?
    StringPiece result = b.buildStringPiece(USTRINGTRIE_BUILD_FAST, status);
    if (U_FAILURE(status)) { return; }

    // Copy the result into the global constant pointer
    size_t numBytes = result.length();
    gSerializedUnitExtrasStemTrie = static_cast<char *>(uprv_malloc(numBytes));
    if (gSerializedUnitExtrasStemTrie == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    uprv_memcpy(gSerializedUnitExtrasStemTrie, result.data(), numBytes);
}

class Token {
public:
    Token(int32_t match) : fMatch(match) {}

    enum Type {
        TYPE_UNDEFINED,
        TYPE_PREFIX,
        // Token type for "-per-", "-", and "-and-".
        TYPE_COMPOUND_PART,
        // Token type for "per-".
        TYPE_INITIAL_COMPOUND_PART,
        TYPE_POWER_PART,
        TYPE_SIMPLE_UNIT,
    };

    // Calling getType() is invalid, resulting in an assertion failure, if Token
    // value isn't positive.
    Type getType() const {
        U_ASSERT(fMatch > 0);
        if (fMatch < kCompoundPartOffset) {
            return TYPE_PREFIX;
        }
        if (fMatch < kInitialCompoundPartOffset) {
            return TYPE_COMPOUND_PART;
        }
        if (fMatch < kPowerPartOffset) {
            return TYPE_INITIAL_COMPOUND_PART;
        }
        if (fMatch < kSimpleUnitOffset) {
            return TYPE_POWER_PART;
        }
        return TYPE_SIMPLE_UNIT;
    }

    UMeasurePrefix getUnitPrefix() const {
        U_ASSERT(getType() == TYPE_PREFIX);
        return static_cast<UMeasurePrefix>(fMatch - kPrefixOffset);
    }

    // Valid only for tokens with type TYPE_COMPOUND_PART.
    int32_t getMatch() const {
        U_ASSERT(getType() == TYPE_COMPOUND_PART);
        return fMatch;
    }

    int32_t getInitialCompoundPart() const {
        // Even if there is only one InitialCompoundPart value, we have this
        // function for the simplicity of code consistency.
        U_ASSERT(getType() == TYPE_INITIAL_COMPOUND_PART);
        // Defensive: if this assert fails, code using this function also needs
        // to change.
        U_ASSERT(fMatch == INITIAL_COMPOUND_PART_PER);
        return fMatch;
    }

    int8_t getPower() const {
        U_ASSERT(getType() == TYPE_POWER_PART);
        return static_cast<int8_t>(fMatch - kPowerPartOffset);
    }

    int32_t getSimpleUnitIndex() const {
        U_ASSERT(getType() == TYPE_SIMPLE_UNIT);
        return fMatch - kSimpleUnitOffset;
    }

private:
    int32_t fMatch;
};

class Parser {
public:
    /**
     * Factory function for parsing the given identifier.
     *
     * @param source The identifier to parse. This function does not make a copy
     * of source: the underlying string that source points at, must outlive the
     * parser.
     * @param status ICU error code.
     */
    static Parser from(StringPiece source, UErrorCode& status) {
        if (U_FAILURE(status)) {
            return {};
        }
        umtx_initOnce(gUnitExtrasInitOnce, &initUnitExtras, status);
        if (U_FAILURE(status)) {
            return {};
        }
        return {source};
    }

    MeasureUnitImpl parse(UErrorCode& status) {
        MeasureUnitImpl result;

        if (U_FAILURE(status)) {
            return result;
        }
        if (fSource.empty()) {
            // The dimenionless unit: nothing to parse. leave result as is.
            return result;
        }

        while (hasNext()) {
            bool sawAnd = false;

            SingleUnitImpl singleUnit = nextSingleUnit(sawAnd, status);
            if (U_FAILURE(status)) {
                return result;
            }

            bool added = result.appendSingleUnit(singleUnit, status);
            if (U_FAILURE(status)) {
                return result;
            }

            if (sawAnd && !added) {
                // Two similar units are not allowed in a mixed unit.
                status = kUnitIdentifierSyntaxError;
                return result;
            }

            if (result.singleUnits.length() >= 2) {
                // nextSingleUnit fails appropriately for "per" and "and" in the
                // same identifier. It doesn't fail for other compound units
                // (COMPOUND_PART_TIMES). Consequently we take care of that
                // here.
                UMeasureUnitComplexity complexity =
                    sawAnd ? UMEASURE_UNIT_MIXED : UMEASURE_UNIT_COMPOUND;
                if (result.singleUnits.length() == 2) {
                    // After appending two singleUnits, the complexity will be `UMEASURE_UNIT_COMPOUND`
                    U_ASSERT(result.complexity == UMEASURE_UNIT_COMPOUND);
                    result.complexity = complexity;
                } else if (result.complexity != complexity) {
                    // Can't have mixed compound units
                    status = kUnitIdentifierSyntaxError;
                    return result;
                }
            }
        }

        return result;
    }

private:
    // Tracks parser progress: the offset into fSource.
    int32_t fIndex = 0;

    // Since we're not owning this memory, whatever is passed to the constructor
    // should live longer than this Parser - and the parser shouldn't return any
    // references to that string.
    StringPiece fSource;
    BytesTrie fTrie;

    // Set to true when we've seen a "-per-" or a "per-", after which all units
    // are in the denominator. Until we find an "-and-", at which point the
    // identifier is invalid pending TODO(CLDR-13701).
    bool fAfterPer = false;

    Parser() : fSource(""), fTrie(u"") {}

    Parser(StringPiece source)
        : fSource(source), fTrie(gSerializedUnitExtrasStemTrie) {}

    inline bool hasNext() const {
        return fIndex < fSource.length();
    }

    // Returns the next Token parsed from fSource, advancing fIndex to the end
    // of that token in fSource. In case of U_FAILURE(status), the token
    // returned will cause an abort if getType() is called on it.
    Token nextToken(UErrorCode& status) {
        fTrie.reset();
        int32_t match = -1;
        // Saves the position in the fSource string for the end of the most
        // recent matching token.
        int32_t previ = -1;
        // Find the longest token that matches a value in the trie:
        while (fIndex < fSource.length()) {
            auto result = fTrie.next(fSource.data()[fIndex++]);
            if (result == USTRINGTRIE_NO_MATCH) {
                break;
            } else if (result == USTRINGTRIE_NO_VALUE) {
                continue;
            }
            U_ASSERT(USTRINGTRIE_HAS_VALUE(result));
            match = fTrie.getValue();
            previ = fIndex;
            if (result == USTRINGTRIE_FINAL_VALUE) {
                break;
            }
            U_ASSERT(result == USTRINGTRIE_INTERMEDIATE_VALUE);
            // continue;
        }

        if (match < 0) {
            status = kUnitIdentifierSyntaxError;
        } else {
            fIndex = previ;
        }
        return {match};
    }

    /**
     * Returns the next "single unit" via result.
     *
     * If a "-per-" was parsed, the result will have appropriate negative
     * dimensionality.
     *
     * Returns an error if we parse both compound units and "-and-", since mixed
     * compound units are not yet supported - TODO(CLDR-13701).
     *
     * @param result Will be overwritten by the result, if status shows success.
     * @param sawAnd If an "-and-" was parsed prior to finding the "single
     * unit", sawAnd is set to true. If not, it is left as is.
     * @param status ICU error code.
     */
    SingleUnitImpl nextSingleUnit(bool &sawAnd, UErrorCode &status) {
        SingleUnitImpl result;
        if (U_FAILURE(status)) {
            return result;
        }

        // state:
        // 0 = no tokens seen yet (will accept power, SI or binary prefix, or simple unit)
        // 1 = power token seen (will not accept another power token)
        // 2 = SI or binary prefix token seen (will not accept a power, or SI or binary prefix token)
        int32_t state = 0;

        bool atStart = fIndex == 0;
        Token token = nextToken(status);
        if (U_FAILURE(status)) {
            return result;
        }

        if (atStart) {
            // Identifiers optionally start with "per-".
            if (token.getType() == Token::TYPE_INITIAL_COMPOUND_PART) {
                U_ASSERT(token.getInitialCompoundPart() == INITIAL_COMPOUND_PART_PER);
                fAfterPer = true;
                result.dimensionality = -1;

                token = nextToken(status);
                if (U_FAILURE(status)) {
                    return result;
                }
            }
        } else {
            // All other SingleUnit's are separated from previous SingleUnit's
            // via a compound part:
            if (token.getType() != Token::TYPE_COMPOUND_PART) {
                status = kUnitIdentifierSyntaxError;
                return result;
            }

            switch (token.getMatch()) {
            case COMPOUND_PART_PER:
                if (sawAnd) {
                    // Mixed compound units not yet supported,
                    // TODO(CLDR-13701).
                    status = kUnitIdentifierSyntaxError;
                    return result;
                }
                fAfterPer = true;
                result.dimensionality = -1;
                break;

            case COMPOUND_PART_TIMES:
                if (fAfterPer) {
                    result.dimensionality = -1;
                }
                break;

            case COMPOUND_PART_AND:
                if (fAfterPer) {
                    // Can't start with "-and-", and mixed compound units
                    // not yet supported, TODO(CLDR-13701).
                    status = kUnitIdentifierSyntaxError;
                    return result;
                }
                sawAnd = true;
                break;
            }

            token = nextToken(status);
            if (U_FAILURE(status)) {
                return result;
            }
        }

        // Read tokens until we have a complete SingleUnit or we reach the end.
        while (true) {
            switch (token.getType()) {
                case Token::TYPE_POWER_PART:
                    if (state > 0) {
                        status = kUnitIdentifierSyntaxError;
                        return result;
                    }
                    result.dimensionality *= token.getPower();
                    state = 1;
                    break;

                case Token::TYPE_PREFIX:
                    if (state > 1) {
                        status = kUnitIdentifierSyntaxError;
                        return result;
                    }
                    result.unitPrefix = token.getUnitPrefix();
                    state = 2;
                    break;

                case Token::TYPE_SIMPLE_UNIT:
                    result.index = token.getSimpleUnitIndex();
                    return result;

                default:
                    status = kUnitIdentifierSyntaxError;
                    return result;
            }

            if (!hasNext()) {
                // We ran out of tokens before finding a complete single unit.
                status = kUnitIdentifierSyntaxError;
                return result;
            }
            token = nextToken(status);
            if (U_FAILURE(status)) {
                return result;
            }
        }

        return result;
    }
};

// Sorting function wrapping SingleUnitImpl::compareTo for use with uprv_sortArray.
int32_t U_CALLCONV
compareSingleUnits(const void* /*context*/, const void* left, const void* right) {
    const auto* realLeft = static_cast<const SingleUnitImpl* const*>(left);
    const auto* realRight = static_cast<const SingleUnitImpl* const*>(right);
    return (*realLeft)->compareTo(**realRight);
}

// Returns an index into the gCategories array, for the "unitQuantity" (aka
// "type" or "category") associated with the given base unit identifier. Returns
// -1 on failure, together with U_UNSUPPORTED_ERROR.
int32_t getUnitCategoryIndex(BytesTrie &trie, StringPiece baseUnitIdentifier, UErrorCode &status) {
    UStringTrieResult result = trie.reset().next(baseUnitIdentifier.data(), baseUnitIdentifier.length());
    if (!USTRINGTRIE_HAS_VALUE(result)) {
        status = U_UNSUPPORTED_ERROR;
        return -1;
    }

    return trie.getValue();
}

} // namespace

U_CAPI int32_t U_EXPORT2
umeas_getPrefixPower(UMeasurePrefix unitPrefix) {
    if (unitPrefix >= UMEASURE_PREFIX_INTERNAL_MIN_BIN &&
        unitPrefix <= UMEASURE_PREFIX_INTERNAL_MAX_BIN) {
        return unitPrefix - UMEASURE_PREFIX_INTERNAL_ONE_BIN;
    }
    U_ASSERT(unitPrefix >= UMEASURE_PREFIX_INTERNAL_MIN_SI &&
             unitPrefix <= UMEASURE_PREFIX_INTERNAL_MAX_SI);
    return unitPrefix - UMEASURE_PREFIX_ONE;
}

U_CAPI int32_t U_EXPORT2
umeas_getPrefixBase(UMeasurePrefix unitPrefix) {
    if (unitPrefix >= UMEASURE_PREFIX_INTERNAL_MIN_BIN &&
        unitPrefix <= UMEASURE_PREFIX_INTERNAL_MAX_BIN) {
        return 1024;
    }
    U_ASSERT(unitPrefix >= UMEASURE_PREFIX_INTERNAL_MIN_SI &&
             unitPrefix <= UMEASURE_PREFIX_INTERNAL_MAX_SI);
    return 10;
}

CharString U_I18N_API getUnitQuantity(const MeasureUnitImpl &baseMeasureUnitImpl, UErrorCode &status) {
    CharString result;
    MeasureUnitImpl baseUnitImpl = baseMeasureUnitImpl.copy(status);
    UErrorCode localStatus = U_ZERO_ERROR;
    umtx_initOnce(gUnitExtrasInitOnce, &initUnitExtras, status);
    if (U_FAILURE(status)) {
        return result;
    }
    BytesTrie trie(gSerializedUnitCategoriesTrie);

    baseUnitImpl.serialize(status);
    StringPiece identifier = baseUnitImpl.identifier.data();
    int32_t idx = getUnitCategoryIndex(trie, identifier, localStatus);
    if (U_FAILURE(status)) {
        return result;
    }

    // In case the base unit identifier did not match any entry.
    if (U_FAILURE(localStatus)) {
        localStatus = U_ZERO_ERROR;
        baseUnitImpl.takeReciprocal(status);
        baseUnitImpl.serialize(status);
        identifier.set(baseUnitImpl.identifier.data());
        idx = getUnitCategoryIndex(trie, identifier, localStatus);

        if (U_FAILURE(status)) {
            return result;
        }
    }

    // In case the reciprocal of the base unit identifier did not match any entry.
    MeasureUnitImpl simplifiedUnit = baseMeasureUnitImpl.copyAndSimplify(status);
    if (U_FAILURE(status)) {
        return result;
    }
    if (U_FAILURE(localStatus)) {
        localStatus = U_ZERO_ERROR;
        simplifiedUnit.serialize(status);
        identifier.set(simplifiedUnit.identifier.data());
        idx = getUnitCategoryIndex(trie, identifier, localStatus);

        if (U_FAILURE(status)) {
            return result;
        }
    }

    // In case the simplified base unit identifier did not match any entry.
    if (U_FAILURE(localStatus)) {
        localStatus = U_ZERO_ERROR;
        simplifiedUnit.takeReciprocal(status);
        simplifiedUnit.serialize(status);
        identifier.set(simplifiedUnit.identifier.data());
        idx = getUnitCategoryIndex(trie, identifier, localStatus);

        if (U_FAILURE(status)) {
            return result;
        }
    }

    // If there is no match at all, throw an exception.
    if (U_FAILURE(localStatus)) {
        status = U_INVALID_FORMAT_ERROR;
        return result;
    }

    if (idx < 0 || idx >= gCategoriesCount) {
        status = U_INVALID_FORMAT_ERROR;
        return result;
    }

    result.appendInvariantChars(gCategories[idx], u_strlen(gCategories[idx]), status);
    return result;
}

// In ICU4J, this is MeasureUnit.getSingleUnitImpl().
SingleUnitImpl SingleUnitImpl::forMeasureUnit(const MeasureUnit& measureUnit, UErrorCode& status) {
    MeasureUnitImpl temp;
    const MeasureUnitImpl& impl = MeasureUnitImpl::forMeasureUnit(measureUnit, temp, status);
    if (U_FAILURE(status)) {
        return {};
    }
    if (impl.singleUnits.length() == 0) {
        return {};
    }
    if (impl.singleUnits.length() == 1) {
        return *impl.singleUnits[0];
    }
    status = U_ILLEGAL_ARGUMENT_ERROR;
    return {};
}

MeasureUnit SingleUnitImpl::build(UErrorCode& status) const {
    MeasureUnitImpl temp;
    temp.appendSingleUnit(*this, status);
    // TODO(icu-units#28): the MeasureUnitImpl::build() method uses
    // findBySubtype, which is relatively slow.
    // - At the time of loading the simple unit IDs, we could also save a
    //   mapping to the builtin MeasureUnit type and subtype they correspond to.
    // - This method could then check dimensionality and index, and if both are
    //   1, directly return MeasureUnit instances very quickly.
    return std::move(temp).build(status);
}

const char *SingleUnitImpl::getSimpleUnitID() const {
    return gSimpleUnits[index];
}

void SingleUnitImpl::appendNeutralIdentifier(CharString &result, UErrorCode &status) const UPRV_NO_SANITIZE_UNDEFINED {
    int32_t absPower = std::abs(this->dimensionality);

    U_ASSERT(absPower > 0); // "this function does not support the dimensionless single units";
    
    if (absPower == 1) {
        // no-op
    } else if (absPower == 2) {
        result.append(StringPiece("square-"), status);
    } else if (absPower == 3) {
        result.append(StringPiece("cubic-"), status);
    } else if (absPower <= 15) {
        result.append(StringPiece("pow"), status);
        result.appendNumber(absPower, status);
        result.append(StringPiece("-"), status);
    } else {
        status = U_ILLEGAL_ARGUMENT_ERROR; // Unit Identifier Syntax Error
        return;
    }

    if (U_FAILURE(status)) {
        return;
    }

    if (this->unitPrefix != UMEASURE_PREFIX_ONE) {
        bool found = false;
        for (const auto &unitPrefixInfo : gUnitPrefixStrings) {
            // TODO: consider using binary search? If we do this, add a unit
            // test to ensure gUnitPrefixStrings is sorted?
            if (unitPrefixInfo.value == this->unitPrefix) {
                result.append(unitPrefixInfo.string, status);
                found = true;
                break;
            }
        }
        if (!found) {
            status = U_UNSUPPORTED_ERROR;
            return;
        }
    }

    result.append(StringPiece(this->getSimpleUnitID()), status);
}

int32_t SingleUnitImpl::getUnitCategoryIndex() const {
    return gSimpleUnitCategories[index];
}

MeasureUnitImpl::MeasureUnitImpl(const SingleUnitImpl &singleUnit, UErrorCode &status) {
    this->appendSingleUnit(singleUnit, status);
}

MeasureUnitImpl MeasureUnitImpl::forIdentifier(StringPiece identifier, UErrorCode& status) {
    return Parser::from(identifier, status).parse(status);
}

const MeasureUnitImpl& MeasureUnitImpl::forMeasureUnit(
        const MeasureUnit& measureUnit, MeasureUnitImpl& memory, UErrorCode& status) {
    if (measureUnit.fImpl) {
        return *measureUnit.fImpl;
    } else {
        memory = Parser::from(measureUnit.getIdentifier(), status).parse(status);
        return memory;
    }
}

MeasureUnitImpl MeasureUnitImpl::forMeasureUnitMaybeCopy(
        const MeasureUnit& measureUnit, UErrorCode& status) {
    if (measureUnit.fImpl) {
        return measureUnit.fImpl->copy(status);
    } else {
        return Parser::from(measureUnit.getIdentifier(), status).parse(status);
    }
}

void MeasureUnitImpl::takeReciprocal(UErrorCode& /*status*/) {
    identifier.clear();
    for (int32_t i = 0; i < singleUnits.length(); i++) {
        singleUnits[i]->dimensionality *= -1;
    }
}

MeasureUnitImpl MeasureUnitImpl::copyAndSimplify(UErrorCode &status) const {
    MeasureUnitImpl result;
    for (int32_t i = 0; i < singleUnits.length(); i++) {
        const SingleUnitImpl &singleUnit = *this->singleUnits[i];
        
        // The following `for` loop will cause time complexity to be O(n^2).
        // However, n is very small (number of units, generally, at maximum equal to 10)
        bool unitExist = false;
        for (int32_t j = 0; j < result.singleUnits.length(); j++) {
            if (uprv_strcmp(result.singleUnits[j]->getSimpleUnitID(), singleUnit.getSimpleUnitID()) ==
                    0 &&
                result.singleUnits[j]->unitPrefix == singleUnit.unitPrefix) {
                unitExist = true;
                result.singleUnits[j]->dimensionality =
                    result.singleUnits[j]->dimensionality + singleUnit.dimensionality;
                break;
            }
        }

        if (!unitExist) {
            result.appendSingleUnit(singleUnit, status);
        }
    }

    return result;
}

bool MeasureUnitImpl::appendSingleUnit(const SingleUnitImpl &singleUnit, UErrorCode &status) {
    identifier.clear();

    if (singleUnit.isDimensionless()) {
        // Do not append dimensionless units.
        return false;
    }

    // Find a similar unit that already exists, to attempt to coalesce
    SingleUnitImpl *oldUnit = nullptr;
    for (int32_t i = 0; i < this->singleUnits.length(); i++) {
        auto *candidate = this->singleUnits[i];
        if (candidate->isCompatibleWith(singleUnit)) {
            oldUnit = candidate;
        }
    }

    if (oldUnit) {
        // Both dimensionalities will be positive, or both will be negative, by
        // virtue of isCompatibleWith().
        oldUnit->dimensionality += singleUnit.dimensionality;

        return false;
    }

    // Add a copy of singleUnit
    // NOTE: MaybeStackVector::emplaceBackAndCheckErrorCode creates new copy of  singleUnit.
    this->singleUnits.emplaceBackAndCheckErrorCode(status, singleUnit);
    if (U_FAILURE(status)) {
        return false;
    }

    // If the MeasureUnitImpl is `UMEASURE_UNIT_SINGLE` and after the appending a unit, the `singleUnits`
    // contains more than one. thus means the complexity should be `UMEASURE_UNIT_COMPOUND`
    if (this->singleUnits.length() > 1 &&
        this->complexity == UMeasureUnitComplexity::UMEASURE_UNIT_SINGLE) {
        this->complexity = UMeasureUnitComplexity::UMEASURE_UNIT_COMPOUND;
    }

    return true;
}

MaybeStackVector<MeasureUnitImplWithIndex>
MeasureUnitImpl::extractIndividualUnitsWithIndices(UErrorCode &status) const {
    MaybeStackVector<MeasureUnitImplWithIndex> result;

    if (this->complexity != UMeasureUnitComplexity::UMEASURE_UNIT_MIXED) {
        result.emplaceBackAndCheckErrorCode(status, 0, *this, status);
        return result;
    }

    for (int32_t i = 0; i < singleUnits.length(); ++i) {
        result.emplaceBackAndCheckErrorCode(status, i, *singleUnits[i], status);
        if (U_FAILURE(status)) {
            return result;
        }
    }

    return result;
}

/**
 * Normalize a MeasureUnitImpl and generate the identifier string in place.
 */
void MeasureUnitImpl::serialize(UErrorCode &status) {
    if (U_FAILURE(status)) {
        return;
    }

    if (this->singleUnits.length() == 0) {
        // Dimensionless, constructed by the default constructor.
        return;
    }

    if (this->complexity == UMEASURE_UNIT_COMPOUND) {
        // Note: don't sort a MIXED unit
        uprv_sortArray(this->singleUnits.getAlias(), this->singleUnits.length(),
                       sizeof(this->singleUnits[0]), compareSingleUnits, nullptr, false, &status);
        if (U_FAILURE(status)) {
            return;
        }
    }

    CharString result;
    bool beforePer = true;
    bool firstTimeNegativeDimension = false;
    for (int32_t i = 0; i < this->singleUnits.length(); i++) {
        if (beforePer && (*this->singleUnits[i]).dimensionality < 0) {
            beforePer = false;
            firstTimeNegativeDimension = true;
        } else if ((*this->singleUnits[i]).dimensionality < 0) {
            firstTimeNegativeDimension = false;
        }

        if (U_FAILURE(status)) {
            return;
        }

        if (this->complexity == UMeasureUnitComplexity::UMEASURE_UNIT_MIXED) {
            if (result.length() != 0) {
                result.append(StringPiece("-and-"), status);
            }
        } else {
            if (firstTimeNegativeDimension) {
                if (result.length() == 0) {
                    result.append(StringPiece("per-"), status);
                } else {
                    result.append(StringPiece("-per-"), status);
                }
            } else {
                if (result.length() != 0) {
                    result.append(StringPiece("-"), status);
                }
            }
        }

        this->singleUnits[i]->appendNeutralIdentifier(result, status);
    }

    this->identifier = CharString(result, status);
}

MeasureUnit MeasureUnitImpl::build(UErrorCode& status) && {
    this->serialize(status);
    return MeasureUnit(std::move(*this));
}

MeasureUnit MeasureUnit::forIdentifier(StringPiece identifier, UErrorCode& status) {
    return Parser::from(identifier, status).parse(status).build(status);
}

UMeasureUnitComplexity MeasureUnit::getComplexity(UErrorCode& status) const {
    MeasureUnitImpl temp;
    return MeasureUnitImpl::forMeasureUnit(*this, temp, status).complexity;
}

UMeasurePrefix MeasureUnit::getPrefix(UErrorCode& status) const {
    return SingleUnitImpl::forMeasureUnit(*this, status).unitPrefix;
}

MeasureUnit MeasureUnit::withPrefix(UMeasurePrefix prefix, UErrorCode& status) const UPRV_NO_SANITIZE_UNDEFINED {
    SingleUnitImpl singleUnit = SingleUnitImpl::forMeasureUnit(*this, status);
    singleUnit.unitPrefix = prefix;
    return singleUnit.build(status);
}

int32_t MeasureUnit::getDimensionality(UErrorCode& status) const {
    SingleUnitImpl singleUnit = SingleUnitImpl::forMeasureUnit(*this, status);
    if (U_FAILURE(status)) { return 0; }
    if (singleUnit.isDimensionless()) {
        return 0;
    }
    return singleUnit.dimensionality;
}

MeasureUnit MeasureUnit::withDimensionality(int32_t dimensionality, UErrorCode& status) const {
    SingleUnitImpl singleUnit = SingleUnitImpl::forMeasureUnit(*this, status);
    singleUnit.dimensionality = dimensionality;
    return singleUnit.build(status);
}

MeasureUnit MeasureUnit::reciprocal(UErrorCode& status) const {
    MeasureUnitImpl impl = MeasureUnitImpl::forMeasureUnitMaybeCopy(*this, status);
    impl.takeReciprocal(status);
    return std::move(impl).build(status);
}

MeasureUnit MeasureUnit::product(const MeasureUnit& other, UErrorCode& status) const {
    MeasureUnitImpl impl = MeasureUnitImpl::forMeasureUnitMaybeCopy(*this, status);
    MeasureUnitImpl temp;
    const MeasureUnitImpl& otherImpl = MeasureUnitImpl::forMeasureUnit(other, temp, status);
    if (impl.complexity == UMEASURE_UNIT_MIXED || otherImpl.complexity == UMEASURE_UNIT_MIXED) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return {};
    }
    for (int32_t i = 0; i < otherImpl.singleUnits.length(); i++) {
        impl.appendSingleUnit(*otherImpl.singleUnits[i], status);
    }
    if (impl.singleUnits.length() > 1) {
        impl.complexity = UMEASURE_UNIT_COMPOUND;
    }
    return std::move(impl).build(status);
}

LocalArray<MeasureUnit> MeasureUnit::splitToSingleUnitsImpl(int32_t& outCount, UErrorCode& status) const {
    MeasureUnitImpl temp;
    const MeasureUnitImpl& impl = MeasureUnitImpl::forMeasureUnit(*this, temp, status);
    outCount = impl.singleUnits.length();
    MeasureUnit* arr = new MeasureUnit[outCount];
    if (arr == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return LocalArray<MeasureUnit>();
    }
    for (int32_t i = 0; i < outCount; i++) {
        arr[i] = impl.singleUnits[i]->build(status);
    }
    return LocalArray<MeasureUnit>(arr, status);
}


U_NAMESPACE_END

#endif /* !UNCONFIG_NO_FORMATTING */
