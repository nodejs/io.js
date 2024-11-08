// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
***************************************************************************
*   Copyright (C) 1999-2014 International Business Machines Corporation   *
*   and others. All rights reserved.                                      *
***************************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_BREAK_ITERATION

#include "unicode/ucptrie.h"
#include "unicode/utypes.h"
#include "rbbidata.h"
#include "rbbirb.h"
#include "udatamem.h"
#include "cmemory.h"
#include "cstring.h"
#include "umutex.h"

#include "uassert.h"


U_NAMESPACE_BEGIN

//-----------------------------------------------------------------------------
//
//    Constructors.
//
//-----------------------------------------------------------------------------
RBBIDataWrapper::RBBIDataWrapper(const RBBIDataHeader *data, UErrorCode &status) {
    init0();
    init(data, status);
}

RBBIDataWrapper::RBBIDataWrapper(const RBBIDataHeader *data, enum EDontAdopt, UErrorCode &status) {
    init0();
    init(data, status);
    fDontFreeData = true;
}

RBBIDataWrapper::RBBIDataWrapper(UDataMemory* udm, UErrorCode &status) {
    init0();
    if (U_FAILURE(status)) {
        return;
    }
    const DataHeader *dh = udm->pHeader;
    int32_t headerSize = dh->dataHeader.headerSize;
    if (  !(headerSize >= 20 &&
            dh->info.isBigEndian == U_IS_BIG_ENDIAN &&
            dh->info.charsetFamily == U_CHARSET_FAMILY &&
            dh->info.dataFormat[0] == 0x42 &&  // dataFormat="Brk "
            dh->info.dataFormat[1] == 0x72 &&
            dh->info.dataFormat[2] == 0x6b &&
            dh->info.dataFormat[3] == 0x20 &&
            isDataVersionAcceptable(dh->info.formatVersion))
        ) {
        status = U_INVALID_FORMAT_ERROR;
        return;
    }
    const char *dataAsBytes = reinterpret_cast<const char *>(dh);
    const RBBIDataHeader *rbbidh = reinterpret_cast<const RBBIDataHeader *>(dataAsBytes + headerSize);
    init(rbbidh, status);
    fUDataMem = udm;
}

UBool RBBIDataWrapper::isDataVersionAcceptable(const UVersionInfo version) {
    return RBBI_DATA_FORMAT_VERSION[0] == version[0];
}


//-----------------------------------------------------------------------------
//
//    init().   Does most of the work of construction, shared between the
//              constructors.
//
//-----------------------------------------------------------------------------
void RBBIDataWrapper::init0() {
    fHeader = nullptr;
    fForwardTable = nullptr;
    fReverseTable = nullptr;
    fRuleSource   = nullptr;
    fRuleStatusTable = nullptr;
    fTrie         = nullptr;
    fUDataMem     = nullptr;
    fRefCount     = 0;
    fDontFreeData = true;
}

void RBBIDataWrapper::init(const RBBIDataHeader *data, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return;
    }
    fHeader = data;
    if (fHeader->fMagic != 0xb1a0 || !isDataVersionAcceptable(fHeader->fFormatVersion)) {
        status = U_INVALID_FORMAT_ERROR;
        return;
    }
    // Note: in ICU version 3.2 and earlier, there was a formatVersion 1
    //       that is no longer supported.  At that time fFormatVersion was
    //       an int32_t field, rather than an array of 4 bytes.

    fDontFreeData = false;
    if (data->fFTableLen != 0) {
        fForwardTable = reinterpret_cast<const RBBIStateTable*>(reinterpret_cast<const char*>(data) + fHeader->fFTable);
    }
    if (data->fRTableLen != 0) {
        fReverseTable = reinterpret_cast<const RBBIStateTable*>(reinterpret_cast<const char*>(data) + fHeader->fRTable);
    }

    fTrie = ucptrie_openFromBinary(UCPTRIE_TYPE_FAST,
                                   UCPTRIE_VALUE_BITS_ANY,
                                   (uint8_t *)data + fHeader->fTrie,
                                   fHeader->fTrieLen,
                                   nullptr,           // *actual length
                                   &status);
    if (U_FAILURE(status)) {
        return;
    }

    UCPTrieValueWidth width = ucptrie_getValueWidth(fTrie);
    if (!(width == UCPTRIE_VALUE_BITS_8 || width == UCPTRIE_VALUE_BITS_16)) {
        status = U_INVALID_FORMAT_ERROR;
        return;
    }

    fRuleSource   = ((char *)data + fHeader->fRuleSource);
    fRuleString = UnicodeString::fromUTF8(StringPiece(fRuleSource, fHeader->fRuleSourceLen));
    U_ASSERT(data->fRuleSourceLen > 0);

    fRuleStatusTable = reinterpret_cast<const int32_t*>(reinterpret_cast<const char*>(data) + fHeader->fStatusTable);
    fStatusMaxIdx    = data->fStatusTableLen / sizeof(int32_t);

    fRefCount = 1;

#ifdef RBBI_DEBUG
    char *debugEnv = getenv("U_RBBIDEBUG");
    if (debugEnv && uprv_strstr(debugEnv, "data")) {this->printData();}
#endif
}


//-----------------------------------------------------------------------------
//
//    Destructor.     Don't call this - use removeReference() instead.
//
//-----------------------------------------------------------------------------
RBBIDataWrapper::~RBBIDataWrapper() {
    U_ASSERT(fRefCount == 0);
    ucptrie_close(fTrie);
    fTrie = nullptr;
    if (fUDataMem) {
        udata_close(fUDataMem);
    } else if (!fDontFreeData) {
        uprv_free((void *)fHeader);
    }
}



//-----------------------------------------------------------------------------
//
//   Operator ==    Consider two RBBIDataWrappers to be equal if they
//                  refer to the same underlying data.  Although
//                  the data wrappers are normally shared between
//                  iterator instances, it's possible to independently
//                  open the same data twice, and get two instances, which
//                  should still be ==.
//
//-----------------------------------------------------------------------------
bool RBBIDataWrapper::operator ==(const RBBIDataWrapper &other) const {
    if (fHeader == other.fHeader) {
        return true;
    }
    if (fHeader->fLength != other.fHeader->fLength) {
        return false;
    }
    if (uprv_memcmp(fHeader, other.fHeader, fHeader->fLength) == 0) {
        return true;
    }
    return false;
}

int32_t  RBBIDataWrapper::hashCode() {
    return fHeader->fFTableLen;
}



//-----------------------------------------------------------------------------
//
//    Reference Counting.   A single RBBIDataWrapper object is shared among
//                          however many RulesBasedBreakIterator instances are
//                          referencing the same data.
//
//-----------------------------------------------------------------------------
void RBBIDataWrapper::removeReference() {
    if (umtx_atomic_dec(&fRefCount) == 0) {
        delete this;
    }
}


RBBIDataWrapper *RBBIDataWrapper::addReference() {
   umtx_atomic_inc(&fRefCount);
   return this;
}



//-----------------------------------------------------------------------------
//
//  getRuleSourceString
//
//-----------------------------------------------------------------------------
const UnicodeString &RBBIDataWrapper::getRuleSourceString() const {
    return fRuleString;
}


//-----------------------------------------------------------------------------
//
//  print   -  debugging function to dump the runtime data tables.
//
//-----------------------------------------------------------------------------
#ifdef RBBI_DEBUG
void  RBBIDataWrapper::printTable(const char *heading, const RBBIStateTable *table) {
    uint32_t   c;
    uint32_t   s;

    RBBIDebugPrintf("%s\n", heading);

    RBBIDebugPrintf("   fDictCategoriesStart: %d\n", table->fDictCategoriesStart);
    RBBIDebugPrintf("   fLookAheadResultsSize: %d\n", table->fLookAheadResultsSize);
    RBBIDebugPrintf("   Flags: %4x RBBI_LOOKAHEAD_HARD_BREAK=%s RBBI_BOF_REQUIRED=%s  RBBI_8BITS_ROWS=%s\n",
                    table->fFlags,
                    table->fFlags & RBBI_LOOKAHEAD_HARD_BREAK ? "T" : "F",
                    table->fFlags & RBBI_BOF_REQUIRED ? "T" : "F",
                    table->fFlags & RBBI_8BITS_ROWS ? "T" : "F");
    RBBIDebugPrintf("\nState |  Acc  LA TagIx");
    for (c=0; c<fHeader->fCatCount; c++) {RBBIDebugPrintf("%3d ", c);}
    RBBIDebugPrintf("\n------|---------------"); for (c=0;c<fHeader->fCatCount; c++) {
        RBBIDebugPrintf("----");
    }
    RBBIDebugPrintf("\n");

    if (table == nullptr) {
        RBBIDebugPrintf("         N U L L   T A B L E\n\n");
        return;
    }
    UBool use8Bits = table->fFlags & RBBI_8BITS_ROWS;
    for (s=0; s<table->fNumStates; s++) {
        RBBIStateTableRow *row = (RBBIStateTableRow *)
                                  (table->fTableData + (table->fRowLen * s));
        if (use8Bits) {
            RBBIDebugPrintf("%4d  |  %3d %3d %3d ", s, row->r8.fAccepting, row->r8.fLookAhead, row->r8.fTagsIdx);
            for (c=0; c<fHeader->fCatCount; c++)  {
                RBBIDebugPrintf("%3d ", row->r8.fNextState[c]);
            }
        } else {
            RBBIDebugPrintf("%4d  |  %3d %3d %3d ", s, row->r16.fAccepting, row->r16.fLookAhead, row->r16.fTagsIdx);
            for (c=0; c<fHeader->fCatCount; c++)  {
                RBBIDebugPrintf("%3d ", row->r16.fNextState[c]);
            }
        }
        RBBIDebugPrintf("\n");
    }
    RBBIDebugPrintf("\n");
}
#endif


void  RBBIDataWrapper::printData() {
#ifdef RBBI_DEBUG
    RBBIDebugPrintf("RBBI Data at %p\n", (void *)fHeader);
    RBBIDebugPrintf("   Version = {%d %d %d %d}\n", fHeader->fFormatVersion[0], fHeader->fFormatVersion[1],
                                                    fHeader->fFormatVersion[2], fHeader->fFormatVersion[3]);
    RBBIDebugPrintf("   total length of data  = %d\n", fHeader->fLength);
    RBBIDebugPrintf("   number of character categories = %d\n\n", fHeader->fCatCount);

    printTable("Forward State Transition Table", fForwardTable);
    printTable("Reverse State Transition Table", fReverseTable);

    RBBIDebugPrintf("\nOriginal Rules source:\n");
    for (int32_t c=0; fRuleSource[c] != 0; c++) {
        RBBIDebugPrintf("%c", fRuleSource[c]);
    }
    RBBIDebugPrintf("\n\n");
#endif
}


U_NAMESPACE_END
U_NAMESPACE_USE

//-----------------------------------------------------------------------------
//
//  ubrk_swap   -  byte swap and char encoding swap of RBBI data
//
//-----------------------------------------------------------------------------

U_CAPI int32_t U_EXPORT2
ubrk_swap(const UDataSwapper *ds, const void *inData, int32_t length, void *outData,
           UErrorCode *status) {

    if (status == nullptr || U_FAILURE(*status)) {
        return 0;
    }
    if(ds==nullptr || inData==nullptr || length<-1 || (length>0 && outData==nullptr)) {
        *status=U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }

    //
    //  Check that the data header is for for break data.
    //    (Header contents are defined in genbrk.cpp)
    //
    const UDataInfo *pInfo = (const UDataInfo *)((const char *)inData+4);
    if(!(  pInfo->dataFormat[0]==0x42 &&   /* dataFormat="Brk " */
           pInfo->dataFormat[1]==0x72 &&
           pInfo->dataFormat[2]==0x6b &&
           pInfo->dataFormat[3]==0x20 &&
           RBBIDataWrapper::isDataVersionAcceptable(pInfo->formatVersion) )) {
        udata_printError(ds, "ubrk_swap(): data format %02x.%02x.%02x.%02x (format version %02x) is not recognized\n",
                         pInfo->dataFormat[0], pInfo->dataFormat[1],
                         pInfo->dataFormat[2], pInfo->dataFormat[3],
                         pInfo->formatVersion[0]);
        *status=U_UNSUPPORTED_ERROR;
        return 0;
    }

    //
    // Swap the data header.  (This is the generic ICU Data Header, not the RBBI Specific
    //                         RBBIDataHeader).  This swap also conveniently gets us
    //                         the size of the ICU d.h., which lets us locate the start
    //                         of the RBBI specific data.
    //
    int32_t headerSize=udata_swapDataHeader(ds, inData, length, outData, status);


    //
    // Get the RRBI Data Header, and check that it appears to be OK.
    //
    const uint8_t  *inBytes =(const uint8_t *)inData+headerSize;
    RBBIDataHeader *rbbiDH = (RBBIDataHeader *)inBytes;
    if (ds->readUInt32(rbbiDH->fMagic) != 0xb1a0 || 
            !RBBIDataWrapper::isDataVersionAcceptable(rbbiDH->fFormatVersion) ||
            ds->readUInt32(rbbiDH->fLength)  <  sizeof(RBBIDataHeader)) {
        udata_printError(ds, "ubrk_swap(): RBBI Data header is invalid.\n");
        *status=U_UNSUPPORTED_ERROR;
        return 0;
    }

    //
    // Prefight operation?  Just return the size
    //
    int32_t breakDataLength = ds->readUInt32(rbbiDH->fLength);
    int32_t totalSize = headerSize + breakDataLength;
    if (length < 0) {
        return totalSize;
    }

    //
    // Check that length passed in is consistent with length from RBBI data header.
    //
    if (length < totalSize) {
        udata_printError(ds, "ubrk_swap(): too few bytes (%d after ICU Data header) for break data.\n",
                            breakDataLength);
        *status=U_INDEX_OUTOFBOUNDS_ERROR;
        return 0;
        }


    //
    // Swap the Data.  Do the data itself first, then the RBBI Data Header, because
    //                 we need to reference the header to locate the data, and an
    //                 inplace swap of the header leaves it unusable.
    //
    uint8_t         *outBytes = (uint8_t *)outData + headerSize;
    RBBIDataHeader  *outputDH = (RBBIDataHeader *)outBytes;

    int32_t   tableStartOffset;
    int32_t   tableLength;

    //
    // If not swapping in place, zero out the output buffer before starting.
    //    Individual tables and other data items within are aligned to 8 byte boundaries
    //    when originally created.  Any unused space between items needs to be zero.
    //
    if (inBytes != outBytes) {
        uprv_memset(outBytes, 0, breakDataLength);
    }

    //
    // Each state table begins with several 32 bit fields.  Calculate the size
    //   in bytes of these.
    //
    int32_t         topSize = offsetof(RBBIStateTable, fTableData);

    // Forward state table.
    tableStartOffset = ds->readUInt32(rbbiDH->fFTable);
    tableLength      = ds->readUInt32(rbbiDH->fFTableLen);

    if (tableLength > 0) {
        RBBIStateTable *rbbiST = (RBBIStateTable *)(inBytes+tableStartOffset);
        UBool use8Bits = ds->readUInt32(rbbiST->fFlags) & RBBI_8BITS_ROWS;

        ds->swapArray32(ds, inBytes+tableStartOffset, topSize,
                            outBytes+tableStartOffset, status);

        // Swap the state table if the table is in 16 bits.
        if (use8Bits) {
            if (outBytes != inBytes) {
                uprv_memmove(outBytes+tableStartOffset+topSize,
                             inBytes+tableStartOffset+topSize,
                             tableLength-topSize);
            }
        } else {
            ds->swapArray16(ds, inBytes+tableStartOffset+topSize, tableLength-topSize,
                                outBytes+tableStartOffset+topSize, status);
        }
    }

    // Reverse state table.  Same layout as forward table, above.
    tableStartOffset = ds->readUInt32(rbbiDH->fRTable);
    tableLength      = ds->readUInt32(rbbiDH->fRTableLen);

    if (tableLength > 0) {
        RBBIStateTable *rbbiST = (RBBIStateTable *)(inBytes+tableStartOffset);
        UBool use8Bits = ds->readUInt32(rbbiST->fFlags) & RBBI_8BITS_ROWS;

        ds->swapArray32(ds, inBytes+tableStartOffset, topSize,
                            outBytes+tableStartOffset, status);

        // Swap the state table if the table is in 16 bits.
        if (use8Bits) {
            if (outBytes != inBytes) {
                uprv_memmove(outBytes+tableStartOffset+topSize,
                             inBytes+tableStartOffset+topSize,
                             tableLength-topSize);
            }
        } else {
            ds->swapArray16(ds, inBytes+tableStartOffset+topSize, tableLength-topSize,
                                outBytes+tableStartOffset+topSize, status);
        }
    }

    // Trie table for character categories
    ucptrie_swap(ds, inBytes+ds->readUInt32(rbbiDH->fTrie), ds->readUInt32(rbbiDH->fTrieLen),
                     outBytes+ds->readUInt32(rbbiDH->fTrie), status);

    // Source Rules Text.  It's UTF8 data
    if (outBytes != inBytes) {
        uprv_memmove(outBytes+ds->readUInt32(rbbiDH->fRuleSource),
                     inBytes+ds->readUInt32(rbbiDH->fRuleSource),
                     ds->readUInt32(rbbiDH->fRuleSourceLen));
    }

    // Table of rule status values.  It's all int_32 values
    ds->swapArray32(ds, inBytes+ds->readUInt32(rbbiDH->fStatusTable), ds->readUInt32(rbbiDH->fStatusTableLen),
                        outBytes+ds->readUInt32(rbbiDH->fStatusTable), status);

    // And, last, the header.
    //   It is all int32_t values except for fFormataVersion, which is an array of four bytes.
    //   Swap the whole thing as int32_t, then re-swap the one field.
    //
    ds->swapArray32(ds, inBytes, sizeof(RBBIDataHeader), outBytes, status);
    ds->swapArray32(ds, outputDH->fFormatVersion, 4, outputDH->fFormatVersion, status);

    return totalSize;
}


#endif /* #if !UCONFIG_NO_BREAK_ITERATION */
