// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2013-2014, International Business Machines
* Corporation and others.  All Rights Reserved.
*******************************************************************************
* collationbuilder.h
*
* created on: 2013may06
* created by: Markus W. Scherer
*/

#ifndef __COLLATIONBUILDER_H__
#define __COLLATIONBUILDER_H__

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

#include "unicode/uniset.h"
#include "unicode/unistr.h"
#include "collationrootelements.h"
#include "collationruleparser.h"
#include "uvectr32.h"
#include "uvectr64.h"

struct UParseError;

U_NAMESPACE_BEGIN

struct CollationData;
struct CollationTailoring;

class CEFinalizer;
class CollationDataBuilder;
class Normalizer2;
class Normalizer2Impl;

class U_I18N_API CollationBuilder : public CollationRuleParser::Sink {
public:
    CollationBuilder(const CollationTailoring *b, UBool icu4xMode, UErrorCode &errorCode);
    CollationBuilder(const CollationTailoring *base, UErrorCode &errorCode);
    virtual ~CollationBuilder();

    void disableFastLatin() { fastLatinEnabled = false; }

    CollationTailoring *parseAndBuild(const UnicodeString &ruleString,
                                      const UVersionInfo rulesVersion,
                                      CollationRuleParser::Importer *importer,
                                      UParseError *outParseError,
                                      UErrorCode &errorCode);

    const char *getErrorReason() const { return errorReason; }

private:
    friend class CEFinalizer;

    /** Implements CollationRuleParser::Sink. */
    virtual void addReset(int32_t strength, const UnicodeString &str,
                          const char *&errorReason, UErrorCode &errorCode) override;
    /**
     * Returns the secondary or tertiary weight preceding the current node's weight.
     * node=nodes[index].
     */
    uint32_t getWeight16Before(int32_t index, int64_t node, int32_t level);

    int64_t getSpecialResetPosition(const UnicodeString &str,
                                    const char *&parserErrorReason, UErrorCode &errorCode);

    /** Implements CollationRuleParser::Sink. */
    virtual void addRelation(int32_t strength, const UnicodeString &prefix,
                             const UnicodeString &str, const UnicodeString &extension,
                             const char *&errorReason, UErrorCode &errorCode) override;

    /**
     * Picks one of the current CEs and finds or inserts a node in the graph
     * for the CE + strength.
     */
    int32_t findOrInsertNodeForCEs(int32_t strength, const char *&parserErrorReason,
                                   UErrorCode &errorCode);
    int32_t findOrInsertNodeForRootCE(int64_t ce, int32_t strength, UErrorCode &errorCode);
    /** Finds or inserts the node for a root CE's primary weight. */
    int32_t findOrInsertNodeForPrimary(uint32_t p, UErrorCode &errorCode);
    /** Finds or inserts the node for a secondary or tertiary weight. */
    int32_t findOrInsertWeakNode(int32_t index, uint32_t weight16, int32_t level,
                                 UErrorCode &errorCode);

    /**
     * Makes and inserts a new tailored node into the list, after the one at index.
     * Skips over nodes of weaker strength to maintain collation order
     * ("postpone insertion").
     * @return the new node's index
     */
    int32_t insertTailoredNodeAfter(int32_t index, int32_t strength, UErrorCode &errorCode);

    /**
     * Inserts a new node into the list, between list-adjacent items.
     * The node's previous and next indexes must not be set yet.
     * @return the new node's index
     */
    int32_t insertNodeBetween(int32_t index, int32_t nextIndex, int64_t node,
                              UErrorCode &errorCode);

    /**
     * Finds the node which implies or contains a common=05 weight of the given strength
     * (secondary or tertiary), if the current node is stronger.
     * Skips weaker nodes and tailored nodes if the current node is stronger
     * and is followed by an explicit-common-weight node.
     * Always returns the input index if that node is no stronger than the given strength.
     */
    int32_t findCommonNode(int32_t index, int32_t strength) const;

    void setCaseBits(const UnicodeString &nfdString,
                     const char *&parserErrorReason, UErrorCode &errorCode);

    /** Implements CollationRuleParser::Sink. */
    virtual void suppressContractions(const UnicodeSet &set, const char *&parserErrorReason,
                                      UErrorCode &errorCode) override;

    /** Implements CollationRuleParser::Sink. */
    virtual void optimize(const UnicodeSet &set, const char *&parserErrorReason,
                          UErrorCode &errorCode) override;

    /**
     * Adds the mapping and its canonical closure.
     * Takes ce32=dataBuilder->encodeCEs(...) so that the data builder
     * need not re-encode the CEs multiple times.
     */
    uint32_t addWithClosure(const UnicodeString &nfdPrefix, const UnicodeString &nfdString,
                            const int64_t newCEs[], int32_t newCEsLength, uint32_t ce32,
                            UErrorCode &errorCode);
    uint32_t addOnlyClosure(const UnicodeString &nfdPrefix, const UnicodeString &nfdString,
                            const int64_t newCEs[], int32_t newCEsLength, uint32_t ce32,
                            UErrorCode &errorCode);
    void addTailComposites(const UnicodeString &nfdPrefix, const UnicodeString &nfdString,
                           UErrorCode &errorCode);
    UBool mergeCompositeIntoString(const UnicodeString &nfdString, int32_t indexAfterLastStarter,
                                   UChar32 composite, const UnicodeString &decomp,
                                   UnicodeString &newNFDString, UnicodeString &newString,
                                   UErrorCode &errorCode) const;

    UBool ignorePrefix(const UnicodeString &s, UErrorCode &errorCode) const;
    UBool ignoreString(const UnicodeString &s, UErrorCode &errorCode) const;
    UBool isFCD(const UnicodeString &s, UErrorCode &errorCode) const;

    void closeOverComposites(UErrorCode &errorCode);

    uint32_t addIfDifferent(const UnicodeString &prefix, const UnicodeString &str,
                            const int64_t newCEs[], int32_t newCEsLength, uint32_t ce32,
                            UErrorCode &errorCode);
    static UBool sameCEs(const int64_t ces1[], int32_t ces1Length,
                         const int64_t ces2[], int32_t ces2Length);

    /**
     * Walks the tailoring graph and overwrites tailored nodes with new CEs.
     * After this, the graph is destroyed.
     * The nodes array can then be used only as a source of tailored CEs.
     */
    void makeTailoredCEs(UErrorCode &errorCode);
    /**
     * Counts the tailored nodes of the given strength up to the next node
     * which is either stronger or has an explicit weight of this strength.
     */
    static int32_t countTailoredNodes(const int64_t *nodesArray, int32_t i, int32_t strength);

    /** Replaces temporary CEs with the final CEs they point to. */
    void finalizeCEs(UErrorCode &errorCode);

    /**
     * Encodes "temporary CE" data into a CE that fits into the CE32 data structure,
     * with 2-byte primary, 1-byte secondary and 6-bit tertiary,
     * with valid CE byte values.
     *
     * The index must not exceed 20 bits (0xfffff).
     * The strength must fit into 2 bits (UCOL_PRIMARY..UCOL_QUATERNARY).
     *
     * Temporary CEs are distinguished from real CEs by their use of
     * secondary weights 06..45 which are otherwise reserved for compressed sort keys.
     *
     * The case bits are unused and available.
     */
    static inline int64_t tempCEFromIndexAndStrength(int32_t index, int32_t strength) {
        return
            // CE byte offsets, to ensure valid CE bytes, and case bits 11
            INT64_C(0x4040000006002000) +
            // index bits 19..13 -> primary byte 1 = CE bits 63..56 (byte values 40..BF)
            (static_cast<int64_t>(index & 0xfe000) << 43) +
            // index bits 12..6 -> primary byte 2 = CE bits 55..48 (byte values 40..BF)
            (static_cast<int64_t>(index & 0x1fc0) << 42) +
            // index bits 5..0 -> secondary byte 1 = CE bits 31..24 (byte values 06..45)
            ((index & 0x3f) << 24) +
            // strength bits 1..0 -> tertiary byte 1 = CE bits 13..8 (byte values 20..23)
            (strength << 8);
    }
    static inline int32_t indexFromTempCE(int64_t tempCE) {
        tempCE -= INT64_C(0x4040000006002000);
        return
            (static_cast<int32_t>(tempCE >> 43) & 0xfe000) |
            (static_cast<int32_t>(tempCE >> 42) & 0x1fc0) |
            (static_cast<int32_t>(tempCE >> 24) & 0x3f);
    }
    static inline int32_t strengthFromTempCE(int64_t tempCE) {
        return (static_cast<int32_t>(tempCE) >> 8) & 3;
    }
    static inline UBool isTempCE(int64_t ce) {
        uint32_t sec = static_cast<uint32_t>(ce) >> 24;
        return 6 <= sec && sec <= 0x45;
    }

    static inline int32_t indexFromTempCE32(uint32_t tempCE32) {
        tempCE32 -= 0x40400620;
        return
            (static_cast<int32_t>(tempCE32 >> 11) & 0xfe000) |
            (static_cast<int32_t>(tempCE32 >> 10) & 0x1fc0) |
            (static_cast<int32_t>(tempCE32 >> 8) & 0x3f);
    }
    static inline UBool isTempCE32(uint32_t ce32) {
        return
            (ce32 & 0xff) >= 2 &&  // not a long-primary/long-secondary CE32
            6 <= ((ce32 >> 8) & 0xff) && ((ce32 >> 8) & 0xff) <= 0x45;
    }

    static int32_t ceStrength(int64_t ce);

    /** At most 1M nodes, limited by the 20 bits in node bit fields. */
    static const int32_t MAX_INDEX = 0xfffff;
    /**
     * Node bit 6 is set on a primary node if there are nodes
     * with secondary values below the common secondary weight (05).
     */
    static const int32_t HAS_BEFORE2 = 0x40;
    /**
     * Node bit 5 is set on a primary or secondary node if there are nodes
     * with tertiary values below the common tertiary weight (05).
     */
    static const int32_t HAS_BEFORE3 = 0x20;
    /**
     * Node bit 3 distinguishes a tailored node, which has no weight value,
     * from a node with an explicit (root or default) weight.
     */
    static const int32_t IS_TAILORED = 8;

    static inline int64_t nodeFromWeight32(uint32_t weight32) {
        return static_cast<int64_t>(weight32) << 32;
    }
    static inline int64_t nodeFromWeight16(uint32_t weight16) {
        return static_cast<int64_t>(weight16) << 48;
    }
    static inline int64_t nodeFromPreviousIndex(int32_t previous) {
        return static_cast<int64_t>(previous) << 28;
    }
    static inline int64_t nodeFromNextIndex(int32_t next) {
        return next << 8;
    }
    static inline int64_t nodeFromStrength(int32_t strength) {
        return strength;
    }

    static inline uint32_t weight32FromNode(int64_t node) {
        return static_cast<uint32_t>(node >> 32);
    }
    static inline uint32_t weight16FromNode(int64_t node) {
        return static_cast<uint32_t>(node >> 48) & 0xffff;
    }
    static inline int32_t previousIndexFromNode(int64_t node) {
        return static_cast<int32_t>(node >> 28) & MAX_INDEX;
    }
    static inline int32_t nextIndexFromNode(int64_t node) {
        return (static_cast<int32_t>(node) >> 8) & MAX_INDEX;
    }
    static inline int32_t strengthFromNode(int64_t node) {
        return static_cast<int32_t>(node) & 3;
    }

    static inline UBool nodeHasBefore2(int64_t node) {
        return (node & HAS_BEFORE2) != 0;
    }
    static inline UBool nodeHasBefore3(int64_t node) {
        return (node & HAS_BEFORE3) != 0;
    }
    static inline UBool nodeHasAnyBefore(int64_t node) {
        return (node & (HAS_BEFORE2 | HAS_BEFORE3)) != 0;
    }
    static inline UBool isTailoredNode(int64_t node) {
        return (node & IS_TAILORED) != 0;
    }

    static inline int64_t changeNodePreviousIndex(int64_t node, int32_t previous) {
        return (node & INT64_C(0xffff00000fffffff)) | nodeFromPreviousIndex(previous);
    }
    static inline int64_t changeNodeNextIndex(int64_t node, int32_t next) {
        return (node & INT64_C(0xfffffffff00000ff)) | nodeFromNextIndex(next);
    }

    const Normalizer2 &nfd, &fcd;
    const Normalizer2Impl &nfcImpl;

    const CollationTailoring *base;
    const CollationData *baseData;
    const CollationRootElements rootElements;
    uint32_t variableTop;

    CollationDataBuilder *dataBuilder;
    UBool fastLatinEnabled;
    UBool icu4xMode;
    UnicodeSet optimizeSet;
    const char *errorReason;

    int64_t ces[Collation::MAX_EXPANSION_LENGTH];
    int32_t cesLength;

    /**
     * Indexes of nodes with root primary weights, sorted by primary.
     * Compact form of a TreeMap from root primary to node index.
     *
     * This is a performance optimization for finding reset positions.
     * Without this, we would have to search through the entire nodes list.
     * It also allows storing root primary weights in list head nodes,
     * without previous index, leaving room in root primary nodes for 32-bit primary weights.
     */
    UVector32 rootPrimaryIndexes;
    /**
     * Data structure for assigning tailored weights and CEs.
     * Doubly-linked lists of nodes in mostly collation order.
     * Each list starts with a root primary node and ends with a nextIndex of 0.
     *
     * When there are any nodes in the list, then there is always a root primary node at index 0.
     * This allows some code not to have to check explicitly for nextIndex==0.
     *
     * Root primary nodes have 32-bit weights but do not have previous indexes.
     * All other nodes have at most 16-bit weights and do have previous indexes.
     *
     * Nodes with explicit weights store root collator weights,
     * or default weak weights (e.g., secondary 05) for stronger nodes.
     * "Tailored" nodes, with the IS_TAILORED bit set,
     * do not store explicit weights but rather
     * create a difference of a certain strength from the preceding node.
     *
     * A root node is followed by either
     * - a root/default node of the same strength, or
     * - a root/default node of the next-weaker strength, or
     * - a tailored node of the same strength.
     *
     * A node of a given strength normally implies "common" weights on weaker levels.
     *
     * A node with HAS_BEFORE2 must be immediately followed by
     * a secondary node with an explicit below-common weight, then a secondary tailored node,
     * and later an explicit common-secondary node.
     * The below-common weight can be a root weight,
     * or it can be BEFORE_WEIGHT16 for tailoring before an implied common weight
     * or before the lowest root weight.
     * (&[before 2] resets to an explicit secondary node so that
     * the following addRelation(secondary) tailors right after that.
     * If we did not have this node and instead were to reset on the primary node,
     * then addRelation(secondary) would skip forward to the the COMMON_WEIGHT16 node.)
     *
     * If the flag is not set, then there are no explicit secondary nodes
     * with the common or lower weights.
     *
     * Same for HAS_BEFORE3 for tertiary nodes and weights.
     * A node must not have both flags set.
     *
     * Tailored CEs are initially represented in a CollationDataBuilder as temporary CEs
     * which point to stable indexes in this list,
     * and temporary CEs stored in a CollationDataBuilder only point to tailored nodes.
     *
     * A temporary CE in the ces[] array may point to a non-tailored reset-before-position node,
     * until the next relation is added.
     *
     * At the end, the tailored weights are allocated as necessary,
     * then the tailored nodes are replaced with final CEs,
     * and the CollationData is rewritten by replacing temporary CEs with final ones.
     *
     * We cannot simply insert new nodes in the middle of the array
     * because that would invalidate the indexes stored in existing temporary CEs.
     * We need to use a linked graph with stable indexes to existing nodes.
     * A doubly-linked list seems easiest to maintain.
     *
     * Each node is stored as an int64_t, with its fields stored as bit fields.
     *
     * Root primary node:
     * - primary weight: 32 bits 63..32
     * - reserved/unused/zero: 4 bits 31..28
     *
     * Weaker root nodes & tailored nodes:
     * - a weight: 16 bits 63..48
     *   + a root or default weight for a non-tailored node
     *   + unused/zero for a tailored node
     * - index to the previous node: 20 bits 47..28
     *
     * All types of nodes:
     * - index to the next node: 20 bits 27..8
     *   + nextIndex=0 in last node per root-primary list
     * - reserved/unused/zero bits: bits 7, 4, 2
     * - HAS_BEFORE2: bit 6
     * - HAS_BEFORE3: bit 5
     * - IS_TAILORED: bit 3
     * - the difference strength (primary/secondary/tertiary/quaternary): 2 bits 1..0
     *
     * We could allocate structs with pointers, but we would have to store them
     * in a pointer list so that they can be indexed from temporary CEs,
     * and they would require more memory allocations.
     */
    UVector64 nodes;
};

U_NAMESPACE_END

#endif  // !UCONFIG_NO_COLLATION
#endif  // __COLLATIONBUILDER_H__
