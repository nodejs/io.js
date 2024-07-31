// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
*   Copyright (c) 2001-2011, International Business Machines
*   Corporation and others.  All Rights Reserved.
**********************************************************************
*   Date        Name        Description
*   04/02/2001  aliu        Creation.
**********************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_TRANSLITERATION

#include "remtrans.h"
#include "unicode/unifilt.h"

static const char16_t CURR_ID[] = {65, 110, 121, 45, 0x52, 0x65, 0x6D, 0x6F, 0x76, 0x65, 0x00}; /* "Any-Remove" */

U_NAMESPACE_BEGIN

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(RemoveTransliterator)

/**
 * Factory method
 */
static Transliterator* RemoveTransliterator_create(const UnicodeString& /*ID*/,
                                                   Transliterator::Token /*context*/) {
    /* We don't need the ID or context. We just remove data */
    return new RemoveTransliterator();
}

/**
 * System registration hook.
 */
void RemoveTransliterator::registerIDs() {

    Transliterator::_registerFactory(UnicodeString(true, ::CURR_ID, -1),
                                     RemoveTransliterator_create, integerToken(0));

    Transliterator::_registerSpecialInverse(UNICODE_STRING_SIMPLE("Remove"),
                                            UNICODE_STRING_SIMPLE("Null"), false);
}

RemoveTransliterator::RemoveTransliterator() : Transliterator(UnicodeString(true, ::CURR_ID, -1), nullptr) {}

RemoveTransliterator::~RemoveTransliterator() {}

RemoveTransliterator* RemoveTransliterator::clone() const {
    RemoveTransliterator* result = new RemoveTransliterator();
    if (result != nullptr && getFilter() != nullptr) {
        result->adoptFilter(getFilter()->clone());
    }
    return result;
}

void RemoveTransliterator::handleTransliterate(Replaceable& text, UTransPosition& index,
                                               UBool /*isIncremental*/) const {
    // Our caller (filteredTransliterate) has already narrowed us
    // to an unfiltered run.  Delete it.
    UnicodeString empty;
    text.handleReplaceBetween(index.start, index.limit, empty);
    int32_t len = index.limit - index.start;
    index.contextLimit -= len;
    index.limit -= len;
}
U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_TRANSLITERATION */
