// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*   Copyright (C) 2013-2016, International Business Machines
*   Corporation and others.  All Rights Reserved.
*******************************************************************************
*   file name:  uscript_props.cpp
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 2013feb16
*   created by: Markus W. Scherer
*/

#include "unicode/utypes.h"
#include "unicode/unistr.h"
#include "unicode/uscript.h"
#include "unicode/utf16.h"
#include "ustr_imp.h"
#include "cmemory.h"

namespace {

// Script metadata (script properties).
// See http://unicode.org/cldr/trac/browser/trunk/common/properties/scriptMetadata.txt

// 0 = NOT_ENCODED, no sample character, default false script properties.
// Bits 20.. 0: sample character

// Bits 23..21: usage
const int32_t UNKNOWN = 1 << 21;
const int32_t EXCLUSION = 2 << 21;
const int32_t LIMITED_USE = 3 << 21;
// st int32_t ASPIRATIONAL = 4 << 21; -- not used any more since Unicode 10
const int32_t RECOMMENDED = 5 << 21;

// Bits 31..24: Single-bit flags
const int32_t RTL = 1 << 24;
const int32_t LB_LETTERS = 1 << 25;
const int32_t CASED = 1 << 26;

const int32_t SCRIPT_PROPS[] = {
    // Begin copy-paste output from
    // tools/trunk/unicode/py/parsescriptmetadata.py
    0x0040 | RECOMMENDED,  // Zyyy
    0x0308 | RECOMMENDED,  // Zinh
    0x0628 | RECOMMENDED | RTL,  // Arab
    0x0531 | RECOMMENDED | CASED,  // Armn
    0x0995 | RECOMMENDED,  // Beng
    0x3105 | RECOMMENDED | LB_LETTERS,  // Bopo
    0x13C4 | LIMITED_USE | CASED,  // Cher
    0x03E2 | EXCLUSION | CASED,  // Copt
    0x042F | RECOMMENDED | CASED,  // Cyrl
    0x10414 | EXCLUSION | CASED,  // Dsrt
    0x0905 | RECOMMENDED,  // Deva
    0x12A0 | RECOMMENDED,  // Ethi
    0x10D3 | RECOMMENDED,  // Geor
    0x10330 | EXCLUSION,  // Goth
    0x03A9 | RECOMMENDED | CASED,  // Grek
    0x0A95 | RECOMMENDED,  // Gujr
    0x0A15 | RECOMMENDED,  // Guru
    0x5B57 | RECOMMENDED | LB_LETTERS,  // Hani
    0xAC00 | RECOMMENDED,  // Hang
    0x05D0 | RECOMMENDED | RTL,  // Hebr
    0x304B | RECOMMENDED | LB_LETTERS,  // Hira
    0x0C95 | RECOMMENDED,  // Knda
    0x30AB | RECOMMENDED | LB_LETTERS,  // Kana
    0x1780 | RECOMMENDED | LB_LETTERS,  // Khmr
    0x0EA5 | RECOMMENDED | LB_LETTERS,  // Laoo
    0x004C | RECOMMENDED | CASED,  // Latn
    0x0D15 | RECOMMENDED,  // Mlym
    0x1826 | EXCLUSION,  // Mong
    0x1000 | RECOMMENDED | LB_LETTERS,  // Mymr
    0x168F | EXCLUSION,  // Ogam
    0x10300 | EXCLUSION,  // Ital
    0x0B15 | RECOMMENDED,  // Orya
    0x16A0 | EXCLUSION,  // Runr
    0x0D85 | RECOMMENDED,  // Sinh
    0x0710 | LIMITED_USE | RTL,  // Syrc
    0x0B95 | RECOMMENDED,  // Taml
    0x0C15 | RECOMMENDED,  // Telu
    0x078C | RECOMMENDED | RTL,  // Thaa
    0x0E17 | RECOMMENDED | LB_LETTERS,  // Thai
    0x0F40 | RECOMMENDED,  // Tibt
    0x14C0 | LIMITED_USE,  // Cans
    0xA288 | LIMITED_USE | LB_LETTERS,  // Yiii
    0x1703 | EXCLUSION,  // Tglg
    0x1723 | EXCLUSION,  // Hano
    0x1743 | EXCLUSION,  // Buhd
    0x1763 | EXCLUSION,  // Tagb
    0x280E | UNKNOWN,  // Brai
    0x10800 | EXCLUSION | RTL,  // Cprt
    0x1900 | LIMITED_USE,  // Limb
    0x10000 | EXCLUSION,  // Linb
    0x10480 | EXCLUSION,  // Osma
    0x10450 | EXCLUSION,  // Shaw
    0x1950 | LIMITED_USE | LB_LETTERS,  // Tale
    0x10380 | EXCLUSION,  // Ugar
    0,
    0x1A00 | EXCLUSION,  // Bugi
    0x2C00 | EXCLUSION | CASED,  // Glag
    0x10A00 | EXCLUSION | RTL,  // Khar
    0xA800 | LIMITED_USE,  // Sylo
    0x1980 | LIMITED_USE | LB_LETTERS,  // Talu
    0x2D30 | LIMITED_USE,  // Tfng
    0x103A0 | EXCLUSION,  // Xpeo
    0x1B05 | LIMITED_USE,  // Bali
    0x1BC0 | LIMITED_USE,  // Batk
    0,
    0x11005 | EXCLUSION,  // Brah
    0xAA00 | LIMITED_USE,  // Cham
    0,
    0,
    0,
    0,
    0x13153 | EXCLUSION,  // Egyp
    0,
    0x5B57 | RECOMMENDED | LB_LETTERS,  // Hans
    0x5B57 | RECOMMENDED | LB_LETTERS,  // Hant
    0x16B1C | EXCLUSION,  // Hmng
    0x10CA1 | EXCLUSION | RTL | CASED,  // Hung
    0,
    0xA984 | LIMITED_USE,  // Java
    0xA90A | LIMITED_USE,  // Kali
    0,
    0,
    0x1C00 | LIMITED_USE,  // Lepc
    0x10647 | EXCLUSION,  // Lina
    0x0840 | LIMITED_USE | RTL,  // Mand
    0,
    0x10980 | EXCLUSION | RTL,  // Mero
    0x07CA | LIMITED_USE | RTL,  // Nkoo
    0x10C00 | EXCLUSION | RTL,  // Orkh
    0x1036B | EXCLUSION,  // Perm
    0xA840 | EXCLUSION,  // Phag
    0x10900 | EXCLUSION | RTL,  // Phnx
    0x16F00 | LIMITED_USE,  // Plrd
    0,
    0,
    0,
    0,
    0,
    0,
    0xA549 | LIMITED_USE,  // Vaii
    0,
    0x12000 | EXCLUSION,  // Xsux
    0,
    0xFDD0 | UNKNOWN,  // Zzzz
    0x102A0 | EXCLUSION,  // Cari
    0x304B | RECOMMENDED | LB_LETTERS,  // Jpan
    0x1A20 | LIMITED_USE | LB_LETTERS,  // Lana
    0x10280 | EXCLUSION,  // Lyci
    0x10920 | EXCLUSION | RTL,  // Lydi
    0x1C5A | LIMITED_USE,  // Olck
    0xA930 | EXCLUSION,  // Rjng
    0xA882 | LIMITED_USE,  // Saur
    0x1D850 | EXCLUSION,  // Sgnw
    0x1B83 | LIMITED_USE,  // Sund
    0,
    0xABC0 | LIMITED_USE,  // Mtei
    0x10840 | EXCLUSION | RTL,  // Armi
    0x10B00 | EXCLUSION | RTL,  // Avst
    0x11103 | LIMITED_USE,  // Cakm
    0xAC00 | RECOMMENDED,  // Kore
    0x11083 | EXCLUSION,  // Kthi
    0x10AD8 | EXCLUSION | RTL,  // Mani
    0x10B60 | EXCLUSION | RTL,  // Phli
    0x10B8F | EXCLUSION | RTL,  // Phlp
    0,
    0x10B40 | EXCLUSION | RTL,  // Prti
    0x0800 | EXCLUSION | RTL,  // Samr
    0xAA80 | LIMITED_USE | LB_LETTERS,  // Tavt
    0,
    0,
    0xA6A0 | LIMITED_USE,  // Bamu
    0xA4D0 | LIMITED_USE,  // Lisu
    0,
    0x10A60 | EXCLUSION | RTL,  // Sarb
    0x16AE6 | EXCLUSION,  // Bass
    0x1BC20 | EXCLUSION,  // Dupl
    0x10500 | EXCLUSION,  // Elba
    0x11315 | EXCLUSION,  // Gran
    0,
    0,
    0x1E802 | EXCLUSION | RTL,  // Mend
    0x109A0 | EXCLUSION | RTL,  // Merc
    0x10A95 | EXCLUSION | RTL,  // Narb
    0x10896 | EXCLUSION | RTL,  // Nbat
    0x10873 | EXCLUSION | RTL,  // Palm
    0x112BE | EXCLUSION,  // Sind
    0x118B4 | EXCLUSION | CASED,  // Wara
    0,
    0,
    0x16A4F | EXCLUSION,  // Mroo
    0x1B1C4 | EXCLUSION | LB_LETTERS,  // Nshu
    0x11183 | EXCLUSION,  // Shrd
    0x110D0 | EXCLUSION,  // Sora
    0x11680 | EXCLUSION,  // Takr
    0x18229 | EXCLUSION | LB_LETTERS,  // Tang
    0,
    0x14400 | EXCLUSION,  // Hluw
    0x11208 | EXCLUSION,  // Khoj
    0x11484 | EXCLUSION,  // Tirh
    0x10537 | EXCLUSION,  // Aghb
    0x11152 | EXCLUSION,  // Mahj
    0x11717 | EXCLUSION | LB_LETTERS,  // Ahom
    0x108F4 | EXCLUSION | RTL,  // Hatr
    0x1160E | EXCLUSION,  // Modi
    0x1128F | EXCLUSION,  // Mult
    0x11AC0 | EXCLUSION,  // Pauc
    0x1158E | EXCLUSION,  // Sidd
    0x1E909 | LIMITED_USE | RTL | CASED,  // Adlm
    0x11C0E | EXCLUSION,  // Bhks
    0x11C72 | EXCLUSION,  // Marc
    0x11412 | LIMITED_USE,  // Newa
    0x104B5 | LIMITED_USE | CASED,  // Osge
    0x5B57 | RECOMMENDED | LB_LETTERS,  // Hanb
    0x1112 | RECOMMENDED,  // Jamo
    0,
    0x11D10 | EXCLUSION,  // Gonm
    0x11A5C | EXCLUSION,  // Soyo
    0x11A0B | EXCLUSION,  // Zanb
    0x1180B | EXCLUSION,  // Dogr
    0x11D71 | LIMITED_USE,  // Gong
    0x11EE5 | EXCLUSION,  // Maka
    0x16E40 | EXCLUSION | CASED,  // Medf
    0x10D12 | LIMITED_USE | RTL,  // Rohg
    0x10F42 | EXCLUSION | RTL,  // Sogd
    0x10F19 | EXCLUSION | RTL,  // Sogo
    0x10FF1 | EXCLUSION | RTL,  // Elym
    0x1E108 | LIMITED_USE,  // Hmnp
    0x119CE | EXCLUSION,  // Nand
    0x1E2E1 | LIMITED_USE,  // Wcho
    0x10FBF | EXCLUSION | RTL,  // Chrs
    0x1190C | EXCLUSION,  // Diak
    0x18C65 | EXCLUSION | LB_LETTERS,  // Kits
    0x10E88 | EXCLUSION | RTL,  // Yezi
    0x12FE5 | EXCLUSION,  // Cpmn
    0x10F7C | EXCLUSION | RTL,  // Ougr
    0x16ABC | EXCLUSION,  // Tnsa
    0x1E290 | EXCLUSION,  // Toto
    0x10582 | EXCLUSION | CASED,  // Vith
    0x11F1B | EXCLUSION | LB_LETTERS,  // Kawi
    0x1E4E6 | EXCLUSION,  // Nagm
    0,
    0x10D5D | EXCLUSION | RTL | CASED,  // Gara
    0x1611C | EXCLUSION,  // Gukh
    0x16D45 | EXCLUSION,  // Krai
    0x1E5D0 | EXCLUSION,  // Onao
    0x11BC4 | EXCLUSION,  // Sunu
    0x105C2 | EXCLUSION,  // Todr
    0x11392 | EXCLUSION,  // Tutg
    // End copy-paste from parsescriptmetadata.py
};

int32_t getScriptProps(UScriptCode script) {
    if (0 <= script && script < UPRV_LENGTHOF(SCRIPT_PROPS)) {
        return SCRIPT_PROPS[script];
    } else {
        return 0;
    }
}

}  // namespace

U_CAPI int32_t U_EXPORT2
uscript_getSampleString(UScriptCode script, char16_t *dest, int32_t capacity, UErrorCode *pErrorCode) {
    if(U_FAILURE(*pErrorCode)) { return 0; }
    if(capacity < 0 || (capacity > 0 && dest == nullptr)) {
        *pErrorCode = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    int32_t sampleChar = getScriptProps(script) & 0x1fffff;
    int32_t length;
    if(sampleChar == 0) {
        length = 0;
    } else {
        length = U16_LENGTH(sampleChar);
        if(length <= capacity) {
            int32_t i = 0;
            U16_APPEND_UNSAFE(dest, i, sampleChar);
        }
    }
    return u_terminateUChars(dest, capacity, length, pErrorCode);
}

U_COMMON_API icu::UnicodeString U_EXPORT2
uscript_getSampleUnicodeString(UScriptCode script) {
    icu::UnicodeString sample;
    int32_t sampleChar = getScriptProps(script) & 0x1fffff;
    if(sampleChar != 0) {
        sample.append(sampleChar);
    }
    return sample;
}

U_CAPI UScriptUsage U_EXPORT2
uscript_getUsage(UScriptCode script) {
    return (UScriptUsage)((getScriptProps(script) >> 21) & 7);
}

U_CAPI UBool U_EXPORT2
uscript_isRightToLeft(UScriptCode script) {
    return (getScriptProps(script) & RTL) != 0;
}

U_CAPI UBool U_EXPORT2
uscript_breaksBetweenLetters(UScriptCode script) {
    return (getScriptProps(script) & LB_LETTERS) != 0;
}

U_CAPI UBool U_EXPORT2
uscript_isCased(UScriptCode script) {
    return (getScriptProps(script) & CASED) != 0;
}
