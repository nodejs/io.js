// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
*   Copyright (C) 2001-2011, International Business Machines
*   Corporation and others.  All Rights Reserved.
**********************************************************************
*   Date        Name        Description
*   05/24/01    aliu        Creation.
**********************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_TRANSLITERATION

#include "unicode/uchar.h"
#include "unicode/uniset.h"
#include "unicode/ustring.h"
#include "unicode/utf16.h"
#include "titletrn.h"
#include "umutex.h"
#include "ucase.h"
#include "cpputils.h"

U_NAMESPACE_BEGIN

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(TitlecaseTransliterator)

TitlecaseTransliterator::TitlecaseTransliterator() :
    CaseMapTransliterator(UNICODE_STRING("Any-Title", 9), nullptr)
{
    // Need to look back 2 characters in the case of "can't"
    setMaximumContextLength(2);
}

/**
 * Destructor.
 */
TitlecaseTransliterator::~TitlecaseTransliterator() {
}

/**
 * Copy constructor.
 */
TitlecaseTransliterator::TitlecaseTransliterator(const TitlecaseTransliterator& o) :
    CaseMapTransliterator(o)
{
}

/**
 * Assignment operator.
 */
/*TitlecaseTransliterator& TitlecaseTransliterator::operator=(
                             const TitlecaseTransliterator& o) {
    CaseMapTransliterator::operator=(o);
    return *this;
}*/

/**
 * Transliterator API.
 */
TitlecaseTransliterator* TitlecaseTransliterator::clone() const {
    return new TitlecaseTransliterator(*this);
}

/**
 * Implements {@link Transliterator#handleTransliterate}.
 */
void TitlecaseTransliterator::handleTransliterate(
                                  Replaceable& text, UTransPosition& offsets,
                                  UBool isIncremental) const
{
    // TODO reimplement, see ustrcase.c
    // using a real word break iterator
    //   instead of just looking for a transition between cased and uncased characters
    // call CaseMapTransliterator::handleTransliterate() for lowercasing? (set fMap)
    // needs to take isIncremental into account because case mappings are context-sensitive
    //   also detect when lowercasing function did not finish because of context

    if (offsets.start >= offsets.limit) {
        return;
    }

    // case type: >0 cased (UCASE_LOWER etc.)  ==0 uncased  <0 case-ignorable
    int32_t type;

    // Our mode; we are either converting letter toTitle or
    // toLower.
    UBool doTitle = true;
    
    // Determine if there is a preceding context of cased case-ignorable*,
    // in which case we want to start in toLower mode.  If the
    // prior context is anything else (including empty) then start
    // in toTitle mode.
    UChar32 c;
    int32_t start;
    for (start = offsets.start - 1; start >= offsets.contextStart; start -= U16_LENGTH(c)) {
        c = text.char32At(start);
        type=ucase_getTypeOrIgnorable(c);
        if(type>0) { // cased
            doTitle=false;
            break;
        } else if(type==0) { // uncased but not ignorable
            break;
        }
        // else (type<0) case-ignorable: continue
    }
    
    // Convert things after a cased character toLower; things
    // after an uncased, non-case-ignorable character toTitle.  Case-ignorable
    // characters are copied directly and do not change the mode.
    UCaseContext csc;
    uprv_memset(&csc, 0, sizeof(csc));
    csc.p = &text;
    csc.start = offsets.contextStart;
    csc.limit = offsets.contextLimit;

    UnicodeString tmp;
    const char16_t *s;
    int32_t textPos, delta, result;

    for(textPos=offsets.start; textPos<offsets.limit;) {
        csc.cpStart=textPos;
        c=text.char32At(textPos);
        csc.cpLimit=textPos+=U16_LENGTH(c);

        type=ucase_getTypeOrIgnorable(c);
        if(type>=0) { // not case-ignorable
            if(doTitle) {
                result=ucase_toFullTitle(c, utrans_rep_caseContextIterator, &csc, &s, UCASE_LOC_ROOT);
            } else {
                result=ucase_toFullLower(c, utrans_rep_caseContextIterator, &csc, &s, UCASE_LOC_ROOT);
            }
            doTitle = static_cast<UBool>(type == 0); // doTitle=isUncased

            if(csc.b1 && isIncremental) {
                // fMap() tried to look beyond the context limit
                // wait for more input
                offsets.start=csc.cpStart;
                return;
            }

            if(result>=0) {
                // replace the current code point with its full case mapping result
                // see UCASE_MAX_STRING_LENGTH
                if(result<=UCASE_MAX_STRING_LENGTH) {
                    // string s[result]
                    tmp.setTo(false, s, result);
                    delta=result-U16_LENGTH(c);
                } else {
                    // single code point
                    tmp.setTo(result);
                    delta=tmp.length()-U16_LENGTH(c);
                }
                text.handleReplaceBetween(csc.cpStart, textPos, tmp);
                if(delta!=0) {
                    textPos+=delta;
                    csc.limit=offsets.contextLimit+=delta;
                    offsets.limit+=delta;
                }
            }
        }
    }
    offsets.start=textPos;
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_TRANSLITERATION */
