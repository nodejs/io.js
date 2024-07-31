// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*
*   Copyright (C) 2004-2012, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*   file name:  ucase.h
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 2004aug30
*   created by: Markus W. Scherer
*
*   Low-level Unicode character/string case mapping code.
*/

#ifndef __UCASE_H__
#define __UCASE_H__

#include "unicode/utypes.h"
#include "unicode/uset.h"
#include "putilimp.h"
#include "uset_imp.h"
#include "udataswp.h"
#include "utrie2.h"

#ifdef __cplusplus
U_NAMESPACE_BEGIN

class UnicodeString;

U_NAMESPACE_END
#endif

/* library API -------------------------------------------------------------- */

U_CFUNC void U_EXPORT2
ucase_addPropertyStarts(const USetAdder *sa, UErrorCode *pErrorCode);

/**
 * Requires non-NULL locale ID but otherwise does the equivalent of
 * checking for language codes as if uloc_getLanguage() were called:
 * Accepts both 2- and 3-letter codes and accepts case variants.
 */
U_CFUNC int32_t
ucase_getCaseLocale(const char *locale);

/* Casing locale types for ucase_getCaseLocale */
enum {
    UCASE_LOC_UNKNOWN,
    UCASE_LOC_ROOT,
    UCASE_LOC_TURKISH,
    UCASE_LOC_LITHUANIAN,
    UCASE_LOC_GREEK,
    UCASE_LOC_DUTCH,
    UCASE_LOC_ARMENIAN
};

/**
 * Bit mask for getting just the options from a string compare options word
 * that are relevant for case-insensitive string comparison.
 * See stringoptions.h. Also include _STRNCMP_STYLE and U_COMPARE_CODE_POINT_ORDER.
 * @internal
 */
#define _STRCASECMP_OPTIONS_MASK 0xffff

/**
 * Bit mask for getting just the options from a string compare options word
 * that are relevant for case folding (of a single string or code point).
 *
 * Currently only bit 0 for U_FOLD_CASE_EXCLUDE_SPECIAL_I.
 * It is conceivable that at some point we might use one more bit for using uppercase sharp s.
 * It is conceivable that at some point we might want the option to use only simple case foldings
 * when operating on strings.
 *
 * See stringoptions.h.
 * @internal
 */
#define _FOLD_CASE_OPTIONS_MASK 7

/* single-code point functions */

U_CAPI UChar32 U_EXPORT2
ucase_tolower(UChar32 c);

U_CAPI UChar32 U_EXPORT2
ucase_toupper(UChar32 c);

U_CAPI UChar32 U_EXPORT2
ucase_totitle(UChar32 c);

U_CAPI UChar32 U_EXPORT2
ucase_fold(UChar32 c, uint32_t options);

/**
 * Adds all simple case mappings and the full case folding for c to sa,
 * and also adds special case closure mappings.
 * c itself is not added.
 * For example, the mappings
 * - for s include long s
 * - for sharp s include ss
 * - for k include the Kelvin sign
 */
U_CFUNC void U_EXPORT2
ucase_addCaseClosure(UChar32 c, const USetAdder *sa);

/** Case closure with only scf=Simple_Case_Folding. */
U_CFUNC void U_EXPORT2
ucase_addSimpleCaseClosure(UChar32 c, const USetAdder *sa);

/**
 * Maps the string to single code points and adds the associated case closure
 * mappings.
 * The string is mapped to code points if it is their full case folding string.
 * In other words, this performs a reverse full case folding and then
 * adds the case closure items of the resulting code points.
 * If the string is found and its closure applied, then
 * the string itself is added as well as part of its code points' closure.
 * It must be length>=0.
 *
 * @return true if the string was found
 */
U_CFUNC UBool U_EXPORT2
ucase_addStringCaseClosure(const UChar *s, int32_t length, const USetAdder *sa);

#ifdef __cplusplus
U_NAMESPACE_BEGIN

/**
 * Iterator over characters with more than one code point in the full default Case_Folding.
 */
class U_COMMON_API FullCaseFoldingIterator {
public:
    /** Constructor. */
    FullCaseFoldingIterator();
    /**
     * Returns the next (cp, full) pair where "full" is cp's full default Case_Folding.
     * Returns a negative cp value at the end of the iteration.
     */
    UChar32 next(UnicodeString &full);
private:
    FullCaseFoldingIterator(const FullCaseFoldingIterator &) = delete;  // no copy
    FullCaseFoldingIterator &operator=(const FullCaseFoldingIterator &) = delete;  // no assignment

    const char16_t *unfold;
    int32_t unfoldRows;
    int32_t unfoldRowWidth;
    int32_t unfoldStringWidth;
    int32_t currentRow;
    int32_t rowCpIndex;
};

/**
 * Fast case mapping data for ASCII/Latin.
 * Linear arrays of delta bytes: 0=no mapping; EXC=exception.
 * Deltas must not cross the ASCII boundary, or else they cannot be easily used
 * in simple UTF-8 code.
 */
namespace LatinCase {

/** Case mapping/folding data for code points up to U+017F. */
constexpr char16_t LIMIT = 0x180;
/** U+017F case-folds and uppercases crossing the ASCII boundary. */
constexpr char16_t LONG_S = 0x17f;
/** Exception: Complex mapping, or too-large delta. */
constexpr int8_t EXC = -0x80;

/** Deltas for lowercasing for most locales, and default case folding. */
extern const int8_t TO_LOWER_NORMAL[LIMIT];
/** Deltas for lowercasing for tr/az/lt, and Turkic case folding. */
extern const int8_t TO_LOWER_TR_LT[LIMIT];

/** Deltas for uppercasing for most locales. */
extern const int8_t TO_UPPER_NORMAL[LIMIT];
/** Deltas for uppercasing for tr/az. */
extern const int8_t TO_UPPER_TR[LIMIT];

}  // namespace LatinCase

U_NAMESPACE_END
#endif

/** @return UCASE_NONE, UCASE_LOWER, UCASE_UPPER, UCASE_TITLE */
U_CAPI int32_t U_EXPORT2
ucase_getType(UChar32 c);

/** @return like ucase_getType() but also sets UCASE_IGNORABLE if c is case-ignorable */
U_CAPI int32_t U_EXPORT2
ucase_getTypeOrIgnorable(UChar32 c);

U_CAPI UBool U_EXPORT2
ucase_isSoftDotted(UChar32 c);

U_CAPI UBool U_EXPORT2
ucase_isCaseSensitive(UChar32 c);

/* string case mapping functions */

U_CDECL_BEGIN

/**
 * Iterator function for string case mappings, which need to look at the
 * context (surrounding text) of a given character for conditional mappings.
 *
 * The iterator only needs to go backward or forward away from the
 * character in question. It does not use any indexes on this interface.
 * It does not support random access or an arbitrary change of
 * iteration direction.
 *
 * The code point being case-mapped itself is never returned by
 * this iterator.
 *
 * @param context A pointer to the iterator's working data.
 * @param dir If <0 then start iterating backward from the character;
 *            if >0 then start iterating forward from the character;
 *            if 0 then continue iterating in the current direction.
 * @return Next code point, or <0 when the iteration is done.
 */
typedef UChar32 U_CALLCONV
UCaseContextIterator(void *context, int8_t dir);

/**
 * Sample struct which may be used by some implementations of
 * UCaseContextIterator.
 */
struct UCaseContext {
    void *p;
    int32_t start, index, limit;
    int32_t cpStart, cpLimit;
    int8_t dir;
    int8_t b1, b2, b3;
};
typedef struct UCaseContext UCaseContext;

U_CDECL_END

#define UCASECONTEXT_INITIALIZER { NULL,  0, 0, 0,  0, 0,  0,  0, 0, 0 }

enum {
    /**
     * For string case mappings, a single character (a code point) is mapped
     * either to itself (in which case in-place mapping functions do nothing),
     * or to another single code point, or to a string.
     * Aside from the string contents, these are indicated with a single int32_t
     * value as follows:
     *
     * Mapping to self: Negative values (~self instead of -self to support U+0000)
     *
     * Mapping to another code point: Positive values >UCASE_MAX_STRING_LENGTH
     *
     * Mapping to a string: The string length (0..UCASE_MAX_STRING_LENGTH) is
     * returned. Note that the string result may indeed have zero length.
     */
    UCASE_MAX_STRING_LENGTH=0x1f
};

/**
 * Get the full lowercase mapping for c.
 *
 * @param csp Case mapping properties.
 * @param c Character to be mapped.
 * @param iter Character iterator, used for context-sensitive mappings.
 *             See UCaseContextIterator for details.
 *             If iter==NULL then a context-independent result is returned.
 * @param context Pointer to be passed into iter.
 * @param pString If the mapping result is a string, then the pointer is
 *                written to *pString.
 * @param caseLocale Case locale value from ucase_getCaseLocale().
 * @return Output code point or string length, see UCASE_MAX_STRING_LENGTH.
 *
 * @see UCaseContextIterator
 * @see UCASE_MAX_STRING_LENGTH
 * @internal
 */
U_CAPI int32_t U_EXPORT2
ucase_toFullLower(UChar32 c,
                  UCaseContextIterator *iter, void *context,
                  const UChar **pString,
                  int32_t caseLocale);

U_CAPI int32_t U_EXPORT2
ucase_toFullUpper(UChar32 c,
                  UCaseContextIterator *iter, void *context,
                  const UChar **pString,
                  int32_t caseLocale);

U_CAPI int32_t U_EXPORT2
ucase_toFullTitle(UChar32 c,
                  UCaseContextIterator *iter, void *context,
                  const UChar **pString,
                  int32_t caseLocale);

U_CAPI int32_t U_EXPORT2
ucase_toFullFolding(UChar32 c,
                    const UChar **pString,
                    uint32_t options);

U_CFUNC int32_t U_EXPORT2
ucase_hasBinaryProperty(UChar32 c, UProperty which);


U_CDECL_BEGIN

/**
 * @internal
 */
typedef int32_t U_CALLCONV
UCaseMapFull(UChar32 c,
             UCaseContextIterator *iter, void *context,
             const UChar **pString,
             int32_t caseLocale);

U_CDECL_END

/* for icuexportdata -------------------------------------------------------- */

struct UCaseProps {
    void *mem;  // TODO: was unused, and type UDataMemory -- remove
    const int32_t *indexes;
    const uint16_t *exceptions;
    const uint16_t *unfold;

    UTrie2 trie;
    uint8_t formatVersion[4];
};

U_CAPI const struct UCaseProps * U_EXPORT2
ucase_getSingleton(int32_t *pExceptionsLength, int32_t *pUnfoldLength);

/* file definitions --------------------------------------------------------- */

#define UCASE_DATA_NAME "ucase"
#define UCASE_DATA_TYPE "icu"

/* format "cAsE" */
#define UCASE_FMT_0 0x63
#define UCASE_FMT_1 0x41
#define UCASE_FMT_2 0x53
#define UCASE_FMT_3 0x45

/* indexes into indexes[] */
enum {
    UCASE_IX_INDEX_TOP,
    UCASE_IX_LENGTH,
    UCASE_IX_TRIE_SIZE,
    UCASE_IX_EXC_LENGTH,
    UCASE_IX_UNFOLD_LENGTH,

    UCASE_IX_MAX_FULL_LENGTH=15,
    UCASE_IX_TOP=16
};

/* definitions for 16-bit case properties word ------------------------------ */

U_CFUNC const UTrie2 * U_EXPORT2
ucase_getTrie(void);

/* 2-bit constants for types of cased characters */
#define UCASE_TYPE_MASK     3
enum {
    UCASE_NONE,
    UCASE_LOWER,
    UCASE_UPPER,
    UCASE_TITLE
};

#define UCASE_GET_TYPE(props) ((props)&UCASE_TYPE_MASK)
#define UCASE_GET_TYPE_AND_IGNORABLE(props) ((props)&7)

#define UCASE_IS_UPPER_OR_TITLE(props) ((props)&2)

#define UCASE_IGNORABLE         4
#define UCASE_EXCEPTION         8
#define UCASE_SENSITIVE         0x10

#define UCASE_HAS_EXCEPTION(props) ((props)&UCASE_EXCEPTION)

#define UCASE_DOT_MASK      0x60
enum {
    UCASE_NO_DOT=0,         /* normal characters with cc=0 */
    UCASE_SOFT_DOTTED=0x20, /* soft-dotted characters with cc=0 */
    UCASE_ABOVE=0x40,       /* "above" accents with cc=230 */
    UCASE_OTHER_ACCENT=0x60 /* other accent character (0<cc!=230) */
};

/* no exception: bits 15..7 are a 9-bit signed case mapping delta */
#define UCASE_DELTA_SHIFT   7
#define UCASE_DELTA_MASK    0xff80
#define UCASE_MAX_DELTA     0xff
#define UCASE_MIN_DELTA     (-UCASE_MAX_DELTA-1)

#if U_SIGNED_RIGHT_SHIFT_IS_ARITHMETIC
#   define UCASE_GET_DELTA(props) ((int16_t)(props)>>UCASE_DELTA_SHIFT)
#else
#   define UCASE_GET_DELTA(props) (int16_t)(((props)&0x8000) ? (((props)>>UCASE_DELTA_SHIFT)|0xfe00) : ((uint16_t)(props)>>UCASE_DELTA_SHIFT))
#endif

/* exception: bits 15..4 are an unsigned 12-bit index into the exceptions array */
#define UCASE_EXC_SHIFT     4
#define UCASE_EXC_MASK      0xfff0
#define UCASE_MAX_EXCEPTIONS ((UCASE_EXC_MASK>>UCASE_EXC_SHIFT)+1)

/* definitions for 16-bit main exceptions word ------------------------------ */

/* first 8 bits indicate values in optional slots */
enum {
    UCASE_EXC_LOWER,
    UCASE_EXC_FOLD,
    UCASE_EXC_UPPER,
    UCASE_EXC_TITLE,
    UCASE_EXC_DELTA,
    UCASE_EXC_5,            /* reserved */
    UCASE_EXC_CLOSURE,
    UCASE_EXC_FULL_MAPPINGS,
    UCASE_EXC_ALL_SLOTS     /* one past the last slot */
};

/* each slot is 2 uint16_t instead of 1 */
#define UCASE_EXC_DOUBLE_SLOTS      0x100

enum {
    UCASE_EXC_NO_SIMPLE_CASE_FOLDING=0x200,
    UCASE_EXC_DELTA_IS_NEGATIVE=0x400,
    UCASE_EXC_SENSITIVE=0x800
};

/* UCASE_EXC_DOT_MASK=UCASE_DOT_MASK<<UCASE_EXC_DOT_SHIFT */
#define UCASE_EXC_DOT_SHIFT     7

/* normally stored in the main word, but pushed out for larger exception indexes */
#define UCASE_EXC_DOT_MASK      0x3000
enum {
    UCASE_EXC_NO_DOT=0,
    UCASE_EXC_SOFT_DOTTED=0x1000,
    UCASE_EXC_ABOVE=0x2000,         /* "above" accents with cc=230 */
    UCASE_EXC_OTHER_ACCENT=0x3000   /* other character (0<cc!=230) */
};

/* complex/conditional mappings */
#define UCASE_EXC_CONDITIONAL_SPECIAL   0x4000
#define UCASE_EXC_CONDITIONAL_FOLD      0x8000

/* definitions for lengths word for full case mappings */
#define UCASE_FULL_LOWER    0xf
#define UCASE_FULL_FOLDING  0xf0
#define UCASE_FULL_UPPER    0xf00
#define UCASE_FULL_TITLE    0xf000

/* maximum lengths */
#define UCASE_FULL_MAPPINGS_MAX_LENGTH (4*0xf)
#define UCASE_CLOSURE_MAX_LENGTH 0xf

/* constants for reverse case folding ("unfold") data */
enum {
    UCASE_UNFOLD_ROWS,
    UCASE_UNFOLD_ROW_WIDTH,
    UCASE_UNFOLD_STRING_WIDTH
};

#endif
