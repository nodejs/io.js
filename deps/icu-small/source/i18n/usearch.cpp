// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
*   Copyright (C) 2001-2015 IBM and others. All rights reserved.
**********************************************************************
*   Date        Name        Description
*  07/02/2001   synwee      Creation.
**********************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION && !UCONFIG_NO_BREAK_ITERATION

#include "unicode/usearch.h"
#include "unicode/ustring.h"
#include "unicode/uchar.h"
#include "unicode/utf16.h"
#include "normalizer2impl.h"
#include "usrchimp.h"
#include "cmemory.h"
#include "ucln_in.h"
#include "uassert.h"
#include "ustr_imp.h"

U_NAMESPACE_USE

// internal definition ---------------------------------------------------

#define LAST_BYTE_MASK_          0xFF
#define SECOND_LAST_BYTE_SHIFT_  8
#define SUPPLEMENTARY_MIN_VALUE_ 0x10000

static const Normalizer2Impl *g_nfcImpl = nullptr;

// internal methods -------------------------------------------------

/**
* Fast collation element iterator setOffset.
* This function does not check for bounds.
* @param coleiter collation element iterator
* @param offset to set
*/
static
inline void setColEIterOffset(UCollationElements *elems,
                              int32_t offset,
                              UErrorCode &status)
{
    // Note: Not "fast" any more after the 2013 collation rewrite.
    // We do not want to expose more internals than necessary.
    ucol_setOffset(elems, offset, &status);
}

/**
* Getting the mask for collation strength
* @param strength collation strength
* @return collation element mask
*/
static
inline uint32_t getMask(UCollationStrength strength)
{
    switch (strength)
    {
    case UCOL_PRIMARY:
        return UCOL_PRIMARYORDERMASK;
    case UCOL_SECONDARY:
        return UCOL_SECONDARYORDERMASK | UCOL_PRIMARYORDERMASK;
    default:
        return UCOL_TERTIARYORDERMASK | UCOL_SECONDARYORDERMASK |
               UCOL_PRIMARYORDERMASK;
    }
}

U_CDECL_BEGIN
static UBool U_CALLCONV
usearch_cleanup() {
    g_nfcImpl = nullptr;
    return true;
}
U_CDECL_END

/**
* Initializing the fcd tables.
* Internal method, status assumed to be a success.
* @param status output error if any, caller to check status before calling
*               method, status assumed to be success when passed in.
*/
static
inline void initializeFCD(UErrorCode *status)
{
    if (g_nfcImpl == nullptr) {
        g_nfcImpl = Normalizer2Factory::getNFCImpl(*status);
        ucln_i18n_registerCleanup(UCLN_I18N_USEARCH, usearch_cleanup);
    }
}

/**
* Gets the fcd value for a character at the argument index.
* This method takes into accounts of the supplementary characters.
* @param str UTF16 string where character for fcd retrieval resides
* @param offset position of the character whose fcd is to be retrieved, to be
*               overwritten with the next character position, taking
*               surrogate characters into consideration.
* @param strlength length of the argument string
* @return fcd value
*/
static
uint16_t getFCD(const char16_t   *str, int32_t *offset,
                             int32_t  strlength)
{
    const char16_t *temp = str + *offset;
    uint16_t    result = g_nfcImpl->nextFCD16(temp, str + strlength);
    *offset = (int32_t)(temp - str);
    return result;
}

/**
* Getting the modified collation elements taking into account the collation
* attributes
* @param strsrch string search data
* @param sourcece
* @return the modified collation element
*/
static
inline int32_t getCE(const UStringSearch *strsrch, uint32_t sourcece)
{
    // note for tertiary we can't use the collator->tertiaryMask, that
    // is a preprocessed mask that takes into account case options. since
    // we are only concerned with exact matches, we don't need that.
    sourcece &= strsrch->ceMask;

    if (strsrch->toShift) {
        // alternate handling here, since only the 16 most significant digits
        // is only used, we can safely do a compare without masking
        // if the ce is a variable, we mask and get only the primary values
        // no shifting to quartenary is required since all primary values
        // less than variabletop will need to be masked off anyway.
        if (strsrch->variableTop > sourcece) {
            if (strsrch->strength >= UCOL_QUATERNARY) {
                sourcece &= UCOL_PRIMARYORDERMASK;
            }
            else {
                sourcece = UCOL_IGNORABLE;
            }
        }
    } else if (strsrch->strength >= UCOL_QUATERNARY && sourcece == UCOL_IGNORABLE) {
        sourcece = 0xFFFF;
    }

    return sourcece;
}

/**
* Allocate a memory and returns nullptr if it failed.
* Internal method, status assumed to be a success.
* @param size to allocate
* @param status output error if any, caller to check status before calling
*               method, status assumed to be success when passed in.
* @return newly allocated array, nullptr otherwise
*/
static
inline void * allocateMemory(uint32_t size, UErrorCode *status)
{
    uint32_t *result = (uint32_t *)uprv_malloc(size);
    if (result == nullptr) {
        *status = U_MEMORY_ALLOCATION_ERROR;
    }
    return result;
}

/**
* Adds a uint32_t value to a destination array.
* Creates a new array if we run out of space. The caller will have to
* manually deallocate the newly allocated array.
* Internal method, status assumed to be success, caller has to check status
* before calling this method. destination not to be nullptr and has at least
* size destinationlength.
* @param destination target array
* @param offset destination offset to add value
* @param destinationlength target array size, return value for the new size
* @param value to be added
* @param increments incremental size expected
* @param status output error if any, caller to check status before calling
*               method, status assumed to be success when passed in.
* @return new destination array, destination if there was no new allocation
*/
static
inline int32_t * addTouint32_tArray(int32_t    *destination,
                                    uint32_t    offset,
                                    uint32_t   *destinationlength,
                                    uint32_t    value,
                                    uint32_t    increments,
                                    UErrorCode *status)
{
    uint32_t newlength = *destinationlength;
    if (offset + 1 == newlength) {
        newlength += increments;
        int32_t *temp = (int32_t *)allocateMemory(
                                         sizeof(int32_t) * newlength, status);
        if (U_FAILURE(*status)) {
            return nullptr;
        }
        uprv_memcpy(temp, destination, sizeof(int32_t) * (size_t)offset);
        *destinationlength = newlength;
        destination        = temp;
    }
    destination[offset] = value;
    return destination;
}

/**
* Adds a uint64_t value to a destination array.
* Creates a new array if we run out of space. The caller will have to
* manually deallocate the newly allocated array.
* Internal method, status assumed to be success, caller has to check status
* before calling this method. destination not to be nullptr and has at least
* size destinationlength.
* @param destination target array
* @param offset destination offset to add value
* @param destinationlength target array size, return value for the new size
* @param value to be added
* @param increments incremental size expected
* @param status output error if any, caller to check status before calling
*               method, status assumed to be success when passed in.
* @return new destination array, destination if there was no new allocation
*/
static
inline int64_t * addTouint64_tArray(int64_t    *destination,
                                    uint32_t    offset,
                                    uint32_t   *destinationlength,
                                    uint64_t    value,
                                    uint32_t    increments,
                                    UErrorCode *status)
{
    uint32_t newlength = *destinationlength;
    if (offset + 1 == newlength) {
        newlength += increments;
        int64_t *temp = (int64_t *)allocateMemory(
                                         sizeof(int64_t) * newlength, status);

        if (U_FAILURE(*status)) {
            return nullptr;
        }

        uprv_memcpy(temp, destination, sizeof(int64_t) * (size_t)offset);
        *destinationlength = newlength;
        destination        = temp;
    }

    destination[offset] = value;

    return destination;
}

/**
* Initializing the ce table for a pattern.
* Stores non-ignorable collation keys.
* Table size will be estimated by the size of the pattern text. Table
* expansion will be perform as we go along. Adding 1 to ensure that the table
* size definitely increases.
* Internal method, status assumed to be a success.
* @param strsrch string search data
* @param status output error if any, caller to check status before calling
*               method, status assumed to be success when passed in.
*/
static
inline void initializePatternCETable(UStringSearch *strsrch, UErrorCode *status)
{
    UPattern *pattern            = &(strsrch->pattern);
    uint32_t  cetablesize        = INITIAL_ARRAY_SIZE_;
    int32_t  *cetable            = pattern->cesBuffer;
    uint32_t  patternlength      = pattern->textLength;
    UCollationElements *coleiter = strsrch->utilIter;

    if (coleiter == nullptr) {
        coleiter = ucol_openElements(strsrch->collator, pattern->text,
                                     patternlength, status);
        // status will be checked in ucol_next(..) later and if it is an
        // error UCOL_NULLORDER the result of ucol_next(..) and 0 will be
        // returned.
        strsrch->utilIter = coleiter;
    }
    else {
        ucol_setText(coleiter, pattern->text, pattern->textLength, status);
    }
    if(U_FAILURE(*status)) {
        return;
    }

    if (pattern->ces != cetable && pattern->ces) {
        uprv_free(pattern->ces);
    }

    uint32_t  offset      = 0;
    int32_t   ce;

    while ((ce = ucol_next(coleiter, status)) != UCOL_NULLORDER &&
           U_SUCCESS(*status)) {
        uint32_t newce = getCE(strsrch, ce);
        if (newce) {
            int32_t *temp = addTouint32_tArray(cetable, offset, &cetablesize,
                                  newce,
                                  patternlength - ucol_getOffset(coleiter) + 1,
                                  status);
            if (U_FAILURE(*status)) {
                return;
            }
            offset ++;
            if (cetable != temp && cetable != pattern->cesBuffer) {
                uprv_free(cetable);
            }
            cetable = temp;
        }
    }

    cetable[offset]   = 0;
    pattern->ces       = cetable;
    pattern->cesLength = offset;
}

/**
* Initializing the pce table for a pattern.
* Stores non-ignorable collation keys.
* Table size will be estimated by the size of the pattern text. Table
* expansion will be perform as we go along. Adding 1 to ensure that the table
* size definitely increases.
* Internal method, status assumed to be a success.
* @param strsrch string search data
* @param status output error if any, caller to check status before calling
*               method, status assumed to be success when passed in.
*/
static
inline void initializePatternPCETable(UStringSearch *strsrch,
                                      UErrorCode    *status)
{
    UPattern *pattern            = &(strsrch->pattern);
    uint32_t  pcetablesize       = INITIAL_ARRAY_SIZE_;
    int64_t  *pcetable           = pattern->pcesBuffer;
    uint32_t  patternlength      = pattern->textLength;
    UCollationElements *coleiter = strsrch->utilIter;

    if (coleiter == nullptr) {
        coleiter = ucol_openElements(strsrch->collator, pattern->text,
                                     patternlength, status);
        // status will be checked in nextProcessed(..) later and if it is an error
        // then UCOL_PROCESSED_NULLORDER is returned by nextProcessed(..), so 0 will be
        // returned.
        strsrch->utilIter = coleiter;
    } else {
        ucol_setText(coleiter, pattern->text, pattern->textLength, status);
    }
    if(U_FAILURE(*status)) {
        return;
    }

    if (pattern->pces != pcetable && pattern->pces != nullptr) {
        uprv_free(pattern->pces);
    }

    uint32_t  offset = 0;
    int64_t   pce;

    icu::UCollationPCE iter(coleiter);

    // ** Should processed CEs be signed or unsigned?
    // ** (the rest of the code in this file seems to play fast-and-loose with
    // **  whether a CE is signed or unsigned. For example, look at routine above this one.)
    while ((pce = iter.nextProcessed(nullptr, nullptr, status)) != UCOL_PROCESSED_NULLORDER &&
           U_SUCCESS(*status)) {
        int64_t *temp = addTouint64_tArray(pcetable, offset, &pcetablesize,
                              pce,
                              patternlength - ucol_getOffset(coleiter) + 1,
                              status);

        if (U_FAILURE(*status)) {
            return;
        }

        offset += 1;

        if (pcetable != temp && pcetable != pattern->pcesBuffer) {
            uprv_free(pcetable);
        }

        pcetable = temp;
    }

    pcetable[offset]   = 0;
    pattern->pces       = pcetable;
    pattern->pcesLength = offset;
}

/**
* Initializes the pattern struct.
* @param strsrch UStringSearch data storage
* @param status output error if any, caller to check status before calling
*               method, status assumed to be success when passed in.
*/
static
inline void initializePattern(UStringSearch *strsrch, UErrorCode *status)
{
    if (U_FAILURE(*status)) { return; }

          UPattern   *pattern     = &(strsrch->pattern);
    const char16_t   *patterntext = pattern->text;
          int32_t     length      = pattern->textLength;
          int32_t index       = 0;

    // Since the strength is primary, accents are ignored in the pattern.
    if (strsrch->strength == UCOL_PRIMARY) {
        pattern->hasPrefixAccents = 0;
        pattern->hasSuffixAccents = 0;
    } else {
        pattern->hasPrefixAccents = getFCD(patterntext, &index, length) >>
                                                         SECOND_LAST_BYTE_SHIFT_;
        index = length;
        U16_BACK_1(patterntext, 0, index);
        pattern->hasSuffixAccents = getFCD(patterntext, &index, length) &
                                                                 LAST_BYTE_MASK_;
    }

    // ** HACK **
    if (strsrch->pattern.pces != nullptr) {
        if (strsrch->pattern.pces != strsrch->pattern.pcesBuffer) {
            uprv_free(strsrch->pattern.pces);
        }

        strsrch->pattern.pces = nullptr;
    }

    initializePatternCETable(strsrch, status);
}

/**
* Initializes the pattern struct and builds the pattern collation element table.
* @param strsrch UStringSearch data storage
* @param status  for output errors if it occurs, status is assumed to be a
*                success when it is passed in.
*/
static
inline void initialize(UStringSearch *strsrch, UErrorCode *status)
{
    initializePattern(strsrch, status);
}

#if !UCONFIG_NO_BREAK_ITERATION
// If the caller provided a character breakiterator we'll return that,
// otherwise we lazily create the internal break iterator. 
static UBreakIterator* getBreakIterator(UStringSearch *strsrch, UErrorCode &status)
{
    if (U_FAILURE(status)) {
        return nullptr;
    }

    if (strsrch->search->breakIter != nullptr) {
        return strsrch->search->breakIter;
    }

    if (strsrch->search->internalBreakIter != nullptr) {
        return strsrch->search->internalBreakIter;
    }

    // Need to create the internal break iterator.
    strsrch->search->internalBreakIter = ubrk_open(UBRK_CHARACTER,
        ucol_getLocaleByType(strsrch->collator, ULOC_VALID_LOCALE, &status),
        strsrch->search->text, strsrch->search->textLength, &status);

    return strsrch->search->internalBreakIter;
}
#endif

/**
* Sets the match result to "not found", regardless of the incoming error status.
* If an error occurs while setting the result, it is reported back.
* 
* @param strsrch string search data
* @param status  for output errors, if they occur.
*/
static
inline void setMatchNotFound(UStringSearch *strsrch, UErrorCode &status)
{
    UErrorCode localStatus = U_ZERO_ERROR;

    strsrch->search->matchedIndex = USEARCH_DONE;
    strsrch->search->matchedLength = 0;
    if (strsrch->search->isForwardSearching) {
        setColEIterOffset(strsrch->textIter, strsrch->search->textLength, localStatus);
    }
    else {
        setColEIterOffset(strsrch->textIter, 0, localStatus);
    }

    // If an error occurred while setting the result to not found (ex: OOM),
    // then we want to report that error back to the caller.
    if (U_SUCCESS(status) && U_FAILURE(localStatus)) {
        status = localStatus;
    }
}

/**
* Checks if the offset runs out of the text string
* @param offset
* @param textlength of the text string
* @return true if offset is out of bounds, false otherwise
*/
static
inline UBool isOutOfBounds(int32_t textlength, int32_t offset)
{
    return offset < 0 || offset > textlength;
}

/**
* Checks for identical match
* @param strsrch string search data
* @param start offset of possible match
* @param end offset of possible match
* @return true if identical match is found
*/
static
inline UBool checkIdentical(const UStringSearch *strsrch, int32_t start, int32_t end)
{
    if (strsrch->strength != UCOL_IDENTICAL) {
        return true;
    }

    // Note: We could use Normalizer::compare() or similar, but for short strings
    // which may not be in FCD it might be faster to just NFD them.
    UErrorCode status = U_ZERO_ERROR;
    UnicodeString t2, p2;
    strsrch->nfd->normalize(
        UnicodeString(false, strsrch->search->text + start, end - start), t2, status);
    strsrch->nfd->normalize(
        UnicodeString(false, strsrch->pattern.text, strsrch->pattern.textLength), p2, status);
    // return false if NFD failed
    return U_SUCCESS(status) && t2 == p2;
}

// constructors and destructor -------------------------------------------

U_CAPI UStringSearch * U_EXPORT2 usearch_open(const char16_t *pattern,
                                          int32_t         patternlength,
                                    const char16_t       *text,
                                          int32_t         textlength,
                                    const char           *locale,
                                          UBreakIterator *breakiter,
                                          UErrorCode     *status)
{
    if (U_FAILURE(*status)) {
        return nullptr;
    }
#if UCONFIG_NO_BREAK_ITERATION
    if (breakiter != nullptr) {
        *status = U_UNSUPPORTED_ERROR;
        return nullptr;
    }
#endif
    if (locale) {
        // ucol_open internally checks for status
        UCollator     *collator = ucol_open(locale, status);
        // pattern, text checks are done in usearch_openFromCollator
        UStringSearch *result   = usearch_openFromCollator(pattern,
                                              patternlength, text, textlength,
                                              collator, breakiter, status);

        if (result == nullptr || U_FAILURE(*status)) {
            if (collator) {
                ucol_close(collator);
            }
            return nullptr;
        }
        else {
            result->ownCollator = true;
        }
        return result;
    }
    *status = U_ILLEGAL_ARGUMENT_ERROR;
    return nullptr;
}

U_CAPI UStringSearch * U_EXPORT2 usearch_openFromCollator(
                                  const char16_t       *pattern,
                                        int32_t         patternlength,
                                  const char16_t       *text,
                                        int32_t         textlength,
                                  const UCollator      *collator,
                                        UBreakIterator *breakiter,
                                        UErrorCode     *status)
{
    if (U_FAILURE(*status)) {
        return nullptr;
    }
#if UCONFIG_NO_BREAK_ITERATION
    if (breakiter != nullptr) {
        *status = U_UNSUPPORTED_ERROR;
        return nullptr;
    }
#endif
    if (pattern == nullptr || text == nullptr || collator == nullptr) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }

    // string search does not really work when numeric collation is turned on
    if(ucol_getAttribute(collator, UCOL_NUMERIC_COLLATION, status) == UCOL_ON) {
        *status = U_UNSUPPORTED_ERROR;
        return nullptr;
    }

    if (U_SUCCESS(*status)) {
        initializeFCD(status);
        if (U_FAILURE(*status)) {
            return nullptr;
        }

        UStringSearch *result;
        if (textlength == -1) {
            textlength = u_strlen(text);
        }
        if (patternlength == -1) {
            patternlength = u_strlen(pattern);
        }
        if (textlength <= 0 || patternlength <= 0) {
            *status = U_ILLEGAL_ARGUMENT_ERROR;
            return nullptr;
        }

        result = (UStringSearch *)uprv_malloc(sizeof(UStringSearch));
        if (result == nullptr) {
            *status = U_MEMORY_ALLOCATION_ERROR;
            return nullptr;
        }

        result->collator    = collator;
        result->strength    = ucol_getStrength(collator);
        result->ceMask      = getMask(result->strength);
        result->toShift     =
             ucol_getAttribute(collator, UCOL_ALTERNATE_HANDLING, status) ==
                                                            UCOL_SHIFTED;
        result->variableTop = ucol_getVariableTop(collator, status);

        result->nfd         = Normalizer2::getNFDInstance(*status);

        if (U_FAILURE(*status)) {
            uprv_free(result);
            return nullptr;
        }

        result->search             = (USearch *)uprv_malloc(sizeof(USearch));
        if (result->search == nullptr) {
            *status = U_MEMORY_ALLOCATION_ERROR;
            uprv_free(result);
            return nullptr;
        }

        result->search->text       = text;
        result->search->textLength = textlength;

        result->pattern.text       = pattern;
        result->pattern.textLength = patternlength;
        result->pattern.ces         = nullptr;
        result->pattern.pces        = nullptr;

        result->search->breakIter  = breakiter;
#if !UCONFIG_NO_BREAK_ITERATION
        result->search->internalBreakIter = nullptr; // Lazily created.
        if (breakiter) {
            ubrk_setText(breakiter, text, textlength, status);
        }
#endif

        result->ownCollator           = false;
        result->search->matchedLength = 0;
        result->search->matchedIndex  = USEARCH_DONE;
        result->utilIter              = nullptr;
        result->textIter              = ucol_openElements(collator, text,
                                                          textlength, status);
        result->textProcessedIter     = nullptr;
        if (U_FAILURE(*status)) {
            usearch_close(result);
            return nullptr;
        }

        result->search->isOverlap          = false;
        result->search->isCanonicalMatch   = false;
        result->search->elementComparisonType = 0;
        result->search->isForwardSearching = true;
        result->search->reset              = true;

        initialize(result, status);

        if (U_FAILURE(*status)) {
            usearch_close(result);
            return nullptr;
        }

        return result;
    }
    return nullptr;
}

U_CAPI void U_EXPORT2 usearch_close(UStringSearch *strsrch)
{
    if (strsrch) {
        if (strsrch->pattern.ces != strsrch->pattern.cesBuffer &&
            strsrch->pattern.ces) {
            uprv_free(strsrch->pattern.ces);
        }

        if (strsrch->pattern.pces != nullptr &&
            strsrch->pattern.pces != strsrch->pattern.pcesBuffer) {
            uprv_free(strsrch->pattern.pces);
        }

        delete strsrch->textProcessedIter;
        ucol_closeElements(strsrch->textIter);
        ucol_closeElements(strsrch->utilIter);

        if (strsrch->ownCollator && strsrch->collator) {
            ucol_close((UCollator *)strsrch->collator);
        }

#if !UCONFIG_NO_BREAK_ITERATION
        if (strsrch->search->internalBreakIter != nullptr) {
            ubrk_close(strsrch->search->internalBreakIter);
        }
#endif

        uprv_free(strsrch->search);
        uprv_free(strsrch);
    }
}

namespace {

UBool initTextProcessedIter(UStringSearch *strsrch, UErrorCode *status) {
    if (U_FAILURE(*status)) { return false; }
    if (strsrch->textProcessedIter == nullptr) {
        strsrch->textProcessedIter = new icu::UCollationPCE(strsrch->textIter);
        if (strsrch->textProcessedIter == nullptr) {
            *status = U_MEMORY_ALLOCATION_ERROR;
            return false;
        }
    } else {
        strsrch->textProcessedIter->init(strsrch->textIter);
    }
    return true;
}

}

// set and get methods --------------------------------------------------

U_CAPI void U_EXPORT2 usearch_setOffset(UStringSearch *strsrch,
                                        int32_t        position,
                                        UErrorCode    *status)
{
    if (U_SUCCESS(*status) && strsrch) {
        if (isOutOfBounds(strsrch->search->textLength, position)) {
            *status = U_INDEX_OUTOFBOUNDS_ERROR;
        }
        else {
            setColEIterOffset(strsrch->textIter, position, *status);
        }
        strsrch->search->matchedIndex  = USEARCH_DONE;
        strsrch->search->matchedLength = 0;
        strsrch->search->reset         = false;
    }
}

U_CAPI int32_t U_EXPORT2 usearch_getOffset(const UStringSearch *strsrch)
{
    if (strsrch) {
        int32_t result = ucol_getOffset(strsrch->textIter);
        if (isOutOfBounds(strsrch->search->textLength, result)) {
            return USEARCH_DONE;
        }
        return result;
    }
    return USEARCH_DONE;
}

U_CAPI void U_EXPORT2 usearch_setAttribute(UStringSearch        *strsrch,
                                           USearchAttribute      attribute,
                                           USearchAttributeValue value,
                                           UErrorCode           *status)
{
    if (U_SUCCESS(*status) && strsrch) {
        switch (attribute)
        {
        case USEARCH_OVERLAP :
            strsrch->search->isOverlap = (value == USEARCH_ON ? true : false);
            break;
        case USEARCH_CANONICAL_MATCH :
            strsrch->search->isCanonicalMatch = (value == USEARCH_ON ? true :
                                                                      false);
            break;
        case USEARCH_ELEMENT_COMPARISON :
            if (value == USEARCH_PATTERN_BASE_WEIGHT_IS_WILDCARD || value == USEARCH_ANY_BASE_WEIGHT_IS_WILDCARD) {
                strsrch->search->elementComparisonType = (int16_t)value;
            } else {
                strsrch->search->elementComparisonType = 0;
            }
            break;
        case USEARCH_ATTRIBUTE_COUNT :
        default:
            *status = U_ILLEGAL_ARGUMENT_ERROR;
        }
    }
    if (value == USEARCH_ATTRIBUTE_VALUE_COUNT) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
    }
}

U_CAPI USearchAttributeValue U_EXPORT2 usearch_getAttribute(
                                                const UStringSearch *strsrch,
                                                USearchAttribute attribute)
{
    if (strsrch) {
        switch (attribute) {
        case USEARCH_OVERLAP :
            return (strsrch->search->isOverlap ? USEARCH_ON : USEARCH_OFF);
        case USEARCH_CANONICAL_MATCH :
            return (strsrch->search->isCanonicalMatch ? USEARCH_ON : USEARCH_OFF);
        case USEARCH_ELEMENT_COMPARISON :
            {
                int16_t value = strsrch->search->elementComparisonType;
                if (value == USEARCH_PATTERN_BASE_WEIGHT_IS_WILDCARD || value == USEARCH_ANY_BASE_WEIGHT_IS_WILDCARD) {
                    return (USearchAttributeValue)value;
                } else {
                    return USEARCH_STANDARD_ELEMENT_COMPARISON;
                }
            }
        case USEARCH_ATTRIBUTE_COUNT :
            return USEARCH_DEFAULT;
        }
    }
    return USEARCH_DEFAULT;
}

U_CAPI int32_t U_EXPORT2 usearch_getMatchedStart(
                                                const UStringSearch *strsrch)
{
    if (strsrch == nullptr) {
        return USEARCH_DONE;
    }
    return strsrch->search->matchedIndex;
}


U_CAPI int32_t U_EXPORT2 usearch_getMatchedText(const UStringSearch *strsrch,
                                            char16_t      *result,
                                            int32_t        resultCapacity,
                                            UErrorCode    *status)
{
    if (U_FAILURE(*status)) {
        return USEARCH_DONE;
    }
    if (strsrch == nullptr || resultCapacity < 0 || (resultCapacity > 0 &&
        result == nullptr)) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return USEARCH_DONE;
    }

    int32_t     copylength = strsrch->search->matchedLength;
    int32_t copyindex  = strsrch->search->matchedIndex;
    if (copyindex == USEARCH_DONE) {
        u_terminateUChars(result, resultCapacity, 0, status);
        return USEARCH_DONE;
    }

    if (resultCapacity < copylength) {
        copylength = resultCapacity;
    }
    if (copylength > 0) {
        uprv_memcpy(result, strsrch->search->text + copyindex,
                    copylength * sizeof(char16_t));
    }
    return u_terminateUChars(result, resultCapacity,
                             strsrch->search->matchedLength, status);
}

U_CAPI int32_t U_EXPORT2 usearch_getMatchedLength(
                                              const UStringSearch *strsrch)
{
    if (strsrch) {
        return strsrch->search->matchedLength;
    }
    return USEARCH_DONE;
}

#if !UCONFIG_NO_BREAK_ITERATION

U_CAPI void U_EXPORT2 usearch_setBreakIterator(UStringSearch  *strsrch,
                                               UBreakIterator *breakiter,
                                               UErrorCode     *status)
{
    if (U_SUCCESS(*status) && strsrch) {
        strsrch->search->breakIter = breakiter;
        if (breakiter) {
            ubrk_setText(breakiter, strsrch->search->text,
                         strsrch->search->textLength, status);
        }
    }
}

U_CAPI const UBreakIterator* U_EXPORT2
usearch_getBreakIterator(const UStringSearch *strsrch)
{
    if (strsrch) {
        return strsrch->search->breakIter;
    }
    return nullptr;
}

#endif

U_CAPI void U_EXPORT2 usearch_setText(      UStringSearch *strsrch,
                                      const char16_t      *text,
                                            int32_t        textlength,
                                            UErrorCode    *status)
{
    if (U_SUCCESS(*status)) {
        if (strsrch == nullptr || text == nullptr || textlength < -1 ||
            textlength == 0) {
            *status = U_ILLEGAL_ARGUMENT_ERROR;
        }
        else {
            if (textlength == -1) {
                textlength = u_strlen(text);
            }
            strsrch->search->text       = text;
            strsrch->search->textLength = textlength;
            ucol_setText(strsrch->textIter, text, textlength, status);
            strsrch->search->matchedIndex  = USEARCH_DONE;
            strsrch->search->matchedLength = 0;
            strsrch->search->reset         = true;
#if !UCONFIG_NO_BREAK_ITERATION
            if (strsrch->search->breakIter != nullptr) {
                ubrk_setText(strsrch->search->breakIter, text,
                             textlength, status);
            }
            if (strsrch->search->internalBreakIter != nullptr) {
                ubrk_setText(strsrch->search->internalBreakIter, text, textlength, status);
            }
#endif
        }
    }
}

U_CAPI const char16_t * U_EXPORT2 usearch_getText(const UStringSearch *strsrch,
                                                     int32_t       *length)
{
    if (strsrch) {
        *length = strsrch->search->textLength;
        return strsrch->search->text;
    }
    return nullptr;
}

U_CAPI void U_EXPORT2 usearch_setCollator(      UStringSearch *strsrch,
                                          const UCollator     *collator,
                                                UErrorCode    *status)
{
    if (U_SUCCESS(*status)) {
        if (collator == nullptr) {
            *status = U_ILLEGAL_ARGUMENT_ERROR;
            return;
        }

        if (strsrch) {
            delete strsrch->textProcessedIter;
            strsrch->textProcessedIter = nullptr;
            ucol_closeElements(strsrch->textIter);
            ucol_closeElements(strsrch->utilIter);
            strsrch->textIter = strsrch->utilIter = nullptr;
            if (strsrch->ownCollator && (strsrch->collator != collator)) {
                ucol_close((UCollator *)strsrch->collator);
                strsrch->ownCollator = false;
            }
            strsrch->collator    = collator;
            strsrch->strength    = ucol_getStrength(collator);
            strsrch->ceMask      = getMask(strsrch->strength);
#if !UCONFIG_NO_BREAK_ITERATION
            if (strsrch->search->internalBreakIter != nullptr) {
                ubrk_close(strsrch->search->internalBreakIter);
                strsrch->search->internalBreakIter = nullptr;   // Lazily created.
            }
#endif
            // if status is a failure, ucol_getAttribute returns UCOL_DEFAULT
            strsrch->toShift     =
               ucol_getAttribute(collator, UCOL_ALTERNATE_HANDLING, status) ==
                                                                UCOL_SHIFTED;
            // if status is a failure, ucol_getVariableTop returns 0
            strsrch->variableTop = ucol_getVariableTop(collator, status);
            strsrch->textIter = ucol_openElements(collator,
                                      strsrch->search->text,
                                      strsrch->search->textLength,
                                      status);
            strsrch->utilIter = ucol_openElements(
                    collator, strsrch->pattern.text, strsrch->pattern.textLength, status);
            // initialize() _after_ setting the iterators for the new collator.
            initialize(strsrch, status);
        }

        // **** are these calls needed?
        // **** we call uprv_init_pce in initializePatternPCETable
        // **** and the CEIBuffer constructor...
#if 0
        uprv_init_pce(strsrch->textIter);
        uprv_init_pce(strsrch->utilIter);
#endif
    }
}

U_CAPI UCollator * U_EXPORT2 usearch_getCollator(const UStringSearch *strsrch)
{
    if (strsrch) {
        return (UCollator *)strsrch->collator;
    }
    return nullptr;
}

U_CAPI void U_EXPORT2 usearch_setPattern(      UStringSearch *strsrch,
                                         const char16_t      *pattern,
                                               int32_t        patternlength,
                                               UErrorCode    *status)
{
    if (U_SUCCESS(*status)) {
        if (strsrch == nullptr || pattern == nullptr) {
            *status = U_ILLEGAL_ARGUMENT_ERROR;
        }
        else {
            if (patternlength == -1) {
                patternlength = u_strlen(pattern);
            }
            if (patternlength == 0) {
                *status = U_ILLEGAL_ARGUMENT_ERROR;
                return;
            }
            strsrch->pattern.text       = pattern;
            strsrch->pattern.textLength = patternlength;
            initialize(strsrch, status);
        }
    }
}

U_CAPI const char16_t* U_EXPORT2
usearch_getPattern(const UStringSearch *strsrch,
                   int32_t             *length)
{
    if (strsrch) {
        *length = strsrch->pattern.textLength;
        return strsrch->pattern.text;
    }
    return nullptr;
}

// miscellaneous methods --------------------------------------------------

U_CAPI int32_t U_EXPORT2 usearch_first(UStringSearch *strsrch,
                                       UErrorCode    *status)
{
    if (strsrch && U_SUCCESS(*status)) {
        strsrch->search->isForwardSearching = true;
        usearch_setOffset(strsrch, 0, status);
        if (U_SUCCESS(*status)) {
            return usearch_next(strsrch, status);
        }
    }
    return USEARCH_DONE;
}

U_CAPI int32_t U_EXPORT2 usearch_following(UStringSearch *strsrch,
                                           int32_t        position,
                                           UErrorCode    *status)
{
    if (strsrch && U_SUCCESS(*status)) {
        strsrch->search->isForwardSearching = true;
        // position checked in usearch_setOffset
        usearch_setOffset(strsrch, position, status);
        if (U_SUCCESS(*status)) {
            return usearch_next(strsrch, status);
        }
    }
    return USEARCH_DONE;
}

U_CAPI int32_t U_EXPORT2 usearch_last(UStringSearch *strsrch,
                                      UErrorCode    *status)
{
    if (strsrch && U_SUCCESS(*status)) {
        strsrch->search->isForwardSearching = false;
        usearch_setOffset(strsrch, strsrch->search->textLength, status);
        if (U_SUCCESS(*status)) {
            return usearch_previous(strsrch, status);
        }
    }
    return USEARCH_DONE;
}

U_CAPI int32_t U_EXPORT2 usearch_preceding(UStringSearch *strsrch,
                                           int32_t        position,
                                           UErrorCode    *status)
{
    if (strsrch && U_SUCCESS(*status)) {
        strsrch->search->isForwardSearching = false;
        // position checked in usearch_setOffset
        usearch_setOffset(strsrch, position, status);
        if (U_SUCCESS(*status)) {
            return usearch_previous(strsrch, status);
        }
    }
    return USEARCH_DONE;
}

/**
* If a direction switch is required, we'll count the number of ces till the
* beginning of the collation element iterator and iterate forwards that
* number of times. This is so that we get to the correct point within the
* string to continue the search in. Imagine when we are in the middle of the
* normalization buffer when the change in direction is request. arrrgghh....
* After searching the offset within the collation element iterator will be
* shifted to the start of the match. If a match is not found, the offset would
* have been set to the end of the text string in the collation element
* iterator.
* Okay, here's my take on normalization buffer. The only time when there can
* be 2 matches within the same normalization is when the pattern is consists
* of all accents. But since the offset returned is from the text string, we
* should not confuse the caller by returning the second match within the
* same normalization buffer. If we do, the 2 results will have the same match
* offsets, and that'll be confusing. I'll return the next match that doesn't
* fall within the same normalization buffer. Note this does not affect the
* results of matches spanning the text and the normalization buffer.
* The position to start searching is taken from the collation element
* iterator. Callers of this API would have to set the offset in the collation
* element iterator before using this method.
*/
U_CAPI int32_t U_EXPORT2 usearch_next(UStringSearch *strsrch,
                                      UErrorCode    *status)
{
    if (U_SUCCESS(*status) && strsrch) {
        // note offset is either equivalent to the start of the previous match
        // or is set by the user
        int32_t      offset       = usearch_getOffset(strsrch);
        USearch     *search       = strsrch->search;
        search->reset             = false;
        int32_t      textlength   = search->textLength;
        if (search->isForwardSearching) {
            if (offset == textlength ||
                (! search->isOverlap &&
                (search->matchedIndex != USEARCH_DONE &&
                offset + search->matchedLength > textlength))) {
                    // not enough characters to match
                    setMatchNotFound(strsrch, *status);
                    return USEARCH_DONE;
            }
        }
        else {
            // switching direction.
            // if matchedIndex == USEARCH_DONE, it means that either a
            // setOffset has been called or that previous ran off the text
            // string. the iterator would have been set to offset 0 if a
            // match is not found.
            search->isForwardSearching = true;
            if (search->matchedIndex != USEARCH_DONE) {
                // there's no need to set the collation element iterator
                // the next call to next will set the offset.
                return search->matchedIndex;
            }
        }

        if (U_SUCCESS(*status)) {
            if (strsrch->pattern.cesLength == 0) {
                if (search->matchedIndex == USEARCH_DONE) {
                    search->matchedIndex = offset;
                }
                else { // moves by codepoints
                    U16_FWD_1(search->text, search->matchedIndex, textlength);
                }

                search->matchedLength = 0;
                setColEIterOffset(strsrch->textIter, search->matchedIndex, *status);
                // status checked below
                if (search->matchedIndex == textlength) {
                    search->matchedIndex = USEARCH_DONE;
                }
            }
            else {
                if (search->matchedLength > 0) {
                    // if matchlength is 0 we are at the start of the iteration
                    if (search->isOverlap) {
                        ucol_setOffset(strsrch->textIter, offset + 1, status);
                    }
                    else {
                        ucol_setOffset(strsrch->textIter,
                                       offset + search->matchedLength, status);
                    }
                }
                else {
                    // for boundary check purposes. this will ensure that the
                    // next match will not precede the current offset
                    // note search->matchedIndex will always be set to something
                    // in the code
                    search->matchedIndex = offset - 1;
                }

                if (search->isCanonicalMatch) {
                    // can't use exact here since extra accents are allowed.
                    usearch_handleNextCanonical(strsrch, status);
                }
                else {
                    usearch_handleNextExact(strsrch, status);
                }
            }

            if (U_FAILURE(*status)) {
                return USEARCH_DONE;
            }

            if (search->matchedIndex == USEARCH_DONE) {
                ucol_setOffset(strsrch->textIter, search->textLength, status);
            } else {
                ucol_setOffset(strsrch->textIter, search->matchedIndex, status);
            }

            return search->matchedIndex;
        }
    }
    return USEARCH_DONE;
}

U_CAPI int32_t U_EXPORT2 usearch_previous(UStringSearch *strsrch,
                                          UErrorCode    *status)
{
    if (U_SUCCESS(*status) && strsrch) {
        int32_t offset;
        USearch *search = strsrch->search;
        if (search->reset) {
            offset                     = search->textLength;
            search->isForwardSearching = false;
            search->reset              = false;
            setColEIterOffset(strsrch->textIter, offset, *status);
        }
        else {
            offset = usearch_getOffset(strsrch);
        }

        int32_t matchedindex = search->matchedIndex;
        if (search->isForwardSearching) {
            // switching direction.
            // if matchedIndex == USEARCH_DONE, it means that either a
            // setOffset has been called or that next ran off the text
            // string. the iterator would have been set to offset textLength if
            // a match is not found.
            search->isForwardSearching = false;
            if (matchedindex != USEARCH_DONE) {
                return matchedindex;
            }
        }
        else {

            // Could check pattern length, but the
            // linear search will do the right thing
            if (offset == 0 || matchedindex == 0) {
                setMatchNotFound(strsrch, *status);
                return USEARCH_DONE;
            }
        }

        if (U_SUCCESS(*status)) {
            if (strsrch->pattern.cesLength == 0) {
                search->matchedIndex =
                      (matchedindex == USEARCH_DONE ? offset : matchedindex);
                if (search->matchedIndex == 0) {
                    setMatchNotFound(strsrch, *status);
                    // status checked below
                }
                else { // move by codepoints
                    U16_BACK_1(search->text, 0, search->matchedIndex);
                    setColEIterOffset(strsrch->textIter, search->matchedIndex, *status);
                    // status checked below
                    search->matchedLength = 0;
                }
            }
            else {
                if (strsrch->search->isCanonicalMatch) {
                    // can't use exact here since extra accents are allowed.
                    usearch_handlePreviousCanonical(strsrch, status);
                    // status checked below
                }
                else {
                    usearch_handlePreviousExact(strsrch, status);
                    // status checked below
                }
            }

            if (U_FAILURE(*status)) {
                return USEARCH_DONE;
            }

            return search->matchedIndex;
        }
    }
    return USEARCH_DONE;
}



U_CAPI void U_EXPORT2 usearch_reset(UStringSearch *strsrch)
{
    /*
    reset is setting the attributes that are already in
    string search, hence all attributes in the collator should
    be retrieved without any problems
    */
    if (strsrch) {
        UErrorCode status            = U_ZERO_ERROR;
        UBool      sameCollAttribute = true;
        uint32_t   ceMask;
        UBool      shift;
        uint32_t   varTop;

        // **** hack to deal w/ how processed CEs encode quaternary ****
        UCollationStrength newStrength = ucol_getStrength(strsrch->collator);
        if ((strsrch->strength < UCOL_QUATERNARY && newStrength >= UCOL_QUATERNARY) ||
            (strsrch->strength >= UCOL_QUATERNARY && newStrength < UCOL_QUATERNARY)) {
                sameCollAttribute = false;
        }

        strsrch->strength    = ucol_getStrength(strsrch->collator);
        ceMask = getMask(strsrch->strength);
        if (strsrch->ceMask != ceMask) {
            strsrch->ceMask = ceMask;
            sameCollAttribute = false;
        }

        // if status is a failure, ucol_getAttribute returns UCOL_DEFAULT
        shift = ucol_getAttribute(strsrch->collator, UCOL_ALTERNATE_HANDLING,
                                  &status) == UCOL_SHIFTED;
        if (strsrch->toShift != shift) {
            strsrch->toShift  = shift;
            sameCollAttribute = false;
        }

        // if status is a failure, ucol_getVariableTop returns 0
        varTop = ucol_getVariableTop(strsrch->collator, &status);
        if (strsrch->variableTop != varTop) {
            strsrch->variableTop = varTop;
            sameCollAttribute    = false;
        }
        if (!sameCollAttribute) {
            initialize(strsrch, &status);
        }
        ucol_setText(strsrch->textIter, strsrch->search->text,
                              strsrch->search->textLength,
                              &status);
        strsrch->search->matchedLength      = 0;
        strsrch->search->matchedIndex       = USEARCH_DONE;
        strsrch->search->isOverlap          = false;
        strsrch->search->isCanonicalMatch   = false;
        strsrch->search->elementComparisonType = 0;
        strsrch->search->isForwardSearching = true;
        strsrch->search->reset              = true;
    }
}

//
//  CEI  Collation Element + source text index.
//       These structs are kept in the circular buffer.
//
struct  CEI {
    int64_t ce;
    int32_t lowIndex;
    int32_t highIndex;
};

U_NAMESPACE_BEGIN

namespace {
//
//  CEIBuffer   A circular buffer of CEs-with-index from the text being searched.
//
#define   DEFAULT_CEBUFFER_SIZE 96
#define   CEBUFFER_EXTRA 32
// Some typical max values to make buffer size more reasonable for asymmetric search.
// #8694 is for a better long-term solution to allocation of this buffer.
#define   MAX_TARGET_IGNORABLES_PER_PAT_JAMO_L 8
#define   MAX_TARGET_IGNORABLES_PER_PAT_OTHER 3
#define   MIGHT_BE_JAMO_L(c) ((c >= 0x1100 && c <= 0x115E) || (c >= 0x3131 && c <= 0x314E) || (c >= 0x3165 && c <= 0x3186))
struct CEIBuffer {
    CEI                  defBuf[DEFAULT_CEBUFFER_SIZE];
    CEI                 *buf;
    int32_t              bufSize;
    int32_t              firstIx;
    int32_t              limitIx;
    UCollationElements  *ceIter;
    UStringSearch       *strSearch;



               CEIBuffer(UStringSearch *ss, UErrorCode *status);
               ~CEIBuffer();
   const CEI   *get(int32_t index);
   const CEI   *getPrevious(int32_t index);
};


CEIBuffer::CEIBuffer(UStringSearch *ss, UErrorCode *status) {
    buf = defBuf;
    strSearch = ss;
    bufSize = ss->pattern.pcesLength + CEBUFFER_EXTRA;
    if (ss->search->elementComparisonType != 0) {
        const char16_t * patText = ss->pattern.text;
        if (patText) {
            const char16_t * patTextLimit = patText + ss->pattern.textLength;
            while ( patText < patTextLimit ) {
                char16_t c = *patText++;
                if (MIGHT_BE_JAMO_L(c)) {
                    bufSize += MAX_TARGET_IGNORABLES_PER_PAT_JAMO_L;
                } else {
                    // No check for surrogates, we might allocate slightly more buffer than necessary.
                    bufSize += MAX_TARGET_IGNORABLES_PER_PAT_OTHER;
                }
            }
        }
    }
    ceIter    = ss->textIter;
    firstIx = 0;
    limitIx = 0;

    if (!initTextProcessedIter(ss, status)) { return; }

    if (bufSize>DEFAULT_CEBUFFER_SIZE) {
        buf = (CEI *)uprv_malloc(bufSize * sizeof(CEI));
        if (buf == nullptr) {
            *status = U_MEMORY_ALLOCATION_ERROR;
        }
    }
}

// TODO: add a reset or init function so that allocated
//       buffers can be retained & reused.

CEIBuffer::~CEIBuffer() {
    if (buf != defBuf) {
        uprv_free(buf);
    }
}


// Get the CE with the specified index.
//   Index must be in the range
//          n-history_size < index < n+1
//   where n is the largest index to have been fetched by some previous call to this function.
//   The CE value will be UCOL__PROCESSED_NULLORDER at end of input.
//
const CEI *CEIBuffer::get(int32_t index) {
    int i = index % bufSize;

    if (index>=firstIx && index<limitIx) {
        // The request was for an entry already in our buffer.
        //  Just return it.
        return &buf[i];
    }

    // Caller is requesting a new, never accessed before, CE.
    //   Verify that it is the next one in sequence, which is all
    //   that is allowed.
    if (index != limitIx) {
        UPRV_UNREACHABLE_ASSERT;
        // TODO: In ICU 64 the above was changed from U_ASSERT to UPRV_UNREACHABLE,
        // which unconditionally called abort(). However, there were cases in which it
        // was being hit, so it was changed back to U_ASSERT per ICU-20680. In ICU 70,
        // we now use the new UPRV_UNREACHABLE_ASSERT to better indicate the situation.
        // ICU-20792 tracks the follow-up work/further investigation on this.
        return nullptr;
    }

    // Manage the circular CE buffer indexing
    limitIx++;

    if (limitIx - firstIx >= bufSize) {
        // The buffer is full, knock out the lowest-indexed entry.
        firstIx++;
    }

    UErrorCode status = U_ZERO_ERROR;

    buf[i].ce = strSearch->textProcessedIter->nextProcessed(&buf[i].lowIndex, &buf[i].highIndex, &status);

    return &buf[i];
}

// Get the CE with the specified index.
//   Index must be in the range
//          n-history_size < index < n+1
//   where n is the largest index to have been fetched by some previous call to this function.
//   The CE value will be UCOL__PROCESSED_NULLORDER at end of input.
//
const CEI *CEIBuffer::getPrevious(int32_t index) {
    int i = index % bufSize;

    if (index>=firstIx && index<limitIx) {
        // The request was for an entry already in our buffer.
        //  Just return it.
        return &buf[i];
    }

    // Caller is requesting a new, never accessed before, CE.
    //   Verify that it is the next one in sequence, which is all
    //   that is allowed.
    if (index != limitIx) {
        UPRV_UNREACHABLE_ASSERT;
        // TODO: In ICU 64 the above was changed from U_ASSERT to UPRV_UNREACHABLE,
        // which unconditionally called abort(). However, there were cases in which it
        // was being hit, so it was changed back to U_ASSERT per ICU-20680. In ICU 70,
        // we now use the new UPRV_UNREACHABLE_ASSERT to better indicate the situation.
        // ICU-20792 tracks the follow-up work/further investigation on this.
        return nullptr;
    }

    // Manage the circular CE buffer indexing
    limitIx++;

    if (limitIx - firstIx >= bufSize) {
        // The buffer is full, knock out the lowest-indexed entry.
        firstIx++;
    }

    UErrorCode status = U_ZERO_ERROR;

    buf[i].ce = strSearch->textProcessedIter->previousProcessed(&buf[i].lowIndex, &buf[i].highIndex, &status);

    return &buf[i];
}

}

U_NAMESPACE_END


// #define USEARCH_DEBUG

#ifdef USEARCH_DEBUG
#include <stdio.h>
#include <stdlib.h>
#endif

/*
 * Find the next break boundary after startIndex. If the UStringSearch object
 * has an external break iterator, use that. Otherwise use the internal character
 * break iterator.
 */
static int32_t nextBoundaryAfter(UStringSearch *strsrch, int32_t startIndex, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return startIndex;
    }
#if 0
    const char16_t *text = strsrch->search->text;
    int32_t textLen   = strsrch->search->textLength;

    U_ASSERT(startIndex>=0);
    U_ASSERT(startIndex<=textLen);

    if (startIndex >= textLen) {
        return startIndex;
    }

    UChar32  c;
    int32_t  i = startIndex;
    U16_NEXT(text, i, textLen, c);

    // If we are on a control character, stop without looking for combining marks.
    //    Control characters do not combine.
    int32_t gcProperty = u_getIntPropertyValue(c, UCHAR_GRAPHEME_CLUSTER_BREAK);
    if (gcProperty==U_GCB_CONTROL || gcProperty==U_GCB_LF || gcProperty==U_GCB_CR) {
        return i;
    }

    // The initial character was not a control, and can thus accept trailing
    //   combining characters.  Advance over however many of them there are.
    int32_t  indexOfLastCharChecked;
    for (;;) {
        indexOfLastCharChecked = i;
        if (i>=textLen) {
            break;
        }
        U16_NEXT(text, i, textLen, c);
        gcProperty = u_getIntPropertyValue(c, UCHAR_GRAPHEME_CLUSTER_BREAK);
        if (gcProperty != U_GCB_EXTEND && gcProperty != U_GCB_SPACING_MARK) {
            break;
        }
    }
    return indexOfLastCharChecked;
#elif !UCONFIG_NO_BREAK_ITERATION
    UBreakIterator *breakiterator = getBreakIterator(strsrch, status);
    if (U_FAILURE(status)) {
        return startIndex;
    }

    return ubrk_following(breakiterator, startIndex);
#else
    // **** or should we use the original code? ****
    return startIndex;
#endif

}

/*
 * Returns true if index is on a break boundary. If the UStringSearch
 * has an external break iterator, test using that, otherwise test
 * using the internal character break iterator.
 */
static UBool isBreakBoundary(UStringSearch *strsrch, int32_t index, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return true;
    }
#if 0
    const char16_t *text = strsrch->search->text;
    int32_t textLen   = strsrch->search->textLength;

    U_ASSERT(index>=0);
    U_ASSERT(index<=textLen);

    if (index>=textLen || index<=0) {
        return true;
    }

    // If the character at the current index is not a GRAPHEME_EXTEND
    //    then we can not be within a combining sequence.
    UChar32  c;
    U16_GET(text, 0, index, textLen, c);
    int32_t gcProperty = u_getIntPropertyValue(c, UCHAR_GRAPHEME_CLUSTER_BREAK);
    if (gcProperty != U_GCB_EXTEND && gcProperty != U_GCB_SPACING_MARK) {
        return true;
    }

    // We are at a combining mark.  If the preceding character is anything
    //   except a CONTROL, CR or LF, we are in a combining sequence.
    U16_PREV(text, 0, index, c);
    gcProperty = u_getIntPropertyValue(c, UCHAR_GRAPHEME_CLUSTER_BREAK);
    UBool combining =  !(gcProperty==U_GCB_CONTROL || gcProperty==U_GCB_LF || gcProperty==U_GCB_CR);
    return !combining;
#elif !UCONFIG_NO_BREAK_ITERATION
    UBreakIterator *breakiterator = getBreakIterator(strsrch, status);
    if (U_FAILURE(status)) {
        return true;
    }

    return ubrk_isBoundary(breakiterator, index);
#else
    // **** or use the original code? ****
    return true;
#endif
}

#if 0
static UBool onBreakBoundaries(const UStringSearch *strsrch, int32_t start, int32_t end, UErrorCode &status)
{
    if (U_FAILURE(status)) {
        return true;
    }

#if !UCONFIG_NO_BREAK_ITERATION
    UBreakIterator *breakiterator = getBreakIterator(strsrch, status);
    if (U_SUCCESS(status)) {
        int32_t startindex = ubrk_first(breakiterator);
        int32_t endindex   = ubrk_last(breakiterator);

        // out-of-range indexes are never boundary positions
        if (start < startindex || start > endindex ||
            end < startindex || end > endindex) {
            return false;
        }

        return ubrk_isBoundary(breakiterator, start) &&
               ubrk_isBoundary(breakiterator, end);
    }
#endif

    return true;
}
#endif

typedef enum {
    U_CE_MATCH = -1,
    U_CE_NO_MATCH = 0,
    U_CE_SKIP_TARG,
    U_CE_SKIP_PATN
} UCompareCEsResult;
#define U_CE_LEVEL2_BASE 0x00000005
#define U_CE_LEVEL3_BASE 0x00050000

static UCompareCEsResult compareCE64s(int64_t targCE, int64_t patCE, int16_t compareType) {
    if (targCE == patCE) {
        return U_CE_MATCH;
    }
    if (compareType == 0) {
        return U_CE_NO_MATCH;
    }
    
    int64_t targCEshifted = targCE >> 32;
    int64_t patCEshifted = patCE >> 32;
    int64_t mask;

    mask = 0xFFFF0000;
    int32_t targLev1 = (int32_t)(targCEshifted & mask);
    int32_t patLev1 = (int32_t)(patCEshifted & mask);
    if ( targLev1 != patLev1 ) {
        if ( targLev1 == 0 ) {
            return U_CE_SKIP_TARG;
        }
        if ( patLev1 == 0 && compareType == USEARCH_ANY_BASE_WEIGHT_IS_WILDCARD ) {
            return U_CE_SKIP_PATN;
        }
        return U_CE_NO_MATCH;
    }

    mask = 0x0000FFFF;
    int32_t targLev2 = (int32_t)(targCEshifted & mask);
    int32_t patLev2 = (int32_t)(patCEshifted & mask);
    if ( targLev2 != patLev2 ) {
        if ( targLev2 == 0 ) {
            return U_CE_SKIP_TARG;
        }
        if ( patLev2 == 0 && compareType == USEARCH_ANY_BASE_WEIGHT_IS_WILDCARD ) {
            return U_CE_SKIP_PATN;
        }
        return (patLev2 == U_CE_LEVEL2_BASE || (compareType == USEARCH_ANY_BASE_WEIGHT_IS_WILDCARD && targLev2 == U_CE_LEVEL2_BASE) )?
            U_CE_MATCH: U_CE_NO_MATCH;
    }
    
    mask = 0xFFFF0000;
    int32_t targLev3 = (int32_t)(targCE & mask);
    int32_t patLev3 = (int32_t)(patCE & mask);
    if ( targLev3 != patLev3 ) {
        return (patLev3 == U_CE_LEVEL3_BASE || (compareType == USEARCH_ANY_BASE_WEIGHT_IS_WILDCARD && targLev3 == U_CE_LEVEL3_BASE) )?
            U_CE_MATCH: U_CE_NO_MATCH;
   }

    return U_CE_MATCH;
}

namespace {

UChar32 codePointAt(const USearch &search, int32_t index) {
    if (index < search.textLength) {
        UChar32 c;
        U16_NEXT(search.text, index, search.textLength, c);
        return c;
    }
    return U_SENTINEL;
}

UChar32 codePointBefore(const USearch &search, int32_t index) {
    if (0 < index) {
        UChar32 c;
        U16_PREV(search.text, 0, index, c);
        return c;
    }
    return U_SENTINEL;
}

}  // namespace

U_CAPI UBool U_EXPORT2 usearch_search(UStringSearch  *strsrch,
                                       int32_t        startIdx,
                                       int32_t        *matchStart,
                                       int32_t        *matchLimit,
                                       UErrorCode     *status)
{
    if (U_FAILURE(*status)) {
        return false;
    }

    // TODO:  reject search patterns beginning with a combining char.

#ifdef USEARCH_DEBUG
    if (getenv("USEARCH_DEBUG") != nullptr) {
        printf("Pattern CEs\n");
        for (int ii=0; ii<strsrch->pattern.cesLength; ii++) {
            printf(" %8x", strsrch->pattern.ces[ii]);
        }
        printf("\n");
    }

#endif
    // Input parameter sanity check.
    //  TODO:  should input indices clip to the text length
    //         in the same way that UText does.
    if(strsrch->pattern.cesLength == 0         ||
       startIdx < 0                           ||
       startIdx > strsrch->search->textLength ||
       strsrch->pattern.ces == nullptr) {
           *status = U_ILLEGAL_ARGUMENT_ERROR;
           return false;
    }

    if (strsrch->pattern.pces == nullptr) {
        initializePatternPCETable(strsrch, status);
    }

    ucol_setOffset(strsrch->textIter, startIdx, status);
    CEIBuffer ceb(strsrch, status);

    // An out-of-memory (OOM) failure can occur in the initializePatternPCETable function
    // or CEIBuffer constructor above, so we need to check the status.
    if (U_FAILURE(*status)) {
        return false;
    }

    int32_t    targetIx = 0;
    const CEI *targetCEI = nullptr;
    int32_t    patIx;
    UBool      found;

    int32_t  mStart = -1;
    int32_t  mLimit = -1;
    int32_t  minLimit;
    int32_t  maxLimit;



    // Outer loop moves over match starting positions in the
    //      target CE space.
    // Here we see the target as a sequence of collation elements, resulting from the following:
    // 1. Target characters were decomposed, and (if appropriate) other compressions and expansions are applied
    //    (for example, digraphs such as IJ may be broken into two characters).
    // 2. An int64_t CE weight is determined for each resulting unit (high 16 bits are primary strength, next
    //    16 bits are secondary, next 16 (the high 16 bits of the low 32-bit half) are tertiary. Any of these
    //    fields that are for strengths below that of the collator are set to 0. If this makes the int64_t
    //    CE weight 0 (as for a combining diacritic with secondary weight when the collator strength is primary),
    //    then the CE is deleted, so the following code sees only CEs that are relevant.
    // For each CE, the lowIndex and highIndex correspond to where this CE begins and ends in the original text.
    // If lowIndex==highIndex, either the CE resulted from an expansion/decomposition of one of the original text
    // characters, or the CE marks the limit of the target text (in which case the CE weight is UCOL_PROCESSED_NULLORDER).
    //
    for(targetIx=0; ; targetIx++)
    {
        found = true;
        //  Inner loop checks for a match beginning at each
        //  position from the outer loop.
        int32_t targetIxOffset = 0;
        int64_t patCE = 0;
        // For targetIx > 0, this ceb.get gets a CE that is as far back in the ring buffer
        // (compared to the last CE fetched for the previous targetIx value) as we need to go
        // for this targetIx value, so if it is non-nullptr then other ceb.get calls should be OK.
        const CEI *firstCEI = ceb.get(targetIx);
        if (firstCEI == nullptr) {
            *status = U_INTERNAL_PROGRAM_ERROR;
            found = false;
            break;
        }
        
        for (patIx=0; patIx<strsrch->pattern.pcesLength; patIx++) {
            patCE = strsrch->pattern.pces[patIx];
            targetCEI = ceb.get(targetIx+patIx+targetIxOffset);
            //  Compare CE from target string with CE from the pattern.
            //    Note that the target CE will be UCOL_PROCESSED_NULLORDER if we reach the end of input,
            //    which will fail the compare, below.
            UCompareCEsResult ceMatch = compareCE64s(targetCEI->ce, patCE, strsrch->search->elementComparisonType);
            if ( ceMatch == U_CE_NO_MATCH ) {
                found = false;
                break;
            } else if ( ceMatch > U_CE_NO_MATCH ) {
                if ( ceMatch == U_CE_SKIP_TARG ) {
                    // redo with same patCE, next targCE
                    patIx--;
                    targetIxOffset++;
                } else { // ceMatch == U_CE_SKIP_PATN
                    // redo with same targCE, next patCE
                    targetIxOffset--;
                }
            }
        }
        targetIxOffset += strsrch->pattern.pcesLength; // this is now the offset in target CE space to end of the match so far

        if (!found && ((targetCEI == nullptr) || (targetCEI->ce != UCOL_PROCESSED_NULLORDER))) {
            // No match at this targetIx.  Try again at the next.
            continue;
        }

        if (!found) {
            // No match at all, we have run off the end of the target text.
            break;
        }


        // We have found a match in CE space.
        // Now determine the bounds in string index space.
        //  There still is a chance of match failure if the CE range not correspond to
        //     an acceptable character range.
        //
        const CEI *lastCEI  = ceb.get(targetIx + targetIxOffset - 1);

        mStart   = firstCEI->lowIndex;
        minLimit = lastCEI->lowIndex;

        // Look at the CE following the match.  If it is UCOL_NULLORDER the match
        //   extended to the end of input, and the match is good.

        // Look at the high and low indices of the CE following the match. If
        // they are the same it means one of two things:
        //    1. The match extended to the last CE from the target text, which is OK, or
        //    2. The last CE that was part of the match is in an expansion that extends
        //       to the first CE after the match. In this case, we reject the match.
        const CEI* nextCEI = nullptr;
        if (strsrch->search->elementComparisonType == 0) {
            nextCEI  = ceb.get(targetIx + targetIxOffset);
            maxLimit = nextCEI->lowIndex;
            if (nextCEI->lowIndex == nextCEI->highIndex && nextCEI->ce != UCOL_PROCESSED_NULLORDER) {
                found = false;
            }
        } else {
            for ( ; ; ++targetIxOffset ) {
                nextCEI = ceb.get(targetIx + targetIxOffset);
                maxLimit = nextCEI->lowIndex;
                // If we are at the end of the target too, match succeeds
                if (  nextCEI->ce == UCOL_PROCESSED_NULLORDER ) {
                    break;
                }
                // As long as the next CE has primary weight of 0,
                // it is part of the last target element matched by the pattern;
                // make sure it can be part of a match with the last patCE
                if ( (((nextCEI->ce) >> 32) & 0xFFFF0000UL) == 0 ) {
                    UCompareCEsResult ceMatch = compareCE64s(nextCEI->ce, patCE, strsrch->search->elementComparisonType);
                    if ( ceMatch == U_CE_NO_MATCH || ceMatch == U_CE_SKIP_PATN ) {
                        found = false;
                        break;
                    }
                // If lowIndex == highIndex, this target CE is part of an expansion of the last matched
                // target element, but it has non-zero primary weight => match fails
                } else if ( nextCEI->lowIndex == nextCEI->highIndex ) {
                    found = false;
                    break;
                // Else the target CE is not part of an expansion of the last matched element, match succeeds
                } else {
                    break;
                }
            }
        }


        // Check for the start of the match being within a combining sequence.
        //   This can happen if the pattern itself begins with a combining char, and
        //   the match found combining marks in the target text that were attached
        //    to something else.
        //   This type of match should be rejected for not completely consuming a
        //   combining sequence.
        if (!isBreakBoundary(strsrch, mStart, *status)) {
            found = false;
        }
        if (U_FAILURE(*status)) {
            break;
        }

        // Check for the start of the match being within an Collation Element Expansion,
        //   meaning that the first char of the match is only partially matched.
        //   With expansions, the first CE will report the index of the source
        //   character, and all subsequent (expansions) CEs will report the source index of the
        //    _following_ character.
        int32_t secondIx = firstCEI->highIndex;
        if (mStart == secondIx) {
            found = false;
        }

        // Allow matches to end in the middle of a grapheme cluster if the following
        // conditions are met; this is needed to make prefix search work properly in
        // Indic, see #11750
        // * the default breakIter is being used
        // * the next collation element after this combining sequence
        //   - has non-zero primary weight
        //   - corresponds to a separate character following the one at end of the current match
        //   (the second of these conditions, and perhaps both, may be redundant given the
        //   subsequent check for normalization boundary; however they are likely much faster
        //   tests in any case)
        // * the match limit is a normalization boundary
        UBool allowMidclusterMatch = false;
        if (strsrch->search->text != nullptr && strsrch->search->textLength > maxLimit) {
            allowMidclusterMatch =
                    strsrch->search->breakIter == nullptr &&
                    nextCEI != nullptr && (((nextCEI->ce) >> 32) & 0xFFFF0000UL) != 0 &&
                    maxLimit >= lastCEI->highIndex && nextCEI->highIndex > maxLimit &&
                    (strsrch->nfd->hasBoundaryBefore(codePointAt(*strsrch->search, maxLimit)) ||
                        strsrch->nfd->hasBoundaryAfter(codePointBefore(*strsrch->search, maxLimit)));
        }
        // If those conditions are met, then:
        // * do NOT advance the candidate match limit (mLimit) to a break boundary; however
        //   the match limit may be backed off to a previous break boundary. This handles
        //   cases in which mLimit includes target characters that are ignorable with current
        //   settings (such as space) and which extend beyond the pattern match.
        // * do NOT require that end of the combining sequence not extend beyond the match in CE space
        // * do NOT require that match limit be on a breakIter boundary

        //  Advance the match end position to the first acceptable match boundary.
        //    This advances the index over any combining characters.
        mLimit = maxLimit;
        if (minLimit < maxLimit) {
            // When the last CE's low index is same with its high index, the CE is likely
            // a part of expansion. In this case, the index is located just after the
            // character corresponding to the CEs compared above. If the index is right
            // at the break boundary, move the position to the next boundary will result
            // incorrect match length when there are ignorable characters exist between
            // the position and the next character produces CE(s). See ticket#8482.
            if (minLimit == lastCEI->highIndex && isBreakBoundary(strsrch, minLimit, *status)) {
                mLimit = minLimit;
            } else {
                int32_t nba = nextBoundaryAfter(strsrch, minLimit, *status);
                // Note that we can have nba < maxLimit && nba >= minLImit, in which
                // case we want to set mLimit to nba regardless of allowMidclusterMatch
                // (i.e. we back off mLimit to the previous breakIterator boundary).
                if (nba >= lastCEI->highIndex && (!allowMidclusterMatch || nba < maxLimit)) {
                    mLimit = nba;
                }
            }
        }

        if (U_FAILURE(*status)) {
            break;
        }

    #ifdef USEARCH_DEBUG
        if (getenv("USEARCH_DEBUG") != nullptr) {
            printf("minLimit, maxLimit, mLimit = %d, %d, %d\n", minLimit, maxLimit, mLimit);
        }
    #endif

        if (!allowMidclusterMatch) {
            // If advancing to the end of a combining sequence in character indexing space
            //   advanced us beyond the end of the match in CE space, reject this match.
            if (mLimit > maxLimit) {
                found = false;
            }

            if (!isBreakBoundary(strsrch, mLimit, *status)) {
                found = false;
            }
            if (U_FAILURE(*status)) {
                break;
            }
        }

        if (! checkIdentical(strsrch, mStart, mLimit)) {
            found = false;
        }

        if (found) {
            break;
        }
    }

    #ifdef USEARCH_DEBUG
    if (getenv("USEARCH_DEBUG") != nullptr) {
        printf("Target CEs [%d .. %d]\n", ceb.firstIx, ceb.limitIx);
        int32_t  lastToPrint = ceb.limitIx+2;
        for (int ii=ceb.firstIx; ii<lastToPrint; ii++) {
            printf("%8x@%d ", ceb.get(ii)->ce, ceb.get(ii)->srcIndex);
        }
        printf("\n%s\n", found? "match found" : "no match");
    }
    #endif

    // All Done.  Store back the match bounds to the caller.
    //

    if (U_FAILURE(*status)) {
        found = false; // No match if a failure occured.
    }

    if (found==false) {
        mLimit = -1;
        mStart = -1;
    }

    if (matchStart != nullptr) {
        *matchStart= mStart;
    }

    if (matchLimit != nullptr) {
        *matchLimit = mLimit;
    }

    return found;
}

U_CAPI UBool U_EXPORT2 usearch_searchBackwards(UStringSearch  *strsrch,
                                                int32_t        startIdx,
                                                int32_t        *matchStart,
                                                int32_t        *matchLimit,
                                                UErrorCode     *status)
{
    if (U_FAILURE(*status)) {
        return false;
    }

    // TODO:  reject search patterns beginning with a combining char.

#ifdef USEARCH_DEBUG
    if (getenv("USEARCH_DEBUG") != nullptr) {
        printf("Pattern CEs\n");
        for (int ii=0; ii<strsrch->pattern.cesLength; ii++) {
            printf(" %8x", strsrch->pattern.ces[ii]);
        }
        printf("\n");
    }

#endif
    // Input parameter sanity check.
    //  TODO:  should input indices clip to the text length
    //         in the same way that UText does.
    if(strsrch->pattern.cesLength == 0        ||
       startIdx < 0                           ||
       startIdx > strsrch->search->textLength ||
       strsrch->pattern.ces == nullptr) {
           *status = U_ILLEGAL_ARGUMENT_ERROR;
           return false;
    }

    if (strsrch->pattern.pces == nullptr) {
        initializePatternPCETable(strsrch, status);
    }

    CEIBuffer ceb(strsrch, status);
    int32_t    targetIx = 0;

    /*
     * Pre-load the buffer with the CE's for the grapheme
     * after our starting position so that we're sure that
     * we can look at the CE following the match when we
     * check the match boundaries.
     *
     * This will also pre-fetch the first CE that we'll
     * consider for the match.
     */
    if (startIdx < strsrch->search->textLength) {
        UBreakIterator *breakiterator = getBreakIterator(strsrch, *status);
        if (U_FAILURE(*status)) {
            return false;
        }
        int32_t next = ubrk_following(breakiterator, startIdx);

        ucol_setOffset(strsrch->textIter, next, status);

        for (targetIx = 0; ; targetIx += 1) {
            if (ceb.getPrevious(targetIx)->lowIndex < startIdx) {
                break;
            }
        }
    } else {
        ucol_setOffset(strsrch->textIter, startIdx, status);
    }

    // An out-of-memory (OOM) failure can occur above, so we need to check the status.
    if (U_FAILURE(*status)) {
        return false;
    }

    const CEI *targetCEI = nullptr;
    int32_t    patIx;
    UBool      found;

    int32_t  limitIx = targetIx;
    int32_t  mStart = -1;
    int32_t  mLimit = -1;
    int32_t  minLimit;
    int32_t  maxLimit;



    // Outer loop moves over match starting positions in the
    //      target CE space.
    // Here, targetIx values increase toward the beginning of the base text (i.e. we get the text CEs in reverse order).
    // But  patIx is 0 at the beginning of the pattern and increases toward the end.
    // So this loop performs a comparison starting with the end of pattern, and prcessd toward the beginning of the pattern
    // and the beginning of the base text.
    for(targetIx = limitIx; ; targetIx += 1)
    {
        found = true;
        // For targetIx > limitIx, this ceb.getPrevious gets a CE that is as far back in the ring buffer
        // (compared to the last CE fetched for the previous targetIx value) as we need to go
        // for this targetIx value, so if it is non-nullptr then other ceb.getPrevious calls should be OK.
        const CEI *lastCEI  = ceb.getPrevious(targetIx);
        if (lastCEI == nullptr) {
            *status = U_INTERNAL_PROGRAM_ERROR;
            found = false;
             break;
        }
        //  Inner loop checks for a match beginning at each
        //  position from the outer loop.
        int32_t targetIxOffset = 0;
        for (patIx = strsrch->pattern.pcesLength - 1; patIx >= 0; patIx -= 1) {
            int64_t patCE = strsrch->pattern.pces[patIx];

            targetCEI = ceb.getPrevious(targetIx + strsrch->pattern.pcesLength - 1 - patIx + targetIxOffset);
            //  Compare CE from target string with CE from the pattern.
            //    Note that the target CE will be UCOL_NULLORDER if we reach the end of input,
            //    which will fail the compare, below.
            UCompareCEsResult ceMatch = compareCE64s(targetCEI->ce, patCE, strsrch->search->elementComparisonType);
            if ( ceMatch == U_CE_NO_MATCH ) {
                found = false;
                break;
            } else if ( ceMatch > U_CE_NO_MATCH ) {
                if ( ceMatch == U_CE_SKIP_TARG ) {
                    // redo with same patCE, next targCE
                    patIx++;
                    targetIxOffset++;
                } else { // ceMatch == U_CE_SKIP_PATN
                    // redo with same targCE, next patCE
                    targetIxOffset--;
                }
            }
        }

        if (!found && ((targetCEI == nullptr) || (targetCEI->ce != UCOL_PROCESSED_NULLORDER))) {
            // No match at this targetIx.  Try again at the next.
            continue;
        }

        if (!found) {
            // No match at all, we have run off the end of the target text.
            break;
        }


        // We have found a match in CE space.
        // Now determine the bounds in string index space.
        //  There still is a chance of match failure if the CE range not correspond to
        //     an acceptable character range.
        //
        const CEI *firstCEI = ceb.getPrevious(targetIx + strsrch->pattern.pcesLength - 1 + targetIxOffset);
        mStart   = firstCEI->lowIndex;

        // Check for the start of the match being within a combining sequence.
        //   This can happen if the pattern itself begins with a combining char, and
        //   the match found combining marks in the target text that were attached
        //    to something else.
        //   This type of match should be rejected for not completely consuming a
        //   combining sequence.
        if (!isBreakBoundary(strsrch, mStart, *status)) {
            found = false;
        }
        if (U_FAILURE(*status)) {
            break;
        }

        // Look at the high index of the first CE in the match. If it's the same as the
        // low index, the first CE in the match is in the middle of an expansion.
        if (mStart == firstCEI->highIndex) {
            found = false;
        }


        minLimit = lastCEI->lowIndex;

        if (targetIx > 0) {
            // Look at the CE following the match.  If it is UCOL_NULLORDER the match
            //   extended to the end of input, and the match is good.

            // Look at the high and low indices of the CE following the match. If
            // they are the same it means one of two things:
            //    1. The match extended to the last CE from the target text, which is OK, or
            //    2. The last CE that was part of the match is in an expansion that extends
            //       to the first CE after the match. In this case, we reject the match.
            const CEI *nextCEI  = ceb.getPrevious(targetIx - 1);

            if (nextCEI->lowIndex == nextCEI->highIndex && nextCEI->ce != UCOL_PROCESSED_NULLORDER) {
                found = false;
            }

            mLimit = maxLimit = nextCEI->lowIndex;

            // Allow matches to end in the middle of a grapheme cluster if the following
            // conditions are met; this is needed to make prefix search work properly in
            // Indic, see #11750
            // * the default breakIter is being used
            // * the next collation element after this combining sequence
            //   - has non-zero primary weight
            //   - corresponds to a separate character following the one at end of the current match
            //   (the second of these conditions, and perhaps both, may be redundant given the
            //   subsequent check for normalization boundary; however they are likely much faster
            //   tests in any case)
            // * the match limit is a normalization boundary
            UBool allowMidclusterMatch = false;
            if (strsrch->search->text != nullptr && strsrch->search->textLength > maxLimit) {
                allowMidclusterMatch =
                        strsrch->search->breakIter == nullptr &&
                        nextCEI != nullptr && (((nextCEI->ce) >> 32) & 0xFFFF0000UL) != 0 &&
                        maxLimit >= lastCEI->highIndex && nextCEI->highIndex > maxLimit &&
                        (strsrch->nfd->hasBoundaryBefore(codePointAt(*strsrch->search, maxLimit)) ||
                            strsrch->nfd->hasBoundaryAfter(codePointBefore(*strsrch->search, maxLimit)));
            }
            // If those conditions are met, then:
            // * do NOT advance the candidate match limit (mLimit) to a break boundary; however
            //   the match limit may be backed off to a previous break boundary. This handles
            //   cases in which mLimit includes target characters that are ignorable with current
            //   settings (such as space) and which extend beyond the pattern match.
            // * do NOT require that end of the combining sequence not extend beyond the match in CE space
            // * do NOT require that match limit be on a breakIter boundary

            //  Advance the match end position to the first acceptable match boundary.
            //    This advances the index over any combining characters.
            if (minLimit < maxLimit) {
                int32_t nba = nextBoundaryAfter(strsrch, minLimit, *status);
                // Note that we can have nba < maxLimit && nba >= minLImit, in which
                // case we want to set mLimit to nba regardless of allowMidclusterMatch
                // (i.e. we back off mLimit to the previous breakIterator boundary).
                if (nba >= lastCEI->highIndex && (!allowMidclusterMatch || nba < maxLimit)) {
                    mLimit = nba;
                }
            }

            if (!allowMidclusterMatch) {
                // If advancing to the end of a combining sequence in character indexing space
                //   advanced us beyond the end of the match in CE space, reject this match.
                if (mLimit > maxLimit) {
                    found = false;
                }

                // Make sure the end of the match is on a break boundary
                if (!isBreakBoundary(strsrch, mLimit, *status)) {
                    found = false;
                }
                if (U_FAILURE(*status)) {
                    break;
                }
            }

        } else {
            // No non-ignorable CEs after this point.
            // The maximum position is detected by boundary after
            // the last non-ignorable CE. Combining sequence
            // across the start index will be truncated.
            int32_t nba = nextBoundaryAfter(strsrch, minLimit, *status);
            mLimit = maxLimit = (nba > 0) && (startIdx > nba) ? nba : startIdx;
        }

    #ifdef USEARCH_DEBUG
        if (getenv("USEARCH_DEBUG") != nullptr) {
            printf("minLimit, maxLimit, mLimit = %d, %d, %d\n", minLimit, maxLimit, mLimit);
        }
    #endif


        if (! checkIdentical(strsrch, mStart, mLimit)) {
            found = false;
        }

        if (found) {
            break;
        }
    }

    #ifdef USEARCH_DEBUG
    if (getenv("USEARCH_DEBUG") != nullptr) {
        printf("Target CEs [%d .. %d]\n", ceb.firstIx, ceb.limitIx);
        int32_t  lastToPrint = ceb.limitIx+2;
        for (int ii=ceb.firstIx; ii<lastToPrint; ii++) {
            printf("%8x@%d ", ceb.get(ii)->ce, ceb.get(ii)->srcIndex);
        }
        printf("\n%s\n", found? "match found" : "no match");
    }
    #endif

    // All Done.  Store back the match bounds to the caller.
    //

    if (U_FAILURE(*status)) {
        found = false; // No match if a failure occured.
    }

    if (found==false) {
        mLimit = -1;
        mStart = -1;
    }

    if (matchStart != nullptr) {
        *matchStart= mStart;
    }

    if (matchLimit != nullptr) {
        *matchLimit = mLimit;
    }

    return found;
}

// internal use methods declared in usrchimp.h -----------------------------

UBool usearch_handleNextExact(UStringSearch *strsrch, UErrorCode *status)
{
    if (U_FAILURE(*status)) {
        setMatchNotFound(strsrch, *status);
        return false;
    }

    int32_t textOffset = ucol_getOffset(strsrch->textIter);
    int32_t start = -1;
    int32_t end = -1;

    if (usearch_search(strsrch, textOffset, &start, &end, status)) {
        strsrch->search->matchedIndex  = start;
        strsrch->search->matchedLength = end - start;
        return true;
    } else {
        setMatchNotFound(strsrch, *status);
        return false;
    }
}

UBool usearch_handleNextCanonical(UStringSearch *strsrch, UErrorCode *status)
{
    if (U_FAILURE(*status)) {
        setMatchNotFound(strsrch, *status);
        return false;
    }

    int32_t textOffset = ucol_getOffset(strsrch->textIter);
    int32_t start = -1;
    int32_t end = -1;

    if (usearch_search(strsrch, textOffset, &start, &end, status)) {
        strsrch->search->matchedIndex  = start;
        strsrch->search->matchedLength = end - start;
        return true;
    } else {
        setMatchNotFound(strsrch, *status);
        return false;
    }
}

UBool usearch_handlePreviousExact(UStringSearch *strsrch, UErrorCode *status)
{
    if (U_FAILURE(*status)) {
        setMatchNotFound(strsrch, *status);
        return false;
    }

    int32_t textOffset;

    if (strsrch->search->isOverlap) {
        if (strsrch->search->matchedIndex != USEARCH_DONE) {
            textOffset = strsrch->search->matchedIndex + strsrch->search->matchedLength - 1;
        } else {
            // move the start position at the end of possible match
            initializePatternPCETable(strsrch, status);
            if (!initTextProcessedIter(strsrch, status)) {
                setMatchNotFound(strsrch, *status);
                return false;
            }
            for (int32_t nPCEs = 0; nPCEs < strsrch->pattern.pcesLength - 1; nPCEs++) {
                int64_t pce = strsrch->textProcessedIter->nextProcessed(nullptr, nullptr, status);
                if (pce == UCOL_PROCESSED_NULLORDER) {
                    // at the end of the text
                    break;
                }
            }
            if (U_FAILURE(*status)) {
                setMatchNotFound(strsrch, *status);
                return false;
            }
            textOffset = ucol_getOffset(strsrch->textIter);
        }
    } else {
        textOffset = ucol_getOffset(strsrch->textIter);
    }

    int32_t start = -1;
    int32_t end = -1;

    if (usearch_searchBackwards(strsrch, textOffset, &start, &end, status)) {
        strsrch->search->matchedIndex = start;
        strsrch->search->matchedLength = end - start;
        return true;
    } else {
        setMatchNotFound(strsrch, *status);
        return false;
    }
}

UBool usearch_handlePreviousCanonical(UStringSearch *strsrch,
                                      UErrorCode    *status)
{
    if (U_FAILURE(*status)) {
        setMatchNotFound(strsrch, *status);
        return false;
    }

    int32_t textOffset;

    if (strsrch->search->isOverlap) {
        if (strsrch->search->matchedIndex != USEARCH_DONE) {
            textOffset = strsrch->search->matchedIndex + strsrch->search->matchedLength - 1;
        } else {
            // move the start position at the end of possible match
            initializePatternPCETable(strsrch, status);
            if (!initTextProcessedIter(strsrch, status)) {
                setMatchNotFound(strsrch, *status);
                return false;
            }
            for (int32_t nPCEs = 0; nPCEs < strsrch->pattern.pcesLength - 1; nPCEs++) {
                int64_t pce = strsrch->textProcessedIter->nextProcessed(nullptr, nullptr, status);
                if (pce == UCOL_PROCESSED_NULLORDER) {
                    // at the end of the text
                    break;
                }
            }
            if (U_FAILURE(*status)) {
                setMatchNotFound(strsrch, *status);
                return false;
            }
            textOffset = ucol_getOffset(strsrch->textIter);
        }
    } else {
        textOffset = ucol_getOffset(strsrch->textIter);
    }

    int32_t start = -1;
    int32_t end = -1;

    if (usearch_searchBackwards(strsrch, textOffset, &start, &end, status)) {
        strsrch->search->matchedIndex = start;
        strsrch->search->matchedLength = end - start;
        return true;
    } else {
        setMatchNotFound(strsrch, *status);
        return false;
    }
}

#endif /* #if !UCONFIG_NO_COLLATION */
