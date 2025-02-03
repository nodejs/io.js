// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2014-2016, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*
*
* File REGION.CPP
*
* Modification History:*
*   Date        Name        Description
* 01/15/13      Emmons      Original Port from ICU4J
********************************************************************************
*/

/**
 * \file
 * \brief C++ API: Region classes (territory containment)
 */

#include "unicode/region.h"
#include "unicode/utypes.h"
#include "unicode/uobject.h"
#include "unicode/unistr.h"
#include "unicode/ures.h"
#include "ucln_in.h"
#include "cstring.h"
#include "mutex.h"
#include "uhash.h"
#include "umutex.h"
#include "uresimp.h"
#include "region_impl.h"
#include "util.h"

#if !UCONFIG_NO_FORMATTING


U_CDECL_BEGIN

/**
 * Cleanup callback func
 */
static UBool U_CALLCONV region_cleanup()
{
    icu::Region::cleanupRegionData();

    return true;
}

U_CDECL_END

U_NAMESPACE_BEGIN

static UInitOnce gRegionDataInitOnce {};
static UVector* availableRegions[URGN_LIMIT];

static UHashtable *regionAliases = nullptr;
static UHashtable *regionIDMap = nullptr;
static UHashtable *numericCodeMap = nullptr;
static UVector *allRegions = nullptr;

static const char16_t UNKNOWN_REGION_ID [] = { 0x5A, 0x5A, 0 };  /* "ZZ" */
static const char16_t OUTLYING_OCEANIA_REGION_ID [] = { 0x51, 0x4F, 0 };  /* "QO" */
static const char16_t WORLD_ID [] = { 0x30, 0x30, 0x31, 0 };  /* "001" */
static const char16_t RANGE_MARKER = 0x7E; /* '~' */

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(RegionNameEnumeration)

/*
 * Initializes the region data from the ICU resource bundles.  The region data
 * contains the basic relationships such as which regions are known, what the numeric
 * codes are, any known aliases, and the territory containment data.
 *
 * If the region data has already loaded, then this method simply returns without doing
 * anything meaningful.
 */
void U_CALLCONV Region::loadRegionData(UErrorCode &status) {

    // Construct service objs first
    LocalUHashtablePointer newRegionIDMap(uhash_open(uhash_hashUnicodeString, uhash_compareUnicodeString, nullptr, &status));
    LocalUHashtablePointer newNumericCodeMap(uhash_open(uhash_hashLong,uhash_compareLong,nullptr,&status));
    LocalUHashtablePointer newRegionAliases(uhash_open(uhash_hashUnicodeString,uhash_compareUnicodeString,nullptr,&status));

    LocalPointer<UVector> continents(new UVector(uprv_deleteUObject, uhash_compareUnicodeString, status), status);
    LocalPointer<UVector> groupings(new UVector(uprv_deleteUObject, uhash_compareUnicodeString, status), status);
    LocalPointer<UVector> lpAllRegions(new UVector(uprv_deleteUObject, uhash_compareUnicodeString, status), status);
    allRegions = lpAllRegions.orphan();

    LocalUResourceBundlePointer metadata(ures_openDirect(nullptr,"metadata",&status));
    LocalUResourceBundlePointer metadataAlias(ures_getByKey(metadata.getAlias(),"alias",nullptr,&status));
    LocalUResourceBundlePointer territoryAlias(ures_getByKey(metadataAlias.getAlias(),"territory",nullptr,&status));

    LocalUResourceBundlePointer supplementalData(ures_openDirect(nullptr,"supplementalData",&status));
    LocalUResourceBundlePointer codeMappings(ures_getByKey(supplementalData.getAlias(),"codeMappings",nullptr,&status));

    LocalUResourceBundlePointer idValidity(ures_getByKey(supplementalData.getAlias(),"idValidity",nullptr,&status));
    LocalUResourceBundlePointer regionList(ures_getByKey(idValidity.getAlias(),"region",nullptr,&status));
    LocalUResourceBundlePointer regionRegular(ures_getByKey(regionList.getAlias(),"regular",nullptr,&status));
    LocalUResourceBundlePointer regionMacro(ures_getByKey(regionList.getAlias(),"macroregion",nullptr,&status));
    LocalUResourceBundlePointer regionUnknown(ures_getByKey(regionList.getAlias(),"unknown",nullptr,&status));

    LocalUResourceBundlePointer territoryContainment(ures_getByKey(supplementalData.getAlias(),"territoryContainment",nullptr,&status));
    LocalUResourceBundlePointer worldContainment(ures_getByKey(territoryContainment.getAlias(),"001",nullptr,&status));
    LocalUResourceBundlePointer groupingContainment(ures_getByKey(territoryContainment.getAlias(),"grouping",nullptr,&status));

    ucln_i18n_registerCleanup(UCLN_I18N_REGION, region_cleanup);
    if (U_FAILURE(status)) {
        return;
    }

    // now, initialize
    uhash_setValueDeleter(newRegionIDMap.getAlias(), uprv_deleteUObject);  // regionIDMap owns objs
    uhash_setKeyDeleter(newRegionAliases.getAlias(), uprv_deleteUObject);  // regionAliases owns the string keys


    while (U_SUCCESS(status) && ures_hasNext(regionRegular.getAlias())) {
        UnicodeString regionName = ures_getNextUnicodeString(regionRegular.getAlias(),nullptr,&status);
        int32_t rangeMarkerLocation = regionName.indexOf(RANGE_MARKER);
        char16_t buf[6];
        regionName.extract(buf,6,status);
        if ( rangeMarkerLocation > 0 ) {
            char16_t endRange = regionName.charAt(rangeMarkerLocation+1);
            buf[rangeMarkerLocation] = 0;
            while (U_SUCCESS(status) && buf[rangeMarkerLocation-1] <= endRange) {
                LocalPointer<UnicodeString> newRegion(new UnicodeString(buf), status);
                allRegions->adoptElement(newRegion.orphan(), status);
                buf[rangeMarkerLocation-1]++;
            }
        } else {
            LocalPointer<UnicodeString> newRegion(new UnicodeString(regionName), status);
            allRegions->adoptElement(newRegion.orphan(), status);
        }
    }

    while (U_SUCCESS(status) && ures_hasNext(regionMacro.getAlias())) {
        UnicodeString regionName = ures_getNextUnicodeString(regionMacro.getAlias(),nullptr,&status);
        int32_t rangeMarkerLocation = regionName.indexOf(RANGE_MARKER);
        char16_t buf[6];
        regionName.extract(buf,6,status);
        if ( rangeMarkerLocation > 0 ) {
            char16_t endRange = regionName.charAt(rangeMarkerLocation+1);
            buf[rangeMarkerLocation] = 0;
            while ( buf[rangeMarkerLocation-1] <= endRange && U_SUCCESS(status)) {
                LocalPointer<UnicodeString> newRegion(new UnicodeString(buf), status);
                allRegions->adoptElement(newRegion.orphan(),status);
                buf[rangeMarkerLocation-1]++;
            }
        } else {
            LocalPointer<UnicodeString> newRegion(new UnicodeString(regionName), status);
            allRegions->adoptElement(newRegion.orphan(),status);
        }
    }

    while (U_SUCCESS(status) && ures_hasNext(regionUnknown.getAlias())) {
        LocalPointer<UnicodeString> regionName (
            new UnicodeString(ures_getNextUnicodeString(regionUnknown.getAlias(), nullptr, &status), status));
        allRegions->adoptElement(regionName.orphan(),status);
    }

    while (U_SUCCESS(status) && ures_hasNext(worldContainment.getAlias())) {
        UnicodeString *continentName = new UnicodeString(ures_getNextUnicodeString(worldContainment.getAlias(),nullptr,&status));
        continents->adoptElement(continentName,status);
    }
    if (U_FAILURE(status)) {
        return;
    }

    for ( int32_t i = 0 ; i < allRegions->size() ; i++ ) {
        LocalPointer<Region> r(new Region(), status);
        if ( U_FAILURE(status) ) {
           return;
        }
        UnicodeString* regionName = static_cast<UnicodeString*>(allRegions->elementAt(i));
        r->idStr = *regionName;

        r->idStr.extract(0,r->idStr.length(),r->id,sizeof(r->id),US_INV);
        r->fType = URGN_TERRITORY; // Only temporary - figure out the real type later once the aliases are known.

        int32_t pos = 0;
        int32_t result = ICU_Utility::parseAsciiInteger(r->idStr, pos);
        if (pos > 0) {
            r->code = result; // Convert string to number
            uhash_iput(newNumericCodeMap.getAlias(),r->code,(void *)(r.getAlias()),&status);
            r->fType = URGN_SUBCONTINENT;
        } else {
            r->code = -1;
        }
        void* idStrAlias = (void*)&(r->idStr); // about to orphan 'r'. Save this off.
        uhash_put(newRegionIDMap.getAlias(),idStrAlias,(void *)(r.orphan()),&status); // regionIDMap takes ownership
    }

    UResourceBundle *groupingBundle = nullptr;
    while (U_SUCCESS(status) && ures_hasNext(groupingContainment.getAlias())) {
        groupingBundle = ures_getNextResource(groupingContainment.getAlias(), groupingBundle, &status);
        if (U_FAILURE(status)) {
            break;
        }
        UnicodeString *groupingName = new UnicodeString(ures_getKey(groupingBundle), -1, US_INV);
        LocalPointer<UnicodeString> lpGroupingName(groupingName, status);
        groupings->adoptElement(lpGroupingName.orphan(), status);
        if (U_FAILURE(status)) {
            break;
        }
        Region* grouping = static_cast<Region*>(uhash_get(newRegionIDMap.getAlias(), groupingName));
        if (grouping != nullptr) {
            for (int32_t i = 0; i < ures_getSize(groupingBundle) && U_SUCCESS(status); i++) {
                UnicodeString child = ures_getUnicodeStringByIndex(groupingBundle, i, &status);
                if (U_SUCCESS(status)) {
                    if (grouping->containedRegions == nullptr) {
                        LocalPointer<UVector> lpContainedRegions(
                            new UVector(uprv_deleteUObject, uhash_compareUnicodeString, status), status);
                        grouping->containedRegions = lpContainedRegions.orphan();
                        if (U_FAILURE(status)) {
                            break;
                        }
                    }
                    LocalPointer<UnicodeString> lpChildCopy(new UnicodeString(child), status);
                    grouping->containedRegions->adoptElement(lpChildCopy.orphan(), status);
                }
            }
        }
    }
    ures_close(groupingBundle);
    
    // Process the territory aliases
    while (U_SUCCESS(status) && ures_hasNext(territoryAlias.getAlias())) {
        LocalUResourceBundlePointer res(ures_getNextResource(territoryAlias.getAlias(),nullptr,&status));
        const char *aliasFrom = ures_getKey(res.getAlias());
        LocalPointer<UnicodeString> aliasFromStr(new UnicodeString(aliasFrom, -1, US_INV), status);
        UnicodeString aliasTo = ures_getUnicodeStringByKey(res.getAlias(),"replacement",&status);
        res.adoptInstead(nullptr);

        const Region* aliasToRegion = static_cast<Region*>(uhash_get(newRegionIDMap.getAlias(), &aliasTo));
        Region* aliasFromRegion = static_cast<Region*>(uhash_get(newRegionIDMap.getAlias(), aliasFromStr.getAlias()));

        if ( aliasToRegion != nullptr && aliasFromRegion == nullptr ) { // This is just an alias from some string to a region
            uhash_put(newRegionAliases.getAlias(),(void *)aliasFromStr.orphan(), (void *)aliasToRegion,&status);
        } else {
            if ( aliasFromRegion == nullptr ) { // Deprecated region code not in the primary codes list - so need to create a deprecated region for it.
                LocalPointer<Region> newRgn(new Region, status); 
                if ( U_SUCCESS(status) ) {
                    aliasFromRegion = newRgn.orphan();
                } else {
                    return; // error out
                }
                aliasFromRegion->idStr.setTo(*aliasFromStr);
                aliasFromRegion->idStr.extract(0,aliasFromRegion->idStr.length(),aliasFromRegion->id,sizeof(aliasFromRegion->id),US_INV);
                uhash_put(newRegionIDMap.getAlias(),(void *)&(aliasFromRegion->idStr),(void *)aliasFromRegion,&status);
                int32_t pos = 0;
                int32_t result = ICU_Utility::parseAsciiInteger(aliasFromRegion->idStr, pos);
                if ( pos > 0 ) {
                    aliasFromRegion->code = result; // Convert string to number
                    uhash_iput(newNumericCodeMap.getAlias(),aliasFromRegion->code,(void *)aliasFromRegion,&status);
                } else {
                    aliasFromRegion->code = -1;
                }
                aliasFromRegion->fType = URGN_DEPRECATED;
            } else {
                aliasFromRegion->fType = URGN_DEPRECATED;
            }

            {
                LocalPointer<UVector> newPreferredValues(new UVector(uprv_deleteUObject, uhash_compareUnicodeString, status), status);
                aliasFromRegion->preferredValues = newPreferredValues.orphan();
            }
            if( U_FAILURE(status)) {
                return;
            }
            UnicodeString currentRegion;
            //currentRegion.remove();   TODO: was already 0 length?
            for (int32_t i = 0 ; i < aliasTo.length() && U_SUCCESS(status); i++ ) {
                if ( aliasTo.charAt(i) != 0x0020 ) {
                    currentRegion.append(aliasTo.charAt(i));
                }
                if ( aliasTo.charAt(i) == 0x0020 || i+1 == aliasTo.length() ) {
                    Region* target = static_cast<Region*>(uhash_get(newRegionIDMap.getAlias(), &currentRegion));
                    if (target) {
                        LocalPointer<UnicodeString> preferredValue(new UnicodeString(target->idStr), status);
                        aliasFromRegion->preferredValues->adoptElement(preferredValue.orphan(),status);  // may add null if err
                    }
                    currentRegion.remove();
                }
            }
        }
    }

    // Process the code mappings - This will allow us to assign numeric codes to most of the territories.
    while (U_SUCCESS(status) && ures_hasNext(codeMappings.getAlias())) {
        UResourceBundle *mapping = ures_getNextResource(codeMappings.getAlias(),nullptr,&status);
        if (U_SUCCESS(status) && ures_getType(mapping) == URES_ARRAY && ures_getSize(mapping) == 3) {
            UnicodeString codeMappingID = ures_getUnicodeStringByIndex(mapping,0,&status);
            UnicodeString codeMappingNumber = ures_getUnicodeStringByIndex(mapping,1,&status);
            UnicodeString codeMapping3Letter = ures_getUnicodeStringByIndex(mapping,2,&status);

            Region* r = static_cast<Region*>(uhash_get(newRegionIDMap.getAlias(), &codeMappingID));
            if ( r ) {
                int32_t pos = 0;
                int32_t result = ICU_Utility::parseAsciiInteger(codeMappingNumber, pos);
                if ( pos > 0 ) {
                    r->code = result; // Convert string to number
                    uhash_iput(newNumericCodeMap.getAlias(),r->code,(void *)r,&status);
                }
                LocalPointer<UnicodeString> code3(new UnicodeString(codeMapping3Letter), status);
                uhash_put(newRegionAliases.getAlias(),(void *)code3.orphan(), (void *)r,&status);
            }
        }
        ures_close(mapping);
    }

    // Now fill in the special cases for WORLD, UNKNOWN, CONTINENTS, and GROUPINGS
    Region *r;
    UnicodeString WORLD_ID_STRING(WORLD_ID);
    r = static_cast<Region*>(uhash_get(newRegionIDMap.getAlias(), &WORLD_ID_STRING));
    if ( r ) {
        r->fType = URGN_WORLD;
    }

    UnicodeString UNKNOWN_REGION_ID_STRING(UNKNOWN_REGION_ID);
    r = static_cast<Region*>(uhash_get(newRegionIDMap.getAlias(), &UNKNOWN_REGION_ID_STRING));
    if ( r ) {
        r->fType = URGN_UNKNOWN;
    }

    for ( int32_t i = 0 ; i < continents->size() ; i++ ) {
        r = static_cast<Region*>(uhash_get(newRegionIDMap.getAlias(), continents->elementAt(i)));
        if ( r ) {
            r->fType = URGN_CONTINENT;
        }
    }

    for ( int32_t i = 0 ; i < groupings->size() ; i++ ) {
        r = static_cast<Region*>(uhash_get(newRegionIDMap.getAlias(), groupings->elementAt(i)));
        if ( r ) {
            r->fType = URGN_GROUPING;
        }
    }

    // Special case: The region code "QO" (Outlying Oceania) is a subcontinent code added by CLDR
    // even though it looks like a territory code.  Need to handle it here.

    UnicodeString OUTLYING_OCEANIA_REGION_ID_STRING(OUTLYING_OCEANIA_REGION_ID);
    r = static_cast<Region*>(uhash_get(newRegionIDMap.getAlias(), &OUTLYING_OCEANIA_REGION_ID_STRING));
    if ( r ) {
        r->fType = URGN_SUBCONTINENT;
    }

    // Load territory containment info from the supplemental data.
    while ( ures_hasNext(territoryContainment.getAlias()) ) {
        LocalUResourceBundlePointer mapping(ures_getNextResource(territoryContainment.getAlias(),nullptr,&status));
        if( U_FAILURE(status) ) {
            return;  // error out
        }
        const char *parent = ures_getKey(mapping.getAlias());
        if (uprv_strcmp(parent, "containedGroupings") == 0 || uprv_strcmp(parent, "deprecated") == 0) {
            continue; // handle new pseudo-parent types added in ICU data per cldrbug 7808; for now just skip.
            // #11232 is to do something useful with these.
        }
        UnicodeString parentStr = UnicodeString(parent, -1 , US_INV);
        Region* parentRegion = static_cast<Region*>(uhash_get(newRegionIDMap.getAlias(), &parentStr));

        for ( int j = 0 ; j < ures_getSize(mapping.getAlias()); j++ ) {
            UnicodeString child = ures_getUnicodeStringByIndex(mapping.getAlias(),j,&status);
            Region* childRegion = static_cast<Region*>(uhash_get(newRegionIDMap.getAlias(), &child));
            if ( parentRegion != nullptr && childRegion != nullptr ) {

                // Add the child region to the set of regions contained by the parent
                if (parentRegion->containedRegions == nullptr) {
                    LocalPointer<UVector> lpContainedRegions(
                        new UVector(uprv_deleteUObject, uhash_compareUnicodeString, status), status);
                    parentRegion->containedRegions = lpContainedRegions.orphan();
                    if (U_FAILURE(status)) {
                        return;
                    }
                }

                LocalPointer<UnicodeString> childStr(new UnicodeString(), status);
                if (U_FAILURE(status)) {
                    return;  // error out
                }
                childStr->fastCopyFrom(childRegion->idStr);
                parentRegion->containedRegions->adoptElement(childStr.orphan(),status);
                if (U_FAILURE(status)) {
                    return;
                }

                // Set the parent region to be the containing region of the child.
                // Regions of type GROUPING can't be set as the parent, since another region
                // such as a SUBCONTINENT, CONTINENT, or WORLD must always be the parent.
                if ( parentRegion->fType != URGN_GROUPING) {
                    childRegion->containingRegion = parentRegion;
                }
            }
        }
    }

    // Create the availableRegions lists
    int32_t pos = UHASH_FIRST;
    while ( const UHashElement* element = uhash_nextElement(newRegionIDMap.getAlias(),&pos)) {
        Region* ar = static_cast<Region*>(element->value.pointer);
        if ( availableRegions[ar->fType] == nullptr ) {
            LocalPointer<UVector> newAr(new UVector(uprv_deleteUObject, uhash_compareUnicodeString, status), status);
            availableRegions[ar->fType] = newAr.orphan();
        }
        LocalPointer<UnicodeString> arString(new UnicodeString(ar->idStr), status);
        if( U_FAILURE(status) ) {
            return;  // error out
        }
        availableRegions[ar->fType]->adoptElement(arString.orphan(), status);
    }
    
    // copy hashtables
    numericCodeMap = newNumericCodeMap.orphan();
    regionIDMap = newRegionIDMap.orphan();
    regionAliases = newRegionAliases.orphan();
}

void Region::cleanupRegionData() {
    for (int32_t i = 0 ; i < URGN_LIMIT ; i++ ) {
        if ( availableRegions[i] ) {
            delete availableRegions[i];
            availableRegions[i] = nullptr;
        }
    }

    if (regionAliases) {
        uhash_close(regionAliases);
    }

    if (numericCodeMap) {
        uhash_close(numericCodeMap);
    }

    if (regionIDMap) {
        uhash_close(regionIDMap);
    }
    if (allRegions) {
        delete allRegions;
        allRegions = nullptr;
    }

    regionAliases = numericCodeMap = regionIDMap = nullptr;

    gRegionDataInitOnce.reset();
}

Region::Region ()
        : code(-1),
          fType(URGN_UNKNOWN),
          containingRegion(nullptr),
          containedRegions(nullptr),
          preferredValues(nullptr) {
    id[0] = 0;
}

Region::~Region () {
    delete containedRegions;
    delete preferredValues;
}

/**
 * Returns true if the two regions are equal.
 * Per PMC, just use pointer compare, since we have at most one instance of each Region.
 */
bool
Region::operator==(const Region &that) const {
    return (idStr == that.idStr);
}

/**
 * Returns true if the two regions are NOT equal; that is, if operator ==() returns false.
 * Per PMC, just use pointer compare, since we have at most one instance of each Region.
 */
bool
Region::operator!=(const Region &that) const {
        return (idStr != that.idStr);
}

/**
 * Returns a pointer to a Region using the given region code.  The region code can be either 2-letter ISO code,
 * 3-letter ISO code,  UNM.49 numeric code, or other valid Unicode Region Code as defined by the LDML specification.
 * The identifier will be canonicalized internally using the supplemental metadata as defined in the CLDR.
 * If the region code is nullptr or not recognized, the appropriate error code will be set ( U_ILLEGAL_ARGUMENT_ERROR )
 */
const Region* U_EXPORT2
Region::getInstance(const char *region_code, UErrorCode &status) {

    umtx_initOnce(gRegionDataInitOnce, &loadRegionData, status);
    if (U_FAILURE(status)) {
        return nullptr;
    }

    if ( !region_code ) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }

    UnicodeString regionCodeString = UnicodeString(region_code, -1, US_INV);
    Region* r = static_cast<Region*>(uhash_get(regionIDMap, &regionCodeString));

    if ( !r ) {
        r = static_cast<Region*>(uhash_get(regionAliases, &regionCodeString));
    }

    if ( !r ) { // Unknown region code
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }

    if ( r->fType == URGN_DEPRECATED && r->preferredValues->size() == 1) {
        StringEnumeration *pv = r->getPreferredValues(status);
        pv->reset(status);
        const UnicodeString *ustr = pv->snext(status);
        r = static_cast<Region*>(uhash_get(regionIDMap, ustr));
        delete pv;
    }

    return r;

}

/**
 * Returns a pointer to a Region using the given numeric region code. If the numeric region code is not recognized,
 * the appropriate error code will be set ( U_ILLEGAL_ARGUMENT_ERROR ).
 */
const Region* U_EXPORT2
Region::getInstance (int32_t code, UErrorCode &status) {

    umtx_initOnce(gRegionDataInitOnce, &loadRegionData, status);
    if (U_FAILURE(status)) {
        return nullptr;
    }

    Region* r = static_cast<Region*>(uhash_iget(numericCodeMap, code));

    if ( !r ) { // Just in case there's an alias that's numeric, try to find it.
        UnicodeString id;
        ICU_Utility::appendNumber(id, code, 10, 1);
        r = static_cast<Region*>(uhash_get(regionAliases, &id));
    }

    if( U_FAILURE(status) ) {
        return nullptr;
    }

    if ( !r ) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }

    if ( r->fType == URGN_DEPRECATED && r->preferredValues->size() == 1) {
        StringEnumeration *pv = r->getPreferredValues(status);
        pv->reset(status);
        const UnicodeString *ustr = pv->snext(status);
        r = static_cast<Region*>(uhash_get(regionIDMap, ustr));
        delete pv;
    }

    return r;
}


/**
 * Returns an enumeration over the IDs of all known regions that match the given type.
 */
StringEnumeration* U_EXPORT2
Region::getAvailable(URegionType type, UErrorCode &status) {
    umtx_initOnce(gRegionDataInitOnce, &loadRegionData, status); // returns immediately if U_FAILURE(status)
    if (U_FAILURE(status)) {
        return nullptr;
    }
    return new RegionNameEnumeration(availableRegions[type],status);
}

/**
 * Returns a pointer to the region that contains this region.  Returns nullptr if this region is code "001" (World)
 * or "ZZ" (Unknown region). For example, calling this method with region "IT" (Italy) returns the
 * region "039" (Southern Europe).
 */
const Region*
Region::getContainingRegion() const {
    UErrorCode status = U_ZERO_ERROR;
    umtx_initOnce(gRegionDataInitOnce, &loadRegionData, status);
    return containingRegion;
}

/**
 * Return a pointer to the region that geographically contains this region and matches the given type,
 * moving multiple steps up the containment chain if necessary.  Returns nullptr if no containing region can be found
 * that matches the given type. Note: The URegionTypes = "URGN_GROUPING", "URGN_DEPRECATED", or "URGN_UNKNOWN"
 * are not appropriate for use in this API. nullptr will be returned in this case. For example, calling this method
 * with region "IT" (Italy) for type "URGN_CONTINENT" returns the region "150" ( Europe ).
 */
const Region*
Region::getContainingRegion(URegionType type) const {
    UErrorCode status = U_ZERO_ERROR;
    umtx_initOnce(gRegionDataInitOnce, &loadRegionData, status);
    if ( containingRegion == nullptr ) {
        return nullptr;
    }

    return ( containingRegion->fType == type)? containingRegion: containingRegion->getContainingRegion(type);
}

/**
 * Return an enumeration over the IDs of all the regions that are immediate children of this region in the
 * region hierarchy. These returned regions could be either macro regions, territories, or a mixture of the two,
 * depending on the containment data as defined in CLDR.  This API may return nullptr if this region doesn't have
 * any sub-regions. For example, calling this method with region "150" (Europe) returns an enumeration containing
 * the various sub regions of Europe - "039" (Southern Europe) - "151" (Eastern Europe) - "154" (Northern Europe)
 * and "155" (Western Europe).
 */
StringEnumeration*
Region::getContainedRegions(UErrorCode &status) const {
    umtx_initOnce(gRegionDataInitOnce, &loadRegionData, status); // returns immediately if U_FAILURE(status)
    if (U_FAILURE(status)) {
        return nullptr;
    }
    return new RegionNameEnumeration(containedRegions,status);
}

/**
 * Returns an enumeration over the IDs of all the regions that are children of this region anywhere in the region
 * hierarchy and match the given type.  This API may return an empty enumeration if this region doesn't have any
 * sub-regions that match the given type. For example, calling this method with region "150" (Europe) and type
 * "URGN_TERRITORY" returns a set containing all the territories in Europe ( "FR" (France) - "IT" (Italy) - "DE" (Germany) etc. )
 */
StringEnumeration*
Region::getContainedRegions( URegionType type, UErrorCode &status ) const {
    umtx_initOnce(gRegionDataInitOnce, &loadRegionData, status); // returns immediately if U_FAILURE(status)

    UVector result(nullptr, uhash_compareChars, status);
    LocalPointer<StringEnumeration> cr(getContainedRegions(status), status);
    if (U_FAILURE(status)) {
        return nullptr;
    }

    const char *regionId;
    while((regionId = cr->next(nullptr, status)) != nullptr && U_SUCCESS(status)) {
        const Region *r = Region::getInstance(regionId, status);
        if ( r->getType() == type) {
            result.addElement(const_cast<UnicodeString *>(&r->idStr), status);
        } else {
            LocalPointer<StringEnumeration> children(r->getContainedRegions(type, status));
            const char *id2;
            while(U_SUCCESS(status) && ((id2 = children->next(nullptr, status)) != nullptr)) {
                const Region *r2 = Region::getInstance(id2,status);
                result.addElement(const_cast<UnicodeString *>(&r2->idStr), status);
            }
        }
    }
    LocalPointer<StringEnumeration> resultEnumeration(
        new RegionNameEnumeration(&result, status), status);
    return U_SUCCESS(status) ? resultEnumeration.orphan() : nullptr;
}

/**
 * Returns true if this region contains the supplied other region anywhere in the region hierarchy.
 */
UBool
Region::contains(const Region &other) const {
    UErrorCode status = U_ZERO_ERROR;
    umtx_initOnce(gRegionDataInitOnce, &loadRegionData, status);

    if (!containedRegions) {
          return false;
    }
    if (containedRegions->contains((void *)&other.idStr)) {
        return true;
    } else {
        for ( int32_t i = 0 ; i < containedRegions->size() ; i++ ) {
            UnicodeString* crStr = static_cast<UnicodeString*>(containedRegions->elementAt(i));
            Region* cr = static_cast<Region*>(uhash_get(regionIDMap, crStr));
            if ( cr && cr->contains(other) ) {
                return true;
            }
        }
    }

    return false;
}

/**
 * For deprecated regions, return an enumeration over the IDs of the regions that are the preferred replacement
 * regions for this region.  Returns nullptr for a non-deprecated region.  For example, calling this method with region
 * "SU" (Soviet Union) would return a list of the regions containing "RU" (Russia), "AM" (Armenia), "AZ" (Azerbaijan), etc...
 */
StringEnumeration*
Region::getPreferredValues(UErrorCode &status) const {
    umtx_initOnce(gRegionDataInitOnce, &loadRegionData, status); // returns immediately if U_FAILURE(status)
    if (U_FAILURE(status) || fType != URGN_DEPRECATED) {
        return nullptr;
    }
    return new RegionNameEnumeration(preferredValues,status);
}


/**
 * Return this region's canonical region code.
 */
const char*
Region::getRegionCode() const {
    return id;
}

int32_t
Region::getNumericCode() const {
    return code;
}

/**
 * Returns the region type of this region.
 */
URegionType
Region::getType() const {
    return fType;
}

RegionNameEnumeration::RegionNameEnumeration(UVector *nameList, UErrorCode& status) :
        pos(0), fRegionNames(nullptr) {
    // TODO: https://unicode-org.atlassian.net/browse/ICU-21829
    // Is all of the copying going on here really necessary?
    if (nameList && U_SUCCESS(status)) {
        LocalPointer<UVector> regionNames(
            new UVector(uprv_deleteUObject, uhash_compareUnicodeString, nameList->size(), status), status);
        for ( int32_t i = 0 ; U_SUCCESS(status) && i < nameList->size() ; i++ ) {
            UnicodeString* this_region_name = static_cast<UnicodeString*>(nameList->elementAt(i));
            LocalPointer<UnicodeString> new_region_name(new UnicodeString(*this_region_name), status);
            regionNames->adoptElement(new_region_name.orphan(), status);
        }
        if (U_SUCCESS(status)) {
            fRegionNames = regionNames.orphan();
        }
    }
}

const UnicodeString*
RegionNameEnumeration::snext(UErrorCode& status) {
  if (U_FAILURE(status) || (fRegionNames==nullptr)) {
    return nullptr;
  }
  const UnicodeString* nextStr = static_cast<const UnicodeString*>(fRegionNames->elementAt(pos));
  if (nextStr!=nullptr) {
    pos++;
  }
  return nextStr;
}

void
RegionNameEnumeration::reset(UErrorCode& /*status*/) {
    pos=0;
}

int32_t
RegionNameEnumeration::count(UErrorCode& /*status*/) const {
    return (fRegionNames==nullptr) ? 0 : fRegionNames->size();
}

RegionNameEnumeration::~RegionNameEnumeration() {
    delete fRegionNames;
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */

//eof
