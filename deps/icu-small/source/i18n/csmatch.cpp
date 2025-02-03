// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
 **********************************************************************
 *   Copyright (C) 2005-2012, International Business Machines
 *   Corporation and others.  All Rights Reserved.
 **********************************************************************
 */

#include "unicode/utypes.h"

#if !UCONFIG_NO_CONVERSION
#include "unicode/unistr.h"
#include "unicode/ucnv.h"

#include "csmatch.h"

#include "csrecog.h"
#include "inputext.h"

U_NAMESPACE_BEGIN

CharsetMatch::CharsetMatch()
  : textIn(nullptr), confidence(0), fCharsetName(nullptr), fLang(nullptr)
{
    // nothing else to do.
}

void CharsetMatch::set(InputText *input, const CharsetRecognizer *cr, int32_t conf,
                       const char *csName, const char *lang)
{
    textIn = input;
    confidence = conf; 
    fCharsetName = csName;
    fLang = lang;
    if (cr != nullptr) {
        if (fCharsetName == nullptr) {
            fCharsetName = cr->getName();
        }
        if (fLang == nullptr) {
            fLang = cr->getLanguage();
        }
    }
}

const char* CharsetMatch::getName()const
{
    return fCharsetName; 
}

const char* CharsetMatch::getLanguage()const
{
    return fLang; 
}

int32_t CharsetMatch::getConfidence()const
{
    return confidence;
}

int32_t CharsetMatch::getUChars(char16_t *buf, int32_t cap, UErrorCode *status) const
{
    UConverter *conv = ucnv_open(getName(), status);
    int32_t result = ucnv_toUChars(conv, buf, cap, reinterpret_cast<const char*>(textIn->fRawInput), textIn->fRawLength, status);

    ucnv_close(conv);

    return result;
}

U_NAMESPACE_END

#endif
