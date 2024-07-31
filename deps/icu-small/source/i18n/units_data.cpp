// © 2020 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "bytesinkutil.h"
#include "charstr.h"
#include "cstring.h"
#include "measunit_impl.h"
#include "number_decimalquantity.h"
#include "resource.h"
#include "uassert.h"
#include "ulocimp.h"
#include "unicode/locid.h"
#include "unicode/unistr.h"
#include "unicode/ures.h"
#include "units_data.h"
#include "uresimp.h"
#include "util.h"
#include <utility>

U_NAMESPACE_BEGIN
namespace units {

namespace {

using icu::number::impl::DecimalQuantity;

void trimSpaces(CharString& factor, UErrorCode& status){
   CharString trimmed;
   for (int i = 0 ; i < factor.length(); i++) {
       if (factor[i] == ' ') continue;

       trimmed.append(factor[i], status);
   }

   factor = std::move(trimmed);
}

/**
 * A ResourceSink that collects conversion rate information.
 *
 * This class is for use by ures_getAllItemsWithFallback.
 */
class ConversionRateDataSink : public ResourceSink {
  public:
    /**
     * Constructor.
     * @param out The vector to which ConversionRateInfo instances are to be
     * added. This vector must outlive the use of the ResourceSink.
     */
    explicit ConversionRateDataSink(MaybeStackVector<ConversionRateInfo> *out) : outVector(out) {}

    /**
     * Method for use by `ures_getAllItemsWithFallback`. Adds the unit
     * conversion rates that are found in `value` to the output vector.
     *
     * @param source This string must be "convertUnits": the resource that this
     * class supports reading.
     * @param value The "convertUnits" resource, containing unit conversion rate
     * information.
     * @param noFallback Ignored.
     * @param status The standard ICU error code output parameter.
     */
    void put(const char *source, ResourceValue &value, UBool /*noFallback*/, UErrorCode &status) override {
        if (U_FAILURE(status)) { return; }
        if (uprv_strcmp(source, "convertUnits") != 0) {
            // This is very strict, however it is the cheapest way to be sure
            // that with `value`, we're looking at the convertUnits table.
            status = U_ILLEGAL_ARGUMENT_ERROR;
            return;
        }
        ResourceTable conversionRateTable = value.getTable(status);
        const char *srcUnit;
        // We're reusing `value`, which seems to be a common pattern:
        for (int32_t unit = 0; conversionRateTable.getKeyAndValue(unit, srcUnit, value); unit++) {
            ResourceTable unitTable = value.getTable(status);
            const char *key;
            UnicodeString baseUnit = ICU_Utility::makeBogusString();
            UnicodeString factor = ICU_Utility::makeBogusString();
            UnicodeString offset = ICU_Utility::makeBogusString();
            UnicodeString special = ICU_Utility::makeBogusString();
            UnicodeString systems = ICU_Utility::makeBogusString();
            for (int32_t i = 0; unitTable.getKeyAndValue(i, key, value); i++) {
                if (uprv_strcmp(key, "target") == 0) {
                    baseUnit = value.getUnicodeString(status);
                } else if (uprv_strcmp(key, "factor") == 0) {
                    factor = value.getUnicodeString(status);
                } else if (uprv_strcmp(key, "offset") == 0) {
                    offset = value.getUnicodeString(status);
                } else if (uprv_strcmp(key, "special") == 0) {
                    special = value.getUnicodeString(status); // the name of a special mapping used instead of factor + optional offset.
                } else if (uprv_strcmp(key, "systems") == 0) {
                    systems = value.getUnicodeString(status);
                }
            }
            if (U_FAILURE(status)) { return; }
            if (baseUnit.isBogus() || (factor.isBogus() && special.isBogus())) {
                // We could not find a usable conversion rate: bad resource.
                status = U_MISSING_RESOURCE_ERROR;
                return;
            }

            // We don't have this ConversionRateInfo yet: add it.
            ConversionRateInfo *cr = outVector->emplaceBack();
            if (!cr) {
                status = U_MEMORY_ALLOCATION_ERROR;
                return;
            } else {
                cr->sourceUnit.append(srcUnit, status);
                cr->baseUnit.appendInvariantChars(baseUnit, status);
                if (!factor.isBogus()) {
                    cr->factor.appendInvariantChars(factor, status);
                    trimSpaces(cr->factor, status);
                }
                if (!offset.isBogus()) cr->offset.appendInvariantChars(offset, status);
                if (!special.isBogus()) cr->specialMappingName.appendInvariantChars(special, status);
                cr->systems.appendInvariantChars(systems, status);
            }
        }
    }

  private:
    MaybeStackVector<ConversionRateInfo> *outVector;
};

bool operator<(const UnitPreferenceMetadata &a, const UnitPreferenceMetadata &b) {
    return a.compareTo(b) < 0;
}

/**
 * A ResourceSink that collects unit preferences information.
 *
 * This class is for use by ures_getAllItemsWithFallback.
 */
class UnitPreferencesSink : public ResourceSink {
  public:
    /**
     * Constructor.
     * @param outPrefs The vector to which UnitPreference instances are to be
     * added. This vector must outlive the use of the ResourceSink.
     * @param outMetadata  The vector to which UnitPreferenceMetadata instances
     * are to be added. This vector must outlive the use of the ResourceSink.
     */
    explicit UnitPreferencesSink(MaybeStackVector<UnitPreference> *outPrefs,
                                 MaybeStackVector<UnitPreferenceMetadata> *outMetadata)
        : preferences(outPrefs), metadata(outMetadata) {}

    /**
     * Method for use by `ures_getAllItemsWithFallback`. Adds the unit
     * preferences info that are found in `value` to the output vector.
     *
     * @param source This string must be "unitPreferenceData": the resource that
     * this class supports reading.
     * @param value The "unitPreferenceData" resource, containing unit
     * preferences data.
     * @param noFallback Ignored.
     * @param status The standard ICU error code output parameter. Note: if an
     * error is returned, outPrefs and outMetadata may be inconsistent.
     */
    void put(const char *key, ResourceValue &value, UBool /*noFallback*/, UErrorCode &status) override {
        if (U_FAILURE(status)) { return; }
        if (uprv_strcmp(key, "unitPreferenceData") != 0) {
            // This is very strict, however it is the cheapest way to be sure
            // that with `value`, we're looking at the convertUnits table.
            status = U_ILLEGAL_ARGUMENT_ERROR;
            return;
        }
        // The unitPreferenceData structure (see data/misc/units.txt) contains a
        // hierarchy of category/usage/region, within which are a set of
        // preferences. Hence three for-loops and another loop for the
        // preferences themselves:
        ResourceTable unitPreferenceDataTable = value.getTable(status);
        const char *category;
        for (int32_t i = 0; unitPreferenceDataTable.getKeyAndValue(i, category, value); i++) {
            ResourceTable categoryTable = value.getTable(status);
            const char *usage;
            for (int32_t j = 0; categoryTable.getKeyAndValue(j, usage, value); j++) {
                ResourceTable regionTable = value.getTable(status);
                const char *region;
                for (int32_t k = 0; regionTable.getKeyAndValue(k, region, value); k++) {
                    // `value` now contains the set of preferences for
                    // category/usage/region.
                    ResourceArray unitPrefs = value.getArray(status);
                    if (U_FAILURE(status)) { return; }
                    int32_t prefLen = unitPrefs.getSize();

                    // Update metadata for this set of preferences.
                    UnitPreferenceMetadata *meta = metadata->emplaceBack(
                        category, usage, region, preferences->length(), prefLen, status);
                    if (!meta) {
                        status = U_MEMORY_ALLOCATION_ERROR;
                        return;
                    }
                    if (U_FAILURE(status)) { return; }
                    if (metadata->length() > 1) {
                        // Verify that unit preferences are sorted and
                        // without duplicates.
                        if (!(*(*metadata)[metadata->length() - 2] <
                              *(*metadata)[metadata->length() - 1])) {
                            status = U_INVALID_FORMAT_ERROR;
                            return;
                        }
                    }

                    // Collect the individual preferences.
                    for (int32_t i = 0; unitPrefs.getValue(i, value); i++) {
                        UnitPreference *up = preferences->emplaceBack();
                        if (!up) {
                            status = U_MEMORY_ALLOCATION_ERROR;
                            return;
                        }
                        ResourceTable unitPref = value.getTable(status);
                        if (U_FAILURE(status)) { return; }
                        for (int32_t i = 0; unitPref.getKeyAndValue(i, key, value); ++i) {
                            if (uprv_strcmp(key, "unit") == 0) {
                                int32_t length;
                                const char16_t *u = value.getString(length, status);
                                up->unit.appendInvariantChars(u, length, status);
                            } else if (uprv_strcmp(key, "geq") == 0) {
                                int32_t length;
                                const char16_t *g = value.getString(length, status);
                                CharString geq;
                                geq.appendInvariantChars(g, length, status);
                                DecimalQuantity dq;
                                dq.setToDecNumber(geq.data(), status);
                                up->geq = dq.toDouble();
                            } else if (uprv_strcmp(key, "skeleton") == 0) {
                                up->skeleton = value.getUnicodeString(status);
                            }
                        }
                    }
                }
            }
        }
    }

  private:
    MaybeStackVector<UnitPreference> *preferences;
    MaybeStackVector<UnitPreferenceMetadata> *metadata;
};

int32_t binarySearch(const MaybeStackVector<UnitPreferenceMetadata> *metadata,
                     const UnitPreferenceMetadata &desired, bool *foundCategory, bool *foundUsage,
                     bool *foundRegion, UErrorCode &status) {
    if (U_FAILURE(status)) { return -1; }
    int32_t start = 0;
    int32_t end = metadata->length();
    *foundCategory = false;
    *foundUsage = false;
    *foundRegion = false;
    while (start < end) {
        int32_t mid = (start + end) / 2;
        int32_t cmp = (*metadata)[mid]->compareTo(desired, foundCategory, foundUsage, foundRegion);
        if (cmp < 0) {
            start = mid + 1;
        } else if (cmp > 0) {
            end = mid;
        } else {
            return mid;
        }
    }
    return -1;
}

/**
 * Finds the UnitPreferenceMetadata instance that matches the given category,
 * usage and region: if missing, region falls back to "001", and usage
 * repeatedly drops tailing components, eventually trying "default"
 * ("land-agriculture-grain" -> "land-agriculture" -> "land" -> "default").
 *
 * @param metadata The full list of UnitPreferenceMetadata instances.
 * @param category The category to search for. See getUnitCategory().
 * @param usage The usage for which formatting preferences is needed. If the
 * given usage is not known, automatic fallback occurs, see function description
 * above.
 * @param region The region for which preferences are needed. If there are no
 * region-specific preferences, this function automatically falls back to the
 * "001" region (global).
 * @param status The standard ICU error code output parameter.
 *   * If an invalid category is given, status will be U_ILLEGAL_ARGUMENT_ERROR.
 *   * If fallback to "default" or "001" didn't resolve, status will be
 *     U_MISSING_RESOURCE.
 * @return The index into the metadata vector which represents the appropriate
 * preferences. If appropriate preferences are not found, -1 is returned.
 */
int32_t getPreferenceMetadataIndex(const MaybeStackVector<UnitPreferenceMetadata> *metadata,
                                   StringPiece category, StringPiece usage, StringPiece region,
                                   UErrorCode &status) {
    if (U_FAILURE(status)) { return -1; }
    bool foundCategory, foundUsage, foundRegion;
    UnitPreferenceMetadata desired(category, usage, region, -1, -1, status);
    int32_t idx = binarySearch(metadata, desired, &foundCategory, &foundUsage, &foundRegion, status);
    if (U_FAILURE(status)) { return -1; }
    if (idx >= 0) { return idx; }
    if (!foundCategory) {
        // TODO: failures can happen if units::getUnitCategory returns a category
        // that does not appear in unitPreferenceData. Do we want a unit test that
        // checks unitPreferenceData has full coverage of categories? Or just trust
        // CLDR?
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return -1;
    }
    U_ASSERT(foundCategory);
    while (!foundUsage) {
        int32_t lastDashIdx = desired.usage.lastIndexOf('-');
        if (lastDashIdx > 0) {
            desired.usage.truncate(lastDashIdx);
        } else if (uprv_strcmp(desired.usage.data(), "default") != 0) {
            desired.usage.truncate(0).append("default", status);
        } else {
            // "default" is not supposed to be missing for any valid category.
            status = U_MISSING_RESOURCE_ERROR;
            return -1;
        }
        idx = binarySearch(metadata, desired, &foundCategory, &foundUsage, &foundRegion, status);
        if (U_FAILURE(status)) { return -1; }
    }
    U_ASSERT(foundCategory);
    U_ASSERT(foundUsage);
    if (!foundRegion) {
        if (uprv_strcmp(desired.region.data(), "001") != 0) {
            desired.region.truncate(0).append("001", status);
            idx = binarySearch(metadata, desired, &foundCategory, &foundUsage, &foundRegion, status);
        }
        if (!foundRegion) {
            // "001" is not supposed to be missing for any valid usage.
            status = U_MISSING_RESOURCE_ERROR;
            return -1;
        }
    }
    U_ASSERT(foundCategory);
    U_ASSERT(foundUsage);
    U_ASSERT(foundRegion);
    U_ASSERT(idx >= 0);
    return idx;
}

} // namespace

UnitPreferenceMetadata::UnitPreferenceMetadata(StringPiece category, StringPiece usage,
                                               StringPiece region, int32_t prefsOffset,
                                               int32_t prefsCount, UErrorCode &status) {
    this->category.append(category, status);
    this->usage.append(usage, status);
    this->region.append(region, status);
    this->prefsOffset = prefsOffset;
    this->prefsCount = prefsCount;
}

int32_t UnitPreferenceMetadata::compareTo(const UnitPreferenceMetadata &other) const {
    int32_t cmp = uprv_strcmp(category.data(), other.category.data());
    if (cmp == 0) {
        cmp = uprv_strcmp(usage.data(), other.usage.data());
    }
    if (cmp == 0) {
        cmp = uprv_strcmp(region.data(), other.region.data());
    }
    return cmp;
}

int32_t UnitPreferenceMetadata::compareTo(const UnitPreferenceMetadata &other, bool *foundCategory,
                                          bool *foundUsage, bool *foundRegion) const {
    int32_t cmp = uprv_strcmp(category.data(), other.category.data());
    if (cmp == 0) {
        *foundCategory = true;
        cmp = uprv_strcmp(usage.data(), other.usage.data());
    }
    if (cmp == 0) {
        *foundUsage = true;
        cmp = uprv_strcmp(region.data(), other.region.data());
    }
    if (cmp == 0) {
        *foundRegion = true;
    }
    return cmp;
}

// TODO: this may be unnecessary. Fold into ConversionRates class? Or move to anonymous namespace?
void U_I18N_API getAllConversionRates(MaybeStackVector<ConversionRateInfo> &result, UErrorCode &status) {
    LocalUResourceBundlePointer unitsBundle(ures_openDirect(nullptr, "units", &status));
    ConversionRateDataSink sink(&result);
    ures_getAllItemsWithFallback(unitsBundle.getAlias(), "convertUnits", sink, status);
}

const ConversionRateInfo *ConversionRates::extractConversionInfo(StringPiece source,
                                                                 UErrorCode &status) const {
    for (size_t i = 0, n = conversionInfo_.length(); i < n; ++i) {
        if (conversionInfo_[i]->sourceUnit == source) return conversionInfo_[i];
    }

    status = U_INTERNAL_PROGRAM_ERROR;
    return nullptr;
}

U_I18N_API UnitPreferences::UnitPreferences(UErrorCode &status) {
    LocalUResourceBundlePointer unitsBundle(ures_openDirect(nullptr, "units", &status));
    UnitPreferencesSink sink(&unitPrefs_, &metadata_);
    ures_getAllItemsWithFallback(unitsBundle.getAlias(), "unitPreferenceData", sink, status);
}

CharString getKeyWordValue(const Locale &locale, StringPiece kw, UErrorCode &status) {
    if (U_FAILURE(status)) { return {}; }
    auto result = locale.getKeywordValue<CharString>(kw, status);
    if (U_SUCCESS(status) && result.isEmpty()) {
        status = U_MISSING_RESOURCE_ERROR;
    }
    return result;
}

MaybeStackVector<UnitPreference>
    U_I18N_API UnitPreferences::getPreferencesFor(StringPiece category, StringPiece usage,
                                                  const Locale &locale, UErrorCode &status) const {

    MaybeStackVector<UnitPreference> result;

    // TODO: remove this once all the categories are allowed.
    // WARNING: when this is removed please make sure to keep the "fahrenhe" => "fahrenheit" mapping
    UErrorCode internalMuStatus = U_ZERO_ERROR;
    if (category.compare("temperature") == 0) {
        CharString localeUnitCharString = getKeyWordValue(locale, "mu", internalMuStatus);
        if (U_SUCCESS(internalMuStatus)) {
            // The value for -u-mu- is `fahrenhe`, but CLDR and everything else uses `fahrenheit`
            if (localeUnitCharString == "fahrenhe") {
                localeUnitCharString = CharString("fahrenheit", status);
            }
            // TODO: use the unit category as Java especially when all the categories are allowed..
            if (localeUnitCharString == "celsius"
                || localeUnitCharString == "fahrenheit"
                || localeUnitCharString == "kelvin"
            ) {
                UnitPreference unitPref;
                unitPref.unit.append(localeUnitCharString, status);
                result.emplaceBackAndCheckErrorCode(status, unitPref);
                return result;
            }
        }
    }

    CharString region = ulocimp_getRegionForSupplementalData(locale.getName(), true, status);

    // Check the locale system tag, e.g `ms=metric`.
    UErrorCode internalMeasureTagStatus = U_ZERO_ERROR;
    CharString localeSystem = getKeyWordValue(locale, "measure", internalMeasureTagStatus);
    bool isLocaleSystem = false;
    if (U_SUCCESS(internalMeasureTagStatus) && (localeSystem == "metric" || localeSystem == "ussystem" || localeSystem == "uksystem")) {
        isLocaleSystem = true;
    }

    int32_t idx =
        getPreferenceMetadataIndex(&metadata_, category, usage, region.toStringPiece(), status);
    if (U_FAILURE(status)) {
        return result;
    }

    U_ASSERT(idx >= 0); // Failures should have been taken care of by `status`.
    const UnitPreferenceMetadata *m = metadata_[idx];
        
    if (isLocaleSystem) {
        // if the locale ID specifies a measurment system, check if ALL of the units we got back
        // are members of that system (or are "metric_adjacent", which we consider to match all
        // the systems)
        bool unitsMatchSystem = true;
        ConversionRates rates(status);
        for (int32_t i = 0; unitsMatchSystem && i < m->prefsCount; i++) {
            const UnitPreference& unitPref = *(unitPrefs_[i + m->prefsOffset]);
            MeasureUnitImpl measureUnit = MeasureUnitImpl::forIdentifier(unitPref.unit.data(), status);
            for (int32_t j = 0; unitsMatchSystem && j < measureUnit.singleUnits.length(); j++) {
                const SingleUnitImpl* singleUnit = measureUnit.singleUnits[j];
                const ConversionRateInfo* rateInfo = rates.extractConversionInfo(singleUnit->getSimpleUnitID(), status);
                CharString systems(rateInfo->systems, status);
                if (!systems.contains("metric_adjacent")) { // "metric-adjacent" is considered to match all the locale systems
                    if (!systems.contains(localeSystem.data())) {
                        unitsMatchSystem = false;
                    }
                }
            }
        }
        
        // if any of the units we got back above don't match the mearurement system the locale ID asked for,
        // throw out the region and just load the units for the base region for the requested measurement system
        if (!unitsMatchSystem) {
            region.clear();
            if (localeSystem == "ussystem") {
                region.append("US", status);
            } else if (localeSystem == "uksystem") {
                region.append("GB", status);
            } else {
                region.append("001", status);
            }
            idx = getPreferenceMetadataIndex(&metadata_, category, usage, region.toStringPiece(), status);
            if (U_FAILURE(status)) {
                return result;
            }
            
            m = metadata_[idx];
        }
    }
        
    for (int32_t i = 0; i < m->prefsCount; i++) {
        result.emplaceBackAndCheckErrorCode(status, *(unitPrefs_[i + m->prefsOffset]));
    }
    return result;
}

} // namespace units
U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */
