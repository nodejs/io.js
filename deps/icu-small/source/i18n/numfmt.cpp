// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 1997-2015, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*
* File NUMFMT.CPP
*
* Modification History:
*
*   Date        Name        Description
*   02/19/97    aliu        Converted from java.
*   03/18/97    clhuang     Implemented with C++ APIs.
*   04/17/97    aliu        Enlarged MAX_INTEGER_DIGITS to fully accommodate the
*                           largest double, by default.
*                           Changed DigitCount to int per code review.
*    07/20/98    stephen        Changed operator== to check for grouping
*                            Changed setMaxIntegerDigits per Java implementation.
*                            Changed setMinIntegerDigits per Java implementation.
*                            Changed setMinFractionDigits per Java implementation.
*                            Changed setMaxFractionDigits per Java implementation.
********************************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/numfmt.h"
#include "unicode/locid.h"
#include "unicode/dcfmtsym.h"
#include "unicode/decimfmt.h"
#include "unicode/ustring.h"
#include "unicode/ucurr.h"
#include "unicode/curramt.h"
#include "unicode/numsys.h"
#include "unicode/rbnf.h"
#include "unicode/localpointer.h"
#include "unicode/udisplaycontext.h"
#include "charstr.h"
#include "winnmfmt.h"
#include "uresimp.h"
#include "uhash.h"
#include "cmemory.h"
#include "servloc.h"
#include "ucln_in.h"
#include "cstring.h"
#include "putilimp.h"
#include "uassert.h"
#include "umutex.h"
#include "mutex.h"
#include <float.h>
#include "sharednumberformat.h"
#include "unifiedcache.h"
#include "number_decimalquantity.h"
#include "number_utils.h"

//#define FMT_DEBUG

#ifdef FMT_DEBUG
#include <stdio.h>
static inline void debugout(UnicodeString s) {
    char buf[2000];
    s.extract((int32_t) 0, s.length(), buf);
    printf("%s", buf);
}
#define debug(x) printf("%s", x);
#else
#define debugout(x)
#define debug(x)
#endif

// If no number pattern can be located for a locale, this is the last
// resort. The patterns are same as the ones in root locale.
static const char16_t gLastResortDecimalPat[] = {
    0x23, 0x2C, 0x23, 0x23, 0x30, 0x2E, 0x23, 0x23, 0x23, 0 /* "#,##0.###" */
};
static const char16_t gLastResortCurrencyPat[] = {
    0xA4, 0xA0, 0x23, 0x2C, 0x23, 0x23, 0x30, 0x2E, 0x30, 0x30, 0 /* "\u00A4\u00A0#,##0.00" */
};
static const char16_t gLastResortPercentPat[] = {
    0x23, 0x2C, 0x23, 0x23, 0x30, 0x25, 0 /* "#,##0%" */
};
static const char16_t gLastResortScientificPat[] = {
    0x23, 0x45, 0x30, 0 /* "#E0" */
};
static const char16_t gLastResortIsoCurrencyPat[] = {
    0xA4, 0xA4, 0xA0, 0x23, 0x2C, 0x23, 0x23, 0x30, 0x2E, 0x30, 0x30, 0  /* "\u00A4\u00A4\u00A0#,##0.00" */
};
static const char16_t gLastResortPluralCurrencyPat[] = {
    0x23, 0x2C, 0x23, 0x23, 0x30, 0x2E, 0x23, 0x23, 0x23, 0x20, 0xA4, 0xA4, 0xA4, 0 /* "#,##0.### \u00A4\u00A4\u00A4*/
};
static const char16_t gLastResortAccountingCurrencyPat[] =  {
    0xA4, 0xA0, 0x23, 0x2C, 0x23, 0x23, 0x30, 0x2E, 0x30, 0x30, 0 /* "\u00A4\u00A0#,##0.00" */
};

static const char16_t gSingleCurrencySign[] = {0xA4, 0};
static const char16_t gDoubleCurrencySign[] = {0xA4, 0xA4, 0};

static const char16_t gSlash = 0x2f;

// If the maximum base 10 exponent were 4, then the largest number would
// be 99,999 which has 5 digits.
// On IEEE754 systems gMaxIntegerDigits is 308 + possible denormalized 15 digits + rounding digit
// With big decimal, the max exponent is 999,999,999 and the max number of digits is the same, 999,999,999
const int32_t icu::NumberFormat::gDefaultMaxIntegerDigits = 2000000000;
const int32_t icu::NumberFormat::gDefaultMinIntegerDigits = 127;

static const char16_t * const gLastResortNumberPatterns[UNUM_FORMAT_STYLE_COUNT] = {
    nullptr,  // UNUM_PATTERN_DECIMAL
    gLastResortDecimalPat,  // UNUM_DECIMAL
    gLastResortCurrencyPat,  // UNUM_CURRENCY
    gLastResortPercentPat,  // UNUM_PERCENT
    gLastResortScientificPat,  // UNUM_SCIENTIFIC
    nullptr,  // UNUM_SPELLOUT
    nullptr,  // UNUM_ORDINAL
    nullptr,  // UNUM_DURATION
    gLastResortDecimalPat,  // UNUM_NUMBERING_SYSTEM
    nullptr,  // UNUM_PATTERN_RULEBASED
    gLastResortIsoCurrencyPat,  // UNUM_CURRENCY_ISO
    gLastResortPluralCurrencyPat,  // UNUM_CURRENCY_PLURAL
    gLastResortAccountingCurrencyPat, // UNUM_CURRENCY_ACCOUNTING
    gLastResortCurrencyPat,  // UNUM_CASH_CURRENCY 
    nullptr,  // UNUM_DECIMAL_COMPACT_SHORT
    nullptr,  // UNUM_DECIMAL_COMPACT_LONG
    gLastResortCurrencyPat,  // UNUM_CURRENCY_STANDARD
};

// Keys used for accessing resource bundles

static const icu::number::impl::CldrPatternStyle gFormatCldrStyles[UNUM_FORMAT_STYLE_COUNT] = {
    /* nullptr */ icu::number::impl::CLDR_PATTERN_STYLE_COUNT,  // UNUM_PATTERN_DECIMAL
    icu::number::impl::CLDR_PATTERN_STYLE_DECIMAL,  // UNUM_DECIMAL
    icu::number::impl::CLDR_PATTERN_STYLE_CURRENCY,  // UNUM_CURRENCY
    icu::number::impl::CLDR_PATTERN_STYLE_PERCENT,  // UNUM_PERCENT
    icu::number::impl::CLDR_PATTERN_STYLE_SCIENTIFIC,  // UNUM_SCIENTIFIC
    /* nullptr */ icu::number::impl::CLDR_PATTERN_STYLE_COUNT,  // UNUM_SPELLOUT
    /* nullptr */ icu::number::impl::CLDR_PATTERN_STYLE_COUNT,  // UNUM_ORDINAL
    /* nullptr */ icu::number::impl::CLDR_PATTERN_STYLE_COUNT,  // UNUM_DURATION
    /* nullptr */ icu::number::impl::CLDR_PATTERN_STYLE_COUNT,  // UNUM_NUMBERING_SYSTEM
    /* nullptr */ icu::number::impl::CLDR_PATTERN_STYLE_COUNT,  // UNUM_PATTERN_RULEBASED
    // For UNUM_CURRENCY_ISO and UNUM_CURRENCY_PLURAL,
    // the pattern is the same as the pattern of UNUM_CURRENCY
    // except for replacing the single currency sign with
    // double currency sign or triple currency sign.
    icu::number::impl::CLDR_PATTERN_STYLE_CURRENCY,  // UNUM_CURRENCY_ISO
    icu::number::impl::CLDR_PATTERN_STYLE_CURRENCY,  // UNUM_CURRENCY_PLURAL
    icu::number::impl::CLDR_PATTERN_STYLE_ACCOUNTING,  // UNUM_CURRENCY_ACCOUNTING
    icu::number::impl::CLDR_PATTERN_STYLE_CURRENCY,  // UNUM_CASH_CURRENCY
    /* nullptr */ icu::number::impl::CLDR_PATTERN_STYLE_COUNT,  // UNUM_DECIMAL_COMPACT_SHORT
    /* nullptr */ icu::number::impl::CLDR_PATTERN_STYLE_COUNT,  // UNUM_DECIMAL_COMPACT_LONG
    icu::number::impl::CLDR_PATTERN_STYLE_CURRENCY,  // UNUM_CURRENCY_STANDARD
};

// Static hashtable cache of NumberingSystem objects used by NumberFormat
static UHashtable * NumberingSystem_cache = nullptr;
static icu::UInitOnce gNSCacheInitOnce {};

#if !UCONFIG_NO_SERVICE
static icu::ICULocaleService* gService = nullptr;
static icu::UInitOnce gServiceInitOnce {};
#endif

/**
 * Release all static memory held by Number Format.
 */
U_CDECL_BEGIN
static void U_CALLCONV
deleteNumberingSystem(void *obj) {
    delete (icu::NumberingSystem *)obj;
}

static UBool U_CALLCONV numfmt_cleanup() {
#if !UCONFIG_NO_SERVICE
    gServiceInitOnce.reset();
    if (gService) {
        delete gService;
        gService = nullptr;
    }
#endif
    gNSCacheInitOnce.reset();
    if (NumberingSystem_cache) {
        // delete NumberingSystem_cache;
        uhash_close(NumberingSystem_cache);
        NumberingSystem_cache = nullptr;
    }
    return true;
}
U_CDECL_END

// *****************************************************************************
// class NumberFormat
// *****************************************************************************

U_NAMESPACE_BEGIN

UOBJECT_DEFINE_ABSTRACT_RTTI_IMPLEMENTATION(NumberFormat)

#if !UCONFIG_NO_SERVICE
// -------------------------------------
// SimpleNumberFormatFactory implementation
NumberFormatFactory::~NumberFormatFactory() {}
SimpleNumberFormatFactory::SimpleNumberFormatFactory(const Locale& locale, UBool visible)
    : _visible(visible)
{
    LocaleUtility::initNameFromLocale(locale, _id);
}

SimpleNumberFormatFactory::~SimpleNumberFormatFactory() {}

UBool SimpleNumberFormatFactory::visible() const {
    return _visible;
}

const UnicodeString *
SimpleNumberFormatFactory::getSupportedIDs(int32_t &count, UErrorCode& status) const
{
    if (U_SUCCESS(status)) {
        count = 1;
        return &_id;
    }
    count = 0;
    return nullptr;
}
#endif /* #if !UCONFIG_NO_SERVICE */

// -------------------------------------
// default constructor
NumberFormat::NumberFormat()
:   fGroupingUsed(true),
    fMaxIntegerDigits(gDefaultMaxIntegerDigits),
    fMinIntegerDigits(1),
    fMaxFractionDigits(3), // invariant, >= minFractionDigits
    fMinFractionDigits(0),
    fParseIntegerOnly(false),
    fLenient(false),
    fCapitalizationContext(UDISPCTX_CAPITALIZATION_NONE)
{
    fCurrency[0] = 0;
}

// -------------------------------------

NumberFormat::~NumberFormat()
{
}

SharedNumberFormat::~SharedNumberFormat() {
    delete ptr;
}

// -------------------------------------
// copy constructor

NumberFormat::NumberFormat(const NumberFormat &source)
:   Format(source)
{
    *this = source;
}

// -------------------------------------
// assignment operator

NumberFormat&
NumberFormat::operator=(const NumberFormat& rhs)
{
    if (this != &rhs)
    {
        Format::operator=(rhs);
        fGroupingUsed = rhs.fGroupingUsed;
        fMaxIntegerDigits = rhs.fMaxIntegerDigits;
        fMinIntegerDigits = rhs.fMinIntegerDigits;
        fMaxFractionDigits = rhs.fMaxFractionDigits;
        fMinFractionDigits = rhs.fMinFractionDigits;
        fParseIntegerOnly = rhs.fParseIntegerOnly;
        u_strncpy(fCurrency, rhs.fCurrency, 3);
        fCurrency[3] = 0;
        fLenient = rhs.fLenient;
        fCapitalizationContext = rhs.fCapitalizationContext;
    }
    return *this;
}

// -------------------------------------

bool
NumberFormat::operator==(const Format& that) const
{
    // Format::operator== guarantees this cast is safe
    NumberFormat* other = (NumberFormat*)&that;

#ifdef FMT_DEBUG
    // This code makes it easy to determine why two format objects that should
    // be equal aren't.
    UBool first = true;
    if (!Format::operator==(that)) {
        if (first) { printf("[ "); first = false; } else { printf(", "); }
        debug("Format::!=");
    }
    if (!(fMaxIntegerDigits == other->fMaxIntegerDigits &&
          fMinIntegerDigits == other->fMinIntegerDigits)) {
        if (first) { printf("[ "); first = false; } else { printf(", "); }
        debug("Integer digits !=");
    }
    if (!(fMaxFractionDigits == other->fMaxFractionDigits &&
          fMinFractionDigits == other->fMinFractionDigits)) {
        if (first) { printf("[ "); first = false; } else { printf(", "); }
        debug("Fraction digits !=");
    }
    if (!(fGroupingUsed == other->fGroupingUsed)) {
        if (first) { printf("[ "); first = false; } else { printf(", "); }
        debug("fGroupingUsed != ");
    }
    if (!(fParseIntegerOnly == other->fParseIntegerOnly)) {
        if (first) { printf("[ "); first = false; } else { printf(", "); }
        debug("fParseIntegerOnly != ");
    }
    if (!(u_strcmp(fCurrency, other->fCurrency) == 0)) {
        if (first) { printf("[ "); first = false; } else { printf(", "); }
        debug("fCurrency !=");
    }
    if (!(fLenient == other->fLenient)) {
        if (first) { printf("[ "); first = false; } else { printf(", "); }
        debug("fLenient != ");
    }
    if (!(fCapitalizationContext == other->fCapitalizationContext)) {
        if (first) { printf("[ "); first = false; } else { printf(", "); }
        debug("fCapitalizationContext != ");
    }
    if (!first) { printf(" ]"); }
#endif

    return ((this == &that) ||
            ((Format::operator==(that) &&
              fMaxIntegerDigits == other->fMaxIntegerDigits &&
              fMinIntegerDigits == other->fMinIntegerDigits &&
              fMaxFractionDigits == other->fMaxFractionDigits &&
              fMinFractionDigits == other->fMinFractionDigits &&
              fGroupingUsed == other->fGroupingUsed &&
              fParseIntegerOnly == other->fParseIntegerOnly &&
              u_strcmp(fCurrency, other->fCurrency) == 0 &&
              fLenient == other->fLenient &&
              fCapitalizationContext == other->fCapitalizationContext)));
}

// -------------------------------------
// Default implementation sets unsupported error; subclasses should
// override.

UnicodeString&
NumberFormat::format(double /* unused number */,
                     UnicodeString& toAppendTo,
                     FieldPositionIterator* /* unused posIter */,
                     UErrorCode& status) const
{
    if (!U_FAILURE(status)) {
        status = U_UNSUPPORTED_ERROR;
    }
    return toAppendTo;
}

// -------------------------------------
// Default implementation sets unsupported error; subclasses should
// override.

UnicodeString&
NumberFormat::format(int32_t /* unused number */,
                     UnicodeString& toAppendTo,
                     FieldPositionIterator* /* unused posIter */,
                     UErrorCode& status) const
{
    if (!U_FAILURE(status)) {
        status = U_UNSUPPORTED_ERROR;
    }
    return toAppendTo;
}

// -------------------------------------
// Default implementation sets unsupported error; subclasses should
// override.

UnicodeString&
NumberFormat::format(int64_t /* unused number */,
                     UnicodeString& toAppendTo,
                     FieldPositionIterator* /* unused posIter */,
                     UErrorCode& status) const
{
    if (!U_FAILURE(status)) {
        status = U_UNSUPPORTED_ERROR;
    }
    return toAppendTo;
}

// ------------------------------------------
// These functions add the status code, just fall back to the non-status versions
UnicodeString&
NumberFormat::format(double number,
                     UnicodeString& appendTo,
                     FieldPosition& pos,
                     UErrorCode &status) const {
    if(U_SUCCESS(status)) {
        return format(number,appendTo,pos);
    } else {
        return appendTo;
    }
}

UnicodeString&
NumberFormat::format(int32_t number,
                     UnicodeString& appendTo,
                     FieldPosition& pos,
                     UErrorCode &status) const {
    if(U_SUCCESS(status)) {
        return format(number,appendTo,pos);
    } else {
        return appendTo;
    }
}

UnicodeString&
NumberFormat::format(int64_t number,
                     UnicodeString& appendTo,
                     FieldPosition& pos,
                     UErrorCode &status) const {
    if(U_SUCCESS(status)) {
        return format(number,appendTo,pos);
    } else {
        return appendTo;
    }
}



// -------------------------------------
// Decimal Number format() default implementation 
// Subclasses do not normally override this function, but rather the DigitList
// formatting functions..
//   The expected call chain from here is
//      this function ->
//      NumberFormat::format(Formattable  ->
//      DecimalFormat::format(DigitList    
//
//   Or, for subclasses of Formattable that do not know about DigitList,
//       this Function ->
//       NumberFormat::format(Formattable  ->
//       NumberFormat::format(DigitList  ->
//       XXXFormat::format(double

UnicodeString&
NumberFormat::format(StringPiece decimalNum,
                     UnicodeString& toAppendTo,
                     FieldPositionIterator* fpi,
                     UErrorCode& status) const
{
    Formattable f;
    f.setDecimalNumber(decimalNum, status);
    format(f, toAppendTo, fpi, status);
    return toAppendTo;
}

/**
 *
// Formats the number object and save the format
// result in the toAppendTo string buffer.

// utility to save/restore state, used in two overloads
// of format(const Formattable&...) below.
*
* Old purpose of ArgExtractor was to avoid const. Not thread safe!
*
* keeping it around as a shim.
*/
class ArgExtractor {
  const Formattable* num;
  char16_t save[4];
  UBool fWasCurrency;

 public:
  ArgExtractor(const NumberFormat& nf, const Formattable& obj, UErrorCode& status);
  ~ArgExtractor();

  const Formattable* number() const;
  const char16_t *iso() const;
  UBool wasCurrency() const;
};

inline const Formattable*
ArgExtractor::number() const {
  return num;
}

inline UBool
ArgExtractor::wasCurrency() const {
  return fWasCurrency;
}

inline const char16_t *
ArgExtractor::iso() const {
  return save;
}

ArgExtractor::ArgExtractor(const NumberFormat& /*nf*/, const Formattable& obj, UErrorCode& /*status*/)
  : num(&obj), fWasCurrency(false) {

    const UObject* o = obj.getObject(); // most commonly o==nullptr
    const CurrencyAmount* amt;
    if (o != nullptr && (amt = dynamic_cast<const CurrencyAmount*>(o)) != nullptr) {
        // getISOCurrency() returns a pointer to internal storage, so we
        // copy it to retain it across the call to setCurrency().
        //const char16_t* curr = amt->getISOCurrency();
        u_strcpy(save, amt->getISOCurrency());
        num = &amt->getNumber();
        fWasCurrency=true;
    } else {
      save[0]=0;
    }
}

ArgExtractor::~ArgExtractor() {
}

UnicodeString& NumberFormat::format(const number::impl::DecimalQuantity &number,
                      UnicodeString& appendTo,
                      FieldPositionIterator* posIter,
                      UErrorCode& status) const {
    // DecimalFormat overrides this function, and handles DigitList based big decimals.
    // Other subclasses (ChoiceFormat) do not (yet) handle DigitLists,
    // so this default implementation falls back to formatting decimal numbers as doubles.
    if (U_FAILURE(status)) {
        return appendTo;
    }
    double dnum = number.toDouble();
    format(dnum, appendTo, posIter, status);
    return appendTo;
}



UnicodeString&
NumberFormat::format(const number::impl::DecimalQuantity &number,
                     UnicodeString& appendTo,
                     FieldPosition& pos,
                     UErrorCode &status) const {
    // DecimalFormat overrides this function, and handles DigitList based big decimals.
    // Other subclasses (ChoiceFormat) do not (yet) handle DigitLists,
    // so this default implementation falls back to formatting decimal numbers as doubles.
    if (U_FAILURE(status)) {
        return appendTo;
    }
    double dnum = number.toDouble();
    format(dnum, appendTo, pos, status);
    return appendTo;
}

UnicodeString&
NumberFormat::format(const Formattable& obj,
                        UnicodeString& appendTo,
                        FieldPosition& pos,
                        UErrorCode& status) const
{
    if (U_FAILURE(status)) return appendTo;

    ArgExtractor arg(*this, obj, status);
    const Formattable *n = arg.number();
    const char16_t *iso = arg.iso();

    if(arg.wasCurrency() && u_strcmp(iso, getCurrency())) {
      // trying to format a different currency.
      // Right now, we clone.
      LocalPointer<NumberFormat> cloneFmt(this->clone());
      cloneFmt->setCurrency(iso, status);
      // next line should NOT recurse, because n is numeric whereas obj was a wrapper around currency amount.
      return cloneFmt->format(*n, appendTo, pos, status);
    }

    if (n->isNumeric() && n->getDecimalQuantity() != nullptr) {
        // Decimal Number.  We will have a DigitList available if the value was
        //   set to a decimal number, or if the value originated with a parse.
        //
        // The default implementation for formatting a DigitList converts it
        // to a double, and formats that, allowing formatting classes that don't
        // know about DigitList to continue to operate as they had.
        //
        // DecimalFormat overrides the DigitList formatting functions.
        format(*n->getDecimalQuantity(), appendTo, pos, status);
    } else {
        switch (n->getType()) {
        case Formattable::kDouble:
            format(n->getDouble(), appendTo, pos, status);
            break;
        case Formattable::kLong:
            format(n->getLong(), appendTo, pos, status);
            break;
        case Formattable::kInt64:
            format(n->getInt64(), appendTo, pos, status);
            break;
        default:
            status = U_INVALID_FORMAT_ERROR;
            break;
        }
    }

    return appendTo;
}

// -------------------------------------x
// Formats the number object and save the format
// result in the toAppendTo string buffer.

UnicodeString&
NumberFormat::format(const Formattable& obj,
                        UnicodeString& appendTo,
                        FieldPositionIterator* posIter,
                        UErrorCode& status) const
{
    if (U_FAILURE(status)) return appendTo;

    ArgExtractor arg(*this, obj, status);
    const Formattable *n = arg.number();
    const char16_t *iso = arg.iso();

    if(arg.wasCurrency() && u_strcmp(iso, getCurrency())) {
      // trying to format a different currency.
      // Right now, we clone.
      LocalPointer<NumberFormat> cloneFmt(this->clone());
      cloneFmt->setCurrency(iso, status);
      // next line should NOT recurse, because n is numeric whereas obj was a wrapper around currency amount.
      return cloneFmt->format(*n, appendTo, posIter, status);
    }

    if (n->isNumeric() && n->getDecimalQuantity() != nullptr) {
        // Decimal Number
        format(*n->getDecimalQuantity(), appendTo, posIter, status);
    } else {
        switch (n->getType()) {
        case Formattable::kDouble:
            format(n->getDouble(), appendTo, posIter, status);
            break;
        case Formattable::kLong:
            format(n->getLong(), appendTo, posIter, status);
            break;
        case Formattable::kInt64:
            format(n->getInt64(), appendTo, posIter, status);
            break;
        default:
            status = U_INVALID_FORMAT_ERROR;
            break;
        }
    }

    return appendTo;
}

// -------------------------------------

UnicodeString&
NumberFormat::format(int64_t number,
                     UnicodeString& appendTo,
                     FieldPosition& pos) const
{
    // default so we don't introduce a new abstract method
    return format(static_cast<int32_t>(number), appendTo, pos);
}

// -------------------------------------
// Parses the string and save the result object as well
// as the final parsed position.

void
NumberFormat::parseObject(const UnicodeString& source,
                             Formattable& result,
                             ParsePosition& parse_pos) const
{
    parse(source, result, parse_pos);
}

// -------------------------------------
// Formats a double number and save the result in a string.

UnicodeString&
NumberFormat::format(double number, UnicodeString& appendTo) const
{
    FieldPosition pos(FieldPosition::DONT_CARE);
    return format(number, appendTo, pos);
}

// -------------------------------------
// Formats a long number and save the result in a string.

UnicodeString&
NumberFormat::format(int32_t number, UnicodeString& appendTo) const
{
    FieldPosition pos(FieldPosition::DONT_CARE);
    return format(number, appendTo, pos);
}

// -------------------------------------
// Formats a long number and save the result in a string.

UnicodeString&
NumberFormat::format(int64_t number, UnicodeString& appendTo) const
{
    FieldPosition pos(FieldPosition::DONT_CARE);
    return format(number, appendTo, pos);
}

// -------------------------------------
// Parses the text and save the result object.  If the returned
// parse position is 0, that means the parsing failed, the status
// code needs to be set to failure.  Ignores the returned parse
// position, otherwise.

void
NumberFormat::parse(const UnicodeString& text,
                        Formattable& result,
                        UErrorCode& status) const
{
    if (U_FAILURE(status)) return;

    ParsePosition parsePosition(0);
    parse(text, result, parsePosition);
    if (parsePosition.getIndex() == 0) {
        status = U_INVALID_FORMAT_ERROR;
    }
}

CurrencyAmount* NumberFormat::parseCurrency(const UnicodeString& text,
                                            ParsePosition& pos) const {
    // Default implementation only -- subclasses should override
    Formattable parseResult;
    int32_t start = pos.getIndex();
    parse(text, parseResult, pos);
    if (pos.getIndex() != start) {
        char16_t curr[4];
        UErrorCode ec = U_ZERO_ERROR;
        getEffectiveCurrency(curr, ec);
        if (U_SUCCESS(ec)) {
            LocalPointer<CurrencyAmount> currAmt(new CurrencyAmount(parseResult, curr, ec), ec);
            if (U_FAILURE(ec)) {
                pos.setIndex(start); // indicate failure
            } else {
                return currAmt.orphan();
            }
        }
    }
    return nullptr;
}

// -------------------------------------
// Sets to only parse integers.

void
NumberFormat::setParseIntegerOnly(UBool value)
{
    fParseIntegerOnly = value;
}

// -------------------------------------
// Sets whether lenient parse is enabled.

void
NumberFormat::setLenient(UBool enable)
{
    fLenient = enable;
}

// -------------------------------------
// Create a number style NumberFormat instance with the default locale.

NumberFormat* U_EXPORT2
NumberFormat::createInstance(UErrorCode& status)
{
    return createInstance(Locale::getDefault(), UNUM_DECIMAL, status);
}

// -------------------------------------
// Create a number style NumberFormat instance with the inLocale locale.

NumberFormat* U_EXPORT2
NumberFormat::createInstance(const Locale& inLocale, UErrorCode& status)
{
    return createInstance(inLocale, UNUM_DECIMAL, status);
}

// -------------------------------------
// Create a currency style NumberFormat instance with the default locale.

NumberFormat* U_EXPORT2
NumberFormat::createCurrencyInstance(UErrorCode& status)
{
    return createCurrencyInstance(Locale::getDefault(),  status);
}

// -------------------------------------
// Create a currency style NumberFormat instance with the inLocale locale.

NumberFormat* U_EXPORT2
NumberFormat::createCurrencyInstance(const Locale& inLocale, UErrorCode& status)
{
    return createInstance(inLocale, UNUM_CURRENCY, status);
}

// -------------------------------------
// Create a percent style NumberFormat instance with the default locale.

NumberFormat* U_EXPORT2
NumberFormat::createPercentInstance(UErrorCode& status)
{
    return createInstance(Locale::getDefault(), UNUM_PERCENT, status);
}

// -------------------------------------
// Create a percent style NumberFormat instance with the inLocale locale.

NumberFormat* U_EXPORT2
NumberFormat::createPercentInstance(const Locale& inLocale, UErrorCode& status)
{
    return createInstance(inLocale, UNUM_PERCENT, status);
}

// -------------------------------------
// Create a scientific style NumberFormat instance with the default locale.

NumberFormat* U_EXPORT2
NumberFormat::createScientificInstance(UErrorCode& status)
{
    return createInstance(Locale::getDefault(), UNUM_SCIENTIFIC, status);
}

// -------------------------------------
// Create a scientific style NumberFormat instance with the inLocale locale.

NumberFormat* U_EXPORT2
NumberFormat::createScientificInstance(const Locale& inLocale, UErrorCode& status)
{
    return createInstance(inLocale, UNUM_SCIENTIFIC, status);
}

// -------------------------------------

const Locale* U_EXPORT2
NumberFormat::getAvailableLocales(int32_t& count)
{
    return Locale::getAvailableLocales(count);
}

// ------------------------------------------
//
// Registration
//
//-------------------------------------------

#if !UCONFIG_NO_SERVICE

// -------------------------------------

class ICUNumberFormatFactory : public ICUResourceBundleFactory {
public:
    virtual ~ICUNumberFormatFactory();
protected:
    virtual UObject* handleCreate(const Locale& loc, int32_t kind, const ICUService* /* service */, UErrorCode& status) const override {
        return NumberFormat::makeInstance(loc, static_cast<UNumberFormatStyle>(kind), status);
    }
};

ICUNumberFormatFactory::~ICUNumberFormatFactory() {}

// -------------------------------------

class NFFactory : public LocaleKeyFactory {
private:
    NumberFormatFactory* _delegate;
    Hashtable* _ids;

public:
    NFFactory(NumberFormatFactory* delegate)
        : LocaleKeyFactory(delegate->visible() ? VISIBLE : INVISIBLE)
        , _delegate(delegate)
        , _ids(nullptr)
    {
    }

    virtual ~NFFactory();

    virtual UObject* create(const ICUServiceKey& key, const ICUService* service, UErrorCode& status) const override
    {
        if (handlesKey(key, status)) {
            const LocaleKey* lkey = dynamic_cast<const LocaleKey*>(&key);
            U_ASSERT(lkey != nullptr);
            Locale loc;
            lkey->canonicalLocale(loc);
            int32_t kind = lkey->kind();

            UObject* result = _delegate->createFormat(loc, static_cast<UNumberFormatStyle>(kind));
            if (result == nullptr) {
                result = service->getKey(const_cast<ICUServiceKey&>(key) /* cast away const */, nullptr, this, status);
            }
            return result;
        }
        return nullptr;
    }

protected:
    /**
     * Return the set of ids that this factory supports (visible or
     * otherwise).  This can be called often and might need to be
     * cached if it is expensive to create.
     */
    virtual const Hashtable* getSupportedIDs(UErrorCode& status) const override
    {
        if (U_SUCCESS(status)) {
            if (!_ids) {
                int32_t count = 0;
                const UnicodeString * const idlist = _delegate->getSupportedIDs(count, status);
                const_cast<NFFactory*>(this)->_ids = new Hashtable(status); /* cast away const */
                if (_ids) {
                    for (int i = 0; i < count; ++i) {
                        _ids->put(idlist[i], (void*)this, status);
                    }
                }
            }
            return _ids;
        }
        return nullptr;
    }
};

NFFactory::~NFFactory()
{
    delete _delegate;
    delete _ids;
}

class ICUNumberFormatService : public ICULocaleService {
public:
    ICUNumberFormatService()
        : ICULocaleService(UNICODE_STRING_SIMPLE("Number Format"))
    {
        UErrorCode status = U_ZERO_ERROR;
        registerFactory(new ICUNumberFormatFactory(), status);
    }

    virtual ~ICUNumberFormatService();

    virtual UObject* cloneInstance(UObject* instance) const override {
        return ((NumberFormat*)instance)->clone();
    }

    virtual UObject* handleDefault(const ICUServiceKey& key, UnicodeString* /* actualID */, UErrorCode& status) const override {
        const LocaleKey* lkey = dynamic_cast<const LocaleKey*>(&key);
        U_ASSERT(lkey != nullptr);
        int32_t kind = lkey->kind();
        Locale loc;
        lkey->currentLocale(loc);
        return NumberFormat::makeInstance(loc, static_cast<UNumberFormatStyle>(kind), status);
    }

    virtual UBool isDefault() const override {
        return countFactories() == 1;
    }
};

ICUNumberFormatService::~ICUNumberFormatService() {}

// -------------------------------------

static void U_CALLCONV initNumberFormatService() {
    U_ASSERT(gService == nullptr);
    ucln_i18n_registerCleanup(UCLN_I18N_NUMFMT, numfmt_cleanup);
    gService = new ICUNumberFormatService();
}

static ICULocaleService*
getNumberFormatService()
{
    umtx_initOnce(gServiceInitOnce, &initNumberFormatService);
    return gService;
}

static UBool haveService() {
    return !gServiceInitOnce.isReset() && (getNumberFormatService() != nullptr);
}

// -------------------------------------

URegistryKey U_EXPORT2
NumberFormat::registerFactory(NumberFormatFactory* toAdopt, UErrorCode& status)
{
    if (U_FAILURE(status)) {
        delete toAdopt;
        return nullptr;
    }
    ICULocaleService *service = getNumberFormatService();
    if (service) {
        NFFactory *tempnnf = new NFFactory(toAdopt);
        if (tempnnf != nullptr) {
            return service->registerFactory(tempnnf, status);
        }
    }
    status = U_MEMORY_ALLOCATION_ERROR;
    return nullptr;
}

// -------------------------------------

UBool U_EXPORT2
NumberFormat::unregister(URegistryKey key, UErrorCode& status)
{
    if (U_FAILURE(status)) {
        return false;
    }
    if (haveService()) {
        return gService->unregister(key, status);
    } else {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return false;
    }
}

// -------------------------------------
StringEnumeration* U_EXPORT2
NumberFormat::getAvailableLocales()
{
  ICULocaleService *service = getNumberFormatService();
  if (service) {
      return service->getAvailableLocales();
  }
  return nullptr; // no way to return error condition
}
#endif /* UCONFIG_NO_SERVICE */
// -------------------------------------

enum { kKeyValueLenMax = 32 };

NumberFormat*
NumberFormat::internalCreateInstance(const Locale& loc, UNumberFormatStyle kind, UErrorCode& status) {
    if (kind == UNUM_CURRENCY) {
        char cfKeyValue[kKeyValueLenMax] = {0};
        UErrorCode kvStatus = U_ZERO_ERROR;
        int32_t kLen = loc.getKeywordValue("cf", cfKeyValue, kKeyValueLenMax, kvStatus);
        if (U_SUCCESS(kvStatus) && kLen > 0 && uprv_strcmp(cfKeyValue,"account")==0) {
            kind = UNUM_CURRENCY_ACCOUNTING;
        }
    }
#if !UCONFIG_NO_SERVICE
    if (haveService()) {
        return (NumberFormat*)gService->get(loc, kind, status);
    }
#endif
    return makeInstance(loc, kind, status);
}

NumberFormat* U_EXPORT2
NumberFormat::createInstance(const Locale& loc, UNumberFormatStyle kind, UErrorCode& status) {
    if (kind != UNUM_DECIMAL) {
        return internalCreateInstance(loc, kind, status);
    }
    const SharedNumberFormat *shared = createSharedInstance(loc, kind, status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    NumberFormat *result = (*shared)->clone();
    shared->removeRef();
    if (result == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
    }
    return result;
}
    

// -------------------------------------
// Checks if the thousand/10 thousand grouping is used in the
// NumberFormat instance.

UBool
NumberFormat::isGroupingUsed() const
{
    return fGroupingUsed;
}

// -------------------------------------
// Sets to use the thousand/10 thousand grouping in the
// NumberFormat instance.

void
NumberFormat::setGroupingUsed(UBool newValue)
{
    fGroupingUsed = newValue;
}

// -------------------------------------
// Gets the maximum number of digits for the integral part for
// this NumberFormat instance.

int32_t NumberFormat::getMaximumIntegerDigits() const
{
    return fMaxIntegerDigits;
}

// -------------------------------------
// Sets the maximum number of digits for the integral part for
// this NumberFormat instance.

void
NumberFormat::setMaximumIntegerDigits(int32_t newValue)
{
    fMaxIntegerDigits = uprv_max(0, uprv_min(newValue, gDefaultMaxIntegerDigits));
    if(fMinIntegerDigits > fMaxIntegerDigits)
        fMinIntegerDigits = fMaxIntegerDigits;
}

// -------------------------------------
// Gets the minimum number of digits for the integral part for
// this NumberFormat instance.

int32_t
NumberFormat::getMinimumIntegerDigits() const
{
    return fMinIntegerDigits;
}

// -------------------------------------
// Sets the minimum number of digits for the integral part for
// this NumberFormat instance.

void
NumberFormat::setMinimumIntegerDigits(int32_t newValue)
{
    fMinIntegerDigits = uprv_max(0, uprv_min(newValue, gDefaultMinIntegerDigits));
    if(fMinIntegerDigits > fMaxIntegerDigits)
        fMaxIntegerDigits = fMinIntegerDigits;
}

// -------------------------------------
// Gets the maximum number of digits for the fractional part for
// this NumberFormat instance.

int32_t
NumberFormat::getMaximumFractionDigits() const
{
    return fMaxFractionDigits;
}

// -------------------------------------
// Sets the maximum number of digits for the fractional part for
// this NumberFormat instance.

void
NumberFormat::setMaximumFractionDigits(int32_t newValue)
{
    fMaxFractionDigits = uprv_max(0, uprv_min(newValue, gDefaultMaxIntegerDigits));
    if(fMaxFractionDigits < fMinFractionDigits)
        fMinFractionDigits = fMaxFractionDigits;
}

// -------------------------------------
// Gets the minimum number of digits for the fractional part for
// this NumberFormat instance.

int32_t
NumberFormat::getMinimumFractionDigits() const
{
    return fMinFractionDigits;
}

// -------------------------------------
// Sets the minimum number of digits for the fractional part for
// this NumberFormat instance.

void
NumberFormat::setMinimumFractionDigits(int32_t newValue)
{
    fMinFractionDigits = uprv_max(0, uprv_min(newValue, gDefaultMinIntegerDigits));
    if (fMaxFractionDigits < fMinFractionDigits)
        fMaxFractionDigits = fMinFractionDigits;
}

// -------------------------------------

void NumberFormat::setCurrency(const char16_t* theCurrency, UErrorCode& ec) {
    if (U_FAILURE(ec)) {
        return;
    }
    if (theCurrency) {
        u_strncpy(fCurrency, theCurrency, 3);
        fCurrency[3] = 0;
    } else {
        fCurrency[0] = 0;
    }
}

const char16_t* NumberFormat::getCurrency() const {
    return fCurrency;
}

void NumberFormat::getEffectiveCurrency(char16_t* result, UErrorCode& ec) const {
    const char16_t* c = getCurrency();
    if (*c != 0) {
        u_strncpy(result, c, 3);
        result[3] = 0;
    } else {
        const char* loc = getLocaleID(ULOC_VALID_LOCALE, ec);
        if (loc == nullptr) {
            loc = uloc_getDefault();
        }
        ucurr_forLocale(loc, result, 4, &ec);
    }
}

//----------------------------------------------------------------------


void NumberFormat::setContext(UDisplayContext value, UErrorCode& status)
{
    if (U_FAILURE(status))
        return;
    if (static_cast<UDisplayContextType>(static_cast<uint32_t>(value) >> 8) == UDISPCTX_TYPE_CAPITALIZATION) {
        fCapitalizationContext = value;
    } else {
        status = U_ILLEGAL_ARGUMENT_ERROR;
   }
}


UDisplayContext NumberFormat::getContext(UDisplayContextType type, UErrorCode& status) const
{
    if (U_FAILURE(status))
        return static_cast<UDisplayContext>(0);
    if (type != UDISPCTX_TYPE_CAPITALIZATION) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return static_cast<UDisplayContext>(0);
    }
    return fCapitalizationContext;
}


// -------------------------------------
// Creates the NumberFormat instance of the specified style (number, currency,
// or percent) for the desired locale.

static void U_CALLCONV nscacheInit() {
    U_ASSERT(NumberingSystem_cache == nullptr);
    ucln_i18n_registerCleanup(UCLN_I18N_NUMFMT, numfmt_cleanup);
    UErrorCode status = U_ZERO_ERROR;
    NumberingSystem_cache = uhash_open(uhash_hashLong,
                                       uhash_compareLong,
                                       nullptr,
                                       &status);
    if (U_FAILURE(status)) {
        // Number Format code will run with no cache if creation fails.
        NumberingSystem_cache = nullptr;
        return;
    }
    uhash_setValueDeleter(NumberingSystem_cache, deleteNumberingSystem);
}

template<> U_I18N_API
const SharedNumberFormat *LocaleCacheKey<SharedNumberFormat>::createObject(
        const void * /*unused*/, UErrorCode &status) const {
    const char *localeId = fLoc.getName();
    NumberFormat *nf = NumberFormat::internalCreateInstance(
            localeId, UNUM_DECIMAL, status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    SharedNumberFormat *result = new SharedNumberFormat(nf);
    if (result == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        delete nf;
        return nullptr;
    }
    result->addRef();
    return result;
}

const SharedNumberFormat* U_EXPORT2
NumberFormat::createSharedInstance(const Locale& loc, UNumberFormatStyle kind, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    if (kind != UNUM_DECIMAL) {
        status = U_UNSUPPORTED_ERROR;
        return nullptr;
    }
    const SharedNumberFormat *result = nullptr;
    UnifiedCache::getByLocale(loc, result, status);
    return result;
}

UBool
NumberFormat::isStyleSupported(UNumberFormatStyle style) {
    return gLastResortNumberPatterns[style] != nullptr;
}

NumberFormat*
NumberFormat::makeInstance(const Locale& desiredLocale,
                           UNumberFormatStyle style,
                           UErrorCode& status) {
  return makeInstance(desiredLocale, style, false, status);
}

NumberFormat*
NumberFormat::makeInstance(const Locale& desiredLocale,
                           UNumberFormatStyle style,
                           UBool mustBeDecimalFormat,
                           UErrorCode& status) {
    if (U_FAILURE(status)) return nullptr;

    if (style < 0 || style >= UNUM_FORMAT_STYLE_COUNT) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }
    
    // For the purposes of general number formatting, UNUM_NUMBERING_SYSTEM should behave the same
    // was as UNUM_DECIMAL.  In both cases, you get either a DecimalFormat or a RuleBasedNumberFormat
    // depending on the locale's numbering system (either the default one for the locale or a specific
    // one specified by using the "@numbers=" or "-u-nu-" parameter in the locale ID.
    if (style == UNUM_NUMBERING_SYSTEM) {
        style = UNUM_DECIMAL;
    }

    // Some styles are not supported. This is a result of merging
    // the @draft ICU 4.2 NumberFormat::EStyles into the long-existing UNumberFormatStyle.
    // Ticket #8503 is for reviewing/fixing/merging the two relevant implementations:
    // this one and unum_open().
    // The UNUM_PATTERN_ styles are not supported here
    // because this method does not take a pattern string.
    if (!isStyleSupported(style)) {
        status = U_UNSUPPORTED_ERROR;
        return nullptr;
    }

#if U_PLATFORM_USES_ONLY_WIN32_API
    if (!mustBeDecimalFormat) {
        char buffer[8];
        int32_t count = desiredLocale.getKeywordValue("compat", buffer, sizeof(buffer), status);

        // if the locale has "@compat=host", create a host-specific NumberFormat
        if (U_SUCCESS(status) && count > 0 && uprv_strcmp(buffer, "host") == 0) {
            UBool curr = true;

            switch (style) {
            case UNUM_DECIMAL:
                curr = false;
                // fall-through
                U_FALLTHROUGH;

            case UNUM_CURRENCY:
            case UNUM_CURRENCY_ISO: // do not support plural formatting here
            case UNUM_CURRENCY_PLURAL:
            case UNUM_CURRENCY_ACCOUNTING:
            case UNUM_CASH_CURRENCY:
            case UNUM_CURRENCY_STANDARD:
            {
                LocalPointer<Win32NumberFormat> f(new Win32NumberFormat(desiredLocale, curr, status), status);
                if (U_SUCCESS(status)) {
                    return f.orphan();
                }
            }
            break;
            default:
                break;
            }
        }
    }
#endif
    // Use numbering system cache hashtable
    umtx_initOnce(gNSCacheInitOnce, &nscacheInit);

    // Get cached numbering system
    LocalPointer<NumberingSystem> ownedNs;
    NumberingSystem *ns = nullptr;
    if (NumberingSystem_cache != nullptr) {
        // TODO: Bad hash key usage, see ticket #8504.
        int32_t hashKey = desiredLocale.hashCode();

        static UMutex nscacheMutex;
        Mutex lock(&nscacheMutex);
        ns = static_cast<NumberingSystem*>(uhash_iget(NumberingSystem_cache, hashKey));
        if (ns == nullptr) {
            ns = NumberingSystem::createInstance(desiredLocale,status);
            uhash_iput(NumberingSystem_cache, hashKey, (void*)ns, &status);
        }
    } else {
        ownedNs.adoptInstead(NumberingSystem::createInstance(desiredLocale,status));
        ns = ownedNs.getAlias();
    }

    // check results of getting a numbering system
    if (U_FAILURE(status)) {
        return nullptr;
    }

    if (mustBeDecimalFormat && ns->isAlgorithmic()) {
        status = U_UNSUPPORTED_ERROR;
        return nullptr;
    }

    LocalPointer<DecimalFormatSymbols> symbolsToAdopt;
    UnicodeString pattern;
    LocalUResourceBundlePointer ownedResource(ures_open(nullptr, desiredLocale.getName(), &status));
    if (U_FAILURE(status)) {
        return nullptr;
    }
    else {
        // Loads the decimal symbols of the desired locale.
        symbolsToAdopt.adoptInsteadAndCheckErrorCode(new DecimalFormatSymbols(desiredLocale, status), status);
        if (U_FAILURE(status)) {
            return nullptr;
        }

        // Load the pattern from data using the common library function
        const char16_t* patternPtr = number::impl::utils::getPatternForStyle(
                desiredLocale,
                ns->getName(),
                gFormatCldrStyles[style],
                status);
        pattern = UnicodeString(true, patternPtr, -1);
    }
    if (U_FAILURE(status)) {
        return nullptr;
    }
    if(style==UNUM_CURRENCY || style == UNUM_CURRENCY_ISO || style == UNUM_CURRENCY_ACCOUNTING 
        || style == UNUM_CASH_CURRENCY || style == UNUM_CURRENCY_STANDARD){
        const char16_t* currPattern = symbolsToAdopt->getCurrencyPattern();
        if(currPattern!=nullptr){
            pattern.setTo(currPattern, u_strlen(currPattern));
        }
    }

    LocalPointer<NumberFormat> f;
    if (ns->isAlgorithmic()) {
        UnicodeString nsDesc;
        UnicodeString nsRuleSetGroup;
        UnicodeString nsRuleSetName;
        Locale nsLoc;
        URBNFRuleSetTag desiredRulesType = URBNF_NUMBERING_SYSTEM;

        nsDesc.setTo(ns->getDescription());
        int32_t firstSlash = nsDesc.indexOf(gSlash);
        int32_t lastSlash = nsDesc.lastIndexOf(gSlash);
        if ( lastSlash > firstSlash ) {
            CharString nsLocID;

            nsLocID.appendInvariantChars(nsDesc.tempSubString(0, firstSlash), status);
            nsRuleSetGroup.setTo(nsDesc,firstSlash+1,lastSlash-firstSlash-1);
            nsRuleSetName.setTo(nsDesc,lastSlash+1);

            nsLoc = Locale::createFromName(nsLocID.data());

            UnicodeString SpelloutRules = UNICODE_STRING_SIMPLE("SpelloutRules");
            if ( nsRuleSetGroup.compare(SpelloutRules) == 0 ) {
                desiredRulesType = URBNF_SPELLOUT;
            }
        } else {
            nsLoc = desiredLocale;
            nsRuleSetName.setTo(nsDesc);
        }

        RuleBasedNumberFormat *r = new RuleBasedNumberFormat(desiredRulesType,nsLoc,status);
        if (r == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return nullptr;
        }
        r->setDefaultRuleSet(nsRuleSetName,status);
        f.adoptInstead(r);
    } else {
        // replace single currency sign in the pattern with double currency sign
        // if the style is UNUM_CURRENCY_ISO
        if (style == UNUM_CURRENCY_ISO) {
            pattern.findAndReplace(UnicodeString(true, gSingleCurrencySign, 1),
                                   UnicodeString(true, gDoubleCurrencySign, 2));
        }

        // "new DecimalFormat()" does not adopt the symbols argument if its memory allocation fails.
        // So we can't use adoptInsteadAndCheckErrorCode as we need to know if the 'new' failed.
        DecimalFormatSymbols *syms = symbolsToAdopt.getAlias();
        LocalPointer<DecimalFormat> df(new DecimalFormat(pattern, syms, style, status));

        if (df.isValid()) {
            // if the DecimalFormat object was successfully new'ed, then it will own symbolsToAdopt, even if the status is a failure.
            symbolsToAdopt.orphan();
        }
        else {
            status = U_MEMORY_ALLOCATION_ERROR;
        }

        if (U_FAILURE(status)) {
            return nullptr;
        }

        // if it is cash currency style, setCurrencyUsage with usage
        if (style == UNUM_CASH_CURRENCY){
            df->setCurrencyUsage(UCURR_USAGE_CASH, &status);
        }

        if (U_FAILURE(status)) {
            return nullptr;
        }

        f.adoptInstead(df.orphan());
    }

    f->setLocaleIDs(ures_getLocaleByType(ownedResource.getAlias(), ULOC_VALID_LOCALE, &status),
                    ures_getLocaleByType(ownedResource.getAlias(), ULOC_ACTUAL_LOCALE, &status));
    if (U_FAILURE(status)) {
        return nullptr;
    }
    return f.orphan();
}

/**
 * Get the rounding mode.
 * @return A rounding mode
 */
NumberFormat::ERoundingMode NumberFormat::getRoundingMode() const {
    // Default value. ICU4J throws an exception and we can't change this API.
    return NumberFormat::ERoundingMode::kRoundUnnecessary;
}

/**
 * Set the rounding mode.  This has no effect unless the rounding
 * increment is greater than zero.
 * @param roundingMode A rounding mode
 */
void NumberFormat::setRoundingMode(NumberFormat::ERoundingMode /*roundingMode*/) {
    // No-op ICU4J throws an exception, and we can't change this API.
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */

//eof
