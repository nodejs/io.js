// © 2022 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/displayoptions.h"
#include "unicode/udisplayoptions.h"
#include "cstring.h"

U_NAMESPACE_BEGIN

DisplayOptions::Builder DisplayOptions::builder() { return {}; }

DisplayOptions::Builder DisplayOptions::copyToBuilder() const { return Builder(*this); }

DisplayOptions::DisplayOptions(const Builder &builder) {
    grammaticalCase = builder.grammaticalCase;
    nounClass = builder.nounClass;
    pluralCategory = builder.pluralCategory;
    capitalization = builder.capitalization;
    nameStyle = builder.nameStyle;
    displayLength = builder.displayLength;
    substituteHandling = builder.substituteHandling;
}

DisplayOptions::Builder::Builder() {
    // Sets default values.
    grammaticalCase = UDISPOPT_GRAMMATICAL_CASE_UNDEFINED;
    nounClass = UDISPOPT_NOUN_CLASS_UNDEFINED;
    pluralCategory = UDISPOPT_PLURAL_CATEGORY_UNDEFINED;
    capitalization = UDISPOPT_CAPITALIZATION_UNDEFINED;
    nameStyle = UDISPOPT_NAME_STYLE_UNDEFINED;
    displayLength = UDISPOPT_DISPLAY_LENGTH_UNDEFINED;
    substituteHandling = UDISPOPT_SUBSTITUTE_HANDLING_UNDEFINED;
}

DisplayOptions::Builder::Builder(const DisplayOptions &displayOptions) {
    grammaticalCase = displayOptions.grammaticalCase;
    nounClass = displayOptions.nounClass;
    pluralCategory = displayOptions.pluralCategory;
    capitalization = displayOptions.capitalization;
    nameStyle = displayOptions.nameStyle;
    displayLength = displayOptions.displayLength;
    substituteHandling = displayOptions.substituteHandling;
}

U_NAMESPACE_END

// C API ------------------------------------------------------------------- ***

U_NAMESPACE_USE

namespace {

const char *grammaticalCaseIds[] = {
    "undefined",           // 0
    "ablative",            // 1
    "accusative",          // 2
    "comitative",          // 3
    "dative",              // 4
    "ergative",            // 5
    "genitive",            // 6
    "instrumental",        // 7
    "locative",            // 8
    "locative_copulative", // 9
    "nominative",          // 10
    "oblique",             // 11
    "prepositional",       // 12
    "sociative",           // 13
    "vocative",            // 14
};

} // namespace

U_CAPI const char * U_EXPORT2
udispopt_getGrammaticalCaseIdentifier(UDisplayOptionsGrammaticalCase grammaticalCase) {
    if (grammaticalCase >= 0 && grammaticalCase < UPRV_LENGTHOF(grammaticalCaseIds)) {
        return grammaticalCaseIds[grammaticalCase];
    }

    return grammaticalCaseIds[0];
}

U_CAPI UDisplayOptionsGrammaticalCase U_EXPORT2
udispopt_fromGrammaticalCaseIdentifier(const char *identifier) {
    for (int32_t i = 0; i < UPRV_LENGTHOF(grammaticalCaseIds); i++) {
        if (uprv_strcmp(identifier, grammaticalCaseIds[i]) == 0) {
            return static_cast<UDisplayOptionsGrammaticalCase>(i);
        }
    }

    return UDISPOPT_GRAMMATICAL_CASE_UNDEFINED;
}

namespace {

const char *pluralCategoryIds[] = {
    "undefined", // 0
    "zero",      // 1
    "one",       // 2
    "two",       // 3
    "few",       // 4
    "many",      // 5
    "other",     // 6
};

} // namespace

U_CAPI const char * U_EXPORT2
udispopt_getPluralCategoryIdentifier(UDisplayOptionsPluralCategory pluralCategory) {
    if (pluralCategory >= 0 && pluralCategory < UPRV_LENGTHOF(pluralCategoryIds)) {
        return pluralCategoryIds[pluralCategory];
    }

    return pluralCategoryIds[0];
}

U_CAPI UDisplayOptionsPluralCategory U_EXPORT2
udispopt_fromPluralCategoryIdentifier(const char *identifier) {
    for (int32_t i = 0; i < UPRV_LENGTHOF(pluralCategoryIds); i++) {
        if (uprv_strcmp(identifier, pluralCategoryIds[i]) == 0) {
            return static_cast<UDisplayOptionsPluralCategory>(i);
        }
    }

    return UDISPOPT_PLURAL_CATEGORY_UNDEFINED;
}

namespace {

const char *nounClassIds[] = {
    "undefined", // 0
    "other",     // 1
    "neuter",    // 2
    "feminine",  // 3
    "masculine", // 4
    "animate",   // 5
    "inanimate", // 6
    "personal",  // 7
    "common",    // 8
};

} // namespace

U_CAPI const char * U_EXPORT2
udispopt_getNounClassIdentifier(UDisplayOptionsNounClass nounClass) {
    if (nounClass >= 0 && nounClass < UPRV_LENGTHOF(nounClassIds)) {
        return nounClassIds[nounClass];
    }

    return nounClassIds[0];
}

U_CAPI UDisplayOptionsNounClass U_EXPORT2
udispopt_fromNounClassIdentifier(const char *identifier) {
    for (int32_t i = 0; i < UPRV_LENGTHOF(nounClassIds); i++) {
        if (uprv_strcmp(identifier, nounClassIds[i]) == 0) {
            return static_cast<UDisplayOptionsNounClass>(i);
        }
    }

    return UDISPOPT_NOUN_CLASS_UNDEFINED;
}

#endif /* #if !UCONFIG_NO_FORMATTING */
