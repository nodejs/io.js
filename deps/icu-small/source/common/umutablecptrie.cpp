// © 2017 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

// umutablecptrie.cpp (inspired by utrie2_builder.cpp)
// created: 2017dec29 Markus W. Scherer

// #define UCPTRIE_DEBUG
#ifdef UCPTRIE_DEBUG
#   include <stdio.h>
#endif

#include "unicode/utypes.h"
#include "unicode/ucptrie.h"
#include "unicode/umutablecptrie.h"
#include "unicode/uobject.h"
#include "unicode/utf16.h"
#include "cmemory.h"
#include "uassert.h"
#include "ucptrie_impl.h"

// ICU-20235 In case Microsoft math.h has defined this, undefine it.
#ifdef OVERFLOW
#undef OVERFLOW
#endif

U_NAMESPACE_BEGIN

namespace {

constexpr int32_t MAX_UNICODE = 0x10ffff;

constexpr int32_t UNICODE_LIMIT = 0x110000;
constexpr int32_t BMP_LIMIT = 0x10000;
constexpr int32_t ASCII_LIMIT = 0x80;

constexpr int32_t I_LIMIT = UNICODE_LIMIT >> UCPTRIE_SHIFT_3;
constexpr int32_t BMP_I_LIMIT = BMP_LIMIT >> UCPTRIE_SHIFT_3;
constexpr int32_t ASCII_I_LIMIT = ASCII_LIMIT >> UCPTRIE_SHIFT_3;

constexpr int32_t SMALL_DATA_BLOCKS_PER_BMP_BLOCK = (1 << (UCPTRIE_FAST_SHIFT - UCPTRIE_SHIFT_3));

// Flag values for data blocks.
constexpr uint8_t ALL_SAME = 0;
constexpr uint8_t MIXED = 1;
constexpr uint8_t SAME_AS = 2;

/** Start with allocation of 16k data entries. */
constexpr int32_t INITIAL_DATA_LENGTH = static_cast<int32_t>(1) << 14;

/** Grow about 8x each time. */
constexpr int32_t MEDIUM_DATA_LENGTH = static_cast<int32_t>(1) << 17;

/**
 * Maximum length of the build-time data array.
 * One entry per 0x110000 code points.
 */
constexpr int32_t MAX_DATA_LENGTH = UNICODE_LIMIT;

// Flag values for index-3 blocks while compacting/building.
constexpr uint8_t I3_NULL = 0;
constexpr uint8_t I3_BMP = 1;
constexpr uint8_t I3_16 = 2;
constexpr uint8_t I3_18 = 3;

constexpr int32_t INDEX_3_18BIT_BLOCK_LENGTH = UCPTRIE_INDEX_3_BLOCK_LENGTH + UCPTRIE_INDEX_3_BLOCK_LENGTH / 8;

class AllSameBlocks;
class MixedBlocks;

class MutableCodePointTrie : public UMemory {
public:
    MutableCodePointTrie(uint32_t initialValue, uint32_t errorValue, UErrorCode &errorCode);
    MutableCodePointTrie(const MutableCodePointTrie &other, UErrorCode &errorCode);
    MutableCodePointTrie(const MutableCodePointTrie &other) = delete;
    ~MutableCodePointTrie();

    MutableCodePointTrie &operator=(const MutableCodePointTrie &other) = delete;

    static MutableCodePointTrie *fromUCPMap(const UCPMap *map, UErrorCode &errorCode);
    static MutableCodePointTrie *fromUCPTrie(const UCPTrie *trie, UErrorCode &errorCode);

    uint32_t get(UChar32 c) const;
    int32_t getRange(UChar32 start, UCPMapValueFilter *filter, const void *context,
                     uint32_t *pValue) const;

    void set(UChar32 c, uint32_t value, UErrorCode &errorCode);
    void setRange(UChar32 start, UChar32 end, uint32_t value, UErrorCode &errorCode);

    UCPTrie *build(UCPTrieType type, UCPTrieValueWidth valueWidth, UErrorCode &errorCode);

private:
    void clear();

    bool ensureHighStart(UChar32 c);
    int32_t allocDataBlock(int32_t blockLength);
    int32_t getDataBlock(int32_t i);

    void maskValues(uint32_t mask);
    UChar32 findHighStart() const;
    int32_t compactWholeDataBlocks(int32_t fastILimit, AllSameBlocks &allSameBlocks);
    int32_t compactData(
            int32_t fastILimit, uint32_t *newData, int32_t newDataCapacity,
            int32_t dataNullIndex, MixedBlocks &mixedBlocks, UErrorCode &errorCode);
    int32_t compactIndex(int32_t fastILimit, MixedBlocks &mixedBlocks, UErrorCode &errorCode);
    int32_t compactTrie(int32_t fastILimit, UErrorCode &errorCode);

    uint32_t *index = nullptr;
    int32_t indexCapacity = 0;
    int32_t index3NullOffset = -1;
    uint32_t *data = nullptr;
    int32_t dataCapacity = 0;
    int32_t dataLength = 0;
    int32_t dataNullOffset = -1;

    uint32_t origInitialValue;
    uint32_t initialValue;
    uint32_t errorValue;
    UChar32 highStart;
    uint32_t highValue;
#ifdef UCPTRIE_DEBUG
public:
    const char *name;
#endif
private:
    /** Temporary array while building the final data. */
    uint16_t *index16 = nullptr;
    uint8_t flags[UNICODE_LIMIT >> UCPTRIE_SHIFT_3];
};

MutableCodePointTrie::MutableCodePointTrie(uint32_t iniValue, uint32_t errValue, UErrorCode &errorCode) :
        origInitialValue(iniValue), initialValue(iniValue), errorValue(errValue),
        highStart(0), highValue(initialValue)
#ifdef UCPTRIE_DEBUG
        , name("open")
#endif
        {
    if (U_FAILURE(errorCode)) { return; }
    index = static_cast<uint32_t*>(uprv_malloc(BMP_I_LIMIT * 4));
    data = static_cast<uint32_t*>(uprv_malloc(INITIAL_DATA_LENGTH * 4));
    if (index == nullptr || data == nullptr) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    indexCapacity = BMP_I_LIMIT;
    dataCapacity = INITIAL_DATA_LENGTH;
}

MutableCodePointTrie::MutableCodePointTrie(const MutableCodePointTrie &other, UErrorCode &errorCode) :
        index3NullOffset(other.index3NullOffset),
        dataNullOffset(other.dataNullOffset),
        origInitialValue(other.origInitialValue), initialValue(other.initialValue),
        errorValue(other.errorValue),
        highStart(other.highStart), highValue(other.highValue)
#ifdef UCPTRIE_DEBUG
        , name("mutable clone")
#endif
        {
    if (U_FAILURE(errorCode)) { return; }
    int32_t iCapacity = highStart <= BMP_LIMIT ? BMP_I_LIMIT : I_LIMIT;
    index = static_cast<uint32_t*>(uprv_malloc(iCapacity * 4));
    data = static_cast<uint32_t*>(uprv_malloc(other.dataCapacity * 4));
    if (index == nullptr || data == nullptr) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    indexCapacity = iCapacity;
    dataCapacity = other.dataCapacity;

    int32_t iLimit = highStart >> UCPTRIE_SHIFT_3;
    uprv_memcpy(flags, other.flags, iLimit);
    uprv_memcpy(index, other.index, iLimit * 4);
    uprv_memcpy(data, other.data, (size_t)other.dataLength * 4);
    dataLength = other.dataLength;
    U_ASSERT(other.index16 == nullptr);
}

MutableCodePointTrie::~MutableCodePointTrie() {
    uprv_free(index);
    uprv_free(data);
    uprv_free(index16);
}

MutableCodePointTrie *MutableCodePointTrie::fromUCPMap(const UCPMap *map, UErrorCode &errorCode) {
    // Use the highValue as the initialValue to reduce the highStart.
    uint32_t errorValue = ucpmap_get(map, -1);
    uint32_t initialValue = ucpmap_get(map, 0x10ffff);
    LocalPointer<MutableCodePointTrie> mutableTrie(
        new MutableCodePointTrie(initialValue, errorValue, errorCode),
        errorCode);
    if (U_FAILURE(errorCode)) {
        return nullptr;
    }
    UChar32 start = 0, end;
    uint32_t value;
    while ((end = ucpmap_getRange(map, start, UCPMAP_RANGE_NORMAL, 0,
                                  nullptr, nullptr, &value)) >= 0) {
        if (value != initialValue) {
            if (start == end) {
                mutableTrie->set(start, value, errorCode);
            } else {
                mutableTrie->setRange(start, end, value, errorCode);
            }
        }
        start = end + 1;
    }
    if (U_SUCCESS(errorCode)) {
        return mutableTrie.orphan();
    } else {
        return nullptr;
    }
}

MutableCodePointTrie *MutableCodePointTrie::fromUCPTrie(const UCPTrie *trie, UErrorCode &errorCode) {
    // Use the highValue as the initialValue to reduce the highStart.
    uint32_t errorValue;
    uint32_t initialValue;
    switch (trie->valueWidth) {
    case UCPTRIE_VALUE_BITS_16:
        errorValue = trie->data.ptr16[trie->dataLength - UCPTRIE_ERROR_VALUE_NEG_DATA_OFFSET];
        initialValue = trie->data.ptr16[trie->dataLength - UCPTRIE_HIGH_VALUE_NEG_DATA_OFFSET];
        break;
    case UCPTRIE_VALUE_BITS_32:
        errorValue = trie->data.ptr32[trie->dataLength - UCPTRIE_ERROR_VALUE_NEG_DATA_OFFSET];
        initialValue = trie->data.ptr32[trie->dataLength - UCPTRIE_HIGH_VALUE_NEG_DATA_OFFSET];
        break;
    case UCPTRIE_VALUE_BITS_8:
        errorValue = trie->data.ptr8[trie->dataLength - UCPTRIE_ERROR_VALUE_NEG_DATA_OFFSET];
        initialValue = trie->data.ptr8[trie->dataLength - UCPTRIE_HIGH_VALUE_NEG_DATA_OFFSET];
        break;
    default:
        // Unreachable if the trie is properly initialized.
        errorCode = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }
    LocalPointer<MutableCodePointTrie> mutableTrie(
        new MutableCodePointTrie(initialValue, errorValue, errorCode),
        errorCode);
    if (U_FAILURE(errorCode)) {
        return nullptr;
    }
    UChar32 start = 0, end;
    uint32_t value;
    while ((end = ucptrie_getRange(trie, start, UCPMAP_RANGE_NORMAL, 0,
                                   nullptr, nullptr, &value)) >= 0) {
        if (value != initialValue) {
            if (start == end) {
                mutableTrie->set(start, value, errorCode);
            } else {
                mutableTrie->setRange(start, end, value, errorCode);
            }
        }
        start = end + 1;
    }
    if (U_SUCCESS(errorCode)) {
        return mutableTrie.orphan();
    } else {
        return nullptr;
    }
}

void MutableCodePointTrie::clear() {
    index3NullOffset = dataNullOffset = -1;
    dataLength = 0;
    highValue = initialValue = origInitialValue;
    highStart = 0;
    uprv_free(index16);
    index16 = nullptr;
}

uint32_t MutableCodePointTrie::get(UChar32 c) const {
    if (static_cast<uint32_t>(c) > MAX_UNICODE) {
        return errorValue;
    }
    if (c >= highStart) {
        return highValue;
    }
    int32_t i = c >> UCPTRIE_SHIFT_3;
    if (flags[i] == ALL_SAME) {
        return index[i];
    } else {
        return data[index[i] + (c & UCPTRIE_SMALL_DATA_MASK)];
    }
}

inline uint32_t maybeFilterValue(uint32_t value, uint32_t initialValue, uint32_t nullValue,
                                 UCPMapValueFilter *filter, const void *context) {
    if (value == initialValue) {
        value = nullValue;
    } else if (filter != nullptr) {
        value = filter(context, value);
    }
    return value;
}

UChar32 MutableCodePointTrie::getRange(
        UChar32 start, UCPMapValueFilter *filter, const void *context,
        uint32_t *pValue) const {
    if (static_cast<uint32_t>(start) > MAX_UNICODE) {
        return U_SENTINEL;
    }
    if (start >= highStart) {
        if (pValue != nullptr) {
            uint32_t value = highValue;
            if (filter != nullptr) { value = filter(context, value); }
            *pValue = value;
        }
        return MAX_UNICODE;
    }
    uint32_t nullValue = initialValue;
    if (filter != nullptr) { nullValue = filter(context, nullValue); }
    UChar32 c = start;
    uint32_t trieValue, value;
    bool haveValue = false;
    int32_t i = c >> UCPTRIE_SHIFT_3;
    do {
        if (flags[i] == ALL_SAME) {
            uint32_t trieValue2 = index[i];
            if (haveValue) {
                if (trieValue2 != trieValue) {
                    if (filter == nullptr ||
                            maybeFilterValue(trieValue2, initialValue, nullValue,
                                             filter, context) != value) {
                        return c - 1;
                    }
                    trieValue = trieValue2;  // may or may not help
                }
            } else {
                trieValue = trieValue2;
                value = maybeFilterValue(trieValue2, initialValue, nullValue, filter, context);
                if (pValue != nullptr) { *pValue = value; }
                haveValue = true;
            }
            c = (c + UCPTRIE_SMALL_DATA_BLOCK_LENGTH) & ~UCPTRIE_SMALL_DATA_MASK;
        } else /* MIXED */ {
            int32_t di = index[i] + (c & UCPTRIE_SMALL_DATA_MASK);
            uint32_t trieValue2 = data[di];
            if (haveValue) {
                if (trieValue2 != trieValue) {
                    if (filter == nullptr ||
                            maybeFilterValue(trieValue2, initialValue, nullValue,
                                             filter, context) != value) {
                        return c - 1;
                    }
                    trieValue = trieValue2;  // may or may not help
                }
            } else {
                trieValue = trieValue2;
                value = maybeFilterValue(trieValue2, initialValue, nullValue, filter, context);
                if (pValue != nullptr) { *pValue = value; }
                haveValue = true;
            }
            while ((++c & UCPTRIE_SMALL_DATA_MASK) != 0) {
                trieValue2 = data[++di];
                if (trieValue2 != trieValue) {
                    if (filter == nullptr ||
                            maybeFilterValue(trieValue2, initialValue, nullValue,
                                             filter, context) != value) {
                        return c - 1;
                    }
                }
                trieValue = trieValue2;  // may or may not help
            }
        }
        ++i;
    } while (c < highStart);
    U_ASSERT(haveValue);
    if (maybeFilterValue(highValue, initialValue, nullValue,
                         filter, context) != value) {
        return c - 1;
    } else {
        return MAX_UNICODE;
    }
}

void
writeBlock(uint32_t *block, uint32_t value) {
    uint32_t *limit = block + UCPTRIE_SMALL_DATA_BLOCK_LENGTH;
    while (block < limit) {
        *block++ = value;
    }
}

bool MutableCodePointTrie::ensureHighStart(UChar32 c) {
    if (c >= highStart) {
        // Round up to a UCPTRIE_CP_PER_INDEX_2_ENTRY boundary to simplify compaction.
        c = (c + UCPTRIE_CP_PER_INDEX_2_ENTRY) & ~(UCPTRIE_CP_PER_INDEX_2_ENTRY - 1);
        int32_t i = highStart >> UCPTRIE_SHIFT_3;
        int32_t iLimit = c >> UCPTRIE_SHIFT_3;
        if (iLimit > indexCapacity) {
            uint32_t* newIndex = static_cast<uint32_t*>(uprv_malloc(I_LIMIT * 4));
            if (newIndex == nullptr) { return false; }
            uprv_memcpy(newIndex, index, i * 4);
            uprv_free(index);
            index = newIndex;
            indexCapacity = I_LIMIT;
        }
        do {
            flags[i] = ALL_SAME;
            index[i] = initialValue;
        } while(++i < iLimit);
        highStart = c;
    }
    return true;
}

int32_t MutableCodePointTrie::allocDataBlock(int32_t blockLength) {
    int32_t newBlock = dataLength;
    int32_t newTop = newBlock + blockLength;
    if (newTop > dataCapacity) {
        int32_t capacity;
        if (dataCapacity < MEDIUM_DATA_LENGTH) {
            capacity = MEDIUM_DATA_LENGTH;
        } else if (dataCapacity < MAX_DATA_LENGTH) {
            capacity = MAX_DATA_LENGTH;
        } else {
            // Should never occur.
            // Either MAX_DATA_LENGTH is incorrect,
            // or the code writes more values than should be possible.
            return -1;
        }
        uint32_t* newData = static_cast<uint32_t*>(uprv_malloc(capacity * 4));
        if (newData == nullptr) {
            return -1;
        }
        uprv_memcpy(newData, data, (size_t)dataLength * 4);
        uprv_free(data);
        data = newData;
        dataCapacity = capacity;
    }
    dataLength = newTop;
    return newBlock;
}

/**
 * No error checking for illegal arguments.
 *
 * @return -1 if no new data block available (out of memory in data array)
 * @internal
 */
int32_t MutableCodePointTrie::getDataBlock(int32_t i) {
    if (flags[i] == MIXED) {
        return index[i];
    }
    if (i < BMP_I_LIMIT) {
        int32_t newBlock = allocDataBlock(UCPTRIE_FAST_DATA_BLOCK_LENGTH);
        if (newBlock < 0) { return newBlock; }
        int32_t iStart = i & ~(SMALL_DATA_BLOCKS_PER_BMP_BLOCK -1);
        int32_t iLimit = iStart + SMALL_DATA_BLOCKS_PER_BMP_BLOCK;
        do {
            U_ASSERT(flags[iStart] == ALL_SAME);
            writeBlock(data + newBlock, index[iStart]);
            flags[iStart] = MIXED;
            index[iStart++] = newBlock;
            newBlock += UCPTRIE_SMALL_DATA_BLOCK_LENGTH;
        } while (iStart < iLimit);
        return index[i];
    } else {
        int32_t newBlock = allocDataBlock(UCPTRIE_SMALL_DATA_BLOCK_LENGTH);
        if (newBlock < 0) { return newBlock; }
        writeBlock(data + newBlock, index[i]);
        flags[i] = MIXED;
        index[i] = newBlock;
        return newBlock;
    }
}

void MutableCodePointTrie::set(UChar32 c, uint32_t value, UErrorCode &errorCode) {
    if (U_FAILURE(errorCode)) {
        return;
    }
    if (static_cast<uint32_t>(c) > MAX_UNICODE) {
        errorCode = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    int32_t block;
    if (!ensureHighStart(c) || (block = getDataBlock(c >> UCPTRIE_SHIFT_3)) < 0) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        return;
    }

    data[block + (c & UCPTRIE_SMALL_DATA_MASK)] = value;
}

void
fillBlock(uint32_t *block, UChar32 start, UChar32 limit, uint32_t value) {
    uint32_t *pLimit = block + limit;
    block += start;
    while (block < pLimit) {
        *block++ = value;
    }
}

void MutableCodePointTrie::setRange(UChar32 start, UChar32 end, uint32_t value, UErrorCode &errorCode) {
    if (U_FAILURE(errorCode)) {
        return;
    }
    if (static_cast<uint32_t>(start) > MAX_UNICODE || static_cast<uint32_t>(end) > MAX_UNICODE || start > end) {
        errorCode = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    if (!ensureHighStart(end)) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        return;
    }

    UChar32 limit = end + 1;
    if (start & UCPTRIE_SMALL_DATA_MASK) {
        // Set partial block at [start..following block boundary[.
        int32_t block = getDataBlock(start >> UCPTRIE_SHIFT_3);
        if (block < 0) {
            errorCode = U_MEMORY_ALLOCATION_ERROR;
            return;
        }

        UChar32 nextStart = (start + UCPTRIE_SMALL_DATA_MASK) & ~UCPTRIE_SMALL_DATA_MASK;
        if (nextStart <= limit) {
            fillBlock(data + block, start & UCPTRIE_SMALL_DATA_MASK, UCPTRIE_SMALL_DATA_BLOCK_LENGTH,
                      value);
            start = nextStart;
        } else {
            fillBlock(data + block, start & UCPTRIE_SMALL_DATA_MASK, limit & UCPTRIE_SMALL_DATA_MASK,
                      value);
            return;
        }
    }

    // Number of positions in the last, partial block.
    int32_t rest = limit & UCPTRIE_SMALL_DATA_MASK;

    // Round down limit to a block boundary.
    limit &= ~UCPTRIE_SMALL_DATA_MASK;

    // Iterate over all-value blocks.
    while (start < limit) {
        int32_t i = start >> UCPTRIE_SHIFT_3;
        if (flags[i] == ALL_SAME) {
            index[i] = value;
        } else /* MIXED */ {
            fillBlock(data + index[i], 0, UCPTRIE_SMALL_DATA_BLOCK_LENGTH, value);
        }
        start += UCPTRIE_SMALL_DATA_BLOCK_LENGTH;
    }

    if (rest > 0) {
        // Set partial block at [last block boundary..limit[.
        int32_t block = getDataBlock(start >> UCPTRIE_SHIFT_3);
        if (block < 0) {
            errorCode = U_MEMORY_ALLOCATION_ERROR;
            return;
        }

        fillBlock(data + block, 0, rest, value);
    }
}

/* compaction --------------------------------------------------------------- */

void MutableCodePointTrie::maskValues(uint32_t mask) {
    initialValue &= mask;
    errorValue &= mask;
    highValue &= mask;
    int32_t iLimit = highStart >> UCPTRIE_SHIFT_3;
    for (int32_t i = 0; i < iLimit; ++i) {
        if (flags[i] == ALL_SAME) {
            index[i] &= mask;
        }
    }
    for (int32_t i = 0; i < dataLength; ++i) {
        data[i] &= mask;
    }
}

template<typename UIntA, typename UIntB>
bool equalBlocks(const UIntA *s, const UIntB *t, int32_t length) {
    while (length > 0 && *s == *t) {
        ++s;
        ++t;
        --length;
    }
    return length == 0;
}

bool allValuesSameAs(const uint32_t *p, int32_t length, uint32_t value) {
    const uint32_t *pLimit = p + length;
    while (p < pLimit && *p == value) { ++p; }
    return p == pLimit;
}

/** Search for an identical block. */
int32_t findSameBlock(const uint16_t *p, int32_t pStart, int32_t length,
                      const uint16_t *q, int32_t qStart, int32_t blockLength) {
    // Ensure that we do not even partially get past length.
    length -= blockLength;

    q += qStart;
    while (pStart <= length) {
        if (equalBlocks(p + pStart, q, blockLength)) {
            return pStart;
        }
        ++pStart;
    }
    return -1;
}

int32_t findAllSameBlock(const uint32_t *p, int32_t start, int32_t limit,
                         uint32_t value, int32_t blockLength) {
    // Ensure that we do not even partially get past limit.
    limit -= blockLength;

    for (int32_t block = start; block <= limit; ++block) {
        if (p[block] == value) {
            for (int32_t i = 1;; ++i) {
                if (i == blockLength) {
                    return block;
                }
                if (p[block + i] != value) {
                    block += i;
                    break;
                }
            }
        }
    }
    return -1;
}

/**
 * Look for maximum overlap of the beginning of the other block
 * with the previous, adjacent block.
 */
template<typename UIntA, typename UIntB>
int32_t getOverlap(const UIntA *p, int32_t length,
                   const UIntB *q, int32_t qStart, int32_t blockLength) {
    int32_t overlap = blockLength - 1;
    U_ASSERT(overlap <= length);
    q += qStart;
    while (overlap > 0 && !equalBlocks(p + (length - overlap), q, overlap)) {
        --overlap;
    }
    return overlap;
}

int32_t getAllSameOverlap(const uint32_t *p, int32_t length, uint32_t value,
                          int32_t blockLength) {
    int32_t min = length - (blockLength - 1);
    int32_t i = length;
    while (min < i && p[i - 1] == value) { --i; }
    return length - i;
}

bool isStartOfSomeFastBlock(uint32_t dataOffset, const uint32_t index[], int32_t fastILimit) {
    for (int32_t i = 0; i < fastILimit; i += SMALL_DATA_BLOCKS_PER_BMP_BLOCK) {
        if (index[i] == dataOffset) {
            return true;
        }
    }
    return false;
}

/**
 * Finds the start of the last range in the trie by enumerating backward.
 * Indexes for code points higher than this will be omitted.
 */
UChar32 MutableCodePointTrie::findHighStart() const {
    int32_t i = highStart >> UCPTRIE_SHIFT_3;
    while (i > 0) {
        bool match;
        if (flags[--i] == ALL_SAME) {
            match = index[i] == highValue;
        } else /* MIXED */ {
            const uint32_t *p = data + index[i];
            for (int32_t j = 0;; ++j) {
                if (j == UCPTRIE_SMALL_DATA_BLOCK_LENGTH) {
                    match = true;
                    break;
                }
                if (p[j] != highValue) {
                    match = false;
                    break;
                }
            }
        }
        if (!match) {
            return (i + 1) << UCPTRIE_SHIFT_3;
        }
    }
    return 0;
}

class AllSameBlocks {
public:
    static constexpr int32_t NEW_UNIQUE = -1;
    static constexpr int32_t OVERFLOW = -2;

    AllSameBlocks() : length(0), mostRecent(-1) {}

    int32_t findOrAdd(int32_t index, int32_t count, uint32_t value) {
        if (mostRecent >= 0 && values[mostRecent] == value) {
            refCounts[mostRecent] += count;
            return indexes[mostRecent];
        }
        for (int32_t i = 0; i < length; ++i) {
            if (values[i] == value) {
                mostRecent = i;
                refCounts[i] += count;
                return indexes[i];
            }
        }
        if (length == CAPACITY) {
            return OVERFLOW;
        }
        mostRecent = length;
        indexes[length] = index;
        values[length] = value;
        refCounts[length++] = count;
        return NEW_UNIQUE;
    }

    /** Replaces the block which has the lowest reference count. */
    void add(int32_t index, int32_t count, uint32_t value) {
        U_ASSERT(length == CAPACITY);
        int32_t least = -1;
        int32_t leastCount = I_LIMIT;
        for (int32_t i = 0; i < length; ++i) {
            U_ASSERT(values[i] != value);
            if (refCounts[i] < leastCount) {
                least = i;
                leastCount = refCounts[i];
            }
        }
        U_ASSERT(least >= 0);
        mostRecent = least;
        indexes[least] = index;
        values[least] = value;
        refCounts[least] = count;
    }

    int32_t findMostUsed() const {
        if (length == 0) { return -1; }
        int32_t max = -1;
        int32_t maxCount = 0;
        for (int32_t i = 0; i < length; ++i) {
            if (refCounts[i] > maxCount) {
                max = i;
                maxCount = refCounts[i];
            }
        }
        return indexes[max];
    }

private:
    static constexpr int32_t CAPACITY = 32;

    int32_t length;
    int32_t mostRecent;

    int32_t indexes[CAPACITY];
    uint32_t values[CAPACITY];
    int32_t refCounts[CAPACITY];
};

// Custom hash table for mixed-value blocks to be found anywhere in the
// compacted data or index so far.
class MixedBlocks {
public:
    MixedBlocks() {}
    ~MixedBlocks() {
        uprv_free(table);
    }

    bool init(int32_t maxLength, int32_t newBlockLength) {
        // We store actual data indexes + 1 to reserve 0 for empty entries.
        int32_t maxDataIndex = maxLength - newBlockLength + 1;
        int32_t newLength;
        if (maxDataIndex <= 0xfff) {  // 4k
            newLength = 6007;
            shift = 12;
            mask = 0xfff;
        } else if (maxDataIndex <= 0x7fff) {  // 32k
            newLength = 50021;
            shift = 15;
            mask = 0x7fff;
        } else if (maxDataIndex <= 0x1ffff) {  // 128k
            newLength = 200003;
            shift = 17;
            mask = 0x1ffff;
        } else {
            // maxDataIndex up to around MAX_DATA_LENGTH, ca. 1.1M
            newLength = 1500007;
            shift = 21;
            mask = 0x1fffff;
        }
        if (newLength > capacity) {
            uprv_free(table);
            table = static_cast<uint32_t*>(uprv_malloc(newLength * 4));
            if (table == nullptr) {
                return false;
            }
            capacity = newLength;
        }
        length = newLength;
        uprv_memset(table, 0, length * 4);

        blockLength = newBlockLength;
        return true;
    }

    template<typename UInt>
    void extend(const UInt *data, int32_t minStart, int32_t prevDataLength, int32_t newDataLength) {
        int32_t start = prevDataLength - blockLength;
        if (start >= minStart) {
            ++start;  // Skip the last block that we added last time.
        } else {
            start = minStart;  // Begin with the first full block.
        }
        for (int32_t end = newDataLength - blockLength; start <= end; ++start) {
            uint32_t hashCode = makeHashCode(data, start);
            addEntry(data, start, hashCode, start);
        }
    }

    template<typename UIntA, typename UIntB>
    int32_t findBlock(const UIntA *data, const UIntB *blockData, int32_t blockStart) const {
        uint32_t hashCode = makeHashCode(blockData, blockStart);
        int32_t entryIndex = findEntry(data, blockData, blockStart, hashCode);
        if (entryIndex >= 0) {
            return (table[entryIndex] & mask) - 1;
        } else {
            return -1;
        }
    }

    int32_t findAllSameBlock(const uint32_t *data, uint32_t blockValue) const {
        uint32_t hashCode = makeHashCode(blockValue);
        int32_t entryIndex = findEntry(data, blockValue, hashCode);
        if (entryIndex >= 0) {
            return (table[entryIndex] & mask) - 1;
        } else {
            return -1;
        }
    }

private:
    template<typename UInt>
    uint32_t makeHashCode(const UInt *blockData, int32_t blockStart) const {
        int32_t blockLimit = blockStart + blockLength;
        uint32_t hashCode = blockData[blockStart++];
        do {
            hashCode = 37 * hashCode + blockData[blockStart++];
        } while (blockStart < blockLimit);
        return hashCode;
    }

    uint32_t makeHashCode(uint32_t blockValue) const {
        uint32_t hashCode = blockValue;
        for (int32_t i = 1; i < blockLength; ++i) {
            hashCode = 37 * hashCode + blockValue;
        }
        return hashCode;
    }

    template<typename UInt>
    void addEntry(const UInt *data, int32_t blockStart, uint32_t hashCode, int32_t dataIndex) {
        U_ASSERT(0 <= dataIndex && dataIndex < (int32_t)mask);
        int32_t entryIndex = findEntry(data, data, blockStart, hashCode);
        if (entryIndex < 0) {
            table[~entryIndex] = (hashCode << shift) | (dataIndex + 1);
        }
    }

    template<typename UIntA, typename UIntB>
    int32_t findEntry(const UIntA *data, const UIntB *blockData, int32_t blockStart,
                      uint32_t hashCode) const {
        uint32_t shiftedHashCode = hashCode << shift;
        int32_t initialEntryIndex = (hashCode % (length - 1)) + 1;  // 1..length-1
        for (int32_t entryIndex = initialEntryIndex;;) {
            uint32_t entry = table[entryIndex];
            if (entry == 0) {
                return ~entryIndex;
            }
            if ((entry & ~mask) == shiftedHashCode) {
                int32_t dataIndex = (entry & mask) - 1;
                if (equalBlocks(data + dataIndex, blockData + blockStart, blockLength)) {
                    return entryIndex;
                }
            }
            entryIndex = nextIndex(initialEntryIndex, entryIndex);
        }
    }

    int32_t findEntry(const uint32_t *data, uint32_t blockValue, uint32_t hashCode) const {
        uint32_t shiftedHashCode = hashCode << shift;
        int32_t initialEntryIndex = (hashCode % (length - 1)) + 1;  // 1..length-1
        for (int32_t entryIndex = initialEntryIndex;;) {
            uint32_t entry = table[entryIndex];
            if (entry == 0) {
                return ~entryIndex;
            }
            if ((entry & ~mask) == shiftedHashCode) {
                int32_t dataIndex = (entry & mask) - 1;
                if (allValuesSameAs(data + dataIndex, blockLength, blockValue)) {
                    return entryIndex;
                }
            }
            entryIndex = nextIndex(initialEntryIndex, entryIndex);
        }
    }

    inline int32_t nextIndex(int32_t initialEntryIndex, int32_t entryIndex) const {
        // U_ASSERT(0 < initialEntryIndex && initialEntryIndex < length);
        return (entryIndex + initialEntryIndex) % length;
    }

    // Hash table.
    // The length is a prime number, larger than the maximum data length.
    // The "shift" lower bits store a data index + 1.
    // The remaining upper bits store a partial hashCode of the block data values.
    uint32_t *table = nullptr;
    int32_t capacity = 0;
    int32_t length = 0;
    int32_t shift = 0;
    uint32_t mask = 0;

    int32_t blockLength = 0;
};

int32_t MutableCodePointTrie::compactWholeDataBlocks(int32_t fastILimit, AllSameBlocks &allSameBlocks) {
#ifdef UCPTRIE_DEBUG
    bool overflow = false;
#endif

    // ASCII data will be stored as a linear table, even if the following code
    // does not yet count it that way.
    int32_t newDataCapacity = ASCII_LIMIT;
    // Add room for a small data null block in case it would match the start of
    // a fast data block where dataNullOffset must not be set in that case.
    newDataCapacity += UCPTRIE_SMALL_DATA_BLOCK_LENGTH;
    // Add room for special values (errorValue, highValue) and padding.
    newDataCapacity += 4;
    int32_t iLimit = highStart >> UCPTRIE_SHIFT_3;
    int32_t blockLength = UCPTRIE_FAST_DATA_BLOCK_LENGTH;
    int32_t inc = SMALL_DATA_BLOCKS_PER_BMP_BLOCK;
    for (int32_t i = 0; i < iLimit; i += inc) {
        if (i == fastILimit) {
            blockLength = UCPTRIE_SMALL_DATA_BLOCK_LENGTH;
            inc = 1;
        }
        uint32_t value = index[i];
        if (flags[i] == MIXED) {
            // Really mixed?
            const uint32_t *p = data + value;
            value = *p;
            if (allValuesSameAs(p + 1, blockLength - 1, value)) {
                flags[i] = ALL_SAME;
                index[i] = value;
                // Fall through to ALL_SAME handling.
            } else {
                newDataCapacity += blockLength;
                continue;
            }
        } else {
            U_ASSERT(flags[i] == ALL_SAME);
            if (inc > 1) {
                // Do all of the fast-range data block's ALL_SAME parts have the same value?
                bool allSame = true;
                int32_t next_i = i + inc;
                for (int32_t j = i + 1; j < next_i; ++j) {
                    U_ASSERT(flags[j] == ALL_SAME);
                    if (index[j] != value) {
                        allSame = false;
                        break;
                    }
                }
                if (!allSame) {
                    // Turn it into a MIXED block.
                    if (getDataBlock(i) < 0) {
                        return -1;
                    }
                    newDataCapacity += blockLength;
                    continue;
                }
            }
        }
        // Is there another ALL_SAME block with the same value?
        int32_t other = allSameBlocks.findOrAdd(i, inc, value);
        if (other == AllSameBlocks::OVERFLOW) {
            // The fixed-size array overflowed. Slow check for a duplicate block.
#ifdef UCPTRIE_DEBUG
            if (!overflow) {
                puts("UCPTrie AllSameBlocks overflow");
                overflow = true;
            }
#endif
            int32_t jInc = SMALL_DATA_BLOCKS_PER_BMP_BLOCK;
            for (int32_t j = 0;; j += jInc) {
                if (j == i) {
                    allSameBlocks.add(i, inc, value);
                    break;
                }
                if (j == fastILimit) {
                    jInc = 1;
                }
                if (flags[j] == ALL_SAME && index[j] == value) {
                    allSameBlocks.add(j, jInc + inc, value);
                    other = j;
                    break;
                    // We could keep counting blocks with the same value
                    // before we add the first one, which may improve compaction in rare cases,
                    // but it would make it slower.
                }
            }
        }
        if (other >= 0) {
            flags[i] = SAME_AS;
            index[i] = other;
        } else {
            // New unique same-value block.
            newDataCapacity += blockLength;
        }
    }
    return newDataCapacity;
}

#ifdef UCPTRIE_DEBUG
#   define DEBUG_DO(expr) expr
#else
#   define DEBUG_DO(expr)
#endif

#ifdef UCPTRIE_DEBUG
// Braille symbols: U+28xx = UTF-8 E2 A0 80..E2 A3 BF
int32_t appendValue(char s[], int32_t length, uint32_t value) {
    value ^= value >> 16;
    value ^= value >> 8;
    s[length] = 0xE2;
    s[length + 1] = (char)(0xA0 + ((value >> 6) & 3));
    s[length + 2] = (char)(0x80 + (value & 0x3F));
    return length + 3;
}

void printBlock(const uint32_t *block, int32_t blockLength, uint32_t value,
                UChar32 start, int32_t overlap, uint32_t initialValue) {
    char s[UCPTRIE_FAST_DATA_BLOCK_LENGTH * 3 + 3];
    int32_t length = 0;
    int32_t i;
    for (i = 0; i < overlap; ++i) {
        length = appendValue(s, length, 0);  // Braille blank
    }
    s[length++] = '|';
    for (; i < blockLength; ++i) {
        if (block != nullptr) {
            value = block[i];
        }
        if (value == initialValue) {
            value = 0x40;  // Braille lower left dot
        }
        length = appendValue(s, length, value);
    }
    s[length] = 0;
    start += overlap;
    if (start <= 0xffff) {
        printf("    %04lX  %s|\n", (long)start, s);
    } else if (start <= 0xfffff) {
        printf("   %5lX  %s|\n", (long)start, s);
    } else {
        printf("  %6lX  %s|\n", (long)start, s);
    }
}
#endif

/**
 * Compacts a build-time trie.
 *
 * The compaction
 * - removes blocks that are identical with earlier ones
 * - overlaps each new non-duplicate block as much as possible with the previously-written one
 * - works with fast-range data blocks whose length is a multiple of that of
 *   higher-code-point data blocks
 *
 * It does not try to find an optimal order of writing, deduplicating, and overlapping blocks.
 */
int32_t MutableCodePointTrie::compactData(
        int32_t fastILimit, uint32_t *newData, int32_t newDataCapacity,
        int32_t dataNullIndex, MixedBlocks &mixedBlocks, UErrorCode &errorCode) {
#ifdef UCPTRIE_DEBUG
    int32_t countSame=0, sumOverlaps=0;
    bool printData = dataLength == 29088 /* line.brk */ ||
        // dataLength == 30048 /* CanonIterData */ ||
        dataLength == 50400 /* zh.txt~stroke */;
#endif

    // The linear ASCII data has been copied into newData already.
    int32_t newDataLength = 0;
    for (int32_t i = 0; newDataLength < ASCII_LIMIT;
            newDataLength += UCPTRIE_FAST_DATA_BLOCK_LENGTH, i += SMALL_DATA_BLOCKS_PER_BMP_BLOCK) {
        index[i] = newDataLength;
#ifdef UCPTRIE_DEBUG
        if (printData) {
            printBlock(newData + newDataLength, UCPTRIE_FAST_DATA_BLOCK_LENGTH, 0, newDataLength, 0, initialValue);
        }
#endif
    }

    int32_t blockLength = UCPTRIE_FAST_DATA_BLOCK_LENGTH;
    if (!mixedBlocks.init(newDataCapacity, blockLength)) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        return 0;
    }
    mixedBlocks.extend(newData, 0, 0, newDataLength);

    int32_t iLimit = highStart >> UCPTRIE_SHIFT_3;
    int32_t inc = SMALL_DATA_BLOCKS_PER_BMP_BLOCK;
    int32_t fastLength = 0;
    for (int32_t i = ASCII_I_LIMIT; i < iLimit; i += inc) {
        if (i == fastILimit) {
            blockLength = UCPTRIE_SMALL_DATA_BLOCK_LENGTH;
            inc = 1;
            fastLength = newDataLength;
            if (!mixedBlocks.init(newDataCapacity, blockLength)) {
                errorCode = U_MEMORY_ALLOCATION_ERROR;
                return 0;
            }
            mixedBlocks.extend(newData, 0, 0, newDataLength);
        }
        if (flags[i] == ALL_SAME) {
            uint32_t value = index[i];
            // Find an earlier part of the data array of length blockLength
            // that is filled with this value.
            int32_t n = mixedBlocks.findAllSameBlock(newData, value);
            // If we find a match, and the current block is the data null block,
            // and it is not a fast block but matches the start of a fast block,
            // then we need to continue looking.
            // This is because this small block is shorter than the fast block,
            // and not all of the rest of the fast block is filled with this value.
            // Otherwise trie.getRange() would detect that the fast block starts at
            // dataNullOffset and assume incorrectly that it is filled with the null value.
            while (n >= 0 && i == dataNullIndex && i >= fastILimit && n < fastLength &&
                    isStartOfSomeFastBlock(n, index, fastILimit)) {
                n = findAllSameBlock(newData, n + 1, newDataLength, value, blockLength);
            }
            if (n >= 0) {
                DEBUG_DO(++countSame);
                index[i] = n;
            } else {
                n = getAllSameOverlap(newData, newDataLength, value, blockLength);
                DEBUG_DO(sumOverlaps += n);
#ifdef UCPTRIE_DEBUG
                if (printData) {
                    printBlock(nullptr, blockLength, value, i << UCPTRIE_SHIFT_3, n, initialValue);
                }
#endif
                index[i] = newDataLength - n;
                int32_t prevDataLength = newDataLength;
                while (n < blockLength) {
                    newData[newDataLength++] = value;
                    ++n;
                }
                mixedBlocks.extend(newData, 0, prevDataLength, newDataLength);
            }
        } else if (flags[i] == MIXED) {
            const uint32_t *block = data + index[i];
            int32_t n = mixedBlocks.findBlock(newData, block, 0);
            if (n >= 0) {
                DEBUG_DO(++countSame);
                index[i] = n;
            } else {
                n = getOverlap(newData, newDataLength, block, 0, blockLength);
                DEBUG_DO(sumOverlaps += n);
#ifdef UCPTRIE_DEBUG
                if (printData) {
                    printBlock(block, blockLength, 0, i << UCPTRIE_SHIFT_3, n, initialValue);
                }
#endif
                index[i] = newDataLength - n;
                int32_t prevDataLength = newDataLength;
                while (n < blockLength) {
                    newData[newDataLength++] = block[n++];
                }
                mixedBlocks.extend(newData, 0, prevDataLength, newDataLength);
            }
        } else /* SAME_AS */ {
            uint32_t j = index[i];
            index[i] = index[j];
        }
    }

#ifdef UCPTRIE_DEBUG
    /* we saved some space */
    printf("compacting UCPTrie: count of 32-bit data words %lu->%lu  countSame=%ld  sumOverlaps=%ld\n",
            (long)dataLength, (long)newDataLength, (long)countSame, (long)sumOverlaps);
#endif
    return newDataLength;
}

int32_t MutableCodePointTrie::compactIndex(int32_t fastILimit, MixedBlocks &mixedBlocks,
                                           UErrorCode &errorCode) {
    int32_t fastIndexLength = fastILimit >> (UCPTRIE_FAST_SHIFT - UCPTRIE_SHIFT_3);
    if ((highStart >> UCPTRIE_FAST_SHIFT) <= fastIndexLength) {
        // Only the linear fast index, no multi-stage index tables.
        index3NullOffset = UCPTRIE_NO_INDEX3_NULL_OFFSET;
        return fastIndexLength;
    }

    // Condense the fast index table.
    // Also, does it contain an index-3 block with all dataNullOffset?
    uint16_t fastIndex[UCPTRIE_BMP_INDEX_LENGTH];  // fastIndexLength
    int32_t i3FirstNull = -1;
    for (int32_t i = 0, j = 0; i < fastILimit; ++j) {
        uint32_t i3 = index[i];
        fastIndex[j] = static_cast<uint16_t>(i3);
        if (i3 == static_cast<uint32_t>(dataNullOffset)) {
            if (i3FirstNull < 0) {
                i3FirstNull = j;
            } else if (index3NullOffset < 0 &&
                    (j - i3FirstNull + 1) == UCPTRIE_INDEX_3_BLOCK_LENGTH) {
                index3NullOffset = i3FirstNull;
            }
        } else {
            i3FirstNull = -1;
        }
        // Set the index entries that compactData() skipped.
        // Needed when the multi-stage index covers the fast index range as well.
        int32_t iNext = i + SMALL_DATA_BLOCKS_PER_BMP_BLOCK;
        while (++i < iNext) {
            i3 += UCPTRIE_SMALL_DATA_BLOCK_LENGTH;
            index[i] = i3;
        }
    }

    if (!mixedBlocks.init(fastIndexLength, UCPTRIE_INDEX_3_BLOCK_LENGTH)) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        return 0;
    }
    mixedBlocks.extend(fastIndex, 0, 0, fastIndexLength);

    // Examine index-3 blocks. For each determine one of:
    // - same as the index-3 null block
    // - same as a fast-index block
    // - 16-bit indexes
    // - 18-bit indexes
    // We store this in the first flags entry for the index-3 block.
    //
    // Also determine an upper limit for the index-3 table length.
    int32_t index3Capacity = 0;
    i3FirstNull = index3NullOffset;
    bool hasLongI3Blocks = false;
    // If the fast index covers the whole BMP, then
    // the multi-stage index is only for supplementary code points.
    // Otherwise, the multi-stage index covers all of Unicode.
    int32_t iStart = fastILimit < BMP_I_LIMIT ? 0 : BMP_I_LIMIT;
    int32_t iLimit = highStart >> UCPTRIE_SHIFT_3;
    for (int32_t i = iStart; i < iLimit;) {
        int32_t j = i;
        int32_t jLimit = i + UCPTRIE_INDEX_3_BLOCK_LENGTH;
        uint32_t oredI3 = 0;
        bool isNull = true;
        do {
            uint32_t i3 = index[j];
            oredI3 |= i3;
            if (i3 != static_cast<uint32_t>(dataNullOffset)) {
                isNull = false;
            }
        } while (++j < jLimit);
        if (isNull) {
            flags[i] = I3_NULL;
            if (i3FirstNull < 0) {
                if (oredI3 <= 0xffff) {
                    index3Capacity += UCPTRIE_INDEX_3_BLOCK_LENGTH;
                } else {
                    index3Capacity += INDEX_3_18BIT_BLOCK_LENGTH;
                    hasLongI3Blocks = true;
                }
                i3FirstNull = 0;
            }
        } else {
            if (oredI3 <= 0xffff) {
                int32_t n = mixedBlocks.findBlock(fastIndex, index, i);
                if (n >= 0) {
                    flags[i] = I3_BMP;
                    index[i] = n;
                } else {
                    flags[i] = I3_16;
                    index3Capacity += UCPTRIE_INDEX_3_BLOCK_LENGTH;
                }
            } else {
                flags[i] = I3_18;
                index3Capacity += INDEX_3_18BIT_BLOCK_LENGTH;
                hasLongI3Blocks = true;
            }
        }
        i = j;
    }

    int32_t index2Capacity = (iLimit - iStart) >> UCPTRIE_SHIFT_2_3;

    // Length of the index-1 table, rounded up.
    int32_t index1Length = (index2Capacity + UCPTRIE_INDEX_2_MASK) >> UCPTRIE_SHIFT_1_2;

    // Index table: Fast index, index-1, index-3, index-2.
    // +1 for possible index table padding.
    int32_t index16Capacity = fastIndexLength + index1Length + index3Capacity + index2Capacity + 1;
    index16 = static_cast<uint16_t*>(uprv_malloc(index16Capacity * 2));
    if (index16 == nullptr) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        return 0;
    }
    uprv_memcpy(index16, fastIndex, fastIndexLength * 2);

    if (!mixedBlocks.init(index16Capacity, UCPTRIE_INDEX_3_BLOCK_LENGTH)) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        return 0;
    }
    MixedBlocks longI3Blocks;
    if (hasLongI3Blocks) {
        if (!longI3Blocks.init(index16Capacity, INDEX_3_18BIT_BLOCK_LENGTH)) {
            errorCode = U_MEMORY_ALLOCATION_ERROR;
            return 0;
        }
    }

    // Compact the index-3 table and write an uncompacted version of the index-2 table.
    uint16_t index2[UNICODE_LIMIT >> UCPTRIE_SHIFT_2];  // index2Capacity
    int32_t i2Length = 0;
    i3FirstNull = index3NullOffset;
    int32_t index3Start = fastIndexLength + index1Length;
    int32_t indexLength = index3Start;
    for (int32_t i = iStart; i < iLimit; i += UCPTRIE_INDEX_3_BLOCK_LENGTH) {
        int32_t i3;
        uint8_t f = flags[i];
        if (f == I3_NULL && i3FirstNull < 0) {
            // First index-3 null block. Write & overlap it like a normal block, then remember it.
            f = dataNullOffset <= 0xffff ? I3_16 : I3_18;
            i3FirstNull = 0;
        }
        if (f == I3_NULL) {
            i3 = index3NullOffset;
        } else if (f == I3_BMP) {
            i3 = index[i];
        } else if (f == I3_16) {
            int32_t n = mixedBlocks.findBlock(index16, index, i);
            if (n >= 0) {
                i3 = n;
            } else {
                if (indexLength == index3Start) {
                    // No overlap at the boundary between the index-1 and index-3 tables.
                    n = 0;
                } else {
                    n = getOverlap(index16, indexLength,
                                   index, i, UCPTRIE_INDEX_3_BLOCK_LENGTH);
                }
                i3 = indexLength - n;
                int32_t prevIndexLength = indexLength;
                while (n < UCPTRIE_INDEX_3_BLOCK_LENGTH) {
                    index16[indexLength++] = index[i + n++];
                }
                mixedBlocks.extend(index16, index3Start, prevIndexLength, indexLength);
                if (hasLongI3Blocks) {
                    longI3Blocks.extend(index16, index3Start, prevIndexLength, indexLength);
                }
            }
        } else {
            U_ASSERT(f == I3_18);
            U_ASSERT(hasLongI3Blocks);
            // Encode an index-3 block that contains one or more data indexes exceeding 16 bits.
            int32_t j = i;
            int32_t jLimit = i + UCPTRIE_INDEX_3_BLOCK_LENGTH;
            int32_t k = indexLength;
            do {
                ++k;
                uint32_t v = index[j++];
                uint32_t upperBits = (v & 0x30000) >> 2;
                index16[k++] = v;
                v = index[j++];
                upperBits |= (v & 0x30000) >> 4;
                index16[k++] = v;
                v = index[j++];
                upperBits |= (v & 0x30000) >> 6;
                index16[k++] = v;
                v = index[j++];
                upperBits |= (v & 0x30000) >> 8;
                index16[k++] = v;
                v = index[j++];
                upperBits |= (v & 0x30000) >> 10;
                index16[k++] = v;
                v = index[j++];
                upperBits |= (v & 0x30000) >> 12;
                index16[k++] = v;
                v = index[j++];
                upperBits |= (v & 0x30000) >> 14;
                index16[k++] = v;
                v = index[j++];
                upperBits |= (v & 0x30000) >> 16;
                index16[k++] = v;
                index16[k - 9] = upperBits;
            } while (j < jLimit);
            int32_t n = longI3Blocks.findBlock(index16, index16, indexLength);
            if (n >= 0) {
                i3 = n | 0x8000;
            } else {
                if (indexLength == index3Start) {
                    // No overlap at the boundary between the index-1 and index-3 tables.
                    n = 0;
                } else {
                    n = getOverlap(index16, indexLength,
                                   index16, indexLength, INDEX_3_18BIT_BLOCK_LENGTH);
                }
                i3 = (indexLength - n) | 0x8000;
                int32_t prevIndexLength = indexLength;
                if (n > 0) {
                    int32_t start = indexLength;
                    while (n < INDEX_3_18BIT_BLOCK_LENGTH) {
                        index16[indexLength++] = index16[start + n++];
                    }
                } else {
                    indexLength += INDEX_3_18BIT_BLOCK_LENGTH;
                }
                mixedBlocks.extend(index16, index3Start, prevIndexLength, indexLength);
                if (hasLongI3Blocks) {
                    longI3Blocks.extend(index16, index3Start, prevIndexLength, indexLength);
                }
            }
        }
        if (index3NullOffset < 0 && i3FirstNull >= 0) {
            index3NullOffset = i3;
        }
        // Set the index-2 table entry.
        index2[i2Length++] = i3;
    }
    U_ASSERT(i2Length == index2Capacity);
    U_ASSERT(indexLength <= index3Start + index3Capacity);

    if (index3NullOffset < 0) {
        index3NullOffset = UCPTRIE_NO_INDEX3_NULL_OFFSET;
    }
    if (indexLength >= (UCPTRIE_NO_INDEX3_NULL_OFFSET + UCPTRIE_INDEX_3_BLOCK_LENGTH)) {
        // The index-3 offsets exceed 15 bits, or
        // the last one cannot be distinguished from the no-null-block value.
        errorCode = U_INDEX_OUTOFBOUNDS_ERROR;
        return 0;
    }

    // Compact the index-2 table and write the index-1 table.
    static_assert(UCPTRIE_INDEX_2_BLOCK_LENGTH == UCPTRIE_INDEX_3_BLOCK_LENGTH,
                  "must re-init mixedBlocks");
    int32_t blockLength = UCPTRIE_INDEX_2_BLOCK_LENGTH;
    int32_t i1 = fastIndexLength;
    for (int32_t i = 0; i < i2Length; i += blockLength) {
        int32_t n;
        if ((i2Length - i) >= blockLength) {
            // normal block
            U_ASSERT(blockLength == UCPTRIE_INDEX_2_BLOCK_LENGTH);
            n = mixedBlocks.findBlock(index16, index2, i);
        } else {
            // highStart is inside the last index-2 block. Shorten it.
            blockLength = i2Length - i;
            n = findSameBlock(index16, index3Start, indexLength,
                              index2, i, blockLength);
        }
        int32_t i2;
        if (n >= 0) {
            i2 = n;
        } else {
            if (indexLength == index3Start) {
                // No overlap at the boundary between the index-1 and index-3/2 tables.
                n = 0;
            } else {
                n = getOverlap(index16, indexLength, index2, i, blockLength);
            }
            i2 = indexLength - n;
            int32_t prevIndexLength = indexLength;
            while (n < blockLength) {
                index16[indexLength++] = index2[i + n++];
            }
            mixedBlocks.extend(index16, index3Start, prevIndexLength, indexLength);
        }
        // Set the index-1 table entry.
        index16[i1++] = i2;
    }
    U_ASSERT(i1 == index3Start);
    U_ASSERT(indexLength <= index16Capacity);

#ifdef UCPTRIE_DEBUG
    /* we saved some space */
    printf("compacting UCPTrie: count of 16-bit index words %lu->%lu\n",
            (long)iLimit, (long)indexLength);
#endif

    return indexLength;
}

int32_t MutableCodePointTrie::compactTrie(int32_t fastILimit, UErrorCode &errorCode) {
    // Find the real highStart and round it up.
    U_ASSERT((highStart & (UCPTRIE_CP_PER_INDEX_2_ENTRY - 1)) == 0);
    highValue = get(MAX_UNICODE);
    int32_t realHighStart = findHighStart();
    realHighStart = (realHighStart + (UCPTRIE_CP_PER_INDEX_2_ENTRY - 1)) &
        ~(UCPTRIE_CP_PER_INDEX_2_ENTRY - 1);
    if (realHighStart == UNICODE_LIMIT) {
        highValue = initialValue;
    }

#ifdef UCPTRIE_DEBUG
    printf("UCPTrie: highStart U+%06lx  highValue 0x%lx  initialValue 0x%lx\n",
            (long)realHighStart, (long)highValue, (long)initialValue);
#endif

    // We always store indexes and data values for the fast range.
    // Pin highStart to the top of that range while building.
    UChar32 fastLimit = fastILimit << UCPTRIE_SHIFT_3;
    if (realHighStart < fastLimit) {
        for (int32_t i = (realHighStart >> UCPTRIE_SHIFT_3); i < fastILimit; ++i) {
            flags[i] = ALL_SAME;
            index[i] = highValue;
        }
        highStart = fastLimit;
    } else {
        highStart = realHighStart;
    }

    uint32_t asciiData[ASCII_LIMIT];
    for (int32_t i = 0; i < ASCII_LIMIT; ++i) {
        asciiData[i] = get(i);
    }

    // First we look for which data blocks have the same value repeated over the whole block,
    // deduplicate such blocks, find a good null data block (for faster enumeration),
    // and get an upper bound for the necessary data array length.
    AllSameBlocks allSameBlocks;
    int32_t newDataCapacity = compactWholeDataBlocks(fastILimit, allSameBlocks);
    if (newDataCapacity < 0) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        return 0;
    }
    uint32_t* newData = static_cast<uint32_t*>(uprv_malloc(newDataCapacity * 4));
    if (newData == nullptr) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        return 0;
    }
    uprv_memcpy(newData, asciiData, sizeof(asciiData));

    int32_t dataNullIndex = allSameBlocks.findMostUsed();

    MixedBlocks mixedBlocks;
    int32_t newDataLength = compactData(fastILimit, newData, newDataCapacity,
                                        dataNullIndex, mixedBlocks, errorCode);
    if (U_FAILURE(errorCode)) { return 0; }
    U_ASSERT(newDataLength <= newDataCapacity);
    uprv_free(data);
    data = newData;
    dataCapacity = newDataCapacity;
    dataLength = newDataLength;
    if (dataLength > (0x3ffff + UCPTRIE_SMALL_DATA_BLOCK_LENGTH)) {
        // The offset of the last data block is too high to be stored in the index table.
        errorCode = U_INDEX_OUTOFBOUNDS_ERROR;
        return 0;
    }

    if (dataNullIndex >= 0) {
        dataNullOffset = index[dataNullIndex];
#ifdef UCPTRIE_DEBUG
        if (data[dataNullOffset] != initialValue) {
            printf("UCPTrie initialValue %lx -> more common nullValue %lx\n",
                   (long)initialValue, (long)data[dataNullOffset]);
        }
#endif
        initialValue = data[dataNullOffset];
    } else {
        dataNullOffset = UCPTRIE_NO_DATA_NULL_OFFSET;
    }

    int32_t indexLength = compactIndex(fastILimit, mixedBlocks, errorCode);
    highStart = realHighStart;
    return indexLength;
}

UCPTrie *MutableCodePointTrie::build(UCPTrieType type, UCPTrieValueWidth valueWidth, UErrorCode &errorCode) {
    if (U_FAILURE(errorCode)) {
        return nullptr;
    }
    if (type < UCPTRIE_TYPE_FAST || UCPTRIE_TYPE_SMALL < type ||
            valueWidth < UCPTRIE_VALUE_BITS_16 || UCPTRIE_VALUE_BITS_8 < valueWidth) {
        errorCode = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }

    // The mutable trie always stores 32-bit values.
    // When we build a UCPTrie for a smaller value width, we first mask off unused bits
    // before compacting the data.
    switch (valueWidth) {
    case UCPTRIE_VALUE_BITS_32:
        break;
    case UCPTRIE_VALUE_BITS_16:
        maskValues(0xffff);
        break;
    case UCPTRIE_VALUE_BITS_8:
        maskValues(0xff);
        break;
    default:
        break;
    }

    UChar32 fastLimit = type == UCPTRIE_TYPE_FAST ? BMP_LIMIT : UCPTRIE_SMALL_LIMIT;
    int32_t indexLength = compactTrie(fastLimit >> UCPTRIE_SHIFT_3, errorCode);
    if (U_FAILURE(errorCode)) {
        clear();
        return nullptr;
    }

    // Ensure data table alignment: The index length must be even for uint32_t data.
    if (valueWidth == UCPTRIE_VALUE_BITS_32 && (indexLength & 1) != 0) {
        index16[indexLength++] = 0xffee;  // arbitrary value
    }

    // Make the total trie structure length a multiple of 4 bytes by padding the data table,
    // and store special values as the last two data values.
    int32_t length = indexLength * 2;
    if (valueWidth == UCPTRIE_VALUE_BITS_16) {
        if (((indexLength ^ dataLength) & 1) != 0) {
            // padding
            data[dataLength++] = errorValue;
        }
        if (data[dataLength - 1] != errorValue || data[dataLength - 2] != highValue) {
            data[dataLength++] = highValue;
            data[dataLength++] = errorValue;
        }
        length += dataLength * 2;
    } else if (valueWidth == UCPTRIE_VALUE_BITS_32) {
        // 32-bit data words never need padding to a multiple of 4 bytes.
        if (data[dataLength - 1] != errorValue || data[dataLength - 2] != highValue) {
            if (data[dataLength - 1] != highValue) {
                data[dataLength++] = highValue;
            }
            data[dataLength++] = errorValue;
        }
        length += dataLength * 4;
    } else {
        int32_t and3 = (length + dataLength) & 3;
        if (and3 == 0 && data[dataLength - 1] == errorValue && data[dataLength - 2] == highValue) {
            // all set
        } else if(and3 == 3 && data[dataLength - 1] == highValue) {
            data[dataLength++] = errorValue;
        } else {
            while (and3 != 2) {
                data[dataLength++] = highValue;
                and3 = (and3 + 1) & 3;
            }
            data[dataLength++] = highValue;
            data[dataLength++] = errorValue;
        }
        length += dataLength;
    }

    // Calculate the total length of the UCPTrie as a single memory block.
    length += sizeof(UCPTrie);
    U_ASSERT((length & 3) == 0);

    uint8_t* bytes = static_cast<uint8_t*>(uprv_malloc(length));
    if (bytes == nullptr) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        clear();
        return nullptr;
    }
    UCPTrie *trie = reinterpret_cast<UCPTrie *>(bytes);
    uprv_memset(trie, 0, sizeof(UCPTrie));
    trie->indexLength = indexLength;
    trie->dataLength = dataLength;

    trie->highStart = highStart;
    // Round up shifted12HighStart to a multiple of 0x1000 for easy testing from UTF-8 lead bytes.
    // Runtime code needs to then test for the real highStart as well.
    trie->shifted12HighStart = (highStart + 0xfff) >> 12;
    trie->type = type;
    trie->valueWidth = valueWidth;

    trie->index3NullOffset = index3NullOffset;
    trie->dataNullOffset = dataNullOffset;
    trie->nullValue = initialValue;

    bytes += sizeof(UCPTrie);

    // Fill the index and data arrays.
    uint16_t* dest16 = reinterpret_cast<uint16_t*>(bytes);
    trie->index = dest16;

    if (highStart <= fastLimit) {
        // Condense only the fast index from the mutable-trie index.
        for (int32_t i = 0, j = 0; j < indexLength; i += SMALL_DATA_BLOCKS_PER_BMP_BLOCK, ++j) {
            *dest16++ = static_cast<uint16_t>(index[i]); // dest16[j]
        }
    } else {
        uprv_memcpy(dest16, index16, indexLength * 2);
        dest16 += indexLength;
    }
    bytes += indexLength * 2;

    // Write the data array.
    const uint32_t *p = data;
    switch (valueWidth) {
    case UCPTRIE_VALUE_BITS_16:
        // Write 16-bit data values.
        trie->data.ptr16 = dest16;
        for (int32_t i = dataLength; i > 0; --i) {
            *dest16++ = static_cast<uint16_t>(*p++);
        }
        break;
    case UCPTRIE_VALUE_BITS_32:
        // Write 32-bit data values.
        trie->data.ptr32 = reinterpret_cast<uint32_t*>(bytes);
        uprv_memcpy(bytes, p, (size_t)dataLength * 4);
        break;
    case UCPTRIE_VALUE_BITS_8:
        // Write 8-bit data values.
        trie->data.ptr8 = bytes;
        for (int32_t i = dataLength; i > 0; --i) {
            *bytes++ = static_cast<uint8_t>(*p++);
        }
        break;
    default:
        // Will not occur, valueWidth checked at the beginning.
        break;
    }

#ifdef UCPTRIE_DEBUG
    trie->name = name;

    ucptrie_printLengths(trie, "");
#endif

    clear();
    return trie;
}

}  // namespace

U_NAMESPACE_END

U_NAMESPACE_USE

U_CAPI UMutableCPTrie * U_EXPORT2
umutablecptrie_open(uint32_t initialValue, uint32_t errorValue, UErrorCode *pErrorCode) {
    if (U_FAILURE(*pErrorCode)) {
        return nullptr;
    }
    LocalPointer<MutableCodePointTrie> trie(
        new MutableCodePointTrie(initialValue, errorValue, *pErrorCode), *pErrorCode);
    if (U_FAILURE(*pErrorCode)) {
        return nullptr;
    }
    return reinterpret_cast<UMutableCPTrie *>(trie.orphan());
}

U_CAPI UMutableCPTrie * U_EXPORT2
umutablecptrie_clone(const UMutableCPTrie *other, UErrorCode *pErrorCode) {
    if (U_FAILURE(*pErrorCode)) {
        return nullptr;
    }
    if (other == nullptr) {
        return nullptr;
    }
    LocalPointer<MutableCodePointTrie> clone(
        new MutableCodePointTrie(*reinterpret_cast<const MutableCodePointTrie *>(other), *pErrorCode), *pErrorCode);
    if (U_FAILURE(*pErrorCode)) {
        return nullptr;
    }
    return reinterpret_cast<UMutableCPTrie *>(clone.orphan());
}

U_CAPI void U_EXPORT2
umutablecptrie_close(UMutableCPTrie *trie) {
    delete reinterpret_cast<MutableCodePointTrie *>(trie);
}

U_CAPI UMutableCPTrie * U_EXPORT2
umutablecptrie_fromUCPMap(const UCPMap *map, UErrorCode *pErrorCode) {
    if (U_FAILURE(*pErrorCode)) {
        return nullptr;
    }
    if (map == nullptr) {
        *pErrorCode = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }
    return reinterpret_cast<UMutableCPTrie *>(MutableCodePointTrie::fromUCPMap(map, *pErrorCode));
}

U_CAPI UMutableCPTrie * U_EXPORT2
umutablecptrie_fromUCPTrie(const UCPTrie *trie, UErrorCode *pErrorCode) {
    if (U_FAILURE(*pErrorCode)) {
        return nullptr;
    }
    if (trie == nullptr) {
        *pErrorCode = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }
    return reinterpret_cast<UMutableCPTrie *>(MutableCodePointTrie::fromUCPTrie(trie, *pErrorCode));
}

U_CAPI uint32_t U_EXPORT2
umutablecptrie_get(const UMutableCPTrie *trie, UChar32 c) {
    return reinterpret_cast<const MutableCodePointTrie *>(trie)->get(c);
}

namespace {

UChar32 getRange(const void *trie, UChar32 start,
                 UCPMapValueFilter *filter, const void *context, uint32_t *pValue) {
    return reinterpret_cast<const MutableCodePointTrie *>(trie)->
        getRange(start, filter, context, pValue);
}

}  // namespace

U_CAPI UChar32 U_EXPORT2
umutablecptrie_getRange(const UMutableCPTrie *trie, UChar32 start,
                        UCPMapRangeOption option, uint32_t surrogateValue,
                        UCPMapValueFilter *filter, const void *context, uint32_t *pValue) {
    return ucptrie_internalGetRange(getRange, trie, start,
                                    option, surrogateValue,
                                    filter, context, pValue);
}

U_CAPI void U_EXPORT2
umutablecptrie_set(UMutableCPTrie *trie, UChar32 c, uint32_t value, UErrorCode *pErrorCode) {
    if (U_FAILURE(*pErrorCode)) {
        return;
    }
    reinterpret_cast<MutableCodePointTrie *>(trie)->set(c, value, *pErrorCode);
}

U_CAPI void U_EXPORT2
umutablecptrie_setRange(UMutableCPTrie *trie, UChar32 start, UChar32 end,
                   uint32_t value, UErrorCode *pErrorCode) {
    if (U_FAILURE(*pErrorCode)) {
        return;
    }
    reinterpret_cast<MutableCodePointTrie *>(trie)->setRange(start, end, value, *pErrorCode);
}

/* Compact and internally serialize the trie. */
U_CAPI UCPTrie * U_EXPORT2
umutablecptrie_buildImmutable(UMutableCPTrie *trie, UCPTrieType type, UCPTrieValueWidth valueWidth,
                              UErrorCode *pErrorCode) {
    if (U_FAILURE(*pErrorCode)) {
        return nullptr;
    }
    return reinterpret_cast<MutableCodePointTrie *>(trie)->build(type, valueWidth, *pErrorCode);
}

#ifdef UCPTRIE_DEBUG
U_CFUNC void umutablecptrie_setName(UMutableCPTrie *trie, const char *name) {
    reinterpret_cast<MutableCodePointTrie *>(trie)->name = name;
}
#endif
