// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2013-2015, International Business Machines
* Corporation and others.  All Rights Reserved.
*******************************************************************************
* collationinfo.cpp
*
* created on: 2013aug05
* created by: Markus W. Scherer
*/

#include <stdio.h>
#include <string.h>

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

#include "collationdata.h"
#include "collationdatareader.h"
#include "collationinfo.h"
#include "uassert.h"
#include "uvectr32.h"

U_NAMESPACE_BEGIN

void
CollationInfo::printSizes(int32_t sizeWithHeader, const int32_t indexes[]) {
    int32_t totalSize = indexes[CollationDataReader::IX_TOTAL_SIZE];
    if(sizeWithHeader > totalSize) {
        printf("  header size:                  %6ld\n", static_cast<long>(sizeWithHeader - totalSize));
    }

    int32_t length = indexes[CollationDataReader::IX_INDEXES_LENGTH];
    printf("  indexes:          %6ld *4 = %6ld\n", static_cast<long>(length), static_cast<long>(length) * 4);

    length = getDataLength(indexes, CollationDataReader::IX_REORDER_CODES_OFFSET);
    if(length != 0) {
        printf("  reorder codes:    %6ld *4 = %6ld\n", static_cast<long>(length) / 4, static_cast<long>(length));
    }

    length = getDataLength(indexes, CollationDataReader::IX_REORDER_TABLE_OFFSET);
    if(length != 0) {
        U_ASSERT(length >= 256);
        printf("  reorder table:                %6ld\n", static_cast<long>(length));
    }

    length = getDataLength(indexes, CollationDataReader::IX_TRIE_OFFSET);
    if(length != 0) {
        printf("  trie size:                    %6ld\n", static_cast<long>(length));
    }

    length = getDataLength(indexes, CollationDataReader::IX_RESERVED8_OFFSET);
    if(length != 0) {
        printf("  reserved (offset 8):          %6ld\n", static_cast<long>(length));
    }

    length = getDataLength(indexes, CollationDataReader::IX_CES_OFFSET);
    if(length != 0) {
        printf("  CEs:              %6ld *8 = %6ld\n", static_cast<long>(length) / 8, static_cast<long>(length));
    }

    length = getDataLength(indexes, CollationDataReader::IX_RESERVED10_OFFSET);
    if(length != 0) {
        printf("  reserved (offset 10):         %6ld\n", static_cast<long>(length));
    }

    length = getDataLength(indexes, CollationDataReader::IX_CE32S_OFFSET);
    if(length != 0) {
        printf("  CE32s:            %6ld *4 = %6ld\n", static_cast<long>(length) / 4, static_cast<long>(length));
    }

    length = getDataLength(indexes, CollationDataReader::IX_ROOT_ELEMENTS_OFFSET);
    if(length != 0) {
        printf("  rootElements:     %6ld *4 = %6ld\n", static_cast<long>(length) / 4, static_cast<long>(length));
    }

    length = getDataLength(indexes, CollationDataReader::IX_CONTEXTS_OFFSET);
    if(length != 0) {
        printf("  contexts:         %6ld *2 = %6ld\n", static_cast<long>(length) / 2, static_cast<long>(length));
    }

    length = getDataLength(indexes, CollationDataReader::IX_UNSAFE_BWD_OFFSET);
    if(length != 0) {
        printf("  unsafeBwdSet:     %6ld *2 = %6ld\n", static_cast<long>(length) / 2, static_cast<long>(length));
    }

    length = getDataLength(indexes, CollationDataReader::IX_FAST_LATIN_TABLE_OFFSET);
    if(length != 0) {
        printf("  fastLatin table:  %6ld *2 = %6ld\n", static_cast<long>(length) / 2, static_cast<long>(length));
    }

    length = getDataLength(indexes, CollationDataReader::IX_SCRIPTS_OFFSET);
    if(length != 0) {
        printf("  scripts data:     %6ld *2 = %6ld\n", static_cast<long>(length) / 2, static_cast<long>(length));
    }

    length = getDataLength(indexes, CollationDataReader::IX_COMPRESSIBLE_BYTES_OFFSET);
    if(length != 0) {
        U_ASSERT(length >= 256);
        printf("  compressibleBytes:            %6ld\n", static_cast<long>(length));
    }

    length = getDataLength(indexes, CollationDataReader::IX_RESERVED18_OFFSET);
    if(length != 0) {
        printf("  reserved (offset 18):         %6ld\n", static_cast<long>(length));
    }

    printf(" collator binary total size:    %6ld\n", static_cast<long>(sizeWithHeader));
}

int32_t
CollationInfo::getDataLength(const int32_t indexes[], int32_t startIndex) {
    return indexes[startIndex + 1] - indexes[startIndex];
}

void
CollationInfo::printReorderRanges(const CollationData &data, const int32_t *codes, int32_t length) {
    UErrorCode errorCode = U_ZERO_ERROR;
    UVector32 ranges(errorCode);
    data.makeReorderRanges(codes, length, ranges, errorCode);
    if(U_FAILURE(errorCode)) {
        printf("  error building reorder ranges: %s\n", u_errorName(errorCode));
        return;
    }

    int32_t start = 0;
    for(int32_t i = 0; i < ranges.size(); ++i) {
        int32_t pair = ranges.elementAti(i);
        int32_t limit = (pair >> 16) & 0xffff;
        int16_t offset = static_cast<int16_t>(pair);
        if(offset == 0) {
            // [inclusive-start, exclusive-limit[
            printf("          [%04x, %04x[\n", start, limit);
        } else if(offset > 0) {
            printf("  reorder [%04x, %04x[ by offset  %02x to [%04x, %04x[\n",
                    start, limit, offset,
                    start + (offset << 8), limit + (offset << 8));
        } else /* offset < 0 */ {
            printf("  reorder [%04x, %04x[ by offset -%02x to [%04x, %04x[\n",
                    start, limit, -offset,
                    start + (offset << 8), limit + (offset << 8));
        }
        start = limit;
    }
}

U_NAMESPACE_END

#endif  // !UCONFIG_NO_COLLATION
