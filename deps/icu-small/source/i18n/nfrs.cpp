// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
******************************************************************************
*   Copyright (C) 1997-2015, International Business Machines
*   Corporation and others.  All Rights Reserved.
******************************************************************************
*   file name:  nfrs.cpp
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
* Modification history
* Date        Name      Comments
* 10/11/2001  Doug      Ported from ICU4J
*/

#include "nfrs.h"

#if U_HAVE_RBNF

#include "unicode/uchar.h"
#include "nfrule.h"
#include "nfrlist.h"
#include "patternprops.h"
#include "putilimp.h"

#ifdef RBNF_DEBUG
#include "cmemory.h"
#endif

enum {
    /** -x */
    NEGATIVE_RULE_INDEX = 0,
    /** x.x */
    IMPROPER_FRACTION_RULE_INDEX = 1,
    /** 0.x */
    PROPER_FRACTION_RULE_INDEX = 2,
    /** x.0 */
    DEFAULT_RULE_INDEX = 3,
    /** Inf */
    INFINITY_RULE_INDEX = 4,
    /** NaN */
    NAN_RULE_INDEX = 5,
    NON_NUMERICAL_RULE_LENGTH = 6
};

U_NAMESPACE_BEGIN

#if 0
// euclid's algorithm works with doubles
// note, doubles only get us up to one quadrillion or so, which
// isn't as much range as we get with longs.  We probably still
// want either 64-bit math, or BigInteger.

static int64_t
util_lcm(int64_t x, int64_t y)
{
    x.abs();
    y.abs();

    if (x == 0 || y == 0) {
        return 0;
    } else {
        do {
            if (x < y) {
                int64_t t = x; x = y; y = t;
            }
            x -= y * (x/y);
        } while (x != 0);

        return y;
    }
}

#else
/**
 * Calculates the least common multiple of x and y.
 */
static int64_t
util_lcm(int64_t x, int64_t y)
{
    // binary gcd algorithm from Knuth, "The Art of Computer Programming,"
    // vol. 2, 1st ed., pp. 298-299
    int64_t x1 = x;
    int64_t y1 = y;

    int p2 = 0;
    while ((x1 & 1) == 0 && (y1 & 1) == 0) {
        ++p2;
        x1 >>= 1;
        y1 >>= 1;
    }

    int64_t t;
    if ((x1 & 1) == 1) {
        t = -y1;
    } else {
        t = x1;
    }

    while (t != 0) {
        while ((t & 1) == 0) {
            t = t >> 1;
        }
        if (t > 0) {
            x1 = t;
        } else {
            y1 = -t;
        }
        t = x1 - y1;
    }

    int64_t gcd = x1 << p2;

    // x * y == gcd(x, y) * lcm(x, y)
    return x / gcd * y;
}
#endif

static const char16_t gPercent = 0x0025;
static const char16_t gColon = 0x003a;
static const char16_t gSemicolon = 0x003b;
static const char16_t gLineFeed = 0x000a;

static const char16_t gPercentPercent[] =
{
    0x25, 0x25, 0
}; /* "%%" */

static const char16_t gNoparse[] =
{
    0x40, 0x6E, 0x6F, 0x70, 0x61, 0x72, 0x73, 0x65, 0
}; /* "@noparse" */

NFRuleSet::NFRuleSet(RuleBasedNumberFormat *_owner, UnicodeString* descriptions, int32_t index, UErrorCode& status)
  : name()
  , rules(0)
  , owner(_owner)
  , fractionRules()
  , fIsFractionRuleSet(false)
  , fIsPublic(false)
  , fIsParseable(true)
{
    for (int32_t i = 0; i < NON_NUMERICAL_RULE_LENGTH; ++i) {
        nonNumericalRules[i] = nullptr;
    }

    if (U_FAILURE(status)) {
        return;
    }

    UnicodeString& description = descriptions[index]; // !!! make sure index is valid

    if (description.length() == 0) {
        // throw new IllegalArgumentException("Empty rule set description");
        status = U_PARSE_ERROR;
        return;
    }

    // if the description begins with a rule set name (the rule set
    // name can be omitted in formatter descriptions that consist
    // of only one rule set), copy it out into our "name" member
    // and delete it from the description
    if (description.charAt(0) == gPercent) {
        int32_t pos = description.indexOf(gColon);
        if (pos == -1) {
            // throw new IllegalArgumentException("Rule set name doesn't end in colon");
            status = U_PARSE_ERROR;
        } else {
            name.setTo(description, 0, pos);
            while (pos < description.length() && PatternProps::isWhiteSpace(description.charAt(++pos))) {
            }
            description.remove(0, pos);
        }
    } else {
        name.setTo(UNICODE_STRING_SIMPLE("%default"));
    }

    if (description.length() == 0) {
        // throw new IllegalArgumentException("Empty rule set description");
        status = U_PARSE_ERROR;
    }

    fIsPublic = name.indexOf(gPercentPercent, 2, 0) != 0;

    if ( name.endsWith(gNoparse,8) ) {
        fIsParseable = false;
        name.truncate(name.length()-8); // remove the @noparse from the name
    }

    // all of the other members of NFRuleSet are initialized
    // by parseRules()
}

void
NFRuleSet::parseRules(UnicodeString& description, UErrorCode& status)
{
    // start by creating a Vector whose elements are Strings containing
    // the descriptions of the rules (one rule per element).  The rules
    // are separated by semicolons (there's no escape facility: ALL
    // semicolons are rule delimiters)

    if (U_FAILURE(status)) {
        return;
    }

    // ensure we are starting with an empty rule list
    rules.deleteAll();

    // dlf - the original code kept a separate description array for no reason,
    // so I got rid of it.  The loop was too complex so I simplified it.

    UnicodeString currentDescription;
    int32_t oldP = 0;
    while (oldP < description.length()) {
        int32_t p = description.indexOf(gSemicolon, oldP);
        if (p == -1) {
            p = description.length();
        }
        currentDescription.setTo(description, oldP, p - oldP);
        NFRule::makeRules(currentDescription, this, rules.last(), owner, rules, status);
        oldP = p + 1;
    }

    // for rules that didn't specify a base value, their base values
    // were initialized to 0.  Make another pass through the list and
    // set all those rules' base values.  We also remove any special
    // rules from the list and put them into their own member variables
    int64_t defaultBaseValue = 0;

    // (this isn't a for loop because we might be deleting items from
    // the vector-- we want to make sure we only increment i when
    // we _didn't_ delete anything from the vector)
    int32_t rulesSize = rules.size();
    for (int32_t i = 0; i < rulesSize; i++) {
        NFRule* rule = rules[i];
        int64_t baseValue = rule->getBaseValue();

        if (baseValue == 0) {
            // if the rule's base value is 0, fill in a default
            // base value (this will be 1 plus the preceding
            // rule's base value for regular rule sets, and the
            // same as the preceding rule's base value in fraction
            // rule sets)
            rule->setBaseValue(defaultBaseValue, status);
        }
        else {
            // if it's a regular rule that already knows its base value,
            // check to make sure the rules are in order, and update
            // the default base value for the next rule
            if (baseValue < defaultBaseValue) {
                // throw new IllegalArgumentException("Rules are not in order");
                status = U_PARSE_ERROR;
                return;
            }
            defaultBaseValue = baseValue;
        }
        if (!fIsFractionRuleSet) {
            ++defaultBaseValue;
        }
    }
}

/**
 * Set one of the non-numerical rules.
 * @param rule The rule to set.
 */
void NFRuleSet::setNonNumericalRule(NFRule *rule) {
    switch (rule->getBaseValue()) {
        case NFRule::kNegativeNumberRule:
            delete nonNumericalRules[NEGATIVE_RULE_INDEX];
            nonNumericalRules[NEGATIVE_RULE_INDEX] = rule;
            return;
        case NFRule::kImproperFractionRule:
            setBestFractionRule(IMPROPER_FRACTION_RULE_INDEX, rule, true);
            return;
        case NFRule::kProperFractionRule:
            setBestFractionRule(PROPER_FRACTION_RULE_INDEX, rule, true);
            return;
        case NFRule::kDefaultRule:
            setBestFractionRule(DEFAULT_RULE_INDEX, rule, true);
            return;
        case NFRule::kInfinityRule:
            delete nonNumericalRules[INFINITY_RULE_INDEX];
            nonNumericalRules[INFINITY_RULE_INDEX] = rule;
            return;
        case NFRule::kNaNRule:
            delete nonNumericalRules[NAN_RULE_INDEX];
            nonNumericalRules[NAN_RULE_INDEX] = rule;
            return;
        case NFRule::kNoBase:
        case NFRule::kOtherRule:
        default:
            // If we do not remember the rule inside the object.
            // delete it here to prevent memory leak.
            delete rule;
            return;
    }
}

/**
 * Determine the best fraction rule to use. Rules matching the decimal point from
 * DecimalFormatSymbols become the main set of rules to use.
 * @param originalIndex The index into nonNumericalRules
 * @param newRule The new rule to consider
 * @param rememberRule Should the new rule be added to fractionRules.
 */
void NFRuleSet::setBestFractionRule(int32_t originalIndex, NFRule *newRule, UBool rememberRule) {
    if (rememberRule) {
        fractionRules.add(newRule);
    }
    NFRule *bestResult = nonNumericalRules[originalIndex];
    if (bestResult == nullptr) {
        nonNumericalRules[originalIndex] = newRule;
    }
    else {
        // We have more than one. Which one is better?
        const DecimalFormatSymbols *decimalFormatSymbols = owner->getDecimalFormatSymbols();
        if (decimalFormatSymbols->getSymbol(DecimalFormatSymbols::kDecimalSeparatorSymbol).charAt(0)
            == newRule->getDecimalPoint())
        {
            nonNumericalRules[originalIndex] = newRule;
        }
        // else leave it alone
    }
}

NFRuleSet::~NFRuleSet()
{
    for (int i = 0; i < NON_NUMERICAL_RULE_LENGTH; i++) {
        if (i != IMPROPER_FRACTION_RULE_INDEX
            && i != PROPER_FRACTION_RULE_INDEX
            && i != DEFAULT_RULE_INDEX)
        {
            delete nonNumericalRules[i];
        }
        // else it will be deleted via NFRuleList fractionRules
    }
}

static UBool
util_equalRules(const NFRule* rule1, const NFRule* rule2)
{
    if (rule1) {
        if (rule2) {
            return *rule1 == *rule2;
        }
    } else if (!rule2) {
        return true;
    }
    return false;
}

bool
NFRuleSet::operator==(const NFRuleSet& rhs) const
{
    if (rules.size() == rhs.rules.size() &&
        fIsFractionRuleSet == rhs.fIsFractionRuleSet &&
        name == rhs.name) {

        // ...then compare the non-numerical rule lists...
        for (int i = 0; i < NON_NUMERICAL_RULE_LENGTH; i++) {
            if (!util_equalRules(nonNumericalRules[i], rhs.nonNumericalRules[i])) {
                return false;
            }
        }

        // ...then compare the rule lists...
        for (uint32_t i = 0; i < rules.size(); ++i) {
            if (*rules[i] != *rhs.rules[i]) {
                return false;
            }
        }
        return true;
    }
    return false;
}

void
NFRuleSet::setDecimalFormatSymbols(const DecimalFormatSymbols &newSymbols, UErrorCode& status) {
    for (uint32_t i = 0; i < rules.size(); ++i) {
        rules[i]->setDecimalFormatSymbols(newSymbols, status);
    }
    // Switch the fraction rules to mirror the DecimalFormatSymbols.
    for (int32_t nonNumericalIdx = IMPROPER_FRACTION_RULE_INDEX; nonNumericalIdx <= DEFAULT_RULE_INDEX; nonNumericalIdx++) {
        if (nonNumericalRules[nonNumericalIdx]) {
            for (uint32_t fIdx = 0; fIdx < fractionRules.size(); fIdx++) {
                NFRule *fractionRule = fractionRules[fIdx];
                if (nonNumericalRules[nonNumericalIdx]->getBaseValue() == fractionRule->getBaseValue()) {
                    setBestFractionRule(nonNumericalIdx, fractionRule, false);
                }
            }
        }
    }

    for (uint32_t nnrIdx = 0; nnrIdx < NON_NUMERICAL_RULE_LENGTH; nnrIdx++) {
        NFRule *rule = nonNumericalRules[nnrIdx];
        if (rule) {
            rule->setDecimalFormatSymbols(newSymbols, status);
        }
    }
}

#define RECURSION_LIMIT 64

void
NFRuleSet::format(int64_t number, UnicodeString& toAppendTo, int32_t pos, int32_t recursionCount, UErrorCode& status) const
{
    if (recursionCount >= RECURSION_LIMIT) {
        // stop recursion
        status = U_INVALID_STATE_ERROR;
        return;
    }
    const NFRule *rule = findNormalRule(number);
    if (rule) { // else error, but can't report it
        rule->doFormat(number, toAppendTo, pos, ++recursionCount, status);
    }
}

void
NFRuleSet::format(double number, UnicodeString& toAppendTo, int32_t pos, int32_t recursionCount, UErrorCode& status) const
{
    if (recursionCount >= RECURSION_LIMIT) {
        // stop recursion
        status = U_INVALID_STATE_ERROR;
        return;
    }
    const NFRule *rule = findDoubleRule(number);
    if (rule) { // else error, but can't report it
        rule->doFormat(number, toAppendTo, pos, ++recursionCount, status);
    }
}

const NFRule*
NFRuleSet::findDoubleRule(double number) const
{
    // if this is a fraction rule set, use findFractionRuleSetRule()
    if (isFractionRuleSet()) {
        return findFractionRuleSetRule(number);
    }

    if (uprv_isNaN(number)) {
        const NFRule *rule = nonNumericalRules[NAN_RULE_INDEX];
        if (!rule) {
            rule = owner->getDefaultNaNRule();
        }
        return rule;
    }

    // if the number is negative, return the negative number rule
    // (if there isn't a negative-number rule, we pretend it's a
    // positive number)
    if (number < 0) {
        if (nonNumericalRules[NEGATIVE_RULE_INDEX]) {
            return  nonNumericalRules[NEGATIVE_RULE_INDEX];
        } else {
            number = -number;
        }
    }

    if (uprv_isInfinite(number)) {
        const NFRule *rule = nonNumericalRules[INFINITY_RULE_INDEX];
        if (!rule) {
            rule = owner->getDefaultInfinityRule();
        }
        return rule;
    }

    // if the number isn't an integer, we use one of the fraction rules...
    if (number != uprv_floor(number)) {
        // if the number is between 0 and 1, return the proper
        // fraction rule
        if (number < 1 && nonNumericalRules[PROPER_FRACTION_RULE_INDEX]) {
            return nonNumericalRules[PROPER_FRACTION_RULE_INDEX];
        }
        // otherwise, return the improper fraction rule
        else if (nonNumericalRules[IMPROPER_FRACTION_RULE_INDEX]) {
            return nonNumericalRules[IMPROPER_FRACTION_RULE_INDEX];
        }
    }

    // if there's a default rule, use it to format the number
    if (nonNumericalRules[DEFAULT_RULE_INDEX]) {
        return nonNumericalRules[DEFAULT_RULE_INDEX];
    }

    // and if we haven't yet returned a rule, use findNormalRule()
    // to find the applicable rule
    int64_t r = util64_fromDouble(number + 0.5);
    return findNormalRule(r);
}

const NFRule *
NFRuleSet::findNormalRule(int64_t number) const
{
    // if this is a fraction rule set, use findFractionRuleSetRule()
    // to find the rule (we should only go into this clause if the
    // value is 0)
    if (fIsFractionRuleSet) {
        return findFractionRuleSetRule(static_cast<double>(number));
    }

    // if the number is negative, return the negative-number rule
    // (if there isn't one, pretend the number is positive)
    if (number < 0) {
        if (nonNumericalRules[NEGATIVE_RULE_INDEX]) {
            return nonNumericalRules[NEGATIVE_RULE_INDEX];
        } else {
            number = -number;
        }
    }

    // we have to repeat the preceding two checks, even though we
    // do them in findRule(), because the version of format() that
    // takes a long bypasses findRule() and goes straight to this
    // function.  This function does skip the fraction rules since
    // we know the value is an integer (it also skips the default
    // rule, since it's considered a fraction rule.  Skipping the
    // default rule in this function is also how we avoid infinite
    // recursion)

    // {dlf} unfortunately this fails if there are no rules except
    // special rules.  If there are no rules, use the default rule.

    // binary-search the rule list for the applicable rule
    // (a rule is used for all values from its base value to
    // the next rule's base value)
    int32_t hi = rules.size();
    if (hi > 0) {
        int32_t lo = 0;

        while (lo < hi) {
            int32_t mid = (lo + hi) / 2;
            if (rules[mid]->getBaseValue() == number) {
                return rules[mid];
            }
            else if (rules[mid]->getBaseValue() > number) {
                hi = mid;
            }
            else {
                lo = mid + 1;
            }
        }
        if (hi == 0) { // bad rule set, minimum base > 0
            return nullptr; // want to throw exception here
        }

        NFRule *result = rules[hi - 1];

        // use shouldRollBack() to see whether we need to invoke the
        // rollback rule (see shouldRollBack()'s documentation for
        // an explanation of the rollback rule).  If we do, roll back
        // one rule and return that one instead of the one we'd normally
        // return
        if (result->shouldRollBack(number)) {
            if (hi == 1) { // bad rule set, no prior rule to rollback to from this base
                return nullptr;
            }
            result = rules[hi - 2];
        }
        return result;
    }
    // else use the default rule
    return nonNumericalRules[DEFAULT_RULE_INDEX];
}

/**
 * If this rule is a fraction rule set, this function is used by
 * findRule() to select the most appropriate rule for formatting
 * the number.  Basically, the base value of each rule in the rule
 * set is treated as the denominator of a fraction.  Whichever
 * denominator can produce the fraction closest in value to the
 * number passed in is the result.  If there's a tie, the earlier
 * one in the list wins.  (If there are two rules in a row with the
 * same base value, the first one is used when the numerator of the
 * fraction would be 1, and the second rule is used the rest of the
 * time.
 * @param number The number being formatted (which will always be
 * a number between 0 and 1)
 * @return The rule to use to format this number
 */
const NFRule*
NFRuleSet::findFractionRuleSetRule(double number) const
{
    // the obvious way to do this (multiply the value being formatted
    // by each rule's base value until you get an integral result)
    // doesn't work because of rounding error.  This method is more
    // accurate

    // find the least common multiple of the rules' base values
    // and multiply this by the number being formatted.  This is
    // all the precision we need, and we can do all of the rest
    // of the math using integer arithmetic
    int64_t leastCommonMultiple = rules[0]->getBaseValue();
    int64_t numerator;
    {
        for (uint32_t i = 1; i < rules.size(); ++i) {
            leastCommonMultiple = util_lcm(leastCommonMultiple, rules[i]->getBaseValue());
        }
        numerator = util64_fromDouble(number * static_cast<double>(leastCommonMultiple) + 0.5);
    }
    // for each rule, do the following...
    int64_t tempDifference;
    int64_t difference = util64_fromDouble(uprv_maxMantissa());
    int32_t winner = 0;
    for (uint32_t i = 0; i < rules.size(); ++i) {
        // "numerator" is the numerator of the fraction if the
        // denominator is the LCD.  The numerator if the rule's
        // base value is the denominator is "numerator" times the
        // base value divided bythe LCD.  Here we check to see if
        // that's an integer, and if not, how close it is to being
        // an integer.
        tempDifference = numerator * rules[i]->getBaseValue() % leastCommonMultiple;


        // normalize the result of the above calculation: we want
        // the numerator's distance from the CLOSEST multiple
        // of the LCD
        if (leastCommonMultiple - tempDifference < tempDifference) {
            tempDifference = leastCommonMultiple - tempDifference;
        }

        // if this is as close as we've come, keep track of how close
        // that is, and the line number of the rule that did it.  If
        // we've scored a direct hit, we don't have to look at any more
        // rules
        if (tempDifference < difference) {
            difference = tempDifference;
            winner = i;
            if (difference == 0) {
                break;
            }
        }
    }

    // if we have two successive rules that both have the winning base
    // value, then the first one (the one we found above) is used if
    // the numerator of the fraction is 1 and the second one is used if
    // the numerator of the fraction is anything else (this lets us
    // do things like "one third"/"two thirds" without having to define
    // a whole bunch of extra rule sets)
    if (static_cast<unsigned>(winner + 1) < rules.size() &&
        rules[winner + 1]->getBaseValue() == rules[winner]->getBaseValue()) {
        double n = static_cast<double>(rules[winner]->getBaseValue()) * number;
        if (n < 0.5 || n >= 2) {
            ++winner;
        }
    }

    // finally, return the winning rule
    return rules[winner];
}

/**
 * Parses a string.  Matches the string to be parsed against each
 * of its rules (with a base value less than upperBound) and returns
 * the value produced by the rule that matched the most characters
 * in the source string.
 * @param text The string to parse
 * @param parsePosition The initial position is ignored and assumed
 * to be 0.  On exit, this object has been updated to point to the
 * first character position this rule set didn't consume.
 * @param upperBound Limits the rules that can be allowed to match.
 * Only rules whose base values are strictly less than upperBound
 * are considered.
 * @return The numerical result of parsing this string.  This will
 * be the matching rule's base value, composed appropriately with
 * the results of matching any of its substitutions.  The object
 * will be an instance of Long if it's an integral value; otherwise,
 * it will be an instance of Double.  This function always returns
 * a valid object: If nothing matched the input string at all,
 * this function returns new Long(0), and the parse position is
 * left unchanged.
 */
#ifdef RBNF_DEBUG
#include <stdio.h>

static void dumpUS(FILE* f, const UnicodeString& us) {
  int len = us.length();
  char* buf = (char *)uprv_malloc((len+1)*sizeof(char)); //new char[len+1];
  if (buf != nullptr) {
	  us.extract(0, len, buf);
	  buf[len] = 0;
	  fprintf(f, "%s", buf);
	  uprv_free(buf); //delete[] buf;
  }
}
#endif

UBool
NFRuleSet::parse(const UnicodeString& text, ParsePosition& pos, double upperBound, uint32_t nonNumericalExecutedRuleMask, int32_t recursionCount, Formattable& result) const
{
    // try matching each rule in the rule set against the text being
    // parsed.  Whichever one matches the most characters is the one
    // that determines the value we return.

    result.setLong(0);

    // dump out if we've reached the recursion limit
    if (recursionCount >= RECURSION_LIMIT) {
        // stop recursion
        return false;
    }

    // dump out if there's no text to parse
    if (text.length() == 0) {
        return 0;
    }

    ParsePosition highWaterMark;
    ParsePosition workingPos = pos;

#ifdef RBNF_DEBUG
    fprintf(stderr, "<nfrs> %x '", this);
    dumpUS(stderr, name);
    fprintf(stderr, "' text '");
    dumpUS(stderr, text);
    fprintf(stderr, "'\n");
    fprintf(stderr, "  parse negative: %d\n", this, negativeNumberRule != 0);
#endif
    // Try each of the negative rules, fraction rules, infinity rules and NaN rules
    for (int i = 0; i < NON_NUMERICAL_RULE_LENGTH; i++) {
        if (nonNumericalRules[i] && ((nonNumericalExecutedRuleMask >> i) & 1) == 0) {
            // Mark this rule as being executed so that we don't try to execute it again.
            nonNumericalExecutedRuleMask |= 1 << i;

            Formattable tempResult;
            UBool success = nonNumericalRules[i]->doParse(text, workingPos, 0, upperBound, nonNumericalExecutedRuleMask, recursionCount + 1, tempResult);
            if (success && (workingPos.getIndex() > highWaterMark.getIndex())) {
                result = tempResult;
                highWaterMark = workingPos;
            }
            workingPos = pos;
        }
    }
#ifdef RBNF_DEBUG
    fprintf(stderr, "<nfrs> continue other with text '");
    dumpUS(stderr, text);
    fprintf(stderr, "' hwm: %d\n", highWaterMark.getIndex());
#endif

    // finally, go through the regular rules one at a time.  We start
    // at the end of the list because we want to try matching the most
    // sigificant rule first (this helps ensure that we parse
    // "five thousand three hundred six" as
    // "(five thousand) (three hundred) (six)" rather than
    // "((five thousand three) hundred) (six)").  Skip rules whose
    // base values are higher than the upper bound (again, this helps
    // limit ambiguity by making sure the rules that match a rule's
    // are less significant than the rule containing the substitutions)/
    {
        int64_t ub = util64_fromDouble(upperBound);
#ifdef RBNF_DEBUG
        {
            char ubstr[64];
            util64_toa(ub, ubstr, 64);
            char ubstrhex[64];
            util64_toa(ub, ubstrhex, 64, 16);
            fprintf(stderr, "ub: %g, i64: %s (%s)\n", upperBound, ubstr, ubstrhex);
        }
#endif
        for (int32_t i = rules.size(); --i >= 0 && highWaterMark.getIndex() < text.length();) {
            if ((!fIsFractionRuleSet) && (rules[i]->getBaseValue() >= ub)) {
                continue;
            }
            Formattable tempResult;
            UBool success = rules[i]->doParse(text, workingPos, fIsFractionRuleSet, upperBound, nonNumericalExecutedRuleMask, recursionCount + 1, tempResult);
            if (success && workingPos.getIndex() > highWaterMark.getIndex()) {
                result = tempResult;
                highWaterMark = workingPos;
            }
            workingPos = pos;
        }
    }
#ifdef RBNF_DEBUG
    fprintf(stderr, "<nfrs> exit\n");
#endif
    // finally, update the parse position we were passed to point to the
    // first character we didn't use, and return the result that
    // corresponds to that string of characters
    pos = highWaterMark;

    return 1;
}

void
NFRuleSet::appendRules(UnicodeString& result) const
{
    uint32_t i;

    // the rule set name goes first...
    result.append(name);
    result.append(gColon);
    result.append(gLineFeed);

    // followed by the regular rules...
    for (i = 0; i < rules.size(); i++) {
        rules[i]->_appendRuleText(result);
        result.append(gLineFeed);
    }

    // followed by the special rules (if they exist)
    for (i = 0; i < NON_NUMERICAL_RULE_LENGTH; ++i) {
        NFRule *rule = nonNumericalRules[i];
        if (nonNumericalRules[i]) {
            if (rule->getBaseValue() == NFRule::kImproperFractionRule
                || rule->getBaseValue() == NFRule::kProperFractionRule
                || rule->getBaseValue() == NFRule::kDefaultRule)
            {
                for (uint32_t fIdx = 0; fIdx < fractionRules.size(); fIdx++) {
                    NFRule *fractionRule = fractionRules[fIdx];
                    if (fractionRule->getBaseValue() == rule->getBaseValue()) {
                        fractionRule->_appendRuleText(result);
                        result.append(gLineFeed);
                    }
                }
            }
            else {
                rule->_appendRuleText(result);
                result.append(gLineFeed);
            }
        }
    }
}

// utility functions

int64_t util64_fromDouble(double d) {
    int64_t result = 0;
    if (!uprv_isNaN(d)) {
        double mant = uprv_maxMantissa();
        if (d < -mant) {
            d = -mant;
        } else if (d > mant) {
            d = mant;
        }
        UBool neg = d < 0; 
        if (neg) {
            d = -d;
        }
        result = static_cast<int64_t>(uprv_floor(d));
        if (neg) {
            result = -result;
        }
    }
    return result;
}

uint64_t util64_pow(uint32_t base, uint16_t exponent)  {
    if (base == 0) {
        return 0;
    }
    uint64_t result = 1;
    uint64_t pow = base;
    while (true) {
        if ((exponent & 1) == 1) {
            result *= pow;
        }
        exponent >>= 1;
        if (exponent == 0) {
            break;
        }
        pow *= pow;
    }
    return result;
}

static const uint8_t asciiDigits[] = { 
    0x30u, 0x31u, 0x32u, 0x33u, 0x34u, 0x35u, 0x36u, 0x37u,
    0x38u, 0x39u, 0x61u, 0x62u, 0x63u, 0x64u, 0x65u, 0x66u,
    0x67u, 0x68u, 0x69u, 0x6au, 0x6bu, 0x6cu, 0x6du, 0x6eu,
    0x6fu, 0x70u, 0x71u, 0x72u, 0x73u, 0x74u, 0x75u, 0x76u,
    0x77u, 0x78u, 0x79u, 0x7au,  
};

static const char16_t kUMinus = static_cast<char16_t>(0x002d);

#ifdef RBNF_DEBUG
static const char kMinus = '-';

static const uint8_t digitInfo[] = {
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
    0x80u, 0x81u, 0x82u, 0x83u, 0x84u, 0x85u, 0x86u, 0x87u,
    0x88u, 0x89u,     0,     0,     0,     0,     0,     0,
        0, 0x8au, 0x8bu, 0x8cu, 0x8du, 0x8eu, 0x8fu, 0x90u,
    0x91u, 0x92u, 0x93u, 0x94u, 0x95u, 0x96u, 0x97u, 0x98u,
    0x99u, 0x9au, 0x9bu, 0x9cu, 0x9du, 0x9eu, 0x9fu, 0xa0u,
    0xa1u, 0xa2u, 0xa3u,     0,     0,     0,     0,     0,
        0, 0x8au, 0x8bu, 0x8cu, 0x8du, 0x8eu, 0x8fu, 0x90u,
    0x91u, 0x92u, 0x93u, 0x94u, 0x95u, 0x96u, 0x97u, 0x98u,
    0x99u, 0x9au, 0x9bu, 0x9cu, 0x9du, 0x9eu, 0x9fu, 0xa0u,
    0xa1u, 0xa2u, 0xa3u,     0,     0,     0,     0,     0,
};

int64_t util64_atoi(const char* str, uint32_t radix)
{
    if (radix > 36) {
        radix = 36;
    } else if (radix < 2) {
        radix = 2;
    }
    int64_t lradix = radix;

    int neg = 0;
    if (*str == kMinus) {
        ++str;
        neg = 1;
    }
    int64_t result = 0;
    uint8_t b;
    while ((b = digitInfo[*str++]) && ((b &= 0x7f) < radix)) {
        result *= lradix;
        result += (int32_t)b;
    }
    if (neg) {
        result = -result;
    }
    return result;
}

int64_t util64_utoi(const char16_t* str, uint32_t radix)
{
    if (radix > 36) {
        radix = 36;
    } else if (radix < 2) {
        radix = 2;
    }
    int64_t lradix = radix;

    int neg = 0;
    if (*str == kUMinus) {
        ++str;
        neg = 1;
    }
    int64_t result = 0;
    char16_t c;
    uint8_t b;
    while (((c = *str++) < 0x0080) && (b = digitInfo[c]) && ((b &= 0x7f) < radix)) {
        result *= lradix;
        result += (int32_t)b;
    }
    if (neg) {
        result = -result;
    }
    return result;
}

uint32_t util64_toa(int64_t w, char* buf, uint32_t len, uint32_t radix, UBool raw)
{    
    if (radix > 36) {
        radix = 36;
    } else if (radix < 2) {
        radix = 2;
    }
    int64_t base = radix;

    char* p = buf;
    if (len && (w < 0) && (radix == 10) && !raw) {
        w = -w;
        *p++ = kMinus;
        --len;
    } else if (len && (w == 0)) {
        *p++ = (char)raw ? 0 : asciiDigits[0];
        --len;
    }

    while (len && w != 0) {
        int64_t n = w / base;
        int64_t m = n * base;
        int32_t d = (int32_t)(w-m);
        *p++ = raw ? (char)d : asciiDigits[d];
        w = n;
        --len;
    }
    if (len) {
        *p = 0; // null terminate if room for caller convenience
    }

    len = p - buf;
    if (*buf == kMinus) {
        ++buf;
    }
    while (--p > buf) {
        char c = *p;
        *p = *buf;
        *buf = c;
        ++buf;
    }

    return len;
}
#endif

uint32_t util64_tou(int64_t w, char16_t* buf, uint32_t len, uint32_t radix, UBool raw)
{    
    if (radix > 36) {
        radix = 36;
    } else if (radix < 2) {
        radix = 2;
    }
    int64_t base = radix;

    char16_t* p = buf;
    if (len && (w < 0) && (radix == 10) && !raw) {
        w = -w;
        *p++ = kUMinus;
        --len;
    } else if (len && (w == 0)) {
        *p++ = static_cast<char16_t>(raw) ? 0 : asciiDigits[0];
        --len;
    }

    while (len && (w != 0)) {
        int64_t n = w / base;
        int64_t m = n * base;
        int32_t d = static_cast<int32_t>(w - m);
        *p++ = static_cast<char16_t>(raw ? d : asciiDigits[d]);
        w = n;
        --len;
    }
    if (len) {
        *p = 0; // null terminate if room for caller convenience
    }

    len = static_cast<uint32_t>(p - buf);
    if (*buf == kUMinus) {
        ++buf;
    }
    while (--p > buf) {
        char16_t c = *p;
        *p = *buf;
        *buf = c;
        ++buf;
    }

    return len;
}


U_NAMESPACE_END

/* U_HAVE_RBNF */
#endif
