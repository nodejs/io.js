// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 1997-2015, International Business Machines
* Corporation and others. All Rights Reserved.
*******************************************************************************
*/

#ifndef NFRULE_H
#define NFRULE_H

#include "unicode/rbnf.h"

#if U_HAVE_RBNF

#include "unicode/utypes.h"
#include "unicode/uobject.h"
#include "unicode/unistr.h"

U_NAMESPACE_BEGIN

class FieldPosition;
class Formattable;
class NFRuleList;
class NFRuleSet;
class NFSubstitution;
class ParsePosition;
class PluralFormat;
class RuleBasedNumberFormat;
class UnicodeString;

class NFRule : public UMemory {
public:

    enum ERuleType {
        kNoBase = 0,
        kNegativeNumberRule = -1,
        kImproperFractionRule = -2,
        kProperFractionRule = -3,
        kDefaultRule = -4,
        kInfinityRule = -5,
        kNaNRule = -6,
        kOtherRule = -7
    };

    static void makeRules(UnicodeString& definition,
                          NFRuleSet* ruleSet, 
                          const NFRule* predecessor, 
                          const RuleBasedNumberFormat* rbnf, 
                          NFRuleList& ruleList,
                          UErrorCode& status);

    NFRule(const RuleBasedNumberFormat* rbnf, const UnicodeString &ruleText, UErrorCode &status);
    ~NFRule();

    bool operator==(const NFRule& rhs) const;
    bool operator!=(const NFRule& rhs) const { return !operator==(rhs); }

    ERuleType getType() const { return (ERuleType)(baseValue <= kNoBase ? (ERuleType)baseValue : kOtherRule); }
    void setType(ERuleType ruleType) { baseValue = (int32_t)ruleType; }

    int64_t getBaseValue() const { return baseValue; }
    void setBaseValue(int64_t value, UErrorCode& status);

    char16_t getDecimalPoint() const { return decimalPoint; }

    int64_t getDivisor() const;
    
    bool hasModulusSubstitution() const;

    void doFormat(int64_t number, UnicodeString& toAppendTo, int32_t pos, int32_t recursionCount, UErrorCode& status) const;
    void doFormat(double  number, UnicodeString& toAppendTo, int32_t pos, int32_t recursionCount, UErrorCode& status) const;

    UBool doParse(const UnicodeString& text, 
                  ParsePosition& pos, 
                  UBool isFractional, 
                  double upperBound,
                  uint32_t nonNumericalExecutedRuleMask,
                  Formattable& result) const;

    UBool shouldRollBack(int64_t number) const;

    void _appendRuleText(UnicodeString& result) const;

    int32_t findTextLenient(const UnicodeString& str, const UnicodeString& key, 
                     int32_t startingAt, int32_t* resultCount) const;

    void setDecimalFormatSymbols(const DecimalFormatSymbols &newSymbols, UErrorCode& status);

private:
    void parseRuleDescriptor(UnicodeString& descriptor, UErrorCode& status);
    void extractSubstitutions(const NFRuleSet* ruleSet, const UnicodeString &ruleText, const NFRule* predecessor, UErrorCode& status);
    NFSubstitution* extractSubstitution(const NFRuleSet* ruleSet, const NFRule* predecessor, UErrorCode& status);
    
    int16_t expectedExponent() const;
    int32_t indexOfAnyRulePrefix() const;
    double matchToDelimiter(const UnicodeString& text, int32_t startPos, double baseValue,
                            const UnicodeString& delimiter, ParsePosition& pp, const NFSubstitution* sub, 
                            uint32_t nonNumericalExecutedRuleMask,
                            double upperBound) const;
    void stripPrefix(UnicodeString& text, const UnicodeString& prefix, ParsePosition& pp) const;

    int32_t prefixLength(const UnicodeString& str, const UnicodeString& prefix, UErrorCode& status) const;
    UBool allIgnorable(const UnicodeString& str, UErrorCode& status) const;
    int32_t findText(const UnicodeString& str, const UnicodeString& key, 
                     int32_t startingAt, int32_t* resultCount) const;

private:
    int64_t baseValue;
    int32_t radix;
    int16_t exponent;
    char16_t decimalPoint;
    UnicodeString fRuleText;
    NFSubstitution* sub1;
    NFSubstitution* sub2;
    const RuleBasedNumberFormat* formatter;
    const PluralFormat* rulePatternFormat;

    NFRule(const NFRule &other); // forbid copying of this class
    NFRule &operator=(const NFRule &other); // forbid copying of this class
    
    // TODO: temporary hack to allow MultiplierSubstitution to get to formatter's rounding mode
    friend class MultiplierSubstitution;
};

U_NAMESPACE_END

/* U_HAVE_RBNF */
#endif

// NFRULE_H
#endif

