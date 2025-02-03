// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
******************************************************************************
*   Copyright (C) 2001-2016, International Business Machines
*   Corporation and others.  All Rights Reserved.
******************************************************************************
*
* File ucoleitr.cpp
*
* Modification History:
*
* Date        Name        Description
* 02/15/2001  synwee      Modified all methods to process its own function 
*                         instead of calling the equivalent c++ api (coleitr.h)
* 2012-2014   markus      Rewritten in C++ again.
******************************************************************************/

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

#include "unicode/coleitr.h"
#include "unicode/tblcoll.h"
#include "unicode/ucoleitr.h"
#include "unicode/ustring.h"
#include "unicode/sortkey.h"
#include "unicode/uobject.h"
#include "cmemory.h"
#include "usrchimp.h"

U_NAMESPACE_USE

#define BUFFER_LENGTH             100

#define DEFAULT_BUFFER_SIZE 16
#define BUFFER_GROW 8

#define ARRAY_COPY(dst, src, count) uprv_memcpy((void *) (dst), (void *) (src), (size_t)(count) * sizeof (src)[0])

#define NEW_ARRAY(type, count) (type *) uprv_malloc((size_t)(count) * sizeof(type))

#define DELETE_ARRAY(array) uprv_free((void *) (array))

struct RCEI
{
    uint32_t ce;
    int32_t  low;
    int32_t  high;
};

U_NAMESPACE_BEGIN

struct RCEBuffer
{
    RCEI    defaultBuffer[DEFAULT_BUFFER_SIZE];
    RCEI   *buffer;
    int32_t bufferIndex;
    int32_t bufferSize;

    RCEBuffer();
    ~RCEBuffer();

    UBool isEmpty() const;
    void  put(uint32_t ce, int32_t ixLow, int32_t ixHigh, UErrorCode &errorCode);
    const RCEI *get();
};

RCEBuffer::RCEBuffer()
{
    buffer = defaultBuffer;
    bufferIndex = 0;
    bufferSize = UPRV_LENGTHOF(defaultBuffer);
}

RCEBuffer::~RCEBuffer()
{
    if (buffer != defaultBuffer) {
        DELETE_ARRAY(buffer);
    }
}

UBool RCEBuffer::isEmpty() const
{
    return bufferIndex <= 0;
}

void RCEBuffer::put(uint32_t ce, int32_t ixLow, int32_t ixHigh, UErrorCode &errorCode)
{
    if (U_FAILURE(errorCode)) {
        return;
    }
    if (bufferIndex >= bufferSize) {
        RCEI *newBuffer = NEW_ARRAY(RCEI, bufferSize + BUFFER_GROW);
        if (newBuffer == nullptr) {
            errorCode = U_MEMORY_ALLOCATION_ERROR;
            return;
        }

        ARRAY_COPY(newBuffer, buffer, bufferSize);

        if (buffer != defaultBuffer) {
            DELETE_ARRAY(buffer);
        }

        buffer = newBuffer;
        bufferSize += BUFFER_GROW;
    }

    buffer[bufferIndex].ce   = ce;
    buffer[bufferIndex].low  = ixLow;
    buffer[bufferIndex].high = ixHigh;

    bufferIndex += 1;
}

const RCEI *RCEBuffer::get()
{
    if (bufferIndex > 0) {
     return &buffer[--bufferIndex];
    }

    return nullptr;
}

PCEBuffer::PCEBuffer()
{
    buffer = defaultBuffer;
    bufferIndex = 0;
    bufferSize = UPRV_LENGTHOF(defaultBuffer);
}

PCEBuffer::~PCEBuffer()
{
    if (buffer != defaultBuffer) {
        DELETE_ARRAY(buffer);
    }
}

void PCEBuffer::reset()
{
    bufferIndex = 0;
}

UBool PCEBuffer::isEmpty() const
{
    return bufferIndex <= 0;
}

void PCEBuffer::put(uint64_t ce, int32_t ixLow, int32_t ixHigh, UErrorCode &errorCode)
{
    if (U_FAILURE(errorCode)) {
        return;
    }
    if (bufferIndex >= bufferSize) {
        PCEI *newBuffer = NEW_ARRAY(PCEI, bufferSize + BUFFER_GROW);
        if (newBuffer == nullptr) {
            errorCode = U_MEMORY_ALLOCATION_ERROR;
            return;
        }

        ARRAY_COPY(newBuffer, buffer, bufferSize);

        if (buffer != defaultBuffer) {
            DELETE_ARRAY(buffer);
        }

        buffer = newBuffer;
        bufferSize += BUFFER_GROW;
    }

    buffer[bufferIndex].ce   = ce;
    buffer[bufferIndex].low  = ixLow;
    buffer[bufferIndex].high = ixHigh;

    bufferIndex += 1;
}

const PCEI *PCEBuffer::get()
{
    if (bufferIndex > 0) {
     return &buffer[--bufferIndex];
    }

    return nullptr;
}

UCollationPCE::UCollationPCE(UCollationElements *elems) { init(elems); }

UCollationPCE::UCollationPCE(CollationElementIterator *iter) { init(iter); }

void UCollationPCE::init(UCollationElements *elems) {
    init(CollationElementIterator::fromUCollationElements(elems));
}

void UCollationPCE::init(CollationElementIterator *iter)
{
    cei = iter;
    init(*iter->rbc_);
}

void UCollationPCE::init(const Collator &coll)
{
    UErrorCode status = U_ZERO_ERROR;

    strength    = coll.getAttribute(UCOL_STRENGTH, status);
    toShift     = coll.getAttribute(UCOL_ALTERNATE_HANDLING, status) == UCOL_SHIFTED;
    isShifted   = false;
    variableTop = coll.getVariableTop(status);
}

UCollationPCE::~UCollationPCE()
{
    // nothing to do
}

uint64_t UCollationPCE::processCE(uint32_t ce)
{
    uint64_t primary = 0, secondary = 0, tertiary = 0, quaternary = 0;

    // This is clean, but somewhat slow...
    // We could apply the mask to ce and then
    // just get all three orders...
    switch(strength) {
    default:
        tertiary = ucol_tertiaryOrder(ce);
        U_FALLTHROUGH;

    case UCOL_SECONDARY:
        secondary = ucol_secondaryOrder(ce);
        U_FALLTHROUGH;

    case UCOL_PRIMARY:
        primary = ucol_primaryOrder(ce);
    }

    // **** This should probably handle continuations too.  ****
    // **** That means that we need 24 bits for the primary ****
    // **** instead of the 16 that we're currently using.   ****
    // **** So we can lay out the 64 bits as: 24.12.12.16.  ****
    // **** Another complication with continuations is that ****
    // **** the *second* CE is marked as a continuation, so ****
    // **** we always have to peek ahead to know how long   ****
    // **** the primary is...                               ****
    if ((toShift && variableTop > ce && primary != 0)
                || (isShifted && primary == 0)) {

        if (primary == 0) {
            return UCOL_IGNORABLE;
        }

        if (strength >= UCOL_QUATERNARY) {
            quaternary = primary;
        }

        primary = secondary = tertiary = 0;
        isShifted = true;
    } else {
        if (strength >= UCOL_QUATERNARY) {
            quaternary = 0xFFFF;
        }

        isShifted = false;
    }

    return primary << 48 | secondary << 32 | tertiary << 16 | quaternary;
}

U_NAMESPACE_END

/* public methods ---------------------------------------------------- */

U_CAPI UCollationElements* U_EXPORT2
ucol_openElements(const UCollator  *coll,
                  const char16_t   *text,
                        int32_t    textLength,
                        UErrorCode *status)
{
    if (U_FAILURE(*status)) {
        return nullptr;
    }
    if (coll == nullptr || (text == nullptr && textLength != 0)) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }
    const RuleBasedCollator *rbc = RuleBasedCollator::rbcFromUCollator(coll);
    if (rbc == nullptr) {
        *status = U_UNSUPPORTED_ERROR;  // coll is a Collator but not a RuleBasedCollator
        return nullptr;
    }

    UnicodeString s(textLength < 0, text, textLength);
    CollationElementIterator *cei = rbc->createCollationElementIterator(s);
    if (cei == nullptr) {
        *status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }

    return cei->toUCollationElements();
}


U_CAPI void U_EXPORT2
ucol_closeElements(UCollationElements *elems)
{
    delete CollationElementIterator::fromUCollationElements(elems);
}

U_CAPI void U_EXPORT2
ucol_reset(UCollationElements *elems)
{
    CollationElementIterator::fromUCollationElements(elems)->reset();
}

U_CAPI int32_t U_EXPORT2
ucol_next(UCollationElements *elems, 
          UErrorCode         *status)
{
    if (U_FAILURE(*status)) {
        return UCOL_NULLORDER;
    }

    return CollationElementIterator::fromUCollationElements(elems)->next(*status);
}

U_NAMESPACE_BEGIN

int64_t
UCollationPCE::nextProcessed(
                   int32_t            *ixLow,
                   int32_t            *ixHigh,
                   UErrorCode         *status)
{
    int64_t result = UCOL_IGNORABLE;
    uint32_t low = 0, high = 0;

    if (U_FAILURE(*status)) {
        return UCOL_PROCESSED_NULLORDER;
    }

    pceBuffer.reset();

    do {
        low = cei->getOffset();
        int32_t ce = cei->next(*status);
        high = cei->getOffset();

        if (ce == UCOL_NULLORDER) {
             result = UCOL_PROCESSED_NULLORDER;
             break;
        }

        result = processCE(static_cast<uint32_t>(ce));
    } while (result == UCOL_IGNORABLE);

    if (ixLow != nullptr) {
        *ixLow = low;
    }

    if (ixHigh != nullptr) {
        *ixHigh = high;
    }

    return result;
}

U_NAMESPACE_END

U_CAPI int32_t U_EXPORT2
ucol_previous(UCollationElements *elems,
              UErrorCode         *status)
{
    if(U_FAILURE(*status)) {
        return UCOL_NULLORDER;
    }
    return CollationElementIterator::fromUCollationElements(elems)->previous(*status);
}

U_NAMESPACE_BEGIN

int64_t
UCollationPCE::previousProcessed(
                   int32_t            *ixLow,
                   int32_t            *ixHigh,
                   UErrorCode         *status)
{
    int64_t result = UCOL_IGNORABLE;
    int32_t  low = 0, high = 0;

    if (U_FAILURE(*status)) {
        return UCOL_PROCESSED_NULLORDER;
    }

    // pceBuffer.reset();

    while (pceBuffer.isEmpty()) {
        // buffer raw CEs up to non-ignorable primary
        RCEBuffer rceb;
        int32_t ce;
        
        // **** do we need to reset rceb, or will it always be empty at this point ****
        do {
            high = cei->getOffset();
            ce   = cei->previous(*status);
            low  = cei->getOffset();

            if (ce == UCOL_NULLORDER) {
                if (!rceb.isEmpty()) {
                    break;
                }

                goto finish;
            }

            rceb.put(static_cast<uint32_t>(ce), low, high, *status);
        } while (U_SUCCESS(*status) && ((ce & UCOL_PRIMARYORDERMASK) == 0 || isContinuation(ce)));

        // process the raw CEs
        while (U_SUCCESS(*status) && !rceb.isEmpty()) {
            const RCEI *rcei = rceb.get();

            result = processCE(rcei->ce);

            if (result != UCOL_IGNORABLE) {
                pceBuffer.put(result, rcei->low, rcei->high, *status);
            }
        }
        if (U_FAILURE(*status)) {
            return UCOL_PROCESSED_NULLORDER;
        }
    }

finish:
    if (pceBuffer.isEmpty()) {
        // **** Is -1 the right value for ixLow, ixHigh? ****
    	if (ixLow != nullptr) {
    		*ixLow = -1;
    	}
    	
    	if (ixHigh != nullptr) {
    		*ixHigh = -1
    		;
    	}
        return UCOL_PROCESSED_NULLORDER;
    }

    const PCEI *pcei = pceBuffer.get();

    if (ixLow != nullptr) {
        *ixLow = pcei->low;
    }

    if (ixHigh != nullptr) {
        *ixHigh = pcei->high;
    }

    return pcei->ce;
}

U_NAMESPACE_END

U_CAPI int32_t U_EXPORT2
ucol_getMaxExpansion(const UCollationElements *elems,
                           int32_t            order)
{
    return CollationElementIterator::fromUCollationElements(elems)->getMaxExpansion(order);

    // TODO: The old code masked the order according to strength and then did a binary search.
    // However this was probably at least partially broken because of the following comment.
    // Still, it might have found a match when this version may not.

    // FIXME: with a masked search, there might be more than one hit,
    // so we need to look forward and backward from the match to find all
    // of the hits...
}

U_CAPI void U_EXPORT2
ucol_setText(      UCollationElements *elems,
             const char16_t           *text,
                   int32_t            textLength,
                   UErrorCode         *status)
{
    if (U_FAILURE(*status)) {
        return;
    }

    if ((text == nullptr && textLength != 0)) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    UnicodeString s(textLength < 0, text, textLength);
    return CollationElementIterator::fromUCollationElements(elems)->setText(s, *status);
}

U_CAPI int32_t U_EXPORT2
ucol_getOffset(const UCollationElements *elems)
{
    return CollationElementIterator::fromUCollationElements(elems)->getOffset();
}

U_CAPI void U_EXPORT2
ucol_setOffset(UCollationElements    *elems,
               int32_t           offset,
               UErrorCode            *status)
{
    if (U_FAILURE(*status)) {
        return;
    }

    CollationElementIterator::fromUCollationElements(elems)->setOffset(offset, *status);
}

U_CAPI int32_t U_EXPORT2
ucol_primaryOrder (int32_t order) 
{
    return (order >> 16) & 0xffff;
}

U_CAPI int32_t U_EXPORT2
ucol_secondaryOrder (int32_t order) 
{
    return (order >> 8) & 0xff;
}

U_CAPI int32_t U_EXPORT2
ucol_tertiaryOrder (int32_t order) 
{
    return order & 0xff;
}

#endif /* #if !UCONFIG_NO_COLLATION */
