// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
*   Copyright (C) 1997-2016, International Business Machines
*   Corporation and others.  All Rights Reserved.
**********************************************************************
*
* File ULOC.CPP
*
* Modification History:
*
*   Date        Name        Description
*   04/01/97    aliu        Creation.
*   08/21/98    stephen     JDK 1.2 sync
*   12/08/98    rtg         New Locale implementation and C API
*   03/15/99    damiba      overhaul.
*   04/06/99    stephen     changed setDefault() to realloc and copy
*   06/14/99    stephen     Changed calls to ures_open for new params
*   07/21/99    stephen     Modified setDefault() to propagate to C++
*   05/14/04    alan        7 years later: refactored, cleaned up, fixed bugs,
*                           brought canonicalization code into line with spec
*****************************************************************************/

/*
   POSIX's locale format, from putil.c: [no spaces]

     ll [ _CC ] [ . MM ] [ @ VV]

     l = lang, C = ctry, M = charmap, V = variant
*/

#include <optional>

#include "unicode/bytestream.h"
#include "unicode/errorcode.h"
#include "unicode/stringpiece.h"
#include "unicode/utypes.h"
#include "unicode/ustring.h"
#include "unicode/uloc.h"

#include "bytesinkutil.h"
#include "putilimp.h"
#include "ustr_imp.h"
#include "ulocimp.h"
#include "umutex.h"
#include "cstring.h"
#include "cmemory.h"
#include "locmap.h"
#include "uarrsort.h"
#include "uenumimp.h"
#include "uassert.h"
#include "charstr.h"

U_NAMESPACE_USE

/* ### Declarations **************************************************/

/* Locale stuff from locid.cpp */
U_CFUNC void locale_set_default(const char *id);
U_CFUNC const char *locale_get_default();

namespace {

/* ### Data tables **************************************************/

/**
 * Table of language codes, both 2- and 3-letter, with preference
 * given to 2-letter codes where possible.  Includes 3-letter codes
 * that lack a 2-letter equivalent.
 *
 * This list must be in sorted order.  This list is returned directly
 * to the user by some API.
 *
 * This list must be kept in sync with LANGUAGES_3, with corresponding
 * entries matched.
 *
 * This table should be terminated with a nullptr entry, followed by a
 * second list, and another nullptr entry.  The first list is visible to
 * user code when this array is returned by API.  The second list
 * contains codes we support, but do not expose through user API.
 *
 * Notes
 *
 * Tables updated per http://lcweb.loc.gov/standards/iso639-2/ to
 * include the revisions up to 2001/7/27 *CWB*
 *
 * The 3 character codes are the terminology codes like RFC 3066.  This
 * is compatible with prior ICU codes
 *
 * "in" "iw" "ji" "jw" & "sh" have been withdrawn but are still in the
 * table but now at the end of the table because 3 character codes are
 * duplicates.  This avoids bad searches going from 3 to 2 character
 * codes.
 *
 * The range qaa-qtz is reserved for local use
 */
/* Generated using org.unicode.cldr.icu.GenerateISO639LanguageTables */
/* ISO639 table version is 20150505 */
/* Subsequent hand addition of selected languages */
constexpr const char* LANGUAGES[] = {
    "aa",  "ab",  "ace", "ach", "ada", "ady", "ae",  "aeb",
    "af",  "afh", "agq", "ain", "ak",  "akk", "akz", "ale",
    "aln", "alt", "am",  "an",  "ang", "anp", "ar",  "arc",
    "arn", "aro", "arp", "arq", "ars", "arw", "ary", "arz", "as",
    "asa", "ase", "ast", "av",  "avk", "awa", "ay",  "az",
    "ba",  "bal", "ban", "bar", "bas", "bax", "bbc", "bbj",
    "be",  "bej", "bem", "bew", "bez", "bfd", "bfq", "bg",
    "bgc", "bgn", "bho", "bi",  "bik", "bin", "bjn", "bkm", "bla",
    "blo", "bm",  "bn",  "bo",  "bpy", "bqi", "br",  "bra", "brh",
    "brx", "bs",  "bss", "bua", "bug", "bum", "byn", "byv",
    "ca",  "cad", "car", "cay", "cch", "ccp", "ce",  "ceb", "cgg",
    "ch",  "chb", "chg", "chk", "chm", "chn", "cho", "chp",
    "chr", "chy", "ckb", "co",  "cop", "cps", "cr",  "crh",
    "cs",  "csb", "csw", "cu",  "cv",  "cy",
    "da",  "dak", "dar", "dav", "de",  "del", "den", "dgr",
    "din", "dje", "doi", "dsb", "dtp", "dua", "dum", "dv",
    "dyo", "dyu", "dz",  "dzg",
    "ebu", "ee",  "efi", "egl", "egy", "eka", "el",  "elx",
    "en",  "enm", "eo",  "es",  "esu", "et",  "eu",  "ewo",
    "ext",
    "fa",  "fan", "fat", "ff",  "fi",  "fil", "fit", "fj",
    "fo",  "fon", "fr",  "frc", "frm", "fro", "frp", "frr",
    "frs", "fur", "fy",
    "ga",  "gaa", "gag", "gan", "gay", "gba", "gbz", "gd",
    "gez", "gil", "gl",  "glk", "gmh", "gn",  "goh", "gom",
    "gon", "gor", "got", "grb", "grc", "gsw", "gu",  "guc",
    "gur", "guz", "gv",  "gwi",
    "ha",  "hai", "hak", "haw", "he",  "hi",  "hif", "hil",
    "hit", "hmn", "ho",  "hr",  "hsb", "hsn", "ht",  "hu",
    "hup", "hy",  "hz",
    "ia",  "iba", "ibb", "id",  "ie",  "ig",  "ii",  "ik",
    "ilo", "inh", "io",  "is",  "it",  "iu",  "izh",
    "ja",  "jam", "jbo", "jgo", "jmc", "jpr", "jrb", "jut",
    "jv",
    "ka",  "kaa", "kab", "kac", "kaj", "kam", "kaw", "kbd",
    "kbl", "kcg", "kde", "kea", "ken", "kfo", "kg",  "kgp",
    "kha", "kho", "khq", "khw", "ki",  "kiu", "kj",  "kk",
    "kkj", "kl",  "kln", "km",  "kmb", "kn",  "ko",  "koi",
    "kok", "kos", "kpe", "kr",  "krc", "kri", "krj", "krl",
    "kru", "ks",  "ksb", "ksf", "ksh", "ku",  "kum", "kut",
    "kv",  "kw",  "kxv", "ky",
    "la",  "lad", "lag", "lah", "lam", "lb",  "lez", "lfn",
    "lg",  "li",  "lij", "liv", "lkt", "lmo", "ln",  "lo",
    "lol", "loz", "lrc", "lt",  "ltg", "lu",  "lua", "lui",
    "lun", "luo", "lus", "luy", "lv",  "lzh", "lzz",
    "mad", "maf", "mag", "mai", "mak", "man", "mas", "mde",
    "mdf", "mdh", "mdr", "men", "mer", "mfe", "mg",  "mga",
    "mgh", "mgo", "mh",  "mi",  "mic", "min", "mis", "mk",
    "ml",  "mn",  "mnc", "mni",
    "moh", "mos", "mr",  "mrj",
    "ms",  "mt",  "mua", "mul", "mus", "mwl", "mwr", "mwv",
    "my",  "mye", "myv", "mzn",
    "na",  "nan", "nap", "naq", "nb",  "nd",  "nds", "ne",
    "new", "ng",  "nia", "niu", "njo", "nl",  "nmg", "nn",
    "nnh", "no",  "nog", "non", "nov", "nqo", "nr",  "nso",
    "nus", "nv",  "nwc", "ny",  "nym", "nyn", "nyo", "nzi",
    "oc",  "oj",  "om",  "or",  "os",  "osa", "ota",
    "pa",  "pag", "pal", "pam", "pap", "pau", "pcd", "pcm", "pdc",
    "pdt", "peo", "pfl", "phn", "pi",  "pl",  "pms", "pnt",
    "pon", "prg", "pro", "ps",  "pt",
    "qu",  "quc", "qug",
    "raj", "rap", "rar", "rgn", "rif", "rm",  "rn",  "ro",
    "rof", "rom", "rtm", "ru",  "rue", "rug", "rup",
    "rw",  "rwk",
    "sa",  "sad", "sah", "sam", "saq", "sas", "sat", "saz",
    "sba", "sbp", "sc",  "scn", "sco", "sd",  "sdc", "sdh",
    "se",  "see", "seh", "sei", "sel", "ses", "sg",  "sga",
    "sgs", "shi", "shn", "shu", "si",  "sid", "sk",
    "sl",  "sli", "sly", "sm",  "sma", "smj", "smn", "sms",
    "sn",  "snk", "so",  "sog", "sq",  "sr",  "srn", "srr",
    "ss",  "ssy", "st",  "stq", "su",  "suk", "sus", "sux",
    "sv",  "sw",  "swb", "syc", "syr", "szl",
    "ta",  "tcy", "te",  "tem", "teo", "ter", "tet", "tg",
    "th",  "ti",  "tig", "tiv", "tk",  "tkl", "tkr",
    "tlh", "tli", "tly", "tmh", "tn",  "to",  "tog", "tok", "tpi",
    "tr",  "tru", "trv", "ts",  "tsd", "tsi", "tt",  "ttt",
    "tum", "tvl", "tw",  "twq", "ty",  "tyv", "tzm",
    "udm", "ug",  "uga", "uk",  "umb", "und", "ur",  "uz",
    "vai", "ve",  "vec", "vep", "vi",  "vls", "vmf", "vmw",
    "vo", "vot", "vro", "vun",
    "wa",  "wae", "wal", "war", "was", "wbp", "wo",  "wuu",
    "xal", "xh",  "xmf", "xnr", "xog",
    "yao", "yap", "yav", "ybb", "yi",  "yo",  "yrl", "yue",
    "za",  "zap", "zbl", "zea", "zen", "zgh", "zh",  "zu",
    "zun", "zxx", "zza",
nullptr,
    "in",  "iw",  "ji",  "jw",  "mo",  "sh",  "swc", "tl",  /* obsolete language codes */
nullptr
};

constexpr const char* DEPRECATED_LANGUAGES[]={
    "in", "iw", "ji", "jw", "mo", nullptr, nullptr
};
constexpr const char* REPLACEMENT_LANGUAGES[]={
    "id", "he", "yi", "jv", "ro", nullptr, nullptr
};

/**
 * Table of 3-letter language codes.
 *
 * This is a lookup table used to convert 3-letter language codes to
 * their 2-letter equivalent, where possible.  It must be kept in sync
 * with LANGUAGES.  For all valid i, LANGUAGES[i] must refer to the
 * same language as LANGUAGES_3[i].  The commented-out lines are
 * copied from LANGUAGES to make eyeballing this baby easier.
 *
 * Where a 3-letter language code has no 2-letter equivalent, the
 * 3-letter code occupies both LANGUAGES[i] and LANGUAGES_3[i].
 *
 * This table should be terminated with a nullptr entry, followed by a
 * second list, and another nullptr entry.  The two lists correspond to
 * the two lists in LANGUAGES.
 */
/* Generated using org.unicode.cldr.icu.GenerateISO639LanguageTables */
/* ISO639 table version is 20150505 */
/* Subsequent hand addition of selected languages */
constexpr const char* LANGUAGES_3[] = {
    "aar", "abk", "ace", "ach", "ada", "ady", "ave", "aeb",
    "afr", "afh", "agq", "ain", "aka", "akk", "akz", "ale",
    "aln", "alt", "amh", "arg", "ang", "anp", "ara", "arc",
    "arn", "aro", "arp", "arq", "ars", "arw", "ary", "arz", "asm",
    "asa", "ase", "ast", "ava", "avk", "awa", "aym", "aze",
    "bak", "bal", "ban", "bar", "bas", "bax", "bbc", "bbj",
    "bel", "bej", "bem", "bew", "bez", "bfd", "bfq", "bul",
    "bgc", "bgn", "bho", "bis", "bik", "bin", "bjn", "bkm", "bla",
    "blo", "bam", "ben", "bod", "bpy", "bqi", "bre", "bra", "brh",
    "brx", "bos", "bss", "bua", "bug", "bum", "byn", "byv",
    "cat", "cad", "car", "cay", "cch", "ccp", "che", "ceb", "cgg",
    "cha", "chb", "chg", "chk", "chm", "chn", "cho", "chp",
    "chr", "chy", "ckb", "cos", "cop", "cps", "cre", "crh",
    "ces", "csb", "csw", "chu", "chv", "cym",
    "dan", "dak", "dar", "dav", "deu", "del", "den", "dgr",
    "din", "dje", "doi", "dsb", "dtp", "dua", "dum", "div",
    "dyo", "dyu", "dzo", "dzg",
    "ebu", "ewe", "efi", "egl", "egy", "eka", "ell", "elx",
    "eng", "enm", "epo", "spa", "esu", "est", "eus", "ewo",
    "ext",
    "fas", "fan", "fat", "ful", "fin", "fil", "fit", "fij",
    "fao", "fon", "fra", "frc", "frm", "fro", "frp", "frr",
    "frs", "fur", "fry",
    "gle", "gaa", "gag", "gan", "gay", "gba", "gbz", "gla",
    "gez", "gil", "glg", "glk", "gmh", "grn", "goh", "gom",
    "gon", "gor", "got", "grb", "grc", "gsw", "guj", "guc",
    "gur", "guz", "glv", "gwi",
    "hau", "hai", "hak", "haw", "heb", "hin", "hif", "hil",
    "hit", "hmn", "hmo", "hrv", "hsb", "hsn", "hat", "hun",
    "hup", "hye", "her",
    "ina", "iba", "ibb", "ind", "ile", "ibo", "iii", "ipk",
    "ilo", "inh", "ido", "isl", "ita", "iku", "izh",
    "jpn", "jam", "jbo", "jgo", "jmc", "jpr", "jrb", "jut",
    "jav",
    "kat", "kaa", "kab", "kac", "kaj", "kam", "kaw", "kbd",
    "kbl", "kcg", "kde", "kea", "ken", "kfo", "kon", "kgp",
    "kha", "kho", "khq", "khw", "kik", "kiu", "kua", "kaz",
    "kkj", "kal", "kln", "khm", "kmb", "kan", "kor", "koi",
    "kok", "kos", "kpe", "kau", "krc", "kri", "krj", "krl",
    "kru", "kas", "ksb", "ksf", "ksh", "kur", "kum", "kut",
    "kom", "cor", "kxv", "kir",
    "lat", "lad", "lag", "lah", "lam", "ltz", "lez", "lfn",
    "lug", "lim", "lij", "liv", "lkt", "lmo", "lin", "lao",
    "lol", "loz", "lrc", "lit", "ltg", "lub", "lua", "lui",
    "lun", "luo", "lus", "luy", "lav", "lzh", "lzz",
    "mad", "maf", "mag", "mai", "mak", "man", "mas", "mde",
    "mdf", "mdh", "mdr", "men", "mer", "mfe", "mlg", "mga",
    "mgh", "mgo", "mah", "mri", "mic", "min", "mis", "mkd",
    "mal", "mon", "mnc", "mni",
    "moh", "mos", "mar", "mrj",
    "msa", "mlt", "mua", "mul", "mus", "mwl", "mwr", "mwv",
    "mya", "mye", "myv", "mzn",
    "nau", "nan", "nap", "naq", "nob", "nde", "nds", "nep",
    "new", "ndo", "nia", "niu", "njo", "nld", "nmg", "nno",
    "nnh", "nor", "nog", "non", "nov", "nqo", "nbl", "nso",
    "nus", "nav", "nwc", "nya", "nym", "nyn", "nyo", "nzi",
    "oci", "oji", "orm", "ori", "oss", "osa", "ota",
    "pan", "pag", "pal", "pam", "pap", "pau", "pcd", "pcm", "pdc",
    "pdt", "peo", "pfl", "phn", "pli", "pol", "pms", "pnt",
    "pon", "prg", "pro", "pus", "por",
    "que", "quc", "qug",
    "raj", "rap", "rar", "rgn", "rif", "roh", "run", "ron",
    "rof", "rom", "rtm", "rus", "rue", "rug", "rup",
    "kin", "rwk",
    "san", "sad", "sah", "sam", "saq", "sas", "sat", "saz",
    "sba", "sbp", "srd", "scn", "sco", "snd", "sdc", "sdh",
    "sme", "see", "seh", "sei", "sel", "ses", "sag", "sga",
    "sgs", "shi", "shn", "shu", "sin", "sid", "slk",
    "slv", "sli", "sly", "smo", "sma", "smj", "smn", "sms",
    "sna", "snk", "som", "sog", "sqi", "srp", "srn", "srr",
    "ssw", "ssy", "sot", "stq", "sun", "suk", "sus", "sux",
    "swe", "swa", "swb", "syc", "syr", "szl",
    "tam", "tcy", "tel", "tem", "teo", "ter", "tet", "tgk",
    "tha", "tir", "tig", "tiv", "tuk", "tkl", "tkr",
    "tlh", "tli", "tly", "tmh", "tsn", "ton", "tog", "tok", "tpi",
    "tur", "tru", "trv", "tso", "tsd", "tsi", "tat", "ttt",
    "tum", "tvl", "twi", "twq", "tah", "tyv", "tzm",
    "udm", "uig", "uga", "ukr", "umb", "und", "urd", "uzb",
    "vai", "ven", "vec", "vep", "vie", "vls", "vmf", "vmw",
    "vol", "vot", "vro", "vun",
    "wln", "wae", "wal", "war", "was", "wbp", "wol", "wuu",
    "xal", "xho", "xmf", "xnr", "xog",
    "yao", "yap", "yav", "ybb", "yid", "yor", "yrl", "yue",
    "zha", "zap", "zbl", "zea", "zen", "zgh", "zho", "zul",
    "zun", "zxx", "zza",
nullptr,
/*  "in",  "iw",  "ji",  "jw",  "mo",  "sh",  "swc", "tl",  */
    "ind", "heb", "yid", "jaw", "mol", "srp", "swc", "tgl",
nullptr
};

/**
 * Table of 2-letter country codes.
 *
 * This list must be in sorted order.  This list is returned directly
 * to the user by some API.
 *
 * This list must be kept in sync with COUNTRIES_3, with corresponding
 * entries matched.
 *
 * This table should be terminated with a nullptr entry, followed by a
 * second list, and another nullptr entry.  The first list is visible to
 * user code when this array is returned by API.  The second list
 * contains codes we support, but do not expose through user API.
 *
 * Notes:
 *
 * ZR(ZAR) is now CD(COD) and FX(FXX) is PS(PSE) as per
 * http://www.evertype.com/standards/iso3166/iso3166-1-en.html added
 * new codes keeping the old ones for compatibility updated to include
 * 1999/12/03 revisions *CWB*
 *
 * RO(ROM) is now RO(ROU) according to
 * http://www.iso.org/iso/en/prods-services/iso3166ma/03updates-on-iso-3166/nlv3e-rou.html
 */
constexpr const char* COUNTRIES[] = {
    "AD",  "AE",  "AF",  "AG",  "AI",  "AL",  "AM",
    "AO",  "AQ",  "AR",  "AS",  "AT",  "AU",  "AW",  "AX",  "AZ",
    "BA",  "BB",  "BD",  "BE",  "BF",  "BG",  "BH",  "BI",
    "BJ",  "BL",  "BM",  "BN",  "BO",  "BQ",  "BR",  "BS",  "BT",  "BV",
    "BW",  "BY",  "BZ",  "CA",  "CC",  "CD",  "CF",  "CG",
    "CH",  "CI",  "CK",  "CL",  "CM",  "CN",  "CO",  "CQ",  "CR",
    "CU",  "CV",  "CW",  "CX",  "CY",  "CZ",  "DE",  "DG",  "DJ",  "DK",
    "DM",  "DO",  "DZ",  "EA",  "EC",  "EE",  "EG",  "EH",  "ER",
    "ES",  "ET",  "FI",  "FJ",  "FK",  "FM",  "FO",  "FR",
    "GA",  "GB",  "GD",  "GE",  "GF",  "GG",  "GH",  "GI",  "GL",
    "GM",  "GN",  "GP",  "GQ",  "GR",  "GS",  "GT",  "GU",
    "GW",  "GY",  "HK",  "HM",  "HN",  "HR",  "HT",  "HU",
    "IC",  "ID",  "IE",  "IL",  "IM",  "IN",  "IO",  "IQ",  "IR",  "IS",
    "IT",  "JE",  "JM",  "JO",  "JP",  "KE",  "KG",  "KH",  "KI",
    "KM",  "KN",  "KP",  "KR",  "KW",  "KY",  "KZ",  "LA",
    "LB",  "LC",  "LI",  "LK",  "LR",  "LS",  "LT",  "LU",
    "LV",  "LY",  "MA",  "MC",  "MD",  "ME",  "MF",  "MG",  "MH",  "MK",
    "ML",  "MM",  "MN",  "MO",  "MP",  "MQ",  "MR",  "MS",
    "MT",  "MU",  "MV",  "MW",  "MX",  "MY",  "MZ",  "NA",
    "NC",  "NE",  "NF",  "NG",  "NI",  "NL",  "NO",  "NP",
    "NR",  "NU",  "NZ",  "OM",  "PA",  "PE",  "PF",  "PG",
    "PH",  "PK",  "PL",  "PM",  "PN",  "PR",  "PS",  "PT",
    "PW",  "PY",  "QA",  "RE",  "RO",  "RS",  "RU",  "RW",  "SA",
    "SB",  "SC",  "SD",  "SE",  "SG",  "SH",  "SI",  "SJ",
    "SK",  "SL",  "SM",  "SN",  "SO",  "SR",  "SS",  "ST",  "SV",
    "SX",  "SY",  "SZ",  "TC",  "TD",  "TF",  "TG",  "TH",  "TJ",
    "TK",  "TL",  "TM",  "TN",  "TO",  "TR",  "TT",  "TV",
    "TW",  "TZ",  "UA",  "UG",  "UM",  "US",  "UY",  "UZ",
    "VA",  "VC",  "VE",  "VG",  "VI",  "VN",  "VU",  "WF",
    "WS",  "XK",  "YE",  "YT",  "ZA",  "ZM",  "ZW",
nullptr,
    "AN",  "BU", "CS", "FX", "RO", "SU", "TP", "YD", "YU", "ZR",   /* obsolete country codes */
nullptr
};

constexpr const char* DEPRECATED_COUNTRIES[] = {
    "AN", "BU", "CS", "DD", "DY", "FX", "HV", "NH", "RH", "SU", "TP", "UK", "VD", "YD", "YU", "ZR", nullptr, nullptr /* deprecated country list */
};
constexpr const char* REPLACEMENT_COUNTRIES[] = {
/*  "AN", "BU", "CS", "DD", "DY", "FX", "HV", "NH", "RH", "SU", "TP", "UK", "VD", "YD", "YU", "ZR" */
    "CW", "MM", "RS", "DE", "BJ", "FR", "BF", "VU", "ZW", "RU", "TL", "GB", "VN", "YE", "RS", "CD", nullptr, nullptr  /* replacement country codes */
};

/**
 * Table of 3-letter country codes.
 *
 * This is a lookup table used to convert 3-letter country codes to
 * their 2-letter equivalent.  It must be kept in sync with COUNTRIES.
 * For all valid i, COUNTRIES[i] must refer to the same country as
 * COUNTRIES_3[i].  The commented-out lines are copied from COUNTRIES
 * to make eyeballing this baby easier.
 *
 * This table should be terminated with a nullptr entry, followed by a
 * second list, and another nullptr entry.  The two lists correspond to
 * the two lists in COUNTRIES.
 */
constexpr const char* COUNTRIES_3[] = {
/*  "AD",  "AE",  "AF",  "AG",  "AI",  "AL",  "AM",      */
    "AND", "ARE", "AFG", "ATG", "AIA", "ALB", "ARM",
/*  "AO",  "AQ",  "AR",  "AS",  "AT",  "AU",  "AW",  "AX",  "AZ",     */
    "AGO", "ATA", "ARG", "ASM", "AUT", "AUS", "ABW", "ALA", "AZE",
/*  "BA",  "BB",  "BD",  "BE",  "BF",  "BG",  "BH",  "BI",     */
    "BIH", "BRB", "BGD", "BEL", "BFA", "BGR", "BHR", "BDI",
/*  "BJ",  "BL",  "BM",  "BN",  "BO",  "BQ",  "BR",  "BS",  "BT",  "BV",     */
    "BEN", "BLM", "BMU", "BRN", "BOL", "BES", "BRA", "BHS", "BTN", "BVT",
/*  "BW",  "BY",  "BZ",  "CA",  "CC",  "CD",  "CF",  "CG",     */
    "BWA", "BLR", "BLZ", "CAN", "CCK", "COD", "CAF", "COG",
/*  "CH",  "CI",  "CK",  "CL",  "CM",  "CN",  "CO",  "CQ",  "CR",     */
    "CHE", "CIV", "COK", "CHL", "CMR", "CHN", "COL", "CRQ", "CRI",
/*  "CU",  "CV",  "CW",  "CX",  "CY",  "CZ",  "DE",  "DG",  "DJ",  "DK",     */
    "CUB", "CPV", "CUW", "CXR", "CYP", "CZE", "DEU", "DGA", "DJI", "DNK",
/*  "DM",  "DO",  "DZ",  "EA",  "EC",  "EE",  "EG",  "EH",  "ER",     */
    "DMA", "DOM", "DZA", "XEA", "ECU", "EST", "EGY", "ESH", "ERI",
/*  "ES",  "ET",  "FI",  "FJ",  "FK",  "FM",  "FO",  "FR",     */
    "ESP", "ETH", "FIN", "FJI", "FLK", "FSM", "FRO", "FRA",
/*  "GA",  "GB",  "GD",  "GE",  "GF",  "GG",  "GH",  "GI",  "GL",     */
    "GAB", "GBR", "GRD", "GEO", "GUF", "GGY", "GHA", "GIB", "GRL",
/*  "GM",  "GN",  "GP",  "GQ",  "GR",  "GS",  "GT",  "GU",     */
    "GMB", "GIN", "GLP", "GNQ", "GRC", "SGS", "GTM", "GUM",
/*  "GW",  "GY",  "HK",  "HM",  "HN",  "HR",  "HT",  "HU",     */
    "GNB", "GUY", "HKG", "HMD", "HND", "HRV", "HTI", "HUN",
/*  "IC",  "ID",  "IE",  "IL",  "IM",  "IN",  "IO",  "IQ",  "IR",  "IS" */
    "XIC", "IDN", "IRL", "ISR", "IMN", "IND", "IOT", "IRQ", "IRN", "ISL",
/*  "IT",  "JE",  "JM",  "JO",  "JP",  "KE",  "KG",  "KH",  "KI",     */
    "ITA", "JEY", "JAM", "JOR", "JPN", "KEN", "KGZ", "KHM", "KIR",
/*  "KM",  "KN",  "KP",  "KR",  "KW",  "KY",  "KZ",  "LA",     */
    "COM", "KNA", "PRK", "KOR", "KWT", "CYM", "KAZ", "LAO",
/*  "LB",  "LC",  "LI",  "LK",  "LR",  "LS",  "LT",  "LU",     */
    "LBN", "LCA", "LIE", "LKA", "LBR", "LSO", "LTU", "LUX",
/*  "LV",  "LY",  "MA",  "MC",  "MD",  "ME",  "MF",  "MG",  "MH",  "MK",     */
    "LVA", "LBY", "MAR", "MCO", "MDA", "MNE", "MAF", "MDG", "MHL", "MKD",
/*  "ML",  "MM",  "MN",  "MO",  "MP",  "MQ",  "MR",  "MS",     */
    "MLI", "MMR", "MNG", "MAC", "MNP", "MTQ", "MRT", "MSR",
/*  "MT",  "MU",  "MV",  "MW",  "MX",  "MY",  "MZ",  "NA",     */
    "MLT", "MUS", "MDV", "MWI", "MEX", "MYS", "MOZ", "NAM",
/*  "NC",  "NE",  "NF",  "NG",  "NI",  "NL",  "NO",  "NP",     */
    "NCL", "NER", "NFK", "NGA", "NIC", "NLD", "NOR", "NPL",
/*  "NR",  "NU",  "NZ",  "OM",  "PA",  "PE",  "PF",  "PG",     */
    "NRU", "NIU", "NZL", "OMN", "PAN", "PER", "PYF", "PNG",
/*  "PH",  "PK",  "PL",  "PM",  "PN",  "PR",  "PS",  "PT",     */
    "PHL", "PAK", "POL", "SPM", "PCN", "PRI", "PSE", "PRT",
/*  "PW",  "PY",  "QA",  "RE",  "RO",  "RS",  "RU",  "RW",  "SA",     */
    "PLW", "PRY", "QAT", "REU", "ROU", "SRB", "RUS", "RWA", "SAU",
/*  "SB",  "SC",  "SD",  "SE",  "SG",  "SH",  "SI",  "SJ",     */
    "SLB", "SYC", "SDN", "SWE", "SGP", "SHN", "SVN", "SJM",
/*  "SK",  "SL",  "SM",  "SN",  "SO",  "SR",  "SS",  "ST",  "SV",     */
    "SVK", "SLE", "SMR", "SEN", "SOM", "SUR", "SSD", "STP", "SLV",
/*  "SX",  "SY",  "SZ",  "TC",  "TD",  "TF",  "TG",  "TH",  "TJ",     */
    "SXM", "SYR", "SWZ", "TCA", "TCD", "ATF", "TGO", "THA", "TJK",
/*  "TK",  "TL",  "TM",  "TN",  "TO",  "TR",  "TT",  "TV",     */
    "TKL", "TLS", "TKM", "TUN", "TON", "TUR", "TTO", "TUV",
/*  "TW",  "TZ",  "UA",  "UG",  "UM",  "US",  "UY",  "UZ",     */
    "TWN", "TZA", "UKR", "UGA", "UMI", "USA", "URY", "UZB",
/*  "VA",  "VC",  "VE",  "VG",  "VI",  "VN",  "VU",  "WF",     */
    "VAT", "VCT", "VEN", "VGB", "VIR", "VNM", "VUT", "WLF",
/*  "WS",  "XK",  "YE",  "YT",  "ZA",  "ZM",  "ZW",          */
    "WSM", "XKK", "YEM", "MYT", "ZAF", "ZMB", "ZWE",
nullptr,
/*  "AN",  "BU",  "CS",  "FX",  "RO", "SU",  "TP",  "YD",  "YU",  "ZR" */
    "ANT", "BUR", "SCG", "FXX", "ROM", "SUN", "TMP", "YMD", "YUG", "ZAR",
nullptr
};

typedef struct CanonicalizationMap {
    const char *id;          /* input ID */
    const char *canonicalID; /* canonicalized output ID */
} CanonicalizationMap;

/**
 * A map to canonicalize locale IDs.  This handles a variety of
 * different semantic kinds of transformations.
 */
constexpr CanonicalizationMap CANONICALIZE_MAP[] = {
    { "art__LOJBAN",    "jbo" }, /* registered name */
    { "hy__AREVELA",    "hy" }, /* Registered IANA variant */
    { "hy__AREVMDA",    "hyw" }, /* Registered IANA variant */
    { "zh__GUOYU",      "zh" }, /* registered name */
    { "zh__HAKKA",      "hak" }, /* registered name */
    { "zh__XIANG",      "hsn" }, /* registered name */
    // subtags with 3 chars won't be treated as variants.
    { "zh_GAN",         "gan" }, /* registered name */
    { "zh_MIN_NAN",     "nan" }, /* registered name */
    { "zh_WUU",         "wuu" }, /* registered name */
    { "zh_YUE",         "yue" }, /* registered name */
};

/* ### BCP47 Conversion *******************************************/
/* Gets the size of the shortest subtag in the given localeID. */
int32_t getShortestSubtagLength(const char *localeID) {
    int32_t localeIDLength = static_cast<int32_t>(uprv_strlen(localeID));
    int32_t length = localeIDLength;
    int32_t tmpLength = 0;
    int32_t i;
    bool reset = true;

    for (i = 0; i < localeIDLength; i++) {
        if (localeID[i] != '_' && localeID[i] != '-') {
            if (reset) {
                tmpLength = 0;
                reset = false;
            }
            tmpLength++;
        } else {
            if (tmpLength != 0 && tmpLength < length) {
                length = tmpLength;
            }
            reset = true;
        }
    }

    return length;
}
/* Test if the locale id has BCP47 u extension and does not have '@' */
inline bool _hasBCP47Extension(const char *id) {
    return id != nullptr && uprv_strstr(id, "@") == nullptr && getShortestSubtagLength(id) == 1;
}

/* ### Keywords **************************************************/
inline bool UPRV_ISDIGIT(char c) { return c >= '0' && c <= '9'; }
inline bool UPRV_ISALPHANUM(char c) { return uprv_isASCIILetter(c) || UPRV_ISDIGIT(c); }
/* Punctuation/symbols allowed in legacy key values */
inline bool UPRV_OK_VALUE_PUNCTUATION(char c) { return c == '_' || c == '-' || c == '+' || c == '/'; }

}  // namespace

#define ULOC_KEYWORD_BUFFER_LEN 25
#define ULOC_MAX_NO_KEYWORDS 25

U_CAPI const char * U_EXPORT2
locale_getKeywordsStart(const char *localeID) {
    const char *result = nullptr;
    if((result = uprv_strchr(localeID, '@')) != nullptr) {
        return result;
    }
#if (U_CHARSET_FAMILY == U_EBCDIC_FAMILY)
    else {
        /* We do this because the @ sign is variant, and the @ sign used on one
        EBCDIC machine won't be compiled the same way on other EBCDIC based
        machines. */
        static const uint8_t ebcdicSigns[] = { 0x7C, 0x44, 0x66, 0x80, 0xAC, 0xAE, 0xAF, 0xB5, 0xEC, 0xEF, 0x00 };
        const uint8_t *charToFind = ebcdicSigns;
        while(*charToFind) {
            if((result = uprv_strchr(localeID, *charToFind)) != nullptr) {
                return result;
            }
            charToFind++;
        }
    }
#endif
    return nullptr;
}

namespace {

/**
 * @param keywordName incoming name to be canonicalized
 * @param status return status (keyword too long)
 * @return the keyword name
 */
CharString locale_canonKeywordName(const char* keywordName, UErrorCode& status)
{
  if (U_FAILURE(status)) { return {}; }
  CharString result;

  for (; *keywordName != 0; keywordName++) {
    if (!UPRV_ISALPHANUM(*keywordName)) {
      status = U_ILLEGAL_ARGUMENT_ERROR; /* malformed keyword name */
      return {};
    }
    result.append(uprv_tolower(*keywordName), status);
  }
  if (result.isEmpty()) {
    status = U_ILLEGAL_ARGUMENT_ERROR; /* empty keyword name */
    return {};
  }

  return result;
}

typedef struct {
    char keyword[ULOC_KEYWORD_BUFFER_LEN];
    int32_t keywordLen;
    const char *valueStart;
    int32_t valueLen;
} KeywordStruct;

int32_t U_CALLCONV
compareKeywordStructs(const void * /*context*/, const void *left, const void *right) {
    const char* leftString = ((const KeywordStruct *)left)->keyword;
    const char* rightString = ((const KeywordStruct *)right)->keyword;
    return uprv_strcmp(leftString, rightString);
}

}  // namespace

U_EXPORT CharString
ulocimp_getKeywords(const char* localeID,
                    char prev,
                    bool valuesToo,
                    UErrorCode& status)
{
    return ByteSinkUtil::viaByteSinkToCharString(
        [&](ByteSink& sink, UErrorCode& status) {
            ulocimp_getKeywords(localeID,
                                prev,
                                sink,
                                valuesToo,
                                status);
        },
        status);
}

U_EXPORT void
ulocimp_getKeywords(const char* localeID,
                    char prev,
                    ByteSink& sink,
                    bool valuesToo,
                    UErrorCode& status)
{
    if (U_FAILURE(status)) { return; }

    KeywordStruct keywordList[ULOC_MAX_NO_KEYWORDS];

    int32_t maxKeywords = ULOC_MAX_NO_KEYWORDS;
    int32_t numKeywords = 0;
    const char* pos = localeID;
    const char* equalSign = nullptr;
    const char* semicolon = nullptr;
    int32_t i = 0, j, n;

    if(prev == '@') { /* start of keyword definition */
        /* we will grab pairs, trim spaces, lowercase keywords, sort and return */
        do {
            bool duplicate = false;
            /* skip leading spaces */
            while(*pos == ' ') {
                pos++;
            }
            if (!*pos) { /* handle trailing "; " */
                break;
            }
            if(numKeywords == maxKeywords) {
                status = U_INTERNAL_PROGRAM_ERROR;
                return;
            }
            equalSign = uprv_strchr(pos, '=');
            semicolon = uprv_strchr(pos, ';');
            /* lack of '=' [foo@currency] is illegal */
            /* ';' before '=' [foo@currency;collation=pinyin] is illegal */
            if(!equalSign || (semicolon && semicolon<equalSign)) {
                status = U_INVALID_FORMAT_ERROR;
                return;
            }
            /* need to normalize both keyword and keyword name */
            if(equalSign - pos >= ULOC_KEYWORD_BUFFER_LEN) {
                /* keyword name too long for internal buffer */
                status = U_INTERNAL_PROGRAM_ERROR;
                return;
            }
            for(i = 0, n = 0; i < equalSign - pos; ++i) {
                if (pos[i] != ' ') {
                    keywordList[numKeywords].keyword[n++] = uprv_tolower(pos[i]);
                }
            }

            /* zero-length keyword is an error. */
            if (n == 0) {
                status = U_INVALID_FORMAT_ERROR;
                return;
            }

            keywordList[numKeywords].keyword[n] = 0;
            keywordList[numKeywords].keywordLen = n;
            /* now grab the value part. First we skip the '=' */
            equalSign++;
            /* then we leading spaces */
            while(*equalSign == ' ') {
                equalSign++;
            }

            /* Premature end or zero-length value */
            if (!*equalSign || equalSign == semicolon) {
                status = U_INVALID_FORMAT_ERROR;
                return;
            }

            keywordList[numKeywords].valueStart = equalSign;

            pos = semicolon;
            i = 0;
            if(pos) {
                while(*(pos - i - 1) == ' ') {
                    i++;
                }
                keywordList[numKeywords].valueLen = (int32_t)(pos - equalSign - i);
                pos++;
            } else {
                i = (int32_t)uprv_strlen(equalSign);
                while(i && equalSign[i-1] == ' ') {
                    i--;
                }
                keywordList[numKeywords].valueLen = i;
            }
            /* If this is a duplicate keyword, then ignore it */
            for (j=0; j<numKeywords; ++j) {
                if (uprv_strcmp(keywordList[j].keyword, keywordList[numKeywords].keyword) == 0) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                ++numKeywords;
            }
        } while(pos);

        /* now we have a list of keywords */
        /* we need to sort it */
        uprv_sortArray(keywordList, numKeywords, sizeof(KeywordStruct), compareKeywordStructs, nullptr, false, &status);

        /* Now construct the keyword part */
        for(i = 0; i < numKeywords; i++) {
            sink.Append(keywordList[i].keyword, keywordList[i].keywordLen);
            if(valuesToo) {
                sink.Append("=", 1);
                sink.Append(keywordList[i].valueStart, keywordList[i].valueLen);
                if(i < numKeywords - 1) {
                    sink.Append(";", 1);
                }
            } else {
                sink.Append("\0", 1);
            }
        }
    }
}

U_CAPI int32_t U_EXPORT2
uloc_getKeywordValue(const char* localeID,
                     const char* keywordName,
                     char* buffer, int32_t bufferCapacity,
                     UErrorCode* status)
{
    return ByteSinkUtil::viaByteSinkToTerminatedChars(
        buffer, bufferCapacity,
        [&](ByteSink& sink, UErrorCode& status) {
            ulocimp_getKeywordValue(localeID, keywordName, sink, status);
        },
        *status);
}

U_EXPORT CharString
ulocimp_getKeywordValue(const char* localeID,
                        const char* keywordName,
                        UErrorCode& status)
{
    return ByteSinkUtil::viaByteSinkToCharString(
        [&](ByteSink& sink, UErrorCode& status) {
            ulocimp_getKeywordValue(localeID, keywordName, sink, status);
        },
        status);
}

U_EXPORT void
ulocimp_getKeywordValue(const char* localeID,
                        const char* keywordName,
                        icu::ByteSink& sink,
                        UErrorCode& status)
{
    if (U_FAILURE(status)) { return; }

    if (localeID == nullptr || keywordName == nullptr || keywordName[0] == 0) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    const char* startSearchHere = nullptr;
    const char* nextSeparator = nullptr;

    CharString tempBuffer;
    const char* tmpLocaleID;

    CharString canonKeywordName = locale_canonKeywordName(keywordName, status);
    if (U_FAILURE(status)) {
      return;
    }

    if (_hasBCP47Extension(localeID)) {
        tempBuffer = ulocimp_forLanguageTag(localeID, -1, nullptr, status);
        tmpLocaleID = U_SUCCESS(status) && !tempBuffer.isEmpty() ? tempBuffer.data() : localeID;
    } else {
        tmpLocaleID=localeID;
    }

    startSearchHere = locale_getKeywordsStart(tmpLocaleID);
    if(startSearchHere == nullptr) {
        /* no keywords, return at once */
        return;
    }

    /* find the first keyword */
    while(startSearchHere) {
        const char* keyValueTail;

        startSearchHere++; /* skip @ or ; */
        nextSeparator = uprv_strchr(startSearchHere, '=');
        if(!nextSeparator) {
            status = U_ILLEGAL_ARGUMENT_ERROR; /* key must have =value */
            return;
        }
        /* strip leading & trailing spaces (TC decided to tolerate these) */
        while(*startSearchHere == ' ') {
            startSearchHere++;
        }
        keyValueTail = nextSeparator;
        while (keyValueTail > startSearchHere && *(keyValueTail-1) == ' ') {
            keyValueTail--;
        }
        /* now keyValueTail points to first char after the keyName */
        /* copy & normalize keyName from locale */
        if (startSearchHere == keyValueTail) {
            status = U_ILLEGAL_ARGUMENT_ERROR; /* empty keyword name in passed-in locale */
            return;
        }
        CharString localeKeywordName;
        while (startSearchHere < keyValueTail) {
          if (!UPRV_ISALPHANUM(*startSearchHere)) {
            status = U_ILLEGAL_ARGUMENT_ERROR; /* malformed keyword name */
            return;
          }
          localeKeywordName.append(uprv_tolower(*startSearchHere++), status);
        }
        if (U_FAILURE(status)) {
            return;
        }

        startSearchHere = uprv_strchr(nextSeparator, ';');

        if (canonKeywordName == localeKeywordName) {
             /* current entry matches the keyword. */
           nextSeparator++; /* skip '=' */
            /* First strip leading & trailing spaces (TC decided to tolerate these) */
            while(*nextSeparator == ' ') {
              nextSeparator++;
            }
            keyValueTail = (startSearchHere)? startSearchHere: nextSeparator + uprv_strlen(nextSeparator);
            while(keyValueTail > nextSeparator && *(keyValueTail-1) == ' ') {
              keyValueTail--;
            }
            /* Now copy the value, but check well-formedness */
            if (nextSeparator == keyValueTail) {
              status = U_ILLEGAL_ARGUMENT_ERROR; /* empty key value name in passed-in locale */
              return;
            }
            while (nextSeparator < keyValueTail) {
              if (!UPRV_ISALPHANUM(*nextSeparator) && !UPRV_OK_VALUE_PUNCTUATION(*nextSeparator)) {
                status = U_ILLEGAL_ARGUMENT_ERROR; /* malformed key value */
                return;
              }
              /* Should we lowercase value to return here? Tests expect as-is. */
              sink.Append(nextSeparator++, 1);
            }
            return;
        }
    }
}

U_CAPI int32_t U_EXPORT2
uloc_setKeywordValue(const char* keywordName,
                     const char* keywordValue,
                     char* buffer, int32_t bufferCapacity,
                     UErrorCode* status)
{
    if (U_FAILURE(*status)) { return 0; }

    if (bufferCapacity <= 1) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }

    int32_t bufLen = (int32_t)uprv_strlen(buffer);
    if(bufferCapacity<bufLen) {
        /* The capacity is less than the length?! Is this NUL terminated? */
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }

    char* keywords = const_cast<char*>(locale_getKeywordsStart(buffer));
    int32_t baseLen = keywords == nullptr ? bufLen : keywords - buffer;
    // Remove -1 from the capacity so that this function can guarantee NUL termination.
    CheckedArrayByteSink sink(keywords == nullptr ? buffer + bufLen : keywords,
                              bufferCapacity - baseLen - 1);
    int32_t reslen = ulocimp_setKeywordValue(
            keywords, keywordName, keywordValue, sink, *status);

    if (U_FAILURE(*status)) {
        return *status == U_BUFFER_OVERFLOW_ERROR ? reslen + baseLen : 0;
    }

    // See the documentation for this function, it's guaranteed to never
    // overflow the buffer but instead abort with BUFFER_OVERFLOW_ERROR.
    // In this case, nothing has been written to the sink, so it cannot have Overflowed().
    U_ASSERT(!sink.Overflowed());
    U_ASSERT(reslen >= 0);
    return u_terminateChars(buffer, bufferCapacity, reslen + baseLen, status);
}

U_EXPORT void
ulocimp_setKeywordValue(const char* keywordName,
                        const char* keywordValue,
                        CharString& localeID,
                        UErrorCode& status)
{
    if (U_FAILURE(status)) { return; }
    // This is safe because CharString::truncate() doesn't actually erase any
    // data, but simply sets the position for where new data will be written.
    const char* keywords = locale_getKeywordsStart(localeID.data());
    if (keywords != nullptr) localeID.truncate(keywords - localeID.data());
    CharStringByteSink sink(&localeID);
    ulocimp_setKeywordValue(keywords, keywordName, keywordValue, sink, status);
}

U_EXPORT int32_t
ulocimp_setKeywordValue(const char* keywords,
                        const char* keywordName,
                        const char* keywordValue,
                        ByteSink& sink,
                        UErrorCode& status)
{
    if (U_FAILURE(status)) { return 0; }

    /* TODO: sorting. removal. */
    int32_t needLen = 0;
    int32_t rc;
    const char* nextSeparator = nullptr;
    const char* nextEqualsign = nullptr;
    const char* keywordStart = nullptr;
    CharString updatedKeysAndValues;
    bool handledInputKeyAndValue = false;
    char keyValuePrefix = '@';

    if (status == U_STRING_NOT_TERMINATED_WARNING) {
        status = U_ZERO_ERROR;
    }
    if (keywordName == nullptr || keywordName[0] == 0) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    CharString canonKeywordName = locale_canonKeywordName(keywordName, status);
    if (U_FAILURE(status)) {
        return 0;
    }

    CharString canonKeywordValue;
    if(keywordValue) {
        while (*keywordValue != 0) {
            if (!UPRV_ISALPHANUM(*keywordValue) && !UPRV_OK_VALUE_PUNCTUATION(*keywordValue)) {
                status = U_ILLEGAL_ARGUMENT_ERROR; /* malformed key value */
                return 0;
            }
            /* Should we force lowercase in value to set? */
            canonKeywordValue.append(*keywordValue++, status);
        }
    }
    if (U_FAILURE(status)) {
        return 0;
    }

    if (keywords == nullptr || keywords[1] == '\0') {
        if (canonKeywordValue.isEmpty()) { /* no keywords = nothing to remove */
            U_ASSERT(status != U_STRING_NOT_TERMINATED_WARNING);
            return 0;
        }

        needLen = 1 + canonKeywordName.length() + 1 + canonKeywordValue.length();
        int32_t capacity = 0;
        char* buffer = sink.GetAppendBuffer(
                needLen, needLen, nullptr, needLen, &capacity);
        if (capacity < needLen || buffer == nullptr) {
            status = U_BUFFER_OVERFLOW_ERROR;
            return needLen; /* no change */
        }
        char* it = buffer;

        *it++ = '@';
        uprv_memcpy(it, canonKeywordName.data(), canonKeywordName.length());
        it += canonKeywordName.length();
        *it++ = '=';
        uprv_memcpy(it, canonKeywordValue.data(), canonKeywordValue.length());
        sink.Append(buffer, needLen);
        U_ASSERT(status != U_STRING_NOT_TERMINATED_WARNING);
        return needLen;
    } /* end shortcut - no @ */

    keywordStart = keywords;
    /* search for keyword */
    while(keywordStart) {
        const char* keyValueTail;

        keywordStart++; /* skip @ or ; */
        nextEqualsign = uprv_strchr(keywordStart, '=');
        if (!nextEqualsign) {
            status = U_ILLEGAL_ARGUMENT_ERROR; /* key must have =value */
            return 0;
        }
        /* strip leading & trailing spaces (TC decided to tolerate these) */
        while(*keywordStart == ' ') {
            keywordStart++;
        }
        keyValueTail = nextEqualsign;
        while (keyValueTail > keywordStart && *(keyValueTail-1) == ' ') {
            keyValueTail--;
        }
        /* now keyValueTail points to first char after the keyName */
        /* copy & normalize keyName from locale */
        if (keywordStart == keyValueTail) {
            status = U_ILLEGAL_ARGUMENT_ERROR; /* empty keyword name in passed-in locale */
            return 0;
        }
        CharString localeKeywordName;
        while (keywordStart < keyValueTail) {
            if (!UPRV_ISALPHANUM(*keywordStart)) {
                status = U_ILLEGAL_ARGUMENT_ERROR; /* malformed keyword name */
                return 0;
            }
            localeKeywordName.append(uprv_tolower(*keywordStart++), status);
        }
        if (U_FAILURE(status)) {
            return 0;
        }

        nextSeparator = uprv_strchr(nextEqualsign, ';');

        /* start processing the value part */
        nextEqualsign++; /* skip '=' */
        /* First strip leading & trailing spaces (TC decided to tolerate these) */
        while(*nextEqualsign == ' ') {
            nextEqualsign++;
        }
        keyValueTail = (nextSeparator)? nextSeparator: nextEqualsign + uprv_strlen(nextEqualsign);
        while(keyValueTail > nextEqualsign && *(keyValueTail-1) == ' ') {
            keyValueTail--;
        }
        if (nextEqualsign == keyValueTail) {
            status = U_ILLEGAL_ARGUMENT_ERROR; /* empty key value in passed-in locale */
            return 0;
        }

        rc = uprv_strcmp(canonKeywordName.data(), localeKeywordName.data());
        if(rc == 0) {
            /* Current entry matches the input keyword. Update the entry */
            if (!canonKeywordValue.isEmpty()) { /* updating a value */
                updatedKeysAndValues.append(keyValuePrefix, status);
                keyValuePrefix = ';'; /* for any subsequent key-value pair */
                updatedKeysAndValues.append(canonKeywordName, status);
                updatedKeysAndValues.append('=', status);
                updatedKeysAndValues.append(canonKeywordValue, status);
            } /* else removing this entry, don't emit anything */
            handledInputKeyAndValue = true;
        } else {
           /* input keyword sorts earlier than current entry, add before current entry */
            if (rc < 0 && !canonKeywordValue.isEmpty() && !handledInputKeyAndValue) {
                /* insert new entry at this location */
                updatedKeysAndValues.append(keyValuePrefix, status);
                keyValuePrefix = ';'; /* for any subsequent key-value pair */
                updatedKeysAndValues.append(canonKeywordName, status);
                updatedKeysAndValues.append('=', status);
                updatedKeysAndValues.append(canonKeywordValue, status);
                handledInputKeyAndValue = true;
            }
            /* copy the current entry */
            updatedKeysAndValues.append(keyValuePrefix, status);
            keyValuePrefix = ';'; /* for any subsequent key-value pair */
            updatedKeysAndValues.append(localeKeywordName, status);
            updatedKeysAndValues.append('=', status);
            updatedKeysAndValues.append(nextEqualsign, static_cast<int32_t>(keyValueTail-nextEqualsign), status);
        }
        if (!nextSeparator && !canonKeywordValue.isEmpty() && !handledInputKeyAndValue) {
            /* append new entry at the end, it sorts later than existing entries */
            updatedKeysAndValues.append(keyValuePrefix, status);
            /* skip keyValuePrefix update, no subsequent key-value pair */
            updatedKeysAndValues.append(canonKeywordName, status);
            updatedKeysAndValues.append('=', status);
            updatedKeysAndValues.append(canonKeywordValue, status);
            handledInputKeyAndValue = true;
        }
        keywordStart = nextSeparator;
    } /* end loop searching */

    /* Any error from updatedKeysAndValues.append above would be internal and not due to
     * problems with the passed-in locale. So if we did encounter problems with the
     * passed-in locale above, those errors took precedence and overrode any error
     * status from updatedKeysAndValues.append, and also caused a return of 0. If there
     * are errors here they are from updatedKeysAndValues.append; they do cause an
     * error return but the passed-in locale is unmodified and the original bufLen is
     * returned.
     */
    if (!handledInputKeyAndValue || U_FAILURE(status)) {
        /* if input key/value specified removal of a keyword not present in locale, or
         * there was an error in CharString.append, leave original locale alone. */
        U_ASSERT(status != U_STRING_NOT_TERMINATED_WARNING);
        return (int32_t)uprv_strlen(keywords);
    }

    needLen = updatedKeysAndValues.length();
    // Check to see can we fit the updatedKeysAndValues, if not, return
    // U_BUFFER_OVERFLOW_ERROR without copy updatedKeysAndValues into it.
    // We do this because this API function does not behave like most others:
    // It promises never to set a U_STRING_NOT_TERMINATED_WARNING.
    // When the contents fits but without the terminating NUL, in this case we need to not change
    // the buffer contents and return with a buffer overflow error.
    if (needLen > 0) {
        int32_t capacity = 0;
        char* buffer = sink.GetAppendBuffer(
                needLen, needLen, nullptr, needLen, &capacity);
        if (capacity < needLen || buffer == nullptr) {
            status = U_BUFFER_OVERFLOW_ERROR;
            return needLen;
        }
        uprv_memcpy(buffer, updatedKeysAndValues.data(), needLen);
        sink.Append(buffer, needLen);
    }
    U_ASSERT(status != U_STRING_NOT_TERMINATED_WARNING);
    return needLen;
}

/* ### ID parsing implementation **************************************************/

namespace {

inline bool _isPrefixLetter(char a) { return a == 'x' || a == 'X' || a == 'i' || a == 'I'; }

/*returns true if one of the special prefixes is here (s=string)
  'x-' or 'i-' */
inline bool _isIDPrefix(const char *s) { return _isPrefixLetter(s[0]) && _isIDSeparator(s[1]); }

/* Dot terminates it because of POSIX form  where dot precedes the codepage
 * except for variant
 */
inline bool _isTerminator(char a) { return a == 0 || a == '.' || a == '@'; }

inline bool _isBCP47Extension(const char* p) {
    return p[0] == '-' &&
           (p[1] == 't' || p[1] == 'T' ||
            p[1] == 'u' || p[1] == 'U' ||
            p[1] == 'x' || p[1] == 'X') &&
           p[2] == '-';
}

/**
 * Lookup 'key' in the array 'list'.  The array 'list' should contain
 * a nullptr entry, followed by more entries, and a second nullptr entry.
 *
 * The 'list' param should be LANGUAGES, LANGUAGES_3, COUNTRIES, or
 * COUNTRIES_3.
 */
std::optional<int16_t> _findIndex(const char* const* list, const char* key)
{
    const char* const* anchor = list;
    int32_t pass = 0;

    /* Make two passes through two nullptr-terminated arrays at 'list' */
    while (pass++ < 2) {
        while (*list) {
            if (uprv_strcmp(key, *list) == 0) {
                return (int16_t)(list - anchor);
            }
            list++;
        }
        ++list;     /* skip final nullptr *CWB*/
    }
    return std::nullopt;
}

}  // namespace

U_CFUNC const char*
uloc_getCurrentCountryID(const char* oldID){
    std::optional<int16_t> offset = _findIndex(DEPRECATED_COUNTRIES, oldID);
    return offset.has_value() ? REPLACEMENT_COUNTRIES[*offset] : oldID;
}
U_CFUNC const char*
uloc_getCurrentLanguageID(const char* oldID){
    std::optional<int16_t> offset = _findIndex(DEPRECATED_LANGUAGES, oldID);
    return offset.has_value() ? REPLACEMENT_LANGUAGES[*offset] : oldID;
}

namespace {

/*
 * the internal functions _getLanguage(), _getScript(), _getRegion(), _getVariant()
 * avoid duplicating code to handle the earlier locale ID pieces
 * in the functions for the later ones by
 * setting the *pEnd pointer to where they stopped parsing
 *
 * TODO try to use this in Locale
 */

void
_getLanguage(const char* localeID,
             ByteSink* sink,
             const char** pEnd,
             UErrorCode& status) {
    U_ASSERT(pEnd != nullptr);
    *pEnd = localeID;

    if (uprv_stricmp(localeID, "root") == 0) {
        localeID += 4;
    } else if (uprv_strnicmp(localeID, "und", 3) == 0 &&
               (localeID[3] == '\0' ||
                localeID[3] == '-' ||
                localeID[3] == '_' ||
                localeID[3] == '@')) {
        localeID += 3;
    }

    constexpr int32_t MAXLEN = ULOC_LANG_CAPACITY - 1;  // Minus NUL.

    /* if it starts with i- or x- then copy that prefix */
    int32_t len = _isIDPrefix(localeID) ? 2 : 0;
    while (!_isTerminator(localeID[len]) && !_isIDSeparator(localeID[len])) {
        if (len == MAXLEN) {
            status = U_ILLEGAL_ARGUMENT_ERROR;
            return;
        }
        len++;
    }

    *pEnd = localeID + len;
    if (sink == nullptr || len == 0) { return; }

    int32_t minCapacity = uprv_max(len, 4);  // Minimum 3 letters plus NUL.
    char scratch[MAXLEN];
    int32_t capacity = 0;
    char* buffer = sink->GetAppendBuffer(
            minCapacity, minCapacity, scratch, UPRV_LENGTHOF(scratch), &capacity);

    for (int32_t i = 0; i < len; ++i) {
        buffer[i] = uprv_tolower(localeID[i]);
    }
    if (_isIDSeparator(localeID[1])) {
        buffer[1] = '-';
    }

    if (len == 3) {
        /* convert 3 character code to 2 character code if possible *CWB*/
        U_ASSERT(capacity >= 4);
        buffer[3] = '\0';
        std::optional<int16_t> offset = _findIndex(LANGUAGES_3, buffer);
        if (offset.has_value()) {
            const char* const alias = LANGUAGES[*offset];
            sink->Append(alias, (int32_t)uprv_strlen(alias));
            return;
        }
    }

    sink->Append(buffer, len);
}

void
_getScript(const char* localeID,
           ByteSink* sink,
           const char** pEnd) {
    U_ASSERT(pEnd != nullptr);
    *pEnd = localeID;

    constexpr int32_t LENGTH = 4;

    int32_t len = 0;
    while (!_isTerminator(localeID[len]) && !_isIDSeparator(localeID[len]) &&
            uprv_isASCIILetter(localeID[len])) {
        if (len == LENGTH) { return; }
        len++;
    }
    if (len != LENGTH) { return; }

    *pEnd = localeID + LENGTH;
    if (sink == nullptr) { return; }

    char scratch[LENGTH];
    int32_t capacity = 0;
    char* buffer = sink->GetAppendBuffer(
            LENGTH, LENGTH, scratch, UPRV_LENGTHOF(scratch), &capacity);

    buffer[0] = uprv_toupper(localeID[0]);
    for (int32_t i = 1; i < LENGTH; ++i) {
        buffer[i] = uprv_tolower(localeID[i]);
    }

    sink->Append(buffer, LENGTH);
}

void
_getRegion(const char* localeID,
           ByteSink* sink,
           const char** pEnd) {
    U_ASSERT(pEnd != nullptr);
    *pEnd = localeID;

    constexpr int32_t MINLEN = 2;
    constexpr int32_t MAXLEN = ULOC_COUNTRY_CAPACITY - 1;  // Minus NUL.

    int32_t len = 0;
    while (!_isTerminator(localeID[len]) && !_isIDSeparator(localeID[len])) {
        if (len == MAXLEN) { return; }
        len++;
    }
    if (len < MINLEN) { return; }

    *pEnd = localeID + len;
    if (sink == nullptr) { return; }

    char scratch[ULOC_COUNTRY_CAPACITY];
    int32_t capacity = 0;
    char* buffer = sink->GetAppendBuffer(
            ULOC_COUNTRY_CAPACITY,
            ULOC_COUNTRY_CAPACITY,
            scratch,
            UPRV_LENGTHOF(scratch),
            &capacity);

    for (int32_t i = 0; i < len; ++i) {
        buffer[i] = uprv_toupper(localeID[i]);
    }

    if (len == 3) {
        /* convert 3 character code to 2 character code if possible *CWB*/
        U_ASSERT(capacity >= 4);
        buffer[3] = '\0';
        std::optional<int16_t> offset = _findIndex(COUNTRIES_3, buffer);
        if (offset.has_value()) {
            const char* const alias = COUNTRIES[*offset];
            sink->Append(alias, (int32_t)uprv_strlen(alias));
            return;
        }
    }

    sink->Append(buffer, len);
}

/**
 * @param needSeparator if true, then add leading '_' if any variants
 * are added to 'variant'
 */
void
_getVariant(const char* localeID,
            char prev,
            ByteSink* sink,
            const char** pEnd,
            bool needSeparator,
            UErrorCode& status) {
    if (U_FAILURE(status)) return;
    if (pEnd != nullptr) { *pEnd = localeID; }

    // Reasonable upper limit for variants
    // There are no strict limitation of the syntax of variant in the legacy
    // locale format. If the locale is constructed from unicode_locale_id
    // as defined in UTS35, then we know each unicode_variant_subtag
    // could have max length of 8 ((alphanum{5,8} | digit alphanum{3})
    // 179 would allow 20 unicode_variant_subtag with sep in the
    // unicode_locale_id
    // 8*20 + 1*(20-1) = 179
    constexpr int32_t MAX_VARIANTS_LENGTH = 179;

    /* get one or more variant tags and separate them with '_' */
    int32_t index = 0;
    if (_isIDSeparator(prev)) {
        /* get a variant string after a '-' or '_' */
        for (index=0; !_isTerminator(localeID[index]); index++) {
            if (index >= MAX_VARIANTS_LENGTH) { // same as length > MAX_VARIANTS_LENGTH
                status = U_ILLEGAL_ARGUMENT_ERROR;
                return;
            }
            if (needSeparator) {
                if (sink != nullptr) {
                    sink->Append("_", 1);
                }
                needSeparator = false;
            }
            if (sink != nullptr) {
                char c = (char)uprv_toupper(localeID[index]);
                if (c == '-') c = '_';
                sink->Append(&c, 1);
            }
        }
        if (pEnd != nullptr) { *pEnd = localeID+index; }
    }

    /* if there is no variant tag after a '-' or '_' then look for '@' */
    if (index == 0) {
        if (prev=='@') {
            /* keep localeID */
        } else if((localeID=locale_getKeywordsStart(localeID))!=nullptr) {
            ++localeID; /* point after the '@' */
        } else {
            return;
        }
        for(; !_isTerminator(localeID[index]); index++) {
            if (index >= MAX_VARIANTS_LENGTH) { // same as length > MAX_VARIANTS_LENGTH
                status = U_ILLEGAL_ARGUMENT_ERROR;
                return;
            }
            if (needSeparator) {
                if (sink != nullptr) {
                    sink->Append("_", 1);
                }
                needSeparator = false;
            }
            if (sink != nullptr) {
                char c = (char)uprv_toupper(localeID[index]);
                if (c == '-' || c == ',') c = '_';
                sink->Append(&c, 1);
            }
        }
        if (pEnd != nullptr) { *pEnd = localeID + index; }
    }
}

}  // namespace

U_EXPORT CharString
ulocimp_getLanguage(const char* localeID, UErrorCode& status) {
    return ByteSinkUtil::viaByteSinkToCharString(
        [&](ByteSink& sink, UErrorCode& status) {
            ulocimp_getSubtags(
                    localeID,
                    &sink,
                    nullptr,
                    nullptr,
                    nullptr,
                    nullptr,
                    status);
        },
        status);
}

U_EXPORT CharString
ulocimp_getScript(const char* localeID, UErrorCode& status) {
    return ByteSinkUtil::viaByteSinkToCharString(
        [&](ByteSink& sink, UErrorCode& status) {
            ulocimp_getSubtags(
                    localeID,
                    nullptr,
                    &sink,
                    nullptr,
                    nullptr,
                    nullptr,
                    status);
        },
        status);
}

U_EXPORT CharString
ulocimp_getRegion(const char* localeID, UErrorCode& status) {
    return ByteSinkUtil::viaByteSinkToCharString(
        [&](ByteSink& sink, UErrorCode& status) {
            ulocimp_getSubtags(
                    localeID,
                    nullptr,
                    nullptr,
                    &sink,
                    nullptr,
                    nullptr,
                    status);
        },
        status);
}

U_EXPORT CharString
ulocimp_getVariant(const char* localeID, UErrorCode& status) {
    return ByteSinkUtil::viaByteSinkToCharString(
        [&](ByteSink& sink, UErrorCode& status) {
            ulocimp_getSubtags(
                    localeID,
                    nullptr,
                    nullptr,
                    nullptr,
                    &sink,
                    nullptr,
                    status);
        },
        status);
}

U_EXPORT void
ulocimp_getSubtags(
        const char* localeID,
        CharString* language,
        CharString* script,
        CharString* region,
        CharString* variant,
        const char** pEnd,
        UErrorCode& status) {
    if (U_FAILURE(status)) { return; }

    std::optional<CharStringByteSink> languageSink;
    std::optional<CharStringByteSink> scriptSink;
    std::optional<CharStringByteSink> regionSink;
    std::optional<CharStringByteSink> variantSink;

    if (language != nullptr) { languageSink.emplace(language); }
    if (script != nullptr) { scriptSink.emplace(script); }
    if (region != nullptr) { regionSink.emplace(region); }
    if (variant != nullptr) { variantSink.emplace(variant); }

    ulocimp_getSubtags(
            localeID,
            languageSink.has_value() ? &*languageSink : nullptr,
            scriptSink.has_value() ? &*scriptSink : nullptr,
            regionSink.has_value() ? &*regionSink : nullptr,
            variantSink.has_value() ? &*variantSink : nullptr,
            pEnd,
            status);
}

U_EXPORT void
ulocimp_getSubtags(
        const char* localeID,
        ByteSink* language,
        ByteSink* script,
        ByteSink* region,
        ByteSink* variant,
        const char** pEnd,
        UErrorCode& status) {
    if (U_FAILURE(status)) { return; }

    if (pEnd != nullptr) {
        *pEnd = localeID;
    } else if (language == nullptr &&
               script == nullptr &&
               region == nullptr &&
               variant == nullptr) {
        return;
    }

    bool hasRegion = false;

    if (localeID == nullptr) {
        localeID = uloc_getDefault();
    }

    _getLanguage(localeID, language, &localeID, status);
    if (U_FAILURE(status)) { return; }
    U_ASSERT(localeID != nullptr);

    if (pEnd != nullptr) {
        *pEnd = localeID;
    } else if (script == nullptr &&
               region == nullptr &&
               variant == nullptr) {
        return;
    }

    if (_isIDSeparator(*localeID)) {
        const char* begin = localeID + 1;
        const char* end = nullptr;
        _getScript(begin, script, &end);
        U_ASSERT(end != nullptr);
        if (end != begin) {
            localeID = end;
            if (pEnd != nullptr) { *pEnd = localeID; }
        }
    }

    if (region == nullptr && variant == nullptr && pEnd == nullptr) { return; }

    if (_isIDSeparator(*localeID)) {
        const char* begin = localeID + 1;
        const char* end = nullptr;
        _getRegion(begin, region, &end);
        U_ASSERT(end != nullptr);
        if (end != begin) {
            hasRegion = true;
            localeID = end;
            if (pEnd != nullptr) { *pEnd = localeID; }
        }
    }

    if (variant == nullptr && pEnd == nullptr) { return; }

    if (_isIDSeparator(*localeID) && !_isBCP47Extension(localeID)) {
        /* If there was no country ID, skip a possible extra IDSeparator */
        if (!hasRegion && _isIDSeparator(localeID[1])) {
            localeID++;
        }
        const char* begin = localeID + 1;
        const char* end = nullptr;
        _getVariant(begin, *localeID, variant, &end, false, status);
        if (U_FAILURE(status)) { return; }
        U_ASSERT(end != nullptr);
        if (end != begin && pEnd != nullptr) { *pEnd = end; }
    }
}

/* Keyword enumeration */

typedef struct UKeywordsContext {
    char* keywords;
    char* current;
} UKeywordsContext;

U_CDECL_BEGIN

static void U_CALLCONV
uloc_kw_closeKeywords(UEnumeration *enumerator) {
    uprv_free(((UKeywordsContext *)enumerator->context)->keywords);
    uprv_free(enumerator->context);
    uprv_free(enumerator);
}

static int32_t U_CALLCONV
uloc_kw_countKeywords(UEnumeration *en, UErrorCode * /*status*/) {
    char *kw = ((UKeywordsContext *)en->context)->keywords;
    int32_t result = 0;
    while(*kw) {
        result++;
        kw += uprv_strlen(kw)+1;
    }
    return result;
}

static const char * U_CALLCONV
uloc_kw_nextKeyword(UEnumeration* en,
                    int32_t* resultLength,
                    UErrorCode* /*status*/) {
    const char* result = ((UKeywordsContext *)en->context)->current;
    int32_t len = 0;
    if(*result) {
        len = (int32_t)uprv_strlen(((UKeywordsContext *)en->context)->current);
        ((UKeywordsContext *)en->context)->current += len+1;
    } else {
        result = nullptr;
    }
    if (resultLength) {
        *resultLength = len;
    }
    return result;
}

static void U_CALLCONV
uloc_kw_resetKeywords(UEnumeration* en,
                      UErrorCode* /*status*/) {
    ((UKeywordsContext *)en->context)->current = ((UKeywordsContext *)en->context)->keywords;
}

U_CDECL_END


static const UEnumeration gKeywordsEnum = {
    nullptr,
    nullptr,
    uloc_kw_closeKeywords,
    uloc_kw_countKeywords,
    uenum_unextDefault,
    uloc_kw_nextKeyword,
    uloc_kw_resetKeywords
};

U_CAPI UEnumeration* U_EXPORT2
uloc_openKeywordList(const char *keywordList, int32_t keywordListSize, UErrorCode* status)
{
    if (U_FAILURE(*status)) { return nullptr; }

    LocalMemory<UKeywordsContext> myContext;
    LocalMemory<UEnumeration> result;

    myContext.adoptInstead(static_cast<UKeywordsContext *>(uprv_malloc(sizeof(UKeywordsContext))));
    result.adoptInstead(static_cast<UEnumeration *>(uprv_malloc(sizeof(UEnumeration))));
    if (myContext.isNull() || result.isNull()) {
        *status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    uprv_memcpy(result.getAlias(), &gKeywordsEnum, sizeof(UEnumeration));
    myContext->keywords = static_cast<char *>(uprv_malloc(keywordListSize+1));
    if (myContext->keywords == nullptr) {
        *status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    uprv_memcpy(myContext->keywords, keywordList, keywordListSize);
    myContext->keywords[keywordListSize] = 0;
    myContext->current = myContext->keywords;
    result->context = myContext.orphan();
    return result.orphan();
}

U_CAPI UEnumeration* U_EXPORT2
uloc_openKeywords(const char* localeID,
                        UErrorCode* status)
{
    if(status==nullptr || U_FAILURE(*status)) {
        return nullptr;
    }

    CharString tempBuffer;
    const char* tmpLocaleID;

    if (_hasBCP47Extension(localeID)) {
        tempBuffer = ulocimp_forLanguageTag(localeID, -1, nullptr, *status);
        tmpLocaleID = U_SUCCESS(*status) && !tempBuffer.isEmpty() ? tempBuffer.data() : localeID;
    } else {
        if (localeID==nullptr) {
            localeID=uloc_getDefault();
        }
        tmpLocaleID=localeID;
    }

    ulocimp_getSubtags(
            tmpLocaleID,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            &tmpLocaleID,
            *status);
    if (U_FAILURE(*status)) {
        return nullptr;
    }

    /* keywords are located after '@' */
    if((tmpLocaleID = locale_getKeywordsStart(tmpLocaleID)) != nullptr) {
        CharString keywords = ulocimp_getKeywords(tmpLocaleID + 1, '@', false, *status);
        if (U_FAILURE(*status)) {
            return nullptr;
        }
        return uloc_openKeywordList(keywords.data(), keywords.length(), status);
    }
    return nullptr;
}


/* bit-flags for 'options' parameter of _canonicalize */
#define _ULOC_STRIP_KEYWORDS 0x2
#define _ULOC_CANONICALIZE   0x1

namespace {

inline bool OPTION_SET(uint32_t options, uint32_t mask) { return (options & mask) != 0; }

constexpr char i_default[] = {'i', '-', 'd', 'e', 'f', 'a', 'u', 'l', 't'};
constexpr int32_t I_DEFAULT_LENGTH = UPRV_LENGTHOF(i_default);

/**
 * Canonicalize the given localeID, to level 1 or to level 2,
 * depending on the options.  To specify level 1, pass in options=0.
 * To specify level 2, pass in options=_ULOC_CANONICALIZE.
 *
 * This is the code underlying uloc_getName and uloc_canonicalize.
 */
void
_canonicalize(const char* localeID,
              ByteSink& sink,
              uint32_t options,
              UErrorCode& err) {
    if (U_FAILURE(err)) {
        return;
    }

    int32_t j, fieldCount=0;
    CharString tempBuffer;  // if localeID has a BCP47 extension, tmpLocaleID points to this
    CharString localeIDWithHyphens;  // if localeID has a BPC47 extension and have _, tmpLocaleID points to this
    const char* origLocaleID;
    const char* tmpLocaleID;
    const char* keywordAssign = nullptr;
    const char* separatorIndicator = nullptr;

    if (_hasBCP47Extension(localeID)) {
        const char* localeIDPtr = localeID;

        // convert all underbars to hyphens, unless the "BCP47 extension" comes at the beginning of the string
        if (uprv_strchr(localeID, '_') != nullptr && localeID[1] != '-' && localeID[1] != '_') {
            localeIDWithHyphens.append(localeID, -1, err);
            if (U_SUCCESS(err)) {
                for (char* p = localeIDWithHyphens.data(); *p != '\0'; ++p) {
                    if (*p == '_') {
                        *p = '-';
                    }
                }
                localeIDPtr = localeIDWithHyphens.data();
            }
        }

        tempBuffer = ulocimp_forLanguageTag(localeIDPtr, -1, nullptr, err);
        tmpLocaleID = U_SUCCESS(err) && !tempBuffer.isEmpty() ? tempBuffer.data() : localeIDPtr;
    } else {
        if (localeID==nullptr) {
           localeID=uloc_getDefault();
        }
        tmpLocaleID=localeID;
    }

    origLocaleID=tmpLocaleID;

    /* get all pieces, one after another, and separate with '_' */
    CharString tag;
    CharString script;
    CharString country;
    CharString variant;
    ulocimp_getSubtags(
            tmpLocaleID,
            &tag,
            &script,
            &country,
            &variant,
            &tmpLocaleID,
            err);
    if (U_FAILURE(err)) {
        return;
    }

    if (tag.length() == I_DEFAULT_LENGTH &&
            uprv_strncmp(origLocaleID, i_default, I_DEFAULT_LENGTH) == 0) {
        tag.clear();
        tag.append(uloc_getDefault(), err);
    } else {
        if (!script.isEmpty()) {
            ++fieldCount;
            tag.append('_', err);
            tag.append(script, err);
        }
        if (!country.isEmpty()) {
            ++fieldCount;
            tag.append('_', err);
            tag.append(country, err);
        }
        if (!variant.isEmpty()) {
            ++fieldCount;
            if (country.isEmpty()) {
                tag.append('_', err);
            }
            tag.append('_', err);
            tag.append(variant, err);
        }
    }

    /* Copy POSIX-style charset specifier, if any [mr.utf8] */
    if (!OPTION_SET(options, _ULOC_CANONICALIZE) && *tmpLocaleID == '.') {
        tag.append('.', err);
        ++tmpLocaleID;
        const char *atPos = nullptr;
        size_t length;
        if((atPos = uprv_strchr(tmpLocaleID, '@')) != nullptr) {
            length = atPos - tmpLocaleID;
        } else {
            length = uprv_strlen(tmpLocaleID);
        }
        // The longest charset name we found in IANA charset registry
        // https://www.iana.org/assignments/character-sets/ is
        // "Extended_UNIX_Code_Packed_Format_for_Japanese" in length 45.
        // we therefore restrict the length here to be 64 which is a power of 2
        // number that is longer than 45.
        constexpr size_t kMaxCharsetLength = 64;
        if (length > kMaxCharsetLength) {
           err = U_ILLEGAL_ARGUMENT_ERROR; /* malformed keyword name */
           return;
        }
        tag.append(tmpLocaleID, static_cast<int32_t>(length), err);
        tmpLocaleID += length;
    }

    /* Scan ahead to next '@' and determine if it is followed by '=' and/or ';'
       After this, tmpLocaleID either points to '@' or is nullptr */
    if ((tmpLocaleID=locale_getKeywordsStart(tmpLocaleID))!=nullptr) {
        keywordAssign = uprv_strchr(tmpLocaleID, '=');
        separatorIndicator = uprv_strchr(tmpLocaleID, ';');
    }

    /* Copy POSIX-style variant, if any [mr@FOO] */
    if (!OPTION_SET(options, _ULOC_CANONICALIZE) &&
        tmpLocaleID != nullptr && keywordAssign == nullptr) {
        for (;;) {
            char c = *tmpLocaleID;
            if (c == 0) {
                break;
            }
            tag.append(c, err);
            ++tmpLocaleID;
        }
    }

    if (OPTION_SET(options, _ULOC_CANONICALIZE)) {
        /* Handle @FOO variant if @ is present and not followed by = */
        if (tmpLocaleID!=nullptr && keywordAssign==nullptr) {
            /* Add missing '_' if needed */
            if (fieldCount < 2 || (fieldCount < 3 && !script.isEmpty())) {
                do {
                    tag.append('_', err);
                    ++fieldCount;
                } while(fieldCount<2);
            }

            CharStringByteSink s(&tag);
            _getVariant(tmpLocaleID+1, '@', &s, nullptr, !variant.isEmpty(), err);
            if (U_FAILURE(err)) { return; }
        }

        /* Look up the ID in the canonicalization map */
        for (j=0; j<UPRV_LENGTHOF(CANONICALIZE_MAP); j++) {
            StringPiece id(CANONICALIZE_MAP[j].id);
            if (tag == id) {
                if (id.empty() && tmpLocaleID != nullptr) {
                    break; /* Don't remap "" if keywords present */
                }
                tag.clear();
                tag.append(CANONICALIZE_MAP[j].canonicalID, err);
                break;
            }
        }
    }

    sink.Append(tag.data(), tag.length());

    if (!OPTION_SET(options, _ULOC_STRIP_KEYWORDS)) {
        if (tmpLocaleID!=nullptr && keywordAssign!=nullptr &&
            (!separatorIndicator || separatorIndicator > keywordAssign)) {
            sink.Append("@", 1);
            ++fieldCount;
            ulocimp_getKeywords(tmpLocaleID+1, '@', sink, true, err);
        }
    }
}

}  // namespace

/* ### ID parsing API **************************************************/

U_CAPI int32_t  U_EXPORT2
uloc_getParent(const char*    localeID,
               char* parent,
               int32_t parentCapacity,
               UErrorCode* err)
{
    return ByteSinkUtil::viaByteSinkToTerminatedChars(
        parent, parentCapacity,
        [&](ByteSink& sink, UErrorCode& status) {
            ulocimp_getParent(localeID, sink, status);
        },
        *err);
}

U_EXPORT CharString
ulocimp_getParent(const char* localeID,
                  UErrorCode& err)
{
    return ByteSinkUtil::viaByteSinkToCharString(
        [&](ByteSink& sink, UErrorCode& status) {
            ulocimp_getParent(localeID, sink, status);
        },
        err);
}

U_EXPORT void
ulocimp_getParent(const char* localeID,
                  icu::ByteSink& sink,
                  UErrorCode& err)
{
    if (U_FAILURE(err)) { return; }

    const char *lastUnderscore;
    int32_t i;

    if (localeID == nullptr)
        localeID = uloc_getDefault();

    lastUnderscore=uprv_strrchr(localeID, '_');
    if(lastUnderscore!=nullptr) {
        i=(int32_t)(lastUnderscore-localeID);
    } else {
        i=0;
    }

    if (i > 0) {
        if (uprv_strnicmp(localeID, "und_", 4) == 0) {
            localeID += 3;
            i -= 3;
        }
        sink.Append(localeID, i);
    }
}

U_CAPI int32_t U_EXPORT2
uloc_getLanguage(const char*    localeID,
         char* language,
         int32_t languageCapacity,
         UErrorCode* err)
{
    /* uloc_getLanguage will return a 2 character iso-639 code if one exists. *CWB*/
    return ByteSinkUtil::viaByteSinkToTerminatedChars(
        language, languageCapacity,
        [&](ByteSink& sink, UErrorCode& status) {
            ulocimp_getSubtags(
                    localeID,
                    &sink,
                    nullptr,
                    nullptr,
                    nullptr,
                    nullptr,
                    status);
        },
        *err);
}

U_CAPI int32_t U_EXPORT2
uloc_getScript(const char*    localeID,
         char* script,
         int32_t scriptCapacity,
         UErrorCode* err)
{
    return ByteSinkUtil::viaByteSinkToTerminatedChars(
        script, scriptCapacity,
        [&](ByteSink& sink, UErrorCode& status) {
            ulocimp_getSubtags(
                    localeID,
                    nullptr,
                    &sink,
                    nullptr,
                    nullptr,
                    nullptr,
                    status);
        },
        *err);
}

U_CAPI int32_t  U_EXPORT2
uloc_getCountry(const char* localeID,
            char* country,
            int32_t countryCapacity,
            UErrorCode* err)
{
    return ByteSinkUtil::viaByteSinkToTerminatedChars(
        country, countryCapacity,
        [&](ByteSink& sink, UErrorCode& status) {
            ulocimp_getSubtags(
                    localeID,
                    nullptr,
                    nullptr,
                    &sink,
                    nullptr,
                    nullptr,
                    status);
        },
        *err);
}

U_CAPI int32_t  U_EXPORT2
uloc_getVariant(const char* localeID,
                char* variant,
                int32_t variantCapacity,
                UErrorCode* err)
{
    return ByteSinkUtil::viaByteSinkToTerminatedChars(
        variant, variantCapacity,
        [&](ByteSink& sink, UErrorCode& status) {
            ulocimp_getSubtags(
                    localeID,
                    nullptr,
                    nullptr,
                    nullptr,
                    &sink,
                    nullptr,
                    status);
        },
        *err);
}

U_CAPI int32_t  U_EXPORT2
uloc_getName(const char* localeID,
             char* name,
             int32_t nameCapacity,
             UErrorCode* err)
{
    return ByteSinkUtil::viaByteSinkToTerminatedChars(
        name, nameCapacity,
        [&](ByteSink& sink, UErrorCode& status) {
            ulocimp_getName(localeID, sink, status);
        },
        *err);
}

U_EXPORT CharString
ulocimp_getName(const char* localeID,
                UErrorCode& err)
{
    return ByteSinkUtil::viaByteSinkToCharString(
        [&](ByteSink& sink, UErrorCode& status) {
            ulocimp_getName(localeID, sink, status);
        },
        err);
}

U_EXPORT void
ulocimp_getName(const char* localeID,
                ByteSink& sink,
                UErrorCode& err)
{
    _canonicalize(localeID, sink, 0, err);
}

U_CAPI int32_t  U_EXPORT2
uloc_getBaseName(const char* localeID,
                 char* name,
                 int32_t nameCapacity,
                 UErrorCode* err)
{
    return ByteSinkUtil::viaByteSinkToTerminatedChars(
        name, nameCapacity,
        [&](ByteSink& sink, UErrorCode& status) {
            ulocimp_getBaseName(localeID, sink, status);
        },
        *err);
}

U_EXPORT CharString
ulocimp_getBaseName(const char* localeID,
                    UErrorCode& err)
{
    return ByteSinkUtil::viaByteSinkToCharString(
        [&](ByteSink& sink, UErrorCode& status) {
            ulocimp_getBaseName(localeID, sink, status);
        },
        err);
}

U_EXPORT void
ulocimp_getBaseName(const char* localeID,
                    ByteSink& sink,
                    UErrorCode& err)
{
    _canonicalize(localeID, sink, _ULOC_STRIP_KEYWORDS, err);
}

U_CAPI int32_t  U_EXPORT2
uloc_canonicalize(const char* localeID,
                  char* name,
                  int32_t nameCapacity,
                  UErrorCode* err)
{
    return ByteSinkUtil::viaByteSinkToTerminatedChars(
        name, nameCapacity,
        [&](ByteSink& sink, UErrorCode& status) {
            ulocimp_canonicalize(localeID, sink, status);
        },
        *err);
}

U_EXPORT CharString
ulocimp_canonicalize(const char* localeID,
                     UErrorCode& err)
{
    return ByteSinkUtil::viaByteSinkToCharString(
        [&](ByteSink& sink, UErrorCode& status) {
            ulocimp_canonicalize(localeID, sink, status);
        },
        err);
}

U_EXPORT void
ulocimp_canonicalize(const char* localeID,
                     ByteSink& sink,
                     UErrorCode& err)
{
    _canonicalize(localeID, sink, _ULOC_CANONICALIZE, err);
}

U_CAPI const char*  U_EXPORT2
uloc_getISO3Language(const char* localeID)
{
    UErrorCode err = U_ZERO_ERROR;

    if (localeID == nullptr)
    {
        localeID = uloc_getDefault();
    }
    CharString lang = ulocimp_getLanguage(localeID, err);
    if (U_FAILURE(err))
        return "";
    std::optional<int16_t> offset = _findIndex(LANGUAGES, lang.data());
    return offset.has_value() ? LANGUAGES_3[*offset] : "";
}

U_CAPI const char*  U_EXPORT2
uloc_getISO3Country(const char* localeID)
{
    UErrorCode err = U_ZERO_ERROR;

    if (localeID == nullptr)
    {
        localeID = uloc_getDefault();
    }
    CharString cntry = ulocimp_getRegion(localeID, err);
    if (U_FAILURE(err))
        return "";
    std::optional<int16_t> offset = _findIndex(COUNTRIES, cntry.data());
    return offset.has_value() ? COUNTRIES_3[*offset] : "";
}

U_CAPI uint32_t  U_EXPORT2
uloc_getLCID(const char* localeID)
{
    UErrorCode status = U_ZERO_ERROR;
    uint32_t   lcid = 0;

    /* Check for incomplete id. */
    if (!localeID || uprv_strlen(localeID) < 2) {
        return 0;
    }

    // First, attempt Windows platform lookup if available, but fall
    // through to catch any special cases (ICU vs Windows name differences).
    lcid = uprv_convertToLCIDPlatform(localeID, &status);
    if (U_FAILURE(status)) {
        return 0;
    }
    if (lcid > 0) {
        // Windows found an LCID, return that
        return lcid;
    }

    CharString langID = ulocimp_getLanguage(localeID, status);
    if (U_FAILURE(status)) {
        return 0;
    }

    if (uprv_strchr(localeID, '@')) {
        // uprv_convertToLCID does not support keywords other than collation.
        // Remove all keywords except collation.
        CharString collVal = ulocimp_getKeywordValue(localeID, "collation", status);
        if (U_SUCCESS(status) && !collVal.isEmpty()) {
            CharString tmpLocaleID = ulocimp_getBaseName(localeID, status);
            ulocimp_setKeywordValue("collation", collVal.data(), tmpLocaleID, status);
            if (U_SUCCESS(status)) {
                return uprv_convertToLCID(langID.data(), tmpLocaleID.data(), &status);
            }
        }

        // fall through - all keywords are simply ignored
        status = U_ZERO_ERROR;
    }

    return uprv_convertToLCID(langID.data(), localeID, &status);
}

U_CAPI int32_t U_EXPORT2
uloc_getLocaleForLCID(uint32_t hostid, char *locale, int32_t localeCapacity,
                UErrorCode *status)
{
    return uprv_convertToPosix(hostid, locale, localeCapacity, status);
}

/* ### Default locale **************************************************/

U_CAPI const char*  U_EXPORT2
uloc_getDefault()
{
    return locale_get_default();
}

U_CAPI void  U_EXPORT2
uloc_setDefault(const char*   newDefaultLocale,
             UErrorCode* err)
{
    if (U_FAILURE(*err))
        return;
    /* the error code isn't currently used for anything by this function*/

    /* propagate change to C++ */
    locale_set_default(newDefaultLocale);
}

/**
 * Returns a list of all 2-letter language codes defined in ISO 639.  This is a pointer
 * to an array of pointers to arrays of char.  All of these pointers are owned
 * by ICU-- do not delete them, and do not write through them.  The array is
 * terminated with a null pointer.
 */
U_CAPI const char* const*  U_EXPORT2
uloc_getISOLanguages()
{
    return LANGUAGES;
}

/**
 * Returns a list of all 2-letter country codes defined in ISO 639.  This is a
 * pointer to an array of pointers to arrays of char.  All of these pointers are
 * owned by ICU-- do not delete them, and do not write through them.  The array is
 * terminated with a null pointer.
 */
U_CAPI const char* const*  U_EXPORT2
uloc_getISOCountries()
{
    return COUNTRIES;
}

U_CAPI const char* U_EXPORT2
uloc_toUnicodeLocaleKey(const char* keyword)
{
    const char* bcpKey = ulocimp_toBcpKey(keyword);
    if (bcpKey == nullptr && ultag_isUnicodeLocaleKey(keyword, -1)) {
        // unknown keyword, but syntax is fine..
        return keyword;
    }
    return bcpKey;
}

U_CAPI const char* U_EXPORT2
uloc_toUnicodeLocaleType(const char* keyword, const char* value)
{
    const char* bcpType = ulocimp_toBcpType(keyword, value, nullptr, nullptr);
    if (bcpType == nullptr && ultag_isUnicodeLocaleType(value, -1)) {
        // unknown keyword, but syntax is fine..
        return value;
    }
    return bcpType;
}

namespace {

bool
isWellFormedLegacyKey(const char* legacyKey)
{
    const char* p = legacyKey;
    while (*p) {
        if (!UPRV_ISALPHANUM(*p)) {
            return false;
        }
        p++;
    }
    return true;
}

bool
isWellFormedLegacyType(const char* legacyType)
{
    const char* p = legacyType;
    int32_t alphaNumLen = 0;
    while (*p) {
        if (*p == '_' || *p == '/' || *p == '-') {
            if (alphaNumLen == 0) {
                return false;
            }
            alphaNumLen = 0;
        } else if (UPRV_ISALPHANUM(*p)) {
            alphaNumLen++;
        } else {
            return false;
        }
        p++;
    }
    return (alphaNumLen != 0);
}

}  // namespace

U_CAPI const char* U_EXPORT2
uloc_toLegacyKey(const char* keyword)
{
    const char* legacyKey = ulocimp_toLegacyKey(keyword);
    if (legacyKey == nullptr) {
        // Checks if the specified locale key is well-formed with the legacy locale syntax.
        //
        // Note:
        //  LDML/CLDR provides some definition of keyword syntax in
        //  * http://www.unicode.org/reports/tr35/#Unicode_locale_identifier and
        //  * http://www.unicode.org/reports/tr35/#Old_Locale_Extension_Syntax
        //  Keys can only consist of [0-9a-zA-Z].
        if (isWellFormedLegacyKey(keyword)) {
            return keyword;
        }
    }
    return legacyKey;
}

U_CAPI const char* U_EXPORT2
uloc_toLegacyType(const char* keyword, const char* value)
{
    const char* legacyType = ulocimp_toLegacyType(keyword, value, nullptr, nullptr);
    if (legacyType == nullptr) {
        // Checks if the specified locale type is well-formed with the legacy locale syntax.
        //
        // Note:
        //  LDML/CLDR provides some definition of keyword syntax in
        //  * http://www.unicode.org/reports/tr35/#Unicode_locale_identifier and
        //  * http://www.unicode.org/reports/tr35/#Old_Locale_Extension_Syntax
        //  Values (types) can only consist of [0-9a-zA-Z], plus for legacy values
        //  we allow [/_-+] in the middle (e.g. "Etc/GMT+1", "Asia/Tel_Aviv")
        if (isWellFormedLegacyType(value)) {
            return value;
        }
    }
    return legacyType;
}

/*eof*/
