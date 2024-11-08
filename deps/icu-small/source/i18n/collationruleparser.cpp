// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2013-2015, International Business Machines
* Corporation and others.  All Rights Reserved.
*******************************************************************************
* collationruleparser.cpp
*
* (replaced the former ucol_tok.cpp)
*
* created on: 2013apr10
* created by: Markus W. Scherer
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

#include "unicode/normalizer2.h"
#include "unicode/parseerr.h"
#include "unicode/uchar.h"
#include "unicode/ucol.h"
#include "unicode/uloc.h"
#include "unicode/unistr.h"
#include "unicode/utf16.h"
#include "charstr.h"
#include "cmemory.h"
#include "collation.h"
#include "collationdata.h"
#include "collationruleparser.h"
#include "collationsettings.h"
#include "collationtailoring.h"
#include "cstring.h"
#include "patternprops.h"
#include "uassert.h"
#include "ulocimp.h"
#include "uvectr32.h"

U_NAMESPACE_BEGIN

namespace {

const char16_t BEFORE[] = { 0x5b, 0x62, 0x65, 0x66, 0x6f, 0x72, 0x65, 0 };  // "[before"
const int32_t BEFORE_LENGTH = 7;

}  // namespace

CollationRuleParser::Sink::~Sink() {}

void
CollationRuleParser::Sink::suppressContractions(const UnicodeSet &, const char *&, UErrorCode &) {}

void
CollationRuleParser::Sink::optimize(const UnicodeSet &, const char *&, UErrorCode &) {}

CollationRuleParser::Importer::~Importer() {}

CollationRuleParser::CollationRuleParser(const CollationData *base, UErrorCode &errorCode)
        : nfd(*Normalizer2::getNFDInstance(errorCode)),
          nfc(*Normalizer2::getNFCInstance(errorCode)),
          rules(nullptr), baseData(base), settings(nullptr),
          parseError(nullptr), errorReason(nullptr),
          sink(nullptr), importer(nullptr),
          ruleIndex(0) {
}

CollationRuleParser::~CollationRuleParser() {
}

void
CollationRuleParser::parse(const UnicodeString &ruleString,
                           CollationSettings &outSettings,
                           UParseError *outParseError,
                           UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }
    settings = &outSettings;
    parseError = outParseError;
    if(parseError != nullptr) {
        parseError->line = 0;
        parseError->offset = -1;
        parseError->preContext[0] = 0;
        parseError->postContext[0] = 0;
    }
    errorReason = nullptr;
    parse(ruleString, errorCode);
}

void
CollationRuleParser::parse(const UnicodeString &ruleString, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }
    rules = &ruleString;
    ruleIndex = 0;

    while(ruleIndex < rules->length()) {
        char16_t c = rules->charAt(ruleIndex);
        if(PatternProps::isWhiteSpace(c)) {
            ++ruleIndex;
            continue;
        }
        switch(c) {
        case 0x26:  // '&'
            parseRuleChain(errorCode);
            break;
        case 0x5b:  // '['
            parseSetting(errorCode);
            break;
        case 0x23:  // '#' starts a comment, until the end of the line
            ruleIndex = skipComment(ruleIndex + 1);
            break;
        case 0x40:  // '@' is equivalent to [backwards 2]
            settings->setFlag(CollationSettings::BACKWARD_SECONDARY,
                              UCOL_ON, 0, errorCode);
            ++ruleIndex;
            break;
        case 0x21:  // '!' used to turn on Thai/Lao character reversal
            // Accept but ignore. The root collator has contractions
            // that are equivalent to the character reversal, where appropriate.
            ++ruleIndex;
            break;
        default:
            setParseError("expected a reset or setting or comment", errorCode);
            break;
        }
        if(U_FAILURE(errorCode)) { return; }
    }
}

void
CollationRuleParser::parseRuleChain(UErrorCode &errorCode) {
    int32_t resetStrength = parseResetAndPosition(errorCode);
    UBool isFirstRelation = true;
    for(;;) {
        int32_t result = parseRelationOperator(errorCode);
        if(U_FAILURE(errorCode)) { return; }
        if(result < 0) {
            if(ruleIndex < rules->length() && rules->charAt(ruleIndex) == 0x23) {
                // '#' starts a comment, until the end of the line
                ruleIndex = skipComment(ruleIndex + 1);
                continue;
            }
            if(isFirstRelation) {
                setParseError("reset not followed by a relation", errorCode);
            }
            return;
        }
        int32_t strength = result & STRENGTH_MASK;
        if(resetStrength < UCOL_IDENTICAL) {
            // reset-before rule chain
            if(isFirstRelation) {
                if(strength != resetStrength) {
                    setParseError("reset-before strength differs from its first relation", errorCode);
                    return;
                }
            } else {
                if(strength < resetStrength) {
                    setParseError("reset-before strength followed by a stronger relation", errorCode);
                    return;
                }
            }
        }
        int32_t i = ruleIndex + (result >> OFFSET_SHIFT);  // skip over the relation operator
        if((result & STARRED_FLAG) == 0) {
            parseRelationStrings(strength, i, errorCode);
        } else {
            parseStarredCharacters(strength, i, errorCode);
        }
        if(U_FAILURE(errorCode)) { return; }
        isFirstRelation = false;
    }
}

int32_t
CollationRuleParser::parseResetAndPosition(UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return UCOL_DEFAULT; }
    int32_t i = skipWhiteSpace(ruleIndex + 1);
    int32_t j;
    char16_t c;
    int32_t resetStrength;
    if(rules->compare(i, BEFORE_LENGTH, BEFORE, 0, BEFORE_LENGTH) == 0 &&
            (j = i + BEFORE_LENGTH) < rules->length() &&
            PatternProps::isWhiteSpace(rules->charAt(j)) &&
            ((j = skipWhiteSpace(j + 1)) + 1) < rules->length() &&
            0x31 <= (c = rules->charAt(j)) && c <= 0x33 &&
            rules->charAt(j + 1) == 0x5d) {
        // &[before n] with n=1 or 2 or 3
        resetStrength = UCOL_PRIMARY + (c - 0x31);
        i = skipWhiteSpace(j + 2);
    } else {
        resetStrength = UCOL_IDENTICAL;
    }
    if(i >= rules->length()) {
        setParseError("reset without position", errorCode);
        return UCOL_DEFAULT;
    }
    UnicodeString str;
    if(rules->charAt(i) == 0x5b) {  // '['
        i = parseSpecialPosition(i, str, errorCode);
    } else {
        i = parseTailoringString(i, str, errorCode);
    }
    sink->addReset(resetStrength, str, errorReason, errorCode);
    if(U_FAILURE(errorCode)) { setErrorContext(); }
    ruleIndex = i;
    return resetStrength;
}

int32_t
CollationRuleParser::parseRelationOperator(UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return UCOL_DEFAULT; }
    ruleIndex = skipWhiteSpace(ruleIndex);
    if(ruleIndex >= rules->length()) { return UCOL_DEFAULT; }
    int32_t strength;
    int32_t i = ruleIndex;
    char16_t c = rules->charAt(i++);
    switch(c) {
    case 0x3c:  // '<'
        if(i < rules->length() && rules->charAt(i) == 0x3c) {  // <<
            ++i;
            if(i < rules->length() && rules->charAt(i) == 0x3c) {  // <<<
                ++i;
                if(i < rules->length() && rules->charAt(i) == 0x3c) {  // <<<<
                    ++i;
                    strength = UCOL_QUATERNARY;
                } else {
                    strength = UCOL_TERTIARY;
                }
            } else {
                strength = UCOL_SECONDARY;
            }
        } else {
            strength = UCOL_PRIMARY;
        }
        if(i < rules->length() && rules->charAt(i) == 0x2a) {  // '*'
            ++i;
            strength |= STARRED_FLAG;
        }
        break;
    case 0x3b:  // ';' same as <<
        strength = UCOL_SECONDARY;
        break;
    case 0x2c:  // ',' same as <<<
        strength = UCOL_TERTIARY;
        break;
    case 0x3d:  // '='
        strength = UCOL_IDENTICAL;
        if(i < rules->length() && rules->charAt(i) == 0x2a) {  // '*'
            ++i;
            strength |= STARRED_FLAG;
        }
        break;
    default:
        return UCOL_DEFAULT;
    }
    return ((i - ruleIndex) << OFFSET_SHIFT) | strength;
}

void
CollationRuleParser::parseRelationStrings(int32_t strength, int32_t i, UErrorCode &errorCode) {
    // Parse
    //     prefix | str / extension
    // where prefix and extension are optional.
    UnicodeString prefix, str, extension;
    i = parseTailoringString(i, str, errorCode);
    if(U_FAILURE(errorCode)) { return; }
    char16_t next = (i < rules->length()) ? rules->charAt(i) : 0;
    if(next == 0x7c) {  // '|' separates the context prefix from the string.
        prefix = str;
        i = parseTailoringString(i + 1, str, errorCode);
        if(U_FAILURE(errorCode)) { return; }
        next = (i < rules->length()) ? rules->charAt(i) : 0;
    }
    if(next == 0x2f) {  // '/' separates the string from the extension.
        i = parseTailoringString(i + 1, extension, errorCode);
    }
    if(!prefix.isEmpty()) {
        UChar32 prefix0 = prefix.char32At(0);
        UChar32 c = str.char32At(0);
        if(!nfc.hasBoundaryBefore(prefix0) || !nfc.hasBoundaryBefore(c)) {
            setParseError("in 'prefix|str', prefix and str must each start with an NFC boundary",
                          errorCode);
            return;
        }
    }
    sink->addRelation(strength, prefix, str, extension, errorReason, errorCode);
    if(U_FAILURE(errorCode)) { setErrorContext(); }
    ruleIndex = i;
}

void
CollationRuleParser::parseStarredCharacters(int32_t strength, int32_t i, UErrorCode &errorCode) {
    UnicodeString empty, raw;
    i = parseString(skipWhiteSpace(i), raw, errorCode);
    if(U_FAILURE(errorCode)) { return; }
    if(raw.isEmpty()) {
        setParseError("missing starred-relation string", errorCode);
        return;
    }
    UChar32 prev = -1;
    int32_t j = 0;
    for(;;) {
        while(j < raw.length()) {
            UChar32 c = raw.char32At(j);
            if(!nfd.isInert(c)) {
                setParseError("starred-relation string is not all NFD-inert", errorCode);
                return;
            }
            sink->addRelation(strength, empty, UnicodeString(c), empty, errorReason, errorCode);
            if(U_FAILURE(errorCode)) {
                setErrorContext();
                return;
            }
            j += U16_LENGTH(c);
            prev = c;
        }
        if(i >= rules->length() || rules->charAt(i) != 0x2d) {  // '-'
            break;
        }
        if(prev < 0) {
            setParseError("range without start in starred-relation string", errorCode);
            return;
        }
        i = parseString(i + 1, raw, errorCode);
        if(U_FAILURE(errorCode)) { return; }
        if(raw.isEmpty()) {
            setParseError("range without end in starred-relation string", errorCode);
            return;
        }
        UChar32 c = raw.char32At(0);
        if(c < prev) {
            setParseError("range start greater than end in starred-relation string", errorCode);
            return;
        }
        // range prev-c
        UnicodeString s;
        while(++prev <= c) {
            if(!nfd.isInert(prev)) {
                setParseError("starred-relation string range is not all NFD-inert", errorCode);
                return;
            }
            if(U_IS_SURROGATE(prev)) {
                setParseError("starred-relation string range contains a surrogate", errorCode);
                return;
            }
            if(0xfffd <= prev && prev <= 0xffff) {
                setParseError("starred-relation string range contains U+FFFD, U+FFFE or U+FFFF", errorCode);
                return;
            }
            s.setTo(prev);
            sink->addRelation(strength, empty, s, empty, errorReason, errorCode);
            if(U_FAILURE(errorCode)) {
                setErrorContext();
                return;
            }
        }
        prev = -1;
        j = U16_LENGTH(c);
    }
    ruleIndex = skipWhiteSpace(i);
}

int32_t
CollationRuleParser::parseTailoringString(int32_t i, UnicodeString &raw, UErrorCode &errorCode) {
    i = parseString(skipWhiteSpace(i), raw, errorCode);
    if(U_SUCCESS(errorCode) && raw.isEmpty()) {
        setParseError("missing relation string", errorCode);
    }
    return skipWhiteSpace(i);
}

int32_t
CollationRuleParser::parseString(int32_t i, UnicodeString &raw, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return i; }
    raw.remove();
    while(i < rules->length()) {
        UChar32 c = rules->charAt(i++);
        if(isSyntaxChar(c)) {
            if(c == 0x27) {  // apostrophe
                if(i < rules->length() && rules->charAt(i) == 0x27) {
                    // Double apostrophe, encodes a single one.
                    raw.append(static_cast<char16_t>(0x27));
                    ++i;
                    continue;
                }
                // Quote literal text until the next single apostrophe.
                for(;;) {
                    if(i == rules->length()) {
                        setParseError("quoted literal text missing terminating apostrophe", errorCode);
                        return i;
                    }
                    c = rules->charAt(i++);
                    if(c == 0x27) {
                        if(i < rules->length() && rules->charAt(i) == 0x27) {
                            // Double apostrophe inside quoted literal text,
                            // still encodes a single apostrophe.
                            ++i;
                        } else {
                            break;
                        }
                    }
                    raw.append(static_cast<char16_t>(c));
                }
            } else if(c == 0x5c) {  // backslash
                if(i == rules->length()) {
                    setParseError("backslash escape at the end of the rule string", errorCode);
                    return i;
                }
                c = rules->char32At(i);
                raw.append(c);
                i += U16_LENGTH(c);
            } else {
                // Any other syntax character terminates a string.
                --i;
                break;
            }
        } else if(PatternProps::isWhiteSpace(c)) {
            // Unquoted white space terminates a string.
            --i;
            break;
        } else {
            raw.append(static_cast<char16_t>(c));
        }
    }
    for(int32_t j = 0; j < raw.length();) {
        UChar32 c = raw.char32At(j);
        if(U_IS_SURROGATE(c)) {
            setParseError("string contains an unpaired surrogate", errorCode);
            return i;
        }
        if(0xfffd <= c && c <= 0xffff) {
            setParseError("string contains U+FFFD, U+FFFE or U+FFFF", errorCode);
            return i;
        }
        j += U16_LENGTH(c);
    }
    return i;
}

namespace {

const char* const positions[] = {
    "first tertiary ignorable",
    "last tertiary ignorable",
    "first secondary ignorable",
    "last secondary ignorable",
    "first primary ignorable",
    "last primary ignorable",
    "first variable",
    "last variable",
    "first regular",
    "last regular",
    "first implicit",
    "last implicit",
    "first trailing",
    "last trailing"
};

}  // namespace

int32_t
CollationRuleParser::parseSpecialPosition(int32_t i, UnicodeString &str, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return 0; }
    UnicodeString raw;
    int32_t j = readWords(i + 1, raw);
    if(j > i && rules->charAt(j) == 0x5d && !raw.isEmpty()) {  // words end with ]
        ++j;
        for(int32_t pos = 0; pos < UPRV_LENGTHOF(positions); ++pos) {
            if(raw == UnicodeString(positions[pos], -1, US_INV)) {
                str.setTo(POS_LEAD).append(static_cast<char16_t>(POS_BASE + pos));
                return j;
            }
        }
        if(raw == UNICODE_STRING_SIMPLE("top")) {
            str.setTo(POS_LEAD).append(static_cast<char16_t>(POS_BASE + LAST_REGULAR));
            return j;
        }
        if(raw == UNICODE_STRING_SIMPLE("variable top")) {
            str.setTo(POS_LEAD).append(static_cast<char16_t>(POS_BASE + LAST_VARIABLE));
            return j;
        }
    }
    setParseError("not a valid special reset position", errorCode);
    return i;
}

void
CollationRuleParser::parseSetting(UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }
    UnicodeString raw;
    int32_t i = ruleIndex + 1;
    int32_t j = readWords(i, raw);
    if(j <= i || raw.isEmpty()) {
        setParseError("expected a setting/option at '['", errorCode);
    }
    if(rules->charAt(j) == 0x5d) {  // words end with ]
        ++j;
        if(raw.startsWith(UNICODE_STRING_SIMPLE("reorder")) &&
                (raw.length() == 7 || raw.charAt(7) == 0x20)) {
            parseReordering(raw, errorCode);
            ruleIndex = j;
            return;
        }
        if(raw == UNICODE_STRING_SIMPLE("backwards 2")) {
            settings->setFlag(CollationSettings::BACKWARD_SECONDARY,
                              UCOL_ON, 0, errorCode);
            ruleIndex = j;
            return;
        }
        UnicodeString v;
        int32_t valueIndex = raw.lastIndexOf(static_cast<char16_t>(0x20));
        if(valueIndex >= 0) {
            v.setTo(raw, valueIndex + 1);
            raw.truncate(valueIndex);
        }
        if(raw == UNICODE_STRING_SIMPLE("strength") && v.length() == 1) {
            int32_t value = UCOL_DEFAULT;
            char16_t c = v.charAt(0);
            if(0x31 <= c && c <= 0x34) {  // 1..4
                value = UCOL_PRIMARY + (c - 0x31);
            } else if(c == 0x49) {  // 'I'
                value = UCOL_IDENTICAL;
            }
            if(value != UCOL_DEFAULT) {
                settings->setStrength(value, 0, errorCode);
                ruleIndex = j;
                return;
            }
        } else if(raw == UNICODE_STRING_SIMPLE("alternate")) {
            UColAttributeValue value = UCOL_DEFAULT;
            if(v == UNICODE_STRING_SIMPLE("non-ignorable")) {
                value = UCOL_NON_IGNORABLE;
            } else if(v == UNICODE_STRING_SIMPLE("shifted")) {
                value = UCOL_SHIFTED;
            }
            if(value != UCOL_DEFAULT) {
                settings->setAlternateHandling(value, 0, errorCode);
                ruleIndex = j;
                return;
            }
        } else if(raw == UNICODE_STRING_SIMPLE("maxVariable")) {
            int32_t value = UCOL_DEFAULT;
            if(v == UNICODE_STRING_SIMPLE("space")) {
                value = CollationSettings::MAX_VAR_SPACE;
            } else if(v == UNICODE_STRING_SIMPLE("punct")) {
                value = CollationSettings::MAX_VAR_PUNCT;
            } else if(v == UNICODE_STRING_SIMPLE("symbol")) {
                value = CollationSettings::MAX_VAR_SYMBOL;
            } else if(v == UNICODE_STRING_SIMPLE("currency")) {
                value = CollationSettings::MAX_VAR_CURRENCY;
            }
            if(value != UCOL_DEFAULT) {
                settings->setMaxVariable(value, 0, errorCode);
                settings->variableTop = baseData->getLastPrimaryForGroup(
                    UCOL_REORDER_CODE_FIRST + value);
                U_ASSERT(settings->variableTop != 0);
                ruleIndex = j;
                return;
            }
        } else if(raw == UNICODE_STRING_SIMPLE("caseFirst")) {
            UColAttributeValue value = UCOL_DEFAULT;
            if(v == UNICODE_STRING_SIMPLE("off")) {
                value = UCOL_OFF;
            } else if(v == UNICODE_STRING_SIMPLE("lower")) {
                value = UCOL_LOWER_FIRST;
            } else if(v == UNICODE_STRING_SIMPLE("upper")) {
                value = UCOL_UPPER_FIRST;
            }
            if(value != UCOL_DEFAULT) {
                settings->setCaseFirst(value, 0, errorCode);
                ruleIndex = j;
                return;
            }
        } else if(raw == UNICODE_STRING_SIMPLE("caseLevel")) {
            UColAttributeValue value = getOnOffValue(v);
            if(value != UCOL_DEFAULT) {
                settings->setFlag(CollationSettings::CASE_LEVEL, value, 0, errorCode);
                ruleIndex = j;
                return;
            }
        } else if(raw == UNICODE_STRING_SIMPLE("normalization")) {
            UColAttributeValue value = getOnOffValue(v);
            if(value != UCOL_DEFAULT) {
                settings->setFlag(CollationSettings::CHECK_FCD, value, 0, errorCode);
                ruleIndex = j;
                return;
            }
        } else if(raw == UNICODE_STRING_SIMPLE("numericOrdering")) {
            UColAttributeValue value = getOnOffValue(v);
            if(value != UCOL_DEFAULT) {
                settings->setFlag(CollationSettings::NUMERIC, value, 0, errorCode);
                ruleIndex = j;
                return;
            }
        } else if(raw == UNICODE_STRING_SIMPLE("hiraganaQ")) {
            UColAttributeValue value = getOnOffValue(v);
            if(value != UCOL_DEFAULT) {
                if(value == UCOL_ON) {
                    setParseError("[hiraganaQ on] is not supported", errorCode);
                }
                ruleIndex = j;
                return;
            }
        } else if(raw == UNICODE_STRING_SIMPLE("import")) {
            CharString lang;
            lang.appendInvariantChars(v, errorCode);
            if(errorCode == U_MEMORY_ALLOCATION_ERROR) { return; }
            // BCP 47 language tag -> ICU locale ID
            int32_t parsedLength;
            CharString localeID = ulocimp_forLanguageTag(lang.data(), -1, &parsedLength, errorCode);
            if(U_FAILURE(errorCode) || parsedLength != lang.length()) {
                errorCode = U_ZERO_ERROR;
                setParseError("expected language tag in [import langTag]", errorCode);
                return;
            }
            // localeID minus all keywords
            char baseID[ULOC_FULLNAME_CAPACITY];
            int32_t length = uloc_getBaseName(localeID.data(), baseID, ULOC_FULLNAME_CAPACITY, &errorCode);
            if(U_FAILURE(errorCode) || length >= ULOC_KEYWORDS_CAPACITY) {
                errorCode = U_ZERO_ERROR;
                setParseError("expected language tag in [import langTag]", errorCode);
                return;
            }
            if(length == 0) {
                uprv_strcpy(baseID, "root");
            } else if(*baseID == '_') {
                uprv_memmove(baseID + 3, baseID, length + 1);
                uprv_memcpy(baseID, "und", 3);
            }
            // @collation=type, or length=0 if not specified
            CharString collationType = ulocimp_getKeywordValue(localeID.data(), "collation", errorCode);
            if(U_FAILURE(errorCode)) {
                errorCode = U_ZERO_ERROR;
                setParseError("expected language tag in [import langTag]", errorCode);
                return;
            }
            if(importer == nullptr) {
                setParseError("[import langTag] is not supported", errorCode);
            } else {
                UnicodeString importedRules;
                importer->getRules(baseID,
                                   !collationType.isEmpty() ? collationType.data() : "standard",
                                   importedRules, errorReason, errorCode);
                if(U_FAILURE(errorCode)) {
                    if(errorReason == nullptr) {
                        errorReason = "[import langTag] failed";
                    }
                    setErrorContext();
                    return;
                }
                const UnicodeString *outerRules = rules;
                int32_t outerRuleIndex = ruleIndex;
                parse(importedRules, errorCode);
                if(U_FAILURE(errorCode)) {
                    if(parseError != nullptr) {
                        parseError->offset = outerRuleIndex;
                    }
                }
                rules = outerRules;
                ruleIndex = j;
            }
            return;
        }
    } else if(rules->charAt(j) == 0x5b) {  // words end with [
        UnicodeSet set;
        j = parseUnicodeSet(j, set, errorCode);
        if(U_FAILURE(errorCode)) { return; }
        if(raw == UNICODE_STRING_SIMPLE("optimize")) {
            sink->optimize(set, errorReason, errorCode);
            if(U_FAILURE(errorCode)) { setErrorContext(); }
            ruleIndex = j;
            return;
        } else if(raw == UNICODE_STRING_SIMPLE("suppressContractions")) {
            sink->suppressContractions(set, errorReason, errorCode);
            if(U_FAILURE(errorCode)) { setErrorContext(); }
            ruleIndex = j;
            return;
        }
    }
    setParseError("not a valid setting/option", errorCode);
}

void
CollationRuleParser::parseReordering(const UnicodeString &raw, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }
    int32_t i = 7;  // after "reorder"
    if(i == raw.length()) {
        // empty [reorder] with no codes
        settings->resetReordering();
        return;
    }
    // Parse the codes in [reorder aa bb cc].
    UVector32 reorderCodes(errorCode);
    if(U_FAILURE(errorCode)) { return; }
    CharString word;
    while(i < raw.length()) {
        ++i;  // skip the word-separating space
        int32_t limit = raw.indexOf(static_cast<char16_t>(0x20), i);
        if(limit < 0) { limit = raw.length(); }
        word.clear().appendInvariantChars(raw.tempSubStringBetween(i, limit), errorCode);
        if(U_FAILURE(errorCode)) { return; }
        int32_t code = getReorderCode(word.data());
        if(code < 0) {
            setParseError("unknown script or reorder code", errorCode);
            return;
        }
        reorderCodes.addElement(code, errorCode);
        if(U_FAILURE(errorCode)) { return; }
        i = limit;
    }
    settings->setReordering(*baseData, reorderCodes.getBuffer(), reorderCodes.size(), errorCode);
}

static const char *const gSpecialReorderCodes[] = {
    "space", "punct", "symbol", "currency", "digit"
};

int32_t
CollationRuleParser::getReorderCode(const char *word) {
    for(int32_t i = 0; i < UPRV_LENGTHOF(gSpecialReorderCodes); ++i) {
        if(uprv_stricmp(word, gSpecialReorderCodes[i]) == 0) {
            return UCOL_REORDER_CODE_FIRST + i;
        }
    }
    int32_t script = u_getPropertyValueEnum(UCHAR_SCRIPT, word);
    if(script >= 0) {
        return script;
    }
    if(uprv_stricmp(word, "others") == 0) {
        return UCOL_REORDER_CODE_OTHERS;  // same as Zzzz = USCRIPT_UNKNOWN
    }
    return -1;
}

UColAttributeValue
CollationRuleParser::getOnOffValue(const UnicodeString &s) {
    if(s == UNICODE_STRING_SIMPLE("on")) {
        return UCOL_ON;
    } else if(s == UNICODE_STRING_SIMPLE("off")) {
        return UCOL_OFF;
    } else {
        return UCOL_DEFAULT;
    }
}

int32_t
CollationRuleParser::parseUnicodeSet(int32_t i, UnicodeSet &set, UErrorCode &errorCode) {
    // Collect a UnicodeSet pattern between a balanced pair of [brackets].
    int32_t level = 0;
    int32_t j = i;
    for(;;) {
        if(j == rules->length()) {
            setParseError("unbalanced UnicodeSet pattern brackets", errorCode);
            return j;
        }
        char16_t c = rules->charAt(j++);
        if(c == 0x5b) {  // '['
            ++level;
        } else if(c == 0x5d) {  // ']'
            if(--level == 0) { break; }
        }
    }
    set.applyPattern(rules->tempSubStringBetween(i, j), errorCode);
    if(U_FAILURE(errorCode)) {
        errorCode = U_ZERO_ERROR;
        setParseError("not a valid UnicodeSet pattern", errorCode);
        return j;
    }
    j = skipWhiteSpace(j);
    if(j == rules->length() || rules->charAt(j) != 0x5d) {
        setParseError("missing option-terminating ']' after UnicodeSet pattern", errorCode);
        return j;
    }
    return ++j;
}

int32_t
CollationRuleParser::readWords(int32_t i, UnicodeString &raw) const {
    static const char16_t sp = 0x20;
    raw.remove();
    i = skipWhiteSpace(i);
    for(;;) {
        if(i >= rules->length()) { return 0; }
        char16_t c = rules->charAt(i);
        if(isSyntaxChar(c) && c != 0x2d && c != 0x5f) {  // syntax except -_
            if(raw.isEmpty()) { return i; }
            if(raw.endsWith(&sp, 1)) {  // remove trailing space
                raw.truncate(raw.length() - 1);
            }
            return i;
        }
        if(PatternProps::isWhiteSpace(c)) {
            raw.append(sp);
            i = skipWhiteSpace(i + 1);
        } else {
            raw.append(c);
            ++i;
        }
    }
}

int32_t
CollationRuleParser::skipComment(int32_t i) const {
    // skip to past the newline
    while(i < rules->length()) {
        char16_t c = rules->charAt(i++);
        // LF or FF or CR or NEL or LS or PS
        if(c == 0xa || c == 0xc || c == 0xd || c == 0x85 || c == 0x2028 || c == 0x2029) {
            // Unicode Newline Guidelines: "A readline function should stop at NLF, LS, FF, or PS."
            // NLF (new line function) = CR or LF or CR+LF or NEL.
            // No need to collect all of CR+LF because a following LF will be ignored anyway.
            break;
        }
    }
    return i;
}

void
CollationRuleParser::setParseError(const char *reason, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }
    // Error code consistent with the old parser (from ca. 2001),
    // rather than U_PARSE_ERROR;
    errorCode = U_INVALID_FORMAT_ERROR;
    errorReason = reason;
    if(parseError != nullptr) { setErrorContext(); }
}

void
CollationRuleParser::setErrorContext() {
    if(parseError == nullptr) { return; }

    // Note: This relies on the calling code maintaining the ruleIndex
    // at a position that is useful for debugging.
    // For example, at the beginning of a reset or relation etc.
    parseError->offset = ruleIndex;
    parseError->line = 0;  // We are not counting line numbers.

    // before ruleIndex
    int32_t start = ruleIndex - (U_PARSE_CONTEXT_LEN - 1);
    if(start < 0) {
        start = 0;
    } else if(start > 0 && U16_IS_TRAIL(rules->charAt(start))) {
        ++start;
    }
    int32_t length = ruleIndex - start;
    rules->extract(start, length, parseError->preContext);
    parseError->preContext[length] = 0;

    // starting from ruleIndex
    length = rules->length() - ruleIndex;
    if(length >= U_PARSE_CONTEXT_LEN) {
        length = U_PARSE_CONTEXT_LEN - 1;
        if(U16_IS_LEAD(rules->charAt(ruleIndex + length - 1))) {
            --length;
        }
    }
    rules->extract(ruleIndex, length, parseError->postContext);
    parseError->postContext[length] = 0;
}

UBool
CollationRuleParser::isSyntaxChar(UChar32 c) {
    return 0x21 <= c && c <= 0x7e &&
            (c <= 0x2f || (0x3a <= c && c <= 0x40) ||
            (0x5b <= c && c <= 0x60) || (0x7b <= c));
}

int32_t
CollationRuleParser::skipWhiteSpace(int32_t i) const {
    while(i < rules->length() && PatternProps::isWhiteSpace(rules->charAt(i))) {
        ++i;
    }
    return i;
}

U_NAMESPACE_END

#endif  // !UCONFIG_NO_COLLATION
