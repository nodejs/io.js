// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2007-2016, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*
* File plurrule.cpp
*/

#include <math.h>
#include <stdio.h>

#include <utility>

#include "unicode/utypes.h"
#include "unicode/localpointer.h"
#include "unicode/plurrule.h"
#include "unicode/upluralrules.h"
#include "unicode/ures.h"
#include "unicode/numfmt.h"
#include "unicode/decimfmt.h"
#include "unicode/numberrangeformatter.h"
#include "charstr.h"
#include "cmemory.h"
#include "cstring.h"
#include "hash.h"
#include "locutil.h"
#include "mutex.h"
#include "number_decnum.h"
#include "patternprops.h"
#include "plurrule_impl.h"
#include "putilimp.h"
#include "ucln_in.h"
#include "ustrfmt.h"
#include "uassert.h"
#include "uvectr32.h"
#include "sharedpluralrules.h"
#include "unifiedcache.h"
#include "number_decimalquantity.h"
#include "util.h"
#include "pluralranges.h"
#include "numrange_impl.h"
#include "ulocimp.h"

#if !UCONFIG_NO_FORMATTING

U_NAMESPACE_BEGIN

using namespace icu::pluralimpl;
using icu::number::impl::DecNum;
using icu::number::impl::DecimalQuantity;
using icu::number::impl::RoundingMode;

static const char16_t PLURAL_KEYWORD_OTHER[]={LOW_O,LOW_T,LOW_H,LOW_E,LOW_R,0};
static const char16_t PLURAL_DEFAULT_RULE[]={LOW_O,LOW_T,LOW_H,LOW_E,LOW_R,COLON,SPACE,LOW_N,0};
static const char16_t PK_IN[]={LOW_I,LOW_N,0};
static const char16_t PK_NOT[]={LOW_N,LOW_O,LOW_T,0};
static const char16_t PK_IS[]={LOW_I,LOW_S,0};
static const char16_t PK_MOD[]={LOW_M,LOW_O,LOW_D,0};
static const char16_t PK_AND[]={LOW_A,LOW_N,LOW_D,0};
static const char16_t PK_OR[]={LOW_O,LOW_R,0};
static const char16_t PK_VAR_N[]={LOW_N,0};
static const char16_t PK_VAR_I[]={LOW_I,0};
static const char16_t PK_VAR_F[]={LOW_F,0};
static const char16_t PK_VAR_T[]={LOW_T,0};
static const char16_t PK_VAR_E[]={LOW_E,0};
static const char16_t PK_VAR_C[]={LOW_C,0};
static const char16_t PK_VAR_V[]={LOW_V,0};
static const char16_t PK_WITHIN[]={LOW_W,LOW_I,LOW_T,LOW_H,LOW_I,LOW_N,0};
static const char16_t PK_DECIMAL[]={LOW_D,LOW_E,LOW_C,LOW_I,LOW_M,LOW_A,LOW_L,0};
static const char16_t PK_INTEGER[]={LOW_I,LOW_N,LOW_T,LOW_E,LOW_G,LOW_E,LOW_R,0};

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(PluralRules)
UOBJECT_DEFINE_RTTI_IMPLEMENTATION(PluralKeywordEnumeration)

PluralRules::PluralRules(UErrorCode& /*status*/)
:   UObject(),
    mRules(nullptr),
    mStandardPluralRanges(nullptr),
    mInternalStatus(U_ZERO_ERROR)
{
}

PluralRules::PluralRules(const PluralRules& other)
: UObject(other),
    mRules(nullptr),
    mStandardPluralRanges(nullptr),
    mInternalStatus(U_ZERO_ERROR)
{
    *this=other;
}

PluralRules::~PluralRules() {
    delete mRules;
    delete mStandardPluralRanges;
}

SharedPluralRules::~SharedPluralRules() {
    delete ptr;
}

PluralRules*
PluralRules::clone() const {
    // Since clone doesn't have a 'status' parameter, the best we can do is return nullptr if
    // the newly created object was not fully constructed properly (an error occurred).
    UErrorCode localStatus = U_ZERO_ERROR;
    return clone(localStatus);
}

PluralRules*
PluralRules::clone(UErrorCode& status) const {
    LocalPointer<PluralRules> newObj(new PluralRules(*this), status);
    if (U_SUCCESS(status) && U_FAILURE(newObj->mInternalStatus)) {
        status = newObj->mInternalStatus;
        newObj.adoptInstead(nullptr);
    }
    return newObj.orphan();
}

PluralRules&
PluralRules::operator=(const PluralRules& other) {
    if (this != &other) {
        delete mRules;
        mRules = nullptr;
        delete mStandardPluralRanges;
        mStandardPluralRanges = nullptr;
        mInternalStatus = other.mInternalStatus;
        if (U_FAILURE(mInternalStatus)) {
            // bail out early if the object we were copying from was already 'invalid'.
            return *this;
        }
        if (other.mRules != nullptr) {
            mRules = new RuleChain(*other.mRules);
            if (mRules == nullptr) {
                mInternalStatus = U_MEMORY_ALLOCATION_ERROR;
            }
            else if (U_FAILURE(mRules->fInternalStatus)) {
                // If the RuleChain wasn't fully copied, then set our status to failure as well.
                mInternalStatus = mRules->fInternalStatus;
            }
        }
        if (other.mStandardPluralRanges != nullptr) {
            mStandardPluralRanges = other.mStandardPluralRanges->copy(mInternalStatus)
                .toPointer(mInternalStatus)
                .orphan();
        }
    }
    return *this;
}

StringEnumeration* PluralRules::getAvailableLocales(UErrorCode &status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    LocalPointer<StringEnumeration> result(new PluralAvailableLocalesEnumeration(status), status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    return result.orphan();
}


PluralRules* U_EXPORT2
PluralRules::createRules(const UnicodeString& description, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    PluralRuleParser parser;
    LocalPointer<PluralRules> newRules(new PluralRules(status), status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    parser.parse(description, newRules.getAlias(), status);
    if (U_FAILURE(status)) {
        newRules.adoptInstead(nullptr);
    }
    return newRules.orphan();
}


PluralRules* U_EXPORT2
PluralRules::createDefaultRules(UErrorCode& status) {
    return createRules(UnicodeString(true, PLURAL_DEFAULT_RULE, -1), status);
}

/******************************************************************************/
/* Create PluralRules cache */

template<> U_I18N_API
const SharedPluralRules *LocaleCacheKey<SharedPluralRules>::createObject(
        const void * /*unused*/, UErrorCode &status) const {
    const char *localeId = fLoc.getName();
    LocalPointer<PluralRules> pr(PluralRules::internalForLocale(localeId, UPLURAL_TYPE_CARDINAL, status), status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    LocalPointer<SharedPluralRules> result(new SharedPluralRules(pr.getAlias()), status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    pr.orphan(); // result was successfully created so it nows pr.
    result->addRef();
    return result.orphan();
}

/* end plural rules cache */
/******************************************************************************/

const SharedPluralRules* U_EXPORT2
PluralRules::createSharedInstance(
        const Locale& locale, UPluralType type, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    if (type != UPLURAL_TYPE_CARDINAL) {
        status = U_UNSUPPORTED_ERROR;
        return nullptr;
    }
    const SharedPluralRules *result = nullptr;
    UnifiedCache::getByLocale(locale, result, status);
    return result;
}

PluralRules* U_EXPORT2
PluralRules::forLocale(const Locale& locale, UErrorCode& status) {
    return forLocale(locale, UPLURAL_TYPE_CARDINAL, status);
}

PluralRules* U_EXPORT2
PluralRules::forLocale(const Locale& locale, UPluralType type, UErrorCode& status) {
    if (type != UPLURAL_TYPE_CARDINAL) {
        return internalForLocale(locale, type, status);
    }
    const SharedPluralRules *shared = createSharedInstance(
            locale, type, status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    PluralRules *result = (*shared)->clone(status);
    shared->removeRef();
    return result;
}

PluralRules* U_EXPORT2
PluralRules::internalForLocale(const Locale& locale, UPluralType type, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    if (type >= UPLURAL_TYPE_COUNT) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }
    LocalPointer<PluralRules> newObj(new PluralRules(status), status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    UnicodeString locRule = newObj->getRuleFromResource(locale, type, status);
    // TODO: which other errors, if any, should be returned?
    if (locRule.length() == 0) {
        // If an out-of-memory error occurred, then stop and report the failure.
        if (status == U_MEMORY_ALLOCATION_ERROR) {
            return nullptr;
        }
        // Locales with no specific rules (all numbers have the "other" category
        //   will return a U_MISSING_RESOURCE_ERROR at this point. This is not
        //   an error.
        locRule =  UnicodeString(PLURAL_DEFAULT_RULE);
        status = U_ZERO_ERROR;
    }
    PluralRuleParser parser;
    parser.parse(locRule, newObj.getAlias(), status);
        //  TODO: should rule parse errors be returned, or
        //        should we silently use default rules?
        //        Original impl used default rules.
        //        Ask the question to ICU Core.

    newObj->mStandardPluralRanges = StandardPluralRanges::forLocale(locale, status)
        .toPointer(status)
        .orphan();

    return newObj.orphan();
}

UnicodeString
PluralRules::select(int32_t number) const {
    return select(FixedDecimal(number));
}

UnicodeString
PluralRules::select(double number) const {
    return select(FixedDecimal(number));
}

UnicodeString
PluralRules::select(const number::FormattedNumber& number, UErrorCode& status) const {
    DecimalQuantity dq;
    number.getDecimalQuantity(dq, status);
    if (U_FAILURE(status)) {
        return ICU_Utility::makeBogusString();
    }
    if (U_FAILURE(mInternalStatus)) {
        status = mInternalStatus;
        return ICU_Utility::makeBogusString();
    }
    return select(dq);
}

UnicodeString
PluralRules::select(const IFixedDecimal &number) const {
    if (mRules == nullptr) {
        return UnicodeString(true, PLURAL_DEFAULT_RULE, -1);
    }
    else {
        return mRules->select(number);
    }
}

UnicodeString
PluralRules::select(const number::FormattedNumberRange& range, UErrorCode& status) const {
    return select(range.getData(status), status);
}

UnicodeString
PluralRules::select(const number::impl::UFormattedNumberRangeData* impl, UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return ICU_Utility::makeBogusString();
    }
    if (U_FAILURE(mInternalStatus)) {
        status = mInternalStatus;
        return ICU_Utility::makeBogusString();
    }
    if (mStandardPluralRanges == nullptr) {
        // Happens if PluralRules was constructed via createRules()
        status = U_UNSUPPORTED_ERROR;
        return ICU_Utility::makeBogusString();
    }
    auto form1 = StandardPlural::fromString(select(impl->quantity1), status);
    auto form2 = StandardPlural::fromString(select(impl->quantity2), status);
    if (U_FAILURE(status)) {
        return ICU_Utility::makeBogusString();
    }
    auto result = mStandardPluralRanges->resolve(form1, form2);
    return UnicodeString(StandardPlural::getKeyword(result), -1, US_INV);
}


StringEnumeration*
PluralRules::getKeywords(UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    if (U_FAILURE(mInternalStatus)) {
        status = mInternalStatus;
        return nullptr;
    }
    LocalPointer<StringEnumeration> nameEnumerator(new PluralKeywordEnumeration(mRules, status), status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    return nameEnumerator.orphan();
}

double
PluralRules::getUniqueKeywordValue(const UnicodeString& /* keyword */) {
  // Not Implemented.
  return UPLRULES_NO_UNIQUE_VALUE;
}

int32_t
PluralRules::getAllKeywordValues(const UnicodeString & /* keyword */, double * /* dest */,
                                 int32_t /* destCapacity */, UErrorCode& error) {
    error = U_UNSUPPORTED_ERROR;
    return 0;
}

/**
 * Helper method for the overrides of getSamples() for double and DecimalQuantity
 * return value types.  Provide only one of an allocated array of double or
 * DecimalQuantity, and a nullptr for the other.
 */
static int32_t
getSamplesFromString(const UnicodeString &samples, double *destDbl,
                        DecimalQuantity* destDq, int32_t destCapacity,
                        UErrorCode& status) {

    if ((destDbl == nullptr && destDq == nullptr)
            || (destDbl != nullptr && destDq != nullptr)) {
        status = U_INTERNAL_PROGRAM_ERROR;
        return 0;
    }

    bool isDouble = destDbl != nullptr;
    int32_t sampleCount = 0;
    int32_t sampleStartIdx = 0;
    int32_t sampleEndIdx = 0;

    //std::string ss;  // TODO: debugging.
    // std::cout << "PluralRules::getSamples(), samples = \"" << samples.toUTF8String(ss) << "\"\n";
    for (sampleCount = 0; sampleCount < destCapacity && sampleStartIdx < samples.length(); ) {
        sampleEndIdx = samples.indexOf(COMMA, sampleStartIdx);
        if (sampleEndIdx == -1) {
            sampleEndIdx = samples.length();
        }
        const UnicodeString &sampleRange = samples.tempSubStringBetween(sampleStartIdx, sampleEndIdx);
        // ss.erase();
        // std::cout << "PluralRules::getSamples(), samplesRange = \"" << sampleRange.toUTF8String(ss) << "\"\n";
        int32_t tildeIndex = sampleRange.indexOf(TILDE);
        if (tildeIndex < 0) {
            DecimalQuantity dq = DecimalQuantity::fromExponentString(sampleRange, status);
            if (isDouble) {
                // See warning note below about lack of precision for floating point samples for numbers with
                // trailing zeroes in the decimal fraction representation.
                double dblValue = dq.toDouble();
                if (!(dblValue == floor(dblValue) && dq.fractionCount() > 0)) {
                    destDbl[sampleCount++] = dblValue;
                }
            } else {
                destDq[sampleCount++] = dq;
            }
        } else {
            DecimalQuantity rangeLo =
                DecimalQuantity::fromExponentString(sampleRange.tempSubStringBetween(0, tildeIndex), status);
            DecimalQuantity rangeHi = DecimalQuantity::fromExponentString(sampleRange.tempSubStringBetween(tildeIndex+1), status);
            if (U_FAILURE(status)) {
                break;
            }
            if (rangeHi.toDouble() < rangeLo.toDouble()) {
                status = U_INVALID_FORMAT_ERROR;
                break;
            }

            DecimalQuantity incrementDq;
            incrementDq.setToInt(1);
            int32_t lowerDispMag = rangeLo.getLowerDisplayMagnitude();
            int32_t exponent = rangeLo.getExponent();
            int32_t incrementScale = lowerDispMag + exponent;
            incrementDq.adjustMagnitude(incrementScale);
            double incrementVal = incrementDq.toDouble();  // 10 ^ incrementScale
            

            DecimalQuantity dq(rangeLo);
            double dblValue = dq.toDouble();
            double end = rangeHi.toDouble();

            while (dblValue <= end) {
                if (isDouble) {
                    // Hack Alert: don't return any decimal samples with integer values that
                    //    originated from a format with trailing decimals.
                    //    This API is returning doubles, which can't distinguish having displayed
                    //    zeros to the right of the decimal.
                    //    This results in test failures with values mapping back to a different keyword.
                    if (!(dblValue == floor(dblValue) && dq.fractionCount() > 0)) {
                        destDbl[sampleCount++] = dblValue;
                    }
                } else {
                    destDq[sampleCount++] = dq;
                }
                if (sampleCount >= destCapacity) {
                    break;
                }

                // Increment dq for next iteration

                // Because DecNum and DecimalQuantity do not support
                // add operations, we need to convert to/from double,
                // despite precision lossiness for decimal fractions like 0.1.
                dblValue += incrementVal;
                DecNum newDqDecNum;
                newDqDecNum.setTo(dblValue, status);
                DecimalQuantity newDq;             
                newDq.setToDecNum(newDqDecNum, status);
                newDq.setMinFraction(-lowerDispMag);
                newDq.roundToMagnitude(lowerDispMag, RoundingMode::UNUM_ROUND_HALFEVEN, status);
                newDq.adjustMagnitude(-exponent);
                newDq.adjustExponent(exponent);
                dblValue = newDq.toDouble();
                dq = newDq;
            }
        }
        sampleStartIdx = sampleEndIdx + 1;
    }
    return sampleCount;
}

int32_t
PluralRules::getSamples(const UnicodeString &keyword, double *dest,
                        int32_t destCapacity, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return 0;
    }
    if (U_FAILURE(mInternalStatus)) {
        status = mInternalStatus;
        return 0;
    }
    if (dest != nullptr ? destCapacity < 0 : destCapacity != 0) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    RuleChain *rc = rulesForKeyword(keyword);
    if (rc == nullptr) {
        return 0;
    }
    int32_t numSamples = getSamplesFromString(rc->fIntegerSamples, dest, nullptr, destCapacity, status);
    if (numSamples == 0) {
        numSamples = getSamplesFromString(rc->fDecimalSamples, dest, nullptr, destCapacity, status);
    }
    return numSamples;
}

int32_t
PluralRules::getSamples(const UnicodeString &keyword, DecimalQuantity *dest,
                        int32_t destCapacity, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return 0;
    }
    if (U_FAILURE(mInternalStatus)) {
        status = mInternalStatus;
        return 0;
    }
    if (dest != nullptr ? destCapacity < 0 : destCapacity != 0) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    RuleChain *rc = rulesForKeyword(keyword);
    if (rc == nullptr) {
        return 0;
    }

    int32_t numSamples = getSamplesFromString(rc->fIntegerSamples, nullptr, dest, destCapacity, status);
    if (numSamples == 0) {
        numSamples = getSamplesFromString(rc->fDecimalSamples, nullptr, dest, destCapacity, status);
    }
    return numSamples;
}


RuleChain *PluralRules::rulesForKeyword(const UnicodeString &keyword) const {
    RuleChain *rc;
    for (rc = mRules; rc != nullptr; rc = rc->fNext) {
        if (rc->fKeyword == keyword) {
            break;
        }
    }
    return rc;
}


UBool
PluralRules::isKeyword(const UnicodeString& keyword) const {
    if (0 == keyword.compare(PLURAL_KEYWORD_OTHER, 5)) {
        return true;
    }
    return rulesForKeyword(keyword) != nullptr;
}

UnicodeString
PluralRules::getKeywordOther() const {
    return UnicodeString(true, PLURAL_KEYWORD_OTHER, 5);
}

bool
PluralRules::operator==(const PluralRules& other) const  {
    const UnicodeString *ptrKeyword;
    UErrorCode status= U_ZERO_ERROR;

    if ( this == &other ) {
        return true;
    }
    LocalPointer<StringEnumeration> myKeywordList(getKeywords(status));
    LocalPointer<StringEnumeration> otherKeywordList(other.getKeywords(status));
    if (U_FAILURE(status)) {
        return false;
    }

    if (myKeywordList->count(status)!=otherKeywordList->count(status)) {
        return false;
    }
    myKeywordList->reset(status);
    while ((ptrKeyword=myKeywordList->snext(status))!=nullptr) {
        if (!other.isKeyword(*ptrKeyword)) {
            return false;
        }
    }
    otherKeywordList->reset(status);
    while ((ptrKeyword=otherKeywordList->snext(status))!=nullptr) {
        if (!this->isKeyword(*ptrKeyword)) {
            return false;
        }
    }
    if (U_FAILURE(status)) {
        return false;
    }

    return true;
}


void
PluralRuleParser::parse(const UnicodeString& ruleData, PluralRules *prules, UErrorCode &status)
{
    if (U_FAILURE(status)) {
        return;
    }
    U_ASSERT(ruleIndex == 0);    // Parsers are good for a single use only!
    ruleSrc = &ruleData;

    while (ruleIndex< ruleSrc->length()) {
        getNextToken(status);
        if (U_FAILURE(status)) {
            return;
        }
        checkSyntax(status);
        if (U_FAILURE(status)) {
            return;
        }
        switch (type) {
        case tAnd:
            U_ASSERT(curAndConstraint != nullptr);
            curAndConstraint = curAndConstraint->add(status);
            break;
        case tOr:
            {
                U_ASSERT(currentChain != nullptr);
                OrConstraint *orNode=currentChain->ruleHeader;
                while (orNode->next != nullptr) {
                    orNode = orNode->next;
                }
                orNode->next= new OrConstraint();
                if (orNode->next == nullptr) {
                    status = U_MEMORY_ALLOCATION_ERROR;
                    break;
                }
                orNode=orNode->next;
                orNode->next=nullptr;
                curAndConstraint = orNode->add(status);
            }
            break;
        case tIs:
            U_ASSERT(curAndConstraint != nullptr);
            U_ASSERT(curAndConstraint->value == -1);
            U_ASSERT(curAndConstraint->rangeList == nullptr);
            break;
        case tNot:
            U_ASSERT(curAndConstraint != nullptr);
            curAndConstraint->negated=true;
            break;

        case tNotEqual:
            curAndConstraint->negated=true;
            U_FALLTHROUGH;
        case tIn:
        case tWithin:
        case tEqual:
            {
                U_ASSERT(curAndConstraint != nullptr);
                if (curAndConstraint->rangeList != nullptr) {
                    // Already get a '='.
                    status = U_UNEXPECTED_TOKEN;
                    break;
                }
                LocalPointer<UVector32> newRangeList(new UVector32(status), status);
                if (U_FAILURE(status)) {
                    break;
                }
                curAndConstraint->rangeList = newRangeList.orphan();
                curAndConstraint->rangeList->addElement(-1, status);  // range Low
                curAndConstraint->rangeList->addElement(-1, status);  // range Hi
                rangeLowIdx = 0;
                rangeHiIdx  = 1;
                curAndConstraint->value=PLURAL_RANGE_HIGH;
                curAndConstraint->integerOnly = (type != tWithin);
            }
            break;
        case tNumber:
            U_ASSERT(curAndConstraint != nullptr);
            if ( (curAndConstraint->op==AndConstraint::MOD)&&
                 (curAndConstraint->opNum == -1 ) ) {
                int32_t num = getNumberValue(token);
                if (num == -1) {
                    status = U_UNEXPECTED_TOKEN;
                    break;
                }
                curAndConstraint->opNum=num;
            }
            else {
                if (curAndConstraint->rangeList == nullptr) {
                    // this is for an 'is' rule
                    int32_t num = getNumberValue(token);
                    if (num == -1) {
                        status = U_UNEXPECTED_TOKEN;
                        break;
                    }
                    curAndConstraint->value = num;
                } else {
                    // this is for an 'in' or 'within' rule
                    if (curAndConstraint->rangeList->elementAti(rangeLowIdx) == -1) {
                        int32_t num = getNumberValue(token);
                        if (num == -1) {
                            status = U_UNEXPECTED_TOKEN;
                            break;
                        }
                        curAndConstraint->rangeList->setElementAt(num, rangeLowIdx);
                        curAndConstraint->rangeList->setElementAt(num, rangeHiIdx);
                    }
                    else {
                        int32_t num = getNumberValue(token);
                        if (num == -1) {
                            status = U_UNEXPECTED_TOKEN;
                            break;
                        }
                        curAndConstraint->rangeList->setElementAt(num, rangeHiIdx);
                        if (curAndConstraint->rangeList->elementAti(rangeLowIdx) >
                                curAndConstraint->rangeList->elementAti(rangeHiIdx)) {
                            // Range Lower bound > Range Upper bound.
                            // U_UNEXPECTED_TOKEN seems a little funny, but it is consistently
                            // used for all plural rule parse errors.
                            status = U_UNEXPECTED_TOKEN;
                            break;
                        }
                    }
                }
            }
            break;
        case tComma:
            // TODO: rule syntax checking is inadequate, can happen with badly formed rules.
            //       Catch cases like "n mod 10, is 1" here instead.
            if (curAndConstraint == nullptr || curAndConstraint->rangeList == nullptr) {
                status = U_UNEXPECTED_TOKEN;
                break;
            }
            U_ASSERT(curAndConstraint->rangeList->size() >= 2);
            rangeLowIdx = curAndConstraint->rangeList->size();
            curAndConstraint->rangeList->addElement(-1, status);  // range Low
            rangeHiIdx = curAndConstraint->rangeList->size();
            curAndConstraint->rangeList->addElement(-1, status);  // range Hi
            break;
        case tMod:
            U_ASSERT(curAndConstraint != nullptr);
            curAndConstraint->op=AndConstraint::MOD;
            break;
        case tVariableN:
        case tVariableI:
        case tVariableF:
        case tVariableT:
        case tVariableE:
        case tVariableC:
        case tVariableV:
            U_ASSERT(curAndConstraint != nullptr);
            curAndConstraint->digitsType = type;
            break;
        case tKeyword:
            {
            RuleChain *newChain = new RuleChain;
            if (newChain == nullptr) {
                status = U_MEMORY_ALLOCATION_ERROR;
                break;
            }
            newChain->fKeyword = token;
            if (prules->mRules == nullptr) {
                prules->mRules = newChain;
            } else {
                // The new rule chain goes at the end of the linked list of rule chains,
                //   unless there is an "other" keyword & chain. "other" must remain last.
                RuleChain *insertAfter = prules->mRules;
                while (insertAfter->fNext!=nullptr &&
                       insertAfter->fNext->fKeyword.compare(PLURAL_KEYWORD_OTHER, 5) != 0 ){
                    insertAfter=insertAfter->fNext;
                }
                newChain->fNext = insertAfter->fNext;
                insertAfter->fNext = newChain;
            }
            OrConstraint *orNode = new OrConstraint();
            if (orNode == nullptr) {
                status = U_MEMORY_ALLOCATION_ERROR;
                break;
            }
            newChain->ruleHeader = orNode;
            curAndConstraint = orNode->add(status);
            currentChain = newChain;
            }
            break;

        case tInteger:
            for (;;) {
                getNextToken(status);
                if (U_FAILURE(status) || type == tSemiColon || type == tEOF || type == tAt) {
                    break;
                }
                if (type == tEllipsis) {
                    currentChain->fIntegerSamplesUnbounded = true;
                    continue;
                }
                currentChain->fIntegerSamples.append(token);
            }
            break;

        case tDecimal:
            for (;;) {
                getNextToken(status);
                if (U_FAILURE(status) || type == tSemiColon || type == tEOF || type == tAt) {
                    break;
                }
                if (type == tEllipsis) {
                    currentChain->fDecimalSamplesUnbounded = true;
                    continue;
                }
                currentChain->fDecimalSamples.append(token);
            }
            break;

        default:
            break;
        }
        prevType=type;
        if (U_FAILURE(status)) {
            break;
        }
    }
}

UnicodeString
PluralRules::getRuleFromResource(const Locale& locale, UPluralType type, UErrorCode& errCode) {
    UnicodeString emptyStr;

    if (U_FAILURE(errCode)) {
        return emptyStr;
    }
    LocalUResourceBundlePointer rb(ures_openDirect(nullptr, "plurals", &errCode));
    if(U_FAILURE(errCode)) {
        return emptyStr;
    }
    const char *typeKey;
    switch (type) {
    case UPLURAL_TYPE_CARDINAL:
        typeKey = "locales";
        break;
    case UPLURAL_TYPE_ORDINAL:
        typeKey = "locales_ordinals";
        break;
    default:
        // Must not occur: The caller should have checked for valid types.
        errCode = U_ILLEGAL_ARGUMENT_ERROR;
        return emptyStr;
    }
    LocalUResourceBundlePointer locRes(ures_getByKey(rb.getAlias(), typeKey, nullptr, &errCode));
    if(U_FAILURE(errCode)) {
        return emptyStr;
    }
    int32_t resLen=0;
    const char *curLocaleName=locale.getBaseName();
    const char16_t* s = ures_getStringByKey(locRes.getAlias(), curLocaleName, &resLen, &errCode);

    if (s == nullptr) {
        // Check parent locales.
        UErrorCode status = U_ZERO_ERROR;
        const char *curLocaleName2=locale.getBaseName();
        CharString parentLocaleName(curLocaleName2, status);

        for (;;) {
            {
                CharString tmp = ulocimp_getParent(parentLocaleName.data(), status);
                if (tmp.isEmpty()) break;
                parentLocaleName = std::move(tmp);
            }
            resLen=0;
            s = ures_getStringByKey(locRes.getAlias(), parentLocaleName.data(), &resLen, &status);
            if (s != nullptr) {
                errCode = U_ZERO_ERROR;
                break;
            }
            status = U_ZERO_ERROR;
        }
    }
    if (s==nullptr) {
        return emptyStr;
    }

    char setKey[256];
    u_UCharsToChars(s, setKey, resLen + 1);
    // printf("\n PluralRule: %s\n", setKey);

    LocalUResourceBundlePointer ruleRes(ures_getByKey(rb.getAlias(), "rules", nullptr, &errCode));
    if(U_FAILURE(errCode)) {
        return emptyStr;
    }
    LocalUResourceBundlePointer setRes(ures_getByKey(ruleRes.getAlias(), setKey, nullptr, &errCode));
    if (U_FAILURE(errCode)) {
        return emptyStr;
    }

    int32_t numberKeys = ures_getSize(setRes.getAlias());
    UnicodeString result;
    const char *key=nullptr;
    for(int32_t i=0; i<numberKeys; ++i) {   // Keys are zero, one, few, ...
        UnicodeString rules = ures_getNextUnicodeString(setRes.getAlias(), &key, &errCode);
        UnicodeString uKey(key, -1, US_INV);
        result.append(uKey);
        result.append(COLON);
        result.append(rules);
        result.append(SEMI_COLON);
    }
    return result;
}


UnicodeString
PluralRules::getRules() const {
    UnicodeString rules;
    if (mRules != nullptr) {
        mRules->dumpRules(rules);
    }
    return rules;
}

AndConstraint::AndConstraint(const AndConstraint& other) {
    this->fInternalStatus = other.fInternalStatus;
    if (U_FAILURE(fInternalStatus)) {
        return; // stop early if the object we are copying from is invalid.
    }
    this->op = other.op;
    this->opNum=other.opNum;
    this->value=other.value;
    if (other.rangeList != nullptr) {
        LocalPointer<UVector32> newRangeList(new UVector32(fInternalStatus), fInternalStatus);
        if (U_FAILURE(fInternalStatus)) {
            return;
        }
        this->rangeList = newRangeList.orphan();
        this->rangeList->assign(*other.rangeList, fInternalStatus);
    }
    this->integerOnly=other.integerOnly;
    this->negated=other.negated;
    this->digitsType = other.digitsType;
    if (other.next != nullptr) {
        this->next = new AndConstraint(*other.next);
        if (this->next == nullptr) {
            fInternalStatus = U_MEMORY_ALLOCATION_ERROR;
        }
    }
}

AndConstraint::~AndConstraint() {
    delete rangeList;
    rangeList = nullptr;
    delete next;
    next = nullptr;
}

UBool
AndConstraint::isFulfilled(const IFixedDecimal &number) {
    UBool result = true;
    if (digitsType == none) {
        // An empty AndConstraint, created by a rule with a keyword but no following expression.
        return true;
    }

    PluralOperand operand = tokenTypeToPluralOperand(digitsType);
    double n = number.getPluralOperand(operand);     // pulls n | i | v | f value for the number.
                                                     // Will always be positive.
                                                     // May be non-integer (n option only)
    do {
        if (integerOnly && n != uprv_floor(n)) {
            result = false;
            break;
        }

        if (op == MOD) {
            n = fmod(n, opNum);
        }
        if (rangeList == nullptr) {
            result = value == -1 ||    // empty rule
                     n == value;       //  'is' rule
            break;
        }
        result = false;                // 'in' or 'within' rule
        for (int32_t r=0; r<rangeList->size(); r+=2) {
            if (rangeList->elementAti(r) <= n && n <= rangeList->elementAti(r+1)) {
                result = true;
                break;
            }
        }
    } while (false);

    if (negated) {
        result = !result;
    }
    return result;
}

AndConstraint*
AndConstraint::add(UErrorCode& status) {
    if (U_FAILURE(fInternalStatus)) {
        status = fInternalStatus;
        return nullptr;
    }
    this->next = new AndConstraint();
    if (this->next == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
    }
    return this->next;
}


OrConstraint::OrConstraint(const OrConstraint& other) {
    this->fInternalStatus = other.fInternalStatus;
    if (U_FAILURE(fInternalStatus)) {
        return; // stop early if the object we are copying from is invalid.
    }
    if ( other.childNode != nullptr ) {
        this->childNode = new AndConstraint(*(other.childNode));
        if (this->childNode == nullptr) {
            fInternalStatus = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
    }
    if (other.next != nullptr ) {
        this->next = new OrConstraint(*(other.next));
        if (this->next == nullptr) {
            fInternalStatus = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
        if (U_FAILURE(this->next->fInternalStatus)) {
            this->fInternalStatus = this->next->fInternalStatus;
        }
    }
}

OrConstraint::~OrConstraint() {
    delete childNode;
    childNode = nullptr;
    delete next;
    next = nullptr;
}

AndConstraint*
OrConstraint::add(UErrorCode& status) {
    if (U_FAILURE(fInternalStatus)) {
        status = fInternalStatus;
        return nullptr;
    }
    OrConstraint *curOrConstraint=this;
    {
        while (curOrConstraint->next!=nullptr) {
            curOrConstraint = curOrConstraint->next;
        }
        U_ASSERT(curOrConstraint->childNode == nullptr);
        curOrConstraint->childNode = new AndConstraint();
        if (curOrConstraint->childNode == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
        }
    }
    return curOrConstraint->childNode;
}

UBool
OrConstraint::isFulfilled(const IFixedDecimal &number) {
    OrConstraint* orRule=this;
    UBool result=false;

    while (orRule!=nullptr && !result) {
        result=true;
        AndConstraint* andRule = orRule->childNode;
        while (andRule!=nullptr && result) {
            result = andRule->isFulfilled(number);
            andRule=andRule->next;
        }
        orRule = orRule->next;
    }

    return result;
}


RuleChain::RuleChain(const RuleChain& other) :
        fKeyword(other.fKeyword), fDecimalSamples(other.fDecimalSamples),
        fIntegerSamples(other.fIntegerSamples), fDecimalSamplesUnbounded(other.fDecimalSamplesUnbounded),
        fIntegerSamplesUnbounded(other.fIntegerSamplesUnbounded), fInternalStatus(other.fInternalStatus) {
    if (U_FAILURE(this->fInternalStatus)) {
        return; // stop early if the object we are copying from is invalid. 
    }
    if (other.ruleHeader != nullptr) {
        this->ruleHeader = new OrConstraint(*(other.ruleHeader));
        if (this->ruleHeader == nullptr) {
            this->fInternalStatus = U_MEMORY_ALLOCATION_ERROR;
        }
        else if (U_FAILURE(this->ruleHeader->fInternalStatus)) {
            // If the OrConstraint wasn't fully copied, then set our status to failure as well.
            this->fInternalStatus = this->ruleHeader->fInternalStatus;
            return; // exit early.
        }
    }
    if (other.fNext != nullptr ) {
        this->fNext = new RuleChain(*other.fNext);
        if (this->fNext == nullptr) {
            this->fInternalStatus = U_MEMORY_ALLOCATION_ERROR;
        }
        else if (U_FAILURE(this->fNext->fInternalStatus)) {
            // If the RuleChain wasn't fully copied, then set our status to failure as well.
            this->fInternalStatus = this->fNext->fInternalStatus;
        }
    }
}

RuleChain::~RuleChain() {
    delete fNext;
    delete ruleHeader;
}

UnicodeString
RuleChain::select(const IFixedDecimal &number) const {
    if (!number.isNaN() && !number.isInfinite()) {
        for (const RuleChain *rules = this; rules != nullptr; rules = rules->fNext) {
             if (rules->ruleHeader->isFulfilled(number)) {
                 return rules->fKeyword;
             }
        }
    }
    return UnicodeString(true, PLURAL_KEYWORD_OTHER, 5);
}

static UnicodeString tokenString(tokenType tok) {
    UnicodeString s;
    switch (tok) {
      case tVariableN:
        s.append(LOW_N); break;
      case tVariableI:
        s.append(LOW_I); break;
      case tVariableF:
        s.append(LOW_F); break;
      case tVariableV:
        s.append(LOW_V); break;
      case tVariableT:
        s.append(LOW_T); break;
      case tVariableE:
        s.append(LOW_E); break;
    case tVariableC:
        s.append(LOW_C); break;
      default:
        s.append(TILDE);
    }
    return s;
}

void
RuleChain::dumpRules(UnicodeString& result) {
    char16_t digitString[16];

    if ( ruleHeader != nullptr ) {
        result +=  fKeyword;
        result += COLON;
        result += SPACE;
        OrConstraint* orRule=ruleHeader;
        while ( orRule != nullptr ) {
            AndConstraint* andRule=orRule->childNode;
            while ( andRule != nullptr ) {
                if ((andRule->op==AndConstraint::NONE) &&  (andRule->rangeList==nullptr) && (andRule->value == -1)) {
                    // Empty Rules.
                } else if ( (andRule->op==AndConstraint::NONE) && (andRule->rangeList==nullptr) ) {
                    result += tokenString(andRule->digitsType);
                    result += UNICODE_STRING_SIMPLE(" is ");
                    if (andRule->negated) {
                        result += UNICODE_STRING_SIMPLE("not ");
                    }
                    uprv_itou(digitString,16, andRule->value,10,0);
                    result += UnicodeString(digitString);
                }
                else {
                    result += tokenString(andRule->digitsType);
                    result += SPACE;
                    if (andRule->op==AndConstraint::MOD) {
                        result += UNICODE_STRING_SIMPLE("mod ");
                        uprv_itou(digitString,16, andRule->opNum,10,0);
                        result += UnicodeString(digitString);
                    }
                    if (andRule->rangeList==nullptr) {
                        if (andRule->negated) {
                            result += UNICODE_STRING_SIMPLE(" is not ");
                            uprv_itou(digitString,16, andRule->value,10,0);
                            result += UnicodeString(digitString);
                        }
                        else {
                            result += UNICODE_STRING_SIMPLE(" is ");
                            uprv_itou(digitString,16, andRule->value,10,0);
                            result += UnicodeString(digitString);
                        }
                    }
                    else {
                        if (andRule->negated) {
                            if ( andRule->integerOnly ) {
                                result += UNICODE_STRING_SIMPLE(" not in ");
                            }
                            else {
                                result += UNICODE_STRING_SIMPLE(" not within ");
                            }
                        }
                        else {
                            if ( andRule->integerOnly ) {
                                result += UNICODE_STRING_SIMPLE(" in ");
                            }
                            else {
                                result += UNICODE_STRING_SIMPLE(" within ");
                            }
                        }
                        for (int32_t r=0; r<andRule->rangeList->size(); r+=2) {
                            int32_t rangeLo = andRule->rangeList->elementAti(r);
                            int32_t rangeHi = andRule->rangeList->elementAti(r+1);
                            uprv_itou(digitString,16, rangeLo, 10, 0);
                            result += UnicodeString(digitString);
                            result += UNICODE_STRING_SIMPLE("..");
                            uprv_itou(digitString,16, rangeHi, 10,0);
                            result += UnicodeString(digitString);
                            if (r+2 < andRule->rangeList->size()) {
                                result += UNICODE_STRING_SIMPLE(", ");
                            }
                        }
                    }
                }
                if ( (andRule=andRule->next) != nullptr) {
                    result += UNICODE_STRING_SIMPLE(" and ");
                }
            }
            if ( (orRule = orRule->next) != nullptr ) {
                result += UNICODE_STRING_SIMPLE(" or ");
            }
        }
    }
    if ( fNext != nullptr ) {
        result += UNICODE_STRING_SIMPLE("; ");
        fNext->dumpRules(result);
    }
}


UErrorCode
RuleChain::getKeywords(int32_t capacityOfKeywords, UnicodeString* keywords, int32_t& arraySize) const {
    if (U_FAILURE(fInternalStatus)) {
        return fInternalStatus;
    }
    if ( arraySize < capacityOfKeywords-1 ) {
        keywords[arraySize++]=fKeyword;
    }
    else {
        return U_BUFFER_OVERFLOW_ERROR;
    }

    if ( fNext != nullptr ) {
        return fNext->getKeywords(capacityOfKeywords, keywords, arraySize);
    }
    else {
        return U_ZERO_ERROR;
    }
}

UBool
RuleChain::isKeyword(const UnicodeString& keywordParam) const {
    if ( fKeyword == keywordParam ) {
        return true;
    }

    if ( fNext != nullptr ) {
        return fNext->isKeyword(keywordParam);
    }
    else {
        return false;
    }
}


PluralRuleParser::PluralRuleParser() :
        ruleIndex(0), token(), type(none), prevType(none),
        curAndConstraint(nullptr), currentChain(nullptr), rangeLowIdx(-1), rangeHiIdx(-1)
{
}

PluralRuleParser::~PluralRuleParser() {
}


int32_t
PluralRuleParser::getNumberValue(const UnicodeString& token) {
    int32_t pos = 0;
    return ICU_Utility::parseNumber(token, pos, 10);
}


void
PluralRuleParser::checkSyntax(UErrorCode &status)
{
    if (U_FAILURE(status)) {
        return;
    }
    if (!(prevType==none || prevType==tSemiColon)) {
        type = getKeyType(token, type);  // Switch token type from tKeyword if we scanned a reserved word,
                                               //   and we are not at the start of a rule, where a
                                               //   keyword is expected.
    }

    switch(prevType) {
    case none:
    case tSemiColon:
        if (type!=tKeyword && type != tEOF) {
            status = U_UNEXPECTED_TOKEN;
        }
        break;
    case tVariableN:
    case tVariableI:
    case tVariableF:
    case tVariableT:
    case tVariableE:
    case tVariableC:
    case tVariableV:
        if (type != tIs && type != tMod && type != tIn &&
            type != tNot && type != tWithin && type != tEqual && type != tNotEqual) {
            status = U_UNEXPECTED_TOKEN;
        }
        break;
    case tKeyword:
        if (type != tColon) {
            status = U_UNEXPECTED_TOKEN;
        }
        break;
    case tColon:
        if (!(type == tVariableN ||
              type == tVariableI ||
              type == tVariableF ||
              type == tVariableT ||
              type == tVariableE ||
              type == tVariableC ||
              type == tVariableV ||
              type == tAt)) {
            status = U_UNEXPECTED_TOKEN;
        }
        break;
    case tIs:
        if ( type != tNumber && type != tNot) {
            status = U_UNEXPECTED_TOKEN;
        }
        break;
    case tNot:
        if (type != tNumber && type != tIn && type != tWithin) {
            status = U_UNEXPECTED_TOKEN;
        }
        break;
    case tMod:
    case tDot2:
    case tIn:
    case tWithin:
    case tEqual:
    case tNotEqual:
        if (type != tNumber) {
            status = U_UNEXPECTED_TOKEN;
        }
        break;
    case tAnd:
    case tOr:
        if ( type != tVariableN &&
             type != tVariableI &&
             type != tVariableF &&
             type != tVariableT &&
             type != tVariableE &&
             type != tVariableC &&
             type != tVariableV) {
            status = U_UNEXPECTED_TOKEN;
        }
        break;
    case tComma:
        if (type != tNumber) {
            status = U_UNEXPECTED_TOKEN;
        }
        break;
    case tNumber:
        if (type != tDot2  && type != tSemiColon && type != tIs       && type != tNot    &&
            type != tIn    && type != tEqual     && type != tNotEqual && type != tWithin &&
            type != tAnd   && type != tOr        && type != tComma    && type != tAt     &&
            type != tEOF)
        {
            status = U_UNEXPECTED_TOKEN;
        }
        // TODO: a comma following a number that is not part of a range will be allowed.
        //       It's not the only case of this sort of thing. Parser needs a re-write.
        break;
    case tAt:
        if (type != tDecimal && type != tInteger) {
            status = U_UNEXPECTED_TOKEN;
        }
        break;
    default:
        status = U_UNEXPECTED_TOKEN;
        break;
    }
}


/*
 *  Scan the next token from the input rules.
 *     rules and returned token type are in the parser state variables.
 */
void
PluralRuleParser::getNextToken(UErrorCode &status)
{
    if (U_FAILURE(status)) {
        return;
    }

    char16_t ch;
    while (ruleIndex < ruleSrc->length()) {
        ch = ruleSrc->charAt(ruleIndex);
        type = charType(ch);
        if (type != tSpace) {
            break;
        }
        ++(ruleIndex);
    }
    if (ruleIndex >= ruleSrc->length()) {
        type = tEOF;
        return;
    }
    int32_t curIndex= ruleIndex;

    switch (type) {
      case tColon:
      case tSemiColon:
      case tComma:
      case tEllipsis:
      case tTilde:   // scanned '~'
      case tAt:      // scanned '@'
      case tEqual:   // scanned '='
      case tMod:     // scanned '%'
        // Single character tokens.
        ++curIndex;
        break;

      case tNotEqual:  // scanned '!'
        if (ruleSrc->charAt(curIndex+1) == EQUALS) {
            curIndex += 2;
        } else {
            type = none;
            curIndex += 1;
        }
        break;

      case tKeyword:
         while (type == tKeyword && ++curIndex < ruleSrc->length()) {
             ch = ruleSrc->charAt(curIndex);
             type = charType(ch);
         }
         type = tKeyword;
         break;

      case tNumber:
         while (type == tNumber && ++curIndex < ruleSrc->length()) {
             ch = ruleSrc->charAt(curIndex);
             type = charType(ch);
         }
         type = tNumber;
         break;

       case tDot:
         // We could be looking at either ".." in a range, or "..." at the end of a sample.
         if (curIndex+1 >= ruleSrc->length() || ruleSrc->charAt(curIndex+1) != DOT) {
             ++curIndex;
             break; // Single dot
         }
         if (curIndex+2 >= ruleSrc->length() || ruleSrc->charAt(curIndex+2) != DOT) {
             curIndex += 2;
             type = tDot2;
             break; // double dot
         }
         type = tEllipsis;
         curIndex += 3;
         break;     // triple dot

       default:
         status = U_UNEXPECTED_TOKEN;
         ++curIndex;
         break;
    }

    U_ASSERT(ruleIndex <= ruleSrc->length());
    U_ASSERT(curIndex <= ruleSrc->length());
    token=UnicodeString(*ruleSrc, ruleIndex, curIndex-ruleIndex);
    ruleIndex = curIndex;
}

tokenType
PluralRuleParser::charType(char16_t ch) {
    if ((ch>=U_ZERO) && (ch<=U_NINE)) {
        return tNumber;
    }
    if (ch>=LOW_A && ch<=LOW_Z) {
        return tKeyword;
    }
    switch (ch) {
    case COLON:
        return tColon;
    case SPACE:
        return tSpace;
    case SEMI_COLON:
        return tSemiColon;
    case DOT:
        return tDot;
    case COMMA:
        return tComma;
    case EXCLAMATION:
        return tNotEqual;
    case EQUALS:
        return tEqual;
    case PERCENT_SIGN:
        return tMod;
    case AT:
        return tAt;
    case ELLIPSIS:
        return tEllipsis;
    case TILDE:
        return tTilde;
    default :
        return none;
    }
}


//  Set token type for reserved words in the Plural Rule syntax.

tokenType
PluralRuleParser::getKeyType(const UnicodeString &token, tokenType keyType)
{
    if (keyType != tKeyword) {
        return keyType;
    }

    if (0 == token.compare(PK_VAR_N, 1)) {
        keyType = tVariableN;
    } else if (0 == token.compare(PK_VAR_I, 1)) {
        keyType = tVariableI;
    } else if (0 == token.compare(PK_VAR_F, 1)) {
        keyType = tVariableF;
    } else if (0 == token.compare(PK_VAR_T, 1)) {
        keyType = tVariableT;
    } else if (0 == token.compare(PK_VAR_E, 1)) {
        keyType = tVariableE;
    } else if (0 == token.compare(PK_VAR_C, 1)) {
        keyType = tVariableC;
    } else if (0 == token.compare(PK_VAR_V, 1)) {
        keyType = tVariableV;
    } else if (0 == token.compare(PK_IS, 2)) {
        keyType = tIs;
    } else if (0 == token.compare(PK_AND, 3)) {
        keyType = tAnd;
    } else if (0 == token.compare(PK_IN, 2)) {
        keyType = tIn;
    } else if (0 == token.compare(PK_WITHIN, 6)) {
        keyType = tWithin;
    } else if (0 == token.compare(PK_NOT, 3)) {
        keyType = tNot;
    } else if (0 == token.compare(PK_MOD, 3)) {
        keyType = tMod;
    } else if (0 == token.compare(PK_OR, 2)) {
        keyType = tOr;
    } else if (0 == token.compare(PK_DECIMAL, 7)) {
        keyType = tDecimal;
    } else if (0 == token.compare(PK_INTEGER, 7)) {
        keyType = tInteger;
    }
    return keyType;
}


PluralKeywordEnumeration::PluralKeywordEnumeration(RuleChain *header, UErrorCode& status)
        : pos(0), fKeywordNames(status) {
    if (U_FAILURE(status)) {
        return;
    }
    fKeywordNames.setDeleter(uprv_deleteUObject);
    UBool  addKeywordOther = true;
    RuleChain *node = header;
    while (node != nullptr) {
        LocalPointer<UnicodeString> newElem(node->fKeyword.clone(), status);
        fKeywordNames.adoptElement(newElem.orphan(), status);
        if (U_FAILURE(status)) {
            return;
        }
        if (0 == node->fKeyword.compare(PLURAL_KEYWORD_OTHER, 5)) {
            addKeywordOther = false;
        }
        node = node->fNext;
    }

    if (addKeywordOther) {
        LocalPointer<UnicodeString> newElem(new UnicodeString(PLURAL_KEYWORD_OTHER), status);
        fKeywordNames.adoptElement(newElem.orphan(), status);
        if (U_FAILURE(status)) {
            return;
        }
    }
}

const UnicodeString*
PluralKeywordEnumeration::snext(UErrorCode& status) {
    if (U_SUCCESS(status) && pos < fKeywordNames.size()) {
        return static_cast<const UnicodeString*>(fKeywordNames.elementAt(pos++));
    }
    return nullptr;
}

void
PluralKeywordEnumeration::reset(UErrorCode& /*status*/) {
    pos=0;
}

int32_t
PluralKeywordEnumeration::count(UErrorCode& /*status*/) const {
    return fKeywordNames.size();
}

PluralKeywordEnumeration::~PluralKeywordEnumeration() {
}

PluralOperand tokenTypeToPluralOperand(tokenType tt) {
    switch(tt) {
    case tVariableN:
        return PLURAL_OPERAND_N;
    case tVariableI:
        return PLURAL_OPERAND_I;
    case tVariableF:
        return PLURAL_OPERAND_F;
    case tVariableV:
        return PLURAL_OPERAND_V;
    case tVariableT:
        return PLURAL_OPERAND_T;
    case tVariableE:
        return PLURAL_OPERAND_E;
    case tVariableC:
        return PLURAL_OPERAND_E;
    default:
        UPRV_UNREACHABLE_EXIT;  // unexpected.
    }
}

FixedDecimal::FixedDecimal(double n, int32_t v, int64_t f, int32_t e, int32_t c) {
    init(n, v, f, e, c);
}

FixedDecimal::FixedDecimal(double n, int32_t v, int64_t f, int32_t e) {
    init(n, v, f, e);
    // check values. TODO make into unit test.
    //            
    //            long visiblePower = (int) Math.pow(10.0, v);
    //            if (decimalDigits > visiblePower) {
    //                throw new IllegalArgumentException();
    //            }
    //            double fraction = intValue + (decimalDigits / (double) visiblePower);
    //            if (fraction != source) {
    //                double diff = Math.abs(fraction - source)/(Math.abs(fraction) + Math.abs(source));
    //                if (diff > 0.00000001d) {
    //                    throw new IllegalArgumentException();
    //                }
    //            }
}

FixedDecimal::FixedDecimal(double n, int32_t v, int64_t f) {
    init(n, v, f);
}

FixedDecimal::FixedDecimal(double n, int32_t v) {
    // Ugly, but for samples we don't care.
    init(n, v, getFractionalDigits(n, v));
}

FixedDecimal::FixedDecimal(double n) {
    init(n);
}

FixedDecimal::FixedDecimal() {
    init(0, 0, 0);
}


// Create a FixedDecimal from a UnicodeString containing a number.
//    Inefficient, but only used for samples, so simplicity trumps efficiency.

FixedDecimal::FixedDecimal(const UnicodeString &num, UErrorCode &status) {
    CharString cs;
    int32_t parsedExponent = 0;
    int32_t parsedCompactExponent = 0;

    int32_t exponentIdx = num.indexOf(u'e');
    if (exponentIdx < 0) {
        exponentIdx = num.indexOf(u'E');
    }
    int32_t compactExponentIdx = num.indexOf(u'c');
    if (compactExponentIdx < 0) {
        compactExponentIdx = num.indexOf(u'C');
    }

    if (exponentIdx >= 0) {
        cs.appendInvariantChars(num.tempSubString(0, exponentIdx), status);
        int32_t expSubstrStart = exponentIdx + 1;
        parsedExponent = ICU_Utility::parseAsciiInteger(num, expSubstrStart);
    }
    else if (compactExponentIdx >= 0) {
        cs.appendInvariantChars(num.tempSubString(0, compactExponentIdx), status);
        int32_t expSubstrStart = compactExponentIdx + 1;
        parsedCompactExponent = ICU_Utility::parseAsciiInteger(num, expSubstrStart);

        parsedExponent = parsedCompactExponent;
        exponentIdx = compactExponentIdx;
    }
    else {
        cs.appendInvariantChars(num, status);
    }

    DecimalQuantity dl;
    dl.setToDecNumber(cs.toStringPiece(), status);
    if (U_FAILURE(status)) {
        init(0, 0, 0);
        return;
    }

    int32_t decimalPoint = num.indexOf(DOT);
    double n = dl.toDouble();
    if (decimalPoint == -1) {
        init(n, 0, 0, parsedExponent);
    } else {
        int32_t fractionNumLength = exponentIdx < 0 ? num.length() : cs.length();
        int32_t v = fractionNumLength - decimalPoint - 1;
        init(n, v, getFractionalDigits(n, v), parsedExponent);
    }
}


FixedDecimal::FixedDecimal(const FixedDecimal &other) {
    source = other.source;
    visibleDecimalDigitCount = other.visibleDecimalDigitCount;
    decimalDigits = other.decimalDigits;
    decimalDigitsWithoutTrailingZeros = other.decimalDigitsWithoutTrailingZeros;
    intValue = other.intValue;
    exponent = other.exponent;
    _hasIntegerValue = other._hasIntegerValue;
    isNegative = other.isNegative;
    _isNaN = other._isNaN;
    _isInfinite = other._isInfinite;
}

FixedDecimal::~FixedDecimal() = default;

FixedDecimal FixedDecimal::createWithExponent(double n, int32_t v, int32_t e) {
    return FixedDecimal(n, v, getFractionalDigits(n, v), e);
}


void FixedDecimal::init(double n) {
    int32_t numFractionDigits = decimals(n);
    init(n, numFractionDigits, getFractionalDigits(n, numFractionDigits));
}


void FixedDecimal::init(double n, int32_t v, int64_t f) {
    int32_t exponent = 0;
    init(n, v, f, exponent);
}

void FixedDecimal::init(double n, int32_t v, int64_t f, int32_t e) {
    // Currently, `c` is an alias for `e`
    init(n, v, f, e, e);
}

void FixedDecimal::init(double n, int32_t v, int64_t f, int32_t e, int32_t c) {
    isNegative = n < 0.0;
    source = fabs(n);
    _isNaN = uprv_isNaN(source);
    _isInfinite = uprv_isInfinite(source);
    exponent = e;
    if (exponent == 0) {
        exponent = c;
    }
    if (_isNaN || _isInfinite ||
        source > static_cast<double>(U_INT64_MAX) ||
        source < static_cast<double>(U_INT64_MIN)) {
        v = 0;
        f = 0;
        intValue = 0;
        _hasIntegerValue = false;
    } else {
        intValue = static_cast<int64_t>(source);
        _hasIntegerValue = (source == intValue);
    }

    visibleDecimalDigitCount = v;
    decimalDigits = f;
    if (f == 0) {
         decimalDigitsWithoutTrailingZeros = 0;
    } else {
        int64_t fdwtz = f;
        while ((fdwtz%10) == 0) {
            fdwtz /= 10;
        }
        decimalDigitsWithoutTrailingZeros = fdwtz;
    }
}


//  Fast path only exact initialization. Return true if successful.
//     Note: Do not multiply by 10 each time through loop, rounding cruft can build
//           up that makes the check for an integer result fail.
//           A single multiply of the original number works more reliably.
static int32_t p10[] = {1, 10, 100, 1000, 10000};
UBool FixedDecimal::quickInit(double n) {
    UBool success = false;
    n = fabs(n);
    int32_t numFractionDigits;
    for (numFractionDigits = 0; numFractionDigits <= 3; numFractionDigits++) {
        double scaledN = n * p10[numFractionDigits];
        if (scaledN == floor(scaledN)) {
            success = true;
            break;
        }
    }
    if (success) {
        init(n, numFractionDigits, getFractionalDigits(n, numFractionDigits));
    }
    return success;
}



int32_t FixedDecimal::decimals(double n) {
    // Count the number of decimal digits in the fraction part of the number, excluding trailing zeros.
    // fastpath the common cases, integers or fractions with 3 or fewer digits
    n = fabs(n);
    for (int ndigits=0; ndigits<=3; ndigits++) {
        double scaledN = n * p10[ndigits];
        if (scaledN == floor(scaledN)) {
            return ndigits;
        }
    }

    // Slow path, convert with snprintf, parse converted output.
    char  buf[30] = {0};
    snprintf(buf, sizeof(buf), "%1.15e", n);
    // formatted number looks like this: 1.234567890123457e-01
    int exponent = atoi(buf+18);
    int numFractionDigits = 15;
    for (int i=16; ; --i) {
        if (buf[i] != '0') {
            break;
        }
        --numFractionDigits;
    }
    numFractionDigits -= exponent;   // Fraction part of fixed point representation.
    return numFractionDigits;
}


// Get the fraction digits of a double, represented as an integer.
//    v is the number of visible fraction digits in the displayed form of the number.
//       Example: n = 1001.234, v = 6, result = 234000
//    TODO: need to think through how this is used in the plural rule context.
//          This function can easily encounter integer overflow, 
//          and can easily return noise digits when the precision of a double is exceeded.

int64_t FixedDecimal::getFractionalDigits(double n, int32_t v) {
    if (v == 0 || n == floor(n) || uprv_isNaN(n) || uprv_isPositiveInfinity(n)) {
        return 0;
    }
    n = fabs(n);
    double fract = n - floor(n);
    switch (v) {
      case 1: return static_cast<int64_t>(fract * 10.0 + 0.5);
      case 2: return static_cast<int64_t>(fract * 100.0 + 0.5);
      case 3: return static_cast<int64_t>(fract * 1000.0 + 0.5);
      default:
          double scaled = floor(fract * pow(10.0, static_cast<double>(v)) + 0.5);
          if (scaled >= static_cast<double>(U_INT64_MAX)) {
              // Note: a double cannot accurately represent U_INT64_MAX. Casting it to double
              //       will round up to the next representable value, which is U_INT64_MAX + 1.
              return U_INT64_MAX;
          } else {
              return static_cast<int64_t>(scaled);
          }
      }
}


void FixedDecimal::adjustForMinFractionDigits(int32_t minFractionDigits) {
    int32_t numTrailingFractionZeros = minFractionDigits - visibleDecimalDigitCount;
    if (numTrailingFractionZeros > 0) {
        for (int32_t i=0; i<numTrailingFractionZeros; i++) {
            // Do not let the decimalDigits value overflow if there are many trailing zeros.
            // Limit the value to 18 digits, the most that a 64 bit int can fully represent.
            if (decimalDigits >= 100000000000000000LL) {
                break;
            }
            decimalDigits *= 10;
        }
        visibleDecimalDigitCount += numTrailingFractionZeros;
    }
}


double FixedDecimal::getPluralOperand(PluralOperand operand) const {
    switch(operand) {
        case PLURAL_OPERAND_N: return (exponent == 0 ? source : source * pow(10.0, exponent));
        case PLURAL_OPERAND_I: return static_cast<double>(longValue());
        case PLURAL_OPERAND_F: return static_cast<double>(decimalDigits);
        case PLURAL_OPERAND_T: return static_cast<double>(decimalDigitsWithoutTrailingZeros);
        case PLURAL_OPERAND_V: return visibleDecimalDigitCount;
        case PLURAL_OPERAND_E: return exponent;
        case PLURAL_OPERAND_C: return exponent;
        default:
             UPRV_UNREACHABLE_EXIT;  // unexpected.
    }
}

bool FixedDecimal::isNaN() const {
    return _isNaN;
}

bool FixedDecimal::isInfinite() const {
    return _isInfinite;
}

bool FixedDecimal::hasIntegerValue() const {
    return _hasIntegerValue;
}

bool FixedDecimal::isNanOrInfinity() const {
    return _isNaN || _isInfinite;
}

int32_t FixedDecimal::getVisibleFractionDigitCount() const {
    return visibleDecimalDigitCount;
}

bool FixedDecimal::operator==(const FixedDecimal &other) const {
    return source == other.source && visibleDecimalDigitCount == other.visibleDecimalDigitCount
        && decimalDigits == other.decimalDigits && exponent == other.exponent;
}

UnicodeString FixedDecimal::toString() const {
    char pattern[15];
    char buffer[20];
    if (exponent != 0) {
        snprintf(pattern, sizeof(pattern), "%%.%dfe%%d", visibleDecimalDigitCount);
        snprintf(buffer, sizeof(buffer), pattern, source, exponent);
    } else {
        snprintf(pattern, sizeof(pattern), "%%.%df", visibleDecimalDigitCount);
        snprintf(buffer, sizeof(buffer), pattern, source);
    }
    return UnicodeString(buffer, -1, US_INV);
}

double FixedDecimal::doubleValue() const {
    return (isNegative ? -source : source) * pow(10.0, exponent);
}

int64_t FixedDecimal::longValue() const {
    if (exponent == 0) {
        return intValue;
    } else {
        return static_cast<long>(pow(10.0, exponent) * intValue);
    }
}


PluralAvailableLocalesEnumeration::PluralAvailableLocalesEnumeration(UErrorCode &status) {
    fOpenStatus = status;
    if (U_FAILURE(status)) {
        return;
    }
    fOpenStatus = U_ZERO_ERROR; // clear any warnings.
    LocalUResourceBundlePointer rb(ures_openDirect(nullptr, "plurals", &fOpenStatus));
    fLocales = ures_getByKey(rb.getAlias(), "locales", nullptr, &fOpenStatus);
}

PluralAvailableLocalesEnumeration::~PluralAvailableLocalesEnumeration() {
    ures_close(fLocales);
    ures_close(fRes);
    fLocales = nullptr;
    fRes = nullptr;
}

const char *PluralAvailableLocalesEnumeration::next(int32_t *resultLength, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    if (U_FAILURE(fOpenStatus)) {
        status = fOpenStatus;
        return nullptr;
    }
    fRes = ures_getNextResource(fLocales, fRes, &status);
    if (fRes == nullptr || U_FAILURE(status)) {
        if (status == U_INDEX_OUTOFBOUNDS_ERROR) {
            status = U_ZERO_ERROR;
        }
        return nullptr;
    }
    const char *result = ures_getKey(fRes);
    if (resultLength != nullptr) {
        *resultLength = static_cast<int32_t>(uprv_strlen(result));
    }
    return result;
}


void PluralAvailableLocalesEnumeration::reset(UErrorCode &status) {
    if (U_FAILURE(status)) {
       return;
    }
    if (U_FAILURE(fOpenStatus)) {
        status = fOpenStatus;
        return;
    }
    ures_resetIterator(fLocales);
}

int32_t PluralAvailableLocalesEnumeration::count(UErrorCode &status) const {
    if (U_FAILURE(status)) {
        return 0;
    }
    if (U_FAILURE(fOpenStatus)) {
        status = fOpenStatus;
        return 0;
    }
    return ures_getSize(fLocales);
}

U_NAMESPACE_END


#endif /* #if !UCONFIG_NO_FORMATTING */

//eof
