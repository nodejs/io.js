// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2013-2014, International Business Machines
* Corporation and others.  All Rights Reserved.
*******************************************************************************
* collationsets.cpp
*
* created on: 2013feb09
* created by: Markus W. Scherer
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

#include "unicode/ucharstrie.h"
#include "unicode/uniset.h"
#include "unicode/unistr.h"
#include "unicode/ustringtrie.h"
#include "collation.h"
#include "collationdata.h"
#include "collationsets.h"
#include "normalizer2impl.h"
#include "uassert.h"
#include "utf16collationiterator.h"
#include "utrie2.h"

U_NAMESPACE_BEGIN

U_CDECL_BEGIN

static UBool U_CALLCONV
enumTailoredRange(const void *context, UChar32 start, UChar32 end, uint32_t ce32) {
    if(ce32 == Collation::FALLBACK_CE32) {
        return true;  // fallback to base, not tailored
    }
    TailoredSet *ts = (TailoredSet *)context;
    return ts->handleCE32(start, end, ce32);
}

U_CDECL_END

void
TailoredSet::forData(const CollationData *d, UErrorCode &ec) {
    if(U_FAILURE(ec)) { return; }
    errorCode = ec;  // Preserve info & warning codes.
    data = d;
    baseData = d->base;
    U_ASSERT(baseData != nullptr);
    utrie2_enum(data->trie, nullptr, enumTailoredRange, this);
    ec = errorCode;
}

UBool
TailoredSet::handleCE32(UChar32 start, UChar32 end, uint32_t ce32) {
    U_ASSERT(ce32 != Collation::FALLBACK_CE32);
    if(Collation::isSpecialCE32(ce32)) {
        ce32 = data->getIndirectCE32(ce32);
        if(ce32 == Collation::FALLBACK_CE32) {
            return U_SUCCESS(errorCode);
        }
    }
    do {
        uint32_t baseCE32 = baseData->getFinalCE32(baseData->getCE32(start));
        // Do not just continue if ce32 == baseCE32 because
        // contractions and expansions in different data objects
        // normally differ even if they have the same data offsets.
        if(Collation::isSelfContainedCE32(ce32) && Collation::isSelfContainedCE32(baseCE32)) {
            // fastpath
            if(ce32 != baseCE32) {
                tailored->add(start);
            }
        } else {
            compare(start, ce32, baseCE32);
        }
    } while(++start <= end);
    return U_SUCCESS(errorCode);
}

void
TailoredSet::compare(UChar32 c, uint32_t ce32, uint32_t baseCE32) {
    if(Collation::isPrefixCE32(ce32)) {
        const char16_t *p = data->contexts + Collation::indexFromCE32(ce32);
        ce32 = data->getFinalCE32(CollationData::readCE32(p));
        if(Collation::isPrefixCE32(baseCE32)) {
            const char16_t *q = baseData->contexts + Collation::indexFromCE32(baseCE32);
            baseCE32 = baseData->getFinalCE32(CollationData::readCE32(q));
            comparePrefixes(c, p + 2, q + 2);
        } else {
            addPrefixes(data, c, p + 2);
        }
    } else if(Collation::isPrefixCE32(baseCE32)) {
        const char16_t *q = baseData->contexts + Collation::indexFromCE32(baseCE32);
        baseCE32 = baseData->getFinalCE32(CollationData::readCE32(q));
        addPrefixes(baseData, c, q + 2);
    }

    if(Collation::isContractionCE32(ce32)) {
        const char16_t *p = data->contexts + Collation::indexFromCE32(ce32);
        if((ce32 & Collation::CONTRACT_SINGLE_CP_NO_MATCH) != 0) {
            ce32 = Collation::NO_CE32;
        } else {
            ce32 = data->getFinalCE32(CollationData::readCE32(p));
        }
        if(Collation::isContractionCE32(baseCE32)) {
            const char16_t *q = baseData->contexts + Collation::indexFromCE32(baseCE32);
            if((baseCE32 & Collation::CONTRACT_SINGLE_CP_NO_MATCH) != 0) {
                baseCE32 = Collation::NO_CE32;
            } else {
                baseCE32 = baseData->getFinalCE32(CollationData::readCE32(q));
            }
            compareContractions(c, p + 2, q + 2);
        } else {
            addContractions(c, p + 2);
        }
    } else if(Collation::isContractionCE32(baseCE32)) {
        const char16_t *q = baseData->contexts + Collation::indexFromCE32(baseCE32);
        baseCE32 = baseData->getFinalCE32(CollationData::readCE32(q));
        addContractions(c, q + 2);
    }

    int32_t tag;
    if(Collation::isSpecialCE32(ce32)) {
        tag = Collation::tagFromCE32(ce32);
        U_ASSERT(tag != Collation::PREFIX_TAG);
        U_ASSERT(tag != Collation::CONTRACTION_TAG);
        // Currently, the tailoring data builder does not write offset tags.
        // They might be useful for saving space,
        // but they would complicate the builder,
        // and in tailorings we assume that performance of tailored characters is more important.
        U_ASSERT(tag != Collation::OFFSET_TAG);
    } else {
        tag = -1;
    }
    int32_t baseTag;
    if(Collation::isSpecialCE32(baseCE32)) {
        baseTag = Collation::tagFromCE32(baseCE32);
        U_ASSERT(baseTag != Collation::PREFIX_TAG);
        U_ASSERT(baseTag != Collation::CONTRACTION_TAG);
    } else {
        baseTag = -1;
    }

    // Non-contextual mappings, expansions, etc.
    if(baseTag == Collation::OFFSET_TAG) {
        // We might be comparing a tailoring CE which is a copy of
        // a base offset-tag CE, via the [optimize [set]] syntax
        // or when a single-character mapping was copied for tailored contractions.
        // Offset tags always result in long-primary CEs,
        // with common secondary/tertiary weights.
        if(!Collation::isLongPrimaryCE32(ce32)) {
            add(c);
            return;
        }
        int64_t dataCE = baseData->ces[Collation::indexFromCE32(baseCE32)];
        uint32_t p = Collation::getThreeBytePrimaryForOffsetData(c, dataCE);
        if(Collation::primaryFromLongPrimaryCE32(ce32) != p) {
            add(c);
            return;
        }
    }

    if(tag != baseTag) {
        add(c);
        return;
    }

    if(tag == Collation::EXPANSION32_TAG) {
        const uint32_t *ce32s = data->ce32s + Collation::indexFromCE32(ce32);
        int32_t length = Collation::lengthFromCE32(ce32);

        const uint32_t *baseCE32s = baseData->ce32s + Collation::indexFromCE32(baseCE32);
        int32_t baseLength = Collation::lengthFromCE32(baseCE32);

        if(length != baseLength) {
            add(c);
            return;
        }
        for(int32_t i = 0; i < length; ++i) {
            if(ce32s[i] != baseCE32s[i]) {
                add(c);
                break;
            }
        }
    } else if(tag == Collation::EXPANSION_TAG) {
        const int64_t *ces = data->ces + Collation::indexFromCE32(ce32);
        int32_t length = Collation::lengthFromCE32(ce32);

        const int64_t *baseCEs = baseData->ces + Collation::indexFromCE32(baseCE32);
        int32_t baseLength = Collation::lengthFromCE32(baseCE32);

        if(length != baseLength) {
            add(c);
            return;
        }
        for(int32_t i = 0; i < length; ++i) {
            if(ces[i] != baseCEs[i]) {
                add(c);
                break;
            }
        }
    } else if(tag == Collation::HANGUL_TAG) {
        char16_t jamos[3];
        int32_t length = Hangul::decompose(c, jamos);
        if(tailored->contains(jamos[0]) || tailored->contains(jamos[1]) ||
                (length == 3 && tailored->contains(jamos[2]))) {
            add(c);
        }
    } else if(ce32 != baseCE32) {
        add(c);
    }
}

void
TailoredSet::comparePrefixes(UChar32 c, const char16_t *p, const char16_t *q) {
    // Parallel iteration over prefixes of both tables.
    UCharsTrie::Iterator prefixes(p, 0, errorCode);
    UCharsTrie::Iterator basePrefixes(q, 0, errorCode);
    const UnicodeString *tp = nullptr;  // Tailoring prefix.
    const UnicodeString *bp = nullptr;  // Base prefix.
    // Use a string with a U+FFFF as the limit sentinel.
    // U+FFFF is untailorable and will not occur in prefixes.
    UnicodeString none(static_cast<char16_t>(0xffff));
    for(;;) {
        if(tp == nullptr) {
            if(prefixes.next(errorCode)) {
                tp = &prefixes.getString();
            } else {
                tp = &none;
            }
        }
        if(bp == nullptr) {
            if(basePrefixes.next(errorCode)) {
                bp = &basePrefixes.getString();
            } else {
                bp = &none;
            }
        }
        if(tp == &none && bp == &none) { break; }
        int32_t cmp = tp->compare(*bp);
        if(cmp < 0) {
            // tp occurs in the tailoring but not in the base.
            addPrefix(data, *tp, c, static_cast<uint32_t>(prefixes.getValue()));
            tp = nullptr;
        } else if(cmp > 0) {
            // bp occurs in the base but not in the tailoring.
            addPrefix(baseData, *bp, c, static_cast<uint32_t>(basePrefixes.getValue()));
            bp = nullptr;
        } else {
            setPrefix(*tp);
            compare(c, static_cast<uint32_t>(prefixes.getValue()), static_cast<uint32_t>(basePrefixes.getValue()));
            resetPrefix();
            tp = nullptr;
            bp = nullptr;
        }
    }
}

void
TailoredSet::compareContractions(UChar32 c, const char16_t *p, const char16_t *q) {
    // Parallel iteration over suffixes of both tables.
    UCharsTrie::Iterator suffixes(p, 0, errorCode);
    UCharsTrie::Iterator baseSuffixes(q, 0, errorCode);
    const UnicodeString *ts = nullptr;  // Tailoring suffix.
    const UnicodeString *bs = nullptr;  // Base suffix.
    // Use a string with two U+FFFF as the limit sentinel.
    // U+FFFF is untailorable and will not occur in contractions except maybe
    // as a single suffix character for a root-collator boundary contraction.
    UnicodeString none(static_cast<char16_t>(0xffff));
    none.append(static_cast<char16_t>(0xffff));
    for(;;) {
        if(ts == nullptr) {
            if(suffixes.next(errorCode)) {
                ts = &suffixes.getString();
            } else {
                ts = &none;
            }
        }
        if(bs == nullptr) {
            if(baseSuffixes.next(errorCode)) {
                bs = &baseSuffixes.getString();
            } else {
                bs = &none;
            }
        }
        if(ts == &none && bs == &none) { break; }
        int32_t cmp = ts->compare(*bs);
        if(cmp < 0) {
            // ts occurs in the tailoring but not in the base.
            addSuffix(c, *ts);
            ts = nullptr;
        } else if(cmp > 0) {
            // bs occurs in the base but not in the tailoring.
            addSuffix(c, *bs);
            bs = nullptr;
        } else {
            suffix = ts;
            compare(c, static_cast<uint32_t>(suffixes.getValue()), static_cast<uint32_t>(baseSuffixes.getValue()));
            suffix = nullptr;
            ts = nullptr;
            bs = nullptr;
        }
    }
}

void
TailoredSet::addPrefixes(const CollationData *d, UChar32 c, const char16_t *p) {
    UCharsTrie::Iterator prefixes(p, 0, errorCode);
    while(prefixes.next(errorCode)) {
        addPrefix(d, prefixes.getString(), c, static_cast<uint32_t>(prefixes.getValue()));
    }
}

void
TailoredSet::addPrefix(const CollationData *d, const UnicodeString &pfx, UChar32 c, uint32_t ce32) {
    setPrefix(pfx);
    ce32 = d->getFinalCE32(ce32);
    if(Collation::isContractionCE32(ce32)) {
        const char16_t *p = d->contexts + Collation::indexFromCE32(ce32);
        addContractions(c, p + 2);
    }
    tailored->add(UnicodeString(unreversedPrefix).append(c));
    resetPrefix();
}

void
TailoredSet::addContractions(UChar32 c, const char16_t *p) {
    UCharsTrie::Iterator suffixes(p, 0, errorCode);
    while(suffixes.next(errorCode)) {
        addSuffix(c, suffixes.getString());
    }
}

void
TailoredSet::addSuffix(UChar32 c, const UnicodeString &sfx) {
    tailored->add(UnicodeString(unreversedPrefix).append(c).append(sfx));
}

void
TailoredSet::add(UChar32 c) {
    if(unreversedPrefix.isEmpty() && suffix == nullptr) {
        tailored->add(c);
    } else {
        UnicodeString s(unreversedPrefix);
        s.append(c);
        if(suffix != nullptr) {
            s.append(*suffix);
        }
        tailored->add(s);
    }
}

ContractionsAndExpansions::CESink::~CESink() {}

U_CDECL_BEGIN

static UBool U_CALLCONV
enumCnERange(const void *context, UChar32 start, UChar32 end, uint32_t ce32) {
    ContractionsAndExpansions *cne = (ContractionsAndExpansions *)context;
    if(cne->checkTailored == 0) {
        // There is no tailoring.
        // No need to collect nor check the tailored set.
    } else if(cne->checkTailored < 0) {
        // Collect the set of code points with mappings in the tailoring data.
        if(ce32 == Collation::FALLBACK_CE32) {
            return true;  // fallback to base, not tailored
        } else {
            cne->tailored.add(start, end);
        }
        // checkTailored > 0: Exclude tailored ranges from the base data enumeration.
    } else if(start == end) {
        if(cne->tailored.contains(start)) {
            return true;
        }
    } else if(cne->tailored.containsSome(start, end)) {
        cne->ranges.set(start, end).removeAll(cne->tailored);
        int32_t count = cne->ranges.getRangeCount();
        for(int32_t i = 0; i < count; ++i) {
            cne->handleCE32(cne->ranges.getRangeStart(i), cne->ranges.getRangeEnd(i), ce32);
        }
        return U_SUCCESS(cne->errorCode);
    }
    cne->handleCE32(start, end, ce32);
    return U_SUCCESS(cne->errorCode);
}

U_CDECL_END

void
ContractionsAndExpansions::forData(const CollationData *d, UErrorCode &ec) {
    if(U_FAILURE(ec)) { return; }
    errorCode = ec;  // Preserve info & warning codes.
    // Add all from the data, can be tailoring or base.
    if(d->base != nullptr) {
        checkTailored = -1;
    }
    data = d;
    utrie2_enum(data->trie, nullptr, enumCnERange, this);
    if(d->base == nullptr || U_FAILURE(errorCode)) {
        ec = errorCode;
        return;
    }
    // Add all from the base data but only for un-tailored code points.
    tailored.freeze();
    checkTailored = 1;
    data = d->base;
    utrie2_enum(data->trie, nullptr, enumCnERange, this);
    ec = errorCode;
}

void
ContractionsAndExpansions::forCodePoint(const CollationData *d, UChar32 c, UErrorCode &ec) {
    if(U_FAILURE(ec)) { return; }
    errorCode = ec;  // Preserve info & warning codes.
    uint32_t ce32 = d->getCE32(c);
    if(ce32 == Collation::FALLBACK_CE32) {
        d = d->base;
        ce32 = d->getCE32(c);
    }
    data = d;
    handleCE32(c, c, ce32);
    ec = errorCode;
}

void
ContractionsAndExpansions::handleCE32(UChar32 start, UChar32 end, uint32_t ce32) {
    for(;;) {
        if((ce32 & 0xff) < Collation::SPECIAL_CE32_LOW_BYTE) {
            // !isSpecialCE32()
            if(sink != nullptr) {
                sink->handleCE(Collation::ceFromSimpleCE32(ce32));
            }
            return;
        }
        switch(Collation::tagFromCE32(ce32)) {
        case Collation::FALLBACK_TAG:
            return;
        case Collation::RESERVED_TAG_3:
        case Collation::BUILDER_DATA_TAG:
        case Collation::LEAD_SURROGATE_TAG:
            if(U_SUCCESS(errorCode)) { errorCode = U_INTERNAL_PROGRAM_ERROR; }
            return;
        case Collation::LONG_PRIMARY_TAG:
            if(sink != nullptr) {
                sink->handleCE(Collation::ceFromLongPrimaryCE32(ce32));
            }
            return;
        case Collation::LONG_SECONDARY_TAG:
            if(sink != nullptr) {
                sink->handleCE(Collation::ceFromLongSecondaryCE32(ce32));
            }
            return;
        case Collation::LATIN_EXPANSION_TAG:
            if(sink != nullptr) {
                ces[0] = Collation::latinCE0FromCE32(ce32);
                ces[1] = Collation::latinCE1FromCE32(ce32);
                sink->handleExpansion(ces, 2);
            }
            // Optimization: If we have a prefix,
            // then the relevant strings have been added already.
            if(unreversedPrefix.isEmpty()) {
                addExpansions(start, end);
            }
            return;
        case Collation::EXPANSION32_TAG:
            if(sink != nullptr) {
                const uint32_t *ce32s = data->ce32s + Collation::indexFromCE32(ce32);
                int32_t length = Collation::lengthFromCE32(ce32);
                for(int32_t i = 0; i < length; ++i) {
                    ces[i] = Collation::ceFromCE32(*ce32s++);
                }
                sink->handleExpansion(ces, length);
            }
            // Optimization: If we have a prefix,
            // then the relevant strings have been added already.
            if(unreversedPrefix.isEmpty()) {
                addExpansions(start, end);
            }
            return;
        case Collation::EXPANSION_TAG:
            if(sink != nullptr) {
                int32_t length = Collation::lengthFromCE32(ce32);
                sink->handleExpansion(data->ces + Collation::indexFromCE32(ce32), length);
            }
            // Optimization: If we have a prefix,
            // then the relevant strings have been added already.
            if(unreversedPrefix.isEmpty()) {
                addExpansions(start, end);
            }
            return;
        case Collation::PREFIX_TAG:
            handlePrefixes(start, end, ce32);
            return;
        case Collation::CONTRACTION_TAG:
            handleContractions(start, end, ce32);
            return;
        case Collation::DIGIT_TAG:
            // Fetch the non-numeric-collation CE32 and continue.
            ce32 = data->ce32s[Collation::indexFromCE32(ce32)];
            break;
        case Collation::U0000_TAG:
            U_ASSERT(start == 0 && end == 0);
            // Fetch the normal ce32 for U+0000 and continue.
            ce32 = data->ce32s[0];
            break;
        case Collation::HANGUL_TAG:
            if(sink != nullptr) {
                // TODO: This should be optimized,
                // especially if [start..end] is the complete Hangul range. (assert that)
                UTF16CollationIterator iter(data, false, nullptr, nullptr, nullptr);
                char16_t hangul[1] = { 0 };
                for(UChar32 c = start; c <= end; ++c) {
                    hangul[0] = static_cast<char16_t>(c);
                    iter.setText(hangul, hangul + 1);
                    int32_t length = iter.fetchCEs(errorCode);
                    if(U_FAILURE(errorCode)) { return; }
                    // Ignore the terminating non-CE.
                    U_ASSERT(length >= 2 && iter.getCE(length - 1) == Collation::NO_CE);
                    sink->handleExpansion(iter.getCEs(), length - 1);
                }
            }
            // Optimization: If we have a prefix,
            // then the relevant strings have been added already.
            if(unreversedPrefix.isEmpty()) {
                addExpansions(start, end);
            }
            return;
        case Collation::OFFSET_TAG:
            // Currently no need to send offset CEs to the sink.
            return;
        case Collation::IMPLICIT_TAG:
            // Currently no need to send implicit CEs to the sink.
            return;
        }
    }
}

void
ContractionsAndExpansions::handlePrefixes(
        UChar32 start, UChar32 end, uint32_t ce32) {
    const char16_t *p = data->contexts + Collation::indexFromCE32(ce32);
    ce32 = CollationData::readCE32(p);  // Default if no prefix match.
    handleCE32(start, end, ce32);
    if(!addPrefixes) { return; }
    UCharsTrie::Iterator prefixes(p + 2, 0, errorCode);
    while(prefixes.next(errorCode)) {
        setPrefix(prefixes.getString());
        // Prefix/pre-context mappings are special kinds of contractions
        // that always yield expansions.
        addStrings(start, end, contractions);
        addStrings(start, end, expansions);
        handleCE32(start, end, static_cast<uint32_t>(prefixes.getValue()));
    }
    resetPrefix();
}

void
ContractionsAndExpansions::handleContractions(
        UChar32 start, UChar32 end, uint32_t ce32) {
    const char16_t *p = data->contexts + Collation::indexFromCE32(ce32);
    if((ce32 & Collation::CONTRACT_SINGLE_CP_NO_MATCH) != 0) {
        // No match on the single code point.
        // We are underneath a prefix, and the default mapping is just
        // a fallback to the mappings for a shorter prefix.
        U_ASSERT(!unreversedPrefix.isEmpty());
    } else {
        ce32 = CollationData::readCE32(p);  // Default if no suffix match.
        U_ASSERT(!Collation::isContractionCE32(ce32));
        handleCE32(start, end, ce32);
    }
    UCharsTrie::Iterator suffixes(p + 2, 0, errorCode);
    while(suffixes.next(errorCode)) {
        suffix = &suffixes.getString();
        addStrings(start, end, contractions);
        if(!unreversedPrefix.isEmpty()) {
            addStrings(start, end, expansions);
        }
        handleCE32(start, end, static_cast<uint32_t>(suffixes.getValue()));
    }
    suffix = nullptr;
}

void
ContractionsAndExpansions::addExpansions(UChar32 start, UChar32 end) {
    if(unreversedPrefix.isEmpty() && suffix == nullptr) {
        if(expansions != nullptr) {
            expansions->add(start, end);
        }
    } else {
        addStrings(start, end, expansions);
    }
}

void
ContractionsAndExpansions::addStrings(UChar32 start, UChar32 end, UnicodeSet *set) {
    if(set == nullptr) { return; }
    UnicodeString s(unreversedPrefix);
    do {
        s.append(start);
        if(suffix != nullptr) {
            s.append(*suffix);
        }
        set->add(s);
        s.truncate(unreversedPrefix.length());
    } while(++start <= end);
}

U_NAMESPACE_END

#endif  // !UCONFIG_NO_COLLATION
