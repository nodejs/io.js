// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2013-2014, International Business Machines
* Corporation and others.  All Rights Reserved.
*******************************************************************************
* collationbuilder.cpp
*
* (replaced the former ucol_bld.cpp)
*
* created on: 2013may06
* created by: Markus W. Scherer
*/

#ifdef DEBUG_COLLATION_BUILDER
#include <stdio.h>
#endif

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

#include "unicode/caniter.h"
#include "unicode/normalizer2.h"
#include "unicode/tblcoll.h"
#include "unicode/parseerr.h"
#include "unicode/uchar.h"
#include "unicode/ucol.h"
#include "unicode/unistr.h"
#include "unicode/usetiter.h"
#include "unicode/utf16.h"
#include "unicode/uversion.h"
#include "cmemory.h"
#include "collation.h"
#include "collationbuilder.h"
#include "collationdata.h"
#include "collationdatabuilder.h"
#include "collationfastlatin.h"
#include "collationroot.h"
#include "collationrootelements.h"
#include "collationruleparser.h"
#include "collationsettings.h"
#include "collationtailoring.h"
#include "collationweights.h"
#include "normalizer2impl.h"
#include "uassert.h"
#include "ucol_imp.h"
#include "utf16collationiterator.h"

U_NAMESPACE_BEGIN

namespace {

class BundleImporter : public CollationRuleParser::Importer {
public:
    BundleImporter() {}
    virtual ~BundleImporter();
    virtual void getRules(
            const char *localeID, const char *collationType,
            UnicodeString &rules,
            const char *&errorReason, UErrorCode &errorCode) override;
};

BundleImporter::~BundleImporter() {}

void
BundleImporter::getRules(
        const char *localeID, const char *collationType,
        UnicodeString &rules,
        const char *& /*errorReason*/, UErrorCode &errorCode) {
    CollationLoader::loadRules(localeID, collationType, rules, errorCode);
}

}  // namespace

// RuleBasedCollator implementation ---------------------------------------- ***

// These methods are here, rather than in rulebasedcollator.cpp,
// for modularization:
// Most code using Collator does not need to build a Collator from rules.
// By moving these constructors and helper methods to a separate file,
// most code will not have a static dependency on the builder code.

RuleBasedCollator::RuleBasedCollator()
        : data(nullptr),
          settings(nullptr),
          tailoring(nullptr),
          cacheEntry(nullptr),
          validLocale(""),
          explicitlySetAttributes(0),
          actualLocaleIsSameAsValid(false) {
}

RuleBasedCollator::RuleBasedCollator(const UnicodeString &rules, UErrorCode &errorCode)
        : data(nullptr),
          settings(nullptr),
          tailoring(nullptr),
          cacheEntry(nullptr),
          validLocale(""),
          explicitlySetAttributes(0),
          actualLocaleIsSameAsValid(false) {
    internalBuildTailoring(rules, UCOL_DEFAULT, UCOL_DEFAULT, nullptr, nullptr, errorCode);
}

RuleBasedCollator::RuleBasedCollator(const UnicodeString &rules, ECollationStrength strength,
                                     UErrorCode &errorCode)
        : data(nullptr),
          settings(nullptr),
          tailoring(nullptr),
          cacheEntry(nullptr),
          validLocale(""),
          explicitlySetAttributes(0),
          actualLocaleIsSameAsValid(false) {
    internalBuildTailoring(rules, strength, UCOL_DEFAULT, nullptr, nullptr, errorCode);
}

RuleBasedCollator::RuleBasedCollator(const UnicodeString &rules,
                                     UColAttributeValue decompositionMode,
                                     UErrorCode &errorCode)
        : data(nullptr),
          settings(nullptr),
          tailoring(nullptr),
          cacheEntry(nullptr),
          validLocale(""),
          explicitlySetAttributes(0),
          actualLocaleIsSameAsValid(false) {
    internalBuildTailoring(rules, UCOL_DEFAULT, decompositionMode, nullptr, nullptr, errorCode);
}

RuleBasedCollator::RuleBasedCollator(const UnicodeString &rules,
                                     ECollationStrength strength,
                                     UColAttributeValue decompositionMode,
                                     UErrorCode &errorCode)
        : data(nullptr),
          settings(nullptr),
          tailoring(nullptr),
          cacheEntry(nullptr),
          validLocale(""),
          explicitlySetAttributes(0),
          actualLocaleIsSameAsValid(false) {
    internalBuildTailoring(rules, strength, decompositionMode, nullptr, nullptr, errorCode);
}

RuleBasedCollator::RuleBasedCollator(const UnicodeString &rules,
                                     UParseError &parseError, UnicodeString &reason,
                                     UErrorCode &errorCode)
        : data(nullptr),
          settings(nullptr),
          tailoring(nullptr),
          cacheEntry(nullptr),
          validLocale(""),
          explicitlySetAttributes(0),
          actualLocaleIsSameAsValid(false) {
    internalBuildTailoring(rules, UCOL_DEFAULT, UCOL_DEFAULT, &parseError, &reason, errorCode);
}

void
RuleBasedCollator::internalBuildTailoring(const UnicodeString &rules,
                                          int32_t strength,
                                          UColAttributeValue decompositionMode,
                                          UParseError *outParseError, UnicodeString *outReason,
                                          UErrorCode &errorCode) {
    const CollationTailoring *base = CollationRoot::getRoot(errorCode);
    if(U_FAILURE(errorCode)) { return; }
    if(outReason != nullptr) { outReason->remove(); }
    CollationBuilder builder(base, errorCode);
    UVersionInfo noVersion = { 0, 0, 0, 0 };
    BundleImporter importer;
    LocalPointer<CollationTailoring> t(builder.parseAndBuild(rules, noVersion,
                                                             &importer,
                                                             outParseError, errorCode));
    if(U_FAILURE(errorCode)) {
        const char *reason = builder.getErrorReason();
        if(reason != nullptr && outReason != nullptr) {
            *outReason = UnicodeString(reason, -1, US_INV);
        }
        return;
    }
    t->actualLocale.setToBogus();
    adoptTailoring(t.orphan(), errorCode);
    // Set attributes after building the collator,
    // to keep the default settings consistent with the rule string.
    if(strength != UCOL_DEFAULT) {
        setAttribute(UCOL_STRENGTH, (UColAttributeValue)strength, errorCode);
    }
    if(decompositionMode != UCOL_DEFAULT) {
        setAttribute(UCOL_NORMALIZATION_MODE, decompositionMode, errorCode);
    }
}

// CollationBuilder implementation ----------------------------------------- ***

CollationBuilder::CollationBuilder(const CollationTailoring *b, UBool icu4xMode, UErrorCode &errorCode)
        : nfd(*Normalizer2::getNFDInstance(errorCode)),
          fcd(*Normalizer2Factory::getFCDInstance(errorCode)),
          nfcImpl(*Normalizer2Factory::getNFCImpl(errorCode)),
          base(b),
          baseData(b->data),
          rootElements(b->data->rootElements, b->data->rootElementsLength),
          variableTop(0),
          dataBuilder(new CollationDataBuilder(icu4xMode, errorCode)), fastLatinEnabled(true),
          icu4xMode(icu4xMode),
          errorReason(nullptr),
          cesLength(0),
          rootPrimaryIndexes(errorCode), nodes(errorCode) {
    nfcImpl.ensureCanonIterData(errorCode);
    if(U_FAILURE(errorCode)) {
        errorReason = "CollationBuilder fields initialization failed";
        return;
    }
    if(dataBuilder == nullptr) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    dataBuilder->initForTailoring(baseData, errorCode);
    if(U_FAILURE(errorCode)) {
        errorReason = "CollationBuilder initialization failed";
    }
}

CollationBuilder::CollationBuilder(const CollationTailoring *b, UErrorCode &errorCode)
  : CollationBuilder(b, false, errorCode)
{}

CollationBuilder::~CollationBuilder() {
    delete dataBuilder;
}

CollationTailoring *
CollationBuilder::parseAndBuild(const UnicodeString &ruleString,
                                const UVersionInfo rulesVersion,
                                CollationRuleParser::Importer *importer,
                                UParseError *outParseError,
                                UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return nullptr; }
    if(baseData->rootElements == nullptr) {
        errorCode = U_MISSING_RESOURCE_ERROR;
        errorReason = "missing root elements data, tailoring not supported";
        return nullptr;
    }
    LocalPointer<CollationTailoring> tailoring(new CollationTailoring(base->settings));
    if(tailoring.isNull() || tailoring->isBogus()) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    CollationRuleParser parser(baseData, errorCode);
    if(U_FAILURE(errorCode)) { return nullptr; }
    // Note: This always bases &[last variable] and &[first regular]
    // on the root collator's maxVariable/variableTop.
    // If we wanted this to change after [maxVariable x], then we would keep
    // the tailoring.settings pointer here and read its variableTop when we need it.
    // See http://unicode.org/cldr/trac/ticket/6070
    variableTop = base->settings->variableTop;
    parser.setSink(this);
    parser.setImporter(importer);
    CollationSettings &ownedSettings = *SharedObject::copyOnWrite(tailoring->settings);
    parser.parse(ruleString, ownedSettings, outParseError, errorCode);
    errorReason = parser.getErrorReason();
    if(U_FAILURE(errorCode)) { return nullptr; }
    if(dataBuilder->hasMappings()) {
        makeTailoredCEs(errorCode);
        if (!icu4xMode) {
            closeOverComposites(errorCode);
        }
        finalizeCEs(errorCode);
        if (!icu4xMode) {
            // Copy all of ASCII, and Latin-1 letters, into each tailoring.
            optimizeSet.add(0, 0x7f);
            optimizeSet.add(0xc0, 0xff);
            // Hangul is decomposed on the fly during collation,
            // and the tailoring data is always built with HANGUL_TAG specials.
            optimizeSet.remove(Hangul::HANGUL_BASE, Hangul::HANGUL_END);
            dataBuilder->optimize(optimizeSet, errorCode);
        }
        tailoring->ensureOwnedData(errorCode);
        if(U_FAILURE(errorCode)) { return nullptr; }
        if(fastLatinEnabled) { dataBuilder->enableFastLatin(); }
        dataBuilder->build(*tailoring->ownedData, errorCode);
        tailoring->builder = dataBuilder;
        dataBuilder = nullptr;
    } else {
        tailoring->data = baseData;
    }
    if(U_FAILURE(errorCode)) { return nullptr; }
    ownedSettings.fastLatinOptions = CollationFastLatin::getOptions(
        tailoring->data, ownedSettings,
        ownedSettings.fastLatinPrimaries, UPRV_LENGTHOF(ownedSettings.fastLatinPrimaries));
    tailoring->rules = ruleString;
    tailoring->rules.getTerminatedBuffer();  // ensure NUL-termination
    tailoring->setVersion(base->version, rulesVersion);
    return tailoring.orphan();
}

void
CollationBuilder::addReset(int32_t strength, const UnicodeString &str,
                           const char *&parserErrorReason, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }
    U_ASSERT(!str.isEmpty());
    if(str.charAt(0) == CollationRuleParser::POS_LEAD) {
        ces[0] = getSpecialResetPosition(str, parserErrorReason, errorCode);
        cesLength = 1;
        if(U_FAILURE(errorCode)) { return; }
        U_ASSERT((ces[0] & Collation::CASE_AND_QUATERNARY_MASK) == 0);
    } else {
        // normal reset to a character or string
        UnicodeString nfdString = nfd.normalize(str, errorCode);
        if(U_FAILURE(errorCode)) {
            parserErrorReason = "normalizing the reset position";
            return;
        }
        cesLength = dataBuilder->getCEs(nfdString, ces, 0);
        if(cesLength > Collation::MAX_EXPANSION_LENGTH) {
            errorCode = U_ILLEGAL_ARGUMENT_ERROR;
            parserErrorReason = "reset position maps to too many collation elements (more than 31)";
            return;
        }
    }
    if(strength == UCOL_IDENTICAL) { return; }  // simple reset-at-position

    // &[before strength]position
    U_ASSERT(UCOL_PRIMARY <= strength && strength <= UCOL_TERTIARY);
    int32_t index = findOrInsertNodeForCEs(strength, parserErrorReason, errorCode);
    if(U_FAILURE(errorCode)) { return; }

    int64_t node = nodes.elementAti(index);
    // If the index is for a "weaker" node,
    // then skip backwards over this and further "weaker" nodes.
    while(strengthFromNode(node) > strength) {
        index = previousIndexFromNode(node);
        node = nodes.elementAti(index);
    }

    // Find or insert a node whose index we will put into a temporary CE.
    if(strengthFromNode(node) == strength && isTailoredNode(node)) {
        // Reset to just before this same-strength tailored node.
        index = previousIndexFromNode(node);
    } else if(strength == UCOL_PRIMARY) {
        // root primary node (has no previous index)
        uint32_t p = weight32FromNode(node);
        if(p == 0) {
            errorCode = U_UNSUPPORTED_ERROR;
            parserErrorReason = "reset primary-before ignorable not possible";
            return;
        }
        if(p <= rootElements.getFirstPrimary()) {
            // There is no primary gap between ignorables and the space-first-primary.
            errorCode = U_UNSUPPORTED_ERROR;
            parserErrorReason = "reset primary-before first non-ignorable not supported";
            return;
        }
        if(p == Collation::FIRST_TRAILING_PRIMARY) {
            // We do not support tailoring to an unassigned-implicit CE.
            errorCode = U_UNSUPPORTED_ERROR;
            parserErrorReason = "reset primary-before [first trailing] not supported";
            return;
        }
        p = rootElements.getPrimaryBefore(p, baseData->isCompressiblePrimary(p));
        index = findOrInsertNodeForPrimary(p, errorCode);
        // Go to the last node in this list:
        // Tailor after the last node between adjacent root nodes.
        for(;;) {
            node = nodes.elementAti(index);
            int32_t nextIndex = nextIndexFromNode(node);
            if(nextIndex == 0) { break; }
            index = nextIndex;
        }
    } else {
        // &[before 2] or &[before 3]
        index = findCommonNode(index, UCOL_SECONDARY);
        if(strength >= UCOL_TERTIARY) {
            index = findCommonNode(index, UCOL_TERTIARY);
        }
        // findCommonNode() stayed on the stronger node or moved to
        // an explicit common-weight node of the reset-before strength.
        node = nodes.elementAti(index);
        if(strengthFromNode(node) == strength) {
            // Found a same-strength node with an explicit weight.
            uint32_t weight16 = weight16FromNode(node);
            if(weight16 == 0) {
                errorCode = U_UNSUPPORTED_ERROR;
                if(strength == UCOL_SECONDARY) {
                    parserErrorReason = "reset secondary-before secondary ignorable not possible";
                } else {
                    parserErrorReason = "reset tertiary-before completely ignorable not possible";
                }
                return;
            }
            U_ASSERT(weight16 > Collation::BEFORE_WEIGHT16);
            // Reset to just before this node.
            // Insert the preceding same-level explicit weight if it is not there already.
            // Which explicit weight immediately precedes this one?
            weight16 = getWeight16Before(index, node, strength);
            // Does this preceding weight have a node?
            uint32_t previousWeight16;
            int32_t previousIndex = previousIndexFromNode(node);
            for(int32_t i = previousIndex;; i = previousIndexFromNode(node)) {
                node = nodes.elementAti(i);
                int32_t previousStrength = strengthFromNode(node);
                if(previousStrength < strength) {
                    U_ASSERT(weight16 >= Collation::COMMON_WEIGHT16 || i == previousIndex);
                    // Either the reset element has an above-common weight and
                    // the parent node provides the implied common weight,
                    // or the reset element has a weight<=common in the node
                    // right after the parent, and we need to insert the preceding weight.
                    previousWeight16 = Collation::COMMON_WEIGHT16;
                    break;
                } else if(previousStrength == strength && !isTailoredNode(node)) {
                    previousWeight16 = weight16FromNode(node);
                    break;
                }
                // Skip weaker nodes and same-level tailored nodes.
            }
            if(previousWeight16 == weight16) {
                // The preceding weight has a node,
                // maybe with following weaker or tailored nodes.
                // Reset to the last of them.
                index = previousIndex;
            } else {
                // Insert a node with the preceding weight, reset to that.
                node = nodeFromWeight16(weight16) | nodeFromStrength(strength);
                index = insertNodeBetween(previousIndex, index, node, errorCode);
            }
        } else {
            // Found a stronger node with implied strength-common weight.
            uint32_t weight16 = getWeight16Before(index, node, strength);
            index = findOrInsertWeakNode(index, weight16, strength, errorCode);
        }
        // Strength of the temporary CE = strength of its reset position.
        // Code above raises an error if the before-strength is stronger.
        strength = ceStrength(ces[cesLength - 1]);
    }
    if(U_FAILURE(errorCode)) {
        parserErrorReason = "inserting reset position for &[before n]";
        return;
    }
    ces[cesLength - 1] = tempCEFromIndexAndStrength(index, strength);
}

uint32_t
CollationBuilder::getWeight16Before(int32_t index, int64_t node, int32_t level) {
    U_ASSERT(strengthFromNode(node) < level || !isTailoredNode(node));
    // Collect the root CE weights if this node is for a root CE.
    // If it is not, then return the low non-primary boundary for a tailored CE.
    uint32_t t;
    if(strengthFromNode(node) == UCOL_TERTIARY) {
        t = weight16FromNode(node);
    } else {
        t = Collation::COMMON_WEIGHT16;  // Stronger node with implied common weight.
    }
    while(strengthFromNode(node) > UCOL_SECONDARY) {
        index = previousIndexFromNode(node);
        node = nodes.elementAti(index);
    }
    if(isTailoredNode(node)) {
        return Collation::BEFORE_WEIGHT16;
    }
    uint32_t s;
    if(strengthFromNode(node) == UCOL_SECONDARY) {
        s = weight16FromNode(node);
    } else {
        s = Collation::COMMON_WEIGHT16;  // Stronger node with implied common weight.
    }
    while(strengthFromNode(node) > UCOL_PRIMARY) {
        index = previousIndexFromNode(node);
        node = nodes.elementAti(index);
    }
    if(isTailoredNode(node)) {
        return Collation::BEFORE_WEIGHT16;
    }
    // [p, s, t] is a root CE. Return the preceding weight for the requested level.
    uint32_t p = weight32FromNode(node);
    uint32_t weight16;
    if(level == UCOL_SECONDARY) {
        weight16 = rootElements.getSecondaryBefore(p, s);
    } else {
        weight16 = rootElements.getTertiaryBefore(p, s, t);
        U_ASSERT((weight16 & ~Collation::ONLY_TERTIARY_MASK) == 0);
    }
    return weight16;
}

int64_t
CollationBuilder::getSpecialResetPosition(const UnicodeString &str,
                                          const char *&parserErrorReason, UErrorCode &errorCode) {
    U_ASSERT(str.length() == 2);
    int64_t ce;
    int32_t strength = UCOL_PRIMARY;
    UBool isBoundary = false;
    UChar32 pos = str.charAt(1) - CollationRuleParser::POS_BASE;
    U_ASSERT(0 <= pos && pos <= CollationRuleParser::LAST_TRAILING);
    switch(pos) {
    case CollationRuleParser::FIRST_TERTIARY_IGNORABLE:
        // Quaternary CEs are not supported.
        // Non-zero quaternary weights are possible only on tertiary or stronger CEs.
        return 0;
    case CollationRuleParser::LAST_TERTIARY_IGNORABLE:
        return 0;
    case CollationRuleParser::FIRST_SECONDARY_IGNORABLE: {
        // Look for a tailored tertiary node after [0, 0, 0].
        int32_t index = findOrInsertNodeForRootCE(0, UCOL_TERTIARY, errorCode);
        if(U_FAILURE(errorCode)) { return 0; }
        int64_t node = nodes.elementAti(index);
        if((index = nextIndexFromNode(node)) != 0) {
            node = nodes.elementAti(index);
            U_ASSERT(strengthFromNode(node) <= UCOL_TERTIARY);
            if(isTailoredNode(node) && strengthFromNode(node) == UCOL_TERTIARY) {
                return tempCEFromIndexAndStrength(index, UCOL_TERTIARY);
            }
        }
        return rootElements.getFirstTertiaryCE();
        // No need to look for nodeHasAnyBefore() on a tertiary node.
    }
    case CollationRuleParser::LAST_SECONDARY_IGNORABLE:
        ce = rootElements.getLastTertiaryCE();
        strength = UCOL_TERTIARY;
        break;
    case CollationRuleParser::FIRST_PRIMARY_IGNORABLE: {
        // Look for a tailored secondary node after [0, 0, *].
        int32_t index = findOrInsertNodeForRootCE(0, UCOL_SECONDARY, errorCode);
        if(U_FAILURE(errorCode)) { return 0; }
        int64_t node = nodes.elementAti(index);
        while((index = nextIndexFromNode(node)) != 0) {
            node = nodes.elementAti(index);
            strength = strengthFromNode(node);
            if(strength < UCOL_SECONDARY) { break; }
            if(strength == UCOL_SECONDARY) {
                if(isTailoredNode(node)) {
                    if(nodeHasBefore3(node)) {
                        index = nextIndexFromNode(nodes.elementAti(nextIndexFromNode(node)));
                        U_ASSERT(isTailoredNode(nodes.elementAti(index)));
                    }
                    return tempCEFromIndexAndStrength(index, UCOL_SECONDARY);
                } else {
                    break;
                }
            }
        }
        ce = rootElements.getFirstSecondaryCE();
        strength = UCOL_SECONDARY;
        break;
    }
    case CollationRuleParser::LAST_PRIMARY_IGNORABLE:
        ce = rootElements.getLastSecondaryCE();
        strength = UCOL_SECONDARY;
        break;
    case CollationRuleParser::FIRST_VARIABLE:
        ce = rootElements.getFirstPrimaryCE();
        isBoundary = true;  // FractionalUCA.txt: FDD1 00A0, SPACE first primary
        break;
    case CollationRuleParser::LAST_VARIABLE:
        ce = rootElements.lastCEWithPrimaryBefore(variableTop + 1);
        break;
    case CollationRuleParser::FIRST_REGULAR:
        ce = rootElements.firstCEWithPrimaryAtLeast(variableTop + 1);
        isBoundary = true;  // FractionalUCA.txt: FDD1 263A, SYMBOL first primary
        break;
    case CollationRuleParser::LAST_REGULAR:
        // Use the Hani-first-primary rather than the actual last "regular" CE before it,
        // for backward compatibility with behavior before the introduction of
        // script-first-primary CEs in the root collator.
        ce = rootElements.firstCEWithPrimaryAtLeast(
            baseData->getFirstPrimaryForGroup(USCRIPT_HAN));
        break;
    case CollationRuleParser::FIRST_IMPLICIT:
        ce = baseData->getSingleCE(0x4e00, errorCode);
        break;
    case CollationRuleParser::LAST_IMPLICIT:
        // We do not support tailoring to an unassigned-implicit CE.
        errorCode = U_UNSUPPORTED_ERROR;
        parserErrorReason = "reset to [last implicit] not supported";
        return 0;
    case CollationRuleParser::FIRST_TRAILING:
        ce = Collation::makeCE(Collation::FIRST_TRAILING_PRIMARY);
        isBoundary = true;  // trailing first primary (there is no mapping for it)
        break;
    case CollationRuleParser::LAST_TRAILING:
        errorCode = U_ILLEGAL_ARGUMENT_ERROR;
        parserErrorReason = "LDML forbids tailoring to U+FFFF";
        return 0;
    default:
        UPRV_UNREACHABLE_EXIT;
    }

    int32_t index = findOrInsertNodeForRootCE(ce, strength, errorCode);
    if(U_FAILURE(errorCode)) { return 0; }
    int64_t node = nodes.elementAti(index);
    if((pos & 1) == 0) {
        // even pos = [first xyz]
        if(!nodeHasAnyBefore(node) && isBoundary) {
            // A <group> first primary boundary is artificially added to FractionalUCA.txt.
            // It is reachable via its special contraction, but is not normally used.
            // Find the first character tailored after the boundary CE,
            // or the first real root CE after it.
            if((index = nextIndexFromNode(node)) != 0) {
                // If there is a following node, then it must be tailored
                // because there are no root CEs with a boundary primary
                // and non-common secondary/tertiary weights.
                node = nodes.elementAti(index);
                U_ASSERT(isTailoredNode(node));
                ce = tempCEFromIndexAndStrength(index, strength);
            } else {
                U_ASSERT(strength == UCOL_PRIMARY);
                uint32_t p = (uint32_t)(ce >> 32);
                int32_t pIndex = rootElements.findPrimary(p);
                UBool isCompressible = baseData->isCompressiblePrimary(p);
                p = rootElements.getPrimaryAfter(p, pIndex, isCompressible);
                ce = Collation::makeCE(p);
                index = findOrInsertNodeForRootCE(ce, UCOL_PRIMARY, errorCode);
                if(U_FAILURE(errorCode)) { return 0; }
                node = nodes.elementAti(index);
            }
        }
        if(nodeHasAnyBefore(node)) {
            // Get the first node that was tailored before this one at a weaker strength.
            if(nodeHasBefore2(node)) {
                index = nextIndexFromNode(nodes.elementAti(nextIndexFromNode(node)));
                node = nodes.elementAti(index);
            }
            if(nodeHasBefore3(node)) {
                index = nextIndexFromNode(nodes.elementAti(nextIndexFromNode(node)));
            }
            U_ASSERT(isTailoredNode(nodes.elementAti(index)));
            ce = tempCEFromIndexAndStrength(index, strength);
        }
    } else {
        // odd pos = [last xyz]
        // Find the last node that was tailored after the [last xyz]
        // at a strength no greater than the position's strength.
        for(;;) {
            int32_t nextIndex = nextIndexFromNode(node);
            if(nextIndex == 0) { break; }
            int64_t nextNode = nodes.elementAti(nextIndex);
            if(strengthFromNode(nextNode) < strength) { break; }
            index = nextIndex;
            node = nextNode;
        }
        // Do not make a temporary CE for a root node.
        // This last node might be the node for the root CE itself,
        // or a node with a common secondary or tertiary weight.
        if(isTailoredNode(node)) {
            ce = tempCEFromIndexAndStrength(index, strength);
        }
    }
    return ce;
}

void
CollationBuilder::addRelation(int32_t strength, const UnicodeString &prefix,
                              const UnicodeString &str, const UnicodeString &extension,
                              const char *&parserErrorReason, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }
    UnicodeString nfdPrefix;
    if(!prefix.isEmpty()) {
        nfd.normalize(prefix, nfdPrefix, errorCode);
        if(U_FAILURE(errorCode)) {
            parserErrorReason = "normalizing the relation prefix";
            return;
        }
    }
    UnicodeString nfdString = nfd.normalize(str, errorCode);
    if(U_FAILURE(errorCode)) {
        parserErrorReason = "normalizing the relation string";
        return;
    }

    // The runtime code decomposes Hangul syllables on the fly,
    // with recursive processing but without making the Jamo pieces visible for matching.
    // It does not work with certain types of contextual mappings.
    int32_t nfdLength = nfdString.length();
    if(nfdLength >= 2) {
        char16_t c = nfdString.charAt(0);
        if(Hangul::isJamoL(c) || Hangul::isJamoV(c)) {
            // While handling a Hangul syllable, contractions starting with Jamo L or V
            // would not see the following Jamo of that syllable.
            errorCode = U_UNSUPPORTED_ERROR;
            parserErrorReason = "contractions starting with conjoining Jamo L or V not supported";
            return;
        }
        c = nfdString.charAt(nfdLength - 1);
        if(Hangul::isJamoL(c) ||
                (Hangul::isJamoV(c) && Hangul::isJamoL(nfdString.charAt(nfdLength - 2)))) {
            // A contraction ending with Jamo L or L+V would require
            // generating Hangul syllables in addTailComposites() (588 for a Jamo L),
            // or decomposing a following Hangul syllable on the fly, during contraction matching.
            errorCode = U_UNSUPPORTED_ERROR;
            parserErrorReason = "contractions ending with conjoining Jamo L or L+V not supported";
            return;
        }
        // A Hangul syllable completely inside a contraction is ok.
    }
    // Note: If there is a prefix, then the parser checked that
    // both the prefix and the string begin with NFC boundaries (not Jamo V or T).
    // Therefore: prefix.isEmpty() || !isJamoVOrT(nfdString.charAt(0))
    // (While handling a Hangul syllable, prefixes on Jamo V or T
    // would not see the previous Jamo of that syllable.)

    if(strength != UCOL_IDENTICAL) {
        // Find the node index after which we insert the new tailored node.
        int32_t index = findOrInsertNodeForCEs(strength, parserErrorReason, errorCode);
        U_ASSERT(cesLength > 0);
        int64_t ce = ces[cesLength - 1];
        if(strength == UCOL_PRIMARY && !isTempCE(ce) && (uint32_t)(ce >> 32) == 0) {
            // There is no primary gap between ignorables and the space-first-primary.
            errorCode = U_UNSUPPORTED_ERROR;
            parserErrorReason = "tailoring primary after ignorables not supported";
            return;
        }
        if(strength == UCOL_QUATERNARY && ce == 0) {
            // The CE data structure does not support non-zero quaternary weights
            // on tertiary ignorables.
            errorCode = U_UNSUPPORTED_ERROR;
            parserErrorReason = "tailoring quaternary after tertiary ignorables not supported";
            return;
        }
        // Insert the new tailored node.
        index = insertTailoredNodeAfter(index, strength, errorCode);
        if(U_FAILURE(errorCode)) {
            parserErrorReason = "modifying collation elements";
            return;
        }
        // Strength of the temporary CE:
        // The new relation may yield a stronger CE but not a weaker one.
        int32_t tempStrength = ceStrength(ce);
        if(strength < tempStrength) { tempStrength = strength; }
        ces[cesLength - 1] = tempCEFromIndexAndStrength(index, tempStrength);
    }

    setCaseBits(nfdString, parserErrorReason, errorCode);
    if(U_FAILURE(errorCode)) { return; }

    int32_t cesLengthBeforeExtension = cesLength;
    if(!extension.isEmpty()) {
        UnicodeString nfdExtension = nfd.normalize(extension, errorCode);
        if(U_FAILURE(errorCode)) {
            parserErrorReason = "normalizing the relation extension";
            return;
        }
        cesLength = dataBuilder->getCEs(nfdExtension, ces, cesLength);
        if(cesLength > Collation::MAX_EXPANSION_LENGTH) {
            errorCode = U_ILLEGAL_ARGUMENT_ERROR;
            parserErrorReason =
                "extension string adds too many collation elements (more than 31 total)";
            return;
        }
    }
    uint32_t ce32 = Collation::UNASSIGNED_CE32;
    if(!icu4xMode && (prefix != nfdPrefix || str != nfdString) &&
            !ignorePrefix(prefix, errorCode) && !ignoreString(str, errorCode)) {
        // Map from the original input to the CEs.
        // We do this in case the canonical closure is incomplete,
        // so that it is possible to explicitly provide the missing mappings.
        ce32 = addIfDifferent(prefix, str, ces, cesLength, ce32, errorCode);
    }
    if (!icu4xMode) {
        addWithClosure(nfdPrefix, nfdString, ces, cesLength, ce32, errorCode);
    } else {
        addIfDifferent(nfdPrefix, nfdString, ces, cesLength, ce32, errorCode);
    }
    if(U_FAILURE(errorCode)) {
        parserErrorReason = "writing collation elements";
        return;
    }
    cesLength = cesLengthBeforeExtension;
}

int32_t
CollationBuilder::findOrInsertNodeForCEs(int32_t strength, const char *&parserErrorReason,
                                         UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return 0; }
    U_ASSERT(UCOL_PRIMARY <= strength && strength <= UCOL_QUATERNARY);

    // Find the last CE that is at least as "strong" as the requested difference.
    // Note: Stronger is smaller (UCOL_PRIMARY=0).
    int64_t ce;
    for(;; --cesLength) {
        if(cesLength == 0) {
            ce = ces[0] = 0;
            cesLength = 1;
            break;
        } else {
            ce = ces[cesLength - 1];
        }
        if(ceStrength(ce) <= strength) { break; }
    }

    if(isTempCE(ce)) {
        // No need to findCommonNode() here for lower levels
        // because insertTailoredNodeAfter() will do that anyway.
        return indexFromTempCE(ce);
    }

    // root CE
    if((uint8_t)(ce >> 56) == Collation::UNASSIGNED_IMPLICIT_BYTE) {
        errorCode = U_UNSUPPORTED_ERROR;
        parserErrorReason = "tailoring relative to an unassigned code point not supported";
        return 0;
    }
    return findOrInsertNodeForRootCE(ce, strength, errorCode);
}

int32_t
CollationBuilder::findOrInsertNodeForRootCE(int64_t ce, int32_t strength, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return 0; }
    U_ASSERT((uint8_t)(ce >> 56) != Collation::UNASSIGNED_IMPLICIT_BYTE);

    // Find or insert the node for each of the root CE's weights,
    // down to the requested level/strength.
    // Root CEs must have common=zero quaternary weights (for which we never insert any nodes).
    U_ASSERT((ce & 0xc0) == 0);
    int32_t index = findOrInsertNodeForPrimary((uint32_t)(ce >> 32), errorCode);
    if(strength >= UCOL_SECONDARY) {
        uint32_t lower32 = (uint32_t)ce;
        index = findOrInsertWeakNode(index, lower32 >> 16, UCOL_SECONDARY, errorCode);
        if(strength >= UCOL_TERTIARY) {
            index = findOrInsertWeakNode(index, lower32 & Collation::ONLY_TERTIARY_MASK,
                                         UCOL_TERTIARY, errorCode);
        }
    }
    return index;
}

namespace {

/**
 * Like Java Collections.binarySearch(List, key, Comparator).
 *
 * @return the index>=0 where the item was found,
 *         or the index<0 for inserting the string at ~index in sorted order
 *         (index into rootPrimaryIndexes)
 */
int32_t
binarySearchForRootPrimaryNode(const int32_t *rootPrimaryIndexes, int32_t length,
                               const int64_t *nodes, uint32_t p) {
    if(length == 0) { return ~0; }
    int32_t start = 0;
    int32_t limit = length;
    for (;;) {
        int32_t i = (start + limit) / 2;
        int64_t node = nodes[rootPrimaryIndexes[i]];
        uint32_t nodePrimary = (uint32_t)(node >> 32);  // weight32FromNode(node)
        if (p == nodePrimary) {
            return i;
        } else if (p < nodePrimary) {
            if (i == start) {
                return ~start;  // insert s before i
            }
            limit = i;
        } else {
            if (i == start) {
                return ~(start + 1);  // insert s after i
            }
            start = i;
        }
    }
}

}  // namespace

int32_t
CollationBuilder::findOrInsertNodeForPrimary(uint32_t p, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return 0; }

    int32_t rootIndex = binarySearchForRootPrimaryNode(
        rootPrimaryIndexes.getBuffer(), rootPrimaryIndexes.size(), nodes.getBuffer(), p);
    if(rootIndex >= 0) {
        return rootPrimaryIndexes.elementAti(rootIndex);
    } else {
        // Start a new list of nodes with this primary.
        int32_t index = nodes.size();
        nodes.addElement(nodeFromWeight32(p), errorCode);
        rootPrimaryIndexes.insertElementAt(index, ~rootIndex, errorCode);
        return index;
    }
}

int32_t
CollationBuilder::findOrInsertWeakNode(int32_t index, uint32_t weight16, int32_t level, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return 0; }
    U_ASSERT(0 <= index && index < nodes.size());
    U_ASSERT(UCOL_SECONDARY <= level && level <= UCOL_TERTIARY);

    if(weight16 == Collation::COMMON_WEIGHT16) {
        return findCommonNode(index, level);
    }

    // If this will be the first below-common weight for the parent node,
    // then we will also need to insert a common weight after it.
    int64_t node = nodes.elementAti(index);
    U_ASSERT(strengthFromNode(node) < level);  // parent node is stronger
    if(weight16 != 0 && weight16 < Collation::COMMON_WEIGHT16) {
        int32_t hasThisLevelBefore = level == UCOL_SECONDARY ? HAS_BEFORE2 : HAS_BEFORE3;
        if((node & hasThisLevelBefore) == 0) {
            // The parent node has an implied level-common weight.
            int64_t commonNode =
                nodeFromWeight16(Collation::COMMON_WEIGHT16) | nodeFromStrength(level);
            if(level == UCOL_SECONDARY) {
                // Move the HAS_BEFORE3 flag from the parent node
                // to the new secondary common node.
                commonNode |= node & HAS_BEFORE3;
                node &= ~(int64_t)HAS_BEFORE3;
            }
            nodes.setElementAt(node | hasThisLevelBefore, index);
            // Insert below-common-weight node.
            int32_t nextIndex = nextIndexFromNode(node);
            node = nodeFromWeight16(weight16) | nodeFromStrength(level);
            index = insertNodeBetween(index, nextIndex, node, errorCode);
            // Insert common-weight node.
            insertNodeBetween(index, nextIndex, commonNode, errorCode);
            // Return index of below-common-weight node.
            return index;
        }
    }

    // Find the root CE's weight for this level.
    // Postpone insertion if not found:
    // Insert the new root node before the next stronger node,
    // or before the next root node with the same strength and a larger weight.
    int32_t nextIndex;
    while((nextIndex = nextIndexFromNode(node)) != 0) {
        node = nodes.elementAti(nextIndex);
        int32_t nextStrength = strengthFromNode(node);
        if(nextStrength <= level) {
            // Insert before a stronger node.
            if(nextStrength < level) { break; }
            // nextStrength == level
            if(!isTailoredNode(node)) {
                uint32_t nextWeight16 = weight16FromNode(node);
                if(nextWeight16 == weight16) {
                    // Found the node for the root CE up to this level.
                    return nextIndex;
                }
                // Insert before a node with a larger same-strength weight.
                if(nextWeight16 > weight16) { break; }
            }
        }
        // Skip the next node.
        index = nextIndex;
    }
    node = nodeFromWeight16(weight16) | nodeFromStrength(level);
    return insertNodeBetween(index, nextIndex, node, errorCode);
}

int32_t
CollationBuilder::insertTailoredNodeAfter(int32_t index, int32_t strength, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return 0; }
    U_ASSERT(0 <= index && index < nodes.size());
    if(strength >= UCOL_SECONDARY) {
        index = findCommonNode(index, UCOL_SECONDARY);
        if(strength >= UCOL_TERTIARY) {
            index = findCommonNode(index, UCOL_TERTIARY);
        }
    }
    // Postpone insertion:
    // Insert the new node before the next one with a strength at least as strong.
    int64_t node = nodes.elementAti(index);
    int32_t nextIndex;
    while((nextIndex = nextIndexFromNode(node)) != 0) {
        node = nodes.elementAti(nextIndex);
        if(strengthFromNode(node) <= strength) { break; }
        // Skip the next node which has a weaker (larger) strength than the new one.
        index = nextIndex;
    }
    node = IS_TAILORED | nodeFromStrength(strength);
    return insertNodeBetween(index, nextIndex, node, errorCode);
}

int32_t
CollationBuilder::insertNodeBetween(int32_t index, int32_t nextIndex, int64_t node,
                                    UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return 0; }
    U_ASSERT(previousIndexFromNode(node) == 0);
    U_ASSERT(nextIndexFromNode(node) == 0);
    U_ASSERT(nextIndexFromNode(nodes.elementAti(index)) == nextIndex);
    // Append the new node and link it to the existing nodes.
    int32_t newIndex = nodes.size();
    node |= nodeFromPreviousIndex(index) | nodeFromNextIndex(nextIndex);
    nodes.addElement(node, errorCode);
    if(U_FAILURE(errorCode)) { return 0; }
    // nodes[index].nextIndex = newIndex
    node = nodes.elementAti(index);
    nodes.setElementAt(changeNodeNextIndex(node, newIndex), index);
    // nodes[nextIndex].previousIndex = newIndex
    if(nextIndex != 0) {
        node = nodes.elementAti(nextIndex);
        nodes.setElementAt(changeNodePreviousIndex(node, newIndex), nextIndex);
    }
    return newIndex;
}

int32_t
CollationBuilder::findCommonNode(int32_t index, int32_t strength) const {
    U_ASSERT(UCOL_SECONDARY <= strength && strength <= UCOL_TERTIARY);
    int64_t node = nodes.elementAti(index);
    if(strengthFromNode(node) >= strength) {
        // The current node is no stronger.
        return index;
    }
    if(strength == UCOL_SECONDARY ? !nodeHasBefore2(node) : !nodeHasBefore3(node)) {
        // The current node implies the strength-common weight.
        return index;
    }
    index = nextIndexFromNode(node);
    node = nodes.elementAti(index);
    U_ASSERT(!isTailoredNode(node) && strengthFromNode(node) == strength &&
            weight16FromNode(node) < Collation::COMMON_WEIGHT16);
    // Skip to the explicit common node.
    do {
        index = nextIndexFromNode(node);
        node = nodes.elementAti(index);
        U_ASSERT(strengthFromNode(node) >= strength);
    } while(isTailoredNode(node) || strengthFromNode(node) > strength ||
            weight16FromNode(node) < Collation::COMMON_WEIGHT16);
    U_ASSERT(weight16FromNode(node) == Collation::COMMON_WEIGHT16);
    return index;
}

void
CollationBuilder::setCaseBits(const UnicodeString &nfdString,
                              const char *&parserErrorReason, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }
    int32_t numTailoredPrimaries = 0;
    for(int32_t i = 0; i < cesLength; ++i) {
        if(ceStrength(ces[i]) == UCOL_PRIMARY) { ++numTailoredPrimaries; }
    }
    // We should not be able to get too many case bits because
    // cesLength<=31==MAX_EXPANSION_LENGTH.
    // 31 pairs of case bits fit into an int64_t without setting its sign bit.
    U_ASSERT(numTailoredPrimaries <= 31);

    int64_t cases = 0;
    if(numTailoredPrimaries > 0) {
        const char16_t *s = nfdString.getBuffer();
        UTF16CollationIterator baseCEs(baseData, false, s, s, s + nfdString.length());
        int32_t baseCEsLength = baseCEs.fetchCEs(errorCode) - 1;
        if(U_FAILURE(errorCode)) {
            parserErrorReason = "fetching root CEs for tailored string";
            return;
        }
        U_ASSERT(baseCEsLength >= 0 && baseCEs.getCE(baseCEsLength) == Collation::NO_CE);

        uint32_t lastCase = 0;
        int32_t numBasePrimaries = 0;
        for(int32_t i = 0; i < baseCEsLength; ++i) {
            int64_t ce = baseCEs.getCE(i);
            if((ce >> 32) != 0) {
                ++numBasePrimaries;
                uint32_t c = ((uint32_t)ce >> 14) & 3;
                U_ASSERT(c == 0 || c == 2);  // lowercase or uppercase, no mixed case in any base CE
                if(numBasePrimaries < numTailoredPrimaries) {
                    cases |= (int64_t)c << ((numBasePrimaries - 1) * 2);
                } else if(numBasePrimaries == numTailoredPrimaries) {
                    lastCase = c;
                } else if(c != lastCase) {
                    // There are more base primary CEs than tailored primaries.
                    // Set mixed case if the case bits of the remainder differ.
                    lastCase = 1;
                    // Nothing more can change.
                    break;
                }
            }
        }
        if(numBasePrimaries >= numTailoredPrimaries) {
            cases |= (int64_t)lastCase << ((numTailoredPrimaries - 1) * 2);
        }
    }

    for(int32_t i = 0; i < cesLength; ++i) {
        int64_t ce = ces[i] & INT64_C(0xffffffffffff3fff);  // clear old case bits
        int32_t strength = ceStrength(ce);
        if(strength == UCOL_PRIMARY) {
            ce |= (cases & 3) << 14;
            cases >>= 2;
        } else if(strength == UCOL_TERTIARY) {
            // Tertiary CEs must have uppercase bits.
            // See the LDML spec, and comments in class CollationCompare.
            ce |= 0x8000;
        }
        // Tertiary ignorable CEs must have 0 case bits.
        // We set 0 case bits for secondary CEs too
        // since currently only U+0345 is cased and maps to a secondary CE,
        // and it is lowercase. Other secondaries are uncased.
        // See [[:Cased:]&[:uca1=:]] where uca1 queries the root primary weight.
        ces[i] = ce;
    }
}

void
CollationBuilder::suppressContractions(const UnicodeSet &set, const char *&parserErrorReason,
                                       UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }
    dataBuilder->suppressContractions(set, errorCode);
    if(U_FAILURE(errorCode)) {
        parserErrorReason = "application of [suppressContractions [set]] failed";
    }
}

void
CollationBuilder::optimize(const UnicodeSet &set, const char *& /* parserErrorReason */,
                           UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }
    optimizeSet.addAll(set);
}

uint32_t
CollationBuilder::addWithClosure(const UnicodeString &nfdPrefix, const UnicodeString &nfdString,
                                 const int64_t newCEs[], int32_t newCEsLength, uint32_t ce32,
                                 UErrorCode &errorCode) {
    // Map from the NFD input to the CEs.
    ce32 = addIfDifferent(nfdPrefix, nfdString, newCEs, newCEsLength, ce32, errorCode);
    ce32 = addOnlyClosure(nfdPrefix, nfdString, newCEs, newCEsLength, ce32, errorCode);
    addTailComposites(nfdPrefix, nfdString, errorCode);
    return ce32;
}

// ICU-22517
// This constant defines a limit for the addOnlyClosure to return
// error, to avoid taking a long time for canonical closure expansion.
// Please let us know if you have a reasonable use case that needed
// for a practical Collation rule that needs to increase this limit.
// This value is needed for compiling a rule with eight Hangul syllables such as
// "&a=b쫊쫊쫊쫊쫊쫊쫊" without error, which should be more than realistic
// usage.
static constexpr int32_t kClosureLoopLimit = 3000;

uint32_t
CollationBuilder::addOnlyClosure(const UnicodeString &nfdPrefix, const UnicodeString &nfdString,
                                 const int64_t newCEs[], int32_t newCEsLength, uint32_t ce32,
                                 UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return ce32; }

    int32_t loop = 0;
    // Map from canonically equivalent input to the CEs. (But not from the all-NFD input.)
    if(nfdPrefix.isEmpty()) {
        CanonicalIterator stringIter(nfdString, errorCode);
        if(U_FAILURE(errorCode)) { return ce32; }
        UnicodeString prefix;
        for(;;) {
            UnicodeString str = stringIter.next();
            if(str.isBogus()) { break; }
            if(ignoreString(str, errorCode) || str == nfdString) { continue; }
            if (loop++ > kClosureLoopLimit) {
                // To avoid hang as in ICU-22517, return with error.
                errorCode = U_INPUT_TOO_LONG_ERROR;
                return ce32;
            }
            ce32 = addIfDifferent(prefix, str, newCEs, newCEsLength, ce32, errorCode);
            if(U_FAILURE(errorCode)) { return ce32; }
        }
    } else {
        CanonicalIterator prefixIter(nfdPrefix, errorCode);
        CanonicalIterator stringIter(nfdString, errorCode);
        if(U_FAILURE(errorCode)) { return ce32; }
        for(;;) {
            UnicodeString prefix = prefixIter.next();
            if(prefix.isBogus()) { break; }
            if(ignorePrefix(prefix, errorCode)) { continue; }
            UBool samePrefix = prefix == nfdPrefix;
            for(;;) {
                UnicodeString str = stringIter.next();
                if(str.isBogus()) { break; }
                if(ignoreString(str, errorCode) || (samePrefix && str == nfdString)) { continue; }
                if (loop++ > kClosureLoopLimit) {
                    // To avoid hang as in ICU-22517, return with error.
                    errorCode = U_INPUT_TOO_LONG_ERROR;
                    return ce32;
                }
                ce32 = addIfDifferent(prefix, str, newCEs, newCEsLength, ce32, errorCode);
                if(U_FAILURE(errorCode)) { return ce32; }
            }
            stringIter.reset();
        }
    }
    return ce32;
}

void
CollationBuilder::addTailComposites(const UnicodeString &nfdPrefix, const UnicodeString &nfdString,
                                    UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }

    // Look for the last starter in the NFD string.
    UChar32 lastStarter;
    int32_t indexAfterLastStarter = nfdString.length();
    for(;;) {
        if(indexAfterLastStarter == 0) { return; }  // no starter at all
        lastStarter = nfdString.char32At(indexAfterLastStarter - 1);
        if(nfd.getCombiningClass(lastStarter) == 0) { break; }
        indexAfterLastStarter -= U16_LENGTH(lastStarter);
    }
    // No closure to Hangul syllables since we decompose them on the fly.
    if(Hangul::isJamoL(lastStarter)) { return; }

    // Are there any composites whose decomposition starts with the lastStarter?
    // Note: Normalizer2Impl does not currently return start sets for NFC_QC=Maybe characters.
    // We might find some more equivalent mappings here if it did.
    UnicodeSet composites;
    if(!nfcImpl.getCanonStartSet(lastStarter, composites)) { return; }

    UnicodeString decomp;
    UnicodeString newNFDString, newString;
    int64_t newCEs[Collation::MAX_EXPANSION_LENGTH];
    UnicodeSetIterator iter(composites);
    while(iter.next()) {
        U_ASSERT(!iter.isString());
        UChar32 composite = iter.getCodepoint();
        nfd.getDecomposition(composite, decomp);
        if(!mergeCompositeIntoString(nfdString, indexAfterLastStarter, composite, decomp,
                                     newNFDString, newString, errorCode)) {
            continue;
        }
        int32_t newCEsLength = dataBuilder->getCEs(nfdPrefix, newNFDString, newCEs, 0);
        if(newCEsLength > Collation::MAX_EXPANSION_LENGTH) {
            // Ignore mappings that we cannot store.
            continue;
        }
        // Note: It is possible that the newCEs do not make use of the mapping
        // for which we are adding the tail composites, in which case we might be adding
        // unnecessary mappings.
        // For example, when we add tail composites for ae^ (^=combining circumflex),
        // UCA discontiguous-contraction matching does not find any matches
        // for ae_^ (_=any combining diacritic below) *unless* there is also
        // a contraction mapping for ae.
        // Thus, if there is no ae contraction, then the ae^ mapping is ignored
        // while fetching the newCEs for ae_^.
        // TODO: Try to detect this effectively.
        // (Alternatively, print a warning when prefix contractions are missing.)

        // We do not need an explicit mapping for the NFD strings.
        // It is fine if the NFD input collates like this via a sequence of mappings.
        // It also saves a little bit of space, and may reduce the set of characters with contractions.
        uint32_t ce32 = addIfDifferent(nfdPrefix, newString,
                                       newCEs, newCEsLength, Collation::UNASSIGNED_CE32, errorCode);
        if(ce32 != Collation::UNASSIGNED_CE32) {
            // was different, was added
            addOnlyClosure(nfdPrefix, newNFDString, newCEs, newCEsLength, ce32, errorCode);
        }
    }
}

UBool
CollationBuilder::mergeCompositeIntoString(const UnicodeString &nfdString,
                                           int32_t indexAfterLastStarter,
                                           UChar32 composite, const UnicodeString &decomp,
                                           UnicodeString &newNFDString, UnicodeString &newString,
                                           UErrorCode &errorCode) const {
    if(U_FAILURE(errorCode)) { return false; }
    U_ASSERT(nfdString.char32At(indexAfterLastStarter - 1) == decomp.char32At(0));
    int32_t lastStarterLength = decomp.moveIndex32(0, 1);
    if(lastStarterLength == decomp.length()) {
        // Singleton decompositions should be found by addWithClosure()
        // and the CanonicalIterator, so we can ignore them here.
        return false;
    }
    if(nfdString.compare(indexAfterLastStarter, 0x7fffffff,
                         decomp, lastStarterLength, 0x7fffffff) == 0) {
        // same strings, nothing new to be found here
        return false;
    }

    // Make new FCD strings that combine a composite, or its decomposition,
    // into the nfdString's last starter and the combining marks following it.
    // Make an NFD version, and a version with the composite.
    newNFDString.setTo(nfdString, 0, indexAfterLastStarter);
    newString.setTo(nfdString, 0, indexAfterLastStarter - lastStarterLength).append(composite);

    // The following is related to discontiguous contraction matching,
    // but builds only FCD strings (or else returns false).
    int32_t sourceIndex = indexAfterLastStarter;
    int32_t decompIndex = lastStarterLength;
    // Small optimization: We keep the source character across loop iterations
    // because we do not always consume it,
    // and then need not fetch it again nor look up its combining class again.
    UChar32 sourceChar = U_SENTINEL;
    // The cc variables need to be declared before the loop so that at the end
    // they are set to the last combining classes seen.
    uint8_t sourceCC = 0;
    uint8_t decompCC = 0;
    for(;;) {
        if(sourceChar < 0) {
            if(sourceIndex >= nfdString.length()) { break; }
            sourceChar = nfdString.char32At(sourceIndex);
            sourceCC = nfd.getCombiningClass(sourceChar);
            U_ASSERT(sourceCC != 0);
        }
        // We consume a decomposition character in each iteration.
        if(decompIndex >= decomp.length()) { break; }
        UChar32 decompChar = decomp.char32At(decompIndex);
        decompCC = nfd.getCombiningClass(decompChar);
        // Compare the two characters and their combining classes.
        if(decompCC == 0) {
            // Unable to merge because the source contains a non-zero combining mark
            // but the composite's decomposition contains another starter.
            // The strings would not be equivalent.
            return false;
        } else if(sourceCC < decompCC) {
            // Composite + sourceChar would not be FCD.
            return false;
        } else if(decompCC < sourceCC) {
            newNFDString.append(decompChar);
            decompIndex += U16_LENGTH(decompChar);
        } else if(decompChar != sourceChar) {
            // Blocked because same combining class.
            return false;
        } else {  // match: decompChar == sourceChar
            newNFDString.append(decompChar);
            decompIndex += U16_LENGTH(decompChar);
            sourceIndex += U16_LENGTH(decompChar);
            sourceChar = U_SENTINEL;
        }
    }
    // We are at the end of at least one of the two inputs.
    if(sourceChar >= 0) {  // more characters from nfdString but not from decomp
        if(sourceCC < decompCC) {
            // Appending the next source character to the composite would not be FCD.
            return false;
        }
        newNFDString.append(nfdString, sourceIndex, 0x7fffffff);
        newString.append(nfdString, sourceIndex, 0x7fffffff);
    } else if(decompIndex < decomp.length()) {  // more characters from decomp, not from nfdString
        newNFDString.append(decomp, decompIndex, 0x7fffffff);
    }
    U_ASSERT(nfd.isNormalized(newNFDString, errorCode));
    U_ASSERT(fcd.isNormalized(newString, errorCode));
    U_ASSERT(nfd.normalize(newString, errorCode) == newNFDString);  // canonically equivalent
    return true;
}

UBool
CollationBuilder::ignorePrefix(const UnicodeString &s, UErrorCode &errorCode) const {
    // Do not map non-FCD prefixes.
    return !isFCD(s, errorCode);
}

UBool
CollationBuilder::ignoreString(const UnicodeString &s, UErrorCode &errorCode) const {
    // Do not map non-FCD strings.
    // Do not map strings that start with Hangul syllables: We decompose those on the fly.
    return !isFCD(s, errorCode) || Hangul::isHangul(s.charAt(0));
}

UBool
CollationBuilder::isFCD(const UnicodeString &s, UErrorCode &errorCode) const {
    return U_SUCCESS(errorCode) && fcd.isNormalized(s, errorCode);
}

void
CollationBuilder::closeOverComposites(UErrorCode &errorCode) {
    UnicodeSet composites(UNICODE_STRING_SIMPLE("[:NFD_QC=N:]"), errorCode);  // Java: static final
    if(U_FAILURE(errorCode)) { return; }
    // Hangul is decomposed on the fly during collation.
    composites.remove(Hangul::HANGUL_BASE, Hangul::HANGUL_END);
    UnicodeString prefix;  // empty
    UnicodeString nfdString;
    UnicodeSetIterator iter(composites);
    while(iter.next()) {
        U_ASSERT(!iter.isString());
        nfd.getDecomposition(iter.getCodepoint(), nfdString);
        cesLength = dataBuilder->getCEs(nfdString, ces, 0);
        if(cesLength > Collation::MAX_EXPANSION_LENGTH) {
            // Too many CEs from the decomposition (unusual), ignore this composite.
            // We could add a capacity parameter to getCEs() and reallocate if necessary.
            // However, this can only really happen in contrived cases.
            continue;
        }
        const UnicodeString &composite(iter.getString());
        addIfDifferent(prefix, composite, ces, cesLength, Collation::UNASSIGNED_CE32, errorCode);
    }
}

uint32_t
CollationBuilder::addIfDifferent(const UnicodeString &prefix, const UnicodeString &str,
                                 const int64_t newCEs[], int32_t newCEsLength, uint32_t ce32,
                                 UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return ce32; }
    int64_t oldCEs[Collation::MAX_EXPANSION_LENGTH];
    int32_t oldCEsLength = dataBuilder->getCEs(prefix, str, oldCEs, 0);
    if(!sameCEs(newCEs, newCEsLength, oldCEs, oldCEsLength)) {
        if(ce32 == Collation::UNASSIGNED_CE32) {
            ce32 = dataBuilder->encodeCEs(newCEs, newCEsLength, errorCode);
        }
        dataBuilder->addCE32(prefix, str, ce32, errorCode);
    }
    return ce32;
}

UBool
CollationBuilder::sameCEs(const int64_t ces1[], int32_t ces1Length,
                          const int64_t ces2[], int32_t ces2Length) {
    if(ces1Length != ces2Length) {
        return false;
    }
    U_ASSERT(ces1Length <= Collation::MAX_EXPANSION_LENGTH);
    for(int32_t i = 0; i < ces1Length; ++i) {
        if(ces1[i] != ces2[i]) { return false; }
    }
    return true;
}

#ifdef DEBUG_COLLATION_BUILDER

uint32_t
alignWeightRight(uint32_t w) {
    if(w != 0) {
        while((w & 0xff) == 0) { w >>= 8; }
    }
    return w;
}

#endif

void
CollationBuilder::makeTailoredCEs(UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }

    CollationWeights primaries, secondaries, tertiaries;
    int64_t *nodesArray = nodes.getBuffer();
#ifdef DEBUG_COLLATION_BUILDER
        puts("\nCollationBuilder::makeTailoredCEs()");
#endif

    for(int32_t rpi = 0; rpi < rootPrimaryIndexes.size(); ++rpi) {
        int32_t i = rootPrimaryIndexes.elementAti(rpi);
        int64_t node = nodesArray[i];
        uint32_t p = weight32FromNode(node);
        uint32_t s = p == 0 ? 0 : Collation::COMMON_WEIGHT16;
        uint32_t t = s;
        uint32_t q = 0;
        UBool pIsTailored = false;
        UBool sIsTailored = false;
        UBool tIsTailored = false;
#ifdef DEBUG_COLLATION_BUILDER
        printf("\nprimary     %lx\n", (long)alignWeightRight(p));
#endif
        int32_t pIndex = p == 0 ? 0 : rootElements.findPrimary(p);
        int32_t nextIndex = nextIndexFromNode(node);
        while(nextIndex != 0) {
            i = nextIndex;
            node = nodesArray[i];
            nextIndex = nextIndexFromNode(node);
            int32_t strength = strengthFromNode(node);
            if(strength == UCOL_QUATERNARY) {
                U_ASSERT(isTailoredNode(node));
#ifdef DEBUG_COLLATION_BUILDER
                printf("      quat+     ");
#endif
                if(q == 3) {
                    errorCode = U_BUFFER_OVERFLOW_ERROR;
                    errorReason = "quaternary tailoring gap too small";
                    return;
                }
                ++q;
            } else {
                if(strength == UCOL_TERTIARY) {
                    if(isTailoredNode(node)) {
#ifdef DEBUG_COLLATION_BUILDER
                        printf("    ter+        ");
#endif
                        if(!tIsTailored) {
                            // First tailored tertiary node for [p, s].
                            int32_t tCount = countTailoredNodes(nodesArray, nextIndex,
                                                                UCOL_TERTIARY) + 1;
                            uint32_t tLimit;
                            if(t == 0) {
                                // Gap at the beginning of the tertiary CE range.
                                t = rootElements.getTertiaryBoundary() - 0x100;
                                tLimit = rootElements.getFirstTertiaryCE() & Collation::ONLY_TERTIARY_MASK;
                            } else if(!pIsTailored && !sIsTailored) {
                                // p and s are root weights.
                                tLimit = rootElements.getTertiaryAfter(pIndex, s, t);
                            } else if(t == Collation::BEFORE_WEIGHT16) {
                                tLimit = Collation::COMMON_WEIGHT16;
                            } else {
                                // [p, s] is tailored.
                                U_ASSERT(t == Collation::COMMON_WEIGHT16);
                                tLimit = rootElements.getTertiaryBoundary();
                            }
                            U_ASSERT(tLimit == 0x4000 || (tLimit & ~Collation::ONLY_TERTIARY_MASK) == 0);
                            tertiaries.initForTertiary();
                            if(!tertiaries.allocWeights(t, tLimit, tCount)) {
                                errorCode = U_BUFFER_OVERFLOW_ERROR;
                                errorReason = "tertiary tailoring gap too small";
                                return;
                            }
                            tIsTailored = true;
                        }
                        t = tertiaries.nextWeight();
                        U_ASSERT(t != 0xffffffff);
                    } else {
                        t = weight16FromNode(node);
                        tIsTailored = false;
#ifdef DEBUG_COLLATION_BUILDER
                        printf("    ter     %lx\n", (long)alignWeightRight(t));
#endif
                    }
                } else {
                    if(strength == UCOL_SECONDARY) {
                        if(isTailoredNode(node)) {
#ifdef DEBUG_COLLATION_BUILDER
                            printf("  sec+          ");
#endif
                            if(!sIsTailored) {
                                // First tailored secondary node for p.
                                int32_t sCount = countTailoredNodes(nodesArray, nextIndex,
                                                                    UCOL_SECONDARY) + 1;
                                uint32_t sLimit;
                                if(s == 0) {
                                    // Gap at the beginning of the secondary CE range.
                                    s = rootElements.getSecondaryBoundary() - 0x100;
                                    sLimit = rootElements.getFirstSecondaryCE() >> 16;
                                } else if(!pIsTailored) {
                                    // p is a root primary.
                                    sLimit = rootElements.getSecondaryAfter(pIndex, s);
                                } else if(s == Collation::BEFORE_WEIGHT16) {
                                    sLimit = Collation::COMMON_WEIGHT16;
                                } else {
                                    // p is a tailored primary.
                                    U_ASSERT(s == Collation::COMMON_WEIGHT16);
                                    sLimit = rootElements.getSecondaryBoundary();
                                }
                                if(s == Collation::COMMON_WEIGHT16) {
                                    // Do not tailor into the getSortKey() range of
                                    // compressed common secondaries.
                                    s = rootElements.getLastCommonSecondary();
                                }
                                secondaries.initForSecondary();
                                if(!secondaries.allocWeights(s, sLimit, sCount)) {
                                    errorCode = U_BUFFER_OVERFLOW_ERROR;
                                    errorReason = "secondary tailoring gap too small";
#ifdef DEBUG_COLLATION_BUILDER
                                    printf("!secondaries.allocWeights(%lx, %lx, sCount=%ld)\n",
                                           (long)alignWeightRight(s), (long)alignWeightRight(sLimit),
                                           (long)alignWeightRight(sCount));
#endif
                                    return;
                                }
                                sIsTailored = true;
                            }
                            s = secondaries.nextWeight();
                            U_ASSERT(s != 0xffffffff);
                        } else {
                            s = weight16FromNode(node);
                            sIsTailored = false;
#ifdef DEBUG_COLLATION_BUILDER
                            printf("  sec       %lx\n", (long)alignWeightRight(s));
#endif
                        }
                    } else /* UCOL_PRIMARY */ {
                        U_ASSERT(isTailoredNode(node));
#ifdef DEBUG_COLLATION_BUILDER
                        printf("pri+            ");
#endif
                        if(!pIsTailored) {
                            // First tailored primary node in this list.
                            int32_t pCount = countTailoredNodes(nodesArray, nextIndex,
                                                                UCOL_PRIMARY) + 1;
                            UBool isCompressible = baseData->isCompressiblePrimary(p);
                            uint32_t pLimit =
                                rootElements.getPrimaryAfter(p, pIndex, isCompressible);
                            primaries.initForPrimary(isCompressible);
                            if(!primaries.allocWeights(p, pLimit, pCount)) {
                                errorCode = U_BUFFER_OVERFLOW_ERROR;  // TODO: introduce a more specific UErrorCode?
                                errorReason = "primary tailoring gap too small";
                                return;
                            }
                            pIsTailored = true;
                        }
                        p = primaries.nextWeight();
                        U_ASSERT(p != 0xffffffff);
                        s = Collation::COMMON_WEIGHT16;
                        sIsTailored = false;
                    }
                    t = s == 0 ? 0 : Collation::COMMON_WEIGHT16;
                    tIsTailored = false;
                }
                q = 0;
            }
            if(isTailoredNode(node)) {
                nodesArray[i] = Collation::makeCE(p, s, t, q);
#ifdef DEBUG_COLLATION_BUILDER
                printf("%016llx\n", (long long)nodesArray[i]);
#endif
            }
        }
    }
}

int32_t
CollationBuilder::countTailoredNodes(const int64_t *nodesArray, int32_t i, int32_t strength) {
    int32_t count = 0;
    for(;;) {
        if(i == 0) { break; }
        int64_t node = nodesArray[i];
        if(strengthFromNode(node) < strength) { break; }
        if(strengthFromNode(node) == strength) {
            if(isTailoredNode(node)) {
                ++count;
            } else {
                break;
            }
        }
        i = nextIndexFromNode(node);
    }
    return count;
}

class CEFinalizer : public CollationDataBuilder::CEModifier {
public:
    CEFinalizer(const int64_t *ces) : finalCEs(ces) {}
    virtual ~CEFinalizer();
    virtual int64_t modifyCE32(uint32_t ce32) const override {
        U_ASSERT(!Collation::isSpecialCE32(ce32));
        if(CollationBuilder::isTempCE32(ce32)) {
            // retain case bits
            return finalCEs[CollationBuilder::indexFromTempCE32(ce32)] | ((ce32 & 0xc0) << 8);
        } else {
            return Collation::NO_CE;
        }
    }
    virtual int64_t modifyCE(int64_t ce) const override {
        if(CollationBuilder::isTempCE(ce)) {
            // retain case bits
            return finalCEs[CollationBuilder::indexFromTempCE(ce)] | (ce & 0xc000);
        } else {
            return Collation::NO_CE;
        }
    }

private:
    const int64_t *finalCEs;
};

CEFinalizer::~CEFinalizer() {}

void
CollationBuilder::finalizeCEs(UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }
    LocalPointer<CollationDataBuilder> newBuilder(new CollationDataBuilder(icu4xMode, errorCode), errorCode);
    if(U_FAILURE(errorCode)) {
        return;
    }
    newBuilder->initForTailoring(baseData, errorCode);
    CEFinalizer finalizer(nodes.getBuffer());
    newBuilder->copyFrom(*dataBuilder, finalizer, errorCode);
    if(U_FAILURE(errorCode)) { return; }
    delete dataBuilder;
    dataBuilder = newBuilder.orphan();
}

int32_t
CollationBuilder::ceStrength(int64_t ce) {
    return
        isTempCE(ce) ? strengthFromTempCE(ce) :
        (ce & INT64_C(0xff00000000000000)) != 0 ? UCOL_PRIMARY :
        ((uint32_t)ce & 0xff000000) != 0 ? UCOL_SECONDARY :
        ce != 0 ? UCOL_TERTIARY :
        UCOL_IDENTICAL;
}

U_NAMESPACE_END

U_NAMESPACE_USE

U_CAPI UCollator * U_EXPORT2
ucol_openRules(const char16_t *rules, int32_t rulesLength,
               UColAttributeValue normalizationMode, UCollationStrength strength,
               UParseError *parseError, UErrorCode *pErrorCode) {
    if(U_FAILURE(*pErrorCode)) { return nullptr; }
    if(rules == nullptr && rulesLength != 0) {
        *pErrorCode = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }
    RuleBasedCollator *coll = new RuleBasedCollator();
    if(coll == nullptr) {
        *pErrorCode = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    UnicodeString r((UBool)(rulesLength < 0), rules, rulesLength);
    coll->internalBuildTailoring(r, strength, normalizationMode, parseError, nullptr, *pErrorCode);
    if(U_FAILURE(*pErrorCode)) {
        delete coll;
        return nullptr;
    }
    return coll->toUCollator();
}

static const int32_t internalBufferSize = 512;

// The @internal ucol_getUnsafeSet() was moved here from ucol_sit.cpp
// because it calls UnicodeSet "builder" code that depends on all Unicode properties,
// and the rest of the collation "runtime" code only depends on normalization.
// This function is not related to the collation builder,
// but it did not seem worth moving it into its own .cpp file,
// nor rewriting it to use lower-level UnicodeSet and Normalizer2Impl methods.
U_CAPI int32_t U_EXPORT2
ucol_getUnsafeSet( const UCollator *coll,
                  USet *unsafe,
                  UErrorCode *status)
{
    char16_t buffer[internalBufferSize];
    int32_t len = 0;

    uset_clear(unsafe);

    // cccpattern = "[[:^tccc=0:][:^lccc=0:]]", unfortunately variant
    static const char16_t cccpattern[25] = { 0x5b, 0x5b, 0x3a, 0x5e, 0x74, 0x63, 0x63, 0x63, 0x3d, 0x30, 0x3a, 0x5d,
                                    0x5b, 0x3a, 0x5e, 0x6c, 0x63, 0x63, 0x63, 0x3d, 0x30, 0x3a, 0x5d, 0x5d, 0x00 };

    // add chars that fail the fcd check
    uset_applyPattern(unsafe, cccpattern, 24, USET_IGNORE_SPACE, status);

    // add lead/trail surrogates
    // (trail surrogates should need to be unsafe only if the caller tests for UTF-16 code *units*,
    // not when testing code *points*)
    uset_addRange(unsafe, 0xd800, 0xdfff);

    USet *contractions = uset_open(0,0);

    int32_t i = 0, j = 0;
    ucol_getContractionsAndExpansions(coll, contractions, nullptr, false, status);
    int32_t contsSize = uset_size(contractions);
    UChar32 c = 0;
    // Contraction set consists only of strings
    // to get unsafe code points, we need to
    // break the strings apart and add them to the unsafe set
    for(i = 0; i < contsSize; i++) {
        len = uset_getItem(contractions, i, nullptr, nullptr, buffer, internalBufferSize, status);
        if(len > 0) {
            j = 0;
            while(j < len) {
                U16_NEXT(buffer, j, len, c);
                if(j < len) {
                    uset_add(unsafe, c);
                }
            }
        }
    }

    uset_close(contractions);

    return uset_size(unsafe);
}

#endif  // !UCONFIG_NO_COLLATION
