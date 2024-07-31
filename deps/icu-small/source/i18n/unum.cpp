// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*   Copyright (C) 1996-2015, International Business Machines
*   Corporation and others.  All Rights Reserved.
*******************************************************************************
* Modification History:
*
*   Date        Name        Description
*   06/24/99    helena      Integrated Alan's NF enhancements and Java2 bug fixes
*******************************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/unum.h"

#include "unicode/uloc.h"
#include "unicode/numfmt.h"
#include "unicode/decimfmt.h"
#include "unicode/rbnf.h"
#include "unicode/compactdecimalformat.h"
#include "unicode/ustring.h"
#include "unicode/fmtable.h"
#include "unicode/dcfmtsym.h"
#include "unicode/curramt.h"
#include "unicode/localpointer.h"
#include "unicode/measfmt.h"
#include "unicode/udisplaycontext.h"
#include "uassert.h"
#include "cpputils.h"
#include "cstring.h"
#include "putilimp.h"


U_NAMESPACE_USE


U_CAPI UNumberFormat* U_EXPORT2
unum_open(  UNumberFormatStyle    style,
            const    char16_t*    pattern,
            int32_t            patternLength,
            const    char*     locale,
            UParseError*       parseErr,
            UErrorCode*        status) {
    if(U_FAILURE(*status)) {
        return nullptr;
    }

    NumberFormat *retVal = nullptr;

    switch(style) {
    case UNUM_DECIMAL:
    case UNUM_CURRENCY:
    case UNUM_PERCENT:
    case UNUM_SCIENTIFIC:
    case UNUM_CURRENCY_ISO:
    case UNUM_CURRENCY_PLURAL:
    case UNUM_CURRENCY_ACCOUNTING:
    case UNUM_CASH_CURRENCY:
    case UNUM_CURRENCY_STANDARD:
        retVal = NumberFormat::createInstance(Locale(locale), style, *status);
        break;

    case UNUM_PATTERN_DECIMAL: {
        UParseError tErr;
        /* UnicodeString can handle the case when patternLength = -1. */
        const UnicodeString pat(pattern, patternLength);

        if(parseErr==nullptr){
            parseErr = &tErr;
        }

        DecimalFormatSymbols *syms = new DecimalFormatSymbols(Locale(locale), *status);
        if(syms == nullptr) {
            *status = U_MEMORY_ALLOCATION_ERROR;
            return nullptr;
        }
        if (U_FAILURE(*status)) {
            delete syms;
            return nullptr;
        }

        retVal = new DecimalFormat(pat, syms, *parseErr, *status);
        if(retVal == nullptr) {
            delete syms;
        }
    } break;

#if U_HAVE_RBNF
    case UNUM_PATTERN_RULEBASED: {
        UParseError tErr;
        /* UnicodeString can handle the case when patternLength = -1. */
        const UnicodeString pat(pattern, patternLength);
        
        if(parseErr==nullptr){
            parseErr = &tErr;
        }
        
        retVal = new RuleBasedNumberFormat(pat, Locale(locale), *parseErr, *status);
    } break;

    case UNUM_SPELLOUT:
        retVal = new RuleBasedNumberFormat(URBNF_SPELLOUT, Locale(locale), *status);
        break;

    case UNUM_ORDINAL:
        retVal = new RuleBasedNumberFormat(URBNF_ORDINAL, Locale(locale), *status);
        break;

    case UNUM_DURATION:
        retVal = new RuleBasedNumberFormat(URBNF_DURATION, Locale(locale), *status);
        break;

    case UNUM_NUMBERING_SYSTEM: {
        // if the locale ID specifies a numbering system, go through NumberFormat::createInstance()
        // to handle it properly (we have to specify UNUM_DEFAULT to get it to handle the numbering
        // system, but we'll always get a RuleBasedNumberFormat back); otherwise, just go ahead and
        // create a RuleBasedNumberFormat ourselves
        UErrorCode localErr = U_ZERO_ERROR;
        Locale localeObj(locale);
        int32_t keywordLength = localeObj.getKeywordValue("numbers", nullptr, 0, localErr);
        if (keywordLength > 0) {
            retVal = NumberFormat::createInstance(localeObj, UNUM_DEFAULT, *status);
        } else {
            retVal = new RuleBasedNumberFormat(URBNF_NUMBERING_SYSTEM, localeObj, *status);
        }
    } break;
#endif

    case UNUM_DECIMAL_COMPACT_SHORT:
        retVal = CompactDecimalFormat::createInstance(Locale(locale), UNUM_SHORT, *status);
        break;

    case UNUM_DECIMAL_COMPACT_LONG:
        retVal = CompactDecimalFormat::createInstance(Locale(locale), UNUM_LONG, *status);
        break;

    default:
        *status = U_UNSUPPORTED_ERROR;
        return nullptr;
    }

    if(retVal == nullptr && U_SUCCESS(*status)) {
        *status = U_MEMORY_ALLOCATION_ERROR;
    }

    if (U_FAILURE(*status) && retVal != nullptr) {
        delete retVal;
        retVal = nullptr;
    }

    return reinterpret_cast<UNumberFormat *>(retVal);
}

U_CAPI void U_EXPORT2
unum_close(UNumberFormat* fmt)
{
    delete (NumberFormat*) fmt;
}

U_CAPI UNumberFormat* U_EXPORT2
unum_clone(const UNumberFormat *fmt,
       UErrorCode *status)
{
    if(U_FAILURE(*status))
        return nullptr;

    Format* res = nullptr;
    const NumberFormat* nf = reinterpret_cast<const NumberFormat*>(fmt);
    const DecimalFormat* df = dynamic_cast<const DecimalFormat*>(nf);
    if (df != nullptr) {
        res = df->clone();
    } else {
        const RuleBasedNumberFormat* rbnf = dynamic_cast<const RuleBasedNumberFormat*>(nf);
        U_ASSERT(rbnf != nullptr);
        res = rbnf->clone();
    }

    if (res == nullptr) {
        *status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    
    return (UNumberFormat*) res;
}

U_CAPI int32_t U_EXPORT2
unum_format(    const    UNumberFormat*    fmt,
        int32_t           number,
        char16_t*            result,
        int32_t           resultLength,
        UFieldPosition    *pos,
        UErrorCode*       status)
{
        return unum_formatInt64(fmt, number, result, resultLength, pos, status);
}

U_CAPI int32_t U_EXPORT2
unum_formatInt64(const UNumberFormat* fmt,
        int64_t         number,
        char16_t*          result,
        int32_t         resultLength,
        UFieldPosition *pos,
        UErrorCode*     status)
{
    if(U_FAILURE(*status))
        return -1;
    
    UnicodeString res;
    if(!(result==nullptr && resultLength==0)) {
        // nullptr destination for pure preflighting: empty dummy string
        // otherwise, alias the destination buffer
        res.setTo(result, 0, resultLength);
    }
    
    FieldPosition fp;

    if (pos != nullptr)
        fp.setField(pos->field);
    
    ((const NumberFormat*)fmt)->format(number, res, fp, *status);

    if (pos != nullptr) {
        pos->beginIndex = fp.getBeginIndex();
        pos->endIndex = fp.getEndIndex();
    }
    
    return res.extract(result, resultLength, *status);
}

U_CAPI int32_t U_EXPORT2
unum_formatDouble(    const    UNumberFormat*  fmt,
            double          number,
            char16_t*          result,
            int32_t         resultLength,
            UFieldPosition  *pos, /* 0 if ignore */
            UErrorCode*     status)
{
 
  if(U_FAILURE(*status)) return -1;

  UnicodeString res;
  if(!(result==nullptr && resultLength==0)) {
    // nullptr destination for pure preflighting: empty dummy string
    // otherwise, alias the destination buffer
    res.setTo(result, 0, resultLength);
  }

  FieldPosition fp;

  if (pos != nullptr)
    fp.setField(pos->field);
  
  ((const NumberFormat*)fmt)->format(number, res, fp, *status);

  if (pos != nullptr) {
    pos->beginIndex = fp.getBeginIndex();
    pos->endIndex = fp.getEndIndex();
  }
  
  return res.extract(result, resultLength, *status);
}

U_CAPI int32_t U_EXPORT2
unum_formatDoubleForFields(const UNumberFormat* format,
                           double number,
                           char16_t* result,
                           int32_t resultLength,
                           UFieldPositionIterator* fpositer,
                           UErrorCode* status)
{
    if (U_FAILURE(*status))
        return -1;

    if (result == nullptr ? resultLength != 0 : resultLength < 0) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return -1;
    }

    UnicodeString res;
    if (result != nullptr) {
        // nullptr destination for pure preflighting: empty dummy string
        // otherwise, alias the destination buffer
        res.setTo(result, 0, resultLength);
    }

    ((const NumberFormat*)format)->format(number, res, (FieldPositionIterator*)fpositer, *status);

    return res.extract(result, resultLength, *status);
}

U_CAPI int32_t U_EXPORT2 
unum_formatDecimal(const    UNumberFormat*  fmt,
            const char *    number,
            int32_t         length,
            char16_t*          result,
            int32_t         resultLength,
            UFieldPosition  *pos, /* 0 if ignore */
            UErrorCode*     status) {

    if(U_FAILURE(*status)) {
        return -1;
    }
    if ((result == nullptr && resultLength != 0) || resultLength < 0) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return -1;
    }

    FieldPosition fp;
    if (pos != nullptr) {
        fp.setField(pos->field);
    }

    if (length < 0) {
        length = static_cast<int32_t>(uprv_strlen(number));
    }
    StringPiece numSP(number, length);
    Formattable numFmtbl(numSP, *status);

    UnicodeString resultStr;
    if (resultLength > 0) {
        // Alias the destination buffer.
        resultStr.setTo(result, 0, resultLength);
    }
    ((const NumberFormat*)fmt)->format(numFmtbl, resultStr, fp, *status);
    if (pos != nullptr) {
        pos->beginIndex = fp.getBeginIndex();
        pos->endIndex = fp.getEndIndex();
    }
    return resultStr.extract(result, resultLength, *status);
}




U_CAPI int32_t U_EXPORT2 
unum_formatDoubleCurrency(const UNumberFormat* fmt,
                          double number,
                          char16_t* currency,
                          char16_t* result,
                          int32_t resultLength,
                          UFieldPosition* pos, /* ignored if 0 */
                          UErrorCode* status) {
    if (U_FAILURE(*status)) return -1;

    UnicodeString res;
    if (!(result==nullptr && resultLength==0)) {
        // nullptr destination for pure preflighting: empty dummy string
        // otherwise, alias the destination buffer
        res.setTo(result, 0, resultLength);
    }
    
    FieldPosition fp;
    if (pos != nullptr) {
        fp.setField(pos->field);
    }
    CurrencyAmount *tempCurrAmnt = new CurrencyAmount(number, currency, *status);
    // Check for null pointer.
    if (tempCurrAmnt == nullptr) {
        *status = U_MEMORY_ALLOCATION_ERROR;
        return -1;
    }
    Formattable n(tempCurrAmnt);
    ((const NumberFormat*)fmt)->format(n, res, fp, *status);

    if (pos != nullptr) {
        pos->beginIndex = fp.getBeginIndex();
        pos->endIndex = fp.getEndIndex();
    }
  
    return res.extract(result, resultLength, *status);
}

static void
parseRes(Formattable& res,
         const   UNumberFormat*  fmt,
         const   char16_t*          text,
         int32_t         textLength,
         int32_t         *parsePos /* 0 = start */,
         UErrorCode      *status)
{
    if(U_FAILURE(*status))
        return;
    
    const UnicodeString src((UBool)(textLength == -1), text, textLength);
    ParsePosition pp;

    if (parsePos != nullptr)
        pp.setIndex(*parsePos);
    
    ((const NumberFormat*)fmt)->parse(src, res, pp);
    
    if(pp.getErrorIndex() != -1) {
        *status = U_PARSE_ERROR;
        if (parsePos != nullptr) {
            *parsePos = pp.getErrorIndex();
        }
    } else if (parsePos != nullptr) {
        *parsePos = pp.getIndex();
    }
}

U_CAPI int32_t U_EXPORT2
unum_parse(    const   UNumberFormat*  fmt,
        const   char16_t*          text,
        int32_t         textLength,
        int32_t         *parsePos /* 0 = start */,
        UErrorCode      *status)
{
    Formattable res;
    parseRes(res, fmt, text, textLength, parsePos, status);
    return res.getLong(*status);
}

U_CAPI int64_t U_EXPORT2
unum_parseInt64(    const   UNumberFormat*  fmt,
        const   char16_t*          text,
        int32_t         textLength,
        int32_t         *parsePos /* 0 = start */,
        UErrorCode      *status)
{
    Formattable res;
    parseRes(res, fmt, text, textLength, parsePos, status);
    return res.getInt64(*status);
}

U_CAPI double U_EXPORT2
unum_parseDouble(    const   UNumberFormat*  fmt,
            const   char16_t*          text,
            int32_t         textLength,
            int32_t         *parsePos /* 0 = start */,
            UErrorCode      *status)
{
    Formattable res;
    parseRes(res, fmt, text, textLength, parsePos, status);
    return res.getDouble(*status);
}

U_CAPI int32_t U_EXPORT2
unum_parseDecimal(const UNumberFormat*  fmt,
            const char16_t*    text,
            int32_t         textLength,
            int32_t         *parsePos /* 0 = start */,
            char            *outBuf,
            int32_t         outBufLength,
            UErrorCode      *status)
{
    if (U_FAILURE(*status)) {
        return -1;
    }
    if ((outBuf == nullptr && outBufLength != 0) || outBufLength < 0) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return -1;
    }
    Formattable res;
    parseRes(res, fmt, text, textLength, parsePos, status);
    StringPiece sp = res.getDecimalNumber(*status);
    if (U_FAILURE(*status)) {
       return -1;
    } else if (sp.size() > outBufLength) {
        *status = U_BUFFER_OVERFLOW_ERROR;
    } else if (sp.size() == outBufLength) {
        uprv_strncpy(outBuf, sp.data(), sp.size());
        *status = U_STRING_NOT_TERMINATED_WARNING;
    } else {
        U_ASSERT(outBufLength > 0);
        uprv_strcpy(outBuf, sp.data());
    }
    return sp.size();
}

U_CAPI double U_EXPORT2
unum_parseDoubleCurrency(const UNumberFormat* fmt,
                         const char16_t* text,
                         int32_t textLength,
                         int32_t* parsePos, /* 0 = start */
                         char16_t* currency,
                         UErrorCode* status) {
    double doubleVal = 0.0;
    currency[0] = 0;
    if (U_FAILURE(*status)) {
        return doubleVal;
    }
    const UnicodeString src((UBool)(textLength == -1), text, textLength);
    ParsePosition pp;
    if (parsePos != nullptr) {
        pp.setIndex(*parsePos);
    }
    *status = U_PARSE_ERROR; // assume failure, reset if succeed
    LocalPointer<CurrencyAmount> currAmt(((const NumberFormat*)fmt)->parseCurrency(src, pp));
    if (pp.getErrorIndex() != -1) {
        if (parsePos != nullptr) {
            *parsePos = pp.getErrorIndex();
        }
    } else {
        if (parsePos != nullptr) {
            *parsePos = pp.getIndex();
        }
        if (pp.getIndex() > 0) {
            *status = U_ZERO_ERROR;
            u_strcpy(currency, currAmt->getISOCurrency());
            doubleVal = currAmt->getNumber().getDouble(*status);
        }
    }
    return doubleVal;
}

U_CAPI const char* U_EXPORT2
unum_getAvailable(int32_t index)
{
    return uloc_getAvailable(index);
}

U_CAPI int32_t U_EXPORT2
unum_countAvailable()
{
    return uloc_countAvailable();
}

U_CAPI bool U_EXPORT2
unum_hasAttribute(const UNumberFormat*          fmt,
          UNumberFormatAttribute  attr)
{
    const NumberFormat* nf = reinterpret_cast<const NumberFormat*>(fmt);
    bool isDecimalFormat = dynamic_cast<const DecimalFormat*>(nf) != nullptr;
    
    switch (attr) {
        case UNUM_LENIENT_PARSE:
        case UNUM_MAX_INTEGER_DIGITS:
        case UNUM_MIN_INTEGER_DIGITS:
        case UNUM_INTEGER_DIGITS:
        case UNUM_MAX_FRACTION_DIGITS:
        case UNUM_MIN_FRACTION_DIGITS:
        case UNUM_FRACTION_DIGITS:
        case UNUM_ROUNDING_MODE:
            return true;
        default:
            return isDecimalFormat;
    }
}

U_CAPI int32_t U_EXPORT2
unum_getAttribute(const UNumberFormat*          fmt,
          UNumberFormatAttribute  attr)
{
    const NumberFormat* nf = reinterpret_cast<const NumberFormat*>(fmt);
    if (attr == UNUM_LENIENT_PARSE) {
        // Supported for all subclasses
        return nf->isLenient();
    }
    else if (attr == UNUM_MAX_INTEGER_DIGITS) {
        return nf->getMaximumIntegerDigits();
    }
    else if (attr == UNUM_MIN_INTEGER_DIGITS) {
        return nf->getMinimumIntegerDigits();
    }
    else if (attr == UNUM_INTEGER_DIGITS) {
        // TODO: what should this return?
        return nf->getMinimumIntegerDigits();
    }
    else if (attr == UNUM_MAX_FRACTION_DIGITS) {
        return nf->getMaximumFractionDigits();
    }
    else if (attr == UNUM_MIN_FRACTION_DIGITS) {
        return nf->getMinimumFractionDigits();
    }
    else if (attr == UNUM_FRACTION_DIGITS) {
        // TODO: what should this return?
        return nf->getMinimumFractionDigits();
    }
    else if (attr == UNUM_ROUNDING_MODE) {
        return nf->getRoundingMode();
    }

    // The remaining attributes are only supported for DecimalFormat
    const DecimalFormat* df = dynamic_cast<const DecimalFormat*>(nf);
    if (df != nullptr) {
        UErrorCode ignoredStatus = U_ZERO_ERROR;
        return df->getAttribute(attr, ignoredStatus);
    }

    return -1;
}

U_CAPI void U_EXPORT2
unum_setAttribute(    UNumberFormat*          fmt,
            UNumberFormatAttribute  attr,
            int32_t                 newValue)
{
    NumberFormat* nf = reinterpret_cast<NumberFormat*>(fmt);
    if (attr == UNUM_LENIENT_PARSE) {
        // Supported for all subclasses
        // keep this here as the class may not be a DecimalFormat
        return nf->setLenient(newValue != 0);
    }
    else if (attr == UNUM_MAX_INTEGER_DIGITS) {
        return nf->setMaximumIntegerDigits(newValue);
    }
    else if (attr == UNUM_MIN_INTEGER_DIGITS) {
        return nf->setMinimumIntegerDigits(newValue);
    }
    else if (attr == UNUM_INTEGER_DIGITS) {
        nf->setMinimumIntegerDigits(newValue);
        return nf->setMaximumIntegerDigits(newValue);
    }
    else if (attr == UNUM_MAX_FRACTION_DIGITS) {
        return nf->setMaximumFractionDigits(newValue);
    }
    else if (attr == UNUM_MIN_FRACTION_DIGITS) {
        return nf->setMinimumFractionDigits(newValue);
    }
    else if (attr == UNUM_FRACTION_DIGITS) {
        nf->setMinimumFractionDigits(newValue);
        return nf->setMaximumFractionDigits(newValue);
    }
    else if (attr == UNUM_ROUNDING_MODE) {
        return nf->setRoundingMode((NumberFormat::ERoundingMode)newValue);
    }

    // The remaining attributes are only supported for DecimalFormat
    DecimalFormat* df = dynamic_cast<DecimalFormat*>(nf);
    if (df != nullptr) {
        UErrorCode ignoredStatus = U_ZERO_ERROR;
        df->setAttribute(attr, newValue, ignoredStatus);
    }
}

U_CAPI double U_EXPORT2
unum_getDoubleAttribute(const UNumberFormat*          fmt,
          UNumberFormatAttribute  attr)
{
    const NumberFormat* nf = reinterpret_cast<const NumberFormat*>(fmt);
    const DecimalFormat* df = dynamic_cast<const DecimalFormat*>(nf);
    if (df != nullptr &&  attr == UNUM_ROUNDING_INCREMENT) {
        return df->getRoundingIncrement();
    } else {
        return -1.0;
    }
}

U_CAPI void U_EXPORT2
unum_setDoubleAttribute(    UNumberFormat*          fmt,
            UNumberFormatAttribute  attr,
            double                 newValue)
{
    NumberFormat* nf = reinterpret_cast<NumberFormat*>(fmt);
    DecimalFormat* df = dynamic_cast<DecimalFormat*>(nf);
    if (df != nullptr && attr == UNUM_ROUNDING_INCREMENT) {   
        df->setRoundingIncrement(newValue);
    }
}

U_CAPI int32_t U_EXPORT2
unum_getTextAttribute(const UNumberFormat*  fmt,
            UNumberFormatTextAttribute      tag,
            char16_t*                          result,
            int32_t                         resultLength,
            UErrorCode*                     status)
{
    if(U_FAILURE(*status))
        return -1;

    UnicodeString res;
    if(!(result==nullptr && resultLength==0)) {
        // nullptr destination for pure preflighting: empty dummy string
        // otherwise, alias the destination buffer
        res.setTo(result, 0, resultLength);
    }

    const NumberFormat* nf = reinterpret_cast<const NumberFormat*>(fmt);
    const DecimalFormat* df = dynamic_cast<const DecimalFormat*>(nf);
    const RuleBasedNumberFormat* rbnf = nullptr;    // cast is below for performance
    if (df != nullptr) {
        switch(tag) {
        case UNUM_POSITIVE_PREFIX:
            df->getPositivePrefix(res);
            break;

        case UNUM_POSITIVE_SUFFIX:
            df->getPositiveSuffix(res);
            break;

        case UNUM_NEGATIVE_PREFIX:
            df->getNegativePrefix(res);
            break;

        case UNUM_NEGATIVE_SUFFIX:
            df->getNegativeSuffix(res);
            break;

        case UNUM_PADDING_CHARACTER:
            res = df->getPadCharacterString();
            break;

        case UNUM_CURRENCY_CODE:
            res = UnicodeString(df->getCurrency());
            break;

        default:
            *status = U_UNSUPPORTED_ERROR;
            return -1;
        }
    } else  if ((rbnf = dynamic_cast<const RuleBasedNumberFormat*>(nf)) != nullptr) {
        U_ASSERT(rbnf != nullptr);
        if (tag == UNUM_DEFAULT_RULESET) {
            res = rbnf->getDefaultRuleSetName();
        } else if (tag == UNUM_PUBLIC_RULESETS) {
            int32_t count = rbnf->getNumberOfRuleSetNames();
            for (int i = 0; i < count; ++i) {
                res += rbnf->getRuleSetName(i);
                res += (char16_t)0x003b; // semicolon
            }
        } else {
            *status = U_UNSUPPORTED_ERROR;
            return -1;
        }
    } else {
        *status = U_UNSUPPORTED_ERROR;
        return -1;
    }

    return res.extract(result, resultLength, *status);
}

U_CAPI void U_EXPORT2
unum_setTextAttribute(    UNumberFormat*                    fmt,
            UNumberFormatTextAttribute      tag,
            const    char16_t*                            newValue,
            int32_t                            newValueLength,
            UErrorCode                        *status)
{
    if(U_FAILURE(*status))
        return;

    UnicodeString val(newValue, newValueLength);
    NumberFormat* nf = reinterpret_cast<NumberFormat*>(fmt);
    DecimalFormat* df = dynamic_cast<DecimalFormat*>(nf);
    if (df != nullptr) {
      switch(tag) {
      case UNUM_POSITIVE_PREFIX:
        df->setPositivePrefix(val);
        break;
        
      case UNUM_POSITIVE_SUFFIX:
        df->setPositiveSuffix(val);
        break;
        
      case UNUM_NEGATIVE_PREFIX:
        df->setNegativePrefix(val);
        break;
        
      case UNUM_NEGATIVE_SUFFIX:
        df->setNegativeSuffix(val);
        break;
        
      case UNUM_PADDING_CHARACTER:
        df->setPadCharacter(val);
        break;
        
      case UNUM_CURRENCY_CODE:
        df->setCurrency(val.getTerminatedBuffer(), *status);
        break;
        
      default:
        *status = U_UNSUPPORTED_ERROR;
        break;
      }
    } else {
      RuleBasedNumberFormat* rbnf = dynamic_cast<RuleBasedNumberFormat*>(nf);
      U_ASSERT(rbnf != nullptr);
      if (tag == UNUM_DEFAULT_RULESET) {
        rbnf->setDefaultRuleSet(val, *status);
      } else {
        *status = U_UNSUPPORTED_ERROR;
      }
    }
}

U_CAPI int32_t U_EXPORT2
unum_toPattern(    const    UNumberFormat*          fmt,
        UBool                  isPatternLocalized,
        char16_t*                  result,
        int32_t                 resultLength,
        UErrorCode*             status)
{
    if(U_FAILURE(*status))
        return -1;
    
    UnicodeString pat;
    if(!(result==nullptr && resultLength==0)) {
        // nullptr destination for pure preflighting: empty dummy string
        // otherwise, alias the destination buffer
        pat.setTo(result, 0, resultLength);
    }

    const NumberFormat* nf = reinterpret_cast<const NumberFormat*>(fmt);
    const DecimalFormat* df = dynamic_cast<const DecimalFormat*>(nf);
    const RuleBasedNumberFormat* rbnf = nullptr;    // cast is below for performance
    if (df != nullptr) {
      if(isPatternLocalized)
        df->toLocalizedPattern(pat);
      else
        df->toPattern(pat);
    } else if ((rbnf = dynamic_cast<const RuleBasedNumberFormat*>(nf)) != nullptr) {
        pat = rbnf->getRules();
    } else {
        // leave `pat` empty
    }
    return pat.extract(result, resultLength, *status);
}

U_CAPI int32_t U_EXPORT2
unum_getSymbol(const UNumberFormat *fmt,
               UNumberFormatSymbol symbol,
               char16_t *buffer,
               int32_t size,
               UErrorCode *status) UPRV_NO_SANITIZE_UNDEFINED {
    if(status==nullptr || U_FAILURE(*status)) {
        return 0;
    }
    if(fmt==nullptr || symbol< 0 || symbol>=UNUM_FORMAT_SYMBOL_COUNT) {
        *status=U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    const NumberFormat *nf = reinterpret_cast<const NumberFormat *>(fmt);
    const DecimalFormat *dcf = dynamic_cast<const DecimalFormat *>(nf);
    if (dcf == nullptr) {
      *status = U_UNSUPPORTED_ERROR;
      return 0;
    }

    return dcf->
      getDecimalFormatSymbols()->
        getConstSymbol((DecimalFormatSymbols::ENumberFormatSymbol)symbol).
          extract(buffer, size, *status);
}

U_CAPI void U_EXPORT2
unum_setSymbol(UNumberFormat *fmt,
               UNumberFormatSymbol symbol,
               const char16_t *value,
               int32_t length,
               UErrorCode *status) UPRV_NO_SANITIZE_UNDEFINED {
    if(status==nullptr || U_FAILURE(*status)) {
        return;
    }
    if(fmt==nullptr || symbol< 0 || symbol>=UNUM_FORMAT_SYMBOL_COUNT || value==nullptr || length<-1) {
        *status=U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    NumberFormat *nf = reinterpret_cast<NumberFormat *>(fmt);
    DecimalFormat *dcf = dynamic_cast<DecimalFormat *>(nf);
    if (dcf == nullptr) {
      *status = U_UNSUPPORTED_ERROR;
      return;
    }

    DecimalFormatSymbols symbols(*dcf->getDecimalFormatSymbols());
    symbols.setSymbol((DecimalFormatSymbols::ENumberFormatSymbol)symbol,
        UnicodeString(value, length));  /* UnicodeString can handle the case when length = -1. */
    dcf->setDecimalFormatSymbols(symbols);
}

U_CAPI void U_EXPORT2
unum_applyPattern(  UNumberFormat  *fmt,
                    UBool          localized,
                    const char16_t *pattern,
                    int32_t        patternLength,
                    UParseError    *parseError,
                    UErrorCode*    status)
{
    UErrorCode tStatus = U_ZERO_ERROR;
    UParseError tParseError;
    
    if(parseError == nullptr){
        parseError = &tParseError;
    }
    
    if(status==nullptr){
        status = &tStatus;
    }
    
    int32_t len = (patternLength == -1 ? u_strlen(pattern) : patternLength);
    const UnicodeString pat((char16_t*)pattern, len, len);

    // Verify if the object passed is a DecimalFormat object
    NumberFormat* nf = reinterpret_cast<NumberFormat*>(fmt);
    DecimalFormat* df = dynamic_cast<DecimalFormat*>(nf);
    if (df != nullptr) {
      if(localized) {
        df->applyLocalizedPattern(pat,*parseError, *status);
      } else {
        df->applyPattern(pat,*parseError, *status);
      }
    } else {
      *status = U_UNSUPPORTED_ERROR;
      return;
    }
}

U_CAPI const char* U_EXPORT2
unum_getLocaleByType(const UNumberFormat *fmt,
                     ULocDataLocaleType type,
                     UErrorCode* status)
{
    if (fmt == nullptr) {
        if (U_SUCCESS(*status)) {
            *status = U_ILLEGAL_ARGUMENT_ERROR;
        }
        return nullptr;
    }
    return ((const Format*)fmt)->getLocaleID(type, *status);
}

U_CAPI void U_EXPORT2
unum_setContext(UNumberFormat* fmt, UDisplayContext value, UErrorCode* status)
{
    if (U_FAILURE(*status)) {
        return;
    }
    ((NumberFormat*)fmt)->setContext(value, *status);
}

U_CAPI UDisplayContext U_EXPORT2
unum_getContext(const UNumberFormat *fmt, UDisplayContextType type, UErrorCode* status)
{
    if (U_FAILURE(*status)) {
        return (UDisplayContext)0;
    }
    return ((const NumberFormat*)fmt)->getContext(type, *status);
}

U_CAPI UFormattable * U_EXPORT2
unum_parseToUFormattable(const UNumberFormat* fmt,
                         UFormattable *result,
                         const char16_t* text,
                         int32_t textLength,
                         int32_t* parsePos, /* 0 = start */
                         UErrorCode* status) {
  UFormattable *newFormattable = nullptr;
  if (U_FAILURE(*status)) return result;
  if (fmt == nullptr || (text==nullptr && textLength!=0)) {
    *status = U_ILLEGAL_ARGUMENT_ERROR;
    return result;
  }
  if (result == nullptr) { // allocate if not allocated.
    newFormattable = result = ufmt_open(status);
  }
  parseRes(*(Formattable::fromUFormattable(result)), fmt, text, textLength, parsePos, status);
  if (U_FAILURE(*status) && newFormattable != nullptr) {
    ufmt_close(newFormattable);
    result = nullptr; // deallocate if there was a parse error
  }
  return result;
}

U_CAPI int32_t U_EXPORT2
unum_formatUFormattable(const UNumberFormat* fmt,
                        const UFormattable *number,
                        char16_t *result,
                        int32_t resultLength,
                        UFieldPosition *pos, /* ignored if 0 */
                        UErrorCode *status) {
    if (U_FAILURE(*status)) {
      return 0;
    }
    if (fmt == nullptr || number==nullptr ||
        (result==nullptr ? resultLength!=0 : resultLength<0)) {
      *status = U_ILLEGAL_ARGUMENT_ERROR;
      return 0;
    }
    UnicodeString res(result, 0, resultLength);

    FieldPosition fp;

    if (pos != nullptr)
        fp.setField(pos->field);

    ((const NumberFormat*)fmt)->format(*(Formattable::fromUFormattable(number)), res, fp, *status);

    if (pos != nullptr) {
        pos->beginIndex = fp.getBeginIndex();
        pos->endIndex = fp.getEndIndex();
    }

    return res.extract(result, resultLength, *status);
}

#endif /* #if !UCONFIG_NO_FORMATTING */
