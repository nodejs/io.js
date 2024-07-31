// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*
*   Copyright (C) 2013-2016, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*   file name:  listformatter.cpp
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 2012aug27
*   created by: Umesh P. Nair
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "cmemory.h"
#include "unicode/fpositer.h"  // FieldPositionIterator
#include "unicode/listformatter.h"
#include "unicode/simpleformatter.h"
#include "unicode/ulistformatter.h"
#include "unicode/uscript.h"
#include "fphdlimp.h"
#include "mutex.h"
#include "hash.h"
#include "cstring.h"
#include "uarrsort.h"
#include "ulocimp.h"
#include "charstr.h"
#include "ucln_in.h"
#include "uresimp.h"
#include "resource.h"
#include "formattedval_impl.h"

U_NAMESPACE_BEGIN

namespace {

class PatternHandler : public UObject {
public:
    PatternHandler(const UnicodeString& two, const UnicodeString& end, UErrorCode& errorCode) :
        twoPattern(two, 2, 2, errorCode),
        endPattern(end, 2, 2, errorCode) {  }

    PatternHandler(const SimpleFormatter& two, const SimpleFormatter& end) :
        twoPattern(two),
        endPattern(end) { }

    virtual ~PatternHandler();

    virtual PatternHandler* clone() const { return new PatternHandler(twoPattern, endPattern); }

    /** Argument: final string in the list. */
    virtual const SimpleFormatter& getTwoPattern(const UnicodeString&) const {
        return twoPattern;
    }

    /** Argument: final string in the list. */
    virtual const SimpleFormatter& getEndPattern(const UnicodeString&) const {
        return endPattern;
    }

protected:
    SimpleFormatter twoPattern;
    SimpleFormatter endPattern;
};

PatternHandler::~PatternHandler() {
}

class ContextualHandler : public PatternHandler {
public:
    ContextualHandler(bool (*testFunc)(const UnicodeString& text),
                      const UnicodeString& thenTwo,
                      const UnicodeString& elseTwo,
                      const UnicodeString& thenEnd,
                      const UnicodeString& elseEnd,
                      UErrorCode& errorCode) :
        PatternHandler(elseTwo, elseEnd, errorCode),
        test(testFunc),
        thenTwoPattern(thenTwo, 2, 2, errorCode),
        thenEndPattern(thenEnd, 2, 2, errorCode) {  }

    ContextualHandler(bool (*testFunc)(const UnicodeString& text),
                      const SimpleFormatter& thenTwo, SimpleFormatter elseTwo,
                      const SimpleFormatter& thenEnd, SimpleFormatter elseEnd) :
      PatternHandler(elseTwo, elseEnd),
      test(testFunc),
      thenTwoPattern(thenTwo),
      thenEndPattern(thenEnd) { }

    ~ContextualHandler() override;

    PatternHandler* clone() const override {
        return new ContextualHandler(
            test, thenTwoPattern, twoPattern, thenEndPattern, endPattern);
    }

    const SimpleFormatter& getTwoPattern(
        const UnicodeString& text) const override {
        return (test)(text) ? thenTwoPattern : twoPattern;
    }

    const SimpleFormatter& getEndPattern(
        const UnicodeString& text) const override {
        return (test)(text) ? thenEndPattern : endPattern;
    }

private:
    bool (*test)(const UnicodeString&);
    SimpleFormatter thenTwoPattern;
    SimpleFormatter thenEndPattern;
};

ContextualHandler::~ContextualHandler() {
}

const char16_t* spanishY = u"{0} y {1}";
const char16_t* spanishE = u"{0} e {1}";
const char16_t* spanishO = u"{0} o {1}";
const char16_t* spanishU = u"{0} u {1}";
const char16_t* hebrewVav = u"{0} \u05D5{1}";
const char16_t* hebrewVavDash = u"{0} \u05D5-{1}";

// Condiction to change to e.
// Starts with "hi" or "i" but not with "hie" nor "hia"
bool shouldChangeToE(const UnicodeString& text) {
    int32_t len = text.length();
    if (len == 0) { return false; }
    // Case insensitive match hi but not hie nor hia.
    if ((text[0] == u'h' || text[0] == u'H') &&
            ((len > 1) && (text[1] == u'i' || text[1] == u'I')) &&
            ((len == 2) || !(text[2] == u'a' || text[2] == u'A' || text[2] == u'e' || text[2] == u'E'))) {
        return true;
    }
    // Case insensitive for "start with i"
    if (text[0] == u'i' || text[0] == u'I') { return true; }
    return false;
}

// Condiction to change to u.
// Starts with "o", "ho", and "8". Also "11" by itself.
// re: ^((o|ho|8).*|11)$
bool shouldChangeToU(const UnicodeString& text) {
    int32_t len = text.length();
    if (len == 0) { return false; }
    // Case insensitive match o.* and 8.*
    if (text[0] == u'o' || text[0] == u'O' || text[0] == u'8') { return true; }
    // Case insensitive match ho.*
    if ((text[0] == u'h' || text[0] == u'H') &&
            ((len > 1) && (text[1] == 'o' || text[1] == u'O'))) {
        return true;
    }
    // match "^11$" and "^11 .*"
    if ((len >= 2) && text[0] == u'1' && text[1] == u'1' && (len == 2 || text[2] == u' ')) { return true; }
    return false;
}

// Condiction to change to VAV follow by a dash.
// Starts with non Hebrew letter.
bool shouldChangeToVavDash(const UnicodeString& text) {
    if (text.isEmpty()) { return false; }
    UErrorCode status = U_ZERO_ERROR;
    return uscript_getScript(text.char32At(0), &status) != USCRIPT_HEBREW;
}

PatternHandler* createPatternHandler(
        const char* lang, const UnicodeString& two, const UnicodeString& end,
    UErrorCode& status) {
    if (uprv_strcmp(lang, "es") == 0) {
        // Spanish
        UnicodeString spanishYStr(true, spanishY, -1);
        bool twoIsY = two == spanishYStr;
        bool endIsY = end == spanishYStr;
        if (twoIsY || endIsY) {
            UnicodeString replacement(true, spanishE, -1);
            return new ContextualHandler(
                shouldChangeToE,
                twoIsY ? replacement : two, two,
                endIsY ? replacement : end, end, status);
        }
        UnicodeString spanishOStr(true, spanishO, -1);
        bool twoIsO = two == spanishOStr;
        bool endIsO = end == spanishOStr;
        if (twoIsO || endIsO) {
            UnicodeString replacement(true, spanishU, -1);
            return new ContextualHandler(
                shouldChangeToU,
                twoIsO ? replacement : two, two,
                endIsO ? replacement : end, end, status);
        }
    } else if (uprv_strcmp(lang, "he") == 0 || uprv_strcmp(lang, "iw") == 0) {
        // Hebrew
        UnicodeString hebrewVavStr(true, hebrewVav, -1);
        bool twoIsVav = two == hebrewVavStr;
        bool endIsVav = end == hebrewVavStr;
        if (twoIsVav || endIsVav) {
            UnicodeString replacement(true, hebrewVavDash, -1);
            return new ContextualHandler(
                shouldChangeToVavDash,
                twoIsVav ? replacement : two, two,
                endIsVav ? replacement : end, end, status);
        }
    }
    return new PatternHandler(two, end, status);
}

}  // namespace

struct ListFormatInternal : public UMemory {
    SimpleFormatter startPattern;
    SimpleFormatter middlePattern;
    LocalPointer<PatternHandler> patternHandler;

ListFormatInternal(
        const UnicodeString& two,
        const UnicodeString& start,
        const UnicodeString& middle,
        const UnicodeString& end,
        const Locale& locale,
        UErrorCode &errorCode) :
        startPattern(start, 2, 2, errorCode),
        middlePattern(middle, 2, 2, errorCode),
        patternHandler(createPatternHandler(locale.getLanguage(), two, end, errorCode), errorCode) { }

ListFormatInternal(const ListFormatData &data, UErrorCode &errorCode) :
        startPattern(data.startPattern, errorCode),
        middlePattern(data.middlePattern, errorCode),
        patternHandler(createPatternHandler(
            data.locale.getLanguage(), data.twoPattern, data.endPattern, errorCode), errorCode) { }

ListFormatInternal(const ListFormatInternal &other) :
    startPattern(other.startPattern),
    middlePattern(other.middlePattern),
    patternHandler(other.patternHandler->clone()) { }
};


class FormattedListData : public FormattedValueStringBuilderImpl {
public:
    FormattedListData(UErrorCode&) : FormattedValueStringBuilderImpl(kUndefinedField) {}
    virtual ~FormattedListData();
};

FormattedListData::~FormattedListData() = default;

UPRV_FORMATTED_VALUE_SUBCLASS_AUTO_IMPL(FormattedList)


static Hashtable* listPatternHash = nullptr;

U_CDECL_BEGIN
static UBool U_CALLCONV uprv_listformatter_cleanup() {
    delete listPatternHash;
    listPatternHash = nullptr;
    return true;
}

static void U_CALLCONV
uprv_deleteListFormatInternal(void *obj) {
    delete static_cast<ListFormatInternal *>(obj);
}

U_CDECL_END

ListFormatter::ListFormatter(const ListFormatter& other) :
        owned(other.owned), data(other.data) {
    if (other.owned != nullptr) {
        owned = new ListFormatInternal(*other.owned);
        data = owned;
    }
}

ListFormatter& ListFormatter::operator=(const ListFormatter& other) {
    if (this == &other) {
        return *this;
    }
    delete owned;
    if (other.owned) {
        owned = new ListFormatInternal(*other.owned);
        data = owned;
    } else {
        owned = nullptr;
        data = other.data;
    }
    return *this;
}

void ListFormatter::initializeHash(UErrorCode& errorCode) {
    if (U_FAILURE(errorCode)) {
        return;
    }

    listPatternHash = new Hashtable();
    if (listPatternHash == nullptr) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        return;
    }

    listPatternHash->setValueDeleter(uprv_deleteListFormatInternal);
    ucln_i18n_registerCleanup(UCLN_I18N_LIST_FORMATTER, uprv_listformatter_cleanup);

}

const ListFormatInternal* ListFormatter::getListFormatInternal(
        const Locale& locale, const char *style, UErrorCode& errorCode) {
    if (U_FAILURE(errorCode)) {
        return nullptr;
    }
    CharString keyBuffer(locale.getName(), errorCode);
    keyBuffer.append(':', errorCode).append(style, errorCode);
    UnicodeString key(keyBuffer.data(), -1, US_INV);
    ListFormatInternal* result = nullptr;
    static UMutex listFormatterMutex;
    {
        Mutex m(&listFormatterMutex);
        if (listPatternHash == nullptr) {
            initializeHash(errorCode);
            if (U_FAILURE(errorCode)) {
                return nullptr;
            }
        }
        result = static_cast<ListFormatInternal*>(listPatternHash->get(key));
    }
    if (result != nullptr) {
        return result;
    }
    result = loadListFormatInternal(locale, style, errorCode);
    if (U_FAILURE(errorCode)) {
        return nullptr;
    }

    {
        Mutex m(&listFormatterMutex);
        ListFormatInternal* temp = static_cast<ListFormatInternal*>(listPatternHash->get(key));
        if (temp != nullptr) {
            delete result;
            result = temp;
        } else {
            listPatternHash->put(key, result, errorCode);
            if (U_FAILURE(errorCode)) {
                return nullptr;
            }
        }
    }
    return result;
}

static const char* typeWidthToStyleString(UListFormatterType type, UListFormatterWidth width) {
    switch (type) {
        case ULISTFMT_TYPE_AND:
            switch (width) {
                case ULISTFMT_WIDTH_WIDE:
                    return "standard";
                case ULISTFMT_WIDTH_SHORT:
                    return "standard-short";
                case ULISTFMT_WIDTH_NARROW:
                    return "standard-narrow";
                default:
                    return nullptr;
            }
            break;

        case ULISTFMT_TYPE_OR:
            switch (width) {
                case ULISTFMT_WIDTH_WIDE:
                    return "or";
                case ULISTFMT_WIDTH_SHORT:
                    return "or-short";
                case ULISTFMT_WIDTH_NARROW:
                    return "or-narrow";
                default:
                    return nullptr;
            }
            break;

        case ULISTFMT_TYPE_UNITS:
            switch (width) {
                case ULISTFMT_WIDTH_WIDE:
                    return "unit";
                case ULISTFMT_WIDTH_SHORT:
                    return "unit-short";
                case ULISTFMT_WIDTH_NARROW:
                    return "unit-narrow";
                default:
                    return nullptr;
            }
    }

    return nullptr;
}

static const char16_t solidus = 0x2F;
static const char16_t aliasPrefix[] = { 0x6C,0x69,0x73,0x74,0x50,0x61,0x74,0x74,0x65,0x72,0x6E,0x2F }; // "listPattern/"
enum {
    kAliasPrefixLen = UPRV_LENGTHOF(aliasPrefix),
    kStyleLenMax = 24 // longest currently is 14
};

struct ListFormatter::ListPatternsSink : public ResourceSink {
    UnicodeString two, start, middle, end;
    char aliasedStyle[kStyleLenMax+1] = {0};

    ListPatternsSink() {}
    virtual ~ListPatternsSink();

    void setAliasedStyle(UnicodeString alias) {
        int32_t startIndex = alias.indexOf(aliasPrefix, kAliasPrefixLen, 0);
        if (startIndex < 0) {
            return;
        }
        startIndex += kAliasPrefixLen;
        int32_t endIndex = alias.indexOf(solidus, startIndex);
        if (endIndex < 0) {
            endIndex = alias.length();
        }
        alias.extract(startIndex, endIndex-startIndex, aliasedStyle, kStyleLenMax+1, US_INV);
        aliasedStyle[kStyleLenMax] = 0;
    }

    void handleValueForPattern(ResourceValue &value, UnicodeString &pattern, UErrorCode &errorCode) {
        if (pattern.isEmpty()) {
            if (value.getType() == URES_ALIAS) {
                if (aliasedStyle[0] == 0) {
                    setAliasedStyle(value.getAliasUnicodeString(errorCode));
                }
            } else {
                pattern = value.getUnicodeString(errorCode);
            }
        }
    }

    virtual void put(const char *key, ResourceValue &value, UBool /*noFallback*/,
            UErrorCode &errorCode) override {
        aliasedStyle[0] = 0;
        if (value.getType() == URES_ALIAS) {
            setAliasedStyle(value.getAliasUnicodeString(errorCode));
            return;
        }
        ResourceTable listPatterns = value.getTable(errorCode);
        for (int i = 0; U_SUCCESS(errorCode) && listPatterns.getKeyAndValue(i, key, value); ++i) {
            if (uprv_strcmp(key, "2") == 0) {
                handleValueForPattern(value, two, errorCode);
            } else if (uprv_strcmp(key, "end") == 0) {
                handleValueForPattern(value, end, errorCode);
            } else if (uprv_strcmp(key, "middle") == 0) {
                handleValueForPattern(value, middle, errorCode);
            } else if (uprv_strcmp(key, "start") == 0) {
                handleValueForPattern(value, start, errorCode);
            }
        }
    }
};

// Virtual destructors must be defined out of line.
ListFormatter::ListPatternsSink::~ListPatternsSink() {}

ListFormatInternal* ListFormatter::loadListFormatInternal(
        const Locale& locale, const char * style, UErrorCode& errorCode) {
    UResourceBundle* rb = ures_open(nullptr, locale.getName(), &errorCode);
    rb = ures_getByKeyWithFallback(rb, "listPattern", rb, &errorCode);
    if (U_FAILURE(errorCode)) {
        ures_close(rb);
        return nullptr;
    }
    ListFormatter::ListPatternsSink sink;
    char currentStyle[kStyleLenMax+1];
    uprv_strncpy(currentStyle, style, kStyleLenMax);
    currentStyle[kStyleLenMax] = 0;

    for (;;) {
        ures_getAllItemsWithFallback(rb, currentStyle, sink, errorCode);
        if (U_FAILURE(errorCode) || sink.aliasedStyle[0] == 0 || uprv_strcmp(currentStyle, sink.aliasedStyle) == 0) {
            break;
        }
        uprv_strcpy(currentStyle, sink.aliasedStyle);
    }
    ures_close(rb);
    if (U_FAILURE(errorCode)) {
        return nullptr;
    }
    if (sink.two.isEmpty() || sink.start.isEmpty() || sink.middle.isEmpty() || sink.end.isEmpty()) {
        errorCode = U_MISSING_RESOURCE_ERROR;
        return nullptr;
    }

    ListFormatInternal* result = new ListFormatInternal(sink.two, sink.start, sink.middle, sink.end, locale, errorCode);
    if (result == nullptr) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    if (U_FAILURE(errorCode)) {
        delete result;
        return nullptr;
    }
    return result;
}

ListFormatter* ListFormatter::createInstance(UErrorCode& errorCode) {
    Locale locale;  // The default locale.
    return createInstance(locale, errorCode);
}

ListFormatter* ListFormatter::createInstance(const Locale& locale, UErrorCode& errorCode) {
    return createInstance(locale, ULISTFMT_TYPE_AND, ULISTFMT_WIDTH_WIDE, errorCode);
}

ListFormatter* ListFormatter::createInstance(
        const Locale& locale, UListFormatterType type, UListFormatterWidth width, UErrorCode& errorCode) {
    const char* style = typeWidthToStyleString(type, width);
    if (style == nullptr) {
        errorCode = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }
    return createInstance(locale, style, errorCode);
}

ListFormatter* ListFormatter::createInstance(const Locale& locale, const char *style, UErrorCode& errorCode) {
    const ListFormatInternal* listFormatInternal = getListFormatInternal(locale, style, errorCode);
    if (U_FAILURE(errorCode)) {
        return nullptr;
    }
    ListFormatter* p = new ListFormatter(listFormatInternal);
    if (p == nullptr) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    return p;
}

ListFormatter::ListFormatter(const ListFormatData& listFormatData, UErrorCode &errorCode) {
    owned = new ListFormatInternal(listFormatData, errorCode);
    data = owned;
}

ListFormatter::ListFormatter(const ListFormatInternal* listFormatterInternal) : owned(nullptr), data(listFormatterInternal) {
}

ListFormatter::~ListFormatter() {
    delete owned;
}

namespace {

class FormattedListBuilder {
public:
    LocalPointer<FormattedListData> data;

    /** For lists of length 1+ */
    FormattedListBuilder(const UnicodeString& start, UErrorCode& status)
            : data(new FormattedListData(status), status) {
        if (U_SUCCESS(status)) {
            data->getStringRef().append(
                start,
                {UFIELD_CATEGORY_LIST, ULISTFMT_ELEMENT_FIELD},
                status);
            data->appendSpanInfo(UFIELD_CATEGORY_LIST_SPAN, 0, -1, start.length(), status);
        }
    }

    /** For lists of length 0 */
    FormattedListBuilder(UErrorCode& status)
            : data(new FormattedListData(status), status) {
    }

    void append(const SimpleFormatter& pattern, const UnicodeString& next, int32_t position, UErrorCode& status) {
        if (U_FAILURE(status)) {
            return;
        }
        if (pattern.getArgumentLimit() != 2) {
            status = U_INTERNAL_PROGRAM_ERROR;
            return;
        }
        // In the pattern, {0} are the pre-existing elements and {1} is the new element.
        int32_t offsets[] = {0, 0};
        UnicodeString temp = pattern.getTextWithNoArguments(offsets, 2);
        if (offsets[0] <= offsets[1]) {
            // prefix{0}infix{1}suffix
            // Prepend prefix, then append infix, element, and suffix
            data->getStringRef().insert(
                0,
                temp.tempSubStringBetween(0, offsets[0]),
                {UFIELD_CATEGORY_LIST, ULISTFMT_LITERAL_FIELD},
                status);
            data->getStringRef().append(
                temp.tempSubStringBetween(offsets[0], offsets[1]),
                {UFIELD_CATEGORY_LIST, ULISTFMT_LITERAL_FIELD},
                status);
            data->getStringRef().append(
                next,
                {UFIELD_CATEGORY_LIST, ULISTFMT_ELEMENT_FIELD},
                status);
            data->appendSpanInfo(UFIELD_CATEGORY_LIST_SPAN, position, -1, next.length(), status);
            data->getStringRef().append(
                temp.tempSubString(offsets[1]),
                {UFIELD_CATEGORY_LIST, ULISTFMT_LITERAL_FIELD},
                status);
        } else {
            // prefix{1}infix{0}suffix
            // Prepend infix, element, and prefix, then append suffix.
            // (We prepend in reverse order because prepending at index 0 is fast.)
            data->getStringRef().insert(
                0,
                temp.tempSubStringBetween(offsets[1], offsets[0]),
                {UFIELD_CATEGORY_LIST, ULISTFMT_LITERAL_FIELD},
                status);
            data->getStringRef().insert(
                0,
                next,
                {UFIELD_CATEGORY_LIST, ULISTFMT_ELEMENT_FIELD},
                status);
            data->prependSpanInfo(UFIELD_CATEGORY_LIST_SPAN, position, -1, next.length(), status);
            data->getStringRef().insert(
                0,
                temp.tempSubStringBetween(0, offsets[1]),
                {UFIELD_CATEGORY_LIST, ULISTFMT_LITERAL_FIELD},
                status);
            data->getStringRef().append(
                temp.tempSubString(offsets[0]),
                {UFIELD_CATEGORY_LIST, ULISTFMT_LITERAL_FIELD},
                status);
        }
    }
};

}

UnicodeString& ListFormatter::format(
        const UnicodeString items[],
        int32_t nItems,
        UnicodeString& appendTo,
        UErrorCode& errorCode) const {
    int32_t offset;
    return format(items, nItems, appendTo, -1, offset, errorCode);
}

UnicodeString& ListFormatter::format(
        const UnicodeString items[],
        int32_t nItems,
        UnicodeString& appendTo,
        int32_t index,
        int32_t &offset,
        UErrorCode& errorCode) const {
    int32_t initialOffset = appendTo.length();
    auto result = formatStringsToValue(items, nItems, errorCode);
    UnicodeStringAppendable appendable(appendTo);
    result.appendTo(appendable, errorCode);
    if (index >= 0) {
        ConstrainedFieldPosition cfpos;
        cfpos.constrainField(UFIELD_CATEGORY_LIST_SPAN, index);
        result.nextPosition(cfpos, errorCode);
        offset = initialOffset + cfpos.getStart();
    }
    return appendTo;
}

FormattedList ListFormatter::formatStringsToValue(
        const UnicodeString items[],
        int32_t nItems,
        UErrorCode& errorCode) const {
    if (nItems == 0) {
        FormattedListBuilder result(errorCode);
        if (U_FAILURE(errorCode)) {
            return FormattedList(errorCode);
        } else {
            return FormattedList(result.data.orphan());
        }
    } else if (nItems == 1) {
        FormattedListBuilder result(items[0], errorCode);
        result.data->getStringRef().writeTerminator(errorCode);
        if (U_FAILURE(errorCode)) {
            return FormattedList(errorCode);
        } else {
            return FormattedList(result.data.orphan());
        }
    } else if (nItems == 2) {
        FormattedListBuilder result(items[0], errorCode);
        if (U_FAILURE(errorCode)) {
            return FormattedList(errorCode);
        }
        result.append(
            data->patternHandler->getTwoPattern(items[1]),
            items[1],
            1,
            errorCode);
        result.data->getStringRef().writeTerminator(errorCode);
        if (U_FAILURE(errorCode)) {
            return FormattedList(errorCode);
        } else {
            return FormattedList(result.data.orphan());
        }
    }

    FormattedListBuilder result(items[0], errorCode);
    if (U_FAILURE(errorCode)) {
        return FormattedList(errorCode);
    }
    result.append(
        data->startPattern,
        items[1],
        1,
        errorCode);
    for (int32_t i = 2; i < nItems - 1; i++) {
        result.append(
            data->middlePattern,
            items[i],
            i,
            errorCode);
    }
    result.append(
        data->patternHandler->getEndPattern(items[nItems-1]),
        items[nItems-1],
        nItems-1,
        errorCode);
    result.data->getStringRef().writeTerminator(errorCode);
    if (U_FAILURE(errorCode)) {
        return FormattedList(errorCode);
    } else {
        return FormattedList(result.data.orphan());
    }
}


U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */
