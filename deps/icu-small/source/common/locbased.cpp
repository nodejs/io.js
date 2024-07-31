// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
* Copyright (c) 2004-2014, International Business Machines
* Corporation and others.  All Rights Reserved.
**********************************************************************
* Author: Alan Liu
* Created: January 16 2004
* Since: ICU 2.8
**********************************************************************
*/
#include "locbased.h"
#include "cstring.h"

U_NAMESPACE_BEGIN

Locale LocaleBased::getLocale(ULocDataLocaleType type, UErrorCode& status) const {
    const char* id = getLocaleID(type, status);
    return Locale(id != nullptr ? id : "");
}

const char* LocaleBased::getLocaleID(ULocDataLocaleType type, UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return nullptr;
    }

    switch(type) {
    case ULOC_VALID_LOCALE:
        return valid;
    case ULOC_ACTUAL_LOCALE:
        return actual;
    default:
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }
}

void LocaleBased::setLocaleIDs(const char* validID, const char* actualID) {
    if (validID != nullptr) {
      uprv_strncpy(valid, validID, ULOC_FULLNAME_CAPACITY);
      valid[ULOC_FULLNAME_CAPACITY-1] = 0; // always terminate
    }
    if (actualID != nullptr) {
      uprv_strncpy(actual, actualID, ULOC_FULLNAME_CAPACITY);
      actual[ULOC_FULLNAME_CAPACITY-1] = 0; // always terminate
    }
}

void LocaleBased::setLocaleIDs(const Locale& validID, const Locale& actualID) {
  uprv_strcpy(valid, validID.getName());
  uprv_strcpy(actual, actualID.getName());
}

U_NAMESPACE_END
