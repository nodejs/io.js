// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
******************************************************************************
*
*   Copyright (C) 2008-2015, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
******************************************************************************
*   file name:  uspoof_conf.cpp
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 2009Jan05  (refactoring earlier files)
*   created by: Andy Heninger
*
*   Internal classes for compiling confusable data into its binary (runtime) form.
*/

#include "unicode/utypes.h"
#include "unicode/uspoof.h"
#if !UCONFIG_NO_REGULAR_EXPRESSIONS
#if !UCONFIG_NO_NORMALIZATION

#include "unicode/unorm.h"
#include "unicode/uregex.h"
#include "unicode/ustring.h"
#include "cmemory.h"
#include "uspoof_impl.h"
#include "uhash.h"
#include "uvector.h"
#include "uassert.h"
#include "uarrsort.h"
#include "uspoof_conf.h"

U_NAMESPACE_USE


//---------------------------------------------------------------------
//
//  buildConfusableData   Compile the source confusable data, as defined by
//                        the Unicode data file confusables.txt, into the binary
//                        structures used by the confusable detector.
//
//                        The binary structures are described in uspoof_impl.h
//
//     1.  Parse the data, making a hash table mapping from a UChar32 to a String.
//
//     2.  Sort all of the strings encountered by length, since they will need to
//         be stored in that order in the final string table.
//         TODO: Sorting these strings by length is no longer needed since the removal of
//         the string lengths table.  This logic can be removed to save processing time
//         when building confusables data.
//
//     3.  Build a list of keys (UChar32s) from the four mapping tables.  Sort the
//         list because that will be the ordering of our runtime table.
//
//     4.  Generate the run time string table.  This is generated before the key & value
//         tables because we need the string indexes when building those tables.
//
//     5.  Build the run-time key and value tables.  These are parallel tables, and are built
//         at the same time
//

SPUString::SPUString(LocalPointer<UnicodeString> s) {
    fStr = std::move(s);
    fCharOrStrTableIndex = 0;
}


SPUString::~SPUString() {
}


SPUStringPool::SPUStringPool(UErrorCode &status) : fVec(nullptr), fHash(nullptr) {
    LocalPointer<UVector> vec(new UVector(status), status);
    if (U_FAILURE(status)) {
        return;
    }
    vec->setDeleter(
        [](void *obj) {delete static_cast<SPUString*>(obj);});
    fVec = vec.orphan();
    fHash = uhash_open(uhash_hashUnicodeString,           // key hash function
                       uhash_compareUnicodeString,        // Key Comparator
                       nullptr,                              // Value Comparator
                       &status);
}


SPUStringPool::~SPUStringPool() {
    delete fVec;
    uhash_close(fHash);
}


int32_t SPUStringPool::size() {
    return fVec->size();
}

SPUString *SPUStringPool::getByIndex(int32_t index) {
    SPUString* retString = static_cast<SPUString*>(fVec->elementAt(index));
    return retString;
}


// Comparison function for ordering strings in the string pool.
// Compare by length first, then, within a group of the same length,
// by code point order.
// Conforms to the type signature for a USortComparator in uvector.h

static int32_t U_CALLCONV SPUStringCompare(UHashTok left, UHashTok right) {
	const SPUString *sL = const_cast<const SPUString *>(
        static_cast<SPUString *>(left.pointer));
 	const SPUString *sR = const_cast<const SPUString *>(
 	    static_cast<SPUString *>(right.pointer));
    int32_t lenL = sL->fStr->length();
    int32_t lenR = sR->fStr->length();
    if (lenL < lenR) {
        return -1;
    } else if (lenL > lenR) {
        return 1;
    } else {
        return sL->fStr->compare(*(sR->fStr));
    }
}

void SPUStringPool::sort(UErrorCode &status) {
    fVec->sort(SPUStringCompare, status);
}


SPUString *SPUStringPool::addString(UnicodeString *src, UErrorCode &status) {
    LocalPointer<UnicodeString> lpSrc(src);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    SPUString *hashedString = static_cast<SPUString *>(uhash_get(fHash, src));
    if (hashedString != nullptr) {
        return hashedString;
    }
    LocalPointer<SPUString> spuStr(new SPUString(std::move(lpSrc)), status);
    hashedString = spuStr.getAlias();
    fVec->adoptElement(spuStr.orphan(), status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    uhash_put(fHash, src, hashedString, &status);
    return hashedString;
}



ConfusabledataBuilder::ConfusabledataBuilder(SpoofImpl *spImpl, UErrorCode &status) :
    fSpoofImpl(spImpl),
    fInput(nullptr),
    fTable(nullptr),
    fKeySet(nullptr),
    fKeyVec(nullptr),
    fValueVec(nullptr),
    fStringTable(nullptr),
    stringPool(nullptr),
    fParseLine(nullptr),
    fParseHexNum(nullptr),
    fLineNum(0)
{
    if (U_FAILURE(status)) {
        return;
    }

    fTable = uhash_open(uhash_hashLong, uhash_compareLong, nullptr, &status);

    fKeySet = new UnicodeSet();
    if (fKeySet == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }

    fKeyVec = new UVector(status);
    if (fKeyVec == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }

    fValueVec = new UVector(status);
    if (fValueVec == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }

    stringPool = new SPUStringPool(status);
    if (stringPool == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
}


ConfusabledataBuilder::~ConfusabledataBuilder() {
    uprv_free(fInput);
    uregex_close(fParseLine);
    uregex_close(fParseHexNum);
    uhash_close(fTable);
    delete fKeySet;
    delete fKeyVec;
    delete fStringTable;
    delete fValueVec;
    delete stringPool;
}


void ConfusabledataBuilder::buildConfusableData(SpoofImpl * spImpl, const char * confusables,
    int32_t confusablesLen, int32_t *errorType, UParseError *pe, UErrorCode &status) {

    if (U_FAILURE(status)) {
        return;
    }
    ConfusabledataBuilder builder(spImpl, status);
    builder.build(confusables, confusablesLen, status);
    if (U_FAILURE(status) && errorType != nullptr) {
        *errorType = USPOOF_SINGLE_SCRIPT_CONFUSABLE;
        pe->line = builder.fLineNum;
    }
}


void ConfusabledataBuilder::build(const char * confusables, int32_t confusablesLen,
               UErrorCode &status) {

    // Convert the user input data from UTF-8 to char16_t (UTF-16)
    int32_t inputLen = 0;
    if (U_FAILURE(status)) {
        return;
    }
    u_strFromUTF8(nullptr, 0, &inputLen, confusables, confusablesLen, &status);
    if (status != U_BUFFER_OVERFLOW_ERROR) {
        return;
    }
    status = U_ZERO_ERROR;
    fInput = static_cast<char16_t *>(uprv_malloc((inputLen+1) * sizeof(char16_t)));
    if (fInput == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    u_strFromUTF8(fInput, inputLen+1, nullptr, confusables, confusablesLen, &status);


    // Regular Expression to parse a line from Confusables.txt.  The expression will match
    // any line.  What was matched is determined by examining which capture groups have a match.
    //   Capture Group 1:  the source char
    //   Capture Group 2:  the replacement chars
    //   Capture Group 3-6  the table type, SL, SA, ML, or MA (deprecated)
    //   Capture Group 7:  A blank or comment only line.
    //   Capture Group 8:  A syntactically invalid line.  Anything that didn't match before.
    // Example Line from the confusables.txt source file:
    //   "1D702 ;	006E 0329 ;	SL	# MATHEMATICAL ITALIC SMALL ETA ... "
    UnicodeString pattern(
        "(?m)^[ \\t]*([0-9A-Fa-f]+)[ \\t]+;"      // Match the source char
        "[ \\t]*([0-9A-Fa-f]+"                    // Match the replacement char(s)
           "(?:[ \\t]+[0-9A-Fa-f]+)*)[ \\t]*;"    //     (continued)
        "\\s*(?:(SL)|(SA)|(ML)|(MA))"             // Match the table type
        "[ \\t]*(?:#.*?)?$"                       // Match any trailing #comment
        "|^([ \\t]*(?:#.*?)?)$"       // OR match empty lines or lines with only a #comment
        "|^(.*?)$", -1, US_INV);      // OR match any line, which catches illegal lines.
    // TODO: Why are we using the regex C API here? C++ would just take UnicodeString...
    fParseLine = uregex_open(pattern.getBuffer(), pattern.length(), 0, nullptr, &status);

    // Regular expression for parsing a hex number out of a space-separated list of them.
    //   Capture group 1 gets the number, with spaces removed.
    pattern = UNICODE_STRING_SIMPLE("\\s*([0-9A-F]+)");
    fParseHexNum = uregex_open(pattern.getBuffer(), pattern.length(), 0, nullptr, &status);

    // Zap any Byte Order Mark at the start of input.  Changing it to a space is benign
    //   given the syntax of the input.
    if (*fInput == 0xfeff) {
        *fInput = 0x20;
    }

    // Parse the input, one line per iteration of this loop.
    uregex_setText(fParseLine, fInput, inputLen, &status);
    while (uregex_findNext(fParseLine, &status)) {
        fLineNum++;
        if (uregex_start(fParseLine, 7, &status) >= 0) {
            // this was a blank or comment line.
            continue;
        }
        if (uregex_start(fParseLine, 8, &status) >= 0) {
            // input file syntax error.
            status = U_PARSE_ERROR;
            return;
        }

        // We have a good input line.  Extract the key character and mapping string, and
        //    put them into the appropriate mapping table.
        UChar32 keyChar = SpoofImpl::ScanHex(fInput, uregex_start(fParseLine, 1, &status),
                          uregex_end(fParseLine, 1, &status), status);

        int32_t mapStringStart = uregex_start(fParseLine, 2, &status);
        int32_t mapStringLength = uregex_end(fParseLine, 2, &status) - mapStringStart;
        uregex_setText(fParseHexNum, &fInput[mapStringStart], mapStringLength, &status);

        UnicodeString  *mapString = new UnicodeString();
        if (mapString == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
        while (uregex_findNext(fParseHexNum, &status)) {
            UChar32 c = SpoofImpl::ScanHex(&fInput[mapStringStart], uregex_start(fParseHexNum, 1, &status),
                                 uregex_end(fParseHexNum, 1, &status), status);
            mapString->append(c);
        }
        U_ASSERT(mapString->length() >= 1);

        // Put the map (value) string into the string pool
        // This a little like a Java intern() - any duplicates will be eliminated.
        SPUString *smapString = stringPool->addString(mapString, status);

        // Add the UChar32 -> string mapping to the table.
        // For Unicode 8, the SL, SA and ML tables have been discontinued.
        //                All input data from confusables.txt is tagged MA.
        uhash_iput(fTable, keyChar, smapString, &status);
        if (U_FAILURE(status)) { return; }
        fKeySet->add(keyChar);
    }

    // Input data is now all parsed and collected.
    // Now create the run-time binary form of the data.
    //
    // This is done in two steps.  First the data is assembled into vectors and strings,
    //   for ease of construction, then the contents of these collections are dumped
    //   into the actual raw-bytes data storage.

    // Build up the string array, and record the index of each string therein
    //  in the (build time only) string pool.
    // Strings of length one are not entered into the strings array.
    // (Strings in the table are sorted by length)
    stringPool->sort(status);
    fStringTable = new UnicodeString();
    int32_t poolSize = stringPool->size();
    int32_t i;
    for (i=0; i<poolSize; i++) {
        SPUString *s = stringPool->getByIndex(i);
        int32_t strLen = s->fStr->length();
        int32_t strIndex = fStringTable->length();
        if (strLen == 1) {
            // strings of length one do not get an entry in the string table.
            // Keep the single string character itself here, which is the same
            //  convention that is used in the final run-time string table index.
            s->fCharOrStrTableIndex = s->fStr->charAt(0);
        } else {
            s->fCharOrStrTableIndex = strIndex;
            fStringTable->append(*(s->fStr));
        }
    }

    // Construct the compile-time Key and Value tables
    //
    // For each key code point, check which mapping tables it applies to,
    //   and create the final data for the key & value structures.
    //
    //   The four logical mapping tables are conflated into one combined table.
    //   If multiple logical tables have the same mapping for some key, they
    //     share a single entry in the combined table.
    //   If more than one mapping exists for the same key code point, multiple
    //     entries will be created in the table

    for (int32_t range=0; range<fKeySet->getRangeCount(); range++) {
        // It is an oddity of the UnicodeSet API that simply enumerating the contained
        //   code points requires a nested loop.
        for (UChar32 keyChar=fKeySet->getRangeStart(range);
                keyChar <= fKeySet->getRangeEnd(range); keyChar++) {
            SPUString *targetMapping = static_cast<SPUString *>(uhash_iget(fTable, keyChar));
            U_ASSERT(targetMapping != nullptr);

            // Set an error code if trying to consume a long string.  Otherwise,
            // codePointAndLengthToKey will abort on a U_ASSERT.
            if (targetMapping->fStr->length() > 256) {
                status = U_ILLEGAL_ARGUMENT_ERROR;
                return;
            }

            int32_t key = ConfusableDataUtils::codePointAndLengthToKey(keyChar,
                targetMapping->fStr->length());
            int32_t value = targetMapping->fCharOrStrTableIndex;

            fKeyVec->addElement(key, status);
            fValueVec->addElement(value, status);
        }
    }

    // Put the assembled data into the flat runtime array
    outputData(status);

    // All of the intermediate allocated data belongs to the ConfusabledataBuilder
    //  object  (this), and is deleted in the destructor.
}

//
// outputData     The confusable data has been compiled and stored in intermediate
//                collections and strings.  Copy it from there to the final flat
//                binary array.
//
//                Note that as each section is added to the output data, the
//                expand (reserveSpace() function will likely relocate it in memory.
//                Be careful with pointers.
//
void ConfusabledataBuilder::outputData(UErrorCode &status) {

    U_ASSERT(fSpoofImpl->fSpoofData->fDataOwned);

    //  The Key Table
    //     While copying the keys to the runtime array,
    //       also sanity check that they are sorted.

    int32_t numKeys = fKeyVec->size();
    int32_t *keys =
        static_cast<int32_t *>(fSpoofImpl->fSpoofData->reserveSpace(numKeys*sizeof(int32_t), status));
    if (U_FAILURE(status)) {
        return;
    }
    int i;
    UChar32 previousCodePoint = 0;
    for (i=0; i<numKeys; i++) {
        int32_t key =  fKeyVec->elementAti(i);
        UChar32 codePoint = ConfusableDataUtils::keyToCodePoint(key);
        (void)previousCodePoint;    // Suppress unused variable warning.
        // strictly greater because there can be only one entry per code point
        U_ASSERT(codePoint > previousCodePoint);
        keys[i] = key;
        previousCodePoint = codePoint;
    }
    SpoofDataHeader *rawData = fSpoofImpl->fSpoofData->fRawData;
    rawData->fCFUKeys = static_cast<int32_t>(reinterpret_cast<char*>(keys) - reinterpret_cast<char*>(rawData));
    rawData->fCFUKeysSize = numKeys;
    fSpoofImpl->fSpoofData->fCFUKeys = keys;


    // The Value Table, parallels the key table
    int32_t numValues = fValueVec->size();
    U_ASSERT(numKeys == numValues);
    uint16_t *values =
        static_cast<uint16_t *>(fSpoofImpl->fSpoofData->reserveSpace(numKeys*sizeof(uint16_t), status));
    if (U_FAILURE(status)) {
        return;
    }
    for (i=0; i<numValues; i++) {
        uint32_t value = static_cast<uint32_t>(fValueVec->elementAti(i));
        U_ASSERT(value < 0xffff);
        values[i] = static_cast<uint16_t>(value);
    }
    rawData = fSpoofImpl->fSpoofData->fRawData;
    rawData->fCFUStringIndex = static_cast<int32_t>(reinterpret_cast<char*>(values) - reinterpret_cast<char*>(rawData));
    rawData->fCFUStringIndexSize = numValues;
    fSpoofImpl->fSpoofData->fCFUValues = values;

    // The Strings Table.

    uint32_t stringsLength = fStringTable->length();
    // Reserve an extra space so the string will be nul-terminated.  This is
    // only a convenience, for when debugging; it is not needed otherwise.
    char16_t *strings =
        static_cast<char16_t *>(fSpoofImpl->fSpoofData->reserveSpace(stringsLength*sizeof(char16_t)+2, status));
    if (U_FAILURE(status)) {
        return;
    }
    fStringTable->extract(strings, stringsLength+1, status);
    rawData = fSpoofImpl->fSpoofData->fRawData;
    U_ASSERT(rawData->fCFUStringTable == 0);
    rawData->fCFUStringTable = static_cast<int32_t>(reinterpret_cast<char*>(strings) - reinterpret_cast<char*>(rawData));
    rawData->fCFUStringTableLen = stringsLength;
    fSpoofImpl->fSpoofData->fCFUStrings = strings;
}

#endif
#endif // !UCONFIG_NO_REGULAR_EXPRESSIONS

