// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*   Copyright (C) 2004-2016, International Business Machines
*   Corporation and others.  All Rights Reserved.
*******************************************************************************
*   file name:  ucol_sit.cpp
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
* Modification history
* Date        Name      Comments
* 03/12/2004  weiv      Creation
*/

#include "unicode/ustring.h"
#include "unicode/udata.h"
#include "unicode/utf16.h"
#include "utracimp.h"
#include "ucol_imp.h"
#include "ulocimp.h"
#include "cmemory.h"
#include "cstring.h"
#include "uresimp.h"
#include "unicode/coll.h"
#include "unicode/stringpiece.h"
#include "charstr.h"

U_NAMESPACE_USE

#ifdef UCOL_TRACE_SIT
# include <stdio.h>
#endif

#if !UCONFIG_NO_COLLATION

#include "unicode/tblcoll.h"

enum OptionsList {
    UCOL_SIT_LANGUAGE = 0,
    UCOL_SIT_SCRIPT   = 1,
    UCOL_SIT_REGION   = 2,
    UCOL_SIT_VARIANT  = 3,
    UCOL_SIT_KEYWORD  = 4,
    UCOL_SIT_PROVIDER = 5,
    UCOL_SIT_LOCELEMENT_MAX = UCOL_SIT_PROVIDER, /* the last element that's part of LocElements */

    UCOL_SIT_BCP47,
    UCOL_SIT_STRENGTH,
    UCOL_SIT_CASE_LEVEL,
    UCOL_SIT_CASE_FIRST,
    UCOL_SIT_NUMERIC_COLLATION,
    UCOL_SIT_ALTERNATE_HANDLING,
    UCOL_SIT_NORMALIZATION_MODE,
    UCOL_SIT_FRENCH_COLLATION,
    UCOL_SIT_HIRAGANA_QUATERNARY,
    UCOL_SIT_VARIABLE_TOP,
    UCOL_SIT_VARIABLE_TOP_VALUE,
    UCOL_SIT_ITEMS_COUNT
};

/* option starters chars. */
static const char alternateHArg     = 'A';
static const char variableTopValArg = 'B';
static const char caseFirstArg      = 'C';
static const char numericCollArg    = 'D';
static const char caseLevelArg      = 'E';
static const char frenchCollArg     = 'F';
static const char hiraganaQArg      = 'H';
static const char keywordArg        = 'K';
static const char languageArg       = 'L';
static const char normArg           = 'N';
static const char providerArg       = 'P';
static const char regionArg         = 'R';
static const char strengthArg       = 'S';
static const char variableTopArg    = 'T';
static const char variantArg        = 'V';
static const char RFC3066Arg        = 'X';
static const char scriptArg         = 'Z';

static const char collationKeyword[]  = "@collation=";
static const char providerKeyword[]  = "@sp=";


static const int32_t locElementCount = UCOL_SIT_LOCELEMENT_MAX+1;
static const int32_t locElementCapacity = 32;
static const int32_t loc3066Capacity = 256;

/* structure containing specification of a collator. Initialized
 * from a short string. Also used to construct a short string from a
 * collator instance
 */
struct CollatorSpec {
    inline CollatorSpec();

    CharString locElements[locElementCount];
    CharString locale;
    UColAttributeValue options[UCOL_ATTRIBUTE_COUNT];
    uint32_t variableTopValue;
    char16_t variableTopString[locElementCapacity];
    int32_t variableTopStringLen;
    UBool variableTopSet;
    CharString entries[UCOL_SIT_ITEMS_COUNT];
};

CollatorSpec::CollatorSpec() :
locale(),
variableTopValue(0),
variableTopString(),
variableTopSet(false)
 {
    // set collation options to default
    for(int32_t i = 0; i < UCOL_ATTRIBUTE_COUNT; i++) {
        options[i] = UCOL_DEFAULT;
    }
}


/* structure for converting between character attribute
 * representation and real collation attribute value.
 */
struct AttributeConversion {
    char letter;
    UColAttributeValue value;
};

static const AttributeConversion conversions[12] = {
    { '1', UCOL_PRIMARY },
    { '2', UCOL_SECONDARY },
    { '3', UCOL_TERTIARY },
    { '4', UCOL_QUATERNARY },
    { 'D', UCOL_DEFAULT },
    { 'I', UCOL_IDENTICAL },
    { 'L', UCOL_LOWER_FIRST },
    { 'N', UCOL_NON_IGNORABLE },
    { 'O', UCOL_ON },
    { 'S', UCOL_SHIFTED },
    { 'U', UCOL_UPPER_FIRST },
    { 'X', UCOL_OFF }
};


static UColAttributeValue
ucol_sit_letterToAttributeValue(char letter, UErrorCode *status) {
    uint32_t i = 0;
    for(i = 0; i < UPRV_LENGTHOF(conversions); i++) {
        if(conversions[i].letter == letter) {
            return conversions[i].value;
        }
    }
    *status = U_ILLEGAL_ARGUMENT_ERROR;
#ifdef UCOL_TRACE_SIT
    fprintf(stderr, "%s:%d: unknown letter %c: %s\n", __FILE__, __LINE__, letter, u_errorName(*status));
#endif    
    return UCOL_DEFAULT;
}

/* function prototype for functions used to parse a short string */
U_CDECL_BEGIN
typedef const char* U_CALLCONV
ActionFunction(CollatorSpec *spec, uint32_t value1, const char* string,
               UErrorCode *status);
U_CDECL_END

U_CDECL_BEGIN
static const char* U_CALLCONV
_processLocaleElement(CollatorSpec *spec, uint32_t value, const char* string,
                      UErrorCode *status)
{
    do {
        if(value == UCOL_SIT_LANGUAGE || value == UCOL_SIT_KEYWORD || value == UCOL_SIT_PROVIDER) {
            spec->locElements[value].append(uprv_tolower(*string), *status);
        } else {
            spec->locElements[value].append(*string, *status);
        }
    } while(*(++string) != '_' && *string && U_SUCCESS(*status));
    // don't skip the underscore at the end
    return string;
}
U_CDECL_END

U_CDECL_BEGIN
static const char* U_CALLCONV
_processRFC3066Locale(CollatorSpec *spec, uint32_t, const char* string,
                      UErrorCode *status)
{
    char terminator = *string;
    string++;
    const char *end = uprv_strchr(string+1, terminator);
    if(end == nullptr || end - string >= loc3066Capacity) {
        *status = U_BUFFER_OVERFLOW_ERROR;
        return string;
    } else {
        spec->locale.copyFrom(CharString(string, static_cast<int32_t>(end-string), *status), *status);
        return end+1;
    }
}

U_CDECL_END

U_CDECL_BEGIN
static const char* U_CALLCONV
_processCollatorOption(CollatorSpec *spec, uint32_t option, const char* string,
                       UErrorCode *status)
{
    spec->options[option] = ucol_sit_letterToAttributeValue(*string, status);
    if((*(++string) != '_' && *string) || U_FAILURE(*status)) {
#ifdef UCOL_TRACE_SIT
    fprintf(stderr, "%s:%d: unknown collator option at '%s': %s\n", __FILE__, __LINE__, string, u_errorName(*status));
#endif    
        *status = U_ILLEGAL_ARGUMENT_ERROR;
    }
    return string;
}
U_CDECL_END


static char16_t
readHexCodeUnit(const char **string, UErrorCode *status)
{
    char16_t result = 0;
    int32_t value = 0;
    char c;
    int32_t noDigits = 0;
    while((c = **string) != 0 && noDigits < 4) {
        if( c >= '0' && c <= '9') {
            value = c - '0';
        } else if ( c >= 'a' && c <= 'f') {
            value = c - 'a' + 10;
        } else if ( c >= 'A' && c <= 'F') {
            value = c - 'A' + 10;
        } else {
            *status = U_ILLEGAL_ARGUMENT_ERROR;
#ifdef UCOL_TRACE_SIT
            fprintf(stderr, "%s:%d: Bad hex char at '%s': %s\n", __FILE__, __LINE__, *string, u_errorName(*status));
#endif    
            return 0;
        }
        result = (result << 4) | (char16_t)value;
        noDigits++;
        (*string)++;
    }
    // if the string was terminated before we read 4 digits, set an error
    if(noDigits < 4) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
#ifdef UCOL_TRACE_SIT
        fprintf(stderr, "%s:%d: Short (only %d digits, wanted 4) at '%s': %s\n", __FILE__, __LINE__, noDigits,*string, u_errorName(*status));
#endif    
    }
    return result;
}

U_CDECL_BEGIN
static const char* U_CALLCONV
_processVariableTop(CollatorSpec *spec, uint32_t value1, const char* string, UErrorCode *status)
{
    // get four digits
    int32_t i = 0;
    if(!value1) {
        while(U_SUCCESS(*status) && i < locElementCapacity && *string != 0 && *string != '_') {
            spec->variableTopString[i++] = readHexCodeUnit(&string, status);
        }
        spec->variableTopStringLen = i;
        if(i == locElementCapacity && *string != 0 && *string != '_') {
            *status = U_BUFFER_OVERFLOW_ERROR;
        }
    } else {
        spec->variableTopValue = readHexCodeUnit(&string, status);
    }
    if(U_SUCCESS(*status)) {
        spec->variableTopSet = true;
    }
    return string;
}
U_CDECL_END


/* Table for parsing short strings */
struct ShortStringOptions {
    char optionStart;
    ActionFunction *action;
    uint32_t attr;
};

static const ShortStringOptions options[UCOL_SIT_ITEMS_COUNT] =
{
/* 10 ALTERNATE_HANDLING */   {alternateHArg,     _processCollatorOption, UCOL_ALTERNATE_HANDLING }, // alternate  N, S, D
/* 15 VARIABLE_TOP_VALUE */   {variableTopValArg, _processVariableTop,    1 },
/* 08 CASE_FIRST */           {caseFirstArg,      _processCollatorOption, UCOL_CASE_FIRST }, // case first L, U, X, D
/* 09 NUMERIC_COLLATION */    {numericCollArg,    _processCollatorOption, UCOL_NUMERIC_COLLATION }, // codan      O, X, D
/* 07 CASE_LEVEL */           {caseLevelArg,      _processCollatorOption, UCOL_CASE_LEVEL }, // case level O, X, D
/* 12 FRENCH_COLLATION */     {frenchCollArg,     _processCollatorOption, UCOL_FRENCH_COLLATION }, // french     O, X, D
/* 13 HIRAGANA_QUATERNARY] */ {hiraganaQArg,      _processCollatorOption, UCOL_HIRAGANA_QUATERNARY_MODE }, // hiragana   O, X, D
/* 04 KEYWORD */              {keywordArg,        _processLocaleElement,  UCOL_SIT_KEYWORD }, // keyword
/* 00 LANGUAGE */             {languageArg,       _processLocaleElement,  UCOL_SIT_LANGUAGE }, // language
/* 11 NORMALIZATION_MODE */   {normArg,           _processCollatorOption, UCOL_NORMALIZATION_MODE }, // norm       O, X, D
/* 02 REGION */               {regionArg,         _processLocaleElement,  UCOL_SIT_REGION }, // region
/* 06 STRENGTH */             {strengthArg,       _processCollatorOption, UCOL_STRENGTH }, // strength   1, 2, 3, 4, I, D
/* 14 VARIABLE_TOP */         {variableTopArg,    _processVariableTop,    0 },
/* 03 VARIANT */              {variantArg,        _processLocaleElement,  UCOL_SIT_VARIANT }, // variant
/* 05 RFC3066BIS */           {RFC3066Arg,        _processRFC3066Locale,  0 }, // rfc3066bis locale name
/* 01 SCRIPT */               {scriptArg,         _processLocaleElement,  UCOL_SIT_SCRIPT },  // script
/*    PROVIDER */             {providerArg,       _processLocaleElement, UCOL_SIT_PROVIDER }
};


static
const char* ucol_sit_readOption(const char *start, CollatorSpec *spec,
                            UErrorCode *status)
{
  int32_t i = 0;

  for(i = 0; i < UCOL_SIT_ITEMS_COUNT; i++) {
      if(*start == options[i].optionStart) {
          const char* end = options[i].action(spec, options[i].attr, start+1, status);
#ifdef UCOL_TRACE_SIT
          fprintf(stderr, "***Set %d to %s...\n", i, start);
#endif
          // assume 'start' does not go away through all this
          spec->entries[i].copyFrom(CharString(start, (int32_t)(end - start), *status), *status);
          return end;
      }
  }
  *status = U_ILLEGAL_ARGUMENT_ERROR;
#ifdef UCOL_TRACE_SIT
  fprintf(stderr, "%s:%d: Unknown option at '%s': %s\n", __FILE__, __LINE__, start, u_errorName(*status));
#endif
  return start;
}

static const char*
ucol_sit_readSpecs(CollatorSpec *s, const char *string,
                        UParseError *parseError, UErrorCode *status)
{
    const char *definition = string;
    while(U_SUCCESS(*status) && *string) {
        string = ucol_sit_readOption(string, s, status);
        // advance over '_'
        while(*string && *string == '_') {
            string++;
        }
    }
    if(U_FAILURE(*status)) {
        parseError->offset = (int32_t)(string - definition);
    }
    return string;
}

static
int32_t ucol_sit_dumpSpecs(CollatorSpec *s, char *destination, int32_t capacity, UErrorCode *status)
{
    int32_t i = 0, j = 0;
    int32_t len = 0;
    char optName;
    if(U_SUCCESS(*status)) {
        for(i = 0; i < UCOL_SIT_ITEMS_COUNT; i++) {
            if(!s->entries[i].isEmpty()) {
                if(len) {
                    if(len < capacity) {
                        uprv_strcat(destination, "_");
                    }
                    len++;
                }
                optName = s->entries[i][0];
                if(optName == languageArg || optName == regionArg || optName == variantArg || optName == keywordArg) {
                    for(j = 0; j < s->entries[i].length(); j++) {
                        if(len + j < capacity) {
                            destination[len+j] = uprv_toupper(s->entries[i][j]);
                        }
                    }
                    len += s->entries[i].length();
                } else {
                    len += s->entries[i].extract(destination + len, capacity - len, *status);
                }
            }
        }
        return len;
    } else {
        return 0;
    }
}

static void
ucol_sit_calculateWholeLocale(CollatorSpec *s, UErrorCode &status) {
    // put the locale together, unless we have a done
    // locale
    if(s->locale.isEmpty()) {
        // first the language
        s->locale.append(s->locElements[UCOL_SIT_LANGUAGE], status);
        // then the script, if present
        if(!s->locElements[UCOL_SIT_SCRIPT].isEmpty()) {
            s->locale.append("_", status);
            s->locale.append(s->locElements[UCOL_SIT_SCRIPT], status);
        }
        // then the region, if present
        if(!s->locElements[UCOL_SIT_REGION].isEmpty()) {
            s->locale.append("_", status);
            s->locale.append(s->locElements[UCOL_SIT_REGION], status);
        } else if(!s->locElements[UCOL_SIT_VARIANT].isEmpty()) { // if there is a variant, we need an underscore
            s->locale.append("_", status);
        }
        // add variant, if there
        if(!s->locElements[UCOL_SIT_VARIANT].isEmpty()) {
            s->locale.append("_", status);
            s->locale.append(s->locElements[UCOL_SIT_VARIANT], status);
        }

        // if there is a collation keyword, add that too
        if(!s->locElements[UCOL_SIT_KEYWORD].isEmpty()) {
            s->locale.append(collationKeyword, status);
            s->locale.append(s->locElements[UCOL_SIT_KEYWORD], status);
        }

        // if there is a provider keyword, add that too
        if(!s->locElements[UCOL_SIT_PROVIDER].isEmpty()) {
            s->locale.append(providerKeyword, status);
            s->locale.append(s->locElements[UCOL_SIT_PROVIDER], status);
        }
    }
}


U_CAPI void U_EXPORT2
ucol_prepareShortStringOpen( const char *definition,
                          UBool,
                          UParseError *parseError,
                          UErrorCode *status)
{
    if(U_FAILURE(*status)) return;

    UParseError internalParseError;

    if(!parseError) {
        parseError = &internalParseError;
    }
    parseError->line = 0;
    parseError->offset = 0;
    parseError->preContext[0] = 0;
    parseError->postContext[0] = 0;


    // first we want to pick stuff out of short string.
    // we'll end up with an UCA version, locale and a bunch of
    // settings

    // analyse the string in order to get everything we need.
    CollatorSpec s;
    ucol_sit_readSpecs(&s, definition, parseError, status);
    ucol_sit_calculateWholeLocale(&s, *status);

    CharString buffer = ulocimp_canonicalize(s.locale.data(), *status);

    UResourceBundle *b = ures_open(U_ICUDATA_COLL, buffer.data(), status);
    /* we try to find stuff from keyword */
    UResourceBundle *collations = ures_getByKey(b, "collations", nullptr, status);
    UResourceBundle *collElem = nullptr;
    // if there is a keyword, we pick it up and try to get elements
    CharString keyBuffer = ulocimp_getKeywordValue(buffer.data(), "collation", *status);
    if(keyBuffer.isEmpty()) {
      // no keyword
      // we try to find the default setting, which will give us the keyword value
      UResourceBundle *defaultColl = ures_getByKeyWithFallback(collations, "default", nullptr, status);
      if(U_SUCCESS(*status)) {
        int32_t defaultKeyLen = 0;
        const char16_t *defaultKey = ures_getString(defaultColl, &defaultKeyLen, status);
        keyBuffer.appendInvariantChars(defaultKey, defaultKeyLen, *status);
      } else {
        *status = U_INTERNAL_PROGRAM_ERROR;
        return;
      }
      ures_close(defaultColl);
    }
    collElem = ures_getByKeyWithFallback(collations, keyBuffer.data(), collElem, status);
    ures_close(collElem);
    ures_close(collations);
    ures_close(b);
}


U_CAPI UCollator* U_EXPORT2
ucol_openFromShortString( const char *definition,
                          UBool forceDefaults,
                          UParseError *parseError,
                          UErrorCode *status)
{
    UTRACE_ENTRY_OC(UTRACE_UCOL_OPEN_FROM_SHORT_STRING);
    UTRACE_DATA1(UTRACE_INFO, "short string = \"%s\"", definition);

    if (U_FAILURE(*status)) return nullptr;

    UParseError internalParseError;

    if(!parseError) {
        parseError = &internalParseError;
    }
    parseError->line = 0;
    parseError->offset = 0;
    parseError->preContext[0] = 0;
    parseError->postContext[0] = 0;


    // first we want to pick stuff out of short string.
    // we'll end up with an UCA version, locale and a bunch of
    // settings

    // analyse the string in order to get everything we need.
    const char *string = definition;
    CollatorSpec s;
    string = ucol_sit_readSpecs(&s, definition, parseError, status);
    ucol_sit_calculateWholeLocale(&s, *status);

#ifdef UCOL_TRACE_SIT
    fprintf(stderr, "DEF %s, DATA %s, ERR %s\n", definition, s.locale.data(), u_errorName(*status));
#endif
    CharString buffer = ulocimp_canonicalize(s.locale.data(), *status);

    UCollator *result = ucol_open(buffer.data(), status);
    int32_t i = 0;

    for(i = 0; i < UCOL_ATTRIBUTE_COUNT; i++) {
        if(s.options[i] != UCOL_DEFAULT) {
            if(forceDefaults || ucol_getAttribute(result, (UColAttribute)i, status) != s.options[i]) {
                ucol_setAttribute(result, (UColAttribute)i, s.options[i], status);
            }

            if(U_FAILURE(*status)) {
                parseError->offset = (int32_t)(string - definition);
                ucol_close(result);
                return nullptr;
            }

        }
    }
    if(s.variableTopSet) {
        if(s.variableTopString[0]) {
            ucol_setVariableTop(result, s.variableTopString, s.variableTopStringLen, status);
        } else { // we set by value, using 'B'
            ucol_restoreVariableTop(result, s.variableTopValue, status);
        }
    }


    if(U_FAILURE(*status)) { // here it can only be a bogus value
        ucol_close(result);
        result = nullptr;
    }

    UTRACE_EXIT_PTR_STATUS(result, *status);
    return result;
}


U_CAPI int32_t U_EXPORT2
ucol_getShortDefinitionString(const UCollator *coll,
                              const char *locale,
                              char *dst,
                              int32_t capacity,
                              UErrorCode *status)
{
    if(U_FAILURE(*status)) return 0;
    if(coll == nullptr) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    return ((icu::Collator*)coll)->internalGetShortDefinitionString(locale,dst,capacity,*status);
}

U_CAPI int32_t U_EXPORT2
ucol_normalizeShortDefinitionString(const char *definition,
                                    char *destination,
                                    int32_t capacity,
                                    UParseError *parseError,
                                    UErrorCode *status)
{

    if(U_FAILURE(*status)) {
        return 0;
    }

    if(destination) {
        uprv_memset(destination, 0, capacity*sizeof(char));
    }

    UParseError pe;
    if(!parseError) {
        parseError = &pe;
    }

    // validate
    CollatorSpec s;
    ucol_sit_readSpecs(&s, definition, parseError, status);
    return ucol_sit_dumpSpecs(&s, destination, capacity, status);
}

/**
 * Get a set containing the contractions defined by the collator. The set includes
 * both the UCA contractions and the contractions defined by the collator
 * @param coll collator
 * @param conts the set to hold the result
 * @param status to hold the error code
 * @return the size of the contraction set
 */
U_CAPI int32_t U_EXPORT2
ucol_getContractions( const UCollator *coll,
                  USet *contractions,
                  UErrorCode *status)
{
  ucol_getContractionsAndExpansions(coll, contractions, nullptr, false, status);
  return uset_getItemCount(contractions);
}

/**
 * Get a set containing the expansions defined by the collator. The set includes
 * both the UCA expansions and the expansions defined by the tailoring
 * @param coll collator
 * @param conts the set to hold the result
 * @param addPrefixes add the prefix contextual elements to contractions
 * @param status to hold the error code
 *
 * @draft ICU 3.4
 */
U_CAPI void U_EXPORT2
ucol_getContractionsAndExpansions( const UCollator *coll,
                  USet *contractions,
                  USet *expansions,
                  UBool addPrefixes,
                  UErrorCode *status)
{
    if(U_FAILURE(*status)) {
        return;
    }
    if(coll == nullptr) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    const icu::RuleBasedCollator *rbc = icu::RuleBasedCollator::rbcFromUCollator(coll);
    if(rbc == nullptr) {
        *status = U_UNSUPPORTED_ERROR;
        return;
    }
    rbc->internalGetContractionsAndExpansions(
            icu::UnicodeSet::fromUSet(contractions),
            icu::UnicodeSet::fromUSet(expansions),
            addPrefixes, *status);
}
#endif
