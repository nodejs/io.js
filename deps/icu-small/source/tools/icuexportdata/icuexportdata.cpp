// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include "unicode/localpointer.h"
#include "unicode/umachine.h"
#include "unicode/unistr.h"
#include "unicode/urename.h"
#include "unicode/uset.h"
#include <vector>
#include <algorithm>
#include "toolutil.h"
#include "uoptions.h"
#include "cmemory.h"
#include "charstr.h"
#include "cstring.h"
#include "unicode/uchar.h"
#include "unicode/errorcode.h"
#include "unicode/uniset.h"
#include "unicode/uscript.h"
#include "unicode/putil.h"
#include "unicode/umutablecptrie.h"
#include "unicode/ucharstriebuilder.h"
#include "ucase.h"
#include "unicode/normalizer2.h"
#include "normalizer2impl.h"
#include "writesrc.h"

U_NAMESPACE_USE

/*
 * Global - verbosity
 */
UBool VERBOSE = false;
UBool QUIET = false;

UBool haveCopyright = true;
UCPTrieType trieType = UCPTRIE_TYPE_SMALL;
const char* destdir = "";

// Mask constants for modified values in the Script CodePointTrie, values are logically 12-bits.
int16_t DATAEXPORT_SCRIPT_X_WITH_COMMON    = 0x0400;
int16_t DATAEXPORT_SCRIPT_X_WITH_INHERITED = 0x0800;
int16_t DATAEXPORT_SCRIPT_X_WITH_OTHER     = 0x0c00;

// TODO(ICU-21821): Replace this with a call to a library function
int32_t scxCodePoints[] = {
      7415, 7377, 7380, 7387, 7390, 7391, 7394, 7395, 7396, 7397,
      7398, 7399, 7400, 7403, 7404, 7406, 7407, 7408, 7409, 113824,
      113825, 113826, 113827, 834, 837, 7616, 7617, 12294, 12350, 12351,
      12688, 12689, 12690, 12691, 12692, 12693, 12694, 12695, 12696, 12697,
      12698, 12699, 12700, 12701, 12702, 12703, 12736, 12737, 12738, 12739,
      12740, 12741, 12742, 12743, 12744, 12745, 12746, 12747, 12748, 12749,
      12750, 12751, 12752, 12753, 12754, 12755, 12756, 12757, 12758, 12759,
      12760, 12761, 12762, 12763, 12764, 12765, 12766, 12767, 12768, 12769,
      12770, 12771, 12832, 12833, 12834, 12835, 12836, 12837, 12838, 12839,
      12840, 12841, 12842, 12843, 12844, 12845, 12846, 12847, 12848, 12849,
      12850, 12851, 12852, 12853, 12854, 12855, 12856, 12857, 12858, 12859,
      12860, 12861, 12862, 12863, 12864, 12865, 12866, 12867, 12868, 12869,
      12870, 12871, 12928, 12929, 12930, 12931, 12932, 12933, 12934, 12935,
      12936, 12937, 12938, 12939, 12940, 12941, 12942, 12943, 12944, 12945,
      12946, 12947, 12948, 12949, 12950, 12951, 12952, 12953, 12954, 12955,
      12956, 12957, 12958, 12959, 12960, 12961, 12962, 12963, 12964, 12965,
      12966, 12967, 12968, 12969, 12970, 12971, 12972, 12973, 12974, 12975,
      12976, 12992, 12993, 12994, 12995, 12996, 12997, 12998, 12999, 13000,
      13001, 13002, 13003, 13055, 13144, 13145, 13146, 13147, 13148, 13149,
      13150, 13151, 13152, 13153, 13154, 13155, 13156, 13157, 13158, 13159,
      13160, 13161, 13162, 13163, 13164, 13165, 13166, 13167, 13168, 13179,
      13180, 13181, 13182, 13183, 13280, 13281, 13282, 13283, 13284, 13285,
      13286, 13287, 13288, 13289, 13290, 13291, 13292, 13293, 13294, 13295,
      13296, 13297, 13298, 13299, 13300, 13301, 13302, 13303, 13304, 13305,
      13306, 13307, 13308, 13309, 13310, 119648, 119649, 119650, 119651, 119652,
      119653, 119654, 119655, 119656, 119657, 119658, 119659, 119660, 119661, 119662,
      119663, 119664, 119665, 127568, 127569, 867, 868, 869, 870, 871,
      872, 873, 874, 875, 876, 877, 878, 879, 7418, 7674,
      66272, 66273, 66274, 66275, 66276, 66277, 66278, 66279, 66280, 66281,
      66282, 66283, 66284, 66285, 66286, 66287, 66288, 66289, 66290, 66291,
      66292, 66293, 66294, 66295, 66296, 66297, 66298, 66299, 1748, 64830,
      64831, 1611, 1612, 1613, 1614, 1615, 1616, 1617, 1618, 1619,
      1620, 1621, 1648, 65010, 65021, 7381, 7382, 7384, 7393, 7402,
      7405, 7413, 7414, 43249, 12330, 12331, 12332, 12333, 43471, 65794,
      65847, 65848, 65849, 65850, 65851, 65852, 65853, 65854, 65855, 1156,
      1159, 11843, 42607, 1157, 1158, 1155, 7672, 7379, 7411, 7416,
      7417, 7401, 7383, 7385, 7388, 7389, 7392, 43251, 4347, 3046,
      3047, 3048, 3049, 3050, 3051, 3052, 3053, 3054, 3055, 3056,
      3057, 3058, 3059, 70401, 70403, 70459, 70460, 73680, 73681, 73683,
      2790, 2791, 2792, 2793, 2794, 2795, 2796, 2797, 2798, 2799,
      2662, 2663, 2664, 2665, 2666, 2667, 2668, 2669, 2670, 2671,
      42752, 42753, 42754, 42755, 42756, 42757, 42758, 42759, 12337, 12338,
      12339, 12340, 12341, 12441, 12442, 12443, 12444, 12448, 12540, 65392,
      65438, 65439, 3302, 3303, 3304, 3305, 3306, 3307, 3308, 3309,
      3310, 3311, 8239, 68338, 6146, 6147, 6149, 1564, 1632, 1633,
      1634, 1635, 1636, 1637, 1638, 1639, 1640, 1641, 2534, 2535,
      2536, 2537, 2538, 2539, 2540, 2541, 2542, 2543, 4160, 4161,
      4162, 4163, 4164, 4165, 4166, 4167, 4168, 4169, 65792, 65793,
      65799, 65800, 65801, 65802, 65803, 65804, 65805, 65806, 65807, 65808,
      65809, 65810, 65811, 65812, 65813, 65814, 65815, 65816, 65817, 65818,
      65819, 65820, 65821, 65822, 65823, 65824, 65825, 65826, 65827, 65828,
      65829, 65830, 65831, 65832, 65833, 65834, 65835, 65836, 65837, 65838,
      65839, 65840, 65841, 65842, 65843, 7412, 8432, 12348, 12349, 43310,
      7376, 7378, 5941, 5942, 2406, 2407, 2408, 2409, 2410, 2411,
      2412, 2413, 2414, 2415, 12291, 12307, 12316, 12317, 12318, 12319,
      12336, 12343, 65093, 65094, 1548, 1563, 12289, 12290, 12296, 12297,
      12298, 12299, 12300, 12301, 12302, 12303, 12304, 12305, 12308, 12309,
      12310, 12311, 12312, 12313, 12314, 12315, 12539, 65377, 65378, 65379,
      65380, 65381, 7386, 1567, 7410, 1600, 43062, 43063, 43064, 43065,
      2386, 2385, 43059, 43060, 43061, 43056, 43057, 43058, 2404, 2405
    };

void handleError(ErrorCode& status, int line, const char* context) {
    if (status.isFailure()) {
        std::cerr << "Error[" << line << "]: " << context << ": " << status.errorName() << std::endl;
        exit(status.reset());
    }
}

class PropertyValueNameGetter : public ValueNameGetter {
public:
    PropertyValueNameGetter(UProperty prop) : property(prop) {}
    ~PropertyValueNameGetter() override;
    const char *getName(uint32_t value) override {
        return u_getPropertyValueName(property, value, U_SHORT_PROPERTY_NAME);
    }

private:
    UProperty property;
};

PropertyValueNameGetter::~PropertyValueNameGetter() {}

// Dump an aliases = [...] key for properties with aliases
void dumpPropertyAliases(UProperty uproperty, FILE* f) {
    int i = U_LONG_PROPERTY_NAME + 1;

    while(true) {
        // The API works by having extra names after U_LONG_PROPERTY_NAME, sequentially,
        // and returning null after that
        const char* alias = u_getPropertyName(uproperty, static_cast<UPropertyNameChoice>(i));
        if (!alias) {
            break;
        }
        if (i == U_LONG_PROPERTY_NAME + 1) {
            fprintf(f, "aliases = [\"%s\"", alias);
        } else {
            fprintf(f, ", \"%s\"", alias);
        }
        i++;
    }
    if (i != U_LONG_PROPERTY_NAME + 1) {
        fprintf(f, "]\n");
    }
}

void dumpBinaryProperty(UProperty uproperty, FILE* f) {
    IcuToolErrorCode status("icuexportdata: dumpBinaryProperty");
    const char* fullPropName = u_getPropertyName(uproperty, U_LONG_PROPERTY_NAME);
    const char* shortPropName = u_getPropertyName(uproperty, U_SHORT_PROPERTY_NAME);
    const USet* uset = u_getBinaryPropertySet(uproperty, status);
    handleError(status, __LINE__, fullPropName);

    fputs("[[binary_property]]\n", f);
    fprintf(f, "long_name = \"%s\"\n", fullPropName);
    if (shortPropName) fprintf(f, "short_name = \"%s\"\n", shortPropName);
    fprintf(f, "uproperty_discr = 0x%X\n", uproperty);
    dumpPropertyAliases(uproperty, f);
    usrc_writeUnicodeSet(f, uset, UPRV_TARGET_SYNTAX_TOML);
}

// If the value exists, dump an indented entry of the format
// `"  {discr = <discriminant>, long = <longname>, short = <shortname>, aliases = [<aliases>]},"`
void dumpValueEntry(UProperty uproperty, int v, bool is_mask, FILE* f) {
    const char* fullValueName = u_getPropertyValueName(uproperty, v, U_LONG_PROPERTY_NAME);
    const char* shortValueName = u_getPropertyValueName(uproperty, v, U_SHORT_PROPERTY_NAME);
    if (!fullValueName) {
        return;
    }
    if (is_mask) {
        fprintf(f, "  {discr = 0x%X", v);
    } else {
        fprintf(f, "  {discr = %i", v);
    }
    fprintf(f, ", long = \"%s\"", fullValueName);
    if (shortValueName) {
        fprintf(f, ", short = \"%s\"", shortValueName);
    }
    int i = U_LONG_PROPERTY_NAME + 1;
    while(true) {
        // The API works by having extra names after U_LONG_PROPERTY_NAME, sequentially,
        // and returning null after that
        const char* alias = u_getPropertyValueName(uproperty, v, static_cast<UPropertyNameChoice>(i));
        if (!alias) {
            break;
        }
        if (i == U_LONG_PROPERTY_NAME + 1) {
            fprintf(f, ", aliases = [\"%s\"", alias);
        } else {
            fprintf(f, ", \"%s\"", alias);
        }
        i++;
    }
    if (i != U_LONG_PROPERTY_NAME + 1) {
        fprintf(f, "]");
    }
    fprintf(f, "},\n");
}

void dumpEnumeratedProperty(UProperty uproperty, FILE* f) {
    IcuToolErrorCode status("icuexportdata: dumpEnumeratedProperty");
    const char* fullPropName = u_getPropertyName(uproperty, U_LONG_PROPERTY_NAME);
    const char* shortPropName = u_getPropertyName(uproperty, U_SHORT_PROPERTY_NAME);
    const UCPMap* umap = u_getIntPropertyMap(uproperty, status);
    handleError(status, __LINE__, fullPropName);

    fputs("[[enum_property]]\n", f);
    fprintf(f, "long_name = \"%s\"\n", fullPropName);
    if (shortPropName) fprintf(f, "short_name = \"%s\"\n", shortPropName);
    fprintf(f, "uproperty_discr = 0x%X\n", uproperty);
    dumpPropertyAliases(uproperty, f);

    int32_t minValue = u_getIntPropertyMinValue(uproperty);
    U_ASSERT(minValue >= 0);
    int32_t maxValue = u_getIntPropertyMaxValue(uproperty);
    U_ASSERT(maxValue >= 0);

    fprintf(f, "values = [\n");
    for (int v = minValue; v <= maxValue; v++) {
        dumpValueEntry(uproperty, v, false, f);
    }
    fprintf(f, "]\n");

    PropertyValueNameGetter valueNameGetter(uproperty);
    usrc_writeUCPMap(f, umap, &valueNameGetter, UPRV_TARGET_SYNTAX_TOML);
    fputs("\n", f);


    UCPTrieValueWidth width = UCPTRIE_VALUE_BITS_32;
    if (maxValue <= 0xff) {
        width = UCPTRIE_VALUE_BITS_8;
    } else if (maxValue <= 0xffff) {
        width = UCPTRIE_VALUE_BITS_16;
    }
    LocalUMutableCPTriePointer builder(umutablecptrie_fromUCPMap(umap, status));
    LocalUCPTriePointer utrie(umutablecptrie_buildImmutable(
        builder.getAlias(),
        trieType,
        width,
        status));
    handleError(status, __LINE__, fullPropName);

    fputs("[enum_property.code_point_trie]\n", f);
    usrc_writeUCPTrie(f, shortPropName, utrie.getAlias(), UPRV_TARGET_SYNTAX_TOML);
}

/*
* Export Bidi_Mirroring_Glyph values (code points) in a similar way to how enumerated
* properties are dumped to file.
* Note: the data will store 0 for code points without a value defined for
* Bidi_Mirroring_Glyph.
*/
void dumpBidiMirroringGlyph(FILE* f) {
    UProperty uproperty = UCHAR_BIDI_MIRRORING_GLYPH;
    IcuToolErrorCode status("icuexportdata: dumpBidiMirroringGlyph");
    const char* fullPropName = u_getPropertyName(uproperty, U_LONG_PROPERTY_NAME);
    const char* shortPropName = u_getPropertyName(uproperty, U_SHORT_PROPERTY_NAME);
    handleError(status, __LINE__, fullPropName);

    // Store 21-bit code point as is
    UCPTrieValueWidth width = UCPTRIE_VALUE_BITS_32;

    // note: unlike dumpEnumeratedProperty, which can get inversion map data using
    // u_getIntPropertyMap(uproperty), the only reliable way to get Bidi_Mirroring_Glyph
    // is to use u_charMirror(cp) over the code point space.
    LocalUMutableCPTriePointer builder(umutablecptrie_open(0, 0, status));
    for(UChar32 c = UCHAR_MIN_VALUE; c <= UCHAR_MAX_VALUE; c++) {
        UChar32 mirroringGlyph = u_charMirror(c);
        // The trie builder code throws an error when it cannot compress the data sufficiently.
        // Therefore, when the value is undefined for a code point, keep a 0 in the trie
        // instead of the ICU API behavior of returning the code point value. Using 0
        // results in a relatively significant space savings by not including redundant data.
        if (c != mirroringGlyph) {
            umutablecptrie_set(builder.getAlias(), c, mirroringGlyph, status);
        }
    }

    LocalUCPTriePointer utrie(umutablecptrie_buildImmutable(
        builder.getAlias(),
        trieType,
        width,
        status));
    handleError(status, __LINE__, fullPropName);

    // currently a trie and inversion map are the same (as relied upon in characterproperties.cpp)
    const UCPMap* umap = reinterpret_cast<UCPMap *>(utrie.getAlias());

    fputs("[[enum_property]]\n", f);
    fprintf(f, "long_name = \"%s\"\n", fullPropName);
    if (shortPropName) {
        fprintf(f, "short_name = \"%s\"\n", shortPropName);
    }
    fprintf(f, "uproperty_discr = 0x%X\n", uproperty);
    dumpPropertyAliases(uproperty, f);

    usrc_writeUCPMap(f, umap, nullptr, UPRV_TARGET_SYNTAX_TOML);
    fputs("\n", f);

    fputs("[enum_property.code_point_trie]\n", f);
    usrc_writeUCPTrie(f, shortPropName, utrie.getAlias(), UPRV_TARGET_SYNTAX_TOML);
}

// After printing property value `v`, print `mask` if and only if `mask` comes immediately
// after the property in the listing
void maybeDumpMaskValue(UProperty uproperty, uint32_t v, uint32_t mask, FILE* f) {
    if (U_MASK(v) < mask && U_MASK(v + 1) > mask)
        dumpValueEntry(uproperty, mask, true, f);
}

void dumpGeneralCategoryMask(FILE* f) {
    IcuToolErrorCode status("icuexportdata: dumpGeneralCategoryMask");
    UProperty uproperty = UCHAR_GENERAL_CATEGORY_MASK;

    fputs("[[mask_property]]\n", f);
    const char* fullPropName = u_getPropertyName(uproperty, U_LONG_PROPERTY_NAME);
    const char* shortPropName = u_getPropertyName(uproperty, U_SHORT_PROPERTY_NAME);
    fprintf(f, "long_name = \"%s\"\n", fullPropName);
    if (shortPropName) fprintf(f, "short_name = \"%s\"\n", shortPropName);
    fprintf(f, "uproperty_discr = 0x%X\n", uproperty);
    dumpPropertyAliases(uproperty, f);


    fprintf(f, "mask_for = \"General_Category\"\n");
    uint32_t minValue = u_getIntPropertyMinValue(UCHAR_GENERAL_CATEGORY);
    U_ASSERT(minValue >= 0);
    uint32_t maxValue = u_getIntPropertyMaxValue(UCHAR_GENERAL_CATEGORY);
    U_ASSERT(maxValue >= 0);

    fprintf(f, "values = [\n");
    for (uint32_t v = minValue; v <= maxValue; v++) {
        dumpValueEntry(uproperty, U_MASK(v), true, f);

        // We want to dump these masks "in order", which means they
        // should come immediately after every property they contain
        maybeDumpMaskValue(uproperty, v, U_GC_L_MASK, f);
        maybeDumpMaskValue(uproperty, v, U_GC_LC_MASK, f);
        maybeDumpMaskValue(uproperty, v, U_GC_M_MASK, f);
        maybeDumpMaskValue(uproperty, v, U_GC_N_MASK, f);
        maybeDumpMaskValue(uproperty, v, U_GC_Z_MASK, f);
        maybeDumpMaskValue(uproperty, v, U_GC_C_MASK, f);
        maybeDumpMaskValue(uproperty, v, U_GC_P_MASK, f);
        maybeDumpMaskValue(uproperty, v, U_GC_S_MASK, f);
    }
    fprintf(f, "]\n");
}

void dumpScriptExtensions(FILE* f) {
    IcuToolErrorCode status("icuexportdata: dumpScriptExtensions");

    fputs("[[script_extensions]]\n", f);
    const char* scxFullPropName = u_getPropertyName(UCHAR_SCRIPT_EXTENSIONS, U_LONG_PROPERTY_NAME);
    const char* scxShortPropName = u_getPropertyName(UCHAR_SCRIPT_EXTENSIONS, U_SHORT_PROPERTY_NAME);
    fprintf(f, "long_name = \"%s\"\n", scxFullPropName);
    if (scxShortPropName) fprintf(f, "short_name = \"%s\"\n", scxShortPropName);
    fprintf(f, "uproperty_discr = 0x%X\n", UCHAR_SCRIPT_EXTENSIONS);
    dumpPropertyAliases(UCHAR_SCRIPT_EXTENSIONS, f);

    // We want to use 16 bits for our exported trie of sc/scx data because we
    // need 12 bits to match the 12 bits of data stored for sc/scx in the trie
    // in the uprops.icu data file.
    UCPTrieValueWidth scWidth = UCPTRIE_VALUE_BITS_16;

    // Create a mutable UCPTrie builder populated with Script property values data.
    const UCPMap* scInvMap = u_getIntPropertyMap(UCHAR_SCRIPT, status);
    handleError(status, __LINE__, scxFullPropName);
    LocalUMutableCPTriePointer builder(umutablecptrie_fromUCPMap(scInvMap, status));
    handleError(status, __LINE__, scxFullPropName);

    // The values for the output scx companion array.
    // Invariant is that all subvectors are distinct.
    std::vector< std::vector<uint16_t> > outputDedupVec;

    // The sc/scx companion array is an array of arrays (of script codes)
    fputs("script_code_array = [\n", f);
    for(const UChar32 cp : scxCodePoints) {
        // Get the Script value
        uint32_t scVal = umutablecptrie_get(builder.getAlias(), cp);
        // Get the Script_Extensions value (array of Script codes)
        const int32_t SCX_ARRAY_CAPACITY = 32;
        UScriptCode scxValArray[SCX_ARRAY_CAPACITY];
        int32_t numScripts = uscript_getScriptExtensions(cp, scxValArray, SCX_ARRAY_CAPACITY, status);
        handleError(status, __LINE__, scxFullPropName);

        // Convert the scx array into a vector
        std::vector<uint16_t> scxValVec;
        for(int i = 0; i < numScripts; i++) {
            scxValVec.push_back(scxValArray[i]);
        }
        // Ensure that it is sorted
        std::sort(scxValVec.begin(), scxValVec.end());
        // Copy the Script value into the first position of the scx array only
        // if we have the "other" case (Script value is not Common nor Inherited).
        // This offers faster access when users want only the Script value.
        if (scVal != USCRIPT_COMMON && scVal != USCRIPT_INHERITED) {
            scxValVec.insert(scxValVec.begin(), scVal);
        }

        // See if there is already an scx value array matching the newly built one.
        // If there is, then use its index.
        // If not, then append the new value array.
        bool isScxValUnique = true;
        size_t outputIndex = 0;
        for (outputIndex = 0; outputIndex < outputDedupVec.size(); outputIndex++) {
            if (outputDedupVec[outputIndex] == scxValVec) {
                isScxValUnique = false;
                break;
            }
        }

        if (isScxValUnique) {
            outputDedupVec.push_back(scxValVec);
            usrc_writeArray(f, "  [", scxValVec.data(), 16, scxValVec.size(), "    ", "],\n");
        }

        // We must update the value in the UCPTrie for the code point to contain:
        // 9..0 the Script code in the lower 10 bits when 11..10 is 0, else it is
        //   the index into the companion array
        // 11..10 the same higher-order 2 bits in the trie in uprops.icu indicating whether
        //   3: other
        //   2: Script=Inherited
        //   1: Script=Common
        //   0: Script=value in 9..0 (N/A because we are in this loop to create the companion array for non-0 cases)
        uint16_t mask = 0;
        if (scVal == USCRIPT_COMMON) {
            mask = DATAEXPORT_SCRIPT_X_WITH_COMMON;
        } else if (scVal == USCRIPT_INHERITED) {
            mask = DATAEXPORT_SCRIPT_X_WITH_INHERITED;
        } else {
            mask = DATAEXPORT_SCRIPT_X_WITH_OTHER;
        }

        // The new trie value is the index into the new array with the high order bits set
        uint32_t newScVal = outputIndex | mask;

        // Update the code point in the mutable trie builder with the trie value
        umutablecptrie_set(builder.getAlias(), cp, newScVal, status);
        handleError(status, __LINE__, scxFullPropName);
    }
    fputs("]\n\n", f);  // Print the TOML close delimiter for the outer array.

    // Convert from mutable trie builder to immutable trie.
    LocalUCPTriePointer utrie(umutablecptrie_buildImmutable(
        builder.getAlias(),
        trieType,
        scWidth,
        status));
    handleError(status, __LINE__, scxFullPropName);

    fputs("[script_extensions.code_point_trie]\n", f);
    usrc_writeUCPTrie(f, scxShortPropName, utrie.getAlias(), UPRV_TARGET_SYNTAX_TOML);
}

FILE* prepareOutputFile(const char* basename) {
    IcuToolErrorCode status("icuexportdata");
    CharString outFileName;
    if (destdir != nullptr && *destdir != 0) {
        outFileName.append(destdir, status).ensureEndsWithFileSeparator(status);
    }
    outFileName.append(basename, status);
    outFileName.append(".toml", status);
    handleError(status, __LINE__, basename);

    FILE* f = fopen(outFileName.data(), "w");
    if (f == nullptr) {
        std::cerr << "Unable to open file: " << outFileName.data() << std::endl;
        exit(U_FILE_ACCESS_ERROR);
    }
    if (!QUIET) {
        std::cout << "Writing to: " << outFileName.data() << std::endl;
    }

    if (haveCopyright) {
        usrc_writeCopyrightHeader(f, "#", 2021);
    }
    usrc_writeFileNameGeneratedBy(f, "#", basename, "icuexportdata.cpp");

    return f;
}

#if !UCONFIG_NO_NORMALIZATION

struct PendingDescriptor {
    UChar32 scalar;
    uint32_t descriptor;
    UBool supplementary;
};

void writeCanonicalCompositions(USet* backwardCombiningStarters) {
    IcuToolErrorCode status("icuexportdata: computeCanonicalCompositions");
    const char* basename = "compositions";
    FILE* f = prepareOutputFile(basename);

    LocalPointer<UCharsTrieBuilder> backwardBuilder(new UCharsTrieBuilder(status), status);

    const int32_t DECOMPOSITION_BUFFER_SIZE = 20;
    UChar32 utf32[DECOMPOSITION_BUFFER_SIZE];

    const Normalizer2* nfc = Normalizer2::getNFCInstance(status);
    for (UChar32 c = 0; c <= 0x10FFFF; ++c) {
        if (c >= 0xD800 && c < 0xE000) {
            // Surrogate
            continue;
        }
        UnicodeString decomposition;
        if (!nfc->getRawDecomposition(c, decomposition)) {
            continue;
        }
        int32_t len = decomposition.toUTF32(utf32, DECOMPOSITION_BUFFER_SIZE, status);
        if (len != 2) {
            continue;
        }
        UChar32 starter = utf32[0];
        UChar32 second = utf32[1];
        UChar32 composite = nfc->composePair(starter, second);
        if (composite < 0) {
            continue;
        }
        if (c != composite) {
            status.set(U_INTERNAL_PROGRAM_ERROR);
            handleError(status, __LINE__, basename);
        }
        if (!u_getCombiningClass(second)) {
            uset_add(backwardCombiningStarters, second);
        }
        if (composite >= 0xAC00 && composite <= 0xD7A3) {
            // Hangul syllable
            continue;
        }

        UnicodeString backward;
        backward.append(second);
        backward.append(starter);
        backwardBuilder->add(backward, static_cast<int32_t>(composite), status);
    }
    UnicodeString canonicalCompositionTrie;
    backwardBuilder->buildUnicodeString(USTRINGTRIE_BUILD_SMALL, canonicalCompositionTrie, status);

    usrc_writeArray(f, "compositions = [\n  ", canonicalCompositionTrie.getBuffer(), 16, canonicalCompositionTrie.length(), "  ", "\n]\n");
    fclose(f);
    handleError(status, __LINE__, basename);
}

void writeDecompositionTables(const char* basename, const uint16_t* ptr16, size_t len16, const uint32_t* ptr32, size_t len32) {
    FILE* f = prepareOutputFile(basename);
    usrc_writeArray(f, "scalars16 = [\n  ", ptr16, 16, len16, "  ", "\n]\n");
    usrc_writeArray(f, "scalars32 = [\n  ", ptr32, 32, len32, "  ", "\n]\n");
    fclose(f);
}

void writeDecompositionData(const char* basename, uint32_t baseSize16, uint32_t baseSize32, uint32_t supplementSize16, USet* uset, USet* reference, const std::vector<PendingDescriptor>& pendingTrieInsertions, char16_t passthroughCap) {
    IcuToolErrorCode status("icuexportdata: writeDecompositionData");
    FILE* f = prepareOutputFile(basename);

    // Zero is a magic number that means the character decomposes to itself.
    LocalUMutableCPTriePointer builder(umutablecptrie_open(0, 0, status));

    // Iterate backwards to insert lower code points in the trie first in case it matters
    // for trie block allocation.
    for (int32_t i = pendingTrieInsertions.size() - 1; i >= 0; --i) {
        const PendingDescriptor& pending = pendingTrieInsertions[i];
        uint32_t additional = 0;
        if (!(pending.descriptor & 0xFFFC0000)) {
            uint32_t offset = pending.descriptor & 0xFFF;
            if (!pending.supplementary) {
                if (offset >= baseSize16) {
                    // This is a offset to supplementary 16-bit data. We have
                    // 16-bit base data and 32-bit base data before. However,
                    // the 16-bit base data length is already part of offset.
                    additional = baseSize32;
                }
            } else {
                if (offset >= baseSize32) {
                    // This is an offset to supplementary 32-bit data. We have 16-bit
                    // base data, 32-bit base data, and 16-bit supplementary data before.
                    // However, the 32-bit base data length is already part
                    // of offset.
                    additional = baseSize16 + supplementSize16;
                } else {
                    // This is an offset to 32-bit base data. We have 16-bit
                    // base data before.
                    additional = baseSize16;
                }
            }
            if (offset + additional > 0xFFF) {
                status.set(U_INTERNAL_PROGRAM_ERROR);
                handleError(status, __LINE__, basename);
            }
        }
        // It turns out it's better to swap the halves compared to the initial
        // idea in order to put special marker values close to zero so that
        // an important marker value becomes 1, so it's efficient to compare
        // "1 or 0". Unfortunately, going through all the code to swap
        // things is too error prone, so let's do the swapping here in one
        // place.
        uint32_t oldTrieValue = pending.descriptor + additional;
        uint32_t swappedTrieValue = (oldTrieValue >> 16) | (oldTrieValue << 16);
        umutablecptrie_set(builder.getAlias(), pending.scalar, swappedTrieValue, status);
    }
    LocalUCPTriePointer utrie(umutablecptrie_buildImmutable(
        builder.getAlias(),
        trieType,
        UCPTRIE_VALUE_BITS_32,
        status));
    handleError(status, __LINE__, basename);

    if (reference) {
        if (uset_contains(reference, 0xFF9E) || uset_contains(reference, 0xFF9F) || !uset_contains(reference, 0x0345)) {
            // NFD expectations don't hold. The set must not contain the half-width
            // kana voicing marks and must contain iota subscript.
            status.set(U_INTERNAL_PROGRAM_ERROR);
            handleError(status, __LINE__, basename);
        }

        USet* halfWidthVoicing = uset_openEmpty();
        uset_add(halfWidthVoicing, 0xFF9E);
        uset_add(halfWidthVoicing, 0xFF9F);

        USet* iotaSubscript = uset_openEmpty();
        uset_add(iotaSubscript, 0x0345);

        uint8_t flags = 0;

        USet* halfWidthCheck = uset_cloneAsThawed(uset);
        uset_removeAll(halfWidthCheck, reference);
        if (uset_equals(halfWidthCheck, halfWidthVoicing)) {
            flags |= 1;
        } else if (!uset_isEmpty(halfWidthCheck)) {
            // The result was neither empty nor contained exactly
            // the two half-width voicing marks. The ICU4X
            // normalizer doesn't know how to deal with this case.
            status.set(U_INTERNAL_PROGRAM_ERROR);
            handleError(status, __LINE__, basename);
        }
        uset_close(halfWidthCheck);

        USet* iotaCheck = uset_cloneAsThawed(reference);
        uset_removeAll(iotaCheck, uset);
        if (!(uset_equals(iotaCheck, iotaSubscript)) && !uset_isEmpty(iotaCheck)) {
            // The result was neither empty nor contained exactly
            // the iota subscript. The ICU4X normalizer doesn't
            // know how to deal with this case.
            status.set(U_INTERNAL_PROGRAM_ERROR);
            handleError(status, __LINE__, basename);
        }

        uset_close(iotaSubscript);
        uset_close(halfWidthVoicing);

        fprintf(f, "flags = 0x%X\n", flags);
        fprintf(f, "cap = 0x%X\n", passthroughCap);
    }
    fprintf(f, "[trie]\n");
    usrc_writeUCPTrie(f, "trie", utrie.getAlias(), UPRV_TARGET_SYNTAX_TOML);
    fclose(f);
    handleError(status, __LINE__, basename);
}

// Special marker for the NFKD form of U+FDFA
const int32_t FDFA_MARKER = 3;

// Special marker for characters whose decomposition starts with a non-starter
// and the decomposition isn't the character itself.
const int32_t SPECIAL_NON_STARTER_DECOMPOSITION_MARKER = 2;

// Special marker for starters that decompose to themselves but that may
// combine backwards under canonical composition
const int32_t BACKWARD_COMBINING_STARTER_MARKER = 1;

/// Marker that a complex decomposition isn't round-trippable
/// under re-composition.
///
/// TODO: When taking a data format break, swap this around with
/// `BACKWARD_COMBINING_STARTER_DECOMPOSITION_MARKER`.
const uint32_t NON_ROUND_TRIP_MARKER = 1;

/// Marker that a complex decomposition starts with a starter
/// that can combine backwards.
///
/// TODO: When taking a data format break, swap this around with
/// `NON_ROUND_TRIP_MARKER` to use the same bit as with characters
/// that decompose to self but can combine backwards.
const uint32_t BACKWARD_COMBINING_STARTER_DECOMPOSITION_MARKER = 2;

UBool permissibleBmpPair(UBool knownToRoundTrip, UChar32 c, UChar32 second) {
    if (knownToRoundTrip) {
        return true;
    }
    // Nuktas, Hebrew presentation forms and polytonic Greek with oxia
    // are special-cased in ICU4X.
    if (c >= 0xFB1D && c <= 0xFB4E) {
        // Hebrew presentation forms
        return true;
    }
    if (c >= 0x1F71 && c <= 0x1FFB) {
        // Polytonic Greek with oxia
        return true;
    }
    if ((second & 0x7F) == 0x3C && second >= 0x0900 && second <= 0x0BFF) {
        // Nukta
        return true;
    }
    // To avoid more branchiness, 4 characters that decompose to
    // a BMP starter followed by a BMP non-starter are excluded
    // from being encoded directly into the trie value and are
    // handled as complex decompositions instead. These are:
    // U+0F76 TIBETAN VOWEL SIGN VOCALIC R
    // U+0F78 TIBETAN VOWEL SIGN VOCALIC L
    // U+212B ANGSTROM SIGN
    // U+2ADC FORKING
    return false;
}


// Find the slice `needle` within `storage` and return its index, failing which,
// append all elements of `needle` to `storage` and return the index of it at the end.
template<typename T>
size_t findOrAppend(std::vector<T>& storage, const UChar32* needle, size_t needleLen) {
    // Last index where we might find the start of the complete needle.
    // bounds check is `i + needleLen <= storage.size()` since the inner
    // loop will range from `i` to `i + needleLen - 1` (the `-1` is why we use `<=`)
    for (size_t i = 0; i + needleLen <= storage.size(); i++) {
        for (size_t j = 0;; j++) {
            if (j == needleLen) {
                return i;  // found a match
            }
            if (storage[i + j] != static_cast<uint32_t>(needle[j])) {
                break;
            }
        }
    }
    // We didn't find anything. Append, keeping the append index in mind.
    size_t index = storage.size();
    for(size_t i = 0; i < needleLen; i++) {
        storage.push_back(static_cast<T>(needle[i]));
    }

    return index;
}


// Computes data for canonical decompositions
void computeDecompositions(const char* basename,
                           const USet* backwardCombiningStarters,
                           std::vector<uint16_t>& storage16,
                           std::vector<uint32_t>& storage32,
                           USet* decompositionStartsWithNonStarter,
                           USet* decompositionStartsWithBackwardCombiningStarter,
                           std::vector<PendingDescriptor>& pendingTrieInsertions,
                           UChar32& decompositionPassthroughBound,
                           UChar32& compositionPassthroughBound) {
    IcuToolErrorCode status("icuexportdata: computeDecompositions");
    const Normalizer2* mainNormalizer;
    const Normalizer2* nfdNormalizer = Normalizer2::getNFDInstance(status);
    const Normalizer2* nfcNormalizer = Normalizer2::getNFCInstance(status);
    FILE* f = nullptr;
    std::vector<uint32_t> nonRecursive32;
    LocalUMutableCPTriePointer nonRecursiveBuilder(umutablecptrie_open(0, 0, status));

    UBool uts46 = false;

    if (uprv_strcmp(basename, "nfkd") == 0) {
        mainNormalizer = Normalizer2::getNFKDInstance(status);
    } else if (uprv_strcmp(basename, "uts46d") == 0) {
        uts46 = true;
        mainNormalizer = Normalizer2::getInstance(nullptr, "uts46", UNORM2_COMPOSE, status);
    } else {
        mainNormalizer = nfdNormalizer;
        f = prepareOutputFile("decompositionex");
    }

    // Max length as of Unicode 14 is 4 for NFD. For NFKD the max
    // is 18 (U+FDFA; special-cased), and the next longest is 8 (U+FDFB).
    const int32_t LONGEST_ENCODABLE_LENGTH_16 = 9;
    const int32_t LONGEST_ENCODABLE_LENGTH_32 = 8;
    const int32_t DECOMPOSITION_BUFFER_SIZE = 20;
    UChar32 utf32[DECOMPOSITION_BUFFER_SIZE];
    const int32_t RAW_DECOMPOSITION_BUFFER_SIZE = 2;
    UChar32 rawUtf32[RAW_DECOMPOSITION_BUFFER_SIZE];

    // Iterate over all scalar values excluding Hangul syllables.
    //
    // We go backwards in order to better find overlapping decompositions.
    //
    // As of Unicode 14:
    // Iterate forward without overlap search:
    // nfd: 16 size: 896, 32 size: 173
    // nfkd: 16 size: 3854, 32 size: 179
    //
    // Iterate forward with overlap search:
    // nfd: 16 size: 888, 32 size: 173
    // nfkd: 16 size: 3266, 32 size: 179
    //
    // Iterate backward with overlap search:
    // nfd: 16 size: 776, 32 size: 173
    // nfkd: 16 size: 2941, 32 size: 179
    //
    // UChar32 is signed!
    for (UChar32 c = 0x10FFFF; c >= 0; --c) {
        if (c >= 0xAC00 && c <= 0xD7A3) {
            // Hangul syllable
            continue;
        }
        if (c >= 0xD800 && c < 0xE000) {
            // Surrogate
            continue;
        }
        UnicodeString src;
        UnicodeString dst;
        // True if we're building non-NFD or we're building NFD but
        // the `c` round trips to NFC.
        // False if we're building NFD and `c` does not round trip to NFC.
        UBool nonNfdOrRoundTrips = true;
        src.append(c);
        if (mainNormalizer != nfdNormalizer) {
            UnicodeString inter;
            mainNormalizer->normalize(src, inter, status);
            nfdNormalizer->normalize(inter, dst, status);
        } else {
            nfdNormalizer->normalize(src, dst, status);
            UnicodeString nfc;
            nfcNormalizer->normalize(dst, nfc, status);
            nonNfdOrRoundTrips = (src == nfc);
        }
        if (uts46) {
            // Work around https://unicode-org.atlassian.net/browse/ICU-22658
            // TODO: Remove the workaround after data corresponding to
            // https://www.unicode.org/L2/L2024/24061.htm#179-C36 lands
            // for Unicode 16.
            switch (c) {
                case 0x2F868:
                    dst.truncate(0);
                    dst.append(static_cast<UChar32>(0x36FC));
                    break;
                case 0x2F874:
                    dst.truncate(0);
                    dst.append(static_cast<UChar32>(0x5F53));
                    break;
                case 0x2F91F:
                    dst.truncate(0);
                    dst.append(static_cast<UChar32>(0x243AB));
                    break;
                case 0x2F95F:
                    dst.truncate(0);
                    dst.append(static_cast<UChar32>(0x7AEE));
                    break;
                case 0x2F9BF:
                    dst.truncate(0);
                    dst.append(static_cast<UChar32>(0x45D7));
                    break;
            }
        }

        int32_t len = dst.toUTF32(utf32, DECOMPOSITION_BUFFER_SIZE, status);

        if (!len || (len == 1 && utf32[0] == 0xFFFD && c != 0xFFFD)) {
            if (!uts46) {
                status.set(U_INTERNAL_PROGRAM_ERROR);
                handleError(status, __LINE__, basename);
            }
        }
        if (len > DECOMPOSITION_BUFFER_SIZE) {
            status.set(U_INTERNAL_PROGRAM_ERROR);
            handleError(status, __LINE__, basename);
        }
        uint8_t firstCombiningClass = u_getCombiningClass(utf32[0]);
        bool specialNonStarterDecomposition = false;
        bool startsWithBackwardCombiningStarter = false;
        if (firstCombiningClass) {
            decompositionPassthroughBound = c;
            compositionPassthroughBound = c;
            uset_add(decompositionStartsWithNonStarter, c);
            if (src != dst) {
                if (c == 0x0340 || c == 0x0341 || c == 0x0343 || c == 0x0344 || c == 0x0F73 || c == 0x0F75 || c == 0x0F81 || c == 0xFF9E || c == 0xFF9F) {
                    specialNonStarterDecomposition = true;
                } else {
                    // A character whose decomposition starts with a non-starter and isn't the same as the character itself and isn't already hard-coded into ICU4X.
                    status.set(U_INTERNAL_PROGRAM_ERROR);
                    handleError(status, __LINE__, basename);
                }
            }
        } else if (uset_contains(backwardCombiningStarters, utf32[0])) {
            compositionPassthroughBound = c;
            startsWithBackwardCombiningStarter = true;
            uset_add(decompositionStartsWithBackwardCombiningStarter, c);
        }
        if (c != BACKWARD_COMBINING_STARTER_MARKER && len == 1 && utf32[0] == BACKWARD_COMBINING_STARTER_MARKER) {
            status.set(U_INTERNAL_PROGRAM_ERROR);
            handleError(status, __LINE__, basename);
        }
        if (c != SPECIAL_NON_STARTER_DECOMPOSITION_MARKER && len == 1 && utf32[0] == SPECIAL_NON_STARTER_DECOMPOSITION_MARKER) {
            status.set(U_INTERNAL_PROGRAM_ERROR);
            handleError(status, __LINE__, basename);
        }
        if (c != FDFA_MARKER && len == 1 && utf32[0] == FDFA_MARKER) {
            status.set(U_INTERNAL_PROGRAM_ERROR);
            handleError(status, __LINE__, basename);
        }
        if (mainNormalizer != nfdNormalizer) {
            UnicodeString nfd;
            nfdNormalizer->normalize(src, nfd, status);
            if (dst == nfd) {
                continue;
            }
            decompositionPassthroughBound = c;
            compositionPassthroughBound = c;
        } else if (firstCombiningClass) {
            len = 1;
            if (specialNonStarterDecomposition) {
                utf32[0] = SPECIAL_NON_STARTER_DECOMPOSITION_MARKER; // magic value
            } else {
                // Use the surrogate range to store the canonical combining class
                utf32[0] = 0xD800 | static_cast<UChar32>(firstCombiningClass);
            }
        } else {
            if (src == dst) {
                if (startsWithBackwardCombiningStarter) {
                    pendingTrieInsertions.push_back({c, BACKWARD_COMBINING_STARTER_MARKER << 16, false});
                }
                continue;
            }
            decompositionPassthroughBound = c;
            // ICU4X hard-codes ANGSTROM SIGN
            if (c != 0x212B) {
                UnicodeString raw;
                if (!nfdNormalizer->getRawDecomposition(c, raw)) {
                    // We're always supposed to have a non-recursive decomposition
                    // if we had a recursive one.
                    status.set(U_INTERNAL_PROGRAM_ERROR);
                    handleError(status, __LINE__, basename);
                }
                // In addition to actual difference, put the whole range that contains characters
                // with oxia into the non-recursive trie in order to catch cases where characters
                // with oxia have singleton decompositions to corresponding characters with tonos.
                // This way, the run-time decision to fall through can be done on the range
                // without checking for individual characters inside the range.
                if (raw != dst || (c >= 0x1F71 && c <= 0x1FFB)) {
                    int32_t rawLen = raw.toUTF32(rawUtf32, RAW_DECOMPOSITION_BUFFER_SIZE, status);
                    if (!rawLen) {
                        status.set(U_INTERNAL_PROGRAM_ERROR);
                        handleError(status, __LINE__, basename);
                    }
                    if (rawLen == 1) {
                        if (c >= 0xFFFF) {
                            status.set(U_INTERNAL_PROGRAM_ERROR);
                            handleError(status, __LINE__, basename);
                        }
                        umutablecptrie_set(nonRecursiveBuilder.getAlias(), c, static_cast<uint32_t>(rawUtf32[0]), status);
                    } else if (rawUtf32[0] <= 0xFFFF && rawUtf32[1] <= 0xFFFF) {
                        if (!rawUtf32[0] || !rawUtf32[1]) {
                            status.set(U_INTERNAL_PROGRAM_ERROR);
                            handleError(status, __LINE__, basename);
                        }
                        // Swapped for consistency with the primary trie
                        uint32_t bmpPair = static_cast<uint32_t>(rawUtf32[1]) << 16 | static_cast<uint32_t>(rawUtf32[0]);
                        umutablecptrie_set(nonRecursiveBuilder.getAlias(), c, bmpPair, status);
                    } else {
                        // Let's add 1 to index to make it always non-zero to distinguish
                        // it from the default zero.
                        uint32_t index = nonRecursive32.size() + 1;
                        nonRecursive32.push_back(static_cast<uint32_t>(rawUtf32[0]));
                        nonRecursive32.push_back(static_cast<uint32_t>(rawUtf32[1]));
                        if (index > 0xFFFF) {
                            status.set(U_INTERNAL_PROGRAM_ERROR);
                            handleError(status, __LINE__, basename);
                        }
                        umutablecptrie_set(nonRecursiveBuilder.getAlias(), c, index << 16, status);
                    }
                }
            }
        }
        if (!nonNfdOrRoundTrips) {
            compositionPassthroughBound = c;
        }
        if (!len) {
            if (!uts46) {
                status.set(U_INTERNAL_PROGRAM_ERROR);
                handleError(status, __LINE__, basename);
            }
            pendingTrieInsertions.push_back({c, 0xFFFFFFFF, false});
        } else if (len == 1 && ((utf32[0] >= 0x1161 && utf32[0] <= 0x1175) || (utf32[0] >= 0x11A8 && utf32[0] <= 0x11C2))) {
            // Singleton decompositions to conjoining jamo.
            if (mainNormalizer == nfdNormalizer) {
                // Not supposed to happen in NFD
                status.set(U_INTERNAL_PROGRAM_ERROR);
                handleError(status, __LINE__, basename);
            }
            pendingTrieInsertions.push_back({c, static_cast<uint32_t>(utf32[0]) << 16, false});
        } else if (!startsWithBackwardCombiningStarter && len == 1 && utf32[0] <= 0xFFFF) {
            pendingTrieInsertions.push_back({c, static_cast<uint32_t>(utf32[0]) << 16, false});
        } else if (!startsWithBackwardCombiningStarter &&
                   len == 2 &&
                   utf32[0] <= 0xFFFF &&
                   utf32[1] <= 0xFFFF &&
                   !u_getCombiningClass(utf32[0]) &&
                   u_getCombiningClass(utf32[1]) &&
                   permissibleBmpPair(nonNfdOrRoundTrips, c, utf32[1])) {
            for (int32_t i = 0; i < len; ++i) {
                if (((utf32[i] == 0x0345) && (uprv_strcmp(basename, "uts46d") == 0)) || utf32[i] == 0xFF9E || utf32[i] == 0xFF9F) {
                    // Assert that iota subscript and half-width voicing marks never occur in these
                    // expansions in the normalization forms where they are special.
                    status.set(U_INTERNAL_PROGRAM_ERROR);
                    handleError(status, __LINE__, basename);
                }
            }
            pendingTrieInsertions.push_back({c, (static_cast<uint32_t>(utf32[0]) << 16) | static_cast<uint32_t>(utf32[1]), false});
        } else {
            UBool supplementary = false;
            UBool nonInitialStarter = false;
            for (int32_t i = 0; i < len; ++i) {
                if (((utf32[i] == 0x0345) && (uprv_strcmp(basename, "uts46d") == 0)) || utf32[i] == 0xFF9E || utf32[i] == 0xFF9F) {
                    // Assert that iota subscript and half-width voicing marks never occur in these
                    // expansions in the normalization forms where they are special.
                    status.set(U_INTERNAL_PROGRAM_ERROR);
                    handleError(status, __LINE__, basename);
                }

                if (utf32[i] > 0xFFFF) {
                    supplementary = true;
                }
                if (utf32[i] == 0) {
                    status.set(U_INTERNAL_PROGRAM_ERROR);
                    handleError(status, __LINE__, basename);
                }
                if (i != 0 && !u_getCombiningClass(utf32[i])) {
                    nonInitialStarter = true;
                }
            }
            if (len == 1) {
                // The format doesn't allow for length 1 for BMP,
                // so if these ever occur, they need to be promoted
                // to wider storage. As of Unicode 16 alpha, this
                // case does not arise.
                supplementary = true;
            }
            if (!supplementary) {
                if (len > LONGEST_ENCODABLE_LENGTH_16 || !len || len == 1) {
                    if (len == 18 && c == 0xFDFA) {
                        // Special marker for the one character whose decomposition
                        // is too long.
                        pendingTrieInsertions.push_back({c, FDFA_MARKER << 16, supplementary});
                        continue;
                    } else {
                        status.set(U_INTERNAL_PROGRAM_ERROR);
                        handleError(status, __LINE__, basename);
                    }
                }
            } else if (len > LONGEST_ENCODABLE_LENGTH_32 || !len) {
                status.set(U_INTERNAL_PROGRAM_ERROR);
                handleError(status, __LINE__, basename);
            }
            // Complex decomposition
            // Format for 16-bit value:
            // 15..13: length minus two for 16-bit case and length minus one for
            //         the 32-bit case. Length 8 needs to fit in three bits in
            //         the 16-bit case, and this way the value is future-proofed
            //         up to 9 in the 16-bit case. Zero is unused and length one
            //         in the 16-bit case goes directly into the trie.
            //     12: 1 if all trailing characters are guaranteed non-starters,
            //         0 if no guarantees about non-starterness.
            //         Note: The bit choice is this way around to allow for
            //         dynamically falling back to not having this but instead
            //         having one more bit for length by merely choosing
            //         different masks.
            //  11..0: Start offset in storage. The offset is to the logical
            //         sequence of scalars16, scalars32, supplementary_scalars16,
            //         supplementary_scalars32.
            uint32_t descriptor = static_cast<uint32_t>(!nonInitialStarter) << 12;
            if (!supplementary) {
                descriptor |= (static_cast<uint32_t>(len) - 2) << 13;
            } else {
                descriptor |= (static_cast<uint32_t>(len) - 1) << 13;
            }
            if (descriptor & 0xFFF) {
                status.set(U_INTERNAL_PROGRAM_ERROR);
                handleError(status, __LINE__, basename);
            }
            size_t index = 0;
            if (!supplementary) {
                index = findOrAppend(storage16, utf32, len);
            } else {
                index = findOrAppend(storage32, utf32, len);
            }
            if (index > 0xFFF) {
                status.set(U_INTERNAL_PROGRAM_ERROR);
                handleError(status, __LINE__, basename);
            }
            descriptor |= static_cast<uint32_t>(index);
            if (!descriptor || descriptor > 0xFFFF) {
                // > 0xFFFF should never happen if the code above is correct.
                // == 0 should not happen due to the nature of the data.
                status.set(U_INTERNAL_PROGRAM_ERROR);
                handleError(status, __LINE__, basename);
            }
            uint32_t nonRoundTripMarker = 0;
            if (!nonNfdOrRoundTrips) {
                nonRoundTripMarker = (NON_ROUND_TRIP_MARKER << 16);
            }
            uint32_t canCombineBackwardsMarker = 0;
            if (startsWithBackwardCombiningStarter) {
                canCombineBackwardsMarker = (BACKWARD_COMBINING_STARTER_DECOMPOSITION_MARKER << 16);
            }
            pendingTrieInsertions.push_back({c, descriptor | nonRoundTripMarker | canCombineBackwardsMarker, supplementary});
        }
    }
    if (storage16.size() + storage32.size() > 0xFFF) {
        status.set(U_INTERNAL_PROGRAM_ERROR);
    }
    if (f) {
        usrc_writeArray(f, "scalars32 = [\n  ", nonRecursive32.data(), 32, nonRecursive32.size(), "  ", "\n]\n");

        LocalUCPTriePointer utrie(umutablecptrie_buildImmutable(
            nonRecursiveBuilder.getAlias(),
            trieType,
            UCPTRIE_VALUE_BITS_32,
            status));
        handleError(status, __LINE__, basename);

        fprintf(f, "[trie]\n");
        usrc_writeUCPTrie(f, "trie", utrie.getAlias(), UPRV_TARGET_SYNTAX_TOML);

        fclose(f);
    }
    handleError(status, __LINE__, basename);
}

#endif // !UCONFIG_NO_NORMALIZATION

enum {
    OPT_HELP_H,
    OPT_HELP_QUESTION_MARK,
    OPT_MODE,
    OPT_TRIE_TYPE,
    OPT_VERSION,
    OPT_DESTDIR,
    OPT_ALL,
    OPT_INDEX,
    OPT_COPYRIGHT,
    OPT_VERBOSE,
    OPT_QUIET,

    OPT_COUNT
};

#define UOPTION_MODE UOPTION_DEF("mode", 'm', UOPT_REQUIRES_ARG)
#define UOPTION_TRIE_TYPE UOPTION_DEF("trie-type", '\1', UOPT_REQUIRES_ARG)
#define UOPTION_ALL UOPTION_DEF("all", '\1', UOPT_NO_ARG)
#define UOPTION_INDEX UOPTION_DEF("index", '\1', UOPT_NO_ARG)

static UOption options[]={
    UOPTION_HELP_H,
    UOPTION_HELP_QUESTION_MARK,
    UOPTION_MODE,
    UOPTION_TRIE_TYPE,
    UOPTION_VERSION,
    UOPTION_DESTDIR,
    UOPTION_ALL,
    UOPTION_INDEX,
    UOPTION_COPYRIGHT,
    UOPTION_VERBOSE,
    UOPTION_QUIET,
};

void printHelp(FILE* stdfile, const char* program) {
  fprintf(stdfile,
          "usage: %s -m mode [-options] [--all | properties...]\n"
          "\tdump Unicode property data to .toml files\n"
          "options:\n"
          "\t-h or -? or --help  this usage text\n"
          "\t-V or --version     show a version message\n"
          "\t-m or --mode        mode: currently only 'uprops', 'ucase', and 'norm', but more may be added\n"
          "\t      --trie-type   set the trie type (small or fast, default small)\n"
          "\t-d or --destdir     destination directory, followed by the path\n"
          "\t      --all         write out all properties known to icuexportdata\n"
          "\t      --index       write an _index.toml summarizing all data exported\n"
          "\t-c or --copyright   include a copyright notice\n"
          "\t-v or --verbose     Turn on verbose output\n"
          "\t-q or --quiet       do not display warnings and progress\n",
          program);
}

int exportUprops(int argc, char* argv[]) {
    // Load list of Unicode properties
    std::vector<const char*> propNames;
    for (int i=1; i<argc; i++) {
        propNames.push_back(argv[i]);
    }
    if (options[OPT_ALL].doesOccur) {
        int i = UCHAR_BINARY_START;
        while (true) {
            if (i == UCHAR_BINARY_LIMIT) {
                i = UCHAR_INT_START;
            }
            if (i == UCHAR_INT_LIMIT) {
                i = UCHAR_GENERAL_CATEGORY_MASK;
            }
            if (i == UCHAR_GENERAL_CATEGORY_MASK + 1) {
                i = UCHAR_BIDI_MIRRORING_GLYPH;
            }
            if (i == UCHAR_BIDI_MIRRORING_GLYPH + 1) {
                i = UCHAR_SCRIPT_EXTENSIONS;
            }
            if (i == UCHAR_SCRIPT_EXTENSIONS + 1) {
                break;
            }
            UProperty uprop = static_cast<UProperty>(i);
            const char* propName = u_getPropertyName(uprop, U_SHORT_PROPERTY_NAME);
            if (propName == nullptr) {
                propName = u_getPropertyName(uprop, U_LONG_PROPERTY_NAME);
                if (propName != nullptr && VERBOSE) {
                    std::cerr << "Note: falling back to long name for: " << propName << std::endl;
                }
            }
            if (propName != nullptr) {
                propNames.push_back(propName);
            } else {
                std::cerr << "Warning: Could not find name for: " << uprop << std::endl;
            }
            i++;
        }
    }

    if (propNames.empty()
            || options[OPT_HELP_H].doesOccur
            || options[OPT_HELP_QUESTION_MARK].doesOccur
            || !options[OPT_MODE].doesOccur) {
        FILE *stdfile=argc<0 ? stderr : stdout;
        fprintf(stdfile,
            "usage: %s -m uprops [-options] [--all | properties...]\n"
            "\tdump Unicode property data to .toml files\n"
            "options:\n"
            "\t-h or -? or --help  this usage text\n"
            "\t-V or --version     show a version message\n"
            "\t-m or --mode        mode: currently only 'uprops', but more may be added\n"
            "\t      --trie-type   set the trie type (small or fast, default small)\n"
            "\t-d or --destdir     destination directory, followed by the path\n"
            "\t      --all         write out all properties known to icuexportdata\n"
            "\t      --index       write an _index.toml summarizing all data exported\n"
            "\t-c or --copyright   include a copyright notice\n"
            "\t-v or --verbose     Turn on verbose output\n"
            "\t-q or --quiet       do not display warnings and progress\n",
            argv[0]);
        return argc<0 ? U_ILLEGAL_ARGUMENT_ERROR : U_ZERO_ERROR;
    }

    const char* mode = options[OPT_MODE].value;
    if (uprv_strcmp(mode, "uprops") != 0) {
        fprintf(stderr, "Invalid option for --mode (must be uprops)\n");
        return U_ILLEGAL_ARGUMENT_ERROR;
    }

    if (options[OPT_TRIE_TYPE].doesOccur) {
        if (uprv_strcmp(options[OPT_TRIE_TYPE].value, "fast") == 0) {
            trieType = UCPTRIE_TYPE_FAST;
        } else if (uprv_strcmp(options[OPT_TRIE_TYPE].value, "small") == 0) {
            trieType = UCPTRIE_TYPE_SMALL;
        } else {
            fprintf(stderr, "Invalid option for --trie-type (must be small or fast)\n");
            return U_ILLEGAL_ARGUMENT_ERROR;
        }
    }

    for (const char* propName : propNames) {
        UProperty propEnum = u_getPropertyEnum(propName);
        if (propEnum == UCHAR_INVALID_CODE) {
            std::cerr << "Error: Invalid property alias: " << propName << std::endl;
            return U_ILLEGAL_ARGUMENT_ERROR;
        }

        FILE* f = prepareOutputFile(propName);

        UVersionInfo versionInfo;
        u_getUnicodeVersion(versionInfo);
        char uvbuf[U_MAX_VERSION_STRING_LENGTH];
        u_versionToString(versionInfo, uvbuf);
        fprintf(f, "icu_version = \"%s\"\nunicode_version = \"%s\"\n\n",
            U_ICU_VERSION,
            uvbuf);

        if (propEnum < UCHAR_BINARY_LIMIT) {
            dumpBinaryProperty(propEnum, f);
        } else if (UCHAR_INT_START <= propEnum && propEnum <= UCHAR_INT_LIMIT) {
            dumpEnumeratedProperty(propEnum, f);
        } else if (propEnum == UCHAR_GENERAL_CATEGORY_MASK) {
            dumpGeneralCategoryMask(f);
        } else if (propEnum == UCHAR_BIDI_MIRRORING_GLYPH) {
            dumpBidiMirroringGlyph(f);
        } else if (propEnum == UCHAR_SCRIPT_EXTENSIONS) {
            dumpScriptExtensions(f);
        } else {
            std::cerr << "Don't know how to write property: " << propEnum << std::endl;
            return U_INTERNAL_PROGRAM_ERROR;
        }

        fclose(f);
    }

    if (options[OPT_INDEX].doesOccur) {
        FILE* f = prepareOutputFile("_index");
        fprintf(f, "index = [\n");
        for (const char* propName : propNames) {
            // At this point, propName is a valid property name, so it should be alphanum ASCII
            fprintf(f, "  { filename=\"%s.toml\" },\n", propName);
        }
        fprintf(f, "]\n");
        fclose(f);
    }

    return 0;
}

struct AddRangeHelper {
    UMutableCPTrie* ucptrie;
};

static UBool U_CALLCONV
addRangeToUCPTrie(const void* context, UChar32 start, UChar32 end, uint32_t value) {
    IcuToolErrorCode status("addRangeToUCPTrie");
    UMutableCPTrie* ucptrie = static_cast<const AddRangeHelper*>(context)->ucptrie;
    umutablecptrie_setRange(ucptrie, start, end, value, status);
    handleError(status, __LINE__, "setRange");

    return true;
}

int exportCase(int argc, char* argv[]) {
    if (argc > 1) {
        fprintf(stderr, "ucase mode does not expect additional arguments\n");
        return U_ILLEGAL_ARGUMENT_ERROR;
    }
    (void) argv; // Suppress unused variable warning

    IcuToolErrorCode status("icuexportdata");
    LocalUMutableCPTriePointer builder(umutablecptrie_open(0, 0, status));
    handleError(status, __LINE__, "exportCase");

    int32_t exceptionsLength, unfoldLength;
    const UCaseProps *caseProps = ucase_getSingleton(&exceptionsLength, &unfoldLength);
    const UTrie2* caseTrie = &caseProps->trie;

    AddRangeHelper helper = { builder.getAlias() };
    utrie2_enum(caseTrie, nullptr, addRangeToUCPTrie, &helper);

    UCPTrieValueWidth width = UCPTRIE_VALUE_BITS_16;
    LocalUCPTriePointer utrie(umutablecptrie_buildImmutable(
        builder.getAlias(),
        trieType,
        width,
        status));
    handleError(status, __LINE__, "exportCase");

    FILE* f = prepareOutputFile("ucase");

    UVersionInfo versionInfo;
    u_getUnicodeVersion(versionInfo);
    char uvbuf[U_MAX_VERSION_STRING_LENGTH];
    u_versionToString(versionInfo, uvbuf);
    fprintf(f, "icu_version = \"%s\"\nunicode_version = \"%s\"\n\n",
            U_ICU_VERSION,
            uvbuf);

    fputs("[ucase.code_point_trie]\n", f);
    usrc_writeUCPTrie(f, "case_trie", utrie.getAlias(), UPRV_TARGET_SYNTAX_TOML);
    fputs("\n", f);

    const char* indent = "  ";
    const char* suffix = "\n]\n";

    fputs("[ucase.exceptions]\n", f);
    const char* exceptionsPrefix = "exceptions = [\n  ";
    int32_t exceptionsWidth = 16;
    usrc_writeArray(f, exceptionsPrefix, caseProps->exceptions, exceptionsWidth,
                    exceptionsLength, indent, suffix);
    fputs("\n", f);

    fputs("[ucase.unfold]\n", f);
    const char* unfoldPrefix = "unfold = [\n  ";
    int32_t unfoldWidth = 16;
    usrc_writeArray(f, unfoldPrefix, caseProps->unfold, unfoldWidth,
                    unfoldLength, indent, suffix);

    return 0;
}

#if !UCONFIG_NO_NORMALIZATION

int exportNorm() {
    IcuToolErrorCode status("icuexportdata: exportNorm");
    USet* backwardCombiningStarters = uset_openEmpty();
    writeCanonicalCompositions(backwardCombiningStarters);

    std::vector<uint16_t> storage16;
    std::vector<uint32_t> storage32;

    // Note: the USets are not exported. They are only used to check that a new
    // Unicode version doesn't violate expectations that are hard-coded in ICU4X.
    USet* nfdDecompositionStartsWithNonStarter = uset_openEmpty();
    USet* nfdDecompositionStartsWithBackwardCombiningStarter = uset_openEmpty();
    std::vector<PendingDescriptor> nfdPendingTrieInsertions;
    UChar32 nfdBound = 0x10FFFF;
    UChar32 nfcBound = 0x10FFFF;
    computeDecompositions("nfd",
                          backwardCombiningStarters,
                          storage16,
                          storage32,
                          nfdDecompositionStartsWithNonStarter,
                          nfdDecompositionStartsWithBackwardCombiningStarter,
                          nfdPendingTrieInsertions,
                          nfdBound,
                          nfcBound);
    if (!(nfdBound == 0xC0 && nfcBound == 0x300)) {
        // Unexpected bounds for NFD/NFC.
        status.set(U_INTERNAL_PROGRAM_ERROR);
        handleError(status, __LINE__, "exportNorm");
    }

    uint32_t baseSize16 = storage16.size();
    uint32_t baseSize32 = storage32.size();

    USet* nfkdDecompositionStartsWithNonStarter = uset_openEmpty();
    USet* nfkdDecompositionStartsWithBackwardCombiningStarter = uset_openEmpty();
    std::vector<PendingDescriptor> nfkdPendingTrieInsertions;
    UChar32 nfkdBound = 0x10FFFF;
    UChar32 nfkcBound = 0x10FFFF;
    computeDecompositions("nfkd",
                          backwardCombiningStarters,
                          storage16,
                          storage32,
                          nfkdDecompositionStartsWithNonStarter,
                          nfkdDecompositionStartsWithBackwardCombiningStarter,
                          nfkdPendingTrieInsertions,
                          nfkdBound,
                          nfkcBound);
    if (!(nfkdBound <= 0xC0 && nfkcBound <= 0x300)) {
        status.set(U_INTERNAL_PROGRAM_ERROR);
        handleError(status, __LINE__, "exportNorm");
    }
    if (nfkcBound > 0xC0) {
        if (nfkdBound != 0xC0) {
            status.set(U_INTERNAL_PROGRAM_ERROR);
            handleError(status, __LINE__, "exportNorm");
        }
    } else {
        if (nfkdBound != nfkcBound) {
            status.set(U_INTERNAL_PROGRAM_ERROR);
            handleError(status, __LINE__, "exportNorm");
        }
    }

    USet* uts46DecompositionStartsWithNonStarter = uset_openEmpty();
    USet* uts46DecompositionStartsWithBackwardCombiningStarter = uset_openEmpty();
    std::vector<PendingDescriptor> uts46PendingTrieInsertions;
    UChar32 uts46dBound = 0x10FFFF;
    UChar32 uts46Bound = 0x10FFFF;
    computeDecompositions("uts46d",
                          backwardCombiningStarters,
                          storage16,
                          storage32,
                          uts46DecompositionStartsWithNonStarter,
                          uts46DecompositionStartsWithBackwardCombiningStarter,
                          uts46PendingTrieInsertions,
                          uts46dBound,
                          uts46Bound);
    if (!(uts46dBound <= 0xC0 && uts46Bound <= 0x300)) {
        status.set(U_INTERNAL_PROGRAM_ERROR);
        handleError(status, __LINE__, "exportNorm");
    }
    if (uts46Bound > 0xC0) {
        if (uts46dBound != 0xC0) {
            status.set(U_INTERNAL_PROGRAM_ERROR);
            handleError(status, __LINE__, "exportNorm");
        }
    } else {
        if (uts46dBound != uts46Bound) {
            status.set(U_INTERNAL_PROGRAM_ERROR);
            handleError(status, __LINE__, "exportNorm");
        }
    }

    uint32_t supplementSize16 = storage16.size() - baseSize16;
    uint32_t supplementSize32 = storage32.size() - baseSize32;

    writeDecompositionData("nfd", baseSize16, baseSize32, supplementSize16, nfdDecompositionStartsWithNonStarter, nullptr, nfdPendingTrieInsertions, static_cast<char16_t>(nfcBound));
    writeDecompositionData("nfkd", baseSize16, baseSize32, supplementSize16, nfkdDecompositionStartsWithNonStarter, nfdDecompositionStartsWithNonStarter, nfkdPendingTrieInsertions, static_cast<char16_t>(nfkcBound));
    writeDecompositionData("uts46d", baseSize16, baseSize32, supplementSize16, uts46DecompositionStartsWithNonStarter, nfdDecompositionStartsWithNonStarter, uts46PendingTrieInsertions, static_cast<char16_t>(uts46Bound));

    writeDecompositionTables("nfdex", storage16.data(), baseSize16, storage32.data(), baseSize32);
    writeDecompositionTables("nfkdex", storage16.data() + baseSize16, supplementSize16, storage32.data() + baseSize32, supplementSize32);

    uset_close(nfdDecompositionStartsWithNonStarter);
    uset_close(nfkdDecompositionStartsWithNonStarter);
    uset_close(uts46DecompositionStartsWithNonStarter);

    uset_close(nfdDecompositionStartsWithBackwardCombiningStarter);
    uset_close(nfkdDecompositionStartsWithBackwardCombiningStarter);
    uset_close(uts46DecompositionStartsWithBackwardCombiningStarter);

    uset_close(backwardCombiningStarters);
    handleError(status, __LINE__, "exportNorm");
    return 0;
}

#endif // !UCONFIG_NO_NORMALIZATION

int main(int argc, char* argv[]) {
    U_MAIN_INIT_ARGS(argc, argv);

    /* preset then read command line options */
    options[OPT_DESTDIR].value=u_getDataDirectory();
    argc=u_parseArgs(argc, argv, UPRV_LENGTHOF(options), options);

    if(options[OPT_VERSION].doesOccur) {
        printf("icuexportdata version %s, ICU tool to dump data files for external consumers\n",
               U_ICU_DATA_VERSION);
        printf("%s\n", U_COPYRIGHT_STRING);
        exit(0);
    }

    /* error handling, printing usage message */
    if(argc<0) {
        fprintf(stderr,
            "error in command line argument \"%s\"\n",
            argv[-argc]);
    }

    if (argc < 0
            || options[OPT_HELP_H].doesOccur
            || options[OPT_HELP_QUESTION_MARK].doesOccur
            || !options[OPT_MODE].doesOccur) {
        FILE *stdfile=argc<0 ? stderr : stdout;
        printHelp(stdfile, argv[0]);
        return argc<0 ? U_ILLEGAL_ARGUMENT_ERROR : U_ZERO_ERROR;
    }

    /* get the options values */
    haveCopyright = options[OPT_COPYRIGHT].doesOccur;
    destdir = options[OPT_DESTDIR].value;
    VERBOSE = options[OPT_VERBOSE].doesOccur;
    QUIET = options[OPT_QUIET].doesOccur;

    if (options[OPT_TRIE_TYPE].doesOccur) {
        if (uprv_strcmp(options[OPT_TRIE_TYPE].value, "fast") == 0) {
            trieType = UCPTRIE_TYPE_FAST;
        } else if (uprv_strcmp(options[OPT_TRIE_TYPE].value, "small") == 0) {
            trieType = UCPTRIE_TYPE_SMALL;
        } else {
            fprintf(stderr, "Invalid option for --trie-type (must be small or fast)\n");
            return U_ILLEGAL_ARGUMENT_ERROR;
        }
    }

    const char* mode = options[OPT_MODE].value;
    if (uprv_strcmp(mode, "norm") == 0) {
#if !UCONFIG_NO_NORMALIZATION
        return exportNorm();
#else
    fprintf(stderr, "Exporting normalization data not supported when compiling without normalization support.\n");
    return U_ILLEGAL_ARGUMENT_ERROR;
#endif
    }
    if (uprv_strcmp(mode, "uprops") == 0) {
        return exportUprops(argc, argv);
    } else if (uprv_strcmp(mode, "ucase") == 0) {
        return exportCase(argc, argv);
    }

    fprintf(stderr, "Invalid option for --mode (must be uprops, ucase, or norm)\n");
    return U_ILLEGAL_ARGUMENT_ERROR;
}
