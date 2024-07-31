// © 2019 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include <utility>

#include "bytesinkutil.h"  // StringByteSink<CharString>
#include "charstr.h"
#include "cstring.h"
#include "ulocimp.h"
#include "unicode/localebuilder.h"
#include "unicode/locid.h"

namespace {

inline bool UPRV_ISDIGIT(char c) { return c >= '0' && c <= '9'; }
inline bool UPRV_ISALPHANUM(char c) { return uprv_isASCIILetter(c) || UPRV_ISDIGIT(c); }

constexpr const char* kAttributeKey = "attribute";

bool _isExtensionSubtags(char key, const char* s, int32_t len) {
    switch (uprv_tolower(key)) {
        case 'u':
            return ultag_isUnicodeExtensionSubtags(s, len);
        case 't':
            return ultag_isTransformedExtensionSubtags(s, len);
        case 'x':
            return ultag_isPrivateuseValueSubtags(s, len);
        default:
            return ultag_isExtensionSubtags(s, len);
    }
}

}  // namespace

U_NAMESPACE_BEGIN

LocaleBuilder::LocaleBuilder() : UObject(), status_(U_ZERO_ERROR), language_(),
    script_(), region_(), variant_(nullptr), extensions_(nullptr)
{
    language_[0] = 0;
    script_[0] = 0;
    region_[0] = 0;
}

LocaleBuilder::~LocaleBuilder()
{
    delete variant_;
    delete extensions_;
}

LocaleBuilder& LocaleBuilder::setLocale(const Locale& locale)
{
    clear();
    setLanguage(locale.getLanguage());
    setScript(locale.getScript());
    setRegion(locale.getCountry());
    setVariant(locale.getVariant());
    extensions_ = locale.clone();
    if (extensions_ == nullptr) {
        status_ = U_MEMORY_ALLOCATION_ERROR;
    }
    return *this;
}

LocaleBuilder& LocaleBuilder::setLanguageTag(StringPiece tag)
{
    Locale l = Locale::forLanguageTag(tag, status_);
    if (U_FAILURE(status_)) { return *this; }
    // Because setLocale will reset status_ we need to return
    // first if we have error in forLanguageTag.
    setLocale(l);
    return *this;
}

namespace {

void setField(StringPiece input, char* dest, UErrorCode& errorCode,
              bool (*test)(const char*, int32_t)) {
    if (U_FAILURE(errorCode)) { return; }
    if (input.empty()) {
        dest[0] = '\0';
    } else if (test(input.data(), input.length())) {
        uprv_memcpy(dest, input.data(), input.length());
        dest[input.length()] = '\0';
    } else {
        errorCode = U_ILLEGAL_ARGUMENT_ERROR;
    }
}

}  // namespace

LocaleBuilder& LocaleBuilder::setLanguage(StringPiece language)
{
    setField(language, language_, status_, &ultag_isLanguageSubtag);
    return *this;
}

LocaleBuilder& LocaleBuilder::setScript(StringPiece script)
{
    setField(script, script_, status_, &ultag_isScriptSubtag);
    return *this;
}

LocaleBuilder& LocaleBuilder::setRegion(StringPiece region)
{
    setField(region, region_, status_, &ultag_isRegionSubtag);
    return *this;
}

namespace {

void transform(char* data, int32_t len) {
    for (int32_t i = 0; i < len; i++, data++) {
        if (*data == '_') {
            *data = '-';
        } else {
            *data = uprv_tolower(*data);
        }
    }
}

}  // namespace

LocaleBuilder& LocaleBuilder::setVariant(StringPiece variant)
{
    if (U_FAILURE(status_)) { return *this; }
    if (variant.empty()) {
        delete variant_;
        variant_ = nullptr;
        return *this;
    }
    CharString* new_variant = new CharString(variant, status_);
    if (U_FAILURE(status_)) { return *this; }
    if (new_variant == nullptr) {
        status_ = U_MEMORY_ALLOCATION_ERROR;
        return *this;
    }
    transform(new_variant->data(), new_variant->length());
    if (!ultag_isVariantSubtags(new_variant->data(), new_variant->length())) {
        delete new_variant;
        status_ = U_ILLEGAL_ARGUMENT_ERROR;
        return *this;
    }
    delete variant_;
    variant_ = new_variant;
    return *this;
}

namespace {

bool
_isKeywordValue(const char* key, const char* value, int32_t value_len)
{
    if (key[1] == '\0') {
        // one char key
        return (UPRV_ISALPHANUM(uprv_tolower(key[0])) &&
                _isExtensionSubtags(key[0], value, value_len));
    } else if (uprv_strcmp(key, kAttributeKey) == 0) {
        // unicode attributes
        return ultag_isUnicodeLocaleAttributes(value, value_len);
    }
    // otherwise: unicode extension value
    // We need to convert from legacy key/value to unicode
    // key/value
    const char* unicode_locale_key = uloc_toUnicodeLocaleKey(key);
    const char* unicode_locale_type = uloc_toUnicodeLocaleType(key, value);

    return unicode_locale_key && unicode_locale_type &&
           ultag_isUnicodeLocaleKey(unicode_locale_key, -1) &&
           ultag_isUnicodeLocaleType(unicode_locale_type, -1);
}

void
_copyExtensions(const Locale& from, icu::StringEnumeration *keywords,
                Locale& to, bool validate, UErrorCode& errorCode)
{
    if (U_FAILURE(errorCode)) { return; }
    LocalPointer<icu::StringEnumeration> ownedKeywords;
    if (keywords == nullptr) {
        ownedKeywords.adoptInstead(from.createKeywords(errorCode));
        if (U_FAILURE(errorCode) || ownedKeywords.isNull()) { return; }
        keywords = ownedKeywords.getAlias();
    }
    const char* key;
    while ((key = keywords->next(nullptr, errorCode)) != nullptr) {
        auto value = from.getKeywordValue<CharString>(key, errorCode);
        if (U_FAILURE(errorCode)) { return; }
        if (uprv_strcmp(key, kAttributeKey) == 0) {
            transform(value.data(), value.length());
        }
        if (validate &&
            !_isKeywordValue(key, value.data(), value.length())) {
            errorCode = U_ILLEGAL_ARGUMENT_ERROR;
            return;
        }
        to.setKeywordValue(key, value.data(), errorCode);
        if (U_FAILURE(errorCode)) { return; }
    }
}

void
_clearUAttributesAndKeyType(Locale& locale, UErrorCode& errorCode)
{
    if (U_FAILURE(errorCode)) { return; }
    // Clear Unicode attributes
    locale.setKeywordValue(kAttributeKey, "", errorCode);

    // Clear all Unicode keyword values
    LocalPointer<icu::StringEnumeration> iter(locale.createUnicodeKeywords(errorCode));
    if (U_FAILURE(errorCode) || iter.isNull()) { return; }
    const char* key;
    while ((key = iter->next(nullptr, errorCode)) != nullptr) {
        locale.setUnicodeKeywordValue(key, nullptr, errorCode);
    }
}

void
_setUnicodeExtensions(Locale& locale, const CharString& value, UErrorCode& errorCode)
{
    if (U_FAILURE(errorCode)) { return; }
    // Add the unicode extensions to extensions_
    CharString locale_str("und-u-", errorCode);
    locale_str.append(value, errorCode);
    _copyExtensions(
        Locale::forLanguageTag(locale_str.data(), errorCode), nullptr,
        locale, false, errorCode);
}

}  // namespace

LocaleBuilder& LocaleBuilder::setExtension(char key, StringPiece value)
{
    if (U_FAILURE(status_)) { return *this; }
    if (!UPRV_ISALPHANUM(key)) {
        status_ = U_ILLEGAL_ARGUMENT_ERROR;
        return *this;
    }
    CharString value_str(value, status_);
    if (U_FAILURE(status_)) { return *this; }
    transform(value_str.data(), value_str.length());
    if (!value_str.isEmpty() &&
            !_isExtensionSubtags(key, value_str.data(), value_str.length())) {
        status_ = U_ILLEGAL_ARGUMENT_ERROR;
        return *this;
    }
    if (extensions_ == nullptr) {
        extensions_ = Locale::getRoot().clone();
        if (extensions_ == nullptr) {
            status_ = U_MEMORY_ALLOCATION_ERROR;
            return *this;
        }
    }
    if (uprv_tolower(key) != 'u') {
        // for t, x and others extension.
        extensions_->setKeywordValue(StringPiece(&key, 1), value_str.data(),
                                     status_);
        return *this;
    }
    _clearUAttributesAndKeyType(*extensions_, status_);
    if (U_FAILURE(status_)) { return *this; }
    if (!value.empty()) {
        _setUnicodeExtensions(*extensions_, value_str, status_);
    }
    return *this;
}

LocaleBuilder& LocaleBuilder::setUnicodeLocaleKeyword(
      StringPiece key, StringPiece type)
{
    if (U_FAILURE(status_)) { return *this; }
    if (!ultag_isUnicodeLocaleKey(key.data(), key.length()) ||
            (!type.empty() &&
                 !ultag_isUnicodeLocaleType(type.data(), type.length()))) {
      status_ = U_ILLEGAL_ARGUMENT_ERROR;
      return *this;
    }
    if (extensions_ == nullptr) {
        extensions_ = Locale::getRoot().clone();
        if (extensions_ == nullptr) {
            status_ = U_MEMORY_ALLOCATION_ERROR;
            return *this;
        }
    }
    extensions_->setUnicodeKeywordValue(key, type, status_);
    return *this;
}

LocaleBuilder& LocaleBuilder::addUnicodeLocaleAttribute(
    StringPiece value)
{
    CharString value_str(value, status_);
    if (U_FAILURE(status_)) { return *this; }
    transform(value_str.data(), value_str.length());
    if (!ultag_isUnicodeLocaleAttribute(value_str.data(), value_str.length())) {
        status_ = U_ILLEGAL_ARGUMENT_ERROR;
        return *this;
    }
    if (extensions_ == nullptr) {
        extensions_ = Locale::getRoot().clone();
        if (extensions_ == nullptr) {
            status_ = U_MEMORY_ALLOCATION_ERROR;
            return *this;
        }
        extensions_->setKeywordValue(kAttributeKey, value_str.data(), status_);
        return *this;
    }

    UErrorCode localErrorCode = U_ZERO_ERROR;
    auto attributes = extensions_->getKeywordValue<CharString>(kAttributeKey, localErrorCode);
    if (U_FAILURE(localErrorCode)) {
        CharString new_attributes(value_str.data(), status_);
        // No attributes, set the attribute.
        extensions_->setKeywordValue(kAttributeKey, new_attributes.data(), status_);
        return *this;
    }

    transform(attributes.data(),attributes.length());
    const char* start = attributes.data();
    const char* limit = attributes.data() + attributes.length();
    CharString new_attributes;
    bool inserted = false;
    while (start < limit) {
        if (!inserted) {
            int cmp = uprv_strcmp(start, value_str.data());
            if (cmp == 0) { return *this; }  // Found it in attributes: Just return
            if (cmp > 0) {
                if (!new_attributes.isEmpty()) new_attributes.append('_', status_);
                new_attributes.append(value_str.data(), status_);
                inserted = true;
            }
        }
        if (!new_attributes.isEmpty()) {
            new_attributes.append('_', status_);
        }
        new_attributes.append(start, status_);
        start += uprv_strlen(start) + 1;
    }
    if (!inserted) {
        if (!new_attributes.isEmpty()) {
            new_attributes.append('_', status_);
        }
        new_attributes.append(value_str.data(), status_);
    }
    // Not yet in the attributes, set the attribute.
    extensions_->setKeywordValue(kAttributeKey, new_attributes.data(), status_);
    return *this;
}

LocaleBuilder& LocaleBuilder::removeUnicodeLocaleAttribute(
    StringPiece value)
{
    CharString value_str(value, status_);
    if (U_FAILURE(status_)) { return *this; }
    transform(value_str.data(), value_str.length());
    if (!ultag_isUnicodeLocaleAttribute(value_str.data(), value_str.length())) {
        status_ = U_ILLEGAL_ARGUMENT_ERROR;
        return *this;
    }
    if (extensions_ == nullptr) { return *this; }
    UErrorCode localErrorCode = U_ZERO_ERROR;
    auto attributes = extensions_->getKeywordValue<CharString>(kAttributeKey, localErrorCode);
    // get failure, just return
    if (U_FAILURE(localErrorCode)) { return *this; }
    // Do not have any attributes, just return.
    if (attributes.isEmpty()) { return *this; }

    char* p = attributes.data();
    // Replace null terminiator in place for _ and - so later
    // we can use uprv_strcmp to compare.
    for (int32_t i = 0; i < attributes.length(); i++, p++) {
        *p = (*p == '_' || *p == '-') ? '\0' : uprv_tolower(*p);
    }

    const char* start = attributes.data();
    const char* limit = attributes.data() + attributes.length();
    CharString new_attributes;
    bool found = false;
    while (start < limit) {
        if (uprv_strcmp(start, value_str.data()) == 0) {
            found = true;
        } else {
            if (!new_attributes.isEmpty()) {
                new_attributes.append('_', status_);
            }
            new_attributes.append(start, status_);
        }
        start += uprv_strlen(start) + 1;
    }
    // Found the value in attributes, set the attribute.
    if (found) {
        extensions_->setKeywordValue(kAttributeKey, new_attributes.data(), status_);
    }
    return *this;
}

LocaleBuilder& LocaleBuilder::clear()
{
    status_ = U_ZERO_ERROR;
    language_[0] = 0;
    script_[0] = 0;
    region_[0] = 0;
    delete variant_;
    variant_ = nullptr;
    clearExtensions();
    return *this;
}

LocaleBuilder& LocaleBuilder::clearExtensions()
{
    delete extensions_;
    extensions_ = nullptr;
    return *this;
}

Locale makeBogusLocale() {
  Locale bogus;
  bogus.setToBogus();
  return bogus;
}

void LocaleBuilder::copyExtensionsFrom(const Locale& src, UErrorCode& errorCode)
{
    if (U_FAILURE(errorCode)) { return; }
    LocalPointer<icu::StringEnumeration> keywords(src.createKeywords(errorCode));
    if (U_FAILURE(errorCode) || keywords.isNull() || keywords->count(errorCode) == 0) {
        // Error, or no extensions to copy.
        return;
    }
    if (extensions_ == nullptr) {
        extensions_ = Locale::getRoot().clone();
        if (extensions_ == nullptr) {
            status_ = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
    }
    _copyExtensions(src, keywords.getAlias(), *extensions_, false, errorCode);
}

Locale LocaleBuilder::build(UErrorCode& errorCode)
{
    if (U_FAILURE(errorCode)) {
        return makeBogusLocale();
    }
    if (U_FAILURE(status_)) {
        errorCode = status_;
        return makeBogusLocale();
    }
    CharString locale_str(language_, errorCode);
    if (uprv_strlen(script_) > 0) {
        locale_str.append('-', errorCode).append(StringPiece(script_), errorCode);
    }
    if (uprv_strlen(region_) > 0) {
        locale_str.append('-', errorCode).append(StringPiece(region_), errorCode);
    }
    if (variant_ != nullptr) {
        locale_str.append('-', errorCode).append(StringPiece(variant_->data()), errorCode);
    }
    if (U_FAILURE(errorCode)) {
        return makeBogusLocale();
    }
    Locale product(locale_str.data());
    if (extensions_ != nullptr) {
        _copyExtensions(*extensions_, nullptr, product, true, errorCode);
    }
    if (U_FAILURE(errorCode)) {
        return makeBogusLocale();
    }
    return product;
}

UBool LocaleBuilder::copyErrorTo(UErrorCode &outErrorCode) const {
    if (U_FAILURE(outErrorCode)) {
        // Do not overwrite the older error code
        return true;
    }
    outErrorCode = status_;
    return U_FAILURE(outErrorCode);
}

U_NAMESPACE_END
