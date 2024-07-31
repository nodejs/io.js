// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
* Copyright (c) 2002-2014, International Business Machines
* Corporation and others.  All Rights Reserved.
**********************************************************************
* Author: Alan Liu
* Created: November 11 2002
* Since: ICU 2.4
**********************************************************************
*/
#include "utypeinfo.h"  // for 'typeid' to work 

#include "unicode/ustring.h"
#include "unicode/strenum.h"
#include "unicode/putil.h"
#include "uenumimp.h"
#include "ustrenum.h"
#include "cstring.h"
#include "cmemory.h"
#include "uassert.h"

U_NAMESPACE_BEGIN
// StringEnumeration implementation ---------------------------------------- ***

StringEnumeration::StringEnumeration()
    : chars(charsBuffer), charsCapacity(sizeof(charsBuffer)) {
}

StringEnumeration::~StringEnumeration() {
    if (chars != nullptr && chars != charsBuffer) {
        uprv_free(chars);
    }
}

// StringEnumeration base class clone() default implementation, does not clone
StringEnumeration *
StringEnumeration::clone() const {
  return nullptr;
}

const char *
StringEnumeration::next(int32_t *resultLength, UErrorCode &status) {
    const UnicodeString *s=snext(status);
    if(U_SUCCESS(status) && s!=nullptr) {
        unistr=*s;
        ensureCharsCapacity(unistr.length()+1, status);
        if(U_SUCCESS(status)) {
            if(resultLength!=nullptr) {
                *resultLength=unistr.length();
            }
            unistr.extract(0, INT32_MAX, chars, charsCapacity, US_INV);
            return chars;
        }
    }

    return nullptr;
}

const char16_t *
StringEnumeration::unext(int32_t *resultLength, UErrorCode &status) {
    const UnicodeString *s=snext(status);
    if(U_SUCCESS(status) && s!=nullptr) {
        unistr=*s;
        if(resultLength!=nullptr) {
            *resultLength=unistr.length();
        }
        return unistr.getTerminatedBuffer();
    }

    return nullptr;
}

const UnicodeString *
StringEnumeration::snext(UErrorCode &status) {
    int32_t length;
    const char *s=next(&length, status);
    return setChars(s, length, status);
}

void
StringEnumeration::ensureCharsCapacity(int32_t capacity, UErrorCode &status) {
    if(U_SUCCESS(status) && capacity>charsCapacity) {
        if(capacity<(charsCapacity+charsCapacity/2)) {
            // avoid allocation thrashing
            capacity=charsCapacity+charsCapacity/2;
        }
        if(chars!=charsBuffer) {
            uprv_free(chars);
        }
        chars=(char *)uprv_malloc(capacity);
        if(chars==nullptr) {
            chars=charsBuffer;
            charsCapacity=sizeof(charsBuffer);
            status=U_MEMORY_ALLOCATION_ERROR;
        } else {
            charsCapacity=capacity;
        }
    }
}

UnicodeString *
StringEnumeration::setChars(const char *s, int32_t length, UErrorCode &status) {
    if(U_SUCCESS(status) && s!=nullptr) {
        if(length<0) {
            length=(int32_t)uprv_strlen(s);
        }

        char16_t *buffer=unistr.getBuffer(length+1);
        if(buffer!=nullptr) {
            u_charsToUChars(s, buffer, length);
            buffer[length]=0;
            unistr.releaseBuffer(length);
            return &unistr;
        } else {
            status=U_MEMORY_ALLOCATION_ERROR;
        }
    }

    return nullptr;
}
bool
StringEnumeration::operator==(const StringEnumeration& that)const {
    return typeid(*this) == typeid(that); 
}

bool
StringEnumeration::operator!=(const StringEnumeration& that)const {
    return !operator==(that);
}

// UStringEnumeration implementation --------------------------------------- ***

UStringEnumeration * U_EXPORT2
UStringEnumeration::fromUEnumeration(
        UEnumeration *uenumToAdopt, UErrorCode &status) {
    if (U_FAILURE(status)) {
        uenum_close(uenumToAdopt);
        return nullptr;
    }
    UStringEnumeration *result = new UStringEnumeration(uenumToAdopt);
    if (result == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        uenum_close(uenumToAdopt);
        return nullptr;
    }
    return result;
}

UStringEnumeration::UStringEnumeration(UEnumeration* _uenum) :
    uenum(_uenum) {
    U_ASSERT(_uenum != 0);
}

UStringEnumeration::~UStringEnumeration() {
    uenum_close(uenum);
}

int32_t UStringEnumeration::count(UErrorCode& status) const {
    return uenum_count(uenum, &status);
}

const char *UStringEnumeration::next(int32_t *resultLength, UErrorCode &status) {
    return uenum_next(uenum, resultLength, &status);
}

const UnicodeString* UStringEnumeration::snext(UErrorCode& status) {
    int32_t length;
    const char16_t* str = uenum_unext(uenum, &length, &status);
    if (str == nullptr || U_FAILURE(status)) {
        return nullptr;
    }
    return &unistr.setTo(str, length);
}

void UStringEnumeration::reset(UErrorCode& status) {
    uenum_reset(uenum, &status);
}

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(UStringEnumeration)
U_NAMESPACE_END

// C wrapper --------------------------------------------------------------- ***

#define THIS(en) ((icu::StringEnumeration*)(en->context))

U_CDECL_BEGIN

/**
 * Wrapper API to make StringEnumeration look like UEnumeration.
 */
static void U_CALLCONV
ustrenum_close(UEnumeration* en) {
    delete THIS(en);
    uprv_free(en);
}

/**
 * Wrapper API to make StringEnumeration look like UEnumeration.
 */
static int32_t U_CALLCONV
ustrenum_count(UEnumeration* en,
               UErrorCode* ec)
{
    return THIS(en)->count(*ec);
}

/**
 * Wrapper API to make StringEnumeration look like UEnumeration.
 */
static const char16_t* U_CALLCONV
ustrenum_unext(UEnumeration* en,
               int32_t* resultLength,
               UErrorCode* ec)
{
    return THIS(en)->unext(resultLength, *ec);
}

/**
 * Wrapper API to make StringEnumeration look like UEnumeration.
 */
static const char* U_CALLCONV
ustrenum_next(UEnumeration* en,
              int32_t* resultLength,
              UErrorCode* ec)
{
    return THIS(en)->next(resultLength, *ec);
}

/**
 * Wrapper API to make StringEnumeration look like UEnumeration.
 */
static void U_CALLCONV
ustrenum_reset(UEnumeration* en,
               UErrorCode* ec)
{
    THIS(en)->reset(*ec);
}

/**
 * Pseudo-vtable for UEnumeration wrapper around StringEnumeration.
 * The StringEnumeration pointer will be stored in 'context'.
 */
static const UEnumeration USTRENUM_VT = {
    nullptr,
    nullptr, // store StringEnumeration pointer here
    ustrenum_close,
    ustrenum_count,
    ustrenum_unext,
    ustrenum_next,
    ustrenum_reset
};

U_CDECL_END

/**
 * Given a StringEnumeration, wrap it in a UEnumeration.  The
 * StringEnumeration is adopted; after this call, the caller must not
 * delete it (regardless of error status).
 */
U_CAPI UEnumeration* U_EXPORT2
uenum_openFromStringEnumeration(icu::StringEnumeration* adopted, UErrorCode* ec) { 
    UEnumeration* result = nullptr;
    if (U_SUCCESS(*ec) && adopted != nullptr) {
        result = (UEnumeration*) uprv_malloc(sizeof(UEnumeration));
        if (result == nullptr) {
            *ec = U_MEMORY_ALLOCATION_ERROR;
        } else {
            uprv_memcpy(result, &USTRENUM_VT, sizeof(USTRENUM_VT));
            result->context = adopted;
        }
    }
    if (result == nullptr) {
        delete adopted;
    }
    return result;
}

// C wrapper --------------------------------------------------------------- ***

U_CDECL_BEGIN

typedef struct UCharStringEnumeration {
    UEnumeration uenum;
    int32_t index, count;
} UCharStringEnumeration;

static void U_CALLCONV
ucharstrenum_close(UEnumeration* en) {
    uprv_free(en);
}

static int32_t U_CALLCONV
ucharstrenum_count(UEnumeration* en,
                   UErrorCode* /*ec*/) {
    return ((UCharStringEnumeration*)en)->count;
}

static const char16_t* U_CALLCONV
ucharstrenum_unext(UEnumeration* en,
                  int32_t* resultLength,
                  UErrorCode* /*ec*/) {
    UCharStringEnumeration *e = (UCharStringEnumeration*) en;
    if (e->index >= e->count) {
        return nullptr;
    }
    const char16_t* result = ((const char16_t**)e->uenum.context)[e->index++];
    if (resultLength) {
        *resultLength = (int32_t)u_strlen(result);
    }
    return result;
}


static const char* U_CALLCONV
ucharstrenum_next(UEnumeration* en,
                  int32_t* resultLength,
                  UErrorCode* /*ec*/) {
    UCharStringEnumeration *e = (UCharStringEnumeration*) en;
    if (e->index >= e->count) {
        return nullptr;
    }
    const char* result = ((const char**)e->uenum.context)[e->index++];
    if (resultLength) {
        *resultLength = (int32_t)uprv_strlen(result);
    }
    return result;
}

static void U_CALLCONV
ucharstrenum_reset(UEnumeration* en,
                   UErrorCode* /*ec*/) {
    ((UCharStringEnumeration*)en)->index = 0;
}

static const UEnumeration UCHARSTRENUM_VT = {
    nullptr,
    nullptr, // store StringEnumeration pointer here
    ucharstrenum_close,
    ucharstrenum_count,
    uenum_unextDefault,
    ucharstrenum_next,
    ucharstrenum_reset
};

static const UEnumeration UCHARSTRENUM_U_VT = {
    nullptr,
    nullptr, // store StringEnumeration pointer here
    ucharstrenum_close,
    ucharstrenum_count,
    ucharstrenum_unext,
    uenum_nextDefault,
    ucharstrenum_reset
};

U_CDECL_END

U_CAPI UEnumeration* U_EXPORT2
uenum_openCharStringsEnumeration(const char* const strings[], int32_t count,
                                 UErrorCode* ec) {
    UCharStringEnumeration* result = nullptr;
    if (U_SUCCESS(*ec) && count >= 0 && (count == 0 || strings != nullptr)) {
        result = (UCharStringEnumeration*) uprv_malloc(sizeof(UCharStringEnumeration));
        if (result == nullptr) {
            *ec = U_MEMORY_ALLOCATION_ERROR;
        } else {
            U_ASSERT((char*)result==(char*)(&result->uenum));
            uprv_memcpy(result, &UCHARSTRENUM_VT, sizeof(UCHARSTRENUM_VT));
            result->uenum.context = (void*)strings;
            result->index = 0;
            result->count = count;
        }
    }
    return (UEnumeration*) result;
}

U_CAPI UEnumeration* U_EXPORT2
uenum_openUCharStringsEnumeration(const char16_t* const strings[], int32_t count,
                                 UErrorCode* ec) {
    UCharStringEnumeration* result = nullptr;
    if (U_SUCCESS(*ec) && count >= 0 && (count == 0 || strings != nullptr)) {
        result = (UCharStringEnumeration*) uprv_malloc(sizeof(UCharStringEnumeration));
        if (result == nullptr) {
            *ec = U_MEMORY_ALLOCATION_ERROR;
        } else {
            U_ASSERT((char*)result==(char*)(&result->uenum));
            uprv_memcpy(result, &UCHARSTRENUM_U_VT, sizeof(UCHARSTRENUM_U_VT));
            result->uenum.context = (void*)strings;
            result->index = 0;
            result->count = count;
        }
    }
    return (UEnumeration*) result;
}


// end C Wrapper
