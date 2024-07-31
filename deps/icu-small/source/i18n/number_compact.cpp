// © 2017 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/ustring.h"
#include "unicode/ures.h"
#include "cstring.h"
#include "charstr.h"
#include "resource.h"
#include "number_compact.h"
#include "number_microprops.h"
#include "uresimp.h"

using namespace icu;
using namespace icu::number;
using namespace icu::number::impl;

namespace {

// A dummy object used when a "0" compact decimal entry is encountered. This is necessary
// in order to prevent falling back to root. Object equality ("==") is intended.
const char16_t *USE_FALLBACK = u"<USE FALLBACK>";

/** Produces a string like "NumberElements/latn/patternsShort/decimalFormat". */
void getResourceBundleKey(const char *nsName, CompactStyle compactStyle, CompactType compactType,
                                 CharString &sb, UErrorCode &status) {
    sb.clear();
    sb.append("NumberElements/", status);
    sb.append(nsName, status);
    sb.append(compactStyle == CompactStyle::UNUM_SHORT ? "/patternsShort" : "/patternsLong", status);
    sb.append(compactType == CompactType::TYPE_DECIMAL ? "/decimalFormat" : "/currencyFormat", status);
}

int32_t getIndex(int32_t magnitude, StandardPlural::Form plural) {
    return magnitude * StandardPlural::COUNT + plural;
}

int32_t countZeros(const char16_t *patternString, int32_t patternLength) {
    // NOTE: This strategy for computing the number of zeros is a hack for efficiency.
    // It could break if there are any 0s that aren't part of the main pattern.
    int32_t numZeros = 0;
    for (int32_t i = 0; i < patternLength; i++) {
        if (patternString[i] == u'0') {
            numZeros++;
        } else if (numZeros > 0) {
            break; // zeros should always be contiguous
        }
    }
    return numZeros;
}

} // namespace

// NOTE: patterns and multipliers both get zero-initialized.
CompactData::CompactData() : patterns(), multipliers(), largestMagnitude(0), isEmpty(true) {
}

void CompactData::populate(const Locale &locale, const char *nsName, CompactStyle compactStyle,
                           CompactType compactType, UErrorCode &status) {
    CompactDataSink sink(*this);
    LocalUResourceBundlePointer rb(ures_open(nullptr, locale.getName(), &status));
    if (U_FAILURE(status)) { return; }

    bool nsIsLatn = strcmp(nsName, "latn") == 0;
    bool compactIsShort = compactStyle == CompactStyle::UNUM_SHORT;

    // Fall back to latn numbering system and/or short compact style.
    CharString resourceKey;
    getResourceBundleKey(nsName, compactStyle, compactType, resourceKey, status);
    UErrorCode localStatus = U_ZERO_ERROR;
    ures_getAllItemsWithFallback(rb.getAlias(), resourceKey.data(), sink, localStatus);
    if (isEmpty && !nsIsLatn) {
        getResourceBundleKey("latn", compactStyle, compactType, resourceKey, status);
        localStatus = U_ZERO_ERROR;
        ures_getAllItemsWithFallback(rb.getAlias(), resourceKey.data(), sink, localStatus);
    }
    if (isEmpty && !compactIsShort) {
        getResourceBundleKey(nsName, CompactStyle::UNUM_SHORT, compactType, resourceKey, status);
        localStatus = U_ZERO_ERROR;
        ures_getAllItemsWithFallback(rb.getAlias(), resourceKey.data(), sink, localStatus);
    }
    if (isEmpty && !nsIsLatn && !compactIsShort) {
        getResourceBundleKey("latn", CompactStyle::UNUM_SHORT, compactType, resourceKey, status);
        localStatus = U_ZERO_ERROR;
        ures_getAllItemsWithFallback(rb.getAlias(), resourceKey.data(), sink, localStatus);
    }

    // The last fallback should be guaranteed to return data.
    if (isEmpty) {
        status = U_INTERNAL_PROGRAM_ERROR;
    }
}

int32_t CompactData::getMultiplier(int32_t magnitude) const {
    if (magnitude < 0) {
        return 0;
    }
    if (magnitude > largestMagnitude) {
        magnitude = largestMagnitude;
    }
    return multipliers[magnitude];
}

const char16_t *CompactData::getPattern(
        int32_t magnitude,
        const PluralRules *rules,
        const DecimalQuantity &dq) const {
    if (magnitude < 0) {
        return nullptr;
    }
    if (magnitude > largestMagnitude) {
        magnitude = largestMagnitude;
    }
    const char16_t *patternString = nullptr;
    if (dq.hasIntegerValue()) {
        int64_t i = dq.toLong(true);
        if (i == 0) {
            patternString = patterns[getIndex(magnitude, StandardPlural::Form::EQ_0)];
        } else if (i == 1) {
            patternString = patterns[getIndex(magnitude, StandardPlural::Form::EQ_1)];
        }
        if (patternString != nullptr) {
            return patternString;
        }
    }
    StandardPlural::Form plural = utils::getStandardPlural(rules, dq);
    patternString = patterns[getIndex(magnitude, plural)];
    if (patternString == nullptr && plural != StandardPlural::OTHER) {
        // Fall back to "other" plural variant
        patternString = patterns[getIndex(magnitude, StandardPlural::OTHER)];
    }
    if (patternString == USE_FALLBACK) { // == is intended
        // Return null if USE_FALLBACK is present
        patternString = nullptr;
    }
    return patternString;
}

void CompactData::getUniquePatterns(UVector &output, UErrorCode &status) const {
    U_ASSERT(output.isEmpty());
    // NOTE: In C++, this is done more manually with a UVector.
    // In Java, we can take advantage of JDK HashSet.
    for (const auto* pattern : patterns) {
        if (pattern == nullptr || pattern == USE_FALLBACK) {
            continue;
        }

        // Insert pattern into the UVector if the UVector does not already contain the pattern.
        // Search the UVector from the end since identical patterns are likely to be adjacent.
        for (int32_t i = output.size() - 1; i >= 0; i--) {
            if (u_strcmp(pattern, static_cast<const char16_t *>(output[i])) == 0) {
                goto continue_outer;
            }
        }

        // The string was not found; add it to the UVector.
        // Note: must cast off const from pattern to store it in a UVector, which expects (void *)
        output.addElement(const_cast<char16_t *>(pattern), status);

        continue_outer:
        continue;
    }
}

void CompactData::CompactDataSink::put(const char *key, ResourceValue &value, UBool /*noFallback*/,
                                       UErrorCode &status) {
    // traverse into the table of powers of ten
    ResourceTable powersOfTenTable = value.getTable(status);
    if (U_FAILURE(status)) { return; }
    for (int i3 = 0; powersOfTenTable.getKeyAndValue(i3, key, value); ++i3) {

        // Assumes that the keys are always of the form "10000" where the magnitude is the
        // length of the key minus one.  We only support magnitudes less than COMPACT_MAX_DIGITS;
        // ignore entries that have greater magnitude.
        auto magnitude = static_cast<int8_t> (strlen(key) - 1);
        U_ASSERT(magnitude < COMPACT_MAX_DIGITS); // debug assert
        if (magnitude >= COMPACT_MAX_DIGITS) { // skip in production
            continue;
        }
        int8_t multiplier = data.multipliers[magnitude];

        // Iterate over the plural variants ("one", "other", etc)
        ResourceTable pluralVariantsTable = value.getTable(status);
        if (U_FAILURE(status)) { return; }
        for (int i4 = 0; pluralVariantsTable.getKeyAndValue(i4, key, value); ++i4) {
            // Skip this magnitude/plural if we already have it from a child locale.
            // Note: This also skips USE_FALLBACK entries.
            StandardPlural::Form plural = StandardPlural::fromString(key, status);
            if (U_FAILURE(status)) { return; }
            if (data.patterns[getIndex(magnitude, plural)] != nullptr) {
                continue;
            }

            // The value "0" means that we need to use the default pattern and not fall back
            // to parent locales. Example locale where this is relevant: 'it'.
            int32_t patternLength;
            const char16_t *patternString = value.getString(patternLength, status);
            if (U_FAILURE(status)) { return; }
            if (u_strcmp(patternString, u"0") == 0) {
                patternString = USE_FALLBACK;
                patternLength = 0;
            }

            // Save the pattern string. We will parse it lazily.
            data.patterns[getIndex(magnitude, plural)] = patternString;

            // If necessary, compute the multiplier: the difference between the magnitude
            // and the number of zeros in the pattern.
            if (multiplier == 0) {
                int32_t numZeros = countZeros(patternString, patternLength);
                if (numZeros > 0) { // numZeros==0 in certain cases, like Somali "Kun"
                    multiplier = static_cast<int8_t> (numZeros - magnitude - 1);
                }
            }
        }

        // Save the multiplier.
        if (data.multipliers[magnitude] == 0) {
            data.multipliers[magnitude] = multiplier;
            if (magnitude > data.largestMagnitude) {
                data.largestMagnitude = magnitude;
            }
            data.isEmpty = false;
        } else {
            U_ASSERT(data.multipliers[magnitude] == multiplier);
        }
    }
}

///////////////////////////////////////////////////////////
/// END OF CompactData.java; BEGIN CompactNotation.java ///
///////////////////////////////////////////////////////////

CompactHandler::CompactHandler(
        CompactStyle compactStyle,
        const Locale &locale,
        const char *nsName,
        CompactType compactType,
        const PluralRules *rules,
        MutablePatternModifier *buildReference,
        bool safe,
        const MicroPropsGenerator *parent,
        UErrorCode &status)
        : rules(rules), parent(parent), safe(safe) {
    data.populate(locale, nsName, compactStyle, compactType, status);
    if (safe) {
        // Safe code path
        precomputeAllModifiers(*buildReference, status);
    } else {
        // Unsafe code path
        // Store the MutablePatternModifier reference.
        unsafePatternModifier = buildReference;
    }
}

CompactHandler::~CompactHandler() {
    for (int32_t i = 0; i < precomputedModsLength; i++) {
        delete precomputedMods[i].mod;
    }
}

void CompactHandler::precomputeAllModifiers(MutablePatternModifier &buildReference, UErrorCode &status) {
    if (U_FAILURE(status)) { return; }

    // Initial capacity of 12 for 0K, 00K, 000K, ...M, ...B, and ...T
    UVector allPatterns(12, status);
    if (U_FAILURE(status)) { return; }
    data.getUniquePatterns(allPatterns, status);
    if (U_FAILURE(status)) { return; }

    // C++ only: ensure that precomputedMods has room.
    precomputedModsLength = allPatterns.size();
    if (precomputedMods.getCapacity() < precomputedModsLength) {
        precomputedMods.resize(allPatterns.size(), status);
        if (U_FAILURE(status)) { return; }
    }

    for (int32_t i = 0; i < precomputedModsLength; i++) {
        const auto* patternString = static_cast<const char16_t*>(allPatterns[i]);
        UnicodeString hello(patternString);
        CompactModInfo &info = precomputedMods[i];
        ParsedPatternInfo patternInfo;
        PatternParser::parseToPatternInfo(UnicodeString(patternString), patternInfo, status);
        if (U_FAILURE(status)) { return; }
        buildReference.setPatternInfo(&patternInfo, {UFIELD_CATEGORY_NUMBER, UNUM_COMPACT_FIELD});
        info.mod = buildReference.createImmutable(status);
        if (U_FAILURE(status)) { return; }
        info.patternString = patternString;
    }
}

void CompactHandler::processQuantity(DecimalQuantity &quantity, MicroProps &micros,
                                     UErrorCode &status) const {
    parent->processQuantity(quantity, micros, status);
    if (U_FAILURE(status)) { return; }

    // Treat zero, NaN, and infinity as if they had magnitude 0
    int32_t magnitude;
    int32_t multiplier = 0;
    if (quantity.isZeroish()) {
        magnitude = 0;
        micros.rounder.apply(quantity, status);
    } else {
        // TODO: Revisit chooseMultiplierAndApply
        multiplier = micros.rounder.chooseMultiplierAndApply(quantity, data, status);
        magnitude = quantity.isZeroish() ? 0 : quantity.getMagnitude();
        magnitude -= multiplier;
    }

    const char16_t *patternString = data.getPattern(magnitude, rules, quantity);
    if (patternString == nullptr) {
        // Use the default (non-compact) modifier.
        // No need to take any action.
    } else if (safe) {
        // Safe code path.
        // Java uses a hash set here for O(1) lookup.  C++ uses a linear search.
        // TODO: Benchmark this and maybe change to a binary search or hash table.
        int32_t i = 0;
        for (; i < precomputedModsLength; i++) {
            const CompactModInfo &info = precomputedMods[i];
            if (u_strcmp(patternString, info.patternString) == 0) {
                info.mod->applyToMicros(micros, quantity, status);
                break;
            }
        }
        // It should be guaranteed that we found the entry.
        U_ASSERT(i < precomputedModsLength);
    } else {
        // Unsafe code path.
        // Overwrite the PatternInfo in the existing modMiddle.
        // C++ Note: Use unsafePatternInfo for proper lifecycle.
        ParsedPatternInfo &patternInfo = const_cast<CompactHandler *>(this)->unsafePatternInfo;
        PatternParser::parseToPatternInfo(UnicodeString(patternString), patternInfo, status);
        unsafePatternModifier->setPatternInfo(
            &unsafePatternInfo,
            {UFIELD_CATEGORY_NUMBER, UNUM_COMPACT_FIELD});
        unsafePatternModifier->setNumberProperties(quantity.signum(), StandardPlural::Form::COUNT);
        micros.modMiddle = unsafePatternModifier;
    }

    // Change the exponent only after we select appropriate plural form
    // for formatting purposes so that we preserve expected formatted
    // string behavior.
    quantity.adjustExponent(-1 * multiplier);

    // We already performed rounding. Do not perform it again.
    micros.rounder = RoundingImpl::passThrough();
}

#endif /* #if !UCONFIG_NO_FORMATTING */
