// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*
*   Copyright (C) 2011, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*   file name:  uniset_closure.cpp
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 2011may30
*   created by: Markus W. Scherer
*
*   UnicodeSet::closeOver() and related methods moved here from uniset_props.cpp
*   to simplify dependencies.
*   In particular, this depends on the BreakIterator, but the BreakIterator
*   code also builds UnicodeSets from patterns and needs uniset_props.
*/

#include "unicode/brkiter.h"
#include "unicode/locid.h"
#include "unicode/parsepos.h"
#include "unicode/uniset.h"
#include "unicode/utf16.h"
#include "cmemory.h"
#include "ruleiter.h"
#include "ucase.h"
#include "uprops.h"
#include "util.h"
#include "uvector.h"

U_NAMESPACE_BEGIN

// TODO memory debugging provided inside uniset.cpp
// could be made available here but probably obsolete with use of modern
// memory leak checker tools
#define _dbgct(me)

//----------------------------------------------------------------
// Constructors &c
//----------------------------------------------------------------

UnicodeSet::UnicodeSet(const UnicodeString& pattern,
                       uint32_t options,
                       const SymbolTable* symbols,
                       UErrorCode& status) {
    applyPattern(pattern, options, symbols, status);
    _dbgct(this);
}

UnicodeSet::UnicodeSet(const UnicodeString& pattern, ParsePosition& pos,
                       uint32_t options,
                       const SymbolTable* symbols,
                       UErrorCode& status) {
    applyPattern(pattern, pos, options, symbols, status);
    _dbgct(this);
}

//----------------------------------------------------------------
// Public API
//----------------------------------------------------------------

UnicodeSet& UnicodeSet::applyPattern(const UnicodeString& pattern,
                                     uint32_t options,
                                     const SymbolTable* symbols,
                                     UErrorCode& status) {
    ParsePosition pos(0);
    applyPattern(pattern, pos, options, symbols, status);
    if (U_FAILURE(status)) return *this;

    int32_t i = pos.getIndex();

    if (options & USET_IGNORE_SPACE) {
        // Skip over trailing whitespace
        ICU_Utility::skipWhitespace(pattern, i, true);
    }

    if (i != pattern.length()) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
    }
    return *this;
}

UnicodeSet& UnicodeSet::applyPattern(const UnicodeString& pattern,
                              ParsePosition& pos,
                              uint32_t options,
                              const SymbolTable* symbols,
                              UErrorCode& status) {
    if (U_FAILURE(status)) {
        return *this;
    }
    if (isFrozen()) {
        status = U_NO_WRITE_PERMISSION;
        return *this;
    }
    // Need to build the pattern in a temporary string because
    // _applyPattern calls add() etc., which set pat to empty.
    UnicodeString rebuiltPat;
    RuleCharacterIterator chars(pattern, symbols, pos);
    applyPattern(chars, symbols, rebuiltPat, options, &UnicodeSet::closeOver, 0, status);
    if (U_FAILURE(status)) return *this;
    if (chars.inVariable()) {
        // syntaxError(chars, "Extra chars in variable value");
        status = U_MALFORMED_SET;
        return *this;
    }
    setPattern(rebuiltPat);
    return *this;
}

// USetAdder implementation
// Does not use uset.h to reduce code dependencies
static void U_CALLCONV
_set_add(USet *set, UChar32 c) {
    reinterpret_cast<UnicodeSet*>(set)->add(c);
}

static void U_CALLCONV
_set_addRange(USet *set, UChar32 start, UChar32 end) {
    reinterpret_cast<UnicodeSet*>(set)->add(start, end);
}

static void U_CALLCONV
_set_addString(USet *set, const char16_t *str, int32_t length) {
    reinterpret_cast<UnicodeSet*>(set)->add(UnicodeString(static_cast<UBool>(length < 0), str, length));
}

//----------------------------------------------------------------
// Case folding API
//----------------------------------------------------------------

// add the result of a full case mapping to the set
// use str as a temporary string to avoid constructing one
static inline void
addCaseMapping(UnicodeSet &set, int32_t result, const char16_t *full, UnicodeString &str) {
    if(result >= 0) {
        if(result > UCASE_MAX_STRING_LENGTH) {
            // add a single-code point case mapping
            set.add(result);
        } else {
            // add a string case mapping from full with length result
            str.setTo(static_cast<UBool>(false), full, result);
            set.add(str);
        }
    }
    // result < 0: the code point mapped to itself, no need to add it
    // see ucase.h
}

namespace {

/** For case closure on a large set, look only at code points with relevant properties. */
const UnicodeSet &maybeOnlyCaseSensitive(const UnicodeSet &src, UnicodeSet &subset) {
    // The subset must have been constructed with all code points,
    // so that the retainAll() intersection effectively copies all single code points from src.
    U_ASSERT(subset.contains(0, 0x10ffff));
    if (src.size() < 30) {
        return src;
    }
    // Return the intersection of the src code points with Case_Sensitive ones.
    UErrorCode errorCode = U_ZERO_ERROR;
    const UnicodeSet *sensitive =
        CharacterProperties::getBinaryPropertySet(UCHAR_CASE_SENSITIVE, errorCode);
    if (U_FAILURE(errorCode)) {
        return src;
    }
    // Start by copying the "smaller" set.
    // (We "copy" by intersecting all Unicode *code points* with the first set,
    // which omits any strings.)
    if (src.getRangeCount() > sensitive->getRangeCount()) {
        subset.retainAll(*sensitive);
        subset.retainAll(src);
    } else {
        subset.retainAll(src);
        subset.retainAll(*sensitive);
    }
    return subset;
}

// Per-character scf = Simple_Case_Folding of a string.
// (Normally when we case-fold a string we use full case foldings.)
bool scfString(const UnicodeString &s, UnicodeString &scf) {
    // Iterate over the raw buffer for best performance.
    const char16_t *p = s.getBuffer();
    int32_t length = s.length();
    // Loop while not needing modification.
    for (int32_t i = 0; i < length;) {
        UChar32 c;
        U16_NEXT(p, i, length, c);  // post-increments i
        UChar32 scfChar = u_foldCase(c, U_FOLD_CASE_DEFAULT);
        if (scfChar != c) {
            // Copy the characters before c.
            scf.setTo(p, i - U16_LENGTH(c));
            // Loop over the rest of the string and keep case-folding.
            for (;;) {
                scf.append(scfChar);
                if (i == length) {
                    return true;
                }
                U16_NEXT(p, i, length, c);  // post-increments i
                scfChar = u_foldCase(c, U_FOLD_CASE_DEFAULT);
            }
        }
    }
    return false;
}

}  // namespace

UnicodeSet& UnicodeSet::closeOver(int32_t attribute) {
    if (isFrozen() || isBogus()) {
        return *this;
    }
    switch (attribute & USET_CASE_MASK) {
    case 0:
        break;
    case USET_CASE_INSENSITIVE:
        closeOverCaseInsensitive(/* simple= */ false);
        break;
    case USET_ADD_CASE_MAPPINGS:
        closeOverAddCaseMappings();
        break;
    case USET_SIMPLE_CASE_INSENSITIVE:
        closeOverCaseInsensitive(/* simple= */ true);
        break;
    default:
        // bad option (unreachable)
        break;
    }
    return *this;
}

void UnicodeSet::closeOverCaseInsensitive(bool simple) {
    // Start with input set to guarantee inclusion.
    UnicodeSet foldSet(*this);
    // Full case mappings closure:
    // Remove strings because the strings will actually be reduced (folded);
    // therefore, start with no strings and add only those needed.
    // Do this before processing code points, because they may add strings.
    if (!simple && foldSet.hasStrings()) {
        foldSet.strings_->removeAllElements();
    }

    USetAdder sa = {
        foldSet.toUSet(),
        _set_add,
        _set_addRange,
        _set_addString,
        nullptr, // don't need remove()
        nullptr // don't need removeRange()
    };

    UnicodeSet subset(0, 0x10ffff);
    const UnicodeSet &codePoints = maybeOnlyCaseSensitive(*this, subset);

    // Iterate over the ranges of single code points. Nested loop for each code point.
    int32_t n = codePoints.getRangeCount();

    for (int32_t i=0; i<n; ++i) {
        UChar32 start = codePoints.getRangeStart(i);
        UChar32 end   = codePoints.getRangeEnd(i);

        if (simple) {
            for (UChar32 cp=start; cp<=end; ++cp) {
                ucase_addSimpleCaseClosure(cp, &sa);
            }
        } else {
            for (UChar32 cp=start; cp<=end; ++cp) {
                ucase_addCaseClosure(cp, &sa);
            }
        }
    }
    if (hasStrings()) {
        UnicodeString str;
        for (int32_t j=0; j<strings_->size(); ++j) {
            const UnicodeString* pStr = static_cast<const UnicodeString*>(strings_->elementAt(j));
            if (simple) {
                if (scfString(*pStr, str)) {
                    foldSet.remove(*pStr).add(str);
                }
            } else {
                str = *pStr;
                str.foldCase();
                if(!ucase_addStringCaseClosure(str.getBuffer(), str.length(), &sa)) {
                    foldSet.add(str); // does not map to code points: add the folded string itself
                }
            }
        }
    }
    *this = foldSet;
}

void UnicodeSet::closeOverAddCaseMappings() {
    // Start with input set to guarantee inclusion.
    UnicodeSet foldSet(*this);

    UnicodeSet subset(0, 0x10ffff);
    const UnicodeSet &codePoints = maybeOnlyCaseSensitive(*this, subset);

    // Iterate over the ranges of single code points. Nested loop for each code point.
    int32_t n = codePoints.getRangeCount();
    UChar32 result;
    const char16_t *full;
    UnicodeString str;

    for (int32_t i=0; i<n; ++i) {
        UChar32 start = codePoints.getRangeStart(i);
        UChar32 end   = codePoints.getRangeEnd(i);

        // add case mappings
        // (does not add long s for regular s, or Kelvin for k, for example)
        for (UChar32 cp=start; cp<=end; ++cp) {
            result = ucase_toFullLower(cp, nullptr, nullptr, &full, UCASE_LOC_ROOT);
            addCaseMapping(foldSet, result, full, str);

            result = ucase_toFullTitle(cp, nullptr, nullptr, &full, UCASE_LOC_ROOT);
            addCaseMapping(foldSet, result, full, str);

            result = ucase_toFullUpper(cp, nullptr, nullptr, &full, UCASE_LOC_ROOT);
            addCaseMapping(foldSet, result, full, str);

            result = ucase_toFullFolding(cp, &full, 0);
            addCaseMapping(foldSet, result, full, str);
        }
    }
    if (hasStrings()) {
        Locale root("");
#if !UCONFIG_NO_BREAK_ITERATION
        UErrorCode status = U_ZERO_ERROR;
        BreakIterator *bi = BreakIterator::createWordInstance(root, status);
        if (U_SUCCESS(status)) {
#endif
            for (int32_t j=0; j<strings_->size(); ++j) {
                const UnicodeString* pStr = static_cast<const UnicodeString*>(strings_->elementAt(j));
                (str = *pStr).toLower(root);
                foldSet.add(str);
#if !UCONFIG_NO_BREAK_ITERATION
                (str = *pStr).toTitle(bi, root);
                foldSet.add(str);
#endif
                (str = *pStr).toUpper(root);
                foldSet.add(str);
                (str = *pStr).foldCase();
                foldSet.add(str);
            }
#if !UCONFIG_NO_BREAK_ITERATION
        }
        delete bi;
#endif
    }
    *this = foldSet;
}

U_NAMESPACE_END
