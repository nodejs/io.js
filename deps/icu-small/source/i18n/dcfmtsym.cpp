// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 1997-2016, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*
* File DCFMTSYM.CPP
*
* Modification History:
*
*   Date        Name        Description
*   02/19/97    aliu        Converted from java.
*   03/18/97    clhuang     Implemented with C++ APIs.
*   03/27/97    helena      Updated to pass the simple test after code review.
*   08/26/97    aliu        Added currency/intl currency symbol support.
*   07/20/98    stephen     Slightly modified initialization of monetarySeparator
********************************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/dcfmtsym.h"
#include "unicode/ures.h"
#include "unicode/decimfmt.h"
#include "unicode/ucurr.h"
#include "unicode/choicfmt.h"
#include "unicode/unistr.h"
#include "unicode/numsys.h"
#include "unicode/unum.h"
#include "unicode/utf16.h"
#include "ucurrimp.h"
#include "cstring.h"
#include "locbased.h"
#include "uresimp.h"
#include "ureslocs.h"
#include "charstr.h"
#include "uassert.h"

// *****************************************************************************
// class DecimalFormatSymbols
// *****************************************************************************

U_NAMESPACE_BEGIN

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(DecimalFormatSymbols)

static const char gNumberElements[] = "NumberElements";
static const char gCurrencySpacingTag[] = "currencySpacing";
static const char gBeforeCurrencyTag[] = "beforeCurrency";
static const char gAfterCurrencyTag[] = "afterCurrency";
static const char gCurrencyMatchTag[] = "currencyMatch";
static const char gCurrencySudMatchTag[] = "surroundingMatch";
static const char gCurrencyInsertBtnTag[] = "insertBetween";
static const char gLatn[] =  "latn";
static const char gSymbols[] = "symbols";
static const char gNumberElementsLatnSymbols[] = "NumberElements/latn/symbols";

static const char16_t INTL_CURRENCY_SYMBOL_STR[] = {0xa4, 0xa4, 0};

// List of field names to be loaded from the data files.
// These are parallel with the enum ENumberFormatSymbol in unicode/dcfmtsym.h.
static const char *gNumberElementKeys[DecimalFormatSymbols::kFormatSymbolCount] = {
    "decimal",
    "group",
    nullptr, /* #11897: the <list> symbol is NOT the pattern separator symbol */
    "percentSign",
    nullptr, /* Native zero digit is deprecated from CLDR - get it from the numbering system */
    nullptr, /* Pattern digit character is deprecated from CLDR - use # by default always */
    "minusSign",
    "plusSign",
    nullptr, /* currency symbol - Wait until we know the currency before loading from CLDR */
    nullptr, /* intl currency symbol - Wait until we know the currency before loading from CLDR */
    "currencyDecimal",
    "exponential",
    "perMille",
    nullptr, /* Escape padding character - not in CLDR */
    "infinity",
    "nan",
    nullptr, /* Significant digit symbol - not in CLDR */
    "currencyGroup",
    nullptr, /* one digit - get it from the numbering system */
    nullptr, /* two digit - get it from the numbering system */
    nullptr, /* three digit - get it from the numbering system */
    nullptr, /* four digit - get it from the numbering system */
    nullptr, /* five digit - get it from the numbering system */
    nullptr, /* six digit - get it from the numbering system */
    nullptr, /* seven digit - get it from the numbering system */
    nullptr, /* eight digit - get it from the numbering system */
    nullptr, /* nine digit - get it from the numbering system */
    "superscriptingExponent", /* Multiplication (x) symbol for exponents */
    "approximatelySign" /* Approximately sign symbol */
};

// -------------------------------------
// Initializes this with the decimal format symbols in the default locale.

DecimalFormatSymbols::DecimalFormatSymbols(UErrorCode& status)
        : UObject(), locale() {
    initialize(locale, status, true);
}

// -------------------------------------
// Initializes this with the decimal format symbols in the desired locale.

DecimalFormatSymbols::DecimalFormatSymbols(const Locale& loc, UErrorCode& status)
        : UObject(), locale(loc) {
    initialize(locale, status);
}

DecimalFormatSymbols::DecimalFormatSymbols(const Locale& loc, const NumberingSystem& ns, UErrorCode& status)
        : UObject(), locale(loc) {
    initialize(locale, status, false, &ns);
}

DecimalFormatSymbols::DecimalFormatSymbols()
        : UObject(), locale(Locale::getRoot()) {
    *validLocale = *actualLocale = 0;
    initialize();
}

DecimalFormatSymbols*
DecimalFormatSymbols::createWithLastResortData(UErrorCode& status) {
    if (U_FAILURE(status)) { return nullptr; }
    DecimalFormatSymbols* sym = new DecimalFormatSymbols();
    if (sym == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
    }
    return sym;
}

// -------------------------------------

DecimalFormatSymbols::~DecimalFormatSymbols()
{
}

// -------------------------------------
// copy constructor

DecimalFormatSymbols::DecimalFormatSymbols(const DecimalFormatSymbols &source)
    : UObject(source)
{
    *this = source;
}

// -------------------------------------
// assignment operator

DecimalFormatSymbols&
DecimalFormatSymbols::operator=(const DecimalFormatSymbols& rhs)
{
    if (this != &rhs) {
        for (int32_t i = 0; i < static_cast<int32_t>(kFormatSymbolCount); ++i) {
            // fastCopyFrom is safe, see docs on fSymbols
            fSymbols[static_cast<ENumberFormatSymbol>(i)].fastCopyFrom(rhs.fSymbols[static_cast<ENumberFormatSymbol>(i)]);
        }
        for (int32_t i = 0; i < static_cast<int32_t>(UNUM_CURRENCY_SPACING_COUNT); ++i) {
            currencySpcBeforeSym[i].fastCopyFrom(rhs.currencySpcBeforeSym[i]);
            currencySpcAfterSym[i].fastCopyFrom(rhs.currencySpcAfterSym[i]);
        }
        locale = rhs.locale;
        uprv_strcpy(validLocale, rhs.validLocale);
        uprv_strcpy(actualLocale, rhs.actualLocale);
        fIsCustomCurrencySymbol = rhs.fIsCustomCurrencySymbol; 
        fIsCustomIntlCurrencySymbol = rhs.fIsCustomIntlCurrencySymbol; 
        fCodePointZero = rhs.fCodePointZero;
        currPattern = rhs.currPattern;
        uprv_strcpy(nsName, rhs.nsName);
    }
    return *this;
}

// -------------------------------------

bool
DecimalFormatSymbols::operator==(const DecimalFormatSymbols& that) const
{
    if (this == &that) {
        return true;
    }
    if (fIsCustomCurrencySymbol != that.fIsCustomCurrencySymbol) { 
        return false;
    } 
    if (fIsCustomIntlCurrencySymbol != that.fIsCustomIntlCurrencySymbol) { 
        return false;
    } 
    for (int32_t i = 0; i < static_cast<int32_t>(kFormatSymbolCount); ++i) {
        if (fSymbols[static_cast<ENumberFormatSymbol>(i)] != that.fSymbols[static_cast<ENumberFormatSymbol>(i)]) {
            return false;
        }
    }
    for (int32_t i = 0; i < static_cast<int32_t>(UNUM_CURRENCY_SPACING_COUNT); ++i) {
        if(currencySpcBeforeSym[i] != that.currencySpcBeforeSym[i]) {
            return false;
        }
        if(currencySpcAfterSym[i] != that.currencySpcAfterSym[i]) {
            return false;
        }
    }
    // No need to check fCodePointZero since it is based on fSymbols
    return locale == that.locale &&
        uprv_strcmp(validLocale, that.validLocale) == 0 &&
        uprv_strcmp(actualLocale, that.actualLocale) == 0;
}

// -------------------------------------

namespace {

/**
 * Sink for enumerating all of the decimal format symbols (more specifically, anything
 * under the "NumberElements.symbols" tree).
 *
 * More specific bundles (en_GB) are enumerated before their parents (en_001, en, root):
 * Only store a value if it is still missing, that is, it has not been overridden.
 */
struct DecFmtSymDataSink : public ResourceSink {

    // Destination for data, modified via setters.
    DecimalFormatSymbols& dfs;
    // Boolean array of whether or not we have seen a particular symbol yet.
    // Can't simply check fSymbols because it is pre-populated with defaults.
    UBool seenSymbol[DecimalFormatSymbols::kFormatSymbolCount];

    // Constructor/Destructor
    DecFmtSymDataSink(DecimalFormatSymbols& _dfs) : dfs(_dfs) {
        uprv_memset(seenSymbol, false, sizeof(seenSymbol));
    }
    virtual ~DecFmtSymDataSink();

    virtual void put(const char *key, ResourceValue &value, UBool /*noFallback*/,
            UErrorCode &errorCode) override {
        ResourceTable symbolsTable = value.getTable(errorCode);
        if (U_FAILURE(errorCode)) { return; }
        for (int32_t j = 0; symbolsTable.getKeyAndValue(j, key, value); ++j) {
            for (int32_t i=0; i<DecimalFormatSymbols::kFormatSymbolCount; i++) {
                if (gNumberElementKeys[i] != nullptr && uprv_strcmp(key, gNumberElementKeys[i]) == 0) {
                    if (!seenSymbol[i]) {
                        seenSymbol[i] = true;
                        dfs.setSymbol(
                            static_cast<DecimalFormatSymbols::ENumberFormatSymbol>(i),
                            value.getUnicodeString(errorCode));
                        if (U_FAILURE(errorCode)) { return; }
                    }
                    break;
                }
            }
        }
    }

    // Returns true if all the symbols have been seen.
    UBool seenAll() {
        for (int32_t i=0; i<DecimalFormatSymbols::kFormatSymbolCount; i++) {
            if (!seenSymbol[i]) {
                return false;
            }
        }
        return true;
    }

    // If monetary decimal or grouping were not explicitly set, then set them to be the
    // same as their non-monetary counterparts.
    void resolveMissingMonetarySeparators(const UnicodeString* fSymbols) {
        if (!seenSymbol[DecimalFormatSymbols::kMonetarySeparatorSymbol]) {
            dfs.setSymbol(
                DecimalFormatSymbols::kMonetarySeparatorSymbol,
                fSymbols[DecimalFormatSymbols::kDecimalSeparatorSymbol]);
        }
        if (!seenSymbol[DecimalFormatSymbols::kMonetaryGroupingSeparatorSymbol]) {
            dfs.setSymbol(
                DecimalFormatSymbols::kMonetaryGroupingSeparatorSymbol,
                fSymbols[DecimalFormatSymbols::kGroupingSeparatorSymbol]);
        }
    }
};

struct CurrencySpacingSink : public ResourceSink {
    DecimalFormatSymbols& dfs;
    UBool hasBeforeCurrency;
    UBool hasAfterCurrency;

    CurrencySpacingSink(DecimalFormatSymbols& _dfs)
        : dfs(_dfs), hasBeforeCurrency(false), hasAfterCurrency(false) {}
    virtual ~CurrencySpacingSink();

    virtual void put(const char *key, ResourceValue &value, UBool /*noFallback*/,
            UErrorCode &errorCode) override {
        ResourceTable spacingTypesTable = value.getTable(errorCode);
        for (int32_t i = 0; spacingTypesTable.getKeyAndValue(i, key, value); ++i) {
            UBool beforeCurrency;
            if (uprv_strcmp(key, gBeforeCurrencyTag) == 0) {
                beforeCurrency = true;
                hasBeforeCurrency = true;
            } else if (uprv_strcmp(key, gAfterCurrencyTag) == 0) {
                beforeCurrency = false;
                hasAfterCurrency = true;
            } else {
                continue;
            }

            ResourceTable patternsTable = value.getTable(errorCode);
            for (int32_t j = 0; patternsTable.getKeyAndValue(j, key, value); ++j) {
                UCurrencySpacing pattern;
                if (uprv_strcmp(key, gCurrencyMatchTag) == 0) {
                    pattern = UNUM_CURRENCY_MATCH;
                } else if (uprv_strcmp(key, gCurrencySudMatchTag) == 0) {
                    pattern = UNUM_CURRENCY_SURROUNDING_MATCH;
                } else if (uprv_strcmp(key, gCurrencyInsertBtnTag) == 0) {
                    pattern = UNUM_CURRENCY_INSERT;
                } else {
                    continue;
                }

                const UnicodeString& current = dfs.getPatternForCurrencySpacing(
                    pattern, beforeCurrency, errorCode);
                if (current.isEmpty()) {
                    dfs.setPatternForCurrencySpacing(
                        pattern, beforeCurrency, value.getUnicodeString(errorCode));
                }
            }
        }
    }

    void resolveMissing() {
        // For consistency with Java, this method overwrites everything with the defaults unless
        // both beforeCurrency and afterCurrency were found in CLDR.
        static const char* defaults[] = { "[:letter:]", "[:digit:]", " " };
        if (!hasBeforeCurrency || !hasAfterCurrency) {
            for (int32_t pattern = 0; pattern < UNUM_CURRENCY_SPACING_COUNT; pattern++) {
                dfs.setPatternForCurrencySpacing(static_cast<UCurrencySpacing>(pattern),
                    false, UnicodeString(defaults[pattern], -1, US_INV));
            }
            for (int32_t pattern = 0; pattern < UNUM_CURRENCY_SPACING_COUNT; pattern++) {
                dfs.setPatternForCurrencySpacing(static_cast<UCurrencySpacing>(pattern),
                    true, UnicodeString(defaults[pattern], -1, US_INV));
            }
        }
    }
};

// Virtual destructors must be defined out of line.
DecFmtSymDataSink::~DecFmtSymDataSink() {}
CurrencySpacingSink::~CurrencySpacingSink() {}

} // namespace

void
DecimalFormatSymbols::initialize(const Locale& loc, UErrorCode& status,
    UBool useLastResortData, const NumberingSystem* ns)
{
    if (U_FAILURE(status)) { return; }
    *validLocale = *actualLocale = 0;

    // First initialize all the symbols to the fallbacks for anything we can't find
    initialize();

    //
    // Next get the numbering system for this locale and set zero digit
    // and the digit string based on the numbering system for the locale
    //
    LocalPointer<NumberingSystem> nsLocal;
    if (ns == nullptr) {
        // Use the numbering system according to the locale.
        // Save it into a LocalPointer so it gets cleaned up.
        nsLocal.adoptInstead(NumberingSystem::createInstance(loc, status));
        ns = nsLocal.getAlias();
    }
    const char *nsName;
    if (U_SUCCESS(status) && ns->getRadix() == 10 && !ns->isAlgorithmic()) {
        nsName = ns->getName();
        UnicodeString digitString(ns->getDescription());
        int32_t digitIndex = 0;
        UChar32 digit = digitString.char32At(0);
        fSymbols[kZeroDigitSymbol].setTo(digit);
        for (int32_t i = kOneDigitSymbol; i <= kNineDigitSymbol; ++i) {
            digitIndex += U16_LENGTH(digit);
            digit = digitString.char32At(digitIndex);
            fSymbols[i].setTo(digit);
        }
    } else {
        nsName = gLatn;
    }
    uprv_strcpy(this->nsName, nsName);

    // Open resource bundles
    const char* locStr = loc.getName();
    LocalUResourceBundlePointer resource(ures_open(nullptr, locStr, &status));
    LocalUResourceBundlePointer numberElementsRes(
        ures_getByKeyWithFallback(resource.getAlias(), gNumberElements, nullptr, &status));

    if (U_FAILURE(status)) {
        if ( useLastResortData ) {
            status = U_USING_DEFAULT_WARNING;
            initialize();
        }
        return;
    }

    // Set locale IDs
    // TODO: Is there a way to do this without depending on the resource bundle instance?
    U_LOCALE_BASED(locBased, *this);
    locBased.setLocaleIDs(
        ures_getLocaleByType(
            numberElementsRes.getAlias(),
            ULOC_VALID_LOCALE, &status),
        ures_getLocaleByType(
            numberElementsRes.getAlias(),
            ULOC_ACTUAL_LOCALE, &status));

    // Now load the rest of the data from the data sink.
    // Start with loading this nsName if it is not Latin.
    DecFmtSymDataSink sink(*this);
    if (uprv_strcmp(nsName, gLatn) != 0) {
        CharString path;
        path.append(gNumberElements, status)
            .append('/', status)
            .append(nsName, status)
            .append('/', status)
            .append(gSymbols, status);
        ures_getAllItemsWithFallback(resource.getAlias(), path.data(), sink, status);

        // If no symbols exist for the given nsName and resource bundle, silently ignore
        // and fall back to Latin.
        if (status == U_MISSING_RESOURCE_ERROR) {
            status = U_ZERO_ERROR;
        } else if (U_FAILURE(status)) {
            return;
        }
    }

    // Continue with Latin if necessary.
    if (!sink.seenAll()) {
        ures_getAllItemsWithFallback(resource.getAlias(), gNumberElementsLatnSymbols, sink, status);
        if (U_FAILURE(status)) { return; }
    }

    // Let the monetary number separators equal the default number separators if necessary.
    sink.resolveMissingMonetarySeparators(fSymbols);

    // Resolve codePointZero
    UChar32 tempCodePointZero = -1;
    for (int32_t i=0; i<=9; i++) {
        const UnicodeString& stringDigit = getConstDigitSymbol(i);
        if (stringDigit.countChar32() != 1) {
            tempCodePointZero = -1;
            break;
        }
        UChar32 cp = stringDigit.char32At(0);
        if (i == 0) {
            tempCodePointZero = cp;
        } else if (cp != tempCodePointZero + i) {
            tempCodePointZero = -1;
            break;
        }
    }
    fCodePointZero = tempCodePointZero;

    // Get the default currency from the currency API.
    UErrorCode internalStatus = U_ZERO_ERROR; // don't propagate failures out
    char16_t curriso[4];
    UnicodeString tempStr;
    int32_t currisoLength = ucurr_forLocale(locStr, curriso, UPRV_LENGTHOF(curriso), &internalStatus);
    if (U_SUCCESS(internalStatus) && currisoLength == 3) {
        setCurrency(curriso, status);
    } else {
        setCurrency(nullptr, status);
    }

    // Currency Spacing.
    LocalUResourceBundlePointer currencyResource(ures_open(U_ICUDATA_CURR, locStr, &status));
    CurrencySpacingSink currencySink(*this);
    ures_getAllItemsWithFallback(currencyResource.getAlias(), gCurrencySpacingTag, currencySink, status);
    currencySink.resolveMissing();
    if (U_FAILURE(status)) { return; }
}

void
DecimalFormatSymbols::initialize() {
    /*
     * These strings used to be in static arrays, but the HP/UX aCC compiler
     * cannot initialize a static array with class constructors.
     *  markus 2000may25
     */
    fSymbols[kDecimalSeparatorSymbol] = static_cast<char16_t>(0x2e); // '.' decimal separator
    fSymbols[kGroupingSeparatorSymbol].remove();        //     group (thousands) separator
    fSymbols[kPatternSeparatorSymbol] = static_cast<char16_t>(0x3b); // ';' pattern separator
    fSymbols[kPercentSymbol] = static_cast<char16_t>(0x25);          // '%' percent sign
    fSymbols[kZeroDigitSymbol] = static_cast<char16_t>(0x30);        // '0' native 0 digit
    fSymbols[kOneDigitSymbol] = static_cast<char16_t>(0x31);         // '1' native 1 digit
    fSymbols[kTwoDigitSymbol] = static_cast<char16_t>(0x32);         // '2' native 2 digit
    fSymbols[kThreeDigitSymbol] = static_cast<char16_t>(0x33);       // '3' native 3 digit
    fSymbols[kFourDigitSymbol] = static_cast<char16_t>(0x34);        // '4' native 4 digit
    fSymbols[kFiveDigitSymbol] = static_cast<char16_t>(0x35);        // '5' native 5 digit
    fSymbols[kSixDigitSymbol] = static_cast<char16_t>(0x36);         // '6' native 6 digit
    fSymbols[kSevenDigitSymbol] = static_cast<char16_t>(0x37);       // '7' native 7 digit
    fSymbols[kEightDigitSymbol] = static_cast<char16_t>(0x38);       // '8' native 8 digit
    fSymbols[kNineDigitSymbol] = static_cast<char16_t>(0x39);        // '9' native 9 digit
    fSymbols[kDigitSymbol] = static_cast<char16_t>(0x23);            // '#' pattern digit
    fSymbols[kPlusSignSymbol] = static_cast<char16_t>(0x002b);       // '+' plus sign
    fSymbols[kMinusSignSymbol] = static_cast<char16_t>(0x2d);        // '-' minus sign
    fSymbols[kCurrencySymbol] = static_cast<char16_t>(0xa4);         // 'OX' currency symbol
    fSymbols[kIntlCurrencySymbol].setTo(true, INTL_CURRENCY_SYMBOL_STR, 2);
    fSymbols[kMonetarySeparatorSymbol] = static_cast<char16_t>(0x2e);  // '.' monetary decimal separator
    fSymbols[kExponentialSymbol] = static_cast<char16_t>(0x45);        // 'E' exponential
    fSymbols[kPerMillSymbol] = static_cast<char16_t>(0x2030);          // '%o' per mill
    fSymbols[kPadEscapeSymbol] = static_cast<char16_t>(0x2a);          // '*' pad escape symbol
    fSymbols[kInfinitySymbol] = static_cast<char16_t>(0x221e);         // 'oo' infinite
    fSymbols[kNaNSymbol] = static_cast<char16_t>(0xfffd);              // SUB NaN
    fSymbols[kSignificantDigitSymbol] = static_cast<char16_t>(0x0040); // '@' significant digit
    fSymbols[kMonetaryGroupingSeparatorSymbol].remove(); // 
    fSymbols[kExponentMultiplicationSymbol] = static_cast<char16_t>(0xd7); // 'x' multiplication symbol for exponents
    fSymbols[kApproximatelySignSymbol] = u'~';          // '~' approximately sign
    fIsCustomCurrencySymbol = false; 
    fIsCustomIntlCurrencySymbol = false;
    fCodePointZero = 0x30;
    U_ASSERT(fCodePointZero == fSymbols[kZeroDigitSymbol].char32At(0));
    currPattern = nullptr;
    nsName[0] = 0;
}

void DecimalFormatSymbols::setCurrency(const char16_t* currency, UErrorCode& status) {
    // TODO: If this method is made public:
    // - Adopt ICU4J behavior of not allowing currency to be null.
    // - Also verify that the length of currency is 3.
    if (!currency) {
        return;
    }

    UnicodeString tempStr;
    uprv_getStaticCurrencyName(currency, locale.getName(), tempStr, status);
    if (U_SUCCESS(status)) {
        fSymbols[kIntlCurrencySymbol].setTo(currency, 3);
        fSymbols[kCurrencySymbol] = tempStr;
    }

    char cc[4]={0};
    u_UCharsToChars(currency, cc, 3);

    /* An explicit currency was requested */
    // TODO(ICU-13297): Move this data loading logic into a centralized place
    UErrorCode localStatus = U_ZERO_ERROR;
    LocalUResourceBundlePointer rbTop(ures_open(U_ICUDATA_CURR, locale.getName(), &localStatus));
    LocalUResourceBundlePointer rb(
        ures_getByKeyWithFallback(rbTop.getAlias(), "Currencies", nullptr, &localStatus));
    ures_getByKeyWithFallback(rb.getAlias(), cc, rb.getAlias(), &localStatus);
    if(U_SUCCESS(localStatus) && ures_getSize(rb.getAlias())>2) { // the length is 3 if more data is present
        ures_getByIndex(rb.getAlias(), 2, rb.getAlias(), &localStatus);
        int32_t currPatternLen = 0;
        currPattern =
            ures_getStringByIndex(rb.getAlias(), static_cast<int32_t>(0), &currPatternLen, &localStatus);
        UnicodeString decimalSep =
            ures_getUnicodeStringByIndex(rb.getAlias(), static_cast<int32_t>(1), &localStatus);
        UnicodeString groupingSep =
            ures_getUnicodeStringByIndex(rb.getAlias(), static_cast<int32_t>(2), &localStatus);
        if(U_SUCCESS(localStatus)){
            fSymbols[kMonetaryGroupingSeparatorSymbol] = groupingSep;
            fSymbols[kMonetarySeparatorSymbol] = decimalSep;
            //pattern.setTo(true, currPattern, currPatternLen);
        }
    }
    /* else An explicit currency was requested and is unknown or locale data is malformed. */
    /* ucurr_* API will get the correct value later on. */
}

Locale
DecimalFormatSymbols::getLocale(ULocDataLocaleType type, UErrorCode& status) const {
    U_LOCALE_BASED(locBased, *this);
    return locBased.getLocale(type, status);
}

const UnicodeString&
DecimalFormatSymbols::getPatternForCurrencySpacing(UCurrencySpacing type,
                                                 UBool beforeCurrency,
                                                 UErrorCode& status) const {
    if (U_FAILURE(status)) {
      return fNoSymbol;  // always empty.
    }
    if (beforeCurrency) {
      return currencySpcBeforeSym[static_cast<int32_t>(type)];
    } else {
      return currencySpcAfterSym[static_cast<int32_t>(type)];
    }
}

void
DecimalFormatSymbols::setPatternForCurrencySpacing(UCurrencySpacing type,
                                                   UBool beforeCurrency,
                                             const UnicodeString& pattern) {
  if (beforeCurrency) {
    currencySpcBeforeSym[static_cast<int32_t>(type)] = pattern;
  } else {
    currencySpcAfterSym[static_cast<int32_t>(type)] = pattern;
  }
}
U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */

//eof
