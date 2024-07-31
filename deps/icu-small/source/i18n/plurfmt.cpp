// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2009-2015, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*
* File PLURFMT.CPP
*******************************************************************************
*/

#include "unicode/decimfmt.h"
#include "unicode/messagepattern.h"
#include "unicode/plurfmt.h"
#include "unicode/plurrule.h"
#include "unicode/utypes.h"
#include "cmemory.h"
#include "messageimpl.h"
#include "nfrule.h"
#include "plurrule_impl.h"
#include "uassert.h"
#include "uhash.h"
#include "number_decimalquantity.h"
#include "number_utils.h"
#include "number_utypes.h"

#if !UCONFIG_NO_FORMATTING

U_NAMESPACE_BEGIN

using number::impl::DecimalQuantity;

static const char16_t OTHER_STRING[] = {
    0x6F, 0x74, 0x68, 0x65, 0x72, 0  // "other"
};

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(PluralFormat)

PluralFormat::PluralFormat(UErrorCode& status)
        : locale(Locale::getDefault()),
          msgPattern(status),
          numberFormat(nullptr),
          offset(0) {
    init(nullptr, UPLURAL_TYPE_CARDINAL, status);
}

PluralFormat::PluralFormat(const Locale& loc, UErrorCode& status)
        : locale(loc),
          msgPattern(status),
          numberFormat(nullptr),
          offset(0) {
    init(nullptr, UPLURAL_TYPE_CARDINAL, status);
}

PluralFormat::PluralFormat(const PluralRules& rules, UErrorCode& status)
        : locale(Locale::getDefault()),
          msgPattern(status),
          numberFormat(nullptr),
          offset(0) {
    init(&rules, UPLURAL_TYPE_COUNT, status);
}

PluralFormat::PluralFormat(const Locale& loc,
                           const PluralRules& rules,
                           UErrorCode& status)
        : locale(loc),
          msgPattern(status),
          numberFormat(nullptr),
          offset(0) {
    init(&rules, UPLURAL_TYPE_COUNT, status);
}

PluralFormat::PluralFormat(const Locale& loc,
                           UPluralType type,
                           UErrorCode& status)
        : locale(loc),
          msgPattern(status),
          numberFormat(nullptr),
          offset(0) {
    init(nullptr, type, status);
}

PluralFormat::PluralFormat(const UnicodeString& pat,
                           UErrorCode& status)
        : locale(Locale::getDefault()),
          msgPattern(status),
          numberFormat(nullptr),
          offset(0) {
    init(nullptr, UPLURAL_TYPE_CARDINAL, status);
    applyPattern(pat, status);
}

PluralFormat::PluralFormat(const Locale& loc,
                           const UnicodeString& pat,
                           UErrorCode& status)
        : locale(loc),
          msgPattern(status),
          numberFormat(nullptr),
          offset(0) {
    init(nullptr, UPLURAL_TYPE_CARDINAL, status);
    applyPattern(pat, status);
}

PluralFormat::PluralFormat(const PluralRules& rules,
                           const UnicodeString& pat,
                           UErrorCode& status)
        : locale(Locale::getDefault()),
          msgPattern(status),
          numberFormat(nullptr),
          offset(0) {
    init(&rules, UPLURAL_TYPE_COUNT, status);
    applyPattern(pat, status);
}

PluralFormat::PluralFormat(const Locale& loc,
                           const PluralRules& rules,
                           const UnicodeString& pat,
                           UErrorCode& status)
        : locale(loc),
          msgPattern(status),
          numberFormat(nullptr),
          offset(0) {
    init(&rules, UPLURAL_TYPE_COUNT, status);
    applyPattern(pat, status);
}

PluralFormat::PluralFormat(const Locale& loc,
                           UPluralType type,
                           const UnicodeString& pat,
                           UErrorCode& status)
        : locale(loc),
          msgPattern(status),
          numberFormat(nullptr),
          offset(0) {
    init(nullptr, type, status);
    applyPattern(pat, status);
}

PluralFormat::PluralFormat(const PluralFormat& other)
        : Format(other),
          locale(other.locale),
          msgPattern(other.msgPattern),
          numberFormat(nullptr),
          offset(other.offset) {
    copyObjects(other);
}

void
PluralFormat::copyObjects(const PluralFormat& other) {
    UErrorCode status = U_ZERO_ERROR;
    delete numberFormat;
    delete pluralRulesWrapper.pluralRules;
    if (other.numberFormat == nullptr) {
        numberFormat = NumberFormat::createInstance(locale, status);
    } else {
        numberFormat = other.numberFormat->clone();
    }
    if (other.pluralRulesWrapper.pluralRules == nullptr) {
        pluralRulesWrapper.pluralRules = PluralRules::forLocale(locale, status);
    } else {
        pluralRulesWrapper.pluralRules = other.pluralRulesWrapper.pluralRules->clone();
    }
}


PluralFormat::~PluralFormat() {
    delete numberFormat;
}

void
PluralFormat::init(const PluralRules* rules, UPluralType type, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }

    if (rules==nullptr) {
        pluralRulesWrapper.pluralRules = PluralRules::forLocale(locale, type, status);
    } else {
        pluralRulesWrapper.pluralRules = rules->clone();
        if (pluralRulesWrapper.pluralRules == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
    }

    numberFormat= NumberFormat::createInstance(locale, status);
}

void
PluralFormat::applyPattern(const UnicodeString& newPattern, UErrorCode& status) {
    msgPattern.parsePluralStyle(newPattern, nullptr, status);
    if (U_FAILURE(status)) {
        msgPattern.clear();
        offset = 0;
        return;
    }
    offset = msgPattern.getPluralOffset(0);
}

UnicodeString&
PluralFormat::format(const Formattable& obj,
                   UnicodeString& appendTo,
                   FieldPosition& pos,
                   UErrorCode& status) const
{
    if (U_FAILURE(status)) return appendTo;

    if (obj.isNumeric()) {
        return format(obj, obj.getDouble(), appendTo, pos, status);
    } else {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return appendTo;
    }
}

UnicodeString
PluralFormat::format(int32_t number, UErrorCode& status) const {
    FieldPosition fpos(FieldPosition::DONT_CARE);
    UnicodeString result;
    return format(Formattable(number), number, result, fpos, status);
}

UnicodeString
PluralFormat::format(double number, UErrorCode& status) const {
    FieldPosition fpos(FieldPosition::DONT_CARE);
    UnicodeString result;
    return format(Formattable(number), number, result, fpos, status);
}


UnicodeString&
PluralFormat::format(int32_t number,
                     UnicodeString& appendTo,
                     FieldPosition& pos,
                     UErrorCode& status) const {
    return format(Formattable(number), (double)number, appendTo, pos, status);
}

UnicodeString&
PluralFormat::format(double number,
                     UnicodeString& appendTo,
                     FieldPosition& pos,
                     UErrorCode& status) const {
    return format(Formattable(number), (double)number, appendTo, pos, status);
}

UnicodeString&
PluralFormat::format(const Formattable& numberObject, double number,
                     UnicodeString& appendTo,
                     FieldPosition& pos,
                     UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return appendTo;
    }
    if (msgPattern.countParts() == 0) {
        return numberFormat->format(numberObject, appendTo, pos, status);
    }

    // Get the appropriate sub-message.
    // Select it based on the formatted number-offset.
    double numberMinusOffset = number - offset;
    // Call NumberFormatter to get both the DecimalQuantity and the string.
    // This call site needs to use more internal APIs than the Java equivalent.
    number::impl::UFormattedNumberData data;
    if (offset == 0) {
        // could be BigDecimal etc.
        numberObject.populateDecimalQuantity(data.quantity, status);
    } else {
        data.quantity.setToDouble(numberMinusOffset);
    }
    UnicodeString numberString;
    auto *decFmt = dynamic_cast<DecimalFormat *>(numberFormat);
    if(decFmt != nullptr) {
        const number::LocalizedNumberFormatter* lnf = decFmt->toNumberFormatter(status);
        if (U_FAILURE(status)) {
            return appendTo;
        }
        lnf->formatImpl(&data, status); // mutates &data
        if (U_FAILURE(status)) {
            return appendTo;
        }
        numberString = data.getStringRef().toUnicodeString();
    } else {
        if (offset == 0) {
            numberFormat->format(numberObject, numberString, status);
        } else {
            numberFormat->format(numberMinusOffset, numberString, status);
        }
    }

    int32_t partIndex = findSubMessage(msgPattern, 0, pluralRulesWrapper, &data.quantity, number, status);
    if (U_FAILURE(status)) { return appendTo; }
    // Replace syntactic # signs in the top level of this sub-message
    // (not in nested arguments) with the formatted number-offset.
    const UnicodeString& pattern = msgPattern.getPatternString();
    int32_t prevIndex = msgPattern.getPart(partIndex).getLimit();
    for (;;) {
        const MessagePattern::Part& part = msgPattern.getPart(++partIndex);
        const UMessagePatternPartType type = part.getType();
        int32_t index = part.getIndex();
        if (type == UMSGPAT_PART_TYPE_MSG_LIMIT) {
            return appendTo.append(pattern, prevIndex, index - prevIndex);
        } else if ((type == UMSGPAT_PART_TYPE_REPLACE_NUMBER) ||
            (type == UMSGPAT_PART_TYPE_SKIP_SYNTAX && MessageImpl::jdkAposMode(msgPattern))) {
            appendTo.append(pattern, prevIndex, index - prevIndex);
            if (type == UMSGPAT_PART_TYPE_REPLACE_NUMBER) {
                appendTo.append(numberString);
            }
            prevIndex = part.getLimit();
        } else if (type == UMSGPAT_PART_TYPE_ARG_START) {
            appendTo.append(pattern, prevIndex, index - prevIndex);
            prevIndex = index;
            partIndex = msgPattern.getLimitPartIndex(partIndex);
            index = msgPattern.getPart(partIndex).getLimit();
            MessageImpl::appendReducedApostrophes(pattern, prevIndex, index, appendTo);
            prevIndex = index;
        }
    }
}

UnicodeString&
PluralFormat::toPattern(UnicodeString& appendTo) {
    if (0 == msgPattern.countParts()) {
        appendTo.setToBogus();
    } else {
        appendTo.append(msgPattern.getPatternString());
    }
    return appendTo;
}

void
PluralFormat::setLocale(const Locale& loc, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }
    locale = loc;
    msgPattern.clear();
    delete numberFormat;
    offset = 0;
    numberFormat = nullptr;
    pluralRulesWrapper.reset();
    init(nullptr, UPLURAL_TYPE_CARDINAL, status);
}

void
PluralFormat::setNumberFormat(const NumberFormat* format, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }
    NumberFormat* nf = format->clone();
    if (nf != nullptr) {
        delete numberFormat;
        numberFormat = nf;
    } else {
        status = U_MEMORY_ALLOCATION_ERROR;
    }
}

PluralFormat*
PluralFormat::clone() const
{
    return new PluralFormat(*this);
}


PluralFormat&
PluralFormat::operator=(const PluralFormat& other) {
    if (this != &other) {
        locale = other.locale;
        msgPattern = other.msgPattern;
        offset = other.offset;
        copyObjects(other);
    }

    return *this;
}

bool
PluralFormat::operator==(const Format& other) const {
    if (this == &other) {
        return true;
    }
    if (!Format::operator==(other)) {
        return false;
    }
    const PluralFormat& o = (const PluralFormat&)other;
    return
        locale == o.locale &&
        msgPattern == o.msgPattern &&  // implies same offset
        (numberFormat == nullptr) == (o.numberFormat == nullptr) &&
        (numberFormat == nullptr || *numberFormat == *o.numberFormat) &&
        (pluralRulesWrapper.pluralRules == nullptr) == (o.pluralRulesWrapper.pluralRules == nullptr) &&
        (pluralRulesWrapper.pluralRules == nullptr ||
            *pluralRulesWrapper.pluralRules == *o.pluralRulesWrapper.pluralRules);
}

bool
PluralFormat::operator!=(const Format& other) const {
    return  !operator==(other);
}

void
PluralFormat::parseObject(const UnicodeString& /*source*/,
                        Formattable& /*result*/,
                        ParsePosition& pos) const
{
    // Parsing not supported.
    pos.setErrorIndex(pos.getIndex());
}

int32_t PluralFormat::findSubMessage(const MessagePattern& pattern, int32_t partIndex,
                                     const PluralSelector& selector, void *context,
                                     double number, UErrorCode& ec) {
    if (U_FAILURE(ec)) {
        return 0;
    }
    int32_t count=pattern.countParts();
    double offset;
    const MessagePattern::Part* part=&pattern.getPart(partIndex);
    if (MessagePattern::Part::hasNumericValue(part->getType())) {
        offset=pattern.getNumericValue(*part);
        ++partIndex;
    } else {
        offset=0;
    }
    // The keyword is empty until we need to match against a non-explicit, not-"other" value.
    // Then we get the keyword from the selector.
    // (In other words, we never call the selector if we match against an explicit value,
    // or if the only non-explicit keyword is "other".)
    UnicodeString keyword;
    UnicodeString other(false, OTHER_STRING, 5);
    // When we find a match, we set msgStart>0 and also set this boolean to true
    // to avoid matching the keyword again (duplicates are allowed)
    // while we continue to look for an explicit-value match.
    UBool haveKeywordMatch=false;
    // msgStart is 0 until we find any appropriate sub-message.
    // We remember the first "other" sub-message if we have not seen any
    // appropriate sub-message before.
    // We remember the first matching-keyword sub-message if we have not seen
    // one of those before.
    // (The parser allows [does not check for] duplicate keywords.
    // We just have to make sure to take the first one.)
    // We avoid matching the keyword twice by also setting haveKeywordMatch=true
    // at the first keyword match.
    // We keep going until we find an explicit-value match or reach the end of the plural style.
    int32_t msgStart=0;
    // Iterate over (ARG_SELECTOR [ARG_INT|ARG_DOUBLE] message) tuples
    // until ARG_LIMIT or end of plural-only pattern.
    do {
        part=&pattern.getPart(partIndex++);
        const UMessagePatternPartType type = part->getType();
        if(type==UMSGPAT_PART_TYPE_ARG_LIMIT) {
            break;
        }
        U_ASSERT (type==UMSGPAT_PART_TYPE_ARG_SELECTOR);
        // part is an ARG_SELECTOR followed by an optional explicit value, and then a message
        if(MessagePattern::Part::hasNumericValue(pattern.getPartType(partIndex))) {
            // explicit value like "=2"
            part=&pattern.getPart(partIndex++);
            if(number==pattern.getNumericValue(*part)) {
                // matches explicit value
                return partIndex;
            }
        } else if(!haveKeywordMatch) {
            // plural keyword like "few" or "other"
            // Compare "other" first and call the selector if this is not "other".
            if(pattern.partSubstringMatches(*part, other)) {
                if(msgStart==0) {
                    msgStart=partIndex;
                    if(0 == keyword.compare(other)) {
                        // This is the first "other" sub-message,
                        // and the selected keyword is also "other".
                        // Do not match "other" again.
                        haveKeywordMatch=true;
                    }
                }
            } else {
                if(keyword.isEmpty()) {
                    keyword=selector.select(context, number-offset, ec);
                    if(msgStart!=0 && (0 == keyword.compare(other))) {
                        // We have already seen an "other" sub-message.
                        // Do not match "other" again.
                        haveKeywordMatch=true;
                        // Skip keyword matching but do getLimitPartIndex().
                    }
                }
                if(!haveKeywordMatch && pattern.partSubstringMatches(*part, keyword)) {
                    // keyword matches
                    msgStart=partIndex;
                    // Do not match this keyword again.
                    haveKeywordMatch=true;
                }
            }
        }
        partIndex=pattern.getLimitPartIndex(partIndex);
    } while(++partIndex<count);
    return msgStart;
}

void PluralFormat::parseType(const UnicodeString& source, const NFRule *rbnfLenientScanner, Formattable& result, FieldPosition& pos) const {
    // If no pattern was applied, return null.
    if (msgPattern.countParts() == 0) {
        pos.setBeginIndex(-1);
        pos.setEndIndex(-1);
        return;
    }
    int partIndex = 0;
    int currMatchIndex;
    int count=msgPattern.countParts();
    int startingAt = pos.getBeginIndex();
    if (startingAt < 0) {
        startingAt = 0;
    }

    // The keyword is null until we need to match against a non-explicit, not-"other" value.
    // Then we get the keyword from the selector.
    // (In other words, we never call the selector if we match against an explicit value,
    // or if the only non-explicit keyword is "other".)
    UnicodeString keyword;
    UnicodeString matchedWord;
    const UnicodeString& pattern = msgPattern.getPatternString();
    int matchedIndex = -1;
    // Iterate over (ARG_SELECTOR ARG_START message ARG_LIMIT) tuples
    // until the end of the plural-only pattern.
    while (partIndex < count) {
        const MessagePattern::Part* partSelector = &msgPattern.getPart(partIndex++);
        if (partSelector->getType() != UMSGPAT_PART_TYPE_ARG_SELECTOR) {
            // Bad format
            continue;
        }

        const MessagePattern::Part* partStart = &msgPattern.getPart(partIndex++);
        if (partStart->getType() != UMSGPAT_PART_TYPE_MSG_START) {
            // Bad format
            continue;
        }

        const MessagePattern::Part* partLimit = &msgPattern.getPart(partIndex++);
        if (partLimit->getType() != UMSGPAT_PART_TYPE_MSG_LIMIT) {
            // Bad format
            continue;
        }

        UnicodeString currArg = pattern.tempSubString(partStart->getLimit(), partLimit->getIndex() - partStart->getLimit());
        if (rbnfLenientScanner != nullptr) {
            // Check if non-lenient rule finds the text before call lenient parsing
            int32_t tempIndex = source.indexOf(currArg, startingAt);
            if (tempIndex >= 0) {
                currMatchIndex = tempIndex;
            } else {
                // If lenient parsing is turned ON, we've got some time consuming parsing ahead of us.
                int32_t length = -1;
                currMatchIndex = rbnfLenientScanner->findTextLenient(source, currArg, startingAt, &length);
            }
        }
        else {
            currMatchIndex = source.indexOf(currArg, startingAt);
        }
        if (currMatchIndex >= 0 && currMatchIndex >= matchedIndex && currArg.length() > matchedWord.length()) {
            matchedIndex = currMatchIndex;
            matchedWord = currArg;
            keyword = pattern.tempSubString(partStart->getLimit(), partLimit->getIndex() - partStart->getLimit());
        }
    }
    if (matchedIndex >= 0) {
        pos.setBeginIndex(matchedIndex);
        pos.setEndIndex(matchedIndex + matchedWord.length());
        result.setString(keyword);
        return;
    }

    // Not found!
    pos.setBeginIndex(-1);
    pos.setEndIndex(-1);
}

PluralFormat::PluralSelector::~PluralSelector() {}

PluralFormat::PluralSelectorAdapter::~PluralSelectorAdapter() {
    delete pluralRules;
}

UnicodeString PluralFormat::PluralSelectorAdapter::select(void *context, double number,
                                                          UErrorCode& /*ec*/) const {
    (void)number;  // unused except in the assertion
    IFixedDecimal *dec=static_cast<IFixedDecimal *>(context);
    return pluralRules->select(*dec);
}

void PluralFormat::PluralSelectorAdapter::reset() {
    delete pluralRules;
    pluralRules = nullptr;
}


U_NAMESPACE_END


#endif /* #if !UCONFIG_NO_FORMATTING */

//eof
