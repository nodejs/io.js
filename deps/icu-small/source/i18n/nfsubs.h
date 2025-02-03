// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
******************************************************************************
*   Copyright (C) 1997-2015, International Business Machines
*   Corporation and others.  All Rights Reserved.
******************************************************************************
*   file name:  nfsubs.h
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
* Modification history
* Date        Name      Comments
* 10/11/2001  Doug      Ported from ICU4J
*/

#ifndef NFSUBS_H
#define NFSUBS_H

#include "unicode/utypes.h"
#include "unicode/uobject.h"
#include "nfrule.h"

#if U_HAVE_RBNF

#include "unicode/utypes.h"
#include "unicode/decimfmt.h"
#include "nfrs.h"
#include <float.h>

U_NAMESPACE_BEGIN

class NFSubstitution : public UObject {
    int32_t pos;
    const NFRuleSet* ruleSet;
    DecimalFormat* numberFormat;
    
protected:
    NFSubstitution(int32_t pos,
        const NFRuleSet* ruleSet,
        const UnicodeString& description,
        UErrorCode& status);
    
    /**
     * Get the Ruleset of the object.
     * @return the Ruleset of the object.
     */
    const NFRuleSet* getRuleSet() const { return ruleSet; }

    /**
     * get the NumberFormat of this object.
     * @return the numberformat of this object.
     */
    const DecimalFormat* getNumberFormat() const { return numberFormat; }
    
public:
    static NFSubstitution* makeSubstitution(int32_t pos, 
        const NFRule* rule, 
        const NFRule* predecessor,
        const NFRuleSet* ruleSet, 
        const RuleBasedNumberFormat* rbnf, 
        const UnicodeString& description,
        UErrorCode& status);
    
    /**
     * Destructor.
     */
    virtual ~NFSubstitution();
    
    /**
     * Return true if the given Format objects are semantically equal.
     * Objects of different subclasses are considered unequal.
     * @param rhs    the object to be compared with.
     * @return       true if the given Format objects are semantically equal.
     */
    virtual bool operator==(const NFSubstitution& rhs) const;

    /**
     * Return true if the given Format objects are semantically unequal.
     * Objects of different subclasses are considered unequal.
     * @param rhs    the object to be compared with.
     * @return       true if the given Format objects are semantically unequal.
     */
    bool operator!=(const NFSubstitution& rhs) const { return !operator==(rhs); }
    
    /**
     * Sets the substitution's divisor.  Used by NFRule.setBaseValue().
     * A no-op for all substitutions except multiplier and modulus
     * substitutions.
     * @param radix The radix of the divisor
     * @param exponent The exponent of the divisor
     */
    virtual void setDivisor(int32_t radix, int16_t exponent, UErrorCode& status);
    
    /**
     * Replaces result with the string describing the substitution.
     * @param result    Output param which will receive the string.
     */
    virtual void toString(UnicodeString& result) const;
    
    void setDecimalFormatSymbols(const DecimalFormatSymbols &newSymbols, UErrorCode& status);

    //-----------------------------------------------------------------------
    // formatting
    //-----------------------------------------------------------------------
    
    /**
     * Performs a mathematical operation on the number, formats it using
     * either ruleSet or decimalFormat, and inserts the result into
     * toInsertInto.
     * @param number The number being formatted.
     * @param toInsertInto The string we insert the result into
     * @param pos The position in toInsertInto where the owning rule's
     * rule text begins (this value is added to this substitution's
     * position to determine exactly where to insert the new text)
     */
    virtual void doSubstitution(int64_t number, UnicodeString& toInsertInto, int32_t pos, int32_t recursionCount, UErrorCode& status) const;

    /**
     * Performs a mathematical operation on the number, formats it using
     * either ruleSet or decimalFormat, and inserts the result into
     * toInsertInto.
     * @param number The number being formatted.
     * @param toInsertInto The string we insert the result into
     * @param pos The position in toInsertInto where the owning rule's
     * rule text begins (this value is added to this substitution's
     * position to determine exactly where to insert the new text)
     */
    virtual void doSubstitution(double number, UnicodeString& toInsertInto, int32_t pos, int32_t recursionCount, UErrorCode& status) const;
    
protected:
    /**
     * Subclasses override this function to perform some kind of
     * mathematical operation on the number.  The result of this operation
     * is formatted using the rule set or DecimalFormat that this
     * substitution refers to, and the result is inserted into the result
     * string.
     * @param The number being formatted
     * @return The result of performing the opreration on the number
     */
    virtual int64_t transformNumber(int64_t number) const = 0;

    /**
     * Subclasses override this function to perform some kind of
     * mathematical operation on the number.  The result of this operation
     * is formatted using the rule set or DecimalFormat that this
     * substitution refers to, and the result is inserted into the result
     * string.
     * @param The number being formatted
     * @return The result of performing the opreration on the number
     */
    virtual double transformNumber(double number) const = 0;
    
public:
    //-----------------------------------------------------------------------
    // parsing
    //-----------------------------------------------------------------------
    
    /**
     * Parses a string using the rule set or DecimalFormat belonging
     * to this substitution.  If there's a match, a mathematical
     * operation (the inverse of the one used in formatting) is
     * performed on the result of the parse and the value passed in
     * and returned as the result.  The parse position is updated to
     * point to the first unmatched character in the string.
     * @param text The string to parse
     * @param parsePosition On entry, ignored, but assumed to be 0.
     * On exit, this is updated to point to the first unmatched
     * character (or 0 if the substitution didn't match)
     * @param baseValue A partial parse result that should be
     * combined with the result of this parse
     * @param upperBound When searching the rule set for a rule
     * matching the string passed in, only rules with base values
     * lower than this are considered
     * @param lenientParse If true and matching against rules fails,
     * the substitution will also try matching the text against
     * numerals using a default-costructed NumberFormat.  If false,
     * no extra work is done.  (This value is false whenever the
     * formatter isn't in lenient-parse mode, but is also false
     * under some conditions even when the formatter _is_ in
     * lenient-parse mode.)
     * @return If there's a match, this is the result of composing
     * baseValue with whatever was returned from matching the
     * characters.  This will be either a Long or a Double.  If there's
     * no match this is new Long(0) (not null), and parsePosition
     * is left unchanged.
     */
    virtual UBool doParse(const UnicodeString& text, 
        ParsePosition& parsePosition, 
        double baseValue,
        double upperBound, 
        UBool lenientParse,
        uint32_t nonNumericalExecutedRuleMask,
        int32_t recursionCount,
        Formattable& result) const;
    
    /**
     * Derives a new value from the two values passed in.  The two values
     * are typically either the base values of two rules (the one containing
     * the substitution and the one matching the substitution) or partial
     * parse results derived in some other way.  The operation is generally
     * the inverse of the operation performed by transformNumber().
     * @param newRuleValue The value produced by matching this substitution
     * @param oldRuleValue The value that was passed to the substitution
     * by the rule that owns it
     * @return A third value derived from the other two, representing a
     * partial parse result
     */
    virtual double composeRuleValue(double newRuleValue, double oldRuleValue) const = 0;
    
    /**
     * Calculates an upper bound when searching for a rule that matches
     * this substitution.  Rules with base values greater than or equal
     * to upperBound are not considered.
     * @param oldUpperBound    The current upper-bound setting.  The new
     *                         upper bound can't be any higher.
     * @return                 the upper bound when searching for a rule that matches
     *                         this substitution.
     */
    virtual double calcUpperBound(double oldUpperBound) const = 0;
    
    //-----------------------------------------------------------------------
    // simple accessors
    //-----------------------------------------------------------------------
    
    /**
     * Returns the substitution's position in the rule that owns it.
     * @return The substitution's position in the rule that owns it.
     */
    int32_t getPos() const { return pos; }
    
    /**
     * Returns the character used in the textual representation of
     * substitutions of this type.  Used by toString().
     * @return This substitution's token character.
     */
    virtual char16_t tokenChar() const = 0;
    
    /**
     * Returns true if this is a modulus substitution.  (We didn't do this
     * with instanceof partially because it causes source files to
     * proliferate and partially because we have to port this to C++.)
     * @return true if this object is an instance of ModulusSubstitution
     */
    virtual UBool isModulusSubstitution() const;
    
private:
    NFSubstitution(const NFSubstitution &other) = delete; // forbid copying of this class
    NFSubstitution &operator=(const NFSubstitution &other) = delete; // forbid copying of this class

public:
    static UClassID getStaticClassID();
    virtual UClassID getDynamicClassID() const override;
};

U_NAMESPACE_END

/* U_HAVE_RBNF */
#endif

// NFSUBS_H
#endif
