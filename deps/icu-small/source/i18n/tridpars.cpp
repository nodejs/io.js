// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
*   Copyright (c) 2002-2014, International Business Machines Corporation
*   and others.  All Rights Reserved.
**********************************************************************
*   Date        Name        Description
*   01/14/2002  aliu        Creation.
**********************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_TRANSLITERATION

#include "tridpars.h"
#include "hash.h"
#include "mutex.h"
#include "transreg.h"
#include "uassert.h"
#include "ucln_in.h"
#include "unicode/parsepos.h"
#include "unicode/translit.h"
#include "unicode/uchar.h"
#include "unicode/uniset.h"
#include "unicode/unistr.h"
#include "unicode/utrans.h"
#include "util.h"
#include "uvector.h"

U_NAMESPACE_BEGIN

static const char16_t ID_DELIM    = 0x003B; // ;
static const char16_t TARGET_SEP  = 0x002D; // -
static const char16_t VARIANT_SEP = 0x002F; // /
static const char16_t OPEN_REV    = 0x0028; // (
static const char16_t CLOSE_REV   = 0x0029; // )

//static const char16_t EMPTY[]     = {0}; // ""
static const char16_t ANY[]       = {65,110,121,0}; // "Any"
static const char16_t ANY_NULL[]  = {65,110,121,45,78,117,108,108,0}; // "Any-Null"

static const int32_t FORWARD = UTRANS_FORWARD;
static const int32_t REVERSE = UTRANS_REVERSE;

static Hashtable* SPECIAL_INVERSES = nullptr;
static UInitOnce gSpecialInversesInitOnce {};

/**
 * The mutex controlling access to SPECIAL_INVERSES
 */
static UMutex LOCK;

TransliteratorIDParser::Specs::Specs(const UnicodeString& s, const UnicodeString& t,
                                     const UnicodeString& v, UBool sawS,
                                     const UnicodeString& f) {
    source = s;
    target = t;
    variant = v;
    sawSource = sawS;
    filter = f;
}

TransliteratorIDParser::SingleID::SingleID(const UnicodeString& c, const UnicodeString& b,
                                           const UnicodeString& f) {
    canonID = c;
    basicID = b;
    filter = f;
}

TransliteratorIDParser::SingleID::SingleID(const UnicodeString& c, const UnicodeString& b) {
    canonID = c;
    basicID = b;
}

Transliterator* TransliteratorIDParser::SingleID::createInstance() {
    Transliterator* t;
    if (basicID.length() == 0) {
        t = createBasicInstance(UnicodeString(true, ANY_NULL, 8), &canonID);
    } else {
        t = createBasicInstance(basicID, &canonID);
    }
    if (t != nullptr) {
        if (filter.length() != 0) {
            UErrorCode ec = U_ZERO_ERROR;
            UnicodeSet *set = new UnicodeSet(filter, ec);
            if (U_FAILURE(ec)) {
                delete set;
            } else {
                t->adoptFilter(set);
            }
        }
    }
    return t;
}


/**
 * Parse a single ID, that is, an ID of the general form
 * "[f1] s1-t1/v1 ([f2] s2-t3/v2)", with the parenthesized element
 * optional, the filters optional, and the variants optional.
 * @param id the id to be parsed
 * @param pos INPUT-OUTPUT parameter.  On input, the position of
 * the first character to parse.  On output, the position after
 * the last character parsed.
 * @param dir the direction.  If the direction is REVERSE then the
 * SingleID is constructed for the reverse direction.
 * @return a SingleID object or nullptr
 */
TransliteratorIDParser::SingleID*
TransliteratorIDParser::parseSingleID(const UnicodeString& id, int32_t& pos,
                                      int32_t dir, UErrorCode& status) {

    int32_t start = pos;

    // The ID will be of the form A, A(), A(B), or (B), where
    // A and B are filter IDs.
    Specs* specsA = nullptr;
    Specs* specsB = nullptr;
    UBool sawParen = false;

    // On the first pass, look for (B) or ().  If this fails, then
    // on the second pass, look for A, A(B), or A().
    for (int32_t pass=1; pass<=2; ++pass) {
        if (pass == 2) {
            specsA = parseFilterID(id, pos, true);
            if (specsA == nullptr) {
                pos = start;
                return nullptr;
            }
        }
        if (ICU_Utility::parseChar(id, pos, OPEN_REV)) {
            sawParen = true;
            if (!ICU_Utility::parseChar(id, pos, CLOSE_REV)) {
                specsB = parseFilterID(id, pos, true);
                // Must close with a ')'
                if (specsB == nullptr || !ICU_Utility::parseChar(id, pos, CLOSE_REV)) {
                    delete specsA;
                    pos = start;
                    return nullptr;
                }
            }
            break;
        }
    }

    // Assemble return results
    SingleID* single;
    if (sawParen) {
        if (dir == FORWARD) {
            SingleID* b = specsToID(specsB, FORWARD);
            single = specsToID(specsA, FORWARD);
            // Null pointers check
            if (b == nullptr || single == nullptr) {
            	delete b;
            	delete single;
            	status = U_MEMORY_ALLOCATION_ERROR;
            	return nullptr;
            }
            single->canonID.append(OPEN_REV)
                .append(b->canonID).append(CLOSE_REV);
            if (specsA != nullptr) {
                single->filter = specsA->filter;
            }
            delete b;
        } else {
            SingleID* a = specsToID(specsA, FORWARD);
            single = specsToID(specsB, FORWARD);
            // Check for null pointer.
            if (a == nullptr || single == nullptr) {
            	delete a;
            	delete single;
            	status = U_MEMORY_ALLOCATION_ERROR;
            	return nullptr;
            }
            single->canonID.append(OPEN_REV)
                .append(a->canonID).append(CLOSE_REV);
            if (specsB != nullptr) {
                single->filter = specsB->filter;
            }
            delete a;
        }
    } else {
        // assert(specsA != nullptr);
        if (dir == FORWARD) {
            single = specsToID(specsA, FORWARD);
        } else {
            single = specsToSpecialInverse(*specsA, status);
            if (single == nullptr) {
                single = specsToID(specsA, REVERSE);
            }
        }
        // Check for nullptr pointer
        if (single == nullptr) {
        	status = U_MEMORY_ALLOCATION_ERROR;
        	return nullptr;
        }
        single->filter = specsA->filter;
    }

    delete specsA;
    delete specsB;

    return single;
}

/**
 * Parse a filter ID, that is, an ID of the general form
 * "[f1] s1-t1/v1", with the filters optional, and the variants optional.
 * @param id the id to be parsed
 * @param pos INPUT-OUTPUT parameter.  On input, the position of
 * the first character to parse.  On output, the position after
 * the last character parsed.
 * @return a SingleID object or null if the parse fails
 */
TransliteratorIDParser::SingleID*
TransliteratorIDParser::parseFilterID(const UnicodeString& id, int32_t& pos) {

    int32_t start = pos;

    Specs* specs = parseFilterID(id, pos, true);
    if (specs == nullptr) {
        pos = start;
        return nullptr;
    }

    // Assemble return results
    SingleID* single = specsToID(specs, FORWARD);
    if (single != nullptr) {
        single->filter = specs->filter;
    }
    delete specs;
    return single;
}

/**
 * Parse a global filter of the form "[f]" or "([f])", depending
 * on 'withParens'.
 * @param id the pattern the parse
 * @param pos INPUT-OUTPUT parameter.  On input, the position of
 * the first character to parse.  On output, the position after
 * the last character parsed.
 * @param dir the direction.
 * @param withParens INPUT-OUTPUT parameter.  On entry, if
 * withParens is 0, then parens are disallowed.  If it is 1,
 * then parens are requires.  If it is -1, then parens are
 * optional, and the return result will be set to 0 or 1.
 * @param canonID OUTPUT parameter.  The pattern for the filter
 * added to the canonID, either at the end, if dir is FORWARD, or
 * at the start, if dir is REVERSE.  The pattern will be enclosed
 * in parentheses if appropriate, and will be suffixed with an
 * ID_DELIM character.  May be nullptr.
 * @return a UnicodeSet object or nullptr.  A non-nullptr results
 * indicates a successful parse, regardless of whether the filter
 * applies to the given direction.  The caller should discard it
 * if withParens != (dir == REVERSE).
 */
UnicodeSet* TransliteratorIDParser::parseGlobalFilter(const UnicodeString& id, int32_t& pos,
                                                      int32_t dir,
                                                      int32_t& withParens,
                                                      UnicodeString* canonID) {
    UnicodeSet* filter = nullptr;
    int32_t start = pos;

    if (withParens == -1) {
        withParens = ICU_Utility::parseChar(id, pos, OPEN_REV) ? 1 : 0;
    } else if (withParens == 1) {
        if (!ICU_Utility::parseChar(id, pos, OPEN_REV)) {
            pos = start;
            return nullptr;
        }
    }

    ICU_Utility::skipWhitespace(id, pos, true);

    if (UnicodeSet::resemblesPattern(id, pos)) {
        ParsePosition ppos(pos);
        UErrorCode ec = U_ZERO_ERROR;
        filter = new UnicodeSet(id, ppos, USET_IGNORE_SPACE, nullptr, ec);
        /* test for nullptr */
        if (filter == nullptr) {
            pos = start;
            return nullptr;
        }
        if (U_FAILURE(ec)) {
            delete filter;
            pos = start;
            return nullptr;
        }

        UnicodeString pattern;
        id.extractBetween(pos, ppos.getIndex(), pattern);
        pos = ppos.getIndex();

        if (withParens == 1 && !ICU_Utility::parseChar(id, pos, CLOSE_REV)) {
            delete filter;
            pos = start;
            return nullptr;
        }

        // In the forward direction, append the pattern to the
        // canonID.  In the reverse, insert it at zero, and invert
        // the presence of parens ("A" <-> "(A)").
        if (canonID != nullptr) {
            if (dir == FORWARD) {
                if (withParens == 1) {
                    pattern.insert(0, OPEN_REV);
                    pattern.append(CLOSE_REV);
                }
                canonID->append(pattern).append(ID_DELIM);
            } else {
                if (withParens == 0) {
                    pattern.insert(0, OPEN_REV);
                    pattern.append(CLOSE_REV);
                }
                canonID->insert(0, pattern);
                canonID->insert(pattern.length(), ID_DELIM);
            }
        }
    }

    return filter;
}

U_CDECL_BEGIN
static void U_CALLCONV _deleteSingleID(void* obj) {
    delete (TransliteratorIDParser::SingleID*) obj;
}

static void U_CALLCONV _deleteTransliteratorTrIDPars(void* obj) {
    delete (Transliterator*) obj;
}
U_CDECL_END

/**
 * Parse a compound ID, consisting of an optional forward global
 * filter, a separator, one or more single IDs delimited by
 * separators, an an optional reverse global filter.  The
 * separator is a semicolon.  The global filters are UnicodeSet
 * patterns.  The reverse global filter must be enclosed in
 * parentheses.
 * @param id the pattern the parse
 * @param dir the direction.
 * @param canonID OUTPUT parameter that receives the canonical ID,
 * consisting of canonical IDs for all elements, as returned by
 * parseSingleID(), separated by semicolons.  Previous contents
 * are discarded.
 * @param list OUTPUT parameter that receives a list of SingleID
 * objects representing the parsed IDs.  Previous contents are
 * discarded.
 * @param globalFilter OUTPUT parameter that receives a pointer to
 * a newly created global filter for this ID in this direction, or
 * nullptr if there is none.
 * @return true if the parse succeeds, that is, if the entire
 * id is consumed without syntax error.
 */
UBool TransliteratorIDParser::parseCompoundID(const UnicodeString& id, int32_t dir,
                                              UnicodeString& canonID,
                                              UVector& list,
                                              UnicodeSet*& globalFilter) {
    UErrorCode ec = U_ZERO_ERROR;
    int32_t i;
    int32_t pos = 0;
    int32_t withParens = 1;
    list.removeAllElements();
    UObjectDeleter *save = list.setDeleter(_deleteSingleID);

    UnicodeSet* filter;
    globalFilter = nullptr;
    canonID.truncate(0);

    // Parse leading global filter, if any
    withParens = 0; // parens disallowed
    filter = parseGlobalFilter(id, pos, dir, withParens, &canonID);
    if (filter != nullptr) {
        if (!ICU_Utility::parseChar(id, pos, ID_DELIM)) {
            // Not a global filter; backup and resume
            canonID.truncate(0);
            pos = 0;
        }
        if (dir == FORWARD) {
            globalFilter = filter;
        } else {
            delete filter;
        }
        filter = nullptr;
    }

    UBool sawDelimiter = true;
    for (;;) {
        SingleID* single = parseSingleID(id, pos, dir, ec);
        if (single == nullptr) {
            break;
        }
        if (dir == FORWARD) {
            list.adoptElement(single, ec);
        } else {
            list.insertElementAt(single, 0, ec);
        }
        if (U_FAILURE(ec)) {
            goto FAIL;
        }
        if (!ICU_Utility::parseChar(id, pos, ID_DELIM)) {
            sawDelimiter = false;
            break;
        }
    }

    if (list.size() == 0) {
        goto FAIL;
    }

    // Construct canonical ID
    for (i=0; i<list.size(); ++i) {
        SingleID* single = (SingleID*) list.elementAt(i);
        canonID.append(single->canonID);
        if (i != (list.size()-1)) {
            canonID.append(ID_DELIM);
        }
    }

    // Parse trailing global filter, if any, and only if we saw
    // a trailing delimiter after the IDs.
    if (sawDelimiter) {
        withParens = 1; // parens required
        filter = parseGlobalFilter(id, pos, dir, withParens, &canonID);
        if (filter != nullptr) {
            // Don't require trailing ';', but parse it if present
            ICU_Utility::parseChar(id, pos, ID_DELIM);

            if (dir == REVERSE) {
                globalFilter = filter;
            } else {
                delete filter;
            }
            filter = nullptr;
        }
    }

    // Trailing unparsed text is a syntax error
    ICU_Utility::skipWhitespace(id, pos, true);
    if (pos != id.length()) {
        goto FAIL;
    }

    list.setDeleter(save);
    return true;

 FAIL:
    list.removeAllElements();
    list.setDeleter(save);
    delete globalFilter;
    globalFilter = nullptr;
    return false;
}

/**
 * Convert the elements of the 'list' vector, which are SingleID
 * objects, into actual Transliterator objects.  In the course of
 * this, some (or all) entries may be removed.  If all entries
 * are removed, the nullptr transliterator will be added.
 *
 * Delete entries with empty basicIDs; these are generated by
 * elements like "(A)" in the forward direction, or "A()" in
 * the reverse.  THIS MAY RESULT IN AN EMPTY VECTOR.  Convert
 * SingleID entries to actual transliterators.
 *
 * @param list vector of SingleID objects.  On exit, vector
 * of one or more Transliterators.
 * @return new value of insertIndex.  The index will shift if
 * there are empty items, like "(Lower)", with indices less than
 * insertIndex.
 */
void TransliteratorIDParser::instantiateList(UVector& list,
                                                UErrorCode& ec) {
    UVector tlist(ec);
    if (U_FAILURE(ec)) {
        goto RETURN;
    }
    tlist.setDeleter(_deleteTransliteratorTrIDPars);

    Transliterator* t;
    int32_t i;
    for (i=0; i<=list.size(); ++i) { // [sic]: i<=list.size()
        // We run the loop too long by one, so we can
        // do an insert after the last element
        if (i==list.size()) {
            break;
        }

        SingleID* single = (SingleID*) list.elementAt(i);
        if (single->basicID.length() != 0) {
            t = single->createInstance();
            if (t == nullptr) {
                ec = U_INVALID_ID;
                goto RETURN;
            }
            tlist.adoptElement(t, ec);
            if (U_FAILURE(ec)) {
                goto RETURN;
            }
        }
    }

    // An empty list is equivalent to a nullptr transliterator.
    if (tlist.size() == 0) {
        t = createBasicInstance(UnicodeString(true, ANY_NULL, 8), nullptr);
        if (t == nullptr) {
            // Should never happen
            ec = U_INTERNAL_TRANSLITERATOR_ERROR;
        }
        tlist.adoptElement(t, ec);
    }

 RETURN:

    UObjectDeleter *save = list.setDeleter(_deleteSingleID);
    list.removeAllElements();

    if (U_SUCCESS(ec)) {
        list.setDeleter(_deleteTransliteratorTrIDPars);

        while (tlist.size() > 0) {
            t = (Transliterator*) tlist.orphanElementAt(0);
            list.adoptElement(t, ec);
            if (U_FAILURE(ec)) {
                list.removeAllElements();
                break;
            }
        }
    }

    list.setDeleter(save);
}

/**
 * Parse an ID into pieces.  Take IDs of the form T, T/V, S-T,
 * S-T/V, or S/V-T.  If the source is missing, return a source of
 * ANY.
 * @param id the id string, in any of several forms
 * @return an array of 4 strings: source, target, variant, and
 * isSourcePresent.  If the source is not present, ANY will be
 * given as the source, and isSourcePresent will be nullptr.  Otherwise
 * isSourcePresent will be non-nullptr.  The target may be empty if the
 * id is not well-formed.  The variant may be empty.
 */
void TransliteratorIDParser::IDtoSTV(const UnicodeString& id,
                                     UnicodeString& source,
                                     UnicodeString& target,
                                     UnicodeString& variant,
                                     UBool& isSourcePresent) {
    source.setTo(ANY, 3);
    target.truncate(0);
    variant.truncate(0);

    int32_t sep = id.indexOf(TARGET_SEP);
    int32_t var = id.indexOf(VARIANT_SEP);
    if (var < 0) {
        var = id.length();
    }
    isSourcePresent = false;

    if (sep < 0) {
        // Form: T/V or T (or /V)
        id.extractBetween(0, var, target);
        id.extractBetween(var, id.length(), variant);
    } else if (sep < var) {
        // Form: S-T/V or S-T (or -T/V or -T)
        if (sep > 0) {
            id.extractBetween(0, sep, source);
            isSourcePresent = true;
        }
        id.extractBetween(++sep, var, target);
        id.extractBetween(var, id.length(), variant);
    } else {
        // Form: (S/V-T or /V-T)
        if (var > 0) {
            id.extractBetween(0, var, source);
            isSourcePresent = true;
        }
        id.extractBetween(var, sep++, variant);
        id.extractBetween(sep, id.length(), target);
    }

    if (variant.length() > 0) {
        variant.remove(0, 1);
    }
}

/**
 * Given source, target, and variant strings, concatenate them into a
 * full ID.  If the source is empty, then "Any" will be used for the
 * source, so the ID will always be of the form s-t/v or s-t.
 */
void TransliteratorIDParser::STVtoID(const UnicodeString& source,
                                     const UnicodeString& target,
                                     const UnicodeString& variant,
                                     UnicodeString& id) {
    id = source;
    if (id.length() == 0) {
        id.setTo(ANY, 3);
    }
    id.append(TARGET_SEP).append(target);
    if (variant.length() != 0) {
        id.append(VARIANT_SEP).append(variant);
    }
    // NUL-terminate the ID string for getTerminatedBuffer.
    // This prevents valgrind and Purify warnings.
    id.append((char16_t)0);
    id.truncate(id.length()-1);
}

/**
 * Register two targets as being inverses of one another.  For
 * example, calling registerSpecialInverse("NFC", "NFD", true) causes
 * Transliterator to form the following inverse relationships:
 *
 * <pre>NFC => NFD
 * Any-NFC => Any-NFD
 * NFD => NFC
 * Any-NFD => Any-NFC</pre>
 *
 * (Without the special inverse registration, the inverse of NFC
 * would be NFC-Any.)  Note that NFD is shorthand for Any-NFD, but
 * that the presence or absence of "Any-" is preserved.
 *
 * <p>The relationship is symmetrical; registering (a, b) is
 * equivalent to registering (b, a).
 *
 * <p>The relevant IDs must still be registered separately as
 * factories or classes.
 *
 * <p>Only the targets are specified.  Special inverses always
 * have the form Any-Target1 <=> Any-Target2.  The target should
 * have canonical casing (the casing desired to be produced when
 * an inverse is formed) and should contain no whitespace or other
 * extraneous characters.
 *
 * @param target the target against which to register the inverse
 * @param inverseTarget the inverse of target, that is
 * Any-target.getInverse() => Any-inverseTarget
 * @param bidirectional if true, register the reverse relation
 * as well, that is, Any-inverseTarget.getInverse() => Any-target
 */
void TransliteratorIDParser::registerSpecialInverse(const UnicodeString& target,
                                                    const UnicodeString& inverseTarget,
                                                    UBool bidirectional,
                                                    UErrorCode &status) {
    umtx_initOnce(gSpecialInversesInitOnce, init, status);
    if (U_FAILURE(status)) {
        return;
    }

    // If target == inverseTarget then force bidirectional => false
    if (bidirectional && 0==target.caseCompare(inverseTarget, U_FOLD_CASE_DEFAULT)) {
        bidirectional = false;
    }

    Mutex lock(&LOCK);

    UnicodeString *tempus = new UnicodeString(inverseTarget);  // Used for null pointer check before usage.
    if (tempus == nullptr) {
    	status = U_MEMORY_ALLOCATION_ERROR;
    	return;
    }
    SPECIAL_INVERSES->put(target, tempus, status);
    if (bidirectional) {
    	tempus = new UnicodeString(target);
    	if (tempus == nullptr) {
    		status = U_MEMORY_ALLOCATION_ERROR;
    		return;
    	}
        SPECIAL_INVERSES->put(inverseTarget, tempus, status);
    }
}

//----------------------------------------------------------------
// Private implementation
//----------------------------------------------------------------

/**
 * Parse an ID into component pieces.  Take IDs of the form T,
 * T/V, S-T, S-T/V, or S/V-T.  If the source is missing, return a
 * source of ANY.
 * @param id the id string, in any of several forms
 * @param pos INPUT-OUTPUT parameter.  On input, pos is the
 * offset of the first character to parse in id.  On output,
 * pos is the offset after the last parsed character.  If the
 * parse failed, pos will be unchanged.
 * @param allowFilter2 if true, a UnicodeSet pattern is allowed
 * at any location between specs or delimiters, and is returned
 * as the fifth string in the array.
 * @return a Specs object, or nullptr if the parse failed.  If
 * neither source nor target was seen in the parsed id, then the
 * parse fails.  If allowFilter is true, then the parsed filter
 * pattern is returned in the Specs object, otherwise the returned
 * filter reference is nullptr.  If the parse fails for any reason
 * nullptr is returned.
 */
TransliteratorIDParser::Specs*
TransliteratorIDParser::parseFilterID(const UnicodeString& id, int32_t& pos,
                                      UBool allowFilter) {
    UnicodeString first;
    UnicodeString source;
    UnicodeString target;
    UnicodeString variant;
    UnicodeString filter;
    char16_t delimiter = 0;
    int32_t specCount = 0;
    int32_t start = pos;

    // This loop parses one of the following things with each
    // pass: a filter, a delimiter character (either '-' or '/'),
    // or a spec (source, target, or variant).
    for (;;) {
        ICU_Utility::skipWhitespace(id, pos, true);
        if (pos == id.length()) {
            break;
        }

        // Parse filters
        if (allowFilter && filter.length() == 0 &&
            UnicodeSet::resemblesPattern(id, pos)) {

            ParsePosition ppos(pos);
            UErrorCode ec = U_ZERO_ERROR;
            UnicodeSet set(id, ppos, USET_IGNORE_SPACE, nullptr, ec);
            if (U_FAILURE(ec)) {
                pos = start;
                return nullptr;
            }
            id.extractBetween(pos, ppos.getIndex(), filter);
            pos = ppos.getIndex();
            continue;
        }

        if (delimiter == 0) {
            char16_t c = id.charAt(pos);
            if ((c == TARGET_SEP && target.length() == 0) ||
                (c == VARIANT_SEP && variant.length() == 0)) {
                delimiter = c;
                ++pos;
                continue;
            }
        }

        // We are about to try to parse a spec with no delimiter
        // when we can no longer do so (we can only do so at the
        // start); break.
        if (delimiter == 0 && specCount > 0) {
            break;
        }

        UnicodeString spec = ICU_Utility::parseUnicodeIdentifier(id, pos);
        if (spec.length() == 0) {
            // Note that if there was a trailing delimiter, we
            // consume it.  So Foo-, Foo/, Foo-Bar/, and Foo/Bar-
            // are legal.
            break;
        }

        switch (delimiter) {
        case 0:
            first = spec;
            break;
        case TARGET_SEP:
            target = spec;
            break;
        case VARIANT_SEP:
            variant = spec;
            break;
        }
        ++specCount;
        delimiter = 0;
    }

    // A spec with no prior character is either source or target,
    // depending on whether an explicit "-target" was seen.
    if (first.length() != 0) {
        if (target.length() == 0) {
            target = first;
        } else {
            source = first;
        }
    }

    // Must have either source or target
    if (source.length() == 0 && target.length() == 0) {
        pos = start;
        return nullptr;
    }

    // Empty source or target defaults to ANY
    UBool sawSource = true;
    if (source.length() == 0) {
        source.setTo(ANY, 3);
        sawSource = false;
    }
    if (target.length() == 0) {
        target.setTo(ANY, 3);
    }

    return new Specs(source, target, variant, sawSource, filter);
}

/**
 * Givens a Spec object, convert it to a SingleID object.  The
 * Spec object is a more unprocessed parse result.  The SingleID
 * object contains information about canonical and basic IDs.
 * @return a SingleID; never returns nullptr.  Returned object always
 * has 'filter' field of nullptr.
 */
TransliteratorIDParser::SingleID*
TransliteratorIDParser::specsToID(const Specs* specs, int32_t dir) {
    UnicodeString canonID;
    UnicodeString basicID;
    UnicodeString basicPrefix;
    if (specs != nullptr) {
        UnicodeString buf;
        if (dir == FORWARD) {
            if (specs->sawSource) {
                buf.append(specs->source).append(TARGET_SEP);
            } else {
                basicPrefix = specs->source;
                basicPrefix.append(TARGET_SEP);
            }
            buf.append(specs->target);
        } else {
            buf.append(specs->target).append(TARGET_SEP).append(specs->source);
        }
        if (specs->variant.length() != 0) {
            buf.append(VARIANT_SEP).append(specs->variant);
        }
        basicID = basicPrefix;
        basicID.append(buf);
        if (specs->filter.length() != 0) {
            buf.insert(0, specs->filter);
        }
        canonID = buf;
    }
    return new SingleID(canonID, basicID);
}

/**
 * Given a Specs object, return a SingleID representing the
 * special inverse of that ID.  If there is no special inverse
 * then return nullptr.
 * @return a SingleID or nullptr.  Returned object always has
 * 'filter' field of nullptr.
 */
TransliteratorIDParser::SingleID*
TransliteratorIDParser::specsToSpecialInverse(const Specs& specs, UErrorCode &status) {
    if (0!=specs.source.caseCompare(ANY, 3, U_FOLD_CASE_DEFAULT)) {
        return nullptr;
    }
    umtx_initOnce(gSpecialInversesInitOnce, init, status);
    if (U_FAILURE(status)) {
        return nullptr;
    }

    UnicodeString* inverseTarget;

    umtx_lock(&LOCK);
    inverseTarget = (UnicodeString*) SPECIAL_INVERSES->get(specs.target);
    umtx_unlock(&LOCK);

    if (inverseTarget != nullptr) {
        // If the original ID contained "Any-" then make the
        // special inverse "Any-Foo"; otherwise make it "Foo".
        // So "Any-NFC" => "Any-NFD" but "NFC" => "NFD".
        UnicodeString buf;
        if (specs.filter.length() != 0) {
            buf.append(specs.filter);
        }
        if (specs.sawSource) {
            buf.append(ANY, 3).append(TARGET_SEP);
        }
        buf.append(*inverseTarget);

        UnicodeString basicID(true, ANY, 3);
        basicID.append(TARGET_SEP).append(*inverseTarget);

        if (specs.variant.length() != 0) {
            buf.append(VARIANT_SEP).append(specs.variant);
            basicID.append(VARIANT_SEP).append(specs.variant);
        }
        return new SingleID(buf, basicID);
    }
    return nullptr;
}

/**
 * Glue method to get around access problems in C++.  This would
 * ideally be inline but we want to avoid a circular header
 * dependency.
 */
Transliterator* TransliteratorIDParser::createBasicInstance(const UnicodeString& id, const UnicodeString* canonID) {
    return Transliterator::createBasicInstance(id, canonID);
}

/**
 * Initialize static memory. Called through umtx_initOnce only.
 */
void U_CALLCONV TransliteratorIDParser::init(UErrorCode &status) {
    U_ASSERT(SPECIAL_INVERSES == nullptr);
    ucln_i18n_registerCleanup(UCLN_I18N_TRANSLITERATOR, utrans_transliterator_cleanup);

    SPECIAL_INVERSES = new Hashtable(true, status);
    if (SPECIAL_INVERSES == nullptr) {
    	status = U_MEMORY_ALLOCATION_ERROR;
    	return;
    }
    SPECIAL_INVERSES->setValueDeleter(uprv_deleteUObject);
}

/**
 * Free static memory.
 */
void TransliteratorIDParser::cleanup() {
    if (SPECIAL_INVERSES) {
        delete SPECIAL_INVERSES;
        SPECIAL_INVERSES = nullptr;
    }
    gSpecialInversesInitOnce.reset();
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_TRANSLITERATION */

//eof
