// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
 **********************************************************************
 *   Copyright (C) 1997-2016, International Business Machines
 *   Corporation and others.  All Rights Reserved.
 **********************************************************************
*
* File locid.cpp
*
* Created by: Richard Gillam
*
* Modification History:
*
*   Date        Name        Description
*   02/11/97    aliu        Changed gLocPath to fgDataDirectory and added
*                           methods to get and set it.
*   04/02/97    aliu        Made operator!= inline; fixed return value
*                           of getName().
*   04/15/97    aliu        Cleanup for AIX/Win32.
*   04/24/97    aliu        Numerous changes per code review.
*   08/18/98    stephen     Changed getDisplayName()
*                           Added SIMPLIFIED_CHINESE, TRADITIONAL_CHINESE
*                           Added getISOCountries(), getISOLanguages(),
*                           getLanguagesForCountry()
*   03/16/99    bertrand    rehaul.
*   07/21/99    stephen     Added U_CFUNC setDefault
*   11/09/99    weiv        Added const char * getName() const;
*   04/12/00    srl         removing unicodestring api's and cached hash code
*   08/10/01    grhoten     Change the static Locales to accessor functions
******************************************************************************
*/

#include <utility>

#include "unicode/bytestream.h"
#include "unicode/locid.h"
#include "unicode/localebuilder.h"
#include "unicode/strenum.h"
#include "unicode/stringpiece.h"
#include "unicode/uloc.h"
#include "unicode/ures.h"

#include "bytesinkutil.h"
#include "charstr.h"
#include "charstrmap.h"
#include "cmemory.h"
#include "cstring.h"
#include "mutex.h"
#include "putilimp.h"
#include "uassert.h"
#include "ucln_cmn.h"
#include "uhash.h"
#include "ulocimp.h"
#include "umutex.h"
#include "uniquecharstr.h"
#include "ustr_imp.h"
#include "uvector.h"

U_NAMESPACE_BEGIN

static Locale   *gLocaleCache = nullptr;
static UInitOnce gLocaleCacheInitOnce {};

// gDefaultLocaleMutex protects all access to gDefaultLocalesHashT and gDefaultLocale.
static UMutex gDefaultLocaleMutex;
static UHashtable *gDefaultLocalesHashT = nullptr;
static Locale *gDefaultLocale = nullptr;

/**
 * \def ULOC_STRING_LIMIT
 * strings beyond this value crash in CharString
 */
#define ULOC_STRING_LIMIT 357913941

U_NAMESPACE_END

typedef enum ELocalePos {
    eENGLISH,
    eFRENCH,
    eGERMAN,
    eITALIAN,
    eJAPANESE,
    eKOREAN,
    eCHINESE,

    eFRANCE,
    eGERMANY,
    eITALY,
    eJAPAN,
    eKOREA,
    eCHINA,      /* Alias for PRC */
    eTAIWAN,
    eUK,
    eUS,
    eCANADA,
    eCANADA_FRENCH,
    eROOT,


    //eDEFAULT,
    eMAX_LOCALES
} ELocalePos;

namespace {

//
// Deleter function for Locales owned by the default Locale hash table/
//
void U_CALLCONV
deleteLocale(void *obj) {
    delete (icu::Locale *) obj;
}

UBool U_CALLCONV locale_cleanup()
{
    U_NAMESPACE_USE

    delete [] gLocaleCache;
    gLocaleCache = nullptr;
    gLocaleCacheInitOnce.reset();

    if (gDefaultLocalesHashT) {
        uhash_close(gDefaultLocalesHashT);   // Automatically deletes all elements, using deleter func.
        gDefaultLocalesHashT = nullptr;
    }
    gDefaultLocale = nullptr;
    return true;
}

void U_CALLCONV locale_init(UErrorCode &status) {
    U_NAMESPACE_USE

    U_ASSERT(gLocaleCache == nullptr);
    gLocaleCache = new Locale[(int)eMAX_LOCALES];
    if (gLocaleCache == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    ucln_common_registerCleanup(UCLN_COMMON_LOCALE, locale_cleanup);
    gLocaleCache[eROOT]          = Locale("");
    gLocaleCache[eENGLISH]       = Locale("en");
    gLocaleCache[eFRENCH]        = Locale("fr");
    gLocaleCache[eGERMAN]        = Locale("de");
    gLocaleCache[eITALIAN]       = Locale("it");
    gLocaleCache[eJAPANESE]      = Locale("ja");
    gLocaleCache[eKOREAN]        = Locale("ko");
    gLocaleCache[eCHINESE]       = Locale("zh");
    gLocaleCache[eFRANCE]        = Locale("fr", "FR");
    gLocaleCache[eGERMANY]       = Locale("de", "DE");
    gLocaleCache[eITALY]         = Locale("it", "IT");
    gLocaleCache[eJAPAN]         = Locale("ja", "JP");
    gLocaleCache[eKOREA]         = Locale("ko", "KR");
    gLocaleCache[eCHINA]         = Locale("zh", "CN");
    gLocaleCache[eTAIWAN]        = Locale("zh", "TW");
    gLocaleCache[eUK]            = Locale("en", "GB");
    gLocaleCache[eUS]            = Locale("en", "US");
    gLocaleCache[eCANADA]        = Locale("en", "CA");
    gLocaleCache[eCANADA_FRENCH] = Locale("fr", "CA");
}

}  // namespace

U_NAMESPACE_BEGIN

Locale *locale_set_default_internal(const char *id, UErrorCode& status) {
    // Synchronize this entire function.
    Mutex lock(&gDefaultLocaleMutex);

    UBool canonicalize = false;

    // If given a nullptr string for the locale id, grab the default
    //   name from the system.
    //   (Different from most other locale APIs, where a null name means use
    //    the current ICU default locale.)
    if (id == nullptr) {
        id = uprv_getDefaultLocaleID();   // This function not thread safe? TODO: verify.
        canonicalize = true; // always canonicalize host ID
    }

    CharString localeNameBuf =
        canonicalize ? ulocimp_canonicalize(id, status) : ulocimp_getName(id, status);

    if (U_FAILURE(status)) {
        return gDefaultLocale;
    }

    if (gDefaultLocalesHashT == nullptr) {
        gDefaultLocalesHashT = uhash_open(uhash_hashChars, uhash_compareChars, nullptr, &status);
        if (U_FAILURE(status)) {
            return gDefaultLocale;
        }
        uhash_setValueDeleter(gDefaultLocalesHashT, deleteLocale);
        ucln_common_registerCleanup(UCLN_COMMON_LOCALE, locale_cleanup);
    }

    Locale *newDefault = (Locale *)uhash_get(gDefaultLocalesHashT, localeNameBuf.data());
    if (newDefault == nullptr) {
        newDefault = new Locale(Locale::eBOGUS);
        if (newDefault == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return gDefaultLocale;
        }
        newDefault->init(localeNameBuf.data(), false);
        uhash_put(gDefaultLocalesHashT, (char*) newDefault->getName(), newDefault, &status);
        if (U_FAILURE(status)) {
            return gDefaultLocale;
        }
    }
    gDefaultLocale = newDefault;
    return gDefaultLocale;
}

U_NAMESPACE_END

/* sfb 07/21/99 */
U_CFUNC void
locale_set_default(const char *id)
{
    U_NAMESPACE_USE
    UErrorCode status = U_ZERO_ERROR;
    locale_set_default_internal(id, status);
}
/* end */

U_CFUNC const char *
locale_get_default()
{
    U_NAMESPACE_USE
    return Locale::getDefault().getName();
}


U_NAMESPACE_BEGIN

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(Locale)

/*Character separating the posix id fields*/
// '_'
// In the platform codepage.
#define SEP_CHAR '_'
#define NULL_CHAR '\0'

Locale::~Locale()
{
    if ((baseName != fullName) && (baseName != fullNameBuffer)) {
        uprv_free(baseName);
    }
    baseName = nullptr;
    /*if fullName is on the heap, we free it*/
    if (fullName != fullNameBuffer)
    {
        uprv_free(fullName);
        fullName = nullptr;
    }
}

Locale::Locale()
    : UObject(), fullName(fullNameBuffer), baseName(nullptr)
{
    init(nullptr, false);
}

/*
 * Internal constructor to allow construction of a locale object with
 *   NO side effects.   (Default constructor tries to get
 *   the default locale.)
 */
Locale::Locale(Locale::ELocaleType)
    : UObject(), fullName(fullNameBuffer), baseName(nullptr)
{
    setToBogus();
}


Locale::Locale( const   char * newLanguage,
                const   char * newCountry,
                const   char * newVariant,
                const   char * newKeywords)
    : UObject(), fullName(fullNameBuffer), baseName(nullptr)
{
    if( (newLanguage==nullptr) && (newCountry == nullptr) && (newVariant == nullptr) )
    {
        init(nullptr, false); /* shortcut */
    }
    else
    {
        UErrorCode status = U_ZERO_ERROR;
        int32_t lsize = 0;
        int32_t csize = 0;
        int32_t vsize = 0;
        int32_t ksize = 0;

        // Check the sizes of the input strings.

        // Language
        if ( newLanguage != nullptr )
        {
            lsize = (int32_t)uprv_strlen(newLanguage);
            if ( lsize < 0 || lsize > ULOC_STRING_LIMIT ) { // int32 wrap
                setToBogus();
                return;
            }
        }

        CharString togo(newLanguage, lsize, status); // start with newLanguage

        // _Country
        if ( newCountry != nullptr )
        {
            csize = (int32_t)uprv_strlen(newCountry);
            if ( csize < 0 || csize > ULOC_STRING_LIMIT ) { // int32 wrap
                setToBogus();
                return;
            }
        }

        // _Variant
        if ( newVariant != nullptr )
        {
            // remove leading _'s
            while(newVariant[0] == SEP_CHAR)
            {
                newVariant++;
            }

            // remove trailing _'s
            vsize = (int32_t)uprv_strlen(newVariant);
            if ( vsize < 0 || vsize > ULOC_STRING_LIMIT ) { // int32 wrap
                setToBogus();
                return;
            }
            while( (vsize>1) && (newVariant[vsize-1] == SEP_CHAR) )
            {
                vsize--;
            }
        }

        if ( newKeywords != nullptr)
        {
            ksize = (int32_t)uprv_strlen(newKeywords);
            if ( ksize < 0 || ksize > ULOC_STRING_LIMIT ) {
              setToBogus();
              return;
            }
        }

        // We've checked the input sizes, now build up the full locale string..

        // newLanguage is already copied

        if ( ( vsize != 0 ) || (csize != 0) )  // at least:  __v
        {                                      //            ^
            togo.append(SEP_CHAR, status);
        }

        if ( csize != 0 )
        {
            togo.append(newCountry, status);
        }

        if ( vsize != 0)
        {
            togo.append(SEP_CHAR, status)
                .append(newVariant, vsize, status);
        }

        if ( ksize != 0)
        {
            if (uprv_strchr(newKeywords, '=')) {
                togo.append('@', status); /* keyword parsing */
            }
            else {
                togo.append('_', status); /* Variant parsing with a script */
                if ( vsize == 0) {
                    togo.append('_', status); /* No country found */
                }
            }
            togo.append(newKeywords, status);
        }

        if (U_FAILURE(status)) {
            // Something went wrong with appending, etc.
            setToBogus();
            return;
        }
        // Parse it, because for example 'language' might really be a complete
        // string.
        init(togo.data(), false);
    }
}

Locale::Locale(const Locale &other)
    : UObject(other), fullName(fullNameBuffer), baseName(nullptr)
{
    *this = other;
}

Locale::Locale(Locale&& other) noexcept
    : UObject(other), fullName(fullNameBuffer), baseName(fullName) {
  *this = std::move(other);
}

Locale& Locale::operator=(const Locale& other) {
    if (this == &other) {
        return *this;
    }

    setToBogus();

    if (other.fullName == other.fullNameBuffer) {
        uprv_strcpy(fullNameBuffer, other.fullNameBuffer);
    } else if (other.fullName == nullptr) {
        fullName = nullptr;
    } else {
        fullName = uprv_strdup(other.fullName);
        if (fullName == nullptr) return *this;
    }

    if (other.baseName == other.fullName) {
        baseName = fullName;
    } else if (other.baseName != nullptr) {
        baseName = uprv_strdup(other.baseName);
        if (baseName == nullptr) return *this;
    }

    uprv_strcpy(language, other.language);
    uprv_strcpy(script, other.script);
    uprv_strcpy(country, other.country);

    variantBegin = other.variantBegin;
    fIsBogus = other.fIsBogus;

    return *this;
}

Locale& Locale::operator=(Locale&& other) noexcept {
    if ((baseName != fullName) && (baseName != fullNameBuffer)) uprv_free(baseName);
    if (fullName != fullNameBuffer) uprv_free(fullName);

    if (other.fullName == other.fullNameBuffer || other.baseName == other.fullNameBuffer) {
        uprv_strcpy(fullNameBuffer, other.fullNameBuffer);
    }
    if (other.fullName == other.fullNameBuffer) {
        fullName = fullNameBuffer;
    } else {
        fullName = other.fullName;
    }

    if (other.baseName == other.fullNameBuffer) {
        baseName = fullNameBuffer;
    } else if (other.baseName == other.fullName) {
        baseName = fullName;
    } else {
        baseName = other.baseName;
    }

    uprv_strcpy(language, other.language);
    uprv_strcpy(script, other.script);
    uprv_strcpy(country, other.country);

    variantBegin = other.variantBegin;
    fIsBogus = other.fIsBogus;

    other.baseName = other.fullName = other.fullNameBuffer;

    return *this;
}

Locale *
Locale::clone() const {
    return new Locale(*this);
}

bool
Locale::operator==( const   Locale& other) const
{
    return (uprv_strcmp(other.fullName, fullName) == 0);
}

namespace {

UInitOnce gKnownCanonicalizedInitOnce {};
UHashtable *gKnownCanonicalized = nullptr;

constexpr const char* KNOWN_CANONICALIZED[] = {
    "c",
    // Commonly used locales known are already canonicalized
    "af", "af_ZA", "am", "am_ET", "ar", "ar_001", "as", "as_IN", "az", "az_AZ",
    "be", "be_BY", "bg", "bg_BG", "bn", "bn_IN", "bs", "bs_BA", "ca", "ca_ES",
    "cs", "cs_CZ", "cy", "cy_GB", "da", "da_DK", "de", "de_DE", "el", "el_GR",
    "en", "en_GB", "en_US", "es", "es_419", "es_ES", "et", "et_EE", "eu",
    "eu_ES", "fa", "fa_IR", "fi", "fi_FI", "fil", "fil_PH", "fr", "fr_FR",
    "ga", "ga_IE", "gl", "gl_ES", "gu", "gu_IN", "he", "he_IL", "hi", "hi_IN",
    "hr", "hr_HR", "hu", "hu_HU", "hy", "hy_AM", "id", "id_ID", "is", "is_IS",
    "it", "it_IT", "ja", "ja_JP", "jv", "jv_ID", "ka", "ka_GE", "kk", "kk_KZ",
    "km", "km_KH", "kn", "kn_IN", "ko", "ko_KR", "ky", "ky_KG", "lo", "lo_LA",
    "lt", "lt_LT", "lv", "lv_LV", "mk", "mk_MK", "ml", "ml_IN", "mn", "mn_MN",
    "mr", "mr_IN", "ms", "ms_MY", "my", "my_MM", "nb", "nb_NO", "ne", "ne_NP",
    "nl", "nl_NL", "no", "or", "or_IN", "pa", "pa_IN", "pl", "pl_PL", "ps", "ps_AF",
    "pt", "pt_BR", "pt_PT", "ro", "ro_RO", "ru", "ru_RU", "sd", "sd_IN", "si",
    "si_LK", "sk", "sk_SK", "sl", "sl_SI", "so", "so_SO", "sq", "sq_AL", "sr",
    "sr_Cyrl_RS", "sr_Latn", "sr_RS", "sv", "sv_SE", "sw", "sw_TZ", "ta",
    "ta_IN", "te", "te_IN", "th", "th_TH", "tk", "tk_TM", "tr", "tr_TR", "uk",
    "uk_UA", "ur", "ur_PK", "uz", "uz_UZ", "vi", "vi_VN", "yue", "yue_Hant",
    "yue_Hant_HK", "yue_HK", "zh", "zh_CN", "zh_Hans", "zh_Hans_CN", "zh_Hant",
    "zh_Hant_TW", "zh_TW", "zu", "zu_ZA"
};

UBool U_CALLCONV cleanupKnownCanonicalized() {
    gKnownCanonicalizedInitOnce.reset();
    if (gKnownCanonicalized) { uhash_close(gKnownCanonicalized); }
    return true;
}

void U_CALLCONV loadKnownCanonicalized(UErrorCode &status) {
    ucln_common_registerCleanup(UCLN_COMMON_LOCALE_KNOWN_CANONICALIZED,
                                cleanupKnownCanonicalized);
    LocalUHashtablePointer newKnownCanonicalizedMap(
        uhash_open(uhash_hashChars, uhash_compareChars, nullptr, &status));
    for (int32_t i = 0;
            U_SUCCESS(status) && i < UPRV_LENGTHOF(KNOWN_CANONICALIZED);
            i++) {
        uhash_puti(newKnownCanonicalizedMap.getAlias(),
                   (void*)KNOWN_CANONICALIZED[i],
                   1, &status);
    }
    if (U_FAILURE(status)) {
        return;
    }

    gKnownCanonicalized = newKnownCanonicalizedMap.orphan();
}

class AliasData;

/**
 * A Builder class to build the alias data.
 */
class AliasDataBuilder {
public:
    AliasDataBuilder() {
    }

    // Build the AliasData from resource.
    AliasData* build(UErrorCode &status);

private:
    void readAlias(UResourceBundle* alias,
                   UniqueCharStrings* strings,
                   LocalMemory<const char*>& types,
                   LocalMemory<int32_t>& replacementIndexes,
                   int32_t &length,
                   void (*checkType)(const char* type),
                   void (*checkReplacement)(const UChar* replacement),
                   UErrorCode &status);

    // Read the languageAlias data from alias to
    // strings+types+replacementIndexes
    // The number of record will be stored into length.
    // Allocate length items for types, to store the type field.
    // Allocate length items for replacementIndexes,
    // to store the index in the strings for the replacement script.
    void readLanguageAlias(UResourceBundle* alias,
                           UniqueCharStrings* strings,
                           LocalMemory<const char*>& types,
                           LocalMemory<int32_t>& replacementIndexes,
                           int32_t &length,
                           UErrorCode &status);

    // Read the scriptAlias data from alias to
    // strings+types+replacementIndexes
    // Allocate length items for types, to store the type field.
    // Allocate length items for replacementIndexes,
    // to store the index in the strings for the replacement script.
    void readScriptAlias(UResourceBundle* alias,
                         UniqueCharStrings* strings,
                         LocalMemory<const char*>& types,
                         LocalMemory<int32_t>& replacementIndexes,
                         int32_t &length, UErrorCode &status);

    // Read the territoryAlias data from alias to
    // strings+types+replacementIndexes
    // Allocate length items for types, to store the type field.
    // Allocate length items for replacementIndexes,
    // to store the index in the strings for the replacement script.
    void readTerritoryAlias(UResourceBundle* alias,
                            UniqueCharStrings* strings,
                            LocalMemory<const char*>& types,
                            LocalMemory<int32_t>& replacementIndexes,
                            int32_t &length, UErrorCode &status);

    // Read the variantAlias data from alias to
    // strings+types+replacementIndexes
    // Allocate length items for types, to store the type field.
    // Allocate length items for replacementIndexes,
    // to store the index in the strings for the replacement variant.
    void readVariantAlias(UResourceBundle* alias,
                          UniqueCharStrings* strings,
                          LocalMemory<const char*>& types,
                          LocalMemory<int32_t>& replacementIndexes,
                          int32_t &length, UErrorCode &status);

    // Read the subdivisionAlias data from alias to
    // strings+types+replacementIndexes
    // Allocate length items for types, to store the type field.
    // Allocate length items for replacementIndexes,
    // to store the index in the strings for the replacement variant.
    void readSubdivisionAlias(UResourceBundle* alias,
                          UniqueCharStrings* strings,
                          LocalMemory<const char*>& types,
                          LocalMemory<int32_t>& replacementIndexes,
                          int32_t &length, UErrorCode &status);
};

/**
 * A class to hold the Alias Data.
 */
class AliasData : public UMemory {
public:
    static const AliasData* singleton(UErrorCode& status) {
        if (U_FAILURE(status)) {
            // Do not get into loadData if the status already has error.
            return nullptr;
        }
        umtx_initOnce(AliasData::gInitOnce, &AliasData::loadData, status);
        return gSingleton;
    }

    const CharStringMap& languageMap() const { return language; }
    const CharStringMap& scriptMap() const { return script; }
    const CharStringMap& territoryMap() const { return territory; }
    const CharStringMap& variantMap() const { return variant; }
    const CharStringMap& subdivisionMap() const { return subdivision; }

    static void U_CALLCONV loadData(UErrorCode &status);
    static UBool U_CALLCONV cleanup();

    static UInitOnce gInitOnce;

private:
    AliasData(CharStringMap languageMap,
              CharStringMap scriptMap,
              CharStringMap territoryMap,
              CharStringMap variantMap,
              CharStringMap subdivisionMap,
              CharString* strings)
        : language(std::move(languageMap)),
          script(std::move(scriptMap)),
          territory(std::move(territoryMap)),
          variant(std::move(variantMap)),
          subdivision(std::move(subdivisionMap)),
          strings(strings) {
    }

    ~AliasData() {
        delete strings;
    }

    static const AliasData* gSingleton;

    CharStringMap language;
    CharStringMap script;
    CharStringMap territory;
    CharStringMap variant;
    CharStringMap subdivision;
    CharString* strings;

    friend class AliasDataBuilder;
};


const AliasData* AliasData::gSingleton = nullptr;
UInitOnce AliasData::gInitOnce {};

UBool U_CALLCONV
AliasData::cleanup()
{
    gInitOnce.reset();
    delete gSingleton;
    return true;
}

void
AliasDataBuilder::readAlias(
        UResourceBundle* alias,
        UniqueCharStrings* strings,
        LocalMemory<const char*>& types,
        LocalMemory<int32_t>& replacementIndexes,
        int32_t &length,
        void (*checkType)(const char* type),
        void (*checkReplacement)(const UChar* replacement),
        UErrorCode &status) {
    if (U_FAILURE(status)) {
        return;
    }
    length = ures_getSize(alias);
    const char** rawTypes = types.allocateInsteadAndCopy(length);
    if (rawTypes == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    int32_t* rawIndexes = replacementIndexes.allocateInsteadAndCopy(length);
    if (rawIndexes == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    for (int i = 0; U_SUCCESS(status) && ures_hasNext(alias); i++) {
        LocalUResourceBundlePointer res(
            ures_getNextResource(alias, nullptr, &status));
        const char* aliasFrom = ures_getKey(res.getAlias());
        const UChar* aliasTo =
            ures_getStringByKey(res.getAlias(), "replacement", nullptr, &status);
        if (U_FAILURE(status)) return;

        checkType(aliasFrom);
        checkReplacement(aliasTo);

        rawTypes[i] = aliasFrom;
        rawIndexes[i] = strings->add(aliasTo, status);
    }
}

/**
 * Read the languageAlias data from alias to strings+types+replacementIndexes.
 * Allocate length items for types, to store the type field. Allocate length
 * items for replacementIndexes, to store the index in the strings for the
 * replacement language.
 */
void
AliasDataBuilder::readLanguageAlias(
        UResourceBundle* alias,
        UniqueCharStrings* strings,
        LocalMemory<const char*>& types,
        LocalMemory<int32_t>& replacementIndexes,
        int32_t &length,
        UErrorCode &status)
{
    return readAlias(
        alias, strings, types, replacementIndexes, length,
#if U_DEBUG
        [](const char* type) {
            // Assert the aliasFrom only contains the following possibilities
            // language_REGION_variant
            // language_REGION
            // language_variant
            // language
            // und_variant
            Locale test(type);
            // Assert no script in aliasFrom
            U_ASSERT(test.getScript()[0] == '\0');
            // Assert when language is und, no REGION in aliasFrom.
            U_ASSERT(test.getLanguage()[0] != '\0' || test.getCountry()[0] == '\0');
        },
#else
        [](const char*) {},
#endif
        [](const UChar*) {}, status);
}

/**
 * Read the scriptAlias data from alias to strings+types+replacementIndexes.
 * Allocate length items for types, to store the type field. Allocate length
 * items for replacementIndexes, to store the index in the strings for the
 * replacement script.
 */
void
AliasDataBuilder::readScriptAlias(
        UResourceBundle* alias,
        UniqueCharStrings* strings,
        LocalMemory<const char*>& types,
        LocalMemory<int32_t>& replacementIndexes,
        int32_t &length,
        UErrorCode &status)
{
    return readAlias(
        alias, strings, types, replacementIndexes, length,
#if U_DEBUG
        [](const char* type) {
            U_ASSERT(uprv_strlen(type) == 4);
        },
        [](const UChar* replacement) {
            U_ASSERT(u_strlen(replacement) == 4);
        },
#else
        [](const char*) {},
        [](const UChar*) { },
#endif
        status);
}

/**
 * Read the territoryAlias data from alias to strings+types+replacementIndexes.
 * Allocate length items for types, to store the type field. Allocate length
 * items for replacementIndexes, to store the index in the strings for the
 * replacement regions.
 */
void
AliasDataBuilder::readTerritoryAlias(
        UResourceBundle* alias,
        UniqueCharStrings* strings,
        LocalMemory<const char*>& types,
        LocalMemory<int32_t>& replacementIndexes,
        int32_t &length,
        UErrorCode &status)
{
    return readAlias(
        alias, strings, types, replacementIndexes, length,
#if U_DEBUG
        [](const char* type) {
            U_ASSERT(uprv_strlen(type) == 2 || uprv_strlen(type) == 3);
        },
#else
        [](const char*) {},
#endif
        [](const UChar*) { },
        status);
}

/**
 * Read the variantAlias data from alias to strings+types+replacementIndexes.
 * Allocate length items for types, to store the type field. Allocate length
 * items for replacementIndexes, to store the index in the strings for the
 * replacement variant.
 */
void
AliasDataBuilder::readVariantAlias(
        UResourceBundle* alias,
        UniqueCharStrings* strings,
        LocalMemory<const char*>& types,
        LocalMemory<int32_t>& replacementIndexes,
        int32_t &length,
        UErrorCode &status)
{
    return readAlias(
        alias, strings, types, replacementIndexes, length,
#if U_DEBUG
        [](const char* type) {
            U_ASSERT(uprv_strlen(type) >= 4 && uprv_strlen(type) <= 8);
            U_ASSERT(uprv_strlen(type) != 4 ||
                     (type[0] >= '0' && type[0] <= '9'));
        },
        [](const UChar* replacement) {
            int32_t len = u_strlen(replacement);
            U_ASSERT(len >= 4 && len <= 8);
            U_ASSERT(len != 4 ||
                     (*replacement >= u'0' &&
                      *replacement <= u'9'));
        },
#else
        [](const char*) {},
        [](const UChar*) { },
#endif
        status);
}

/**
 * Read the subdivisionAlias data from alias to strings+types+replacementIndexes.
 * Allocate length items for types, to store the type field. Allocate length
 * items for replacementIndexes, to store the index in the strings for the
 * replacement regions.
 */
void
AliasDataBuilder::readSubdivisionAlias(
        UResourceBundle* alias,
        UniqueCharStrings* strings,
        LocalMemory<const char*>& types,
        LocalMemory<int32_t>& replacementIndexes,
        int32_t &length,
        UErrorCode &status)
{
    return readAlias(
        alias, strings, types, replacementIndexes, length,
#if U_DEBUG
        [](const char* type) {
            U_ASSERT(uprv_strlen(type) >= 3 && uprv_strlen(type) <= 8);
        },
#else
        [](const char*) {},
#endif
        [](const UChar*) { },
        status);
}

/**
 * Initializes the alias data from the ICU resource bundles. The alias data
 * contains alias of language, country, script and variants.
 *
 * If the alias data has already loaded, then this method simply returns without
 * doing anything meaningful.
 */
void U_CALLCONV
AliasData::loadData(UErrorCode &status)
{
#ifdef LOCALE_CANONICALIZATION_DEBUG
    UDate start = uprv_getRawUTCtime();
#endif  // LOCALE_CANONICALIZATION_DEBUG
    ucln_common_registerCleanup(UCLN_COMMON_LOCALE_ALIAS, cleanup);
    AliasDataBuilder builder;
    gSingleton = builder.build(status);
#ifdef LOCALE_CANONICALIZATION_DEBUG
    UDate end = uprv_getRawUTCtime();
    printf("AliasData::loadData took total %f ms\n", end - start);
#endif  // LOCALE_CANONICALIZATION_DEBUG
}

/**
 * Build the alias data from resources.
 */
AliasData*
AliasDataBuilder::build(UErrorCode &status) {
    if (U_FAILURE(status)) { return nullptr; }

    LocalUResourceBundlePointer metadata(
        ures_openDirect(nullptr, "metadata", &status));
    LocalUResourceBundlePointer metadataAlias(
        ures_getByKey(metadata.getAlias(), "alias", nullptr, &status));
    LocalUResourceBundlePointer languageAlias(
        ures_getByKey(metadataAlias.getAlias(), "language", nullptr, &status));
    LocalUResourceBundlePointer scriptAlias(
        ures_getByKey(metadataAlias.getAlias(), "script", nullptr, &status));
    LocalUResourceBundlePointer territoryAlias(
        ures_getByKey(metadataAlias.getAlias(), "territory", nullptr, &status));
    LocalUResourceBundlePointer variantAlias(
        ures_getByKey(metadataAlias.getAlias(), "variant", nullptr, &status));
    LocalUResourceBundlePointer subdivisionAlias(
        ures_getByKey(metadataAlias.getAlias(), "subdivision", nullptr, &status));

    if (U_FAILURE(status)) {
        return nullptr;
    }
    int32_t languagesLength = 0, scriptLength = 0, territoryLength = 0,
            variantLength = 0, subdivisionLength = 0;

    // Read the languageAlias into languageTypes, languageReplacementIndexes
    // and strings
    UniqueCharStrings strings(status);
    LocalMemory<const char*> languageTypes;
    LocalMemory<int32_t> languageReplacementIndexes;
    readLanguageAlias(languageAlias.getAlias(),
                      &strings,
                      languageTypes,
                      languageReplacementIndexes,
                      languagesLength,
                      status);

    // Read the scriptAlias into scriptTypes, scriptReplacementIndexes
    // and strings
    LocalMemory<const char*> scriptTypes;
    LocalMemory<int32_t> scriptReplacementIndexes;
    readScriptAlias(scriptAlias.getAlias(),
                    &strings,
                    scriptTypes,
                    scriptReplacementIndexes,
                    scriptLength,
                    status);

    // Read the territoryAlias into territoryTypes, territoryReplacementIndexes
    // and strings
    LocalMemory<const char*> territoryTypes;
    LocalMemory<int32_t> territoryReplacementIndexes;
    readTerritoryAlias(territoryAlias.getAlias(),
                       &strings,
                       territoryTypes,
                       territoryReplacementIndexes,
                       territoryLength, status);

    // Read the variantAlias into variantTypes, variantReplacementIndexes
    // and strings
    LocalMemory<const char*> variantTypes;
    LocalMemory<int32_t> variantReplacementIndexes;
    readVariantAlias(variantAlias.getAlias(),
                     &strings,
                     variantTypes,
                     variantReplacementIndexes,
                     variantLength, status);

    // Read the subdivisionAlias into subdivisionTypes, subdivisionReplacementIndexes
    // and strings
    LocalMemory<const char*> subdivisionTypes;
    LocalMemory<int32_t> subdivisionReplacementIndexes;
    readSubdivisionAlias(subdivisionAlias.getAlias(),
                         &strings,
                         subdivisionTypes,
                         subdivisionReplacementIndexes,
                         subdivisionLength, status);

    if (U_FAILURE(status)) {
        return nullptr;
    }

    // We can only use strings after freeze it.
    strings.freeze();

    // Build the languageMap from languageTypes & languageReplacementIndexes
    CharStringMap languageMap(490, status);
    for (int32_t i = 0; U_SUCCESS(status) && i < languagesLength; i++) {
        languageMap.put(languageTypes[i],
                        strings.get(languageReplacementIndexes[i]),
                        status);
    }

    // Build the scriptMap from scriptTypes & scriptReplacementIndexes
    CharStringMap scriptMap(1, status);
    for (int32_t i = 0; U_SUCCESS(status) && i < scriptLength; i++) {
        scriptMap.put(scriptTypes[i],
                      strings.get(scriptReplacementIndexes[i]),
                      status);
    }

    // Build the territoryMap from territoryTypes & territoryReplacementIndexes
    CharStringMap territoryMap(650, status);
    for (int32_t i = 0; U_SUCCESS(status) && i < territoryLength; i++) {
        territoryMap.put(territoryTypes[i],
                         strings.get(territoryReplacementIndexes[i]),
                         status);
    }

    // Build the variantMap from variantTypes & variantReplacementIndexes.
    CharStringMap variantMap(2, status);
    for (int32_t i = 0; U_SUCCESS(status) && i < variantLength; i++) {
        variantMap.put(variantTypes[i],
                       strings.get(variantReplacementIndexes[i]),
                       status);
    }

    // Build the subdivisionMap from subdivisionTypes & subdivisionReplacementIndexes.
    CharStringMap subdivisionMap(2, status);
    for (int32_t i = 0; U_SUCCESS(status) && i < subdivisionLength; i++) {
        subdivisionMap.put(subdivisionTypes[i],
                       strings.get(subdivisionReplacementIndexes[i]),
                       status);
    }

    if (U_FAILURE(status)) {
        return nullptr;
    }

    // copy hashtables
    auto *data = new AliasData(
        std::move(languageMap),
        std::move(scriptMap),
        std::move(territoryMap),
        std::move(variantMap),
        std::move(subdivisionMap),
        strings.orphanCharStrings());

    if (data == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
    }
    return data;
}

/**
 * A class that find the replacement values of locale fields by using AliasData.
 */
class AliasReplacer {
public:
    AliasReplacer(UErrorCode& status) :
            language(nullptr), script(nullptr), region(nullptr),
            extensions(nullptr),
            // store value in variants only once
            variants(nullptr,
                     ([](UElement e1, UElement e2) -> UBool {
                       return 0==uprv_strcmp((const char*)e1.pointer,
                                             (const char*)e2.pointer);}),
                     status),
            data(nullptr) {
    }
    ~AliasReplacer() {
    }

    // Check the fields inside locale, if need to replace fields,
    // place the the replaced locale ID in out and return true.
    // Otherwise return false for no replacement or error.
    bool replace(
        const Locale& locale, CharString& out, UErrorCode& status);

private:
    const char* language;
    const char* script;
    const char* region;
    const char* extensions;
    UVector variants;

    const AliasData* data;

    inline bool notEmpty(const char* str) {
        return str && str[0] != NULL_CHAR;
    }

    /**
     * If replacement is neither null nor empty and input is either null or empty,
     * return replacement.
     * If replacement is neither null nor empty but input is not empty, return input.
     * If replacement is either null or empty and type is either null or empty,
     * return input.
     * Otherwise return null.
     *   replacement     input      type        return
     *    AAA             nullptr    *           AAA
     *    AAA             BBB        *           BBB
     *    nullptr || ""   CCC        nullptr     CCC
     *    nullptr || ""   *          DDD         nullptr
     */
    inline const char* deleteOrReplace(
            const char* input, const char* type, const char* replacement) {
        return notEmpty(replacement) ?
            ((input == nullptr) ?  replacement : input) :
            ((type == nullptr) ? input  : nullptr);
    }

    inline bool same(const char* a, const char* b) {
        if (a == nullptr && b == nullptr) {
            return true;
        }
        if ((a == nullptr && b != nullptr) ||
            (a != nullptr && b == nullptr)) {
          return false;
        }
        return uprv_strcmp(a, b) == 0;
    }

    // Gather fields and generate locale ID into out.
    CharString& outputToString(CharString& out, UErrorCode& status);

    // Generate the lookup key.
    CharString& generateKey(const char* language, const char* region,
                            const char* variant, CharString& out,
                            UErrorCode& status);

    void parseLanguageReplacement(const char* replacement,
                                  const char*& replaceLanguage,
                                  const char*& replaceScript,
                                  const char*& replaceRegion,
                                  const char*& replaceVariant,
                                  const char*& replaceExtensions,
                                  UVector& toBeFreed,
                                  UErrorCode& status);

    // Replace by using languageAlias.
    bool replaceLanguage(bool checkLanguage, bool checkRegion,
                         bool checkVariants, UVector& toBeFreed,
                         UErrorCode& status);

    // Replace by using territoryAlias.
    bool replaceTerritory(UVector& toBeFreed, UErrorCode& status);

    // Replace by using scriptAlias.
    bool replaceScript(UErrorCode& status);

    // Replace by using variantAlias.
    bool replaceVariant(UErrorCode& status);

    // Replace by using subdivisionAlias.
    bool replaceSubdivision(StringPiece subdivision,
                            CharString& output, UErrorCode& status);

    // Replace transformed extensions.
    bool replaceTransformedExtensions(
        CharString& transformedExtensions, CharString& output, UErrorCode& status);
};

CharString&
AliasReplacer::generateKey(
        const char* language, const char* region, const char* variant,
        CharString& out, UErrorCode& status)
{
    if (U_FAILURE(status)) { return out; }
    out.append(language, status);
    if (notEmpty(region)) {
        out.append(SEP_CHAR, status)
            .append(region, status);
    }
    if (notEmpty(variant)) {
       out.append(SEP_CHAR, status)
           .append(variant, status);
    }
    return out;
}

void
AliasReplacer::parseLanguageReplacement(
    const char* replacement,
    const char*& replacedLanguage,
    const char*& replacedScript,
    const char*& replacedRegion,
    const char*& replacedVariant,
    const char*& replacedExtensions,
    UVector& toBeFreed,
    UErrorCode& status)
{
    if (U_FAILURE(status)) {
        return;
    }
    replacedScript = replacedRegion = replacedVariant
        = replacedExtensions = nullptr;
    if (uprv_strchr(replacement, '_') == nullptr) {
        replacedLanguage = replacement;
        // reach the end, just return it.
        return;
    }
    // We have multiple field so we have to allocate and parse
    CharString* str = new CharString(
        replacement, (int32_t)uprv_strlen(replacement), status);
    LocalPointer<CharString> lpStr(str, status);
    toBeFreed.adoptElement(lpStr.orphan(), status);
    if (U_FAILURE(status)) {
        return;
    }
    char* data = str->data();
    replacedLanguage = (const char*) data;
    char* endOfField = uprv_strchr(data, '_');
    *endOfField = '\0'; // null terminiate it.
    endOfField++;
    const char* start = endOfField;
    endOfField = (char*) uprv_strchr(start, '_');
    size_t len = 0;
    if (endOfField == nullptr) {
        len = uprv_strlen(start);
    } else {
        len = endOfField - start;
        *endOfField = '\0'; // null terminiate it.
    }
    if (len == 4 && uprv_isASCIILetter(*start)) {
        // Got a script
        replacedScript = start;
        if (endOfField == nullptr) {
            return;
        }
        start = endOfField++;
        endOfField = (char*)uprv_strchr(start, '_');
        if (endOfField == nullptr) {
            len = uprv_strlen(start);
        } else {
            len = endOfField - start;
            *endOfField = '\0'; // null terminiate it.
        }
    }
    if (len >= 2 && len <= 3) {
        // Got a region
        replacedRegion = start;
        if (endOfField == nullptr) {
            return;
        }
        start = endOfField++;
        endOfField = (char*)uprv_strchr(start, '_');
        if (endOfField == nullptr) {
            len = uprv_strlen(start);
        } else {
            len = endOfField - start;
            *endOfField = '\0'; // null terminiate it.
        }
    }
    if (len >= 4) {
        // Got a variant
        replacedVariant = start;
        if (endOfField == nullptr) {
            return;
        }
        start = endOfField++;
    }
    replacedExtensions = start;
}

bool
AliasReplacer::replaceLanguage(
        bool checkLanguage, bool checkRegion,
        bool checkVariants, UVector& toBeFreed, UErrorCode& status)
{
    if (U_FAILURE(status)) {
        return false;
    }
    if (    (checkRegion && region == nullptr) ||
            (checkVariants && variants.size() == 0)) {
        // Nothing to search.
        return false;
    }
    int32_t variant_size = checkVariants ? variants.size() : 1;
    // Since we may have more than one variant, we need to loop through them.
    const char* searchLanguage = checkLanguage ? language : "und";
    const char* searchRegion = checkRegion ? region : nullptr;
    const char* searchVariant = nullptr;
    for (int32_t variant_index = 0;
            variant_index < variant_size;
            variant_index++) {
        if (checkVariants) {
            U_ASSERT(variant_index < variant_size);
            searchVariant = (const char*)(variants.elementAt(variant_index));
        }

        if (searchVariant != nullptr && uprv_strlen(searchVariant) < 4) {
            // Do not consider  ill-formed variant subtag.
            searchVariant = nullptr;
        }
        CharString typeKey;
        generateKey(searchLanguage, searchRegion, searchVariant, typeKey,
                    status);
        if (U_FAILURE(status)) {
            return false;
        }
        const char *replacement = data->languageMap().get(typeKey.data());
        if (replacement == nullptr) {
            // Found no replacement data.
            continue;
        }

        const char* replacedLanguage = nullptr;
        const char* replacedScript = nullptr;
        const char* replacedRegion = nullptr;
        const char* replacedVariant = nullptr;
        const char* replacedExtensions = nullptr;
        parseLanguageReplacement(replacement,
                                 replacedLanguage,
                                 replacedScript,
                                 replacedRegion,
                                 replacedVariant,
                                 replacedExtensions,
                                 toBeFreed,
                                 status);
        replacedLanguage =
            (replacedLanguage != nullptr && uprv_strcmp(replacedLanguage, "und") == 0) ?
            language : replacedLanguage;
        replacedScript = deleteOrReplace(script, nullptr, replacedScript);
        replacedRegion = deleteOrReplace(region, searchRegion, replacedRegion);
        replacedVariant = deleteOrReplace(
            searchVariant, searchVariant, replacedVariant);

        if (    same(language, replacedLanguage) &&
                same(script, replacedScript) &&
                same(region, replacedRegion) &&
                same(searchVariant, replacedVariant) &&
                replacedExtensions == nullptr) {
            // Replacement produce no changes.
            continue;
        }

        language = replacedLanguage;
        region = replacedRegion;
        script = replacedScript;
        if (searchVariant != nullptr) {
            if (notEmpty(replacedVariant)) {
                variants.setElementAt((void*)replacedVariant, variant_index);
            } else {
                variants.removeElementAt(variant_index);
            }
        }
        if (replacedExtensions != nullptr) {
            // DO NOTHING
            // UTS35 does not specify what should we do if we have extensions in the
            // replacement. Currently we know only the following 4 "BCP47 LegacyRules" have
            // extensions in them languageAlias:
            //  i_default => en_x_i_default
            //  i_enochian => und_x_i_enochian
            //  i_mingo => see_x_i_mingo
            //  zh_min => nan_x_zh_min
            // But all of them are already changed by code inside ultag_parse() before
            // hitting this code.
        }

        // Something changed by language alias data.
        return true;
    }
    // Nothing changed by language alias data.
    return false;
}

bool
AliasReplacer::replaceTerritory(UVector& toBeFreed, UErrorCode& status)
{
    if (U_FAILURE(status)) {
        return false;
    }
    if (region == nullptr) {
        // No region to search.
        return false;
    }
    const char *replacement = data->territoryMap().get(region);
    if (replacement == nullptr) {
        // Found no replacement data for this region.
        return false;
    }
    const char* replacedRegion = replacement;
    const char* firstSpace = uprv_strchr(replacement, ' ');
    if (firstSpace != nullptr) {
        // If there are are more than one region in the replacement.
        // We need to check which one match based on the language.
        // Cannot use nullptr for language because that will construct
        // the default locale, in that case, use "und" to get the correct
        // locale.
        Locale l = LocaleBuilder()
            .setLanguage(language == nullptr ? "und" : language)
            .setScript(script)
            .build(status);
        l.addLikelySubtags(status);
        const char* likelyRegion = l.getCountry();
        LocalPointer<CharString> item;
        if (likelyRegion != nullptr && uprv_strlen(likelyRegion) > 0) {
            size_t len = uprv_strlen(likelyRegion);
            const char* foundInReplacement = uprv_strstr(replacement,
                                                         likelyRegion);
            if (foundInReplacement != nullptr) {
                // Assuming the case there are no three letter region code in
                // the replacement of territoryAlias
                U_ASSERT(foundInReplacement == replacement ||
                         *(foundInReplacement-1) == ' ');
                U_ASSERT(foundInReplacement[len] == ' ' ||
                         foundInReplacement[len] == '\0');
                item.adoptInsteadAndCheckErrorCode(
                    new CharString(foundInReplacement, (int32_t)len, status), status);
            }
        }
        if (item.isNull() && U_SUCCESS(status)) {
            item.adoptInsteadAndCheckErrorCode(
                new CharString(replacement,
                               (int32_t)(firstSpace - replacement), status), status);
        }
        if (U_FAILURE(status)) { return false; }
        replacedRegion = item->data();
        toBeFreed.adoptElement(item.orphan(), status);
        if (U_FAILURE(status)) { return false; }
    }
    U_ASSERT(!same(region, replacedRegion));
    region = replacedRegion;
    // The region is changed by data in territory alias.
    return true;
}

bool
AliasReplacer::replaceScript(UErrorCode& status)
{
    if (U_FAILURE(status)) {
        return false;
    }
    if (script == nullptr) {
        // No script to search.
        return false;
    }
    const char *replacement = data->scriptMap().get(script);
    if (replacement == nullptr) {
        // Found no replacement data for this script.
        return false;
    }
    U_ASSERT(!same(script, replacement));
    script = replacement;
    // The script is changed by data in script alias.
    return true;
}

bool
AliasReplacer::replaceVariant(UErrorCode& status)
{
    if (U_FAILURE(status)) {
        return false;
    }
    // Since we may have more than one variant, we need to loop through them.
    for (int32_t i = 0; i < variants.size(); i++) {
        const char *variant = (const char*)(variants.elementAt(i));
        const char *replacement = data->variantMap().get(variant);
        if (replacement == nullptr) {
            // Found no replacement data for this variant.
            continue;
        }
        U_ASSERT((uprv_strlen(replacement) >= 5  &&
                  uprv_strlen(replacement) <= 8) ||
                 (uprv_strlen(replacement) == 4 &&
                  replacement[0] >= '0' &&
                  replacement[0] <= '9'));
        if (!same(variant, replacement)) {
            variants.setElementAt((void*)replacement, i);
            // Special hack to handle hepburn-heploc => alalc97
            if (uprv_strcmp(variant, "heploc") == 0) {
                for (int32_t j = 0; j < variants.size(); j++) {
                     if (uprv_strcmp((const char*)(variants.elementAt(j)),
                                     "hepburn") == 0) {
                         variants.removeElementAt(j);
                     }
                }
            }
            return true;
        }
    }
    return false;
}

bool
AliasReplacer::replaceSubdivision(
    StringPiece subdivision, CharString& output, UErrorCode& status)
{
    if (U_FAILURE(status)) {
        return false;
    }
    const char *replacement = data->subdivisionMap().get(subdivision.data());
    if (replacement != nullptr) {
        const char* firstSpace = uprv_strchr(replacement, ' ');
        // Found replacement data for this subdivision.
        size_t len = (firstSpace != nullptr) ?
            (firstSpace - replacement) : uprv_strlen(replacement);
        if (2 <= len && len <= 8) {
            output.append(replacement, (int32_t)len, status);
            if (2 == len) {
                // Add 'zzzz' based on changes to UTS #35 for CLDR-14312.
                output.append("zzzz", 4, status);
            }
        }
        return true;
    }
    return false;
}

bool
AliasReplacer::replaceTransformedExtensions(
    CharString& transformedExtensions, CharString& output, UErrorCode& status)
{
    // The content of the transformedExtensions will be modified in this
    // function to NUL-terminating (tkey-tvalue) pairs.
    if (U_FAILURE(status)) {
        return false;
    }
    int32_t len = transformedExtensions.length();
    const char* str = transformedExtensions.data();
    const char* tkey = ultag_getTKeyStart(str);
    int32_t tlangLen = (tkey == str) ? 0 :
        ((tkey == nullptr) ? len : static_cast<int32_t>((tkey - str - 1)));
    if (tlangLen > 0) {
        Locale tlang = LocaleBuilder()
            .setLanguageTag(StringPiece(str, tlangLen))
            .build(status);
        tlang.canonicalize(status);
        output = tlang.toLanguageTag<CharString>(status);
        if (U_FAILURE(status)) {
            return false;
        }
        T_CString_toLowerCase(output.data());
    }
    if (tkey != nullptr) {
        // We need to sort the tfields by tkey
        UVector tfields(status);
        if (U_FAILURE(status)) {
            return false;
        }
        do {
            const char* tvalue = uprv_strchr(tkey, '-');
            if (tvalue == nullptr) {
                status = U_ILLEGAL_ARGUMENT_ERROR;
                return false;
            }
            const char* nextTKey = ultag_getTKeyStart(tvalue);
            if (nextTKey != nullptr) {
                *((char*)(nextTKey-1)) = '\0';  // NUL terminate tvalue
            }
            tfields.insertElementAt((void*)tkey, tfields.size(), status);
            if (U_FAILURE(status)) {
                return false;
            }
            tkey = nextTKey;
        } while (tkey != nullptr);
        tfields.sort([](UElement e1, UElement e2) -> int32_t {
            return uprv_strcmp((const char*)e1.pointer, (const char*)e2.pointer);
        }, status);
        for (int32_t i = 0; i < tfields.size(); i++) {
             if (output.length() > 0) {
                 output.append('-', status);
             }
             const char* tfield = (const char*) tfields.elementAt(i);
             const char* tvalue = uprv_strchr(tfield, '-');
             if (tvalue == nullptr) {
                 status = U_ILLEGAL_ARGUMENT_ERROR;
                 return false;
             }
             // Split the "tkey-tvalue" pair string so that we can canonicalize the tvalue.
             *((char*)tvalue++) = '\0'; // NUL terminate tkey
             output.append(tfield, status).append('-', status);
             const char* bcpTValue = ulocimp_toBcpType(tfield, tvalue, nullptr, nullptr);
             output.append((bcpTValue == nullptr) ? tvalue : bcpTValue, status);
        }
    }
    if (U_FAILURE(status)) {
        return false;
    }
    return true;
}

CharString&
AliasReplacer::outputToString(
    CharString& out, UErrorCode& status)
{
    if (U_FAILURE(status)) { return out; }
    out.append(language, status);
    if (notEmpty(script)) {
        out.append(SEP_CHAR, status)
            .append(script, status);
    }
    if (notEmpty(region)) {
        out.append(SEP_CHAR, status)
            .append(region, status);
    }
    if (variants.size() > 0) {
        if (!notEmpty(script) && !notEmpty(region)) {
          out.append(SEP_CHAR, status);
        }
        variants.sort([](UElement e1, UElement e2) -> int32_t {
            return uprv_strcmp((const char*)e1.pointer, (const char*)e2.pointer);
        }, status);
        int32_t variantsStart = out.length();
        for (int32_t i = 0; i < variants.size(); i++) {
             out.append(SEP_CHAR, status)
                 .append((const char*)(variants.elementAt(i)),
                         status);
        }
        T_CString_toUpperCase(out.data() + variantsStart);
    }
    if (notEmpty(extensions)) {
        CharString tmp("und_", status);
        tmp.append(extensions, status);
        Locale tmpLocale(tmp.data());
        // only support x extension inside CLDR for now.
        U_ASSERT(extensions[0] == 'x');
        out.append(tmpLocale.getName() + 1, status);
    }
    return out;
}

bool
AliasReplacer::replace(const Locale& locale, CharString& out, UErrorCode& status)
{
    data = AliasData::singleton(status);
    if (U_FAILURE(status)) {
        return false;
    }
    U_ASSERT(data != nullptr);
    out.clear();
    language = locale.getLanguage();
    if (!notEmpty(language)) {
        language = nullptr;
    }
    script = locale.getScript();
    if (!notEmpty(script)) {
        script = nullptr;
    }
    region = locale.getCountry();
    if (!notEmpty(region)) {
        region = nullptr;
    }
    const char* variantsStr = locale.getVariant();
    CharString variantsBuff(variantsStr, -1, status);
    if (!variantsBuff.isEmpty()) {
        if (U_FAILURE(status)) { return false; }
        char* start = variantsBuff.data();
        T_CString_toLowerCase(start);
        char* end;
        while ((end = uprv_strchr(start, SEP_CHAR)) != nullptr &&
               U_SUCCESS(status)) {
            *end = NULL_CHAR;  // null terminate inside variantsBuff
            // do not add "" or duplicate data to variants
            if (*start && !variants.contains(start)) {
                variants.addElement(start, status);
            }
            start = end + 1;
        }
        // do not add "" or duplicate data to variants
        if (*start && !variants.contains(start)) {
            variants.addElement(start, status);
        }
    }
    if (U_FAILURE(status)) { return false; }

    // Sort the variants
    variants.sort([](UElement e1, UElement e2) -> int32_t {
        return uprv_strcmp((const char*)e1.pointer, (const char*)e2.pointer);
    }, status);

    // A changed count to assert when loop too many times.
    int changed = 0;
    // A UVector to to hold CharString allocated by the replace* method
    // and freed when out of scope from his function.
    UVector stringsToBeFreed([](void *obj){ delete ((CharString*) obj); },
                             nullptr, 10, status);
    while (U_SUCCESS(status)) {
        // Something wrong with the data cause looping here more than 10 times
        // already.
        U_ASSERT(changed < 5);
        // From observation of key in data/misc/metadata.txt
        // we know currently we only need to search in the following combination
        // of fields for type in languageAlias:
        // * lang_region_variant
        // * lang_region
        // * lang_variant
        // * lang
        // * und_variant
        // This assumption is ensured by the U_ASSERT in readLanguageAlias
        //
        //                      lang  REGION variant
        if (    replaceLanguage(true, true,  true,  stringsToBeFreed, status) ||
                replaceLanguage(true, true,  false, stringsToBeFreed, status) ||
                replaceLanguage(true, false, true,  stringsToBeFreed, status) ||
                replaceLanguage(true, false, false, stringsToBeFreed, status) ||
                replaceLanguage(false,false, true,  stringsToBeFreed, status) ||
                replaceTerritory(stringsToBeFreed, status) ||
                replaceScript(status) ||
                replaceVariant(status)) {
            // Some values in data is changed, try to match from the beginning
            // again.
            changed++;
            continue;
        }
        // Nothing changed. Break out.
        break;
    }  // while(1)

    if (U_FAILURE(status)) { return false; }
    // Nothing changed and we know the order of the variants are not change
    // because we have no variant or only one.
    const char* extensionsStr = locale_getKeywordsStart(locale.getName());
    if (changed == 0 && variants.size() <= 1 && extensionsStr == nullptr) {
        return false;
    }
    outputToString(out, status);
    if (U_FAILURE(status)) {
        return false;
    }
    if (extensionsStr != nullptr) {
        changed = 0;
        Locale temp(locale);
        LocalPointer<icu::StringEnumeration> iter(locale.createKeywords(status));
        if (U_SUCCESS(status) && !iter.isNull()) {
            const char* key;
            while ((key = iter->next(nullptr, status)) != nullptr) {
                if (uprv_strcmp("sd", key) == 0 || uprv_strcmp("rg", key) == 0 ||
                        uprv_strcmp("t", key) == 0) {
                    auto value = locale.getKeywordValue<CharString>(key, status);
                    if (U_FAILURE(status)) {
                        status = U_ZERO_ERROR;
                        continue;
                    }
                    CharString replacement;
                    if (uprv_strlen(key) == 2) {
                        if (replaceSubdivision(value.toStringPiece(), replacement, status)) {
                            changed++;
                            temp.setKeywordValue(key, replacement.data(), status);
                        }
                    } else {
                        U_ASSERT(uprv_strcmp(key, "t") == 0);
                        if (replaceTransformedExtensions(value, replacement, status)) {
                            changed++;
                            temp.setKeywordValue(key, replacement.data(), status);
                        }
                    }
                    if (U_FAILURE(status)) {
                        return false;
                    }
                }
            }
        }
        if (changed != 0) {
            extensionsStr = locale_getKeywordsStart(temp.getName());
        }
        out.append(extensionsStr, status);
    }
    if (U_FAILURE(status)) {
        return false;
    }
    // If the tag is not changed, return.
    if (uprv_strcmp(out.data(), locale.getName()) == 0) {
        out.clear();
        return false;
    }
    return true;
}

// Return true if the locale is changed during canonicalization.
// The replaced value then will be put into out.
bool
canonicalizeLocale(const Locale& locale, CharString& out, UErrorCode& status)
{
    if (U_FAILURE(status)) { return false; }
    AliasReplacer replacer(status);
    return replacer.replace(locale, out, status);
}

// Function to optimize for known cases without so we can skip the loading
// of resources in the startup time until we really need it.
bool
isKnownCanonicalizedLocale(const char* locale, UErrorCode& status)
{
    if (U_FAILURE(status)) { return false; }

    if (    uprv_strcmp(locale, "c") == 0 ||
            uprv_strcmp(locale, "en") == 0 ||
            uprv_strcmp(locale, "en_US") == 0) {
        return true;
    }

    // common well-known Canonicalized.
    umtx_initOnce(gKnownCanonicalizedInitOnce,
                  &loadKnownCanonicalized, status);
    if (U_FAILURE(status)) {
        return false;
    }
    U_ASSERT(gKnownCanonicalized != nullptr);
    return uhash_geti(gKnownCanonicalized, locale) != 0;
}

}  // namespace

U_NAMESPACE_END

// Function for testing.
U_EXPORT const char* const*
ulocimp_getKnownCanonicalizedLocaleForTest(int32_t& length)
{
    U_NAMESPACE_USE
    length = UPRV_LENGTHOF(KNOWN_CANONICALIZED);
    return KNOWN_CANONICALIZED;
}

// Function for testing.
U_EXPORT bool
ulocimp_isCanonicalizedLocaleForTest(const char* localeName)
{
    U_NAMESPACE_USE
    Locale l(localeName);
    UErrorCode status = U_ZERO_ERROR;
    CharString temp;
    return !canonicalizeLocale(l, temp, status) && U_SUCCESS(status);
}

U_NAMESPACE_BEGIN

/*This function initializes a Locale from a C locale ID*/
Locale& Locale::init(const char* localeID, UBool canonicalize)
{
    fIsBogus = false;
    /* Free our current storage */
    if ((baseName != fullName) && (baseName != fullNameBuffer)) {
        uprv_free(baseName);
    }
    baseName = nullptr;
    if(fullName != fullNameBuffer) {
        uprv_free(fullName);
        fullName = fullNameBuffer;
    }

    // not a loop:
    // just an easy way to have a common error-exit
    // without goto and without another function
    do {
        char *separator;
        char *field[5] = {nullptr};
        int32_t fieldLen[5] = {0};
        int32_t fieldIdx;
        int32_t variantField;
        int32_t length;
        UErrorCode err;

        if(localeID == nullptr) {
            // not an error, just set the default locale
            return *this = getDefault();
        }

        /* preset all fields to empty */
        language[0] = script[0] = country[0] = 0;

        // "canonicalize" the locale ID to ICU/Java format
        err = U_ZERO_ERROR;
        length = canonicalize ?
            uloc_canonicalize(localeID, fullName, sizeof(fullNameBuffer), &err) :
            uloc_getName(localeID, fullName, sizeof(fullNameBuffer), &err);

        if(err == U_BUFFER_OVERFLOW_ERROR || length >= (int32_t)sizeof(fullNameBuffer)) {
            U_ASSERT(baseName == nullptr);
            /*Go to heap for the fullName if necessary*/
            fullName = (char *)uprv_malloc(sizeof(char)*(length + 1));
            if (fullName == nullptr) {
                fullName = fullNameBuffer;
                break; // error: out of memory
            }
            err = U_ZERO_ERROR;
            length = canonicalize ?
                uloc_canonicalize(localeID, fullName, length+1, &err) :
                uloc_getName(localeID, fullName, length+1, &err);
        }
        if(U_FAILURE(err) || err == U_STRING_NOT_TERMINATED_WARNING) {
            /* should never occur */
            break;
        }

        variantBegin = length;

        /* after uloc_getName/canonicalize() we know that only '_' are separators */
        /* But _ could also appeared in timezone such as "en@timezone=America/Los_Angeles" */
        separator = field[0] = fullName;
        fieldIdx = 1;
        char* at = uprv_strchr(fullName, '@');
        while ((separator = uprv_strchr(field[fieldIdx-1], SEP_CHAR)) != nullptr &&
               fieldIdx < UPRV_LENGTHOF(field)-1 &&
               (at == nullptr || separator < at)) {
            field[fieldIdx] = separator + 1;
            fieldLen[fieldIdx-1] = (int32_t)(separator - field[fieldIdx-1]);
            fieldIdx++;
        }
        // variant may contain @foo or .foo POSIX cruft; remove it
        separator = uprv_strchr(field[fieldIdx-1], '@');
        char* sep2 = uprv_strchr(field[fieldIdx-1], '.');
        if (separator!=nullptr || sep2!=nullptr) {
            if (separator==nullptr || (sep2!=nullptr && separator > sep2)) {
                separator = sep2;
            }
            fieldLen[fieldIdx-1] = (int32_t)(separator - field[fieldIdx-1]);
        } else {
            fieldLen[fieldIdx-1] = length - (int32_t)(field[fieldIdx-1] - fullName);
        }

        if (fieldLen[0] >= (int32_t)(sizeof(language)))
        {
            break; // error: the language field is too long
        }

        variantField = 1; /* Usually the 2nd one, except when a script or country is also used. */
        if (fieldLen[0] > 0) {
            /* We have a language */
            uprv_memcpy(language, fullName, fieldLen[0]);
            language[fieldLen[0]] = 0;
        }
        if (fieldLen[1] == 4 && uprv_isASCIILetter(field[1][0]) &&
                uprv_isASCIILetter(field[1][1]) && uprv_isASCIILetter(field[1][2]) &&
                uprv_isASCIILetter(field[1][3])) {
            /* We have at least a script */
            uprv_memcpy(script, field[1], fieldLen[1]);
            script[fieldLen[1]] = 0;
            variantField++;
        }

        if (fieldLen[variantField] == 2 || fieldLen[variantField] == 3) {
            /* We have a country */
            uprv_memcpy(country, field[variantField], fieldLen[variantField]);
            country[fieldLen[variantField]] = 0;
            variantField++;
        } else if (fieldLen[variantField] == 0) {
            variantField++; /* script or country empty but variant in next field (i.e. en__POSIX) */
        }

        if (fieldLen[variantField] > 0) {
            /* We have a variant */
            variantBegin = (int32_t)(field[variantField] - fullName);
        }

        err = U_ZERO_ERROR;
        initBaseName(err);
        if (U_FAILURE(err)) {
            break;
        }

        if (canonicalize) {
            if (!isKnownCanonicalizedLocale(fullName, err)) {
                CharString replaced;
                // Not sure it is already canonicalized
                if (canonicalizeLocale(*this, replaced, err)) {
                    U_ASSERT(U_SUCCESS(err));
                    // If need replacement, call init again.
                    init(replaced.data(), false);
                }
                if (U_FAILURE(err)) {
                    break;
                }
            }
        }   // if (canonicalize) {

        // successful end of init()
        return *this;
    } while(0); /*loop doesn't iterate*/

    // when an error occurs, then set this object to "bogus" (there is no UErrorCode here)
    setToBogus();

    return *this;
}

/*
 * Set up the base name.
 * If there are no key words, it's exactly the full name.
 * If key words exist, it's the full name truncated at the '@' character.
 * Need to set up both at init() and after setting a keyword.
 */
void
Locale::initBaseName(UErrorCode &status) {
    if (U_FAILURE(status)) {
        return;
    }
    U_ASSERT(baseName==nullptr || baseName==fullName);
    const char *atPtr = uprv_strchr(fullName, '@');
    const char *eqPtr = uprv_strchr(fullName, '=');
    if (atPtr && eqPtr && atPtr < eqPtr) {
        // Key words exist.
        int32_t baseNameLength = (int32_t)(atPtr - fullName);
        baseName = (char *)uprv_malloc(baseNameLength + 1);
        if (baseName == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
        uprv_strncpy(baseName, fullName, baseNameLength);
        baseName[baseNameLength] = 0;

        // The original computation of variantBegin leaves it equal to the length
        // of fullName if there is no variant.  It should instead be
        // the length of the baseName.
        if (variantBegin > baseNameLength) {
            variantBegin = baseNameLength;
        }
    } else {
        baseName = fullName;
    }
}


int32_t
Locale::hashCode() const
{
    return ustr_hashCharsN(fullName, static_cast<int32_t>(uprv_strlen(fullName)));
}

void
Locale::setToBogus() {
    /* Free our current storage */
    if((baseName != fullName) && (baseName != fullNameBuffer)) {
        uprv_free(baseName);
    }
    baseName = nullptr;
    if(fullName != fullNameBuffer) {
        uprv_free(fullName);
        fullName = fullNameBuffer;
    }
    *fullNameBuffer = 0;
    *language = 0;
    *script = 0;
    *country = 0;
    fIsBogus = true;
    variantBegin = 0;
}

const Locale& U_EXPORT2
Locale::getDefault()
{
    {
        Mutex lock(&gDefaultLocaleMutex);
        if (gDefaultLocale != nullptr) {
            return *gDefaultLocale;
        }
    }
    UErrorCode status = U_ZERO_ERROR;
    return *locale_set_default_internal(nullptr, status);
}



void U_EXPORT2
Locale::setDefault( const   Locale&     newLocale,
                            UErrorCode&  status)
{
    if (U_FAILURE(status)) {
        return;
    }

    /* Set the default from the full name string of the supplied locale.
     * This is a convenient way to access the default locale caching mechanisms.
     */
    const char *localeID = newLocale.getName();
    locale_set_default_internal(localeID, status);
}

void
Locale::addLikelySubtags(UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }

    CharString maximizedLocaleID = ulocimp_addLikelySubtags(fullName, status);

    if (U_FAILURE(status)) {
        return;
    }

    init(maximizedLocaleID.data(), /*canonicalize=*/false);
    if (isBogus()) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
    }
}

void
Locale::minimizeSubtags(UErrorCode& status) {
    Locale::minimizeSubtags(false, status);
}
void
Locale::minimizeSubtags(bool favorScript, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }

    CharString minimizedLocaleID = ulocimp_minimizeSubtags(fullName, favorScript, status);

    if (U_FAILURE(status)) {
        return;
    }

    init(minimizedLocaleID.data(), /*canonicalize=*/false);
    if (isBogus()) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
    }
}

void
Locale::canonicalize(UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }
    if (isBogus()) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    CharString uncanonicalized(fullName, status);
    if (U_FAILURE(status)) {
        return;
    }
    init(uncanonicalized.data(), /*canonicalize=*/true);
    if (isBogus()) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
    }
}

Locale U_EXPORT2
Locale::forLanguageTag(StringPiece tag, UErrorCode& status)
{
    Locale result(Locale::eBOGUS);

    if (U_FAILURE(status)) {
        return result;
    }

    // If a BCP 47 language tag is passed as the language parameter to the
    // normal Locale constructor, it will actually fall back to invoking
    // uloc_forLanguageTag() to parse it if it somehow is able to detect that
    // the string actually is BCP 47. This works well for things like strings
    // using BCP 47 extensions, but it does not at all work for things like
    // legacy language tags (marked as “Type: grandfathered” in BCP 47,
    // e.g., "en-GB-oed") which are possible to also
    // interpret as ICU locale IDs and because of that won't trigger the BCP 47
    // parsing. Therefore the code here explicitly calls uloc_forLanguageTag()
    // and then Locale::init(), instead of just calling the normal constructor.

    int32_t parsedLength;
    CharString localeID = ulocimp_forLanguageTag(
            tag.data(),
            tag.length(),
            &parsedLength,
            status);

    if (U_FAILURE(status)) {
        return result;
    }

    if (parsedLength != tag.size()) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return result;
    }

    result.init(localeID.data(), /*canonicalize=*/false);
    if (result.isBogus()) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
    }
    return result;
}

void
Locale::toLanguageTag(ByteSink& sink, UErrorCode& status) const
{
    if (U_FAILURE(status)) {
        return;
    }

    if (fIsBogus) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    ulocimp_toLanguageTag(fullName, sink, /*strict=*/false, status);
}

Locale U_EXPORT2
Locale::createFromName (const char *name)
{
    if (name) {
        Locale l("");
        l.init(name, false);
        return l;
    }
    else {
        return getDefault();
    }
}

Locale U_EXPORT2
Locale::createCanonical(const char* name) {
    Locale loc("");
    loc.init(name, true);
    return loc;
}

const char *
Locale::getISO3Language() const
{
    return uloc_getISO3Language(fullName);
}


const char *
Locale::getISO3Country() const
{
    return uloc_getISO3Country(fullName);
}

/**
 * Return the LCID value as specified in the "LocaleID" resource for this
 * locale.  The LocaleID must be expressed as a hexadecimal number, from
 * one to four digits.  If the LocaleID resource is not present, or is
 * in an incorrect format, 0 is returned.  The LocaleID is for use in
 * Windows (it is an LCID), but is available on all platforms.
 */
uint32_t
Locale::getLCID() const
{
    return uloc_getLCID(fullName);
}

const char* const* U_EXPORT2 Locale::getISOCountries()
{
    return uloc_getISOCountries();
}

const char* const* U_EXPORT2 Locale::getISOLanguages()
{
    return uloc_getISOLanguages();
}

// Set the locale's data based on a posix id.
void Locale::setFromPOSIXID(const char *posixID)
{
    init(posixID, true);
}

const Locale & U_EXPORT2
Locale::getRoot()
{
    return getLocale(eROOT);
}

const Locale & U_EXPORT2
Locale::getEnglish()
{
    return getLocale(eENGLISH);
}

const Locale & U_EXPORT2
Locale::getFrench()
{
    return getLocale(eFRENCH);
}

const Locale & U_EXPORT2
Locale::getGerman()
{
    return getLocale(eGERMAN);
}

const Locale & U_EXPORT2
Locale::getItalian()
{
    return getLocale(eITALIAN);
}

const Locale & U_EXPORT2
Locale::getJapanese()
{
    return getLocale(eJAPANESE);
}

const Locale & U_EXPORT2
Locale::getKorean()
{
    return getLocale(eKOREAN);
}

const Locale & U_EXPORT2
Locale::getChinese()
{
    return getLocale(eCHINESE);
}

const Locale & U_EXPORT2
Locale::getSimplifiedChinese()
{
    return getLocale(eCHINA);
}

const Locale & U_EXPORT2
Locale::getTraditionalChinese()
{
    return getLocale(eTAIWAN);
}


const Locale & U_EXPORT2
Locale::getFrance()
{
    return getLocale(eFRANCE);
}

const Locale & U_EXPORT2
Locale::getGermany()
{
    return getLocale(eGERMANY);
}

const Locale & U_EXPORT2
Locale::getItaly()
{
    return getLocale(eITALY);
}

const Locale & U_EXPORT2
Locale::getJapan()
{
    return getLocale(eJAPAN);
}

const Locale & U_EXPORT2
Locale::getKorea()
{
    return getLocale(eKOREA);
}

const Locale & U_EXPORT2
Locale::getChina()
{
    return getLocale(eCHINA);
}

const Locale & U_EXPORT2
Locale::getPRC()
{
    return getLocale(eCHINA);
}

const Locale & U_EXPORT2
Locale::getTaiwan()
{
    return getLocale(eTAIWAN);
}

const Locale & U_EXPORT2
Locale::getUK()
{
    return getLocale(eUK);
}

const Locale & U_EXPORT2
Locale::getUS()
{
    return getLocale(eUS);
}

const Locale & U_EXPORT2
Locale::getCanada()
{
    return getLocale(eCANADA);
}

const Locale & U_EXPORT2
Locale::getCanadaFrench()
{
    return getLocale(eCANADA_FRENCH);
}

const Locale &
Locale::getLocale(int locid)
{
    Locale *localeCache = getLocaleCache();
    U_ASSERT((locid < eMAX_LOCALES)&&(locid>=0));
    if (localeCache == nullptr) {
        // Failure allocating the locale cache.
        //   The best we can do is return a nullptr reference.
        locid = 0;
    }
    return localeCache[locid]; /*operating on nullptr*/
}

/*
This function is defined this way in order to get around static
initialization and static destruction.
 */
Locale *
Locale::getLocaleCache()
{
    UErrorCode status = U_ZERO_ERROR;
    umtx_initOnce(gLocaleCacheInitOnce, locale_init, status);
    return gLocaleCache;
}

class KeywordEnumeration : public StringEnumeration {
protected:
    CharString keywords;
private:
    const char *current;
    static const char fgClassID;

public:
    static UClassID U_EXPORT2 getStaticClassID() { return (UClassID)&fgClassID; }
    virtual UClassID getDynamicClassID() const override { return getStaticClassID(); }
public:
    KeywordEnumeration(const char *keys, int32_t keywordLen, int32_t currentIndex, UErrorCode &status)
        : keywords(), current(keywords.data()) {
        if(U_SUCCESS(status) && keywordLen != 0) {
            if(keys == nullptr || keywordLen < 0) {
                status = U_ILLEGAL_ARGUMENT_ERROR;
            } else {
                keywords.append(keys, keywordLen, status);
                current = keywords.data() + currentIndex;
            }
        }
    }

    virtual ~KeywordEnumeration();

    virtual StringEnumeration * clone() const override
    {
        UErrorCode status = U_ZERO_ERROR;
        return new KeywordEnumeration(
                keywords.data(), keywords.length(),
                (int32_t)(current - keywords.data()), status);
    }

    virtual int32_t count(UErrorCode& status) const override {
        if (U_FAILURE(status)) { return 0; }
        const char *kw = keywords.data();
        int32_t result = 0;
        while(*kw) {
            result++;
            kw += uprv_strlen(kw)+1;
        }
        return result;
    }

    virtual const char* next(int32_t* resultLength, UErrorCode& status) override {
        const char* result;
        int32_t len;
        if(U_SUCCESS(status) && *current != 0) {
            result = current;
            len = (int32_t)uprv_strlen(current);
            current += len+1;
            if(resultLength != nullptr) {
                *resultLength = len;
            }
        } else {
            if(resultLength != nullptr) {
                *resultLength = 0;
            }
            result = nullptr;
        }
        return result;
    }

    virtual const UnicodeString* snext(UErrorCode& status) override {
        if (U_FAILURE(status)) { return nullptr; }
        int32_t resultLength = 0;
        const char *s = next(&resultLength, status);
        return setChars(s, resultLength, status);
    }

    virtual void reset(UErrorCode& status) override {
        if (U_FAILURE(status)) { return; }
        current = keywords.data();
    }
};

const char KeywordEnumeration::fgClassID = '\0';

// Out-of-line virtual destructor to serve as the "key function".
KeywordEnumeration::~KeywordEnumeration() = default;

// A wrapper around KeywordEnumeration that calls uloc_toUnicodeLocaleKey() in
// the next() method for each keyword before returning it.
class UnicodeKeywordEnumeration : public KeywordEnumeration {
public:
    using KeywordEnumeration::KeywordEnumeration;
    virtual ~UnicodeKeywordEnumeration();

    virtual const char* next(int32_t* resultLength, UErrorCode& status) override {
        const char* legacy_key = KeywordEnumeration::next(nullptr, status);
        while (U_SUCCESS(status) && legacy_key != nullptr) {
            const char* key = uloc_toUnicodeLocaleKey(legacy_key);
            if (key != nullptr) {
                if (resultLength != nullptr) {
                    *resultLength = static_cast<int32_t>(uprv_strlen(key));
                }
                return key;
            }
            // Not a Unicode keyword, could be a t, x or other, continue to look at the next one.
            legacy_key = KeywordEnumeration::next(nullptr, status);
        }
        if (resultLength != nullptr) *resultLength = 0;
        return nullptr;
    }
    virtual int32_t count(UErrorCode& status) const override {
        if (U_FAILURE(status)) { return 0; }
        const char *kw = keywords.data();
        int32_t result = 0;
        while(*kw) {
            if (uloc_toUnicodeLocaleKey(kw) != nullptr) {
                result++;
            }
            kw += uprv_strlen(kw)+1;
        }
        return result;
    }
};

// Out-of-line virtual destructor to serve as the "key function".
UnicodeKeywordEnumeration::~UnicodeKeywordEnumeration() = default;

StringEnumeration *
Locale::createKeywords(UErrorCode &status) const
{
    StringEnumeration *result = nullptr;

    if (U_FAILURE(status)) {
        return result;
    }

    const char* variantStart = uprv_strchr(fullName, '@');
    const char* assignment = uprv_strchr(fullName, '=');
    if(variantStart) {
        if(assignment > variantStart) {
            CharString keywords = ulocimp_getKeywords(variantStart + 1, '@', false, status);
            if (U_SUCCESS(status) && !keywords.isEmpty()) {
                result = new KeywordEnumeration(keywords.data(), keywords.length(), 0, status);
                if (!result) {
                    status = U_MEMORY_ALLOCATION_ERROR;
                }
            }
        } else {
            status = U_INVALID_FORMAT_ERROR;
        }
    }
    return result;
}

StringEnumeration *
Locale::createUnicodeKeywords(UErrorCode &status) const
{
    StringEnumeration *result = nullptr;

    if (U_FAILURE(status)) {
        return result;
    }

    const char* variantStart = uprv_strchr(fullName, '@');
    const char* assignment = uprv_strchr(fullName, '=');
    if(variantStart) {
        if(assignment > variantStart) {
            CharString keywords = ulocimp_getKeywords(variantStart + 1, '@', false, status);
            if (U_SUCCESS(status) && !keywords.isEmpty()) {
                result = new UnicodeKeywordEnumeration(keywords.data(), keywords.length(), 0, status);
                if (!result) {
                    status = U_MEMORY_ALLOCATION_ERROR;
                }
            }
        } else {
            status = U_INVALID_FORMAT_ERROR;
        }
    }
    return result;
}

int32_t
Locale::getKeywordValue(const char* keywordName, char *buffer, int32_t bufLen, UErrorCode &status) const
{
    return uloc_getKeywordValue(fullName, keywordName, buffer, bufLen, &status);
}

void
Locale::getKeywordValue(StringPiece keywordName, ByteSink& sink, UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return;
    }

    if (fIsBogus) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    // TODO: Remove the need for a const char* to a NUL terminated buffer.
    const CharString keywordName_nul(keywordName, status);
    if (U_FAILURE(status)) {
        return;
    }

    ulocimp_getKeywordValue(fullName, keywordName_nul.data(), sink, status);
}

void
Locale::getUnicodeKeywordValue(StringPiece keywordName,
                               ByteSink& sink,
                               UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return;
    }

    // TODO: Remove the need for a const char* to a NUL terminated buffer.
    const CharString keywordName_nul(keywordName, status);
    if (U_FAILURE(status)) {
        return;
    }

    const char* legacy_key = uloc_toLegacyKey(keywordName_nul.data());
    if (legacy_key == nullptr) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    auto legacy_value = getKeywordValue<CharString>(legacy_key, status);

    if (U_FAILURE(status)) {
        return;
    }

    const char* unicode_value = uloc_toUnicodeLocaleType(
            keywordName_nul.data(), legacy_value.data());

    if (unicode_value == nullptr) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    sink.Append(unicode_value, static_cast<int32_t>(uprv_strlen(unicode_value)));
}

void
Locale::setKeywordValue(const char* keywordName, const char* keywordValue, UErrorCode &status)
{
    if (U_FAILURE(status)) {
        return;
    }
    if (status == U_STRING_NOT_TERMINATED_WARNING) {
        status = U_ZERO_ERROR;
    }
    int32_t bufferLength = uprv_max((int32_t)(uprv_strlen(fullName) + 1), ULOC_FULLNAME_CAPACITY);
    int32_t newLength = uloc_setKeywordValue(keywordName, keywordValue, fullName,
                                             bufferLength, &status) + 1;
    U_ASSERT(status != U_STRING_NOT_TERMINATED_WARNING);
    /* Handle the case the current buffer is not enough to hold the new id */
    if (status == U_BUFFER_OVERFLOW_ERROR) {
        U_ASSERT(newLength > bufferLength);
        char* newFullName = (char *)uprv_malloc(newLength);
        if (newFullName == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
        uprv_strcpy(newFullName, fullName);
        if (fullName != fullNameBuffer) {
            // if full Name is already on the heap, need to free it.
            uprv_free(fullName);
            if (baseName == fullName) {
                baseName = newFullName; // baseName should not point to freed memory.
            }
        }
        fullName = newFullName;
        status = U_ZERO_ERROR;
        uloc_setKeywordValue(keywordName, keywordValue, fullName, newLength, &status);
        U_ASSERT(status != U_STRING_NOT_TERMINATED_WARNING);
    } else {
        U_ASSERT(newLength <= bufferLength);
    }
    if (U_SUCCESS(status) && baseName == fullName) {
        // May have added the first keyword, meaning that the fullName is no longer also the baseName.
        initBaseName(status);
    }
}

void
Locale::setKeywordValue(StringPiece keywordName,
                        StringPiece keywordValue,
                        UErrorCode& status) {
    if (U_FAILURE(status)) { return; }
    // TODO: Remove the need for a const char* to a NUL terminated buffer.
    const CharString keywordName_nul(keywordName, status);
    const CharString keywordValue_nul(keywordValue, status);
    setKeywordValue(keywordName_nul.data(), keywordValue_nul.data(), status);
}

void
Locale::setUnicodeKeywordValue(StringPiece keywordName,
                               StringPiece keywordValue,
                               UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }

    // TODO: Remove the need for a const char* to a NUL terminated buffer.
    const CharString keywordName_nul(keywordName, status);
    const CharString keywordValue_nul(keywordValue, status);
    if (U_FAILURE(status)) {
        return;
    }

    const char* legacy_key = uloc_toLegacyKey(keywordName_nul.data());
    if (legacy_key == nullptr) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    const char* legacy_value = nullptr;

    if (!keywordValue_nul.isEmpty()) {
        legacy_value =
            uloc_toLegacyType(keywordName_nul.data(), keywordValue_nul.data());

        if (legacy_value == nullptr) {
            status = U_ILLEGAL_ARGUMENT_ERROR;
            return;
        }
    }

    setKeywordValue(legacy_key, legacy_value, status);
}

const char *
Locale::getBaseName() const {
    return baseName;
}

Locale::Iterator::~Iterator() = default;

//eof
U_NAMESPACE_END
