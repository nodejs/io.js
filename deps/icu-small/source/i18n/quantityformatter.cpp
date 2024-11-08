// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
******************************************************************************
* Copyright (C) 2014-2016, International Business Machines
* Corporation and others.  All Rights Reserved.
******************************************************************************
* quantityformatter.cpp
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/simpleformatter.h"
#include "quantityformatter.h"
#include "uassert.h"
#include "unicode/unistr.h"
#include "unicode/decimfmt.h"
#include "cstring.h"
#include "unicode/plurrule.h"
#include "charstr.h"
#include "unicode/fmtable.h"
#include "unicode/fieldpos.h"
#include "standardplural.h"
#include "uassert.h"
#include "number_decimalquantity.h"
#include "number_utypes.h"
#include "formatted_string_builder.h"

U_NAMESPACE_BEGIN

QuantityFormatter::QuantityFormatter() {
    for (int32_t i = 0; i < UPRV_LENGTHOF(formatters); ++i) {
        formatters[i] = nullptr;
    }
}

QuantityFormatter::QuantityFormatter(const QuantityFormatter &other) {
    for (int32_t i = 0; i < UPRV_LENGTHOF(formatters); ++i) {
        if (other.formatters[i] == nullptr) {
            formatters[i] = nullptr;
        } else {
            formatters[i] = new SimpleFormatter(*other.formatters[i]);
        }
    }
}

QuantityFormatter &QuantityFormatter::operator=(
        const QuantityFormatter& other) {
    if (this == &other) {
        return *this;
    }
    for (int32_t i = 0; i < UPRV_LENGTHOF(formatters); ++i) {
        delete formatters[i];
        if (other.formatters[i] == nullptr) {
            formatters[i] = nullptr;
        } else {
            formatters[i] = new SimpleFormatter(*other.formatters[i]);
        }
    }
    return *this;
}

QuantityFormatter::~QuantityFormatter() {
    for (int32_t i = 0; i < UPRV_LENGTHOF(formatters); ++i) {
        delete formatters[i];
    }
}

void QuantityFormatter::reset() {
    for (int32_t i = 0; i < UPRV_LENGTHOF(formatters); ++i) {
        delete formatters[i];
        formatters[i] = nullptr;
    }
}

UBool QuantityFormatter::addIfAbsent(
        const char *variant,
        const UnicodeString &rawPattern,
        UErrorCode &status) {
    int32_t pluralIndex = StandardPlural::indexFromString(variant, status);
    if (U_FAILURE(status)) {
        return false;
    }
    if (formatters[pluralIndex] != nullptr) {
        return true;
    }
    SimpleFormatter *newFmt = new SimpleFormatter(rawPattern, 0, 1, status);
    if (newFmt == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return false;
    }
    if (U_FAILURE(status)) {
        delete newFmt;
        return false;
    }
    formatters[pluralIndex] = newFmt;
    return true;
}

UBool QuantityFormatter::isValid() const {
    return formatters[StandardPlural::OTHER] != nullptr;
}

const SimpleFormatter *QuantityFormatter::getByVariant(
        const char *variant) const {
    U_ASSERT(isValid());
    int32_t pluralIndex = StandardPlural::indexOrOtherIndexFromString(variant);
    const SimpleFormatter *pattern = formatters[pluralIndex];
    if (pattern == nullptr) {
        pattern = formatters[StandardPlural::OTHER];
    }
    return pattern;
}

UnicodeString &QuantityFormatter::format(
            const Formattable &number,
            const NumberFormat &fmt,
            const PluralRules &rules,
            UnicodeString &appendTo,
            FieldPosition &pos,
            UErrorCode &status) const {
    UnicodeString formattedNumber;
    StandardPlural::Form p = selectPlural(number, fmt, rules, formattedNumber, pos, status);
    if (U_FAILURE(status)) {
        return appendTo;
    }
    const SimpleFormatter *pattern = formatters[p];
    if (pattern == nullptr) {
        pattern = formatters[StandardPlural::OTHER];
        if (pattern == nullptr) {
            status = U_INVALID_STATE_ERROR;
            return appendTo;
        }
    }
    return format(*pattern, formattedNumber, appendTo, pos, status);
}

// The following methods live here so that class PluralRules does not depend on number formatting,
// and the SimpleFormatter does not depend on FieldPosition.

StandardPlural::Form QuantityFormatter::selectPlural(
            const Formattable &number,
            const NumberFormat &fmt,
            const PluralRules &rules,
            UnicodeString &formattedNumber,
            FieldPosition &pos,
            UErrorCode &status) {
    if (U_FAILURE(status)) {
        return StandardPlural::OTHER;
    }
    UnicodeString pluralKeyword;
    const DecimalFormat *decFmt = dynamic_cast<const DecimalFormat *>(&fmt);
    if (decFmt != nullptr) {
        number::impl::DecimalQuantity dq;
        decFmt->formatToDecimalQuantity(number, dq, status);
        if (U_FAILURE(status)) {
            return StandardPlural::OTHER;
        }
        pluralKeyword = rules.select(dq);
        decFmt->format(number, formattedNumber, pos, status);
    } else {
        if (number.getType() == Formattable::kDouble) {
            pluralKeyword = rules.select(number.getDouble());
        } else if (number.getType() == Formattable::kLong) {
            pluralKeyword = rules.select(number.getLong());
        } else if (number.getType() == Formattable::kInt64) {
            pluralKeyword = rules.select(static_cast<double>(number.getInt64()));
        } else {
            status = U_ILLEGAL_ARGUMENT_ERROR;
            return StandardPlural::OTHER;
        }
        fmt.format(number, formattedNumber, pos, status);
    }
    return StandardPlural::orOtherFromString(pluralKeyword);
}

void QuantityFormatter::formatAndSelect(
        double quantity,
        const NumberFormat& fmt,
        const PluralRules& rules,
        FormattedStringBuilder& output,
        StandardPlural::Form& pluralForm,
        UErrorCode& status) {
    UnicodeString pluralKeyword;
    const DecimalFormat* df = dynamic_cast<const DecimalFormat*>(&fmt);
    if (df != nullptr) {
        number::impl::UFormattedNumberData fn;
        fn.quantity.setToDouble(quantity);
        const number::LocalizedNumberFormatter* lnf = df->toNumberFormatter(status);
        if (U_FAILURE(status)) {
            return;
        }
        lnf->formatImpl(&fn, status);
        if (U_FAILURE(status)) {
            return;
        }
        output = std::move(fn.getStringRef());
        pluralKeyword = rules.select(fn.quantity);
    } else {
        UnicodeString result;
        fmt.format(quantity, result, status);
        if (U_FAILURE(status)) {
            return;
        }
        // This code path is probably RBNF. Use the generic numeric field.
        output.append(result, kGeneralNumericField, status);
        if (U_FAILURE(status)) {
            return;
        }
        pluralKeyword = rules.select(quantity);
    }
    pluralForm = StandardPlural::orOtherFromString(pluralKeyword);
}

UnicodeString &QuantityFormatter::format(
            const SimpleFormatter &pattern,
            const UnicodeString &value,
            UnicodeString &appendTo,
            FieldPosition &pos,
            UErrorCode &status) {
    if (U_FAILURE(status)) {
        return appendTo;
    }
    const UnicodeString *param = &value;
    int32_t offset;
    pattern.formatAndAppend(&param, 1, appendTo, &offset, 1, status);
    if (pos.getBeginIndex() != 0 || pos.getEndIndex() != 0) {
        if (offset >= 0) {
            pos.setBeginIndex(pos.getBeginIndex() + offset);
            pos.setEndIndex(pos.getEndIndex() + offset);
        } else {
            pos.setBeginIndex(0);
            pos.setEndIndex(0);
        }
    }
    return appendTo;
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */
