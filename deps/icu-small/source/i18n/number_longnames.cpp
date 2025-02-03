// © 2017 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include <cstdlib>

#include "unicode/simpleformatter.h"
#include "unicode/ures.h"
#include "unicode/plurrule.h"
#include "unicode/strenum.h"
#include "ureslocs.h"
#include "charstr.h"
#include "uresimp.h"
#include "measunit_impl.h"
#include "number_longnames.h"
#include "number_microprops.h"
#include <algorithm>
#include "cstring.h"
#include "util.h"
#include "sharedpluralrules.h"

using namespace icu;
using namespace icu::number;
using namespace icu::number::impl;

namespace {

/**
 * Display Name (this format has no placeholder).
 *
 * Used as an index into the LongNameHandler::simpleFormats array. Units
 * resources cover the normal set of PluralRules keys, as well as `dnam` and
 * `per` forms.
 */
constexpr int32_t DNAM_INDEX = StandardPlural::Form::COUNT;
/**
 * "per" form (e.g. "{0} per day" is day's "per" form).
 *
 * Used as an index into the LongNameHandler::simpleFormats array. Units
 * resources cover the normal set of PluralRules keys, as well as `dnam` and
 * `per` forms.
 */
constexpr int32_t PER_INDEX = StandardPlural::Form::COUNT + 1;
/**
 * Gender of the word, in languages with grammatical gender.
 */
constexpr int32_t GENDER_INDEX = StandardPlural::Form::COUNT + 2;
// Number of keys in the array populated by PluralTableSink.
constexpr int32_t ARRAY_LENGTH = StandardPlural::Form::COUNT + 3;

// TODO(icu-units#28): load this list from resources, after creating a "&set"
// function for use in ldml2icu rules.
const int32_t GENDER_COUNT = 7;
const char *gGenders[GENDER_COUNT] = {"animate",   "common", "feminine", "inanimate",
                                      "masculine", "neuter", "personal"};

// Converts a UnicodeString to a const char*, either pointing to a string in
// gGenders, or pointing to an empty string if an appropriate string was not
// found.
const char *getGenderString(UnicodeString uGender, UErrorCode status) {
    if (uGender.length() == 0) {
        return "";
    }
    CharString gender;
    gender.appendInvariantChars(uGender, status);
    if (U_FAILURE(status)) {
        return "";
    }
    int32_t first = 0;
    int32_t last = GENDER_COUNT;
    while (first < last) {
        int32_t mid = (first + last) / 2;
        int32_t cmp = uprv_strcmp(gender.data(), gGenders[mid]);
        if (cmp == 0) {
            return gGenders[mid];
        } else if (cmp > 0) {
            first = mid + 1;
        } else if (cmp < 0) {
            last = mid;
        }
    }
    // We don't return an error in case our gGenders list is incomplete in
    // production.
    //
    // TODO(icu-units#28): a unit test checking all locales' genders are covered
    // by gGenders? Else load a complete list of genders found in
    // grammaticalFeatures in an initOnce.
    return "";
}

// Returns the array index that corresponds to the given pluralKeyword.
int32_t getIndex(const char* pluralKeyword, UErrorCode& status) {
    // pluralKeyword can also be "dnam", "per", or "gender"
    switch (*pluralKeyword) {
    case 'd':
        if (uprv_strcmp(pluralKeyword + 1, "nam") == 0) {
            return DNAM_INDEX;
        }
        break;
    case 'g':
        if (uprv_strcmp(pluralKeyword + 1, "ender") == 0) {
            return GENDER_INDEX;
        }
        break;
    case 'p':
        if (uprv_strcmp(pluralKeyword + 1, "er") == 0) {
            return PER_INDEX;
        }
        break;
    default:
        break;
    }
    StandardPlural::Form plural = StandardPlural::fromString(pluralKeyword, status);
    return plural;
}

// Selects a string out of the `strings` array which corresponds to the
// specified plural form, with fallback to the OTHER form.
//
// The `strings` array must have ARRAY_LENGTH items: one corresponding to each
// of the plural forms, plus a display name ("dnam") and a "per" form.
UnicodeString getWithPlural(
        const UnicodeString* strings,
        StandardPlural::Form plural,
        UErrorCode& status) {
    UnicodeString result = strings[plural];
    if (result.isBogus()) {
        result = strings[StandardPlural::Form::OTHER];
    }
    if (result.isBogus()) {
        // There should always be data in the "other" plural variant.
        status = U_INTERNAL_PROGRAM_ERROR;
    }
    return result;
}

enum PlaceholderPosition { PH_EMPTY, PH_NONE, PH_BEGINNING, PH_MIDDLE, PH_END };

/**
 * Returns three outputs extracted from pattern.
 *
 * @param coreUnit is extracted as per Extract(...) in the spec:
 *   https://unicode.org/reports/tr35/tr35-general.html#compound-units
 * @param PlaceholderPosition indicates where in the string the placeholder was
 *   found.
 * @param joinerChar Iff the placeholder was at the beginning or end, joinerChar
 *   contains the space character (if any) that separated the placeholder from
 *   the rest of the pattern. Otherwise, joinerChar is set to NUL. Only one
 *   space character is considered.
 */
void extractCorePattern(const UnicodeString &pattern,
                        UnicodeString &coreUnit,
                        PlaceholderPosition &placeholderPosition,
                        char16_t &joinerChar) {
    joinerChar = 0;
    int32_t len = pattern.length();
    if (pattern.startsWith(u"{0}", 3)) {
        placeholderPosition = PH_BEGINNING;
        if (u_isJavaSpaceChar(pattern[3])) {
            joinerChar = pattern[3];
            coreUnit.setTo(pattern, 4, len - 4);
        } else {
            coreUnit.setTo(pattern, 3, len - 3);
        }
    } else if (pattern.endsWith(u"{0}", 3)) {
        placeholderPosition = PH_END;
        if (u_isJavaSpaceChar(pattern[len - 4])) {
            coreUnit.setTo(pattern, 0, len - 4);
            joinerChar = pattern[len - 4];
        } else {
            coreUnit.setTo(pattern, 0, len - 3);
        }
    } else if (pattern.indexOf(u"{0}", 3, 1, len - 2) == -1) {
        placeholderPosition = PH_NONE;
        coreUnit = pattern;
    } else {
        placeholderPosition = PH_MIDDLE;
        coreUnit = pattern;
    }
}

//////////////////////////
/// BEGIN DATA LOADING ///
//////////////////////////

// Gets the gender of a built-in unit: unit must be a built-in. Returns an empty
// string both in case of unknown gender and in case of unknown unit.
UnicodeString
getGenderForBuiltin(const Locale &locale, const MeasureUnit &builtinUnit, UErrorCode &status) {
    LocalUResourceBundlePointer unitsBundle(ures_open(U_ICUDATA_UNIT, locale.getName(), &status));
    if (U_FAILURE(status)) { return {}; }

    // Map duration-year-person, duration-week-person, etc. to duration-year, duration-week, ...
    // TODO(ICU-20400): Get duration-*-person data properly with aliases.
    StringPiece subtypeForResource;
    int32_t subtypeLen = static_cast<int32_t>(uprv_strlen(builtinUnit.getSubtype()));
    if (subtypeLen > 7 && uprv_strcmp(builtinUnit.getSubtype() + subtypeLen - 7, "-person") == 0) {
        subtypeForResource = {builtinUnit.getSubtype(), subtypeLen - 7};
    } else {
        subtypeForResource = builtinUnit.getSubtype();
    }

    CharString key;
    key.append("units/", status);
    key.append(builtinUnit.getType(), status);
    key.append("/", status);
    key.append(subtypeForResource, status);
    key.append("/gender", status);

    UErrorCode localStatus = status;
    int32_t resultLen = 0;
    const char16_t *result =
        ures_getStringByKeyWithFallback(unitsBundle.getAlias(), key.data(), &resultLen, &localStatus);
    if (U_SUCCESS(localStatus)) {
        status = localStatus;
        return UnicodeString(true, result, resultLen);
    } else {
        // TODO(icu-units#28): "$unitRes/gender" does not exist. Do we want to
        // check whether the parent "$unitRes" exists? Then we could return
        // U_MISSING_RESOURCE_ERROR for incorrect usage (e.g. builtinUnit not
        // being a builtin).
        return {};
    }
}

// Loads data from a resource tree with paths matching
// $key/$pluralForm/$gender/$case, with lateral inheritance for missing cases
// and genders.
//
// An InflectedPluralSink is configured to load data for a specific gender and
// case. It loads all plural forms, because selection between plural forms is
// dependent upon the value being formatted.
//
// See data/unit/de.txt and data/unit/fr.txt for examples - take a look at
// units/compound/power2: German has case, French has differences for gender,
// but no case.
//
// TODO(icu-units#138): Conceptually similar to PluralTableSink, however the
// tree structures are different. After homogenizing the structures, we may be
// able to unify the two classes.
//
// TODO: Spec violation: expects presence of "count" - does not fallback to an
// absent "count"! If this fallback were added, getCompoundValue could be
// superseded?
class InflectedPluralSink : public ResourceSink {
  public:
    // Accepts `char*` rather than StringPiece because
    // ResourceTable::findValue(...) requires a null-terminated `char*`.
    //
    // NOTE: outArray MUST have a length of at least ARRAY_LENGTH. No bounds
    // checking is performed.
    explicit InflectedPluralSink(const char *gender, const char *caseVariant, UnicodeString *outArray)
        : gender(gender), caseVariant(caseVariant), outArray(outArray) {
        // Initialize the array to bogus strings.
        for (int32_t i = 0; i < ARRAY_LENGTH; i++) {
            outArray[i].setToBogus();
        }
    }

    // See ResourceSink::put().
    void put(const char *key, ResourceValue &value, UBool /*noFallback*/, UErrorCode &status) override {
        int32_t pluralIndex = getIndex(key, status);
        if (U_FAILURE(status)) { return; }
        if (!outArray[pluralIndex].isBogus()) {
            // We already have a pattern
            return;
        }
        ResourceTable genderTable = value.getTable(status);
        ResourceTable caseTable; // This instance has to outlive `value`
        if (loadForPluralForm(genderTable, caseTable, value, status)) {
            outArray[pluralIndex] = value.getUnicodeString(status);
        }
    }

  private:
    // Tries to load data for the configured gender from `genderTable`. Returns
    // true if found, returning the data in `value`. The returned data will be
    // for the configured gender if found, falling back to "neuter" and
    // no-gender if not. The caseTable parameter holds the intermediate
    // ResourceTable for the sake of lifetime management.
    bool loadForPluralForm(const ResourceTable &genderTable,
                           ResourceTable &caseTable,
                           ResourceValue &value,
                           UErrorCode &status) {
        if (uprv_strcmp(gender, "") != 0) {
            if (loadForGender(genderTable, gender, caseTable, value, status)) {
                return true;
            }
            if (uprv_strcmp(gender, "neuter") != 0 &&
                loadForGender(genderTable, "neuter", caseTable, value, status)) {
                return true;
            }
        }
        if (loadForGender(genderTable, "_", caseTable, value, status)) {
            return true;
        }
        return false;
    }

    // Tries to load data for the given gender from `genderTable`. Returns true
    // if found, returning the data in `value`. The returned data will be for
    // the configured case if found, falling back to "nominative" and no-case if
    // not.
    bool loadForGender(const ResourceTable &genderTable,
                       const char *genderVal,
                       ResourceTable &caseTable,
                       ResourceValue &value,
                       UErrorCode &status) {
        if (!genderTable.findValue(genderVal, value)) {
            return false;
        }
        caseTable = value.getTable(status);
        if (uprv_strcmp(caseVariant, "") != 0) {
            if (loadForCase(caseTable, caseVariant, value)) {
                return true;
            }
            if (uprv_strcmp(caseVariant, "nominative") != 0 &&
                loadForCase(caseTable, "nominative", value)) {
                return true;
            }
        }
        if (loadForCase(caseTable, "_", value)) {
            return true;
        }
        return false;
    }

    // Tries to load data for the given case from `caseTable`. Returns true if
    // found, returning the data in `value`.
    bool loadForCase(const ResourceTable &caseTable, const char *caseValue, ResourceValue &value) {
        if (!caseTable.findValue(caseValue, value)) {
            return false;
        }
        return true;
    }

    const char *gender;
    const char *caseVariant;
    UnicodeString *outArray;
};

// Fetches localised formatting patterns for the given subKey. See documentation
// for InflectedPluralSink for details.
//
// Data is loaded for the appropriate unit width, with missing data filled in
// from unitsShort.
void getInflectedMeasureData(StringPiece subKey,
                             const Locale &locale,
                             const UNumberUnitWidth &width,
                             const char *gender,
                             const char *caseVariant,
                             UnicodeString *outArray,
                             UErrorCode &status) {
    InflectedPluralSink sink(gender, caseVariant, outArray);
    LocalUResourceBundlePointer unitsBundle(ures_open(U_ICUDATA_UNIT, locale.getName(), &status));
    if (U_FAILURE(status)) { return; }

    CharString key;
    key.append("units", status);
    if (width == UNUM_UNIT_WIDTH_NARROW) {
        key.append("Narrow", status);
    } else if (width == UNUM_UNIT_WIDTH_SHORT) {
        key.append("Short", status);
    }
    key.append("/", status);
    key.append(subKey, status);

    UErrorCode localStatus = status;
    ures_getAllChildrenWithFallback(unitsBundle.getAlias(), key.data(), sink, localStatus);
    if (width == UNUM_UNIT_WIDTH_SHORT) {
        status = localStatus;
        return;
    }
}

class PluralTableSink : public ResourceSink {
  public:
    // NOTE: outArray MUST have a length of at least ARRAY_LENGTH. No bounds
    // checking is performed.
    explicit PluralTableSink(UnicodeString *outArray) : outArray(outArray) {
        // Initialize the array to bogus strings.
        for (int32_t i = 0; i < ARRAY_LENGTH; i++) {
            outArray[i].setToBogus();
        }
    }

    void put(const char *key, ResourceValue &value, UBool /*noFallback*/, UErrorCode &status) override {
        if (uprv_strcmp(key, "case") == 0) {
            return;
        }
        int32_t index = getIndex(key, status);
        if (U_FAILURE(status)) { return; }
        if (!outArray[index].isBogus()) {
            return;
        }
        outArray[index] = value.getUnicodeString(status);
        if (U_FAILURE(status)) { return; }
    }

  private:
    UnicodeString *outArray;
};

/**
 * Populates outArray with `locale`-specific values for `unit` through use of
 * PluralTableSink. Only the set of basic units are supported!
 *
 * Reading from resources *unitsNarrow* and *unitsShort* (for width
 * UNUM_UNIT_WIDTH_NARROW), or just *unitsShort* (for width
 * UNUM_UNIT_WIDTH_SHORT). For other widths, it reads just "units".
 *
 * @param unit must be a built-in unit, i.e. must have a type and subtype,
 *     listed in gTypes and gSubTypes in measunit.cpp.
 * @param unitDisplayCase the empty string and "nominative" are treated the
 *     same. For other cases, strings for the requested case are used if found.
 *     (For any missing case-specific data, we fall back to nominative.)
 * @param outArray must be of fixed length ARRAY_LENGTH.
 */
void getMeasureData(const Locale &locale,
                    const MeasureUnit &unit,
                    const UNumberUnitWidth &width,
                    const char *unitDisplayCase,
                    UnicodeString *outArray,
                    UErrorCode &status) {
    PluralTableSink sink(outArray);
    LocalUResourceBundlePointer unitsBundle(ures_open(U_ICUDATA_UNIT, locale.getName(), &status));
    if (U_FAILURE(status)) { return; }

    CharString subKey;
    subKey.append("/", status);
    subKey.append(unit.getType(), status);
    subKey.append("/", status);

    // Check if unitSubType is an alias or not.
    LocalUResourceBundlePointer aliasBundle(ures_open(U_ICUDATA_ALIAS, "metadata", &status));

    UErrorCode aliasStatus = status;
    StackUResourceBundle aliasFillIn;
    CharString aliasKey;
    aliasKey.append("alias/unit/", aliasStatus);
    aliasKey.append(unit.getSubtype(), aliasStatus);
    aliasKey.append("/replacement", aliasStatus);
    ures_getByKeyWithFallback(aliasBundle.getAlias(), aliasKey.data(), aliasFillIn.getAlias(),
                              &aliasStatus);
    CharString unitSubType;
    if (!U_FAILURE(aliasStatus)) {
        // This means the subType is an alias. Then, replace unitSubType with the replacement.
        auto replacement = ures_getUnicodeString(aliasFillIn.getAlias(), &status);
        unitSubType.appendInvariantChars(replacement, status);
    } else {
        unitSubType.append(unit.getSubtype(), status);
    }

    // Map duration-year-person, duration-week-person, etc. to duration-year, duration-week, ...
    // TODO(ICU-20400): Get duration-*-person data properly with aliases.
    int32_t subtypeLen = static_cast<int32_t>(uprv_strlen(unitSubType.data()));
    if (subtypeLen > 7 && uprv_strcmp(unitSubType.data() + subtypeLen - 7, "-person") == 0) {
        subKey.append({unitSubType.data(), subtypeLen - 7}, status);
    } else {
        subKey.append({unitSubType.data(), subtypeLen}, status);
    }

    if (width != UNUM_UNIT_WIDTH_FULL_NAME) {
        UErrorCode localStatus = status;
        CharString genderKey;
        genderKey.append("units", localStatus);
        genderKey.append(subKey, localStatus);
        genderKey.append("/gender", localStatus);
        StackUResourceBundle fillIn;
        ures_getByKeyWithFallback(unitsBundle.getAlias(), genderKey.data(), fillIn.getAlias(),
                                  &localStatus);
        outArray[GENDER_INDEX] = ures_getUnicodeString(fillIn.getAlias(), &localStatus);
    }

    CharString key;
    key.append("units", status);
    if (width == UNUM_UNIT_WIDTH_NARROW) {
        key.append("Narrow", status);
    } else if (width == UNUM_UNIT_WIDTH_SHORT) {
        key.append("Short", status);
    }
    key.append(subKey, status);

    // Grab desired case first, if available. Then grab no-case data to fill in
    // the gaps.
    if (width == UNUM_UNIT_WIDTH_FULL_NAME && unitDisplayCase[0] != 0) {
        CharString caseKey;
        caseKey.append(key, status);
        caseKey.append("/case/", status);
        caseKey.append(unitDisplayCase, status);

        UErrorCode localStatus = U_ZERO_ERROR;
        // TODO(icu-units#138): our fallback logic is not spec-compliant:
        // lateral fallback should happen before locale fallback. Switch to
        // getInflectedMeasureData after homogenizing data format? Find a unit
        // test case that demonstrates the incorrect fallback logic (via
        // regional variant of an inflected language?)
        ures_getAllChildrenWithFallback(unitsBundle.getAlias(), caseKey.data(), sink, localStatus);
    }

    // TODO(icu-units#138): our fallback logic is not spec-compliant: we
    // check the given case, then go straight to the no-case data. The spec
    // states we should first look for case="nominative". As part of #138,
    // either get the spec changed, or add unit tests that warn us if
    // case="nominative" data differs from no-case data?
    UErrorCode localStatus = U_ZERO_ERROR;
    ures_getAllChildrenWithFallback(unitsBundle.getAlias(), key.data(), sink, localStatus);
    if (width == UNUM_UNIT_WIDTH_SHORT) {
        if (U_FAILURE(localStatus)) {
            status = localStatus;
        }
        return;
    }
}

// NOTE: outArray MUST have a length of at least ARRAY_LENGTH.
void getCurrencyLongNameData(const Locale &locale, const CurrencyUnit &currency, UnicodeString *outArray,
                             UErrorCode &status) {
    // In ICU4J, this method gets a CurrencyData from CurrencyData.provider.
    // TODO(ICU4J): Implement this without going through CurrencyData, like in ICU4C?
    PluralTableSink sink(outArray);
    // Here all outArray entries are bogus.
    LocalUResourceBundlePointer unitsBundle(ures_open(U_ICUDATA_CURR, locale.getName(), &status));
    if (U_FAILURE(status)) { return; }
    ures_getAllChildrenWithFallback(unitsBundle.getAlias(), "CurrencyUnitPatterns", sink, status);
    if (U_FAILURE(status)) { return; }
    // Here the outArray[] entries are filled in with any CurrencyUnitPatterns data for locale,
    // or if there is no CurrencyUnitPatterns data for locale since the patterns all inherited
    // from the "other" pattern in root (which is true for many locales in CLDR 46), then only
    // the "other" entry has a currency pattern. So now what we do is: For all valid plural keywords
    // for the locale, if the corresponding outArray[] entry is bogus, fill it in from the "other"
    // entry. In the longer run, clients of this should instead consider using CurrencyPluralInfo
    // (see i18n/unicode/currpinf.h).
    UErrorCode localStatus = U_ZERO_ERROR;
    const SharedPluralRules *pr = PluralRules::createSharedInstance(
            locale, UPLURAL_TYPE_CARDINAL, localStatus);
    if (U_SUCCESS(localStatus)) {
        LocalPointer<StringEnumeration> keywords((*pr)->getKeywords(localStatus), localStatus);
        if (U_SUCCESS(localStatus)) {
            const char* keyword;
            while (((keyword = keywords->next(nullptr, localStatus)) != nullptr) && U_SUCCESS(localStatus)) {
                int32_t index = StandardPlural::indexOrOtherIndexFromString(keyword);
                if (index != StandardPlural::Form::OTHER && outArray[index].isBogus()) {
                    outArray[index].setTo(outArray[StandardPlural::Form::OTHER]);
                }
            }
        }
        pr->removeRef();
    }

    for (int32_t i = 0; i < StandardPlural::Form::COUNT; i++) {
        UnicodeString &pattern = outArray[i];
        if (pattern.isBogus()) {
            continue;
        }
        int32_t longNameLen = 0;
        const char16_t *longName = ucurr_getPluralName(
                currency.getISOCurrency(),
                locale.getName(),
                nullptr /* isChoiceFormat */,
                StandardPlural::getKeyword(static_cast<StandardPlural::Form>(i)),
                &longNameLen,
                &status);
        // Example pattern from data: "{0} {1}"
        // Example output after find-and-replace: "{0} US dollars"
        pattern.findAndReplace(UnicodeString(u"{1}"), UnicodeString(longName, longNameLen));
    }
}

UnicodeString getCompoundValue(StringPiece compoundKey,
                               const Locale &locale,
                               const UNumberUnitWidth &width,
                               UErrorCode &status) {
    LocalUResourceBundlePointer unitsBundle(ures_open(U_ICUDATA_UNIT, locale.getName(), &status));
    if (U_FAILURE(status)) { return {}; }
    CharString key;
    key.append("units", status);
    if (width == UNUM_UNIT_WIDTH_NARROW) {
        key.append("Narrow", status);
    } else if (width == UNUM_UNIT_WIDTH_SHORT) {
        key.append("Short", status);
    }
    key.append("/compound/", status);
    key.append(compoundKey, status);

    UErrorCode localStatus = status;
    int32_t len = 0;
    const char16_t *ptr =
        ures_getStringByKeyWithFallback(unitsBundle.getAlias(), key.data(), &len, &localStatus);
    if (U_FAILURE(localStatus) && width != UNUM_UNIT_WIDTH_SHORT) {
        // Fall back to short, which contains more compound data
        key.clear();
        key.append("unitsShort/compound/", status);
        key.append(compoundKey, status);
        ptr = ures_getStringByKeyWithFallback(unitsBundle.getAlias(), key.data(), &len, &status);
    } else {
        status = localStatus;
    }
    if (U_FAILURE(status)) {
        return {};
    }
    return UnicodeString(ptr, len);
}

/**
 * Loads and applies deriveComponent rules from CLDR's grammaticalFeatures.xml.
 *
 * Consider a deriveComponent rule that looks like this:
 *
 *     <deriveComponent feature="case" structure="per" value0="compound" value1="nominative"/>
 *
 * Instantiating an instance as follows:
 *
 *     DerivedComponents d(loc, "case", "per");
 *
 * Applying the rule in the XML element above, `d.value0("foo")` will be "foo",
 * and `d.value1("foo")` will be "nominative".
 *
 * The values returned by value0(...) and value1(...) are valid only while the
 * instance exists. In case of any kind of failure, value0(...) and value1(...)
 * will return "".
 */
class DerivedComponents {
  public:
    /**
     * Constructor.
     *
     * The feature and structure parameters must be null-terminated. The string
     * referenced by compoundValue must exist for longer than the
     * DerivedComponents instance.
     */
    DerivedComponents(const Locale &locale, const char *feature, const char *structure) {
        StackUResourceBundle derivationsBundle, stackBundle;
        ures_openDirectFillIn(derivationsBundle.getAlias(), nullptr, "grammaticalFeatures", &status);
        ures_getByKey(derivationsBundle.getAlias(), "grammaticalData", derivationsBundle.getAlias(),
                      &status);
        ures_getByKey(derivationsBundle.getAlias(), "derivations", derivationsBundle.getAlias(),
                      &status);
        if (U_FAILURE(status)) {
            return;
        }
        UErrorCode localStatus = U_ZERO_ERROR;
        // TODO(icu-units#28): use standard normal locale resolution algorithms
        // rather than just grabbing language:
        ures_getByKey(derivationsBundle.getAlias(), locale.getLanguage(), stackBundle.getAlias(),
                      &localStatus);
        // TODO(icu-units#28):
        // - code currently assumes if the locale exists, the rules are there -
        //   instead of falling back to root when the requested rule is missing.
        // - investigate ures.h functions, see if one that uses res_findResource()
        //   might be better (or use res_findResource directly), or maybe help
        //   improve ures documentation to guide function selection?
        if (localStatus == U_MISSING_RESOURCE_ERROR) {
            ures_getByKey(derivationsBundle.getAlias(), "root", stackBundle.getAlias(), &status);
        } else {
            status = localStatus;
        }
        ures_getByKey(stackBundle.getAlias(), "component", stackBundle.getAlias(), &status);
        ures_getByKey(stackBundle.getAlias(), feature, stackBundle.getAlias(), &status);
        ures_getByKey(stackBundle.getAlias(), structure, stackBundle.getAlias(), &status);
        UnicodeString val0 = ures_getUnicodeStringByIndex(stackBundle.getAlias(), 0, &status);
        UnicodeString val1 = ures_getUnicodeStringByIndex(stackBundle.getAlias(), 1, &status);
        if (U_SUCCESS(status)) {
            if (val0.compare(UnicodeString(u"compound")) == 0) {
                compound0_ = true;
            } else {
                compound0_ = false;
                value0_.appendInvariantChars(val0, status);
            }
            if (val1.compare(UnicodeString(u"compound")) == 0) {
                compound1_ = true;
            } else {
                compound1_ = false;
                value1_.appendInvariantChars(val1, status);
            }
        }
    }

    // Returns a StringPiece that is only valid as long as the instance exists.
    StringPiece value0(const StringPiece compoundValue) const {
        return compound0_ ? compoundValue : value0_.toStringPiece();
    }

    // Returns a StringPiece that is only valid as long as the instance exists.
    StringPiece value1(const StringPiece compoundValue) const {
        return compound1_ ? compoundValue : value1_.toStringPiece();
    }

    // Returns a char* that is only valid as long as the instance exists.
    const char *value0(const char *compoundValue) const {
        return compound0_ ? compoundValue : value0_.data();
    }

    // Returns a char* that is only valid as long as the instance exists.
    const char *value1(const char *compoundValue) const {
        return compound1_ ? compoundValue : value1_.data();
    }

  private:
    UErrorCode status = U_ZERO_ERROR;

    // Holds strings referred to by value0 and value1;
    bool compound0_ = false, compound1_ = false;
    CharString value0_, value1_;
};

// TODO(icu-units#28): test somehow? Associate with an ICU ticket for adding
// testsuite support for testing with synthetic data?
/**
 * Loads and returns the value in rules that look like these:
 *
 * <deriveCompound feature="gender" structure="per" value="0"/>
 * <deriveCompound feature="gender" structure="times" value="1"/>
 *
 * Currently a fake example, but spec compliant:
 * <deriveCompound feature="gender" structure="power" value="feminine"/>
 *
 * NOTE: If U_FAILURE(status), returns an empty string.
 */ 
UnicodeString
getDeriveCompoundRule(Locale locale, const char *feature, const char *structure, UErrorCode &status) {
    StackUResourceBundle derivationsBundle, stackBundle;
    ures_openDirectFillIn(derivationsBundle.getAlias(), nullptr, "grammaticalFeatures", &status);
    ures_getByKey(derivationsBundle.getAlias(), "grammaticalData", derivationsBundle.getAlias(),
                  &status);
    ures_getByKey(derivationsBundle.getAlias(), "derivations", derivationsBundle.getAlias(), &status);
    // TODO: use standard normal locale resolution algorithms rather than just grabbing language:
    ures_getByKey(derivationsBundle.getAlias(), locale.getLanguage(), stackBundle.getAlias(), &status);
    // TODO:
    // - code currently assumes if the locale exists, the rules are there -
    //   instead of falling back to root when the requested rule is missing.
    // - investigate ures.h functions, see if one that uses res_findResource()
    //   might be better (or use res_findResource directly), or maybe help
    //   improve ures documentation to guide function selection?
    if (status == U_MISSING_RESOURCE_ERROR) {
        status = U_ZERO_ERROR;
        ures_getByKey(derivationsBundle.getAlias(), "root", stackBundle.getAlias(), &status);
    }
    ures_getByKey(stackBundle.getAlias(), "compound", stackBundle.getAlias(), &status);
    ures_getByKey(stackBundle.getAlias(), feature, stackBundle.getAlias(), &status);
    UnicodeString uVal = ures_getUnicodeStringByKey(stackBundle.getAlias(), structure, &status);
    if (U_FAILURE(status)) {
        return {};
    }
    U_ASSERT(!uVal.isBogus());
    return uVal;
}

// Returns the gender string for structures following these rules:
//
// <deriveCompound feature="gender" structure="per" value="0"/>
// <deriveCompound feature="gender" structure="times" value="1"/>
//
// Fake example:
// <deriveCompound feature="gender" structure="power" value="feminine"/>
//
// data0 and data1 should be pattern arrays (UnicodeString[ARRAY_SIZE]) that
// correspond to value="0" and value="1".
//
// Pass a nullptr to data1 if the structure has no concept of value="1" (e.g.
// "prefix" doesn't).
UnicodeString getDerivedGender(Locale locale,
                               const char *structure,
                               UnicodeString *data0,
                               UnicodeString *data1,
                               UErrorCode &status) {
    UnicodeString val = getDeriveCompoundRule(locale, "gender", structure, status);
    if (val.length() == 1) {
        switch (val[0]) {
        case u'0':
            return data0[GENDER_INDEX];
        case u'1':
            if (data1 == nullptr) {
                return {};
            }
            return data1[GENDER_INDEX];
        }
    }
    return val;
}

////////////////////////
/// END DATA LOADING ///
////////////////////////

// TODO: promote this somewhere? It's based on patternprops.cpp' trimWhitespace
const char16_t *trimSpaceChars(const char16_t *s, int32_t &length) {
    if (length <= 0 || (!u_isJavaSpaceChar(s[0]) && !u_isJavaSpaceChar(s[length - 1]))) {
        return s;
    }
    int32_t start = 0;
    int32_t limit = length;
    while (start < limit && u_isJavaSpaceChar(s[start])) {
        ++start;
    }
    if (start < limit) {
        // There is non-white space at start; we will not move limit below that,
        // so we need not test start<limit in the loop.
        while (u_isJavaSpaceChar(s[limit - 1])) {
            --limit;
        }
    }
    length = limit - start;
    return s + start;
}

/**
 * Calculates the gender of an arbitrary unit: this is the *second*
 * implementation of an algorithm to do this:
 *
 * Gender is also calculated in "processPatternTimes": that code path is "bottom
 * up", loading the gender for every component of a compound unit (at the same
 * time as loading the Long Names formatting patterns), even if the gender is
 * unneeded, then combining the single units' genders into the compound unit's
 * gender, according to the rules. This algorithm does a lazier "top-down"
 * evaluation, starting with the compound unit, calculating which single unit's
 * gender is needed by breaking it down according to the rules, and then loading
 * only the gender of the one single unit who's gender is needed.
 *
 * For future refactorings:
 * 1. we could drop processPatternTimes' gender calculation and just call this
 *    function: for UNUM_UNIT_WIDTH_FULL_NAME, the unit gender is in the very
 *    same table as the formatting patterns, so loading it then may be
 *    efficient. For other unit widths however, it needs to be explicitly looked
 *    up anyway.
 * 2. alternatively, if CLDR is providing all the genders we need such that we
 *    don't need to calculate them in ICU anymore, we could drop this function
 *    and keep only processPatternTimes' calculation. (And optimise it a bit?)
 *
 * @param locale The desired locale.
 * @param unit The measure unit to calculate the gender for.
 * @return The gender string for the unit, or an empty string if unknown or
 *     ungendered.
 */
UnicodeString calculateGenderForUnit(const Locale &locale, const MeasureUnit &unit, UErrorCode &status) {
    MeasureUnitImpl impl;
    const MeasureUnitImpl& mui = MeasureUnitImpl::forMeasureUnit(unit, impl, status);
    int32_t singleUnitIndex = 0;
    if (mui.complexity == UMEASURE_UNIT_COMPOUND) {
        int32_t startSlice = 0;
        // inclusive
        int32_t endSlice = mui.singleUnits.length()-1;
        U_ASSERT(endSlice > 0); // Else it would not be COMPOUND
        if (mui.singleUnits[endSlice]->dimensionality < 0) {
            // We have a -per- construct
            UnicodeString perRule = getDeriveCompoundRule(locale, "gender", "per", status);
            if (perRule.length() != 1) {
                // Fixed gender for -per- units
                return perRule;
            }
            if (perRule[0] == u'1') {
                // Find the start of the denominator. We already know there is one.
                while (mui.singleUnits[startSlice]->dimensionality >= 0) {
                    startSlice++;
                }
            } else {
                // Find the end of the numerator
                while (endSlice >= 0 && mui.singleUnits[endSlice]->dimensionality < 0) {
                    endSlice--;
                }
                if (endSlice < 0) {
                    // We have only a denominator, e.g. "per-second".
                    // TODO(icu-units#28): find out what gender to use in the
                    // absence of a first value - mentioned in CLDR-14253.
                    return {};
                }
            }
        }
        if (endSlice > startSlice) {
            // We have a -times- construct
            UnicodeString timesRule = getDeriveCompoundRule(locale, "gender", "times", status);
            if (timesRule.length() != 1) {
                // Fixed gender for -times- units
                return timesRule;
            }
            if (timesRule[0] == u'0') {
                endSlice = startSlice;
            } else {
                // We assume timesRule[0] == u'1'
                startSlice = endSlice;
            }
        }
        U_ASSERT(startSlice == endSlice);
        singleUnitIndex = startSlice;
    } else if (mui.complexity == UMEASURE_UNIT_MIXED) {
        status = U_INTERNAL_PROGRAM_ERROR;
        return {};
    } else {
        U_ASSERT(mui.complexity == UMEASURE_UNIT_SINGLE);
        U_ASSERT(mui.singleUnits.length() == 1);
    }

    // Now we know which singleUnit's gender we want
    const SingleUnitImpl *singleUnit = mui.singleUnits[singleUnitIndex];
    // Check for any power-prefix gender override:
    if (std::abs(singleUnit->dimensionality) != 1) {
        UnicodeString powerRule = getDeriveCompoundRule(locale, "gender", "power", status);
        if (powerRule.length() != 1) {
            // Fixed gender for -powN- units
            return powerRule;
        }
        // powerRule[0] == u'0'; u'1' not currently in spec.
    }
    // Check for any SI and binary prefix gender override:
    if (std::abs(singleUnit->dimensionality) != 1) {
        UnicodeString prefixRule = getDeriveCompoundRule(locale, "gender", "prefix", status);
        if (prefixRule.length() != 1) {
            // Fixed gender for -powN- units
            return prefixRule;
        }
        // prefixRule[0] == u'0'; u'1' not currently in spec.
    }
    // Now we've boiled it down to the gender of one simple unit identifier:
    return getGenderForBuiltin(locale, MeasureUnit::forIdentifier(singleUnit->getSimpleUnitID(), status),
                               status);
}

void maybeCalculateGender(const Locale &locale,
                          const MeasureUnit &unitRef,
                          UnicodeString *outArray,
                          UErrorCode &status) {
    if (outArray[GENDER_INDEX].isBogus()) {
        UnicodeString meterGender = getGenderForBuiltin(locale, MeasureUnit::getMeter(), status);
        if (meterGender.isEmpty()) {
            // No gender for meter: assume ungendered language
            return;
        }
        // We have a gendered language, but are lacking gender for unitRef.
        outArray[GENDER_INDEX] = calculateGenderForUnit(locale, unitRef, status);
    }
}

} // namespace

void LongNameHandler::forMeasureUnit(const Locale &loc,
                                     const MeasureUnit &unitRef,
                                     const UNumberUnitWidth &width,
                                     const char *unitDisplayCase,
                                     const PluralRules *rules,
                                     const MicroPropsGenerator *parent,
                                     LongNameHandler *fillIn,
                                     UErrorCode &status) {
    // From https://unicode.org/reports/tr35/tr35-general.html#compound-units -
    // Points 1 and 2 are mostly handled by MeasureUnit:
    //
    // 1. If the unitId is empty or invalid, fail
    // 2. Put the unitId into normalized order
    U_ASSERT(fillIn != nullptr);

    if (uprv_strcmp(unitRef.getType(), "") != 0) {
        // Handling built-in units:
        //
        // 3. Set result to be getValue(unitId with length, pluralCategory, caseVariant)
        //    - If result is not empty, return it
        UnicodeString simpleFormats[ARRAY_LENGTH];
        getMeasureData(loc, unitRef, width, unitDisplayCase, simpleFormats, status);
        maybeCalculateGender(loc, unitRef, simpleFormats, status);
        if (U_FAILURE(status)) {
            return;
        }
        fillIn->rules = rules;
        fillIn->parent = parent;
        fillIn->simpleFormatsToModifiers(simpleFormats,
                                         {UFIELD_CATEGORY_NUMBER, UNUM_MEASURE_UNIT_FIELD}, status);
        if (!simpleFormats[GENDER_INDEX].isBogus()) {
            fillIn->gender = getGenderString(simpleFormats[GENDER_INDEX], status);
        }
        return;

        // TODO(icu-units#145): figure out why this causes a failure in
        // format/MeasureFormatTest/TestIndividualPluralFallback and other
        // tests, when it should have been an alternative for the lines above:

        // forArbitraryUnit(loc, unitRef, width, unitDisplayCase, fillIn, status);
        // fillIn->rules = rules;
        // fillIn->parent = parent;
        // return;
    } else {
        // Check if it is a MeasureUnit this constructor handles: this
        // constructor does not handle mixed units
        U_ASSERT(unitRef.getComplexity(status) != UMEASURE_UNIT_MIXED);
        forArbitraryUnit(loc, unitRef, width, unitDisplayCase, fillIn, status);
        fillIn->rules = rules;
        fillIn->parent = parent;
        return;
    }
}

void LongNameHandler::forArbitraryUnit(const Locale &loc,
                                       const MeasureUnit &unitRef,
                                       const UNumberUnitWidth &width,
                                       const char *unitDisplayCase,
                                       LongNameHandler *fillIn,
                                       UErrorCode &status) {
    if (U_FAILURE(status)) {
        return;
    }
    if (fillIn == nullptr) {
        status = U_INTERNAL_PROGRAM_ERROR;
        return;
    }

    // Numbered list items are from the algorithms at
    // https://unicode.org/reports/tr35/tr35-general.html#compound-units:
    //
    // 4. Divide the unitId into numerator (the part before the "-per-") and
    //    denominator (the part after the "-per-). If both are empty, fail
    MeasureUnitImpl unit;
    MeasureUnitImpl perUnit;
    {
        MeasureUnitImpl fullUnit = MeasureUnitImpl::forMeasureUnitMaybeCopy(unitRef, status);
        if (U_FAILURE(status)) {
            return;
        }
        for (int32_t i = 0; i < fullUnit.singleUnits.length(); i++) {
            SingleUnitImpl *subUnit = fullUnit.singleUnits[i];
            if (subUnit->dimensionality > 0) {
                unit.appendSingleUnit(*subUnit, status);
            } else {
                subUnit->dimensionality *= -1;
                perUnit.appendSingleUnit(*subUnit, status);
            }
        }
    }

    // TODO(icu-units#28): check placeholder logic, see if it needs to be
    // present here instead of only in processPatternTimes:
    //
    // 5. Set both globalPlaceholder and globalPlaceholderPosition to be empty

    DerivedComponents derivedPerCases(loc, "case", "per");

    // 6. numeratorUnitString
    UnicodeString numeratorUnitData[ARRAY_LENGTH];
    processPatternTimes(std::move(unit), loc, width, derivedPerCases.value0(unitDisplayCase),
                        numeratorUnitData, status);

    // 7. denominatorUnitString
    UnicodeString denominatorUnitData[ARRAY_LENGTH];
    processPatternTimes(std::move(perUnit), loc, width, derivedPerCases.value1(unitDisplayCase),
                        denominatorUnitData, status);

    // TODO(icu-units#139):
    // - implement DerivedComponents for "plural/times" and "plural/power":
    //   French has different rules, we'll be producing the wrong results
    //   currently. (Prove via tests!)
    // - implement DerivedComponents for "plural/per", "plural/prefix",
    //   "case/times", "case/power", and "case/prefix" - although they're
    //   currently hardcoded. Languages with different rules are surely on the
    //   way.
    //
    // Currently we only use "case/per", "plural/times", "case/times", and
    // "case/power".
    //
    // This may have impact on multiSimpleFormatsToModifiers(...) below too?
    // These rules are currently (ICU 69) all the same and hard-coded below.
    UnicodeString perUnitPattern;
    if (!denominatorUnitData[PER_INDEX].isBogus()) {
        // If we have no denominator, we obtain the empty string:
        perUnitPattern = denominatorUnitData[PER_INDEX];
    } else {
        // 8. Set perPattern to be getValue([per], locale, length)
        UnicodeString rawPerUnitFormat = getCompoundValue("per", loc, width, status);
        // rawPerUnitFormat is something like "{0} per {1}"; we need to substitute in the secondary unit.
        SimpleFormatter perPatternFormatter(rawPerUnitFormat, 2, 2, status);
        if (U_FAILURE(status)) {
            return;
        }
        // Plural and placeholder handling for 7. denominatorUnitString:
        // TODO(icu-units#139): hardcoded:
        // <deriveComponent feature="plural" structure="per" value0="compound" value1="one"/>
        UnicodeString denominatorFormat =
            getWithPlural(denominatorUnitData, StandardPlural::Form::ONE, status);
        // Some "one" pattern may not contain "{0}". For example in "ar" or "ne" locale.
        SimpleFormatter denominatorFormatter(denominatorFormat, 0, 1, status);
        if (U_FAILURE(status)) {
            return;
        }
        UnicodeString denominatorPattern = denominatorFormatter.getTextWithNoArguments();
        int32_t trimmedLen = denominatorPattern.length();
        const char16_t *trimmed = trimSpaceChars(denominatorPattern.getBuffer(), trimmedLen);
        UnicodeString denominatorString(false, trimmed, trimmedLen);
        // 9. If the denominatorString is empty, set result to
        //    [numeratorString], otherwise set result to format(perPattern,
        //    numeratorString, denominatorString)
        //
        // TODO(icu-units#28): Why does UnicodeString need to be explicit in the
        // following line?
        perPatternFormatter.format(UnicodeString(u"{0}"), denominatorString, perUnitPattern, status);
        if (U_FAILURE(status)) {
            return;
        }
    }
    if (perUnitPattern.length() == 0) {
        fillIn->simpleFormatsToModifiers(numeratorUnitData,
                                         {UFIELD_CATEGORY_NUMBER, UNUM_MEASURE_UNIT_FIELD}, status);
    } else {
        fillIn->multiSimpleFormatsToModifiers(numeratorUnitData, perUnitPattern,
                                              {UFIELD_CATEGORY_NUMBER, UNUM_MEASURE_UNIT_FIELD}, status);
    }

    // Gender
    //
    // TODO(icu-units#28): find out what gender to use in the absence of a first
    // value - e.g. what's the gender of "per-second"? Mentioned in CLDR-14253.
    //
    // gender/per deriveCompound rules don't say:
    // <deriveCompound feature="gender" structure="per" value="0"/> <!-- gender(gram-per-meter) ←  gender(gram) -->
    fillIn->gender = getGenderString(
        getDerivedGender(loc, "per", numeratorUnitData, denominatorUnitData, status), status);
}

void LongNameHandler::processPatternTimes(MeasureUnitImpl &&productUnit,
                                          Locale loc,
                                          const UNumberUnitWidth &width,
                                          const char *caseVariant,
                                          UnicodeString *outArray,
                                          UErrorCode &status) {
    if (U_FAILURE(status)) {
        return;
    }
    if (productUnit.complexity == UMEASURE_UNIT_MIXED) {
        // These are handled by MixedUnitLongNameHandler
        status = U_UNSUPPORTED_ERROR;
        return;
    }

#if U_DEBUG
    for (int32_t pluralIndex = 0; pluralIndex < ARRAY_LENGTH; pluralIndex++) {
        U_ASSERT(outArray[pluralIndex].length() == 0);
        U_ASSERT(!outArray[pluralIndex].isBogus());
    }
#endif

    if (productUnit.identifier.isEmpty()) {
        // TODO(icu-units#28): consider when serialize should be called.
        // identifier might also be empty for MeasureUnit().
        productUnit.serialize(status);
    }
    if (U_FAILURE(status)) {
        return;
    }
    if (productUnit.identifier.length() == 0) {
        // MeasureUnit(): no units: return empty strings.
        return;
    }

    MeasureUnit builtinUnit;
    if (MeasureUnit::findBySubType(productUnit.identifier.toStringPiece(), &builtinUnit)) {
        // TODO(icu-units#145): spec doesn't cover builtin-per-builtin, it
        // breaks them all down. Do we want to drop this?
        // - findBySubType isn't super efficient, if we skip it and go to basic
        //   singles, we don't have to construct MeasureUnit's anymore.
        // - Check all the existing unit tests that fail without this: is it due
        //   to incorrect fallback via getMeasureData?
        // - Do those unit tests cover this code path representatively?
        if (builtinUnit != MeasureUnit()) {
            getMeasureData(loc, builtinUnit, width, caseVariant, outArray, status);
            maybeCalculateGender(loc, builtinUnit, outArray, status);
        }
        return;
    }

    // 2. Set timesPattern to be getValue(times, locale, length)
    UnicodeString timesPattern = getCompoundValue("times", loc, width, status);
    SimpleFormatter timesPatternFormatter(timesPattern, 2, 2, status);
    if (U_FAILURE(status)) {
        return;
    }

    PlaceholderPosition globalPlaceholder[ARRAY_LENGTH];
    char16_t globalJoinerChar = 0;
    // Numbered list items are from the algorithms at
    // https://unicode.org/reports/tr35/tr35-general.html#compound-units:
    //
    // pattern(...) point 5:
    // - Set both globalPlaceholder and globalPlaceholderPosition to be empty
    //
    // 3. Set result to be empty
    for (int32_t pluralIndex = 0; pluralIndex < ARRAY_LENGTH; pluralIndex++) {
        // Initial state: empty string pattern, via all falling back to OTHER:
        if (pluralIndex == StandardPlural::Form::OTHER) {
            outArray[pluralIndex].remove();
        } else {
            outArray[pluralIndex].setToBogus();
        }
        globalPlaceholder[pluralIndex] = PH_EMPTY;
    }

    // Empty string represents "compound" (propagate the plural form).
    const char *pluralCategory = "";
    DerivedComponents derivedTimesPlurals(loc, "plural", "times");
    DerivedComponents derivedTimesCases(loc, "case", "times");
    DerivedComponents derivedPowerCases(loc, "case", "power");

    // 4. For each single_unit in product_unit
    for (int32_t singleUnitIndex = 0; singleUnitIndex < productUnit.singleUnits.length();
         singleUnitIndex++) {
        SingleUnitImpl *singleUnit = productUnit.singleUnits[singleUnitIndex];
        const char *singlePluralCategory;
        const char *singleCaseVariant;
        // TODO(icu-units#28): ensure we have unit tests that change/fail if we
        // assign incorrect case variants here:
        if (singleUnitIndex < productUnit.singleUnits.length() - 1) {
            // 4.1. If hasMultiple
            singlePluralCategory = derivedTimesPlurals.value0(pluralCategory);
            singleCaseVariant = derivedTimesCases.value0(caseVariant);
            pluralCategory = derivedTimesPlurals.value1(pluralCategory);
            caseVariant = derivedTimesCases.value1(caseVariant);
        } else {
            singlePluralCategory = derivedTimesPlurals.value1(pluralCategory);
            singleCaseVariant = derivedTimesCases.value1(caseVariant);
        }

        // 4.2. Get the gender of that single_unit
        MeasureUnit simpleUnit;
        if (!MeasureUnit::findBySubType(singleUnit->getSimpleUnitID(), &simpleUnit)) {
            // Ideally all simple units should be known, but they're not:
            // 100-kilometer is internally treated as a simple unit, but it is
            // not a built-in unit and does not have formatting data in CLDR 39.
            //
            // TODO(icu-units#28): test (desirable) invariants in unit tests.
            status = U_UNSUPPORTED_ERROR;
            return;
        }
        const char *gender = getGenderString(getGenderForBuiltin(loc, simpleUnit, status), status);

        // 4.3. If singleUnit starts with a dimensionality_prefix, such as 'square-'
        U_ASSERT(singleUnit->dimensionality > 0);
        int32_t dimensionality = singleUnit->dimensionality;
        UnicodeString dimensionalityPrefixPatterns[ARRAY_LENGTH];
        if (dimensionality != 1) {
            // 4.3.1. set dimensionalityPrefixPattern to be
            //   getValue(that dimensionality_prefix, locale, length, singlePluralCategory, singleCaseVariant, gender),
            //   such as "{0} kwadratowym"
            CharString dimensionalityKey("compound/power", status);
            dimensionalityKey.appendNumber(dimensionality, status);
            getInflectedMeasureData(dimensionalityKey.toStringPiece(), loc, width, gender,
                                    singleCaseVariant, dimensionalityPrefixPatterns, status);
            if (U_FAILURE(status)) {
                // At the time of writing, only pow2 and pow3 are supported.
                // Attempting to format other powers results in a
                // U_RESOURCE_TYPE_MISMATCH. We convert the error if we
                // understand it:
                if (status == U_RESOURCE_TYPE_MISMATCH && dimensionality > 3) {
                    status = U_UNSUPPORTED_ERROR;
                }
                return;
            }

            // TODO(icu-units#139):
            // 4.3.2. set singlePluralCategory to be power0(singlePluralCategory)

            // 4.3.3. set singleCaseVariant to be power0(singleCaseVariant)
            singleCaseVariant = derivedPowerCases.value0(singleCaseVariant);
            // 4.3.4. remove the dimensionality_prefix from singleUnit
            singleUnit->dimensionality = 1;
        }

        // 4.4. if singleUnit starts with an si_prefix, such as 'centi'
        UMeasurePrefix prefix = singleUnit->unitPrefix;
        UnicodeString prefixPattern;
        if (prefix != UMEASURE_PREFIX_ONE) {
            // 4.4.1. set siPrefixPattern to be getValue(that si_prefix, locale,
            //        length), such as "centy{0}"
            CharString prefixKey;
            // prefixKey looks like "1024p3" or "10p-2":
            prefixKey.appendNumber(umeas_getPrefixBase(prefix), status);
            prefixKey.append('p', status);
            prefixKey.appendNumber(umeas_getPrefixPower(prefix), status);
            // Contains a pattern like "centy{0}".
            prefixPattern = getCompoundValue(prefixKey.toStringPiece(), loc, width, status);

            // 4.4.2. set singlePluralCategory to be prefix0(singlePluralCategory)
            //
            // TODO(icu-units#139): that refers to these rules:
            // <deriveComponent feature="plural" structure="prefix" value0="one" value1="compound"/>
            // though I'm not sure what other value they might end up having.
            //
            // 4.4.3. set singleCaseVariant to be prefix0(singleCaseVariant)
            //
            // TODO(icu-units#139): that refers to:
            // <deriveComponent feature="case" structure="prefix" value0="nominative"
            // value1="compound"/> but the prefix (value0) doesn't have case, the rest simply
            // propagates.

            // 4.4.4. remove the si_prefix from singleUnit
            singleUnit->unitPrefix = UMEASURE_PREFIX_ONE;
        }

        // 4.5. Set corePattern to be the getValue(singleUnit, locale, length,
        //      singlePluralCategory, singleCaseVariant), such as "{0} metrem"
        UnicodeString singleUnitArray[ARRAY_LENGTH];
        // At this point we are left with a Simple Unit:
        U_ASSERT(uprv_strcmp(singleUnit->build(status).getIdentifier(), singleUnit->getSimpleUnitID()) ==
                 0);
        getMeasureData(loc, singleUnit->build(status), width, singleCaseVariant, singleUnitArray,
                       status);
        if (U_FAILURE(status)) {
            // Shouldn't happen if we have data for all single units
            return;
        }

        // Calculate output gender
        if (!singleUnitArray[GENDER_INDEX].isBogus()) {
            U_ASSERT(!singleUnitArray[GENDER_INDEX].isEmpty());
            UnicodeString uVal;

            if (prefix != UMEASURE_PREFIX_ONE) {
                singleUnitArray[GENDER_INDEX] =
                    getDerivedGender(loc, "prefix", singleUnitArray, nullptr, status);
            }

            if (dimensionality != 1) {
                singleUnitArray[GENDER_INDEX] =
                    getDerivedGender(loc, "power", singleUnitArray, nullptr, status);
            }

            UnicodeString timesGenderRule = getDeriveCompoundRule(loc, "gender", "times", status);
            if (timesGenderRule.length() == 1) {
                switch (timesGenderRule[0]) {
                case u'0':
                    if (singleUnitIndex == 0) {
                        U_ASSERT(outArray[GENDER_INDEX].isBogus());
                        outArray[GENDER_INDEX] = singleUnitArray[GENDER_INDEX];
                    }
                    break;
                case u'1':
                    if (singleUnitIndex == productUnit.singleUnits.length() - 1) {
                        U_ASSERT(outArray[GENDER_INDEX].isBogus());
                        outArray[GENDER_INDEX] = singleUnitArray[GENDER_INDEX];
                    }
                }
            } else {
                if (outArray[GENDER_INDEX].isBogus()) {
                    outArray[GENDER_INDEX] = timesGenderRule;
                }
            }
        }

        // Calculate resulting patterns for each plural form
        for (int32_t pluralIndex = 0; pluralIndex < StandardPlural::Form::COUNT; pluralIndex++) {
            StandardPlural::Form plural = static_cast<StandardPlural::Form>(pluralIndex);

            // singleUnitArray[pluralIndex] looks something like "{0} Meter"
            if (outArray[pluralIndex].isBogus()) {
                if (singleUnitArray[pluralIndex].isBogus()) {
                    // Let the usual plural fallback mechanism take care of this
                    // plural form
                    continue;
                } else {
                    // Since our singleUnit can have a plural form that outArray
                    // doesn't yet have (relying on fallback to OTHER), we start
                    // by grabbing it with the normal plural fallback mechanism
                    outArray[pluralIndex] = getWithPlural(outArray, plural, status);
                    if (U_FAILURE(status)) {
                        return;
                    }
                }
            }

            if (uprv_strcmp(singlePluralCategory, "") != 0) {
                plural = static_cast<StandardPlural::Form>(getIndex(singlePluralCategory, status));
            }

            // 4.6. Extract(corePattern, coreUnit, placeholder, placeholderPosition) from that pattern.
            UnicodeString coreUnit;
            PlaceholderPosition placeholderPosition;
            char16_t joinerChar;
            extractCorePattern(getWithPlural(singleUnitArray, plural, status), coreUnit,
                               placeholderPosition, joinerChar);

            // 4.7 If the position is middle, then fail
            if (placeholderPosition == PH_MIDDLE) {
                status = U_UNSUPPORTED_ERROR;
                return;
            }

            // 4.8. If globalPlaceholder is empty
            if (globalPlaceholder[pluralIndex] == PH_EMPTY) {
                globalPlaceholder[pluralIndex] = placeholderPosition;
                globalJoinerChar = joinerChar;
            } else {
                // Expect all units involved to have the same placeholder position
                U_ASSERT(globalPlaceholder[pluralIndex] == placeholderPosition);
                // TODO(icu-units#28): Do we want to add a unit test that checks
                // for consistent joiner chars? Probably not, given how
                // inconsistent they are. File a CLDR ticket with examples?
            }
            // Now coreUnit would be just "Meter"

            // 4.9. If siPrefixPattern is not empty
            if (prefix != UMEASURE_PREFIX_ONE) {
                SimpleFormatter prefixCompiled(prefixPattern, 1, 1, status);
                if (U_FAILURE(status)) {
                    return;
                }

                // 4.9.1. Set coreUnit to be the combineLowercasing(locale, length, siPrefixPattern,
                //        coreUnit)
                UnicodeString tmp;
                // combineLowercasing(locale, length, prefixPattern, coreUnit)
                //
                // TODO(icu-units#28): run this only if prefixPattern does not
                // contain space characters - do languages "as", "bn", "hi",
                // "kk", etc have concepts of upper and lower case?:
                if (width == UNUM_UNIT_WIDTH_FULL_NAME) {
                    coreUnit.toLower(loc);
                }
                prefixCompiled.format(coreUnit, tmp, status);
                if (U_FAILURE(status)) {
                    return;
                }
                coreUnit = tmp;
            }

            // 4.10. If dimensionalityPrefixPattern is not empty
            if (dimensionality != 1) {
                SimpleFormatter dimensionalityCompiled(
                    getWithPlural(dimensionalityPrefixPatterns, plural, status), 1, 1, status);
                if (U_FAILURE(status)) {
                    return;
                }

                // 4.10.1. Set coreUnit to be the combineLowercasing(locale, length,
                //         dimensionalityPrefixPattern, coreUnit)
                UnicodeString tmp;
                // combineLowercasing(locale, length, prefixPattern, coreUnit)
                //
                // TODO(icu-units#28): run this only if prefixPattern does not
                // contain space characters - do languages "as", "bn", "hi",
                // "kk", etc have concepts of upper and lower case?:
                if (width == UNUM_UNIT_WIDTH_FULL_NAME) {
                    coreUnit.toLower(loc);
                }
                dimensionalityCompiled.format(coreUnit, tmp, status);
                if (U_FAILURE(status)) {
                    return;
                }
                coreUnit = tmp;
            }

            if (outArray[pluralIndex].length() == 0) {
                // 4.11. If the result is empty, set result to be coreUnit
                outArray[pluralIndex] = coreUnit;
            } else {
                // 4.12. Otherwise set result to be format(timesPattern, result, coreUnit)
                UnicodeString tmp;
                timesPatternFormatter.format(outArray[pluralIndex], coreUnit, tmp, status);
                outArray[pluralIndex] = tmp;
            }
        }
    }
    for (int32_t pluralIndex = 0; pluralIndex < StandardPlural::Form::COUNT; pluralIndex++) {
        if (globalPlaceholder[pluralIndex] == PH_BEGINNING) {
            UnicodeString tmp;
            tmp.append(u"{0}", 3);
            if (globalJoinerChar != 0) {
                tmp.append(globalJoinerChar);
            }
            tmp.append(outArray[pluralIndex]);
            outArray[pluralIndex] = tmp;
        } else if (globalPlaceholder[pluralIndex] == PH_END) {
            if (globalJoinerChar != 0) {
                outArray[pluralIndex].append(globalJoinerChar);
            }
            outArray[pluralIndex].append(u"{0}", 3);
        }
    }
}

UnicodeString LongNameHandler::getUnitDisplayName(
        const Locale& loc,
        const MeasureUnit& unit,
        UNumberUnitWidth width,
        UErrorCode& status) {
    if (U_FAILURE(status)) {
        return ICU_Utility::makeBogusString();
    }
    UnicodeString simpleFormats[ARRAY_LENGTH];
    getMeasureData(loc, unit, width, "", simpleFormats, status);
    return simpleFormats[DNAM_INDEX];
}

UnicodeString LongNameHandler::getUnitPattern(
        const Locale& loc,
        const MeasureUnit& unit,
        UNumberUnitWidth width,
        StandardPlural::Form pluralForm,
        UErrorCode& status) {
    if (U_FAILURE(status)) {
        return ICU_Utility::makeBogusString();
    }
    UnicodeString simpleFormats[ARRAY_LENGTH];
    getMeasureData(loc, unit, width, "", simpleFormats, status);
    // The above already handles fallback from other widths to short
    if (U_FAILURE(status)) {
        return ICU_Utility::makeBogusString();
    }
    // Now handle fallback from other plural forms to OTHER
    return (!(simpleFormats[pluralForm]).isBogus())? simpleFormats[pluralForm]:
            simpleFormats[StandardPlural::Form::OTHER];
}

LongNameHandler* LongNameHandler::forCurrencyLongNames(const Locale &loc, const CurrencyUnit &currency,
                                                      const PluralRules *rules,
                                                      const MicroPropsGenerator *parent,
                                                      UErrorCode &status) {
    LocalPointer<LongNameHandler> result(new LongNameHandler(rules, parent), status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    UnicodeString simpleFormats[ARRAY_LENGTH];
    getCurrencyLongNameData(loc, currency, simpleFormats, status);
    if (U_FAILURE(status)) { return nullptr; }
    result->simpleFormatsToModifiers(simpleFormats, {UFIELD_CATEGORY_NUMBER, UNUM_CURRENCY_FIELD}, status);
    // TODO(icu-units#28): currency gender?
    return result.orphan();
}

void LongNameHandler::simpleFormatsToModifiers(const UnicodeString *simpleFormats, Field field,
                                               UErrorCode &status) {
    for (int32_t i = 0; i < StandardPlural::Form::COUNT; i++) {
        StandardPlural::Form plural = static_cast<StandardPlural::Form>(i);
        UnicodeString simpleFormat = getWithPlural(simpleFormats, plural, status);
        if (U_FAILURE(status)) { return; }
        SimpleFormatter compiledFormatter(simpleFormat, 0, 1, status);
        if (U_FAILURE(status)) { return; }
        fModifiers[i] = SimpleModifier(compiledFormatter, field, false, {this, SIGNUM_POS_ZERO, plural});
    }
}

void LongNameHandler::multiSimpleFormatsToModifiers(const UnicodeString *leadFormats, UnicodeString trailFormat,
                                                    Field field, UErrorCode &status) {
    SimpleFormatter trailCompiled(trailFormat, 1, 1, status);
    if (U_FAILURE(status)) { return; }
    for (int32_t i = 0; i < StandardPlural::Form::COUNT; i++) {
        StandardPlural::Form plural = static_cast<StandardPlural::Form>(i);
        UnicodeString leadFormat = getWithPlural(leadFormats, plural, status);
        if (U_FAILURE(status)) { return; }
        UnicodeString compoundFormat;
        if (leadFormat.length() == 0) {
            compoundFormat = trailFormat;
        } else {
            trailCompiled.format(leadFormat, compoundFormat, status);
            if (U_FAILURE(status)) { return; }
        }
        SimpleFormatter compoundCompiled(compoundFormat, 0, 1, status);
        if (U_FAILURE(status)) { return; }
        fModifiers[i] = SimpleModifier(compoundCompiled, field, false, {this, SIGNUM_POS_ZERO, plural});
    }
}

void LongNameHandler::processQuantity(DecimalQuantity &quantity, MicroProps &micros,
                                      UErrorCode &status) const {
    if (parent != nullptr) {
        parent->processQuantity(quantity, micros, status);
    }
    StandardPlural::Form pluralForm = utils::getPluralSafe(micros.rounder, rules, quantity, status);
    micros.modOuter = &fModifiers[pluralForm];
    micros.gender = gender;
}

const Modifier* LongNameHandler::getModifier(Signum /*signum*/, StandardPlural::Form plural) const {
    return &fModifiers[plural];
}

void MixedUnitLongNameHandler::forMeasureUnit(const Locale &loc,
                                              const MeasureUnit &mixedUnit,
                                              const UNumberUnitWidth &width,
                                              const char *unitDisplayCase,
                                              const PluralRules *rules,
                                              const MicroPropsGenerator *parent,
                                              MixedUnitLongNameHandler *fillIn,
                                              UErrorCode &status) {
    U_ASSERT(mixedUnit.getComplexity(status) == UMEASURE_UNIT_MIXED);
    U_ASSERT(fillIn != nullptr);
    if (U_FAILURE(status)) {
        return;
    }

    MeasureUnitImpl temp;
    const MeasureUnitImpl &impl = MeasureUnitImpl::forMeasureUnit(mixedUnit, temp, status);
    // Defensive, for production code:
    if (impl.complexity != UMEASURE_UNIT_MIXED) {
        // Should be using the normal LongNameHandler
        status = U_UNSUPPORTED_ERROR;
        return;
    }

    fillIn->fMixedUnitCount = impl.singleUnits.length();
    fillIn->fMixedUnitData.adoptInstead(new UnicodeString[fillIn->fMixedUnitCount * ARRAY_LENGTH]);
    for (int32_t i = 0; i < fillIn->fMixedUnitCount; i++) {
        // Grab data for each of the components.
        UnicodeString *unitData = &fillIn->fMixedUnitData[i * ARRAY_LENGTH];
        // TODO(CLDR-14582): check from the CLDR-14582 ticket whether this
        // propagation of unitDisplayCase is correct:
        getMeasureData(loc, impl.singleUnits[i]->build(status), width, unitDisplayCase, unitData,
                       status);
        // TODO(ICU-21494): if we add support for gender for mixed units, we may
        // need maybeCalculateGender() here.
    }

    // TODO(icu-units#120): Make sure ICU doesn't output zero-valued
    // high-magnitude fields
    // * for mixed units count N, produce N listFormatters, one for each subset
    //   that might be formatted.
    UListFormatterWidth listWidth = ULISTFMT_WIDTH_SHORT;
    if (width == UNUM_UNIT_WIDTH_NARROW) {
        listWidth = ULISTFMT_WIDTH_NARROW;
    } else if (width == UNUM_UNIT_WIDTH_FULL_NAME) {
        // This might be the same as SHORT in most languages:
        listWidth = ULISTFMT_WIDTH_WIDE;
    }
    fillIn->fListFormatter.adoptInsteadAndCheckErrorCode(
        ListFormatter::createInstance(loc, ULISTFMT_TYPE_UNITS, listWidth, status), status);
    // TODO(ICU-21494): grab gender of each unit, calculate the gender
    // associated with this list formatter, save it for later.
    fillIn->rules = rules;
    fillIn->parent = parent;

    // We need a localised NumberFormatter for the numbers of the bigger units
    // (providing Arabic numerals, for example).
    fillIn->fNumberFormatter = NumberFormatter::withLocale(loc);
}

void MixedUnitLongNameHandler::processQuantity(DecimalQuantity &quantity, MicroProps &micros,
                                               UErrorCode &status) const {
    U_ASSERT(fMixedUnitCount > 1);
    if (parent != nullptr) {
        parent->processQuantity(quantity, micros, status);
    }
    micros.modOuter = getMixedUnitModifier(quantity, micros, status);
}

const Modifier *MixedUnitLongNameHandler::getMixedUnitModifier(DecimalQuantity &quantity,
                                                               MicroProps &micros,
                                                               UErrorCode &status) const {
    if (micros.mixedMeasuresCount == 0) {
        U_ASSERT(micros.mixedMeasuresCount > 0); // Mixed unit: we must have more than one unit value
        status = U_UNSUPPORTED_ERROR;
        return &micros.helpers.emptyWeakModifier;
    }

    // Algorithm:
    //
    // For the mixed-units measurement of: "3 yard, 1 foot, 2.6 inch", we should
    // find "3 yard" and "1 foot" in micros.mixedMeasures.
    //
    // Obtain long-names with plural forms corresponding to measure values:
    //   * {0} yards, {0} foot, {0} inches
    //
    // Format the integer values appropriately and modify with the format
    // strings:
    //   - 3 yards, 1 foot
    //
    // Use ListFormatter to combine, with one placeholder:
    //   - 3 yards, 1 foot and {0} inches
    //
    // Return a SimpleModifier for this pattern, letting the rest of the
    // pipeline take care of the remaining inches.

    LocalArray<UnicodeString> outputMeasuresList(new UnicodeString[fMixedUnitCount], status);
    if (U_FAILURE(status)) {
        return &micros.helpers.emptyWeakModifier;
    }

    StandardPlural::Form quantityPlural = StandardPlural::Form::OTHER;
    for (int32_t i = 0; i < micros.mixedMeasuresCount; i++) {
        DecimalQuantity fdec;

        // If numbers are negative, only the first number needs to have its
        // negative sign formatted.
        int64_t number = i > 0 ? std::abs(micros.mixedMeasures[i]) : micros.mixedMeasures[i];

        if (micros.indexOfQuantity == i) { // Insert placeholder for `quantity`
            // If quantity is not the first value and quantity is negative
            if (micros.indexOfQuantity > 0 && quantity.isNegative()) {
                quantity.negate();
            }

            StandardPlural::Form quantityPlural =
                utils::getPluralSafe(micros.rounder, rules, quantity, status);
            UnicodeString quantityFormatWithPlural =
                getWithPlural(&fMixedUnitData[i * ARRAY_LENGTH], quantityPlural, status);
            SimpleFormatter quantityFormatter(quantityFormatWithPlural, 0, 1, status);
            quantityFormatter.format(UnicodeString(u"{0}"), outputMeasuresList[i], status);
        } else {
            fdec.setToLong(number);
            StandardPlural::Form pluralForm = utils::getStandardPlural(rules, fdec);
            UnicodeString simpleFormat =
                getWithPlural(&fMixedUnitData[i * ARRAY_LENGTH], pluralForm, status);
            SimpleFormatter compiledFormatter(simpleFormat, 0, 1, status);
            UnicodeString num;
            auto appendable = UnicodeStringAppendable(num);

            fNumberFormatter.formatDecimalQuantity(fdec, status).appendTo(appendable, status);
            compiledFormatter.format(num, outputMeasuresList[i], status);
        }
    }

    // TODO(ICU-21494): implement gender for lists of mixed units. Presumably we
    // can set micros.gender to the gender associated with the list formatter in
    // use below (once we have correct support for that). And then document this
    // appropriately? "getMixedUnitModifier" doesn't sound like it would do
    // something like this.

    // Combine list into a "premixed" pattern
    UnicodeString premixedFormatPattern;
    fListFormatter->format(outputMeasuresList.getAlias(), fMixedUnitCount, premixedFormatPattern,
                           status);
    SimpleFormatter premixedCompiled(premixedFormatPattern, 0, 1, status);
    if (U_FAILURE(status)) {
        return &micros.helpers.emptyWeakModifier;
    }

    micros.helpers.mixedUnitModifier =
        SimpleModifier(premixedCompiled, kUndefinedField, false, {this, SIGNUM_POS_ZERO, quantityPlural});
    return &micros.helpers.mixedUnitModifier;
}

const Modifier *MixedUnitLongNameHandler::getModifier(Signum /*signum*/,
                                                      StandardPlural::Form /*plural*/) const {
    // TODO(icu-units#28): investigate this method when investigating where
    // ModifierStore::getModifier() gets used. To be sure it remains
    // unreachable:
    UPRV_UNREACHABLE_EXIT;
    return nullptr;
}

LongNameMultiplexer *LongNameMultiplexer::forMeasureUnits(const Locale &loc,
                                                          const MaybeStackVector<MeasureUnit> &units,
                                                          const UNumberUnitWidth &width,
                                                          const char *unitDisplayCase,
                                                          const PluralRules *rules,
                                                          const MicroPropsGenerator *parent,
                                                          UErrorCode &status) {
    LocalPointer<LongNameMultiplexer> result(new LongNameMultiplexer(parent), status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    U_ASSERT(units.length() > 0);
    if (result->fHandlers.resize(units.length()) == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    result->fMeasureUnits.adoptInstead(new MeasureUnit[units.length()]);
    for (int32_t i = 0, length = units.length(); i < length; i++) {
        const MeasureUnit &unit = *units[i];
        result->fMeasureUnits[i] = unit;
        if (unit.getComplexity(status) == UMEASURE_UNIT_MIXED) {
            MixedUnitLongNameHandler *mlnh = result->fMixedUnitHandlers.createAndCheckErrorCode(status);
            MixedUnitLongNameHandler::forMeasureUnit(loc, unit, width, unitDisplayCase, rules, nullptr,
                                                     mlnh, status);
            result->fHandlers[i] = mlnh;
        } else {
            LongNameHandler *lnh = result->fLongNameHandlers.createAndCheckErrorCode(status);
            LongNameHandler::forMeasureUnit(loc, unit, width, unitDisplayCase, rules, nullptr, lnh, status);
            result->fHandlers[i] = lnh;
        }
        if (U_FAILURE(status)) {
            return nullptr;
        }
    }
    return result.orphan();
}

void LongNameMultiplexer::processQuantity(DecimalQuantity &quantity, MicroProps &micros,
                                          UErrorCode &status) const {
    // We call parent->processQuantity() from the Multiplexer, instead of
    // letting LongNameHandler handle it: we don't know which LongNameHandler to
    // call until we've called the parent!
    fParent->processQuantity(quantity, micros, status);

    // Call the correct LongNameHandler based on outputUnit
    for (int i = 0; i < fHandlers.getCapacity(); i++) {
        if (fMeasureUnits[i] == micros.outputUnit) {
            fHandlers[i]->processQuantity(quantity, micros, status);
            return;
        }
    }
    if (U_FAILURE(status)) {
        return;
    }
    // We shouldn't receive any outputUnit for which we haven't already got a
    // LongNameHandler:
    status = U_INTERNAL_PROGRAM_ERROR;
}

#endif /* #if !UCONFIG_NO_FORMATTING */
