// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2009-2014, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

#include "unicode/alphaindex.h"
#include "unicode/coll.h"
#include "unicode/localpointer.h"
#include "unicode/normalizer2.h"
#include "unicode/tblcoll.h"
#include "unicode/uchar.h"
#include "unicode/ulocdata.h"
#include "unicode/uniset.h"
#include "unicode/uobject.h"
#include "unicode/usetiter.h"
#include "unicode/utf16.h"

#include "cmemory.h"
#include "cstring.h"
#include "uassert.h"
#include "uvector.h"
#include "uvectr64.h"

//#include <string>
//#include <iostream>

U_NAMESPACE_BEGIN

namespace {

/**
 * Prefix string for Chinese index buckets.
 * See http://unicode.org/repos/cldr/trunk/specs/ldml/tr35-collation.html#Collation_Indexes
 */
const char16_t BASE[1] = { 0xFDD0 };
const int32_t BASE_LENGTH = 1;

UBool isOneLabelBetterThanOther(const Normalizer2 &nfkdNormalizer,
                                const UnicodeString &one, const UnicodeString &other);

}  // namespace

static int32_t U_CALLCONV
collatorComparator(const void *context, const void *left, const void *right);

static int32_t U_CALLCONV
recordCompareFn(const void *context, const void *left, const void *right);

//  UVector<Record *> support function, delete a Record.
static void U_CALLCONV
alphaIndex_deleteRecord(void *obj) {
    delete static_cast<AlphabeticIndex::Record *>(obj);
}

namespace {

UnicodeString *ownedString(const UnicodeString &s, LocalPointer<UnicodeString> &owned,
                           UErrorCode &errorCode) {
    if (U_FAILURE(errorCode)) { return nullptr; }
    if (owned.isValid()) {
        return owned.orphan();
    }
    UnicodeString *p = new UnicodeString(s);
    if (p == nullptr) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
    }
    return p;
}

inline UnicodeString *getString(const UVector &list, int32_t i) {
    return static_cast<UnicodeString *>(list[i]);
}

inline AlphabeticIndex::Bucket *getBucket(const UVector &list, int32_t i) {
    return static_cast<AlphabeticIndex::Bucket *>(list[i]);
}

inline AlphabeticIndex::Record *getRecord(const UVector &list, int32_t i) {
    return static_cast<AlphabeticIndex::Record *>(list[i]);
}

/**
 * Like Java Collections.binarySearch(List, String, Comparator).
 *
 * @return the index>=0 where the item was found,
 *         or the index<0 for inserting the string at ~index in sorted order
 */
int32_t binarySearch(const UVector &list, const UnicodeString &s, const Collator &coll) {
    if (list.size() == 0) { return ~0; }
    int32_t start = 0;
    int32_t limit = list.size();
    for (;;) {
        int32_t i = (start + limit) / 2;
        const UnicodeString *si = static_cast<UnicodeString *>(list.elementAt(i));
        UErrorCode errorCode = U_ZERO_ERROR;
        UCollationResult cmp = coll.compare(s, *si, errorCode);
        if (cmp == UCOL_EQUAL) {
            return i;
        } else if (cmp < 0) {
            if (i == start) {
                return ~start;  // insert s before *si
            }
            limit = i;
        } else {
            if (i == start) {
                return ~(start + 1);  // insert s after *si
            }
            start = i;
        }
    }
}

}  // namespace

// The BucketList is not in the anonymous namespace because only Clang
// seems to support its use in other classes from there.
// However, we also don't need U_I18N_API because it is not used from outside the i18n library.
class BucketList : public UObject {
public:
    BucketList(UVector *bucketList, UVector *publicBucketList)
            : bucketList_(bucketList), immutableVisibleList_(publicBucketList) {
        int32_t displayIndex = 0;
        for (int32_t i = 0; i < publicBucketList->size(); ++i) {
            getBucket(*publicBucketList, i)->displayIndex_ = displayIndex++;
        }
    }

    // The virtual destructor must not be inline.
    // See ticket #8454 for details.
    virtual ~BucketList();

    int32_t getBucketCount() const {
        return immutableVisibleList_->size();
    }

    int32_t getBucketIndex(const UnicodeString &name, const Collator &collatorPrimaryOnly,
                           UErrorCode &errorCode) {
        // binary search
        int32_t start = 0;
        int32_t limit = bucketList_->size();
        while ((start + 1) < limit) {
            int32_t i = (start + limit) / 2;
            const AlphabeticIndex::Bucket *bucket = getBucket(*bucketList_, i);
            UCollationResult nameVsBucket =
                collatorPrimaryOnly.compare(name, bucket->lowerBoundary_, errorCode);
            if (nameVsBucket < 0) {
                limit = i;
            } else {
                start = i;
            }
        }
        const AlphabeticIndex::Bucket *bucket = getBucket(*bucketList_, start);
        if (bucket->displayBucket_ != nullptr) {
            bucket = bucket->displayBucket_;
        }
        return bucket->displayIndex_;
    }

    /** All of the buckets, visible and invisible. */
    UVector *bucketList_;
    /** Just the visible buckets. */
    UVector *immutableVisibleList_;
};

BucketList::~BucketList() {
    delete bucketList_;
    if (immutableVisibleList_ != bucketList_) {
        delete immutableVisibleList_;
    }
}

AlphabeticIndex::ImmutableIndex::~ImmutableIndex() {
    delete buckets_;
    delete collatorPrimaryOnly_;
}

int32_t
AlphabeticIndex::ImmutableIndex::getBucketCount() const {
    return buckets_->getBucketCount();
}

int32_t
AlphabeticIndex::ImmutableIndex::getBucketIndex(
        const UnicodeString &name, UErrorCode &errorCode) const {
    return buckets_->getBucketIndex(name, *collatorPrimaryOnly_, errorCode);
}

const AlphabeticIndex::Bucket *
AlphabeticIndex::ImmutableIndex::getBucket(int32_t index) const {
    if (0 <= index && index < buckets_->getBucketCount()) {
        return icu::getBucket(*buckets_->immutableVisibleList_, index);
    } else {
        return nullptr;
    }
}

AlphabeticIndex::AlphabeticIndex(const Locale &locale, UErrorCode &status)
        : inputList_(nullptr),
          labelsIterIndex_(-1), itemsIterIndex_(0), currentBucket_(nullptr),
          maxLabelCount_(99),
          initialLabels_(nullptr), firstCharsInScripts_(nullptr),
          collator_(nullptr), collatorPrimaryOnly_(nullptr),
          buckets_(nullptr) {
    init(&locale, status);
}


AlphabeticIndex::AlphabeticIndex(RuleBasedCollator *collator, UErrorCode &status)
        : inputList_(nullptr),
          labelsIterIndex_(-1), itemsIterIndex_(0), currentBucket_(nullptr),
          maxLabelCount_(99),
          initialLabels_(nullptr), firstCharsInScripts_(nullptr),
          collator_(collator), collatorPrimaryOnly_(nullptr),
          buckets_(nullptr) {
    init(nullptr, status);
}



AlphabeticIndex::~AlphabeticIndex() {
    delete collator_;
    delete collatorPrimaryOnly_;
    delete firstCharsInScripts_;
    delete buckets_;
    delete inputList_;
    delete initialLabels_;
}


AlphabeticIndex &AlphabeticIndex::addLabels(const UnicodeSet &additions, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return *this;
    }
    initialLabels_->addAll(additions);
    clearBuckets();
    return *this;
}


AlphabeticIndex &AlphabeticIndex::addLabels(const Locale &locale, UErrorCode &status) {
    addIndexExemplars(locale, status);
    clearBuckets();
    return *this;
}


AlphabeticIndex::ImmutableIndex *AlphabeticIndex::buildImmutableIndex(UErrorCode &errorCode) {
    if (U_FAILURE(errorCode)) { return nullptr; }
    // In C++, the ImmutableIndex must own its copy of the BucketList,
    // even if it contains no records, for proper memory management.
    // We could clone the buckets_ if they are not nullptr,
    // but that would be worth it only if this method is called multiple times,
    // or called after using the old-style bucket iterator API.
    LocalPointer<BucketList> immutableBucketList(createBucketList(errorCode));
    LocalPointer<RuleBasedCollator> coll(collatorPrimaryOnly_->clone());
    if (immutableBucketList.isNull() || coll.isNull()) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    ImmutableIndex *immIndex = new ImmutableIndex(immutableBucketList.getAlias(), coll.getAlias());
    if (immIndex == nullptr) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    // The ImmutableIndex adopted its parameter objects.
    immutableBucketList.orphan();
    coll.orphan();
    return immIndex;
}

int32_t AlphabeticIndex::getBucketCount(UErrorCode &status) {
    initBuckets(status);
    if (U_FAILURE(status)) {
        return 0;
    }
    return buckets_->getBucketCount();
}


int32_t AlphabeticIndex::getRecordCount(UErrorCode &status) {
    if (U_FAILURE(status) || inputList_ == nullptr) {
        return 0;
    }
    return inputList_->size();
}

void AlphabeticIndex::initLabels(UVector &indexCharacters, UErrorCode &errorCode) const {
    U_ASSERT(indexCharacters.hasDeleter());
    const Normalizer2 *nfkdNormalizer = Normalizer2::getNFKDInstance(errorCode);
    if (U_FAILURE(errorCode)) { return; }

    const UnicodeString &firstScriptBoundary = *getString(*firstCharsInScripts_, 0);
    const UnicodeString &overflowBoundary =
        *getString(*firstCharsInScripts_, firstCharsInScripts_->size() - 1);

    // We make a sorted array of elements.
    // Some of the input may be redundant.
    // That is, we might have c, ch, d, where "ch" sorts just like "c", "h".
    // We filter out those cases.
    UnicodeSetIterator iter(*initialLabels_);
    while (U_SUCCESS(errorCode) && iter.next()) {
        const UnicodeString *item = &iter.getString();
        LocalPointer<UnicodeString> ownedItem;
        UBool checkDistinct;
        int32_t itemLength = item->length();
        if (!item->hasMoreChar32Than(0, itemLength, 1)) {
            checkDistinct = false;
        } else if(item->charAt(itemLength - 1) == 0x2a &&  // '*'
                item->charAt(itemLength - 2) != 0x2a) {
            // Use a label if it is marked with one trailing star,
            // even if the label string sorts the same when all contractions are suppressed.
            ownedItem.adoptInstead(new UnicodeString(*item, 0, itemLength - 1));
            item = ownedItem.getAlias();
            if (item == nullptr) {
                errorCode = U_MEMORY_ALLOCATION_ERROR;
                return;
            }
            checkDistinct = false;
        } else {
            checkDistinct = true;
        }
        if (collatorPrimaryOnly_->compare(*item, firstScriptBoundary, errorCode) < 0) {
            // Ignore a primary-ignorable or non-alphabetic index character.
        } else if (collatorPrimaryOnly_->compare(*item, overflowBoundary, errorCode) >= 0) {
            // Ignore an index character that will land in the overflow bucket.
        } else if (checkDistinct &&
                collatorPrimaryOnly_->compare(*item, separated(*item), errorCode) == 0) {
            // Ignore a multi-code point index character that does not sort distinctly
            // from the sequence of its separate characters.
        } else {
            int32_t insertionPoint = binarySearch(indexCharacters, *item, *collatorPrimaryOnly_);
            if (insertionPoint < 0) {
                indexCharacters.insertElementAt(
                    ownedString(*item, ownedItem, errorCode), ~insertionPoint, errorCode);
            } else {
                const UnicodeString &itemAlreadyIn = *getString(indexCharacters, insertionPoint);
                if (isOneLabelBetterThanOther(*nfkdNormalizer, *item, itemAlreadyIn)) {
                    indexCharacters.setElementAt(
                        ownedString(*item, ownedItem, errorCode), insertionPoint);
                }
            }
        }
    }
    if (U_FAILURE(errorCode)) { return; }

    // if the result is still too large, cut down to maxLabelCount_ elements, by removing every nth element

    int32_t size = indexCharacters.size() - 1;
    if (size > maxLabelCount_) {
        int32_t count = 0;
        int32_t old = -1;
        for (int32_t i = 0; i < indexCharacters.size();) {
            ++count;
            int32_t bump = count * maxLabelCount_ / size;
            if (bump == old) {
                indexCharacters.removeElementAt(i);
            } else {
                old = bump;
                ++i;
            }
        }
    }
}

namespace {

const UnicodeString &fixLabel(const UnicodeString &current, UnicodeString &temp) {
    if (!current.startsWith(BASE, BASE_LENGTH)) {
        return current;
    }
    char16_t rest = current.charAt(BASE_LENGTH);
    if (0x2800 < rest && rest <= 0x28FF) { // stroke count
        int32_t count = rest-0x2800;
        temp.setTo(static_cast<char16_t>(0x30 + count % 10));
        if (count >= 10) {
            count /= 10;
            temp.insert(0, static_cast<char16_t>(0x30 + count % 10));
            if (count >= 10) {
                count /= 10;
                temp.insert(0, static_cast<char16_t>(0x30 + count));
            }
        }
        return temp.append(static_cast<char16_t>(0x5283));
    }
    return temp.setTo(current, BASE_LENGTH);
}

UBool hasMultiplePrimaryWeights(
        const RuleBasedCollator &coll, uint32_t variableTop,
        const UnicodeString &s, UVector64 &ces, UErrorCode &errorCode) {
    ces.removeAllElements();
    coll.internalGetCEs(s, ces, errorCode);
    if (U_FAILURE(errorCode)) { return false; }
    UBool seenPrimary = false;
    for (int32_t i = 0; i < ces.size(); ++i) {
        int64_t ce = ces.elementAti(i);
        uint32_t p = static_cast<uint32_t>(ce >> 32);
        if (p > variableTop) {
            // not primary ignorable
            if (seenPrimary) {
                return true;
            }
            seenPrimary = true;
        }
    }
    return false;
}

}  // namespace

BucketList *AlphabeticIndex::createBucketList(UErrorCode &errorCode) const {
    // Initialize indexCharacters.
    UVector indexCharacters(errorCode);
    indexCharacters.setDeleter(uprv_deleteUObject);
    initLabels(indexCharacters, errorCode);
    if (U_FAILURE(errorCode)) { return nullptr; }

    // Variables for hasMultiplePrimaryWeights().
    UVector64 ces(errorCode);
    uint32_t variableTop;
    if (collatorPrimaryOnly_->getAttribute(UCOL_ALTERNATE_HANDLING, errorCode) == UCOL_SHIFTED) {
        variableTop = collatorPrimaryOnly_->getVariableTop(errorCode);
    } else {
        variableTop = 0;
    }
    UBool hasInvisibleBuckets = false;

    // Helper arrays for Chinese Pinyin collation.
    Bucket *asciiBuckets[26] = {
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
    };
    Bucket *pinyinBuckets[26] = {
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
    };
    UBool hasPinyin = false;

    LocalPointer<UVector> bucketList(new UVector(errorCode), errorCode);
    if (U_FAILURE(errorCode)) {
        return nullptr;
    }
    bucketList->setDeleter(uprv_deleteUObject);

    // underflow bucket
    LocalPointer<Bucket> bucket(new Bucket(getUnderflowLabel(), emptyString_, U_ALPHAINDEX_UNDERFLOW), errorCode);
    if (U_FAILURE(errorCode)) {
        return nullptr;
    }
    bucketList->adoptElement(bucket.orphan(), errorCode);
    if (U_FAILURE(errorCode)) { return nullptr; }

    UnicodeString temp;

    // fix up the list, adding underflow, additions, overflow
    // Insert inflow labels as needed.
    int32_t scriptIndex = -1;
    const UnicodeString *scriptUpperBoundary = &emptyString_;
    for (int32_t i = 0; i < indexCharacters.size(); ++i) {
        UnicodeString &current = *getString(indexCharacters, i);
        if (collatorPrimaryOnly_->compare(current, *scriptUpperBoundary, errorCode) >= 0) {
            // We crossed the script boundary into a new script.
            const UnicodeString &inflowBoundary = *scriptUpperBoundary;
            UBool skippedScript = false;
            for (;;) {
                scriptUpperBoundary = getString(*firstCharsInScripts_, ++scriptIndex);
                if (collatorPrimaryOnly_->compare(current, *scriptUpperBoundary, errorCode) < 0) {
                    break;
                }
                skippedScript = true;
            }
            if (skippedScript && bucketList->size() > 1) {
                // We are skipping one or more scripts,
                // and we are not just getting out of the underflow label.
                bucket.adoptInsteadAndCheckErrorCode(
                    new Bucket(getInflowLabel(), inflowBoundary, U_ALPHAINDEX_INFLOW), errorCode);
                bucketList->adoptElement(bucket.orphan(), errorCode);
                if (U_FAILURE(errorCode)) { return nullptr; }
            }
        }
        // Add a bucket with the current label.
        bucket.adoptInsteadAndCheckErrorCode(
            new Bucket(fixLabel(current, temp), current, U_ALPHAINDEX_NORMAL), errorCode);
        bucketList->adoptElement(bucket.orphan(), errorCode);
        if (U_FAILURE(errorCode)) { return nullptr; }
        // Remember ASCII and Pinyin buckets for Pinyin redirects.
        char16_t c;
        if (current.length() == 1 && 0x41 <= (c = current.charAt(0)) && c <= 0x5A) {  // A-Z
            asciiBuckets[c - 0x41] = static_cast<Bucket*>(bucketList->lastElement());
        } else if (current.length() == BASE_LENGTH + 1 && current.startsWith(BASE, BASE_LENGTH) &&
                0x41 <= (c = current.charAt(BASE_LENGTH)) && c <= 0x5A) {
            pinyinBuckets[c - 0x41] = static_cast<Bucket*>(bucketList->lastElement());
            hasPinyin = true;
        }
        // Check for multiple primary weights.
        if (!current.startsWith(BASE, BASE_LENGTH) &&
                hasMultiplePrimaryWeights(*collatorPrimaryOnly_, variableTop, current,
                                          ces, errorCode) &&
                current.charAt(current.length() - 1) != 0xFFFF /* !current.endsWith("\uffff") */) {
            // "AE-ligature" or "Sch" etc.
            for (int32_t j = bucketList->size() - 2;; --j) {
                Bucket *singleBucket = getBucket(*bucketList, j);
                if (singleBucket->labelType_ != U_ALPHAINDEX_NORMAL) {
                    // There is no single-character bucket since the last
                    // underflow or inflow label.
                    break;
                }
                if (singleBucket->displayBucket_ == nullptr &&
                        !hasMultiplePrimaryWeights(*collatorPrimaryOnly_, variableTop,
                                                   singleBucket->lowerBoundary_,
                                                   ces, errorCode)) {
                    // Add an invisible bucket that redirects strings greater than the expansion
                    // to the previous single-character bucket.
                    // For example, after ... Q R S Sch we add Sch\uFFFF->S
                    // and after ... Q R S Sch Sch\uFFFF St we add St\uFFFF->S.
                    bucket.adoptInsteadAndCheckErrorCode(new Bucket(emptyString_,
                        UnicodeString(current).append(static_cast<char16_t>(0xFFFF)),
                        U_ALPHAINDEX_NORMAL),
                        errorCode);
                    if (U_FAILURE(errorCode)) {
                        return nullptr;
                    }
                    bucket->displayBucket_ = singleBucket;
                    bucketList->adoptElement(bucket.orphan(), errorCode);
                    if (U_FAILURE(errorCode)) { return nullptr; }
                    hasInvisibleBuckets = true;
                    break;
                }
            }
        }
    }
    if (U_FAILURE(errorCode)) { return nullptr; }
    if (bucketList->size() == 1) {
        // No real labels, show only the underflow label.
        BucketList *bl = new BucketList(bucketList.getAlias(), bucketList.getAlias());
        if (bl == nullptr) {
            errorCode = U_MEMORY_ALLOCATION_ERROR;
            return nullptr;
        }
        bucketList.orphan();
        return bl;
    }
    // overflow bucket
    bucket.adoptInsteadAndCheckErrorCode(
        new Bucket(getOverflowLabel(), *scriptUpperBoundary, U_ALPHAINDEX_OVERFLOW), errorCode);
    bucketList->adoptElement(bucket.orphan(), errorCode); // final
    if (U_FAILURE(errorCode)) { return nullptr; }

    if (hasPinyin) {
        // Redirect Pinyin buckets.
        Bucket *asciiBucket = nullptr;
        for (int32_t i = 0; i < 26; ++i) {
            if (asciiBuckets[i] != nullptr) {
                asciiBucket = asciiBuckets[i];
            }
            if (pinyinBuckets[i] != nullptr && asciiBucket != nullptr) {
                pinyinBuckets[i]->displayBucket_ = asciiBucket;
                hasInvisibleBuckets = true;
            }
        }
    }

    if (U_FAILURE(errorCode)) { return nullptr; }
    if (!hasInvisibleBuckets) {
        BucketList *bl = new BucketList(bucketList.getAlias(), bucketList.getAlias());
        if (bl == nullptr) {
            errorCode = U_MEMORY_ALLOCATION_ERROR;
            return nullptr;
        }
        bucketList.orphan();
        return bl;
    }
    // Merge inflow buckets that are visually adjacent.
    // Iterate backwards: Merge inflow into overflow rather than the other way around.
    int32_t i = bucketList->size() - 1;
    Bucket *nextBucket = getBucket(*bucketList, i);
    while (--i > 0) {
        Bucket *bucket = getBucket(*bucketList, i);
        if (bucket->displayBucket_ != nullptr) {
            continue;  // skip invisible buckets
        }
        if (bucket->labelType_ == U_ALPHAINDEX_INFLOW) {
            if (nextBucket->labelType_ != U_ALPHAINDEX_NORMAL) {
                bucket->displayBucket_ = nextBucket;
                continue;
            }
        }
        nextBucket = bucket;
    }

    LocalPointer<UVector> publicBucketList(new UVector(errorCode), errorCode);
    if (U_FAILURE(errorCode)) {
        return nullptr;
    }
    // Do not call publicBucketList->setDeleter():
    // This vector shares its objects with the bucketList.
    for (int32_t j = 0; j < bucketList->size(); ++j) {
        Bucket *bucket = getBucket(*bucketList, j);
        if (bucket->displayBucket_ == nullptr) {
            publicBucketList->addElement(bucket, errorCode);
        }
    }
    if (U_FAILURE(errorCode)) { return nullptr; }
    BucketList *bl = new BucketList(bucketList.getAlias(), publicBucketList.getAlias());
    if (bl == nullptr) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    bucketList.orphan();
    publicBucketList.orphan();
    return bl;
}

/**
 * Creates an index, and buckets and sorts the list of records into the index.
 */
void AlphabeticIndex::initBuckets(UErrorCode &errorCode) {
    if (U_FAILURE(errorCode) || buckets_ != nullptr) {
        return;
    }
    buckets_ = createBucketList(errorCode);
    if (U_FAILURE(errorCode) || inputList_ == nullptr || inputList_->isEmpty()) {
        return;
    }

    // Sort the records by name.
    // Stable sort preserves input order of collation duplicates.
    inputList_->sortWithUComparator(recordCompareFn, collator_, errorCode);

    // Now, we traverse all of the input, which is now sorted.
    // If the item doesn't go in the current bucket, we find the next bucket that contains it.
    // This makes the process order n*log(n), since we just sort the list and then do a linear process.
    // However, if the user adds an item at a time and then gets the buckets, this isn't efficient, so
    // we need to improve it for that case.

    Bucket *currentBucket = getBucket(*buckets_->bucketList_, 0);
    int32_t bucketIndex = 1;
    Bucket *nextBucket;
    const UnicodeString *upperBoundary;
    if (bucketIndex < buckets_->bucketList_->size()) {
        nextBucket = getBucket(*buckets_->bucketList_, bucketIndex++);
        upperBoundary = &nextBucket->lowerBoundary_;
    } else {
        nextBucket = nullptr;
        upperBoundary = nullptr;
    }
    for (int32_t i = 0; i < inputList_->size(); ++i) {
        Record *r = getRecord(*inputList_, i);
        // if the current bucket isn't the right one, find the one that is
        // We have a special flag for the last bucket so that we don't look any further
        while (upperBoundary != nullptr &&
                collatorPrimaryOnly_->compare(r->name_, *upperBoundary, errorCode) >= 0) {
            currentBucket = nextBucket;
            // now reset the boundary that we compare against
            if (bucketIndex < buckets_->bucketList_->size()) {
                nextBucket = getBucket(*buckets_->bucketList_, bucketIndex++);
                upperBoundary = &nextBucket->lowerBoundary_;
            } else {
                upperBoundary = nullptr;
            }
        }
        // now put the record into the bucket.
        Bucket *bucket = currentBucket;
        if (bucket->displayBucket_ != nullptr) {
            bucket = bucket->displayBucket_;
        }
        if (bucket->records_ == nullptr) {
            LocalPointer<UVector> records(new UVector(errorCode), errorCode);
            if (U_FAILURE(errorCode)) {
                return;
            }
            bucket->records_ = records.orphan();
        }
        bucket->records_->addElement(r, errorCode);
    }
}

void AlphabeticIndex::clearBuckets() {
    if (buckets_ != nullptr) {
        delete buckets_;
        buckets_ = nullptr;
        internalResetBucketIterator();
    }
}

void AlphabeticIndex::internalResetBucketIterator() {
    labelsIterIndex_ = -1;
    currentBucket_ = nullptr;
}


void AlphabeticIndex::addIndexExemplars(const Locale &locale, UErrorCode &status) {
    LocalULocaleDataPointer uld(ulocdata_open(locale.getName(), &status));
    if (U_FAILURE(status)) {
        return;
    }

    UnicodeSet exemplars;
    ulocdata_getExemplarSet(uld.getAlias(), exemplars.toUSet(), 0, ULOCDATA_ES_INDEX, &status);
    if (U_SUCCESS(status)) {
        initialLabels_->addAll(exemplars);
        return;
    }
    status = U_ZERO_ERROR;  // Clear out U_MISSING_RESOURCE_ERROR

    // The locale data did not include explicit Index characters.
    // Synthesize a set of them from the locale's standard exemplar characters.
    ulocdata_getExemplarSet(uld.getAlias(), exemplars.toUSet(), 0, ULOCDATA_ES_STANDARD, &status);
    if (U_FAILURE(status)) {
        return;
    }

    // question: should we add auxiliary exemplars?
    if (exemplars.containsSome(0x61, 0x7A) /* a-z */ || exemplars.isEmpty()) {
        exemplars.add(0x61, 0x7A);
    }
    if (exemplars.containsSome(0xAC00, 0xD7A3)) {  // Hangul syllables
        // cut down to small list
        exemplars.remove(0xAC00, 0xD7A3).
            add(0xAC00).add(0xB098).add(0xB2E4).add(0xB77C).
            add(0xB9C8).add(0xBC14).add(0xC0AC).add(0xC544).
            add(0xC790).add(0xCC28).add(0xCE74).add(0xD0C0).
            add(0xD30C).add(0xD558);
    }
    if (exemplars.containsSome(0x1200, 0x137F)) {  // Ethiopic block
        // cut down to small list
        // make use of the fact that Ethiopic is allocated in 8's, where
        // the base is 0 mod 8.
        UnicodeSet ethiopic(UnicodeString(u"[ሀለሐመሠረሰሸቀቈቐቘበቨተቸኀኈነኘአከኰኸዀወዐዘዠየደዸጀገጐጘጠጨጰጸፀፈፐፘ]"), status);
        ethiopic.retainAll(exemplars);
        exemplars.remove(u'ሀ', 0x137F).addAll(ethiopic);
    }

    // Upper-case any that aren't already so.
    //   (We only do this for synthesized index characters.)
    UnicodeSetIterator it(exemplars);
    UnicodeString upperC;
    while (it.next()) {
        const UnicodeString &exemplarC = it.getString();
        upperC = exemplarC;
        upperC.toUpper(locale);
        initialLabels_->add(upperC);
    }
}

UBool AlphabeticIndex::addChineseIndexCharacters(UErrorCode &errorCode) {
    UnicodeSet contractions;
    collatorPrimaryOnly_->internalAddContractions(BASE[0], contractions, errorCode);
    if (U_FAILURE(errorCode) || contractions.isEmpty()) { return false; }
    initialLabels_->addAll(contractions);
    UnicodeSetIterator iter(contractions);
    while (iter.next()) {
        const UnicodeString &s = iter.getString();
        U_ASSERT (s.startsWith(BASE, BASE_LENGTH));
        char16_t c = s.charAt(s.length() - 1);
        if (0x41 <= c && c <= 0x5A) {  // A-Z
            // There are Pinyin labels, add ASCII A-Z labels as well.
            initialLabels_->add(0x41, 0x5A);  // A-Z
            break;
        }
    }
    return true;
}


/*
 * Return the string with interspersed CGJs. Input must have more than 2 codepoints.
 */
static const char16_t CGJ = 0x034F;
UnicodeString AlphabeticIndex::separated(const UnicodeString &item) {
    UnicodeString result;
    if (item.length() == 0) {
        return result;
    }
    int32_t i = 0;
    for (;;) {
        UChar32  cp = item.char32At(i);
        result.append(cp);
        i = item.moveIndex32(i, 1);
        if (i >= item.length()) {
            break;
        }
        result.append(CGJ);
    }
    return result;
}


bool AlphabeticIndex::operator==(const AlphabeticIndex& /* other */) const {
    return false;
}


bool AlphabeticIndex::operator!=(const AlphabeticIndex& /* other */) const {
    return false;
}


const RuleBasedCollator &AlphabeticIndex::getCollator() const {
    return *collator_;
}


const UnicodeString &AlphabeticIndex::getInflowLabel() const {
    return inflowLabel_;
}

const UnicodeString &AlphabeticIndex::getOverflowLabel() const {
    return overflowLabel_;
}


const UnicodeString &AlphabeticIndex::getUnderflowLabel() const {
    return underflowLabel_;
}


AlphabeticIndex &AlphabeticIndex::setInflowLabel(const UnicodeString &label, UErrorCode &/*status*/) {
    inflowLabel_ = label;
    clearBuckets();
    return *this;
}


AlphabeticIndex &AlphabeticIndex::setOverflowLabel(const UnicodeString &label, UErrorCode &/*status*/) {
    overflowLabel_ = label;
    clearBuckets();
    return *this;
}


AlphabeticIndex &AlphabeticIndex::setUnderflowLabel(const UnicodeString &label, UErrorCode &/*status*/) {
    underflowLabel_ = label;
    clearBuckets();
    return *this;
}


int32_t AlphabeticIndex::getMaxLabelCount() const {
    return maxLabelCount_;
}


AlphabeticIndex &AlphabeticIndex::setMaxLabelCount(int32_t maxLabelCount, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return *this;
    }
    if (maxLabelCount <= 0) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return *this;
    }
    maxLabelCount_ = maxLabelCount;
    clearBuckets();
    return *this;
}


//
//  init() - Common code for constructors.
//

void AlphabeticIndex::init(const Locale *locale, UErrorCode &status) {
    if (U_FAILURE(status)) { return; }
    if (locale == nullptr && collator_ == nullptr) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    initialLabels_         = new UnicodeSet();
    if (initialLabels_ == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }

    inflowLabel_.setTo(static_cast<char16_t>(0x2026)); // Ellipsis
    overflowLabel_ = inflowLabel_;
    underflowLabel_ = inflowLabel_;

    if (collator_ == nullptr) {
        Collator *coll = Collator::createInstance(*locale, status);
        if (U_FAILURE(status)) {
            delete coll;
            return;
        }
        if (coll == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
        collator_ = dynamic_cast<RuleBasedCollator *>(coll);
        if (collator_ == nullptr) {
            delete coll;
            status = U_UNSUPPORTED_ERROR;
            return;
        }
    }
    collatorPrimaryOnly_ = collator_->clone();
    if (collatorPrimaryOnly_ == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    collatorPrimaryOnly_->setAttribute(UCOL_STRENGTH, UCOL_PRIMARY, status);
    firstCharsInScripts_ = firstStringsInScript(status);
    if (U_FAILURE(status)) { return; }
    firstCharsInScripts_->sortWithUComparator(collatorComparator, collatorPrimaryOnly_, status);
    // Guard against a degenerate collator where
    // some script boundary strings are primary ignorable.
    for (;;) {
        if (U_FAILURE(status)) { return; }
        if (firstCharsInScripts_->isEmpty()) {
            // AlphabeticIndex requires some non-ignorable script boundary strings.
            status = U_ILLEGAL_ARGUMENT_ERROR;
            return;
        }
        if (collatorPrimaryOnly_->compare(
                *static_cast<UnicodeString *>(firstCharsInScripts_->elementAt(0)),
                emptyString_, status) == UCOL_EQUAL) {
            firstCharsInScripts_->removeElementAt(0);
        } else {
            break;
        }
    }

    // Chinese index characters, which are specific to each of the several Chinese tailorings,
    // take precedence over the single locale data exemplar set per language.
    if (!addChineseIndexCharacters(status) && locale != nullptr) {
        addIndexExemplars(*locale, status);
    }
}


//
//  Comparison function for UVector<UnicodeString *> sorting with a collator.
//
static int32_t U_CALLCONV
collatorComparator(const void *context, const void *left, const void *right) {
    const UElement *leftElement = static_cast<const UElement *>(left);
    const UElement *rightElement = static_cast<const UElement *>(right);
    const UnicodeString *leftString  = static_cast<const UnicodeString *>(leftElement->pointer);
    const UnicodeString *rightString = static_cast<const UnicodeString *>(rightElement->pointer);

    if (leftString == rightString) {
        // Catches case where both are nullptr
        return 0;
    }
    if (leftString == nullptr) {
        return 1;
    }
    if (rightString == nullptr) {
        return -1;
    }
    const Collator *col = static_cast<const Collator *>(context);
    UErrorCode errorCode = U_ZERO_ERROR;
    return col->compare(*leftString, *rightString, errorCode);
}

//
//  Comparison function for UVector<Record *> sorting with a collator.
//
static int32_t U_CALLCONV
recordCompareFn(const void *context, const void *left, const void *right) {
    const UElement *leftElement = static_cast<const UElement *>(left);
    const UElement *rightElement = static_cast<const UElement *>(right);
    const AlphabeticIndex::Record *leftRec  = static_cast<const AlphabeticIndex::Record *>(leftElement->pointer);
    const AlphabeticIndex::Record *rightRec = static_cast<const AlphabeticIndex::Record *>(rightElement->pointer);
    const Collator *col = static_cast<const Collator *>(context);
    UErrorCode errorCode = U_ZERO_ERROR;
    return col->compare(leftRec->name_, rightRec->name_, errorCode);
}

UVector *AlphabeticIndex::firstStringsInScript(UErrorCode &status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    LocalPointer<UVector> dest(new UVector(status), status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    dest->setDeleter(uprv_deleteUObject);
    // Fetch the script-first-primary contractions which are defined in the root collator.
    // They all start with U+FDD1.
    UnicodeSet set;
    collatorPrimaryOnly_->internalAddContractions(0xFDD1, set, status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    if (set.isEmpty()) {
        status = U_UNSUPPORTED_ERROR;
        return nullptr;
    }
    UnicodeSetIterator iter(set);
    while (iter.next()) {
        const UnicodeString &boundary = iter.getString();
        uint32_t gcMask = U_GET_GC_MASK(boundary.char32At(1));
        if ((gcMask & (U_GC_L_MASK | U_GC_CN_MASK)) == 0) {
            // Ignore boundaries for the special reordering groups.
            // Take only those for "real scripts" (where the sample character is a Letter,
            // and the one for unassigned implicit weights (Cn).
            continue;
        }
        LocalPointer<UnicodeString> s(new UnicodeString(boundary), status);
        dest->adoptElement(s.orphan(), status);
        if (U_FAILURE(status)) {
            return nullptr;
        }
    }
    return dest.orphan();
}


namespace {

/**
 * Returns true if one index character string is "better" than the other.
 * Shorter NFKD is better, and otherwise NFKD-binary-less-than is
 * better, and otherwise binary-less-than is better.
 */
UBool isOneLabelBetterThanOther(const Normalizer2 &nfkdNormalizer,
                                const UnicodeString &one, const UnicodeString &other) {
    // This is called with primary-equal strings, but never with one.equals(other).
    UErrorCode status = U_ZERO_ERROR;
    UnicodeString n1 = nfkdNormalizer.normalize(one, status);
    UnicodeString n2 = nfkdNormalizer.normalize(other, status);
    if (U_FAILURE(status)) { return false; }
    int32_t result = n1.countChar32() - n2.countChar32();
    if (result != 0) {
        return result < 0;
    }
    result = n1.compareCodePointOrder(n2);
    if (result != 0) {
        return result < 0;
    }
    return one.compareCodePointOrder(other) < 0;
}

}  // namespace

//
//  Constructor & Destructor for AlphabeticIndex::Record
//
//     Records are internal only, instances are not directly surfaced in the public API.
//     This class is mostly struct-like, with all public fields.

AlphabeticIndex::Record::Record(const UnicodeString &name, const void *data)
        : name_(name), data_(data) {}

AlphabeticIndex::Record::~Record() {
}


AlphabeticIndex & AlphabeticIndex::addRecord(const UnicodeString &name, const void *data, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return *this;
    }
    if (inputList_ == nullptr) {
        LocalPointer<UVector> inputList(new UVector(status), status);
        if (U_FAILURE(status)) {
            return *this;
        }
        inputList_ = inputList.orphan();
        inputList_->setDeleter(alphaIndex_deleteRecord);
    }
    LocalPointer<Record> r(new Record(name, data), status);
    inputList_->adoptElement(r.orphan(), status);
    if (U_FAILURE(status)) {
        return *this;
    }
    clearBuckets();
    //std::string ss;
    //std::string ss2;
    //std::cout << "added record: name = \"" << r->name_.toUTF8String(ss) << "\"" << 
    //             "   sortingName = \"" << r->sortingName_.toUTF8String(ss2) << "\"" << std::endl;
    return *this;
}


AlphabeticIndex &AlphabeticIndex::clearRecords(UErrorCode &status) {
    if (U_SUCCESS(status) && inputList_ != nullptr && !inputList_->isEmpty()) {
        inputList_->removeAllElements();
        clearBuckets();
    }
    return *this;
}

int32_t AlphabeticIndex::getBucketIndex(const UnicodeString &name, UErrorCode &status) {
    initBuckets(status);
    if (U_FAILURE(status)) {
        return 0;
    }
    return buckets_->getBucketIndex(name, *collatorPrimaryOnly_, status);
}


int32_t AlphabeticIndex::getBucketIndex() const {
    return labelsIterIndex_;
}


UBool AlphabeticIndex::nextBucket(UErrorCode &status) {
    if (U_FAILURE(status)) {
        return false;
    }
    if (buckets_ == nullptr && currentBucket_ != nullptr) {
        status = U_ENUM_OUT_OF_SYNC_ERROR;
        return false;
    }
    initBuckets(status);
    if (U_FAILURE(status)) {
        return false;
    }
    ++labelsIterIndex_;
    if (labelsIterIndex_ >= buckets_->getBucketCount()) {
        labelsIterIndex_ = buckets_->getBucketCount();
        return false;
    }
    currentBucket_ = getBucket(*buckets_->immutableVisibleList_, labelsIterIndex_);
    resetRecordIterator();
    return true;
}

const UnicodeString &AlphabeticIndex::getBucketLabel() const {
    if (currentBucket_ != nullptr) {
        return currentBucket_->label_;
    } else {
        return emptyString_;
    }
}


UAlphabeticIndexLabelType AlphabeticIndex::getBucketLabelType() const {
    if (currentBucket_ != nullptr) {
        return currentBucket_->labelType_;
    } else {
        return U_ALPHAINDEX_NORMAL;
    }
}


int32_t AlphabeticIndex::getBucketRecordCount() const {
    if (currentBucket_ != nullptr && currentBucket_->records_ != nullptr) {
        return currentBucket_->records_->size();
    } else {
        return 0;
    }
}


AlphabeticIndex &AlphabeticIndex::resetBucketIterator(UErrorCode &status) {
    if (U_FAILURE(status)) {
        return *this;
    }
    internalResetBucketIterator();
    return *this;
}


UBool AlphabeticIndex::nextRecord(UErrorCode &status) {
    if (U_FAILURE(status)) {
        return false;
    }
    if (currentBucket_ == nullptr) {
        // We are trying to iterate over the items in a bucket, but there is no
        // current bucket from the enumeration of buckets.
        status = U_INVALID_STATE_ERROR;
        return false;
    }
    if (buckets_ == nullptr) {
        status = U_ENUM_OUT_OF_SYNC_ERROR;
        return false;
    }
    if (currentBucket_->records_ == nullptr) {
        return false;
    }
    ++itemsIterIndex_;
    if (itemsIterIndex_ >= currentBucket_->records_->size()) {
        itemsIterIndex_  = currentBucket_->records_->size();
        return false;
    }
    return true;
}


const UnicodeString &AlphabeticIndex::getRecordName() const {
    const UnicodeString *retStr = &emptyString_;
    if (currentBucket_ != nullptr && currentBucket_->records_ != nullptr &&
        itemsIterIndex_ >= 0 &&
        itemsIterIndex_ < currentBucket_->records_->size()) {
            Record *item = static_cast<Record *>(currentBucket_->records_->elementAt(itemsIterIndex_));
            retStr = &item->name_;
    }
    return *retStr;
}

const void *AlphabeticIndex::getRecordData() const {
    const void *retPtr = nullptr;
    if (currentBucket_ != nullptr && currentBucket_->records_ != nullptr &&
        itemsIterIndex_ >= 0 &&
        itemsIterIndex_ < currentBucket_->records_->size()) {
            Record *item = static_cast<Record *>(currentBucket_->records_->elementAt(itemsIterIndex_));
            retPtr = item->data_;
    }
    return retPtr;
}


AlphabeticIndex & AlphabeticIndex::resetRecordIterator() {
    itemsIterIndex_ = -1;
    return *this;
}



AlphabeticIndex::Bucket::Bucket(const UnicodeString &label,
                                const UnicodeString &lowerBoundary,
                                UAlphabeticIndexLabelType type)
        : label_(label), lowerBoundary_(lowerBoundary), labelType_(type),
          displayBucket_(nullptr), displayIndex_(-1),
          records_(nullptr) {
}


AlphabeticIndex::Bucket::~Bucket() {
    delete records_;
}

U_NAMESPACE_END

#endif  // !UCONFIG_NO_COLLATION
