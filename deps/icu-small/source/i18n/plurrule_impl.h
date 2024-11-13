// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2007-2016, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*
* File PLURRULE_IMPL.H
*
*******************************************************************************
*/


#ifndef PLURRULE_IMPL
#define PLURRULE_IMPL

// Internal definitions for the PluralRules implementation.

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/format.h"
#include "unicode/locid.h"
#include "unicode/parseerr.h"
#include "unicode/strenum.h"
#include "unicode/ures.h"
#include "uvector.h"
#include "hash.h"
#include "uassert.h"

/**
 * A FixedDecimal version of UPLRULES_NO_UNIQUE_VALUE used in PluralRulesTest
 * for parsing of samples.
 */
#define UPLRULES_NO_UNIQUE_VALUE_DECIMAL(ERROR_CODE) (DecimalQuantity::fromExponentString(u"-0.00123456777", ERROR_CODE))

class PluralRulesTest;

U_NAMESPACE_BEGIN

class AndConstraint;
class RuleChain;
class DigitInterval;
class PluralRules;
class VisibleDigits;

namespace pluralimpl {

// TODO: Remove this and replace with u"" literals. Was for EBCDIC compatibility.

static const char16_t DOT = static_cast<char16_t>(0x002E);
static const char16_t SINGLE_QUOTE = static_cast<char16_t>(0x0027);
static const char16_t SLASH = static_cast<char16_t>(0x002F);
static const char16_t BACKSLASH = static_cast<char16_t>(0x005C);
static const char16_t SPACE = static_cast<char16_t>(0x0020);
static const char16_t EXCLAMATION = static_cast<char16_t>(0x0021);
static const char16_t QUOTATION_MARK = static_cast<char16_t>(0x0022);
static const char16_t NUMBER_SIGN = static_cast<char16_t>(0x0023);
static const char16_t PERCENT_SIGN = static_cast<char16_t>(0x0025);
static const char16_t ASTERISK = static_cast<char16_t>(0x002A);
static const char16_t COMMA = static_cast<char16_t>(0x002C);
static const char16_t HYPHEN = static_cast<char16_t>(0x002D);
static const char16_t U_ZERO = static_cast<char16_t>(0x0030);
static const char16_t U_ONE = static_cast<char16_t>(0x0031);
static const char16_t U_TWO = static_cast<char16_t>(0x0032);
static const char16_t U_THREE = static_cast<char16_t>(0x0033);
static const char16_t U_FOUR = static_cast<char16_t>(0x0034);
static const char16_t U_FIVE = static_cast<char16_t>(0x0035);
static const char16_t U_SIX = static_cast<char16_t>(0x0036);
static const char16_t U_SEVEN = static_cast<char16_t>(0x0037);
static const char16_t U_EIGHT = static_cast<char16_t>(0x0038);
static const char16_t U_NINE = static_cast<char16_t>(0x0039);
static const char16_t COLON = static_cast<char16_t>(0x003A);
static const char16_t SEMI_COLON = static_cast<char16_t>(0x003B);
static const char16_t EQUALS = static_cast<char16_t>(0x003D);
static const char16_t AT = static_cast<char16_t>(0x0040);
static const char16_t CAP_A = static_cast<char16_t>(0x0041);
static const char16_t CAP_B = static_cast<char16_t>(0x0042);
static const char16_t CAP_R = static_cast<char16_t>(0x0052);
static const char16_t CAP_Z = static_cast<char16_t>(0x005A);
static const char16_t LOWLINE = static_cast<char16_t>(0x005F);
static const char16_t LEFTBRACE = static_cast<char16_t>(0x007B);
static const char16_t RIGHTBRACE = static_cast<char16_t>(0x007D);
static const char16_t TILDE = static_cast<char16_t>(0x007E);
static const char16_t ELLIPSIS = static_cast<char16_t>(0x2026);

static const char16_t LOW_A = static_cast<char16_t>(0x0061);
static const char16_t LOW_B = static_cast<char16_t>(0x0062);
static const char16_t LOW_C = static_cast<char16_t>(0x0063);
static const char16_t LOW_D = static_cast<char16_t>(0x0064);
static const char16_t LOW_E = static_cast<char16_t>(0x0065);
static const char16_t LOW_F = static_cast<char16_t>(0x0066);
static const char16_t LOW_G = static_cast<char16_t>(0x0067);
static const char16_t LOW_H = static_cast<char16_t>(0x0068);
static const char16_t LOW_I = static_cast<char16_t>(0x0069);
static const char16_t LOW_J = static_cast<char16_t>(0x006a);
static const char16_t LOW_K = static_cast<char16_t>(0x006B);
static const char16_t LOW_L = static_cast<char16_t>(0x006C);
static const char16_t LOW_M = static_cast<char16_t>(0x006D);
static const char16_t LOW_N = static_cast<char16_t>(0x006E);
static const char16_t LOW_O = static_cast<char16_t>(0x006F);
static const char16_t LOW_P = static_cast<char16_t>(0x0070);
static const char16_t LOW_Q = static_cast<char16_t>(0x0071);
static const char16_t LOW_R = static_cast<char16_t>(0x0072);
static const char16_t LOW_S = static_cast<char16_t>(0x0073);
static const char16_t LOW_T = static_cast<char16_t>(0x0074);
static const char16_t LOW_U = static_cast<char16_t>(0x0075);
static const char16_t LOW_V = static_cast<char16_t>(0x0076);
static const char16_t LOW_W = static_cast<char16_t>(0x0077);
static const char16_t LOW_Y = static_cast<char16_t>(0x0079);
static const char16_t LOW_Z = static_cast<char16_t>(0x007A);

}


static const int32_t PLURAL_RANGE_HIGH = 0x7fffffff;

enum tokenType {
  none,
  tNumber,
  tComma,
  tSemiColon,
  tSpace,
  tColon,
  tAt,           // '@'
  tDot,
  tDot2,
  tEllipsis,
  tKeyword,
  tAnd,
  tOr,
  tMod,          // 'mod' or '%'
  tNot,          //  'not' only.
  tIn,           //  'in'  only.
  tEqual,        //  '='   only.
  tNotEqual,     //  '!='
  tTilde,
  tWithin,
  tIs,
  tVariableN,
  tVariableI,
  tVariableF,
  tVariableV,
  tVariableT,
  tVariableE,
  tVariableC,
  tDecimal,
  tInteger,
  tEOF
};


class PluralRuleParser: public UMemory {
public:
    PluralRuleParser();
    virtual ~PluralRuleParser();

    void parse(const UnicodeString &rules, PluralRules *dest, UErrorCode &status);
    void getNextToken(UErrorCode &status);
    void checkSyntax(UErrorCode &status);
    static int32_t getNumberValue(const UnicodeString &token);

private:
    static tokenType getKeyType(const UnicodeString& token, tokenType type);
    static tokenType charType(char16_t ch);
    static UBool isValidKeyword(const UnicodeString& token);

    const UnicodeString  *ruleSrc;  // The rules string.
    int32_t        ruleIndex;       // String index in the input rules, the current parse position.
    UnicodeString  token;           // Token most recently scanned.
    tokenType      type;
    tokenType      prevType;

                                    // The items currently being parsed & built.
                                    // Note: currentChain may not be the last RuleChain in the
                                    //       list because the "other" chain is forced to the end.
    AndConstraint *curAndConstraint;
    RuleChain     *currentChain;

    int32_t        rangeLowIdx;     // Indices in the UVector of ranges of the
    int32_t        rangeHiIdx;      //    low and hi values currently being parsed.

    enum EParseState {
       kKeyword,
       kExpr,
       kValue,
       kRangeList,
       kSamples
    };
};

enum PluralOperand {
    /**
    * The double value of the entire number.
    */
    PLURAL_OPERAND_N,

    /**
     * The integer value, with the fraction digits truncated off.
     */
    PLURAL_OPERAND_I,

    /**
     * All visible fraction digits as an integer, including trailing zeros.
     */
    PLURAL_OPERAND_F,

    /**
     * Visible fraction digits as an integer, not including trailing zeros.
     */
    PLURAL_OPERAND_T,

    /**
     * Number of visible fraction digits.
     */
    PLURAL_OPERAND_V,

    /**
     * Number of visible fraction digits, not including trailing zeros.
     */
    PLURAL_OPERAND_W,

    /**
     * Suppressed exponent for scientific notation (exponent needed in
     * scientific notation to approximate i).
     */
    PLURAL_OPERAND_E,

    /**
     * This operand is currently treated as an alias for `PLURAL_OPERAND_E`.
     * In the future, it will represent:
     *
     * Suppressed exponent for compact notation (exponent needed in
     * compact notation to approximate i).
     */
    PLURAL_OPERAND_C,

    /**
     * THIS OPERAND IS DEPRECATED AND HAS BEEN REMOVED FROM THE SPEC.
     *
     * <p>Returns the integer value, but will fail if the number has fraction digits.
     * That is, using "j" instead of "i" is like implicitly adding "v is 0".
     *
     * <p>For example, "j is 3" is equivalent to "i is 3 and v is 0": it matches
     * "3" but not "3.1" or "3.0".
     */
    PLURAL_OPERAND_J
};

/**
 * Converts from the tokenType enum to PluralOperand. Asserts that the given
 * tokenType can be mapped to a PluralOperand.
 */
PluralOperand tokenTypeToPluralOperand(tokenType tt);

/**
 * An interface to FixedDecimal, allowing for other implementations.
 * @internal
 */
class U_I18N_API IFixedDecimal {
  public:
    virtual ~IFixedDecimal();

    /**
     * Returns the value corresponding to the specified operand (n, i, f, t, v, or w).
     * If the operand is 'n', returns a double; otherwise, returns an integer.
     */
    virtual double getPluralOperand(PluralOperand operand) const = 0;

    virtual bool isNaN() const = 0;

    virtual bool isInfinite() const = 0;

    /** Whether the number has no nonzero fraction digits. */
    virtual bool hasIntegerValue() const = 0;
};

/**
 * class FixedDecimal serves to communicate the properties
 * of a formatted number from a decimal formatter to PluralRules::select()
 *
 * see DecimalFormat::getFixedDecimal()
 * @internal
 */
class U_I18N_API FixedDecimal: public IFixedDecimal, public UObject {
  public:
    /**
      * @param n   the number, e.g. 12.345
      * @param v   The number of visible fraction digits, e.g. 3
      * @param f   The fraction digits, e.g. 345
      * @param e   The exponent, e.g. 7 in 1.2e7, for scientific notation
      * @param c   Currently: an alias for param `e`.
      */
    FixedDecimal(double  n, int32_t v, int64_t f, int32_t e, int32_t c);
    FixedDecimal(double  n, int32_t v, int64_t f, int32_t e);
    FixedDecimal(double  n, int32_t v, int64_t f);
    FixedDecimal(double n, int32_t);
    explicit FixedDecimal(double n);
    FixedDecimal();
    ~FixedDecimal() override;
    FixedDecimal(const UnicodeString &s, UErrorCode &ec);
    FixedDecimal(const FixedDecimal &other);

    static FixedDecimal createWithExponent(double n, int32_t v, int32_t e);

    double getPluralOperand(PluralOperand operand) const override;
    bool isNaN() const override;
    bool isInfinite() const override;
    bool hasIntegerValue() const override;

    bool isNanOrInfinity() const;  // used in decimfmtimpl.cpp

    int32_t getVisibleFractionDigitCount() const;

    void init(double n, int32_t v, int64_t f, int32_t e, int32_t c);
    void init(double n, int32_t v, int64_t f, int32_t e);
    void init(double n, int32_t v, int64_t f);
    void init(double n);
    UBool quickInit(double n);  // Try a fast-path only initialization,
                                //    return true if successful.
    void adjustForMinFractionDigits(int32_t min);
    static int64_t getFractionalDigits(double n, int32_t v);
    static int32_t decimals(double n);

    FixedDecimal& operator=(const FixedDecimal& other) = default;
    bool operator==(const FixedDecimal &other) const;

    UnicodeString toString() const;

    double doubleValue() const;
    int64_t longValue() const;

    double      source;
    int32_t     visibleDecimalDigitCount;
    int64_t     decimalDigits;
    int64_t     decimalDigitsWithoutTrailingZeros;
    int64_t     intValue;
    int32_t     exponent;
    UBool       _hasIntegerValue;
    UBool       isNegative;
    UBool       _isNaN;
    UBool       _isInfinite;
};

class AndConstraint : public UMemory  {
public:
    typedef enum RuleOp {
        NONE,
        MOD
    } RuleOp;
    RuleOp op = AndConstraint::NONE;
    int32_t opNum = -1;             // for mod expressions, the right operand of the mod.
    int32_t value = -1;             // valid for 'is' rules only.
    UVector32 *rangeList = nullptr; // for 'in', 'within' rules. Null otherwise.
    UBool negated = false;          // true for negated rules.
    UBool integerOnly = false;      // true for 'within' rules.
    tokenType digitsType = none;    // n | i | v | f constraint.
    AndConstraint *next = nullptr;
    // Internal error status, used for errors that occur during the copy constructor.
    UErrorCode fInternalStatus = U_ZERO_ERROR;    

    AndConstraint() = default;
    AndConstraint(const AndConstraint& other);
    virtual ~AndConstraint();
    AndConstraint* add(UErrorCode& status);
    // UBool isFulfilled(double number);
    UBool isFulfilled(const IFixedDecimal &number);
};

class OrConstraint : public UMemory  {
public:
    AndConstraint *childNode = nullptr;
    OrConstraint *next = nullptr;
    // Internal error status, used for errors that occur during the copy constructor.
    UErrorCode fInternalStatus = U_ZERO_ERROR;

    OrConstraint() = default;
    OrConstraint(const OrConstraint& other);
    virtual ~OrConstraint();
    AndConstraint* add(UErrorCode& status);
    // UBool isFulfilled(double number);
    UBool isFulfilled(const IFixedDecimal &number);
};

class RuleChain : public UMemory  {
public:
    UnicodeString   fKeyword;
    RuleChain      *fNext = nullptr;
    OrConstraint   *ruleHeader = nullptr;
    UnicodeString   fDecimalSamples;  // Samples strings from rule source
    UnicodeString   fIntegerSamples;  //   without @decimal or @integer, otherwise unprocessed.
    UBool           fDecimalSamplesUnbounded = false;
    UBool           fIntegerSamplesUnbounded = false;
    // Internal error status, used for errors that occur during the copy constructor.
    UErrorCode      fInternalStatus = U_ZERO_ERROR;

    RuleChain() = default;
    RuleChain(const RuleChain& other);
    virtual ~RuleChain();

    UnicodeString select(const IFixedDecimal &number) const;
    void          dumpRules(UnicodeString& result);
    UErrorCode    getKeywords(int32_t maxArraySize, UnicodeString *keywords, int32_t& arraySize) const;
    UBool         isKeyword(const UnicodeString& keyword) const;
};

class PluralKeywordEnumeration : public StringEnumeration {
public:
    PluralKeywordEnumeration(RuleChain *header, UErrorCode& status);
    virtual ~PluralKeywordEnumeration();
    static UClassID U_EXPORT2 getStaticClassID();
    virtual UClassID getDynamicClassID() const override;
    virtual const UnicodeString* snext(UErrorCode& status) override;
    virtual void reset(UErrorCode& status) override;
    virtual int32_t count(UErrorCode& status) const override;
private:
    int32_t         pos;
    UVector         fKeywordNames;
};


class U_I18N_API PluralAvailableLocalesEnumeration: public StringEnumeration {
  public:
    PluralAvailableLocalesEnumeration(UErrorCode &status);
    virtual ~PluralAvailableLocalesEnumeration();
    virtual const char* next(int32_t *resultLength, UErrorCode& status) override;
    virtual void reset(UErrorCode& status) override;
    virtual int32_t count(UErrorCode& status) const override;
  private:
    UErrorCode      fOpenStatus;
    UResourceBundle *fLocales = nullptr;
    UResourceBundle *fRes = nullptr;
};

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */

#endif // _PLURRULE_IMPL
//eof
