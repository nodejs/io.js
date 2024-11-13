// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
******************************************************************************
*
*   Copyright (C) 2000-2016, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
******************************************************************************
*   file name:  ucnvmbcs.cpp
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 2000jul03
*   created by: Markus W. Scherer
*
*   The current code in this file replaces the previous implementation
*   of conversion code from multi-byte codepages to Unicode and back.
*   This implementation supports the following:
*   - legacy variable-length codepages with up to 4 bytes per character
*   - all Unicode code points (up to 0x10ffff)
*   - efficient distinction of unassigned vs. illegal byte sequences
*   - it is possible in fromUnicode() to directly deal with simple
*     stateful encodings (used for EBCDIC_STATEFUL)
*   - it is possible to convert Unicode code points
*     to a single zero byte (but not as a fallback except for SBCS)
*
*   Remaining limitations in fromUnicode:
*   - byte sequences must not have leading zero bytes
*   - except for SBCS codepages: no fallback mapping from Unicode to a zero byte
*   - limitation to up to 4 bytes per character
*
*   ICU 2.8 (late 2003) adds a secondary data structure which lifts some of these
*   limitations and adds m:n character mappings and other features.
*   See ucnv_ext.h for details.
*
*   Change history: 
*
*    5/6/2001       Ram       Moved  MBCS_SINGLE_RESULT_FROM_U,MBCS_STAGE_2_FROM_U,
*                             MBCS_VALUE_2_FROM_STAGE_2, MBCS_VALUE_4_FROM_STAGE_2
*                             macros to ucnvmbcs.h file
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_CONVERSION && !UCONFIG_NO_LEGACY_CONVERSION

#include "unicode/ucnv.h"
#include "unicode/ucnv_cb.h"
#include "unicode/udata.h"
#include "unicode/uset.h"
#include "unicode/utf8.h"
#include "unicode/utf16.h"
#include "ucnv_bld.h"
#include "ucnvmbcs.h"
#include "ucnv_ext.h"
#include "ucnv_cnv.h"
#include "cmemory.h"
#include "cstring.h"
#include "umutex.h"
#include "ustr_imp.h"

/* control optimizations according to the platform */
#define MBCS_UNROLL_SINGLE_TO_BMP 1
#define MBCS_UNROLL_SINGLE_FROM_BMP 0

/*
 * _MBCSHeader versions 5.3 & 4.3
 * (Note that the _MBCSHeader version is in addition to the converter formatVersion.)
 *
 * This version is optional. Version 5 is used for incompatible data format changes.
 * makeconv will continue to generate version 4 files if possible.
 *
 * Changes from version 4:
 *
 * The main difference is an additional _MBCSHeader field with
 * - the length (number of uint32_t) of the _MBCSHeader
 * - flags for further incompatible data format changes
 * - flags for further, backward compatible data format changes
 *
 * The MBCS_OPT_FROM_U flag indicates that most of the fromUnicode data is omitted from
 * the file and needs to be reconstituted at load time.
 * This requires a utf8Friendly format with an additional mbcsIndex table for fast
 * (and UTF-8-friendly) fromUnicode conversion for Unicode code points up to maxFastUChar.
 * (For details about these structures see below, and see ucnvmbcs.h.)
 *
 *   utf8Friendly also implies that the fromUnicode mappings are stored in ascending order
 *   of the Unicode code points. (This requires that the .ucm file has the |0 etc.
 *   precision markers for all mappings.)
 *
 *   All fallbacks have been moved to the extension table, leaving only roundtrips in the
 *   omitted data that can be reconstituted from the toUnicode data.
 *
 *   Of the stage 2 table, the part corresponding to maxFastUChar and below is omitted.
 *   With only roundtrip mappings in the base fromUnicode data, this part is fully
 *   redundant with the mbcsIndex and will be reconstituted from that (also using the
 *   stage 1 table which contains the information about how stage 2 was compacted).
 *
 *   The rest of the stage 2 table, the part for code points above maxFastUChar,
 *   is stored in the file and will be appended to the reconstituted part.
 *
 *   The entire fromUBytes array is omitted from the file and will be reconstitued.
 *   This is done by enumerating all toUnicode roundtrip mappings, performing
 *   each mapping (using the stage 1 and reconstituted stage 2 tables) and
 *   writing instead of reading the byte values.
 *
 * _MBCSHeader version 4.3
 *
 * Change from version 4.2:
 * - Optional utf8Friendly data structures, with 64-entry stage 3 block
 *   allocation for parts of the BMP, and an additional mbcsIndex in non-SBCS
 *   files which can be used instead of stages 1 & 2.
 *   Faster lookups for roundtrips from most commonly used characters,
 *   and lookups from UTF-8 byte sequences with a natural bit distribution.
 *   See ucnvmbcs.h for more details.
 *
 * Change from version 4.1:
 * - Added an optional extension table structure at the end of the .cnv file.
 *   It is present if the upper bits of the header flags field contains a non-zero
 *   byte offset to it.
 *   Files that contain only a conversion table and no base table
 *   use the special outputType MBCS_OUTPUT_EXT_ONLY.
 *   These contain the base table name between the MBCS header and the extension
 *   data.
 *
 * Change from version 4.0:
 * - Replace header.reserved with header.fromUBytesLength so that all
 *   fields in the data have length.
 *
 * Changes from version 3 (for performance improvements):
 * - new bit distribution for state table entries
 * - reordered action codes
 * - new data structure for single-byte fromUnicode
 *   + stage 2 only contains indexes
 *   + stage 3 stores 16 bits per character with classification bits 15..8
 * - no multiplier for stage 1 entries
 * - stage 2 for non-single-byte codepages contains the index and the flags in
 *   one 32-bit value
 * - 2-byte and 4-byte fromUnicode results are stored directly as 16/32-bit integers
 *
 * For more details about old versions of the MBCS data structure, see
 * the corresponding versions of this file.
 *
 * Converting stateless codepage data ---------------------------------------***
 * (or codepage data with simple states) to Unicode.
 *
 * Data structure and algorithm for converting from complex legacy codepages
 * to Unicode. (Designed before 2000-may-22.)
 *
 * The basic idea is that the structure of legacy codepages can be described
 * with state tables.
 * When reading a byte stream, each input byte causes a state transition.
 * Some transitions result in the output of a code point, some result in
 * "unassigned" or "illegal" output.
 * This is used here for character conversion.
 *
 * The data structure begins with a state table consisting of a row
 * per state, with 256 entries (columns) per row for each possible input
 * byte value.
 * Each entry is 32 bits wide, with two formats distinguished by
 * the sign bit (bit 31):
 *
 * One format for transitional entries (bit 31 not set) for non-final bytes, and
 * one format for final entries (bit 31 set).
 * Both formats contain the number of the next state in the same bit
 * positions.
 * State 0 is the initial state.
 *
 * Most of the time, the offset values of subsequent states are added
 * up to a scalar value. This value will eventually be the index of
 * the Unicode code point in a table that follows the state table.
 * The effect is that the code points for final state table rows
 * are contiguous. The code points of final state rows follow each other
 * in the order of the references to those final states by previous
 * states, etc.
 *
 * For some terminal states, the offset is itself the output Unicode
 * code point (16 bits for a BMP code point or 20 bits for a supplementary
 * code point (stored as code point minus 0x10000 so that 20 bits are enough).
 * For others, the code point in the Unicode table is stored with either
 * one or two code units: one for BMP code points, two for a pair of
 * surrogates.
 * All code points for a final state entry take up the same number of code
 * units, regardless of whether they all actually _use_ the same number
 * of code units. This is necessary for simple array access.
 *
 * An additional feature comes in with what in ICU is called "fallback"
 * mappings:
 *
 * In addition to round-trippable, precise, 1:1 mappings, there are often
 * mappings defined between similar, though not the same, characters.
 * Typically, such mappings occur only in fromUnicode mapping tables because
 * Unicode has a superset repertoire of most other codepages. However, it
 * is possible to provide such mappings in the toUnicode tables, too.
 * In this case, the fallback mappings are partly integrated into the
 * general state tables because the structure of the encoding includes their
 * byte sequences.
 * For final entries in an initial state, fallback mappings are stored in
 * the entry itself like with roundtrip mappings.
 * For other final entries, they are stored in the code units table if
 * the entry is for a pair of code units.
 * For single-unit results in the code units table, there is no space to
 * alternatively hold a fallback mapping; in this case, the code unit
 * is stored as U+fffe (unassigned), and the fallback mapping needs to
 * be looked up by the scalar offset value in a separate table.
 *
 * "Unassigned" state entries really mean "structurally unassigned",
 * i.e., such a byte sequence will never have a mapping result.
 *
 * The interpretation of the bits in each entry is as follows:
 *
 * Bit 31 not set, not a terminal entry ("transitional"):
 * 30..24 next state
 * 23..0  offset delta, to be added up
 *
 * Bit 31 set, terminal ("final") entry:
 * 30..24 next state (regardless of action code)
 * 23..20 action code:
 *        action codes 0 and 1 result in precise-mapping Unicode code points
 *        0  valid byte sequence
 *           19..16 not used, 0
 *           15..0  16-bit Unicode BMP code point
 *                  never U+fffe or U+ffff
 *        1  valid byte sequence
 *           19..0  20-bit Unicode supplementary code point
 *                  never U+fffe or U+ffff
 *
 *        action codes 2 and 3 result in fallback (unidirectional-mapping) Unicode code points
 *        2  valid byte sequence (fallback)
 *           19..16 not used, 0
 *           15..0  16-bit Unicode BMP code point as fallback result
 *        3  valid byte sequence (fallback)
 *           19..0  20-bit Unicode supplementary code point as fallback result
 *
 *        action codes 4 and 5 may result in roundtrip/fallback/unassigned/illegal results
 *        depending on the code units they result in
 *        4  valid byte sequence
 *           19..9  not used, 0
 *            8..0  final offset delta
 *                  pointing to one 16-bit code unit which may be
 *                  fffe  unassigned -- look for a fallback for this offset
 *                  ffff  illegal
 *        5  valid byte sequence
 *           19..9  not used, 0
 *            8..0  final offset delta
 *                  pointing to two 16-bit code units
 *                  (typically UTF-16 surrogates)
 *                  the result depends on the first code unit as follows:
 *                  0000..d7ff  roundtrip BMP code point (1st alone)
 *                  d800..dbff  roundtrip surrogate pair (1st, 2nd)
 *                  dc00..dfff  fallback surrogate pair (1st-400, 2nd)
 *                  e000        roundtrip BMP code point (2nd alone)
 *                  e001        fallback BMP code point (2nd alone)
 *                  fffe        unassigned
 *                  ffff        illegal
 *           (the final offset deltas are at most 255 * 2,
 *            times 2 because of storing code unit pairs)
 *
 *        6  unassigned byte sequence
 *           19..16 not used, 0
 *           15..0  16-bit Unicode BMP code point U+fffe (new with version 2)
 *                  this does not contain a final offset delta because the main
 *                  purpose of this action code is to save scalar offset values;
 *                  therefore, fallback values cannot be assigned to byte
 *                  sequences that result in this action code
 *        7  illegal byte sequence
 *           19..16 not used, 0
 *           15..0  16-bit Unicode BMP code point U+ffff (new with version 2)
 *        8  state change only
 *           19..0  not used, 0
 *           useful for state changes in simple stateful encodings,
 *           at Shift-In/Shift-Out codes
 *
 *
 *        9..15 reserved for future use
 *           current implementations will only perform a state change
 *           and ignore bits 19..0
 *
 * An encoding with contiguous ranges of unassigned byte sequences, like
 * Shift-JIS and especially EUC-TW, can be stored efficiently by having
 * at least two states for the trail bytes:
 * One trail byte state that results in code points, and one that only
 * has "unassigned" and "illegal" terminal states.
 *
 * Note: partly by accident, this data structure supports simple stateful
 * encodings without any additional logic.
 * Currently, only simple Shift-In/Shift-Out schemes are handled with
 * appropriate state tables (especially EBCDIC_STATEFUL!).
 *
 * MBCS version 2 added:
 * unassigned and illegal action codes have U+fffe and U+ffff
 * instead of unused bits; this is useful for _MBCS_SINGLE_SIMPLE_GET_NEXT_BMP()
 *
 * Converting from Unicode to codepage bytes --------------------------------***
 *
 * The conversion data structure for fromUnicode is designed for the known
 * structure of Unicode. It maps from 21-bit code points (0..0x10ffff) to
 * a sequence of 1..4 bytes, in addition to a flag that indicates if there is
 * a roundtrip mapping.
 *
 * The lookup is done with a 3-stage trie, using 11/6/4 bits for stage 1/2/3
 * like in the character properties table.
 * The beginning of the trie is at offsetFromUTable, the beginning of stage 3
 * with the resulting bytes is at offsetFromUBytes.
 *
 * Beginning with version 4, single-byte codepages have a significantly different
 * trie compared to other codepages.
 * In all cases, the entry in stage 1 is directly the index of the block of
 * 64 entries in stage 2.
 *
 * Single-byte lookup:
 *
 * Stage 2 only contains 16-bit indexes directly to the 16-blocks in stage 3.
 * Stage 3 contains one 16-bit word per result:
 * Bits 15..8 indicate the kind of result:
 *    f  roundtrip result
 *    c  fallback result from private-use code point
 *    8  fallback result from other code points
 *    0  unassigned
 * Bits 7..0 contain the codepage byte. A zero byte is always possible.
 *
 * In version 4.3, the runtime code can build an sbcsIndex for a utf8Friendly
 * file. For 2-byte UTF-8 byte sequences and some 3-byte sequences the lookup
 * becomes a 2-stage (single-index) trie lookup with 6 bits for stage 3.
 * ASCII code points can be looked up with a linear array access into stage 3.
 * See maxFastUChar and other details in ucnvmbcs.h.
 *
 * Multi-byte lookup:
 *
 * Stage 2 contains a 32-bit word for each 16-block in stage 3:
 * Bits 31..16 contain flags for which stage 3 entries contain roundtrip results
 *             test: MBCS_FROM_U_IS_ROUNDTRIP(stage2Entry, c)
 *             If this test is false, then a non-zero result will be interpreted as
 *             a fallback mapping.
 * Bits 15..0  contain the index to stage 3, which must be multiplied by 16*(bytes per char)
 *
 * Stage 3 contains 2, 3, or 4 bytes per result.
 * 2 or 4 bytes are stored as uint16_t/uint32_t in platform endianness,
 * while 3 bytes are stored as bytes in big-endian order.
 * Leading zero bytes are ignored, and the number of bytes is counted.
 * A zero byte mapping result is possible as a roundtrip result.
 * For some output types, the actual result is processed from this;
 * see ucnv_MBCSFromUnicodeWithOffsets().
 *
 * Note that stage 1 always contains 0x440=1088 entries (0x440==0x110000>>10),
 * or (version 3 and up) for BMP-only codepages, it contains 64 entries.
 *
 * In version 4.3, a utf8Friendly file contains an mbcsIndex table.
 * For 2-byte UTF-8 byte sequences and most 3-byte sequences the lookup
 * becomes a 2-stage (single-index) trie lookup with 6 bits for stage 3.
 * ASCII code points can be looked up with a linear array access into stage 3.
 * See maxFastUChar, mbcsIndex and other details in ucnvmbcs.h.
 *
 * In version 3, stage 2 blocks may overlap by multiples of the multiplier
 * for compaction.
 * In version 4, stage 2 blocks (and for single-byte codepages, stage 3 blocks)
 * may overlap by any number of entries.
 *
 * MBCS version 2 added:
 * the converter checks for known output types, which allows
 * adding new ones without crashing an unaware converter
 */

/**
 * Callback from ucnv_MBCSEnumToUnicode(), takes 32 mappings from
 * consecutive sequences of bytes, starting from the one encoded in value,
 * to Unicode code points. (Multiple mappings to reduce per-function call overhead.)
 * Does not currently support m:n mappings or reverse fallbacks.
 * This function will not be called for sequences of bytes with leading zeros.
 *
 * @param context an opaque pointer, as passed into ucnv_MBCSEnumToUnicode()
 * @param value contains 1..4 bytes of the first byte sequence, right-aligned
 * @param codePoints resulting Unicode code points, or negative if a byte sequence does
 *        not map to anything
 * @return true to continue enumeration, false to stop
 */
typedef UBool U_CALLCONV
UConverterEnumToUCallback(const void *context, uint32_t value, UChar32 codePoints[32]);

static void U_CALLCONV
ucnv_MBCSLoad(UConverterSharedData *sharedData,
          UConverterLoadArgs *pArgs,
          const uint8_t *raw,
          UErrorCode *pErrorCode);

static void U_CALLCONV
ucnv_MBCSUnload(UConverterSharedData *sharedData);

static void U_CALLCONV
ucnv_MBCSOpen(UConverter *cnv,
              UConverterLoadArgs *pArgs,
              UErrorCode *pErrorCode);

static UChar32 U_CALLCONV
ucnv_MBCSGetNextUChar(UConverterToUnicodeArgs *pArgs,
                  UErrorCode *pErrorCode);

static void U_CALLCONV
ucnv_MBCSGetStarters(const UConverter* cnv,
                 UBool starters[256],
                 UErrorCode *pErrorCode);

U_CDECL_BEGIN
static const char* U_CALLCONV
ucnv_MBCSGetName(const UConverter *cnv);
U_CDECL_END

static void U_CALLCONV
ucnv_MBCSWriteSub(UConverterFromUnicodeArgs *pArgs,
              int32_t offsetIndex,
              UErrorCode *pErrorCode);

static UChar32 U_CALLCONV
ucnv_MBCSGetNextUChar(UConverterToUnicodeArgs *pArgs,
                  UErrorCode *pErrorCode);

static void U_CALLCONV
ucnv_SBCSFromUTF8(UConverterFromUnicodeArgs *pFromUArgs,
                  UConverterToUnicodeArgs *pToUArgs,
                  UErrorCode *pErrorCode);

static void U_CALLCONV
ucnv_MBCSGetUnicodeSet(const UConverter *cnv,
                   const USetAdder *sa,
                   UConverterUnicodeSet which,
                   UErrorCode *pErrorCode);

static void U_CALLCONV
ucnv_DBCSFromUTF8(UConverterFromUnicodeArgs *pFromUArgs,
                  UConverterToUnicodeArgs *pToUArgs,
                  UErrorCode *pErrorCode);

static const UConverterImpl _SBCSUTF8Impl={
    UCNV_MBCS,

    ucnv_MBCSLoad,
    ucnv_MBCSUnload,

    ucnv_MBCSOpen,
    nullptr,
    nullptr,

    ucnv_MBCSToUnicodeWithOffsets,
    ucnv_MBCSToUnicodeWithOffsets,
    ucnv_MBCSFromUnicodeWithOffsets,
    ucnv_MBCSFromUnicodeWithOffsets,
    ucnv_MBCSGetNextUChar,

    ucnv_MBCSGetStarters,
    ucnv_MBCSGetName,
    ucnv_MBCSWriteSub,
    nullptr,
    ucnv_MBCSGetUnicodeSet,

    nullptr,
    ucnv_SBCSFromUTF8
};

static const UConverterImpl _DBCSUTF8Impl={
    UCNV_MBCS,

    ucnv_MBCSLoad,
    ucnv_MBCSUnload,

    ucnv_MBCSOpen,
    nullptr,
    nullptr,

    ucnv_MBCSToUnicodeWithOffsets,
    ucnv_MBCSToUnicodeWithOffsets,
    ucnv_MBCSFromUnicodeWithOffsets,
    ucnv_MBCSFromUnicodeWithOffsets,
    ucnv_MBCSGetNextUChar,

    ucnv_MBCSGetStarters,
    ucnv_MBCSGetName,
    ucnv_MBCSWriteSub,
    nullptr,
    ucnv_MBCSGetUnicodeSet,

    nullptr,
    ucnv_DBCSFromUTF8
};

static const UConverterImpl _MBCSImpl={
    UCNV_MBCS,

    ucnv_MBCSLoad,
    ucnv_MBCSUnload,

    ucnv_MBCSOpen,
    nullptr,
    nullptr,

    ucnv_MBCSToUnicodeWithOffsets,
    ucnv_MBCSToUnicodeWithOffsets,
    ucnv_MBCSFromUnicodeWithOffsets,
    ucnv_MBCSFromUnicodeWithOffsets,
    ucnv_MBCSGetNextUChar,

    ucnv_MBCSGetStarters,
    ucnv_MBCSGetName,
    ucnv_MBCSWriteSub,
    nullptr,
    ucnv_MBCSGetUnicodeSet,
    nullptr,
    nullptr
};

/* Static data is in tools/makeconv/ucnvstat.c for data-based
 * converters. Be sure to update it as well.
 */

const UConverterSharedData _MBCSData={
    sizeof(UConverterSharedData), 1,
    nullptr, nullptr, false, true, &_MBCSImpl,
    0, UCNV_MBCS_TABLE_INITIALIZER
};


/* GB 18030 data ------------------------------------------------------------ */

/* helper macros for linear values for GB 18030 four-byte sequences */
#define LINEAR_18030(a, b, c, d) ((((a)*10+(b))*126L+(c))*10L+(d))

#define LINEAR_18030_BASE LINEAR_18030(0x81, 0x30, 0x81, 0x30)

#define LINEAR(x) LINEAR_18030(x>>24, (x>>16)&0xff, (x>>8)&0xff, x&0xff)

/*
 * Some ranges of GB 18030 where both the Unicode code points and the
 * GB four-byte sequences are contiguous and are handled algorithmically by
 * the special callback functions below.
 * The values are start & end of Unicode & GB codes.
 *
 * Note that single surrogates are not mapped by GB 18030
 * as of the re-released mapping tables from 2000-nov-30.
 */
static const uint32_t
gb18030Ranges[14][4]={
    {0x10000, 0x10FFFF, LINEAR(0x90308130), LINEAR(0xE3329A35)},
    {0x9FA6, 0xD7FF, LINEAR(0x82358F33), LINEAR(0x8336C738)},
    {0x0452, 0x1E3E, LINEAR(0x8130D330), LINEAR(0x8135F436)},
    {0x1E40, 0x200F, LINEAR(0x8135F438), LINEAR(0x8136A531)},
    {0xE865, 0xF92B, LINEAR(0x8336D030), LINEAR(0x84308534)},
    {0x2643, 0x2E80, LINEAR(0x8137A839), LINEAR(0x8138FD38)},
    {0xFA2A, 0xFE2F, LINEAR(0x84309C38), LINEAR(0x84318537)},
    {0x3CE1, 0x4055, LINEAR(0x8231D438), LINEAR(0x8232AF32)},
    {0x361B, 0x3917, LINEAR(0x8230A633), LINEAR(0x8230F237)},
    {0x49B8, 0x4C76, LINEAR(0x8234A131), LINEAR(0x8234E733)},
    {0x4160, 0x4336, LINEAR(0x8232C937), LINEAR(0x8232F837)},
    {0x478E, 0x4946, LINEAR(0x8233E838), LINEAR(0x82349638)},
    {0x44D7, 0x464B, LINEAR(0x8233A339), LINEAR(0x8233C931)},
    {0xFFE6, 0xFFFF, LINEAR(0x8431A234), LINEAR(0x8431A439)}
};

/* bit flag for UConverter.options indicating GB 18030 special handling */
#define _MBCS_OPTION_GB18030 0x8000

/* bit flag for UConverter.options indicating KEIS,JEF,JIF special handling */
#define _MBCS_OPTION_KEIS 0x01000
#define _MBCS_OPTION_JEF  0x02000
#define _MBCS_OPTION_JIPS 0x04000

#define KEIS_SO_CHAR_1 0x0A
#define KEIS_SO_CHAR_2 0x42
#define KEIS_SI_CHAR_1 0x0A
#define KEIS_SI_CHAR_2 0x41

#define JEF_SO_CHAR 0x28
#define JEF_SI_CHAR 0x29

#define JIPS_SO_CHAR_1 0x1A
#define JIPS_SO_CHAR_2 0x70
#define JIPS_SI_CHAR_1 0x1A
#define JIPS_SI_CHAR_2 0x71

enum SISO_Option {
    SI,
    SO
};
typedef enum SISO_Option SISO_Option;

static int32_t getSISOBytes(SISO_Option option, uint32_t cnvOption, uint8_t *value) {
    int32_t SISOLength = 0;

    switch (option) {
        case SI:
            if ((cnvOption&_MBCS_OPTION_KEIS)!=0) {
                value[0] = KEIS_SI_CHAR_1;
                value[1] = KEIS_SI_CHAR_2;
                SISOLength = 2;
            } else if ((cnvOption&_MBCS_OPTION_JEF)!=0) {
                value[0] = JEF_SI_CHAR;
                SISOLength = 1;
            } else if ((cnvOption&_MBCS_OPTION_JIPS)!=0) {
                value[0] = JIPS_SI_CHAR_1;
                value[1] = JIPS_SI_CHAR_2;
                SISOLength = 2;
            } else {
                value[0] = UCNV_SI;
                SISOLength = 1;
            }
            break;
        case SO:
            if ((cnvOption&_MBCS_OPTION_KEIS)!=0) {
                value[0] = KEIS_SO_CHAR_1;
                value[1] = KEIS_SO_CHAR_2;
                SISOLength = 2;
            } else if ((cnvOption&_MBCS_OPTION_JEF)!=0) {
                value[0] = JEF_SO_CHAR;
                SISOLength = 1;
            } else if ((cnvOption&_MBCS_OPTION_JIPS)!=0) {
                value[0] = JIPS_SO_CHAR_1;
                value[1] = JIPS_SO_CHAR_2;
                SISOLength = 2;
            } else {
                value[0] = UCNV_SO;
                SISOLength = 1;
            }
            break;
        default:
            /* Should never happen. */
            break;
    }

    return SISOLength;
}

/* Miscellaneous ------------------------------------------------------------ */

/* similar to ucnv_MBCSGetNextUChar() but recursive */
static UBool
enumToU(UConverterMBCSTable *mbcsTable, int8_t stateProps[],
        int32_t state, uint32_t offset,
        uint32_t value,
        UConverterEnumToUCallback *callback, const void *context,
        UErrorCode *pErrorCode) {
    UChar32 codePoints[32];
    const int32_t *row;
    const uint16_t *unicodeCodeUnits;
    UChar32 anyCodePoints;
    int32_t b, limit;

    row=mbcsTable->stateTable[state];
    unicodeCodeUnits=mbcsTable->unicodeCodeUnits;

    value<<=8;
    anyCodePoints=-1;  /* becomes non-negative if there is a mapping */

    b=(stateProps[state]&0x38)<<2;
    if(b==0 && stateProps[state]>=0x40) {
        /* skip byte sequences with leading zeros because they are not stored in the fromUnicode table */
        codePoints[0]=U_SENTINEL;
        b=1;
    }
    limit=((stateProps[state]&7)+1)<<5;
    while(b<limit) {
        int32_t entry=row[b];
        if(MBCS_ENTRY_IS_TRANSITION(entry)) {
            int32_t nextState=MBCS_ENTRY_TRANSITION_STATE(entry);
            if(stateProps[nextState]>=0) {
                /* recurse to a state with non-ignorable actions */
                if(!enumToU(
                        mbcsTable, stateProps, nextState,
                        offset+MBCS_ENTRY_TRANSITION_OFFSET(entry),
                        value | static_cast<uint32_t>(b),
                        callback, context,
                        pErrorCode)) {
                    return false;
                }
            }
            codePoints[b&0x1f]=U_SENTINEL;
        } else {
            UChar32 c;
            int32_t action;

            /*
             * An if-else-if chain provides more reliable performance for
             * the most common cases compared to a switch.
             */
            action=MBCS_ENTRY_FINAL_ACTION(entry);
            if(action==MBCS_STATE_VALID_DIRECT_16) {
                /* output BMP code point */
                c = static_cast<char16_t>(MBCS_ENTRY_FINAL_VALUE_16(entry));
            } else if(action==MBCS_STATE_VALID_16) {
                int32_t finalOffset=offset+MBCS_ENTRY_FINAL_VALUE_16(entry);
                c=unicodeCodeUnits[finalOffset];
                if(c<0xfffe) {
                    /* output BMP code point */
                } else {
                    c=U_SENTINEL;
                }
            } else if(action==MBCS_STATE_VALID_16_PAIR) {
                int32_t finalOffset=offset+MBCS_ENTRY_FINAL_VALUE_16(entry);
                c=unicodeCodeUnits[finalOffset++];
                if(c<0xd800) {
                    /* output BMP code point below 0xd800 */
                } else if(c<=0xdbff) {
                    /* output roundtrip or fallback supplementary code point */
                    c=((c&0x3ff)<<10)+unicodeCodeUnits[finalOffset]+(0x10000-0xdc00);
                } else if(c==0xe000) {
                    /* output roundtrip BMP code point above 0xd800 or fallback BMP code point */
                    c=unicodeCodeUnits[finalOffset];
                } else {
                    c=U_SENTINEL;
                }
            } else if(action==MBCS_STATE_VALID_DIRECT_20) {
                /* output supplementary code point */
                c = static_cast<UChar32>(MBCS_ENTRY_FINAL_VALUE(entry) + 0x10000);
            } else {
                c=U_SENTINEL;
            }

            codePoints[b&0x1f]=c;
            anyCodePoints&=c;
        }
        if(((++b)&0x1f)==0) {
            if(anyCodePoints>=0) {
                if (!callback(context, value | static_cast<uint32_t>(b - 0x20), codePoints)) {
                    return false;
                }
                anyCodePoints=-1;
            }
        }
    }
    return true;
}

/*
 * Only called if stateProps[state]==-1.
 * A recursive call may do stateProps[state]|=0x40 if this state is the target of an
 * MBCS_STATE_CHANGE_ONLY.
 */
static int8_t
getStateProp(const int32_t (*stateTable)[256], int8_t stateProps[], int state) {
    const int32_t *row;
    int32_t min, max, entry, nextState;

    row=stateTable[state];
    stateProps[state]=0;

    /* find first non-ignorable state */
    for(min=0;; ++min) {
        entry=row[min];
        nextState=MBCS_ENTRY_STATE(entry);
        if(stateProps[nextState]==-1) {
            getStateProp(stateTable, stateProps, nextState);
        }
        if(MBCS_ENTRY_IS_TRANSITION(entry)) {
            if(stateProps[nextState]>=0) {
                break;
            }
        } else if(MBCS_ENTRY_FINAL_ACTION(entry)<MBCS_STATE_UNASSIGNED) {
            break;
        }
        if(min==0xff) {
            stateProps[state]=-0x40;  /* (int8_t)0xc0 */
            return stateProps[state];
        }
    }
    stateProps[state] |= static_cast<int8_t>((min >> 5) << 3);

    /* find last non-ignorable state */
    for(max=0xff; min<max; --max) {
        entry=row[max];
        nextState=MBCS_ENTRY_STATE(entry);
        if(stateProps[nextState]==-1) {
            getStateProp(stateTable, stateProps, nextState);
        }
        if(MBCS_ENTRY_IS_TRANSITION(entry)) {
            if(stateProps[nextState]>=0) {
                break;
            }
        } else if(MBCS_ENTRY_FINAL_ACTION(entry)<MBCS_STATE_UNASSIGNED) {
            break;
        }
    }
    stateProps[state] |= static_cast<int8_t>(max >> 5);

    /* recurse further and collect direct-state information */
    while(min<=max) {
        entry=row[min];
        nextState=MBCS_ENTRY_STATE(entry);
        if(stateProps[nextState]==-1) {
            getStateProp(stateTable, stateProps, nextState);
        }
        if(MBCS_ENTRY_IS_FINAL(entry)) {
            stateProps[nextState]|=0x40;
            if(MBCS_ENTRY_FINAL_ACTION(entry)<=MBCS_STATE_FALLBACK_DIRECT_20) {
                stateProps[state]|=0x40;
            }
        }
        ++min;
    }
    return stateProps[state];
}

/*
 * Internal function enumerating the toUnicode data of an MBCS converter.
 * Currently only used for reconstituting data for a MBCS_OPT_NO_FROM_U
 * table, but could also be used for a future ucnv_getUnicodeSet() option
 * that includes reverse fallbacks (after updating this function's implementation).
 * Currently only handles roundtrip mappings.
 * Does not currently handle extensions.
 */
static void
ucnv_MBCSEnumToUnicode(UConverterMBCSTable *mbcsTable,
                       UConverterEnumToUCallback *callback, const void *context,
                       UErrorCode *pErrorCode) {
    /*
     * Properties for each state, to speed up the enumeration.
     * Ignorable actions are unassigned/illegal/state-change-only:
     * They do not lead to mappings.
     *
     * Bits 7..6:
     * 1 direct/initial state (stateful converters have multiple)
     * 0 non-initial state with transitions or with non-ignorable result actions
     * -1 final state with only ignorable actions
     *
     * Bits 5..3:
     * The lowest byte value with non-ignorable actions is
     * value<<5 (rounded down).
     *
     * Bits 2..0:
     * The highest byte value with non-ignorable actions is
     * (value<<5)&0x1f (rounded up).
     */
    int8_t stateProps[MBCS_MAX_STATE_COUNT];
    int32_t state;

    uprv_memset(stateProps, -1, sizeof(stateProps));

    /* recurse from state 0 and set all stateProps */
    getStateProp(mbcsTable->stateTable, stateProps, 0);

    for(state=0; state<mbcsTable->countStates; ++state) {
        /*if(stateProps[state]==-1) {
            printf("unused/unreachable <icu:state> %d\n", state);
        }*/
        if(stateProps[state]>=0x40) {
            /* start from each direct state */
            enumToU(
                mbcsTable, stateProps, state, 0, 0,
                callback, context,
                pErrorCode);
        }
    }
}

U_CFUNC void 
ucnv_MBCSGetFilteredUnicodeSetForUnicode(const UConverterSharedData *sharedData,
                                         const USetAdder *sa,
                                         UConverterUnicodeSet which,
                                         UConverterSetFilter filter,
                                         UErrorCode *pErrorCode) {
    const UConverterMBCSTable *mbcsTable;
    const uint16_t *table;

    uint32_t st3;
    uint16_t st1, maxStage1, st2;

    UChar32 c;

    /* enumerate the from-Unicode trie table */
    mbcsTable=&sharedData->mbcs;
    table=mbcsTable->fromUnicodeTable;
    if(mbcsTable->unicodeMask&UCNV_HAS_SUPPLEMENTARY) {
        maxStage1=0x440;
    } else {
        maxStage1=0x40;
    }

    c=0; /* keep track of the current code point while enumerating */

    if(mbcsTable->outputType==MBCS_OUTPUT_1) {
        const uint16_t *stage2, *stage3, *results;
        uint16_t minValue;

        results=(const uint16_t *)mbcsTable->fromUnicodeBytes;

        /*
         * Set a threshold variable for selecting which mappings to use.
         * See ucnv_MBCSSingleFromBMPWithOffsets() and
         * MBCS_SINGLE_RESULT_FROM_U() for details.
         */
        if(which==UCNV_ROUNDTRIP_SET) {
            /* use only roundtrips */
            minValue=0xf00;
        } else /* UCNV_ROUNDTRIP_AND_FALLBACK_SET */ {
            /* use all roundtrip and fallback results */
            minValue=0x800;
        }

        for(st1=0; st1<maxStage1; ++st1) {
            st2=table[st1];
            if(st2>maxStage1) {
                stage2=table+st2;
                for(st2=0; st2<64; ++st2) {
                    if((st3=stage2[st2])!=0) {
                        /* read the stage 3 block */
                        stage3=results+st3;

                        do {
                            if(*stage3++>=minValue) {
                                sa->add(sa->set, c);
                            }
                        } while((++c&0xf)!=0);
                    } else {
                        c+=16; /* empty stage 3 block */
                    }
                }
            } else {
                c+=1024; /* empty stage 2 block */
            }
        }
    } else {
        const uint32_t *stage2;
        const uint8_t *stage3, *bytes;
        uint32_t st3Multiplier;
        uint32_t value;
        UBool useFallback;

        bytes=mbcsTable->fromUnicodeBytes;

        useFallback = which == UCNV_ROUNDTRIP_AND_FALLBACK_SET;

        switch(mbcsTable->outputType) {
        case MBCS_OUTPUT_3:
        case MBCS_OUTPUT_4_EUC:
            st3Multiplier=3;
            break;
        case MBCS_OUTPUT_4:
            st3Multiplier=4;
            break;
        default:
            st3Multiplier=2;
            break;
        }

        for(st1=0; st1<maxStage1; ++st1) {
            st2=table[st1];
            if(st2>(maxStage1>>1)) {
                stage2=(const uint32_t *)table+st2;
                for(st2=0; st2<64; ++st2) {
                    if((st3=stage2[st2])!=0) {
                        /* read the stage 3 block */
                        stage3=bytes+st3Multiplier*16*(uint32_t)(uint16_t)st3;

                        /* get the roundtrip flags for the stage 3 block */
                        st3>>=16;

                        /*
                         * Add code points for which the roundtrip flag is set,
                         * or which map to non-zero bytes if we use fallbacks.
                         * See ucnv_MBCSFromUnicodeWithOffsets() for details.
                         */
                        switch(filter) {
                        case UCNV_SET_FILTER_NONE:
                            do {
                                if(st3&1) {
                                    sa->add(sa->set, c);
                                    stage3+=st3Multiplier;
                                } else if(useFallback) {
                                    uint8_t b=0;
                                    switch(st3Multiplier) {
                                    case 4:
                                        b|=*stage3++;
                                        U_FALLTHROUGH;
                                    case 3:
                                        b|=*stage3++;
                                        U_FALLTHROUGH;
                                    case 2:
                                        b|=stage3[0]|stage3[1];
                                        stage3+=2;
                                        U_FALLTHROUGH;
                                    default:
                                        break;
                                    }
                                    if(b!=0) {
                                        sa->add(sa->set, c);
                                    }
                                }
                                st3>>=1;
                            } while((++c&0xf)!=0);
                            break;
                        case UCNV_SET_FILTER_DBCS_ONLY:
                             /* Ignore single-byte results (<0x100). */
                            do {
                                if(((st3&1)!=0 || useFallback) && *((const uint16_t *)stage3)>=0x100) {
                                    sa->add(sa->set, c);
                                }
                                st3>>=1;
                                stage3+=2;  /* +=st3Multiplier */
                            } while((++c&0xf)!=0);
                            break;
                        case UCNV_SET_FILTER_2022_CN:
                             /* Only add code points that map to CNS 11643 planes 1 & 2 for non-EXT ISO-2022-CN. */
                            do {
                                if(((st3&1)!=0 || useFallback) && ((value=*stage3)==0x81 || value==0x82)) {
                                    sa->add(sa->set, c);
                                }
                                st3>>=1;
                                stage3+=3;  /* +=st3Multiplier */
                            } while((++c&0xf)!=0);
                            break;
                        case UCNV_SET_FILTER_SJIS:
                             /* Only add code points that map to Shift-JIS codes corresponding to JIS X 0208. */
                            do {
                                if(((st3&1)!=0 || useFallback) && (value=*((const uint16_t *)stage3))>=0x8140 && value<=0xeffc) {
                                    sa->add(sa->set, c);
                                }
                                st3>>=1;
                                stage3+=2;  /* +=st3Multiplier */
                            } while((++c&0xf)!=0);
                            break;
                        case UCNV_SET_FILTER_GR94DBCS:
                            /* Only add code points that map to ISO 2022 GR 94 DBCS codes (each byte A1..FE). */
                            do {
                                if( ((st3&1)!=0 || useFallback) &&
                                    (uint16_t)((value=*((const uint16_t *)stage3)) - 0xa1a1)<=(0xfefe - 0xa1a1) &&
                                    (uint8_t)(value-0xa1)<=(0xfe - 0xa1)
                                ) {
                                    sa->add(sa->set, c);
                                }
                                st3>>=1;
                                stage3+=2;  /* +=st3Multiplier */
                            } while((++c&0xf)!=0);
                            break;
                        case UCNV_SET_FILTER_HZ:
                            /* Only add code points that are suitable for HZ DBCS (lead byte A1..FD). */
                            do {
                                if( ((st3&1)!=0 || useFallback) &&
                                    (uint16_t)((value=*((const uint16_t *)stage3))-0xa1a1)<=(0xfdfe - 0xa1a1) &&
                                    (uint8_t)(value-0xa1)<=(0xfe - 0xa1)
                                ) {
                                    sa->add(sa->set, c);
                                }
                                st3>>=1;
                                stage3+=2;  /* +=st3Multiplier */
                            } while((++c&0xf)!=0);
                            break;
                        default:
                            *pErrorCode=U_INTERNAL_PROGRAM_ERROR;
                            return;
                        }
                    } else {
                        c+=16; /* empty stage 3 block */
                    }
                }
            } else {
                c+=1024; /* empty stage 2 block */
            }
        }
    }

    ucnv_extGetUnicodeSet(sharedData, sa, which, filter, pErrorCode);
}

U_CFUNC void
ucnv_MBCSGetUnicodeSetForUnicode(const UConverterSharedData *sharedData,
                                 const USetAdder *sa,
                                 UConverterUnicodeSet which,
                                 UErrorCode *pErrorCode) {
    ucnv_MBCSGetFilteredUnicodeSetForUnicode(
        sharedData, sa, which,
        sharedData->mbcs.outputType==MBCS_OUTPUT_DBCS_ONLY ?
            UCNV_SET_FILTER_DBCS_ONLY :
            UCNV_SET_FILTER_NONE,
        pErrorCode);
}

static void U_CALLCONV
ucnv_MBCSGetUnicodeSet(const UConverter *cnv,
                   const USetAdder *sa,
                   UConverterUnicodeSet which,
                   UErrorCode *pErrorCode) {
    if(cnv->options&_MBCS_OPTION_GB18030) {
        sa->addRange(sa->set, 0, 0xd7ff);
        sa->addRange(sa->set, 0xe000, 0x10ffff);
    } else {
        ucnv_MBCSGetUnicodeSetForUnicode(cnv->sharedData, sa, which, pErrorCode);
    }
}

/* conversion extensions for input not in the main table -------------------- */

/*
 * Hardcoded extension handling for GB 18030.
 * Definition of LINEAR macros and gb18030Ranges see near the beginning of the file.
 *
 * In the future, conversion extensions may handle m:n mappings and delta tables,
 * see https://htmlpreview.github.io/?https://github.com/unicode-org/icu-docs/blob/main/design/conversion/conversion_extensions.html
 *
 * If an input character cannot be mapped, then these functions set an error
 * code. The framework will then call the callback function.
 */

/*
 * @return if(U_FAILURE) return the code point for cnv->fromUChar32
 *         else return 0 after output has been written to the target
 */
static UChar32
_extFromU(UConverter *cnv, const UConverterSharedData *sharedData,
          UChar32 cp,
          const char16_t **source, const char16_t *sourceLimit,
          uint8_t **target, const uint8_t *targetLimit,
          int32_t **offsets, int32_t sourceIndex,
          UBool flush,
          UErrorCode *pErrorCode) {
    const int32_t *cx;

    cnv->useSubChar1=false;

    if( (cx=sharedData->mbcs.extIndexes)!=nullptr &&
        ucnv_extInitialMatchFromU(
            cnv, cx,
            cp, source, sourceLimit,
            reinterpret_cast<char**>(target), reinterpret_cast<const char*>(targetLimit),
            offsets, sourceIndex,
            flush,
            pErrorCode)
    ) {
        return 0; /* an extension mapping handled the input */
    }

    /* GB 18030 */
    if((cnv->options&_MBCS_OPTION_GB18030)!=0) {
        const uint32_t *range;
        int32_t i;

        range=gb18030Ranges[0];
        for(i=0; i<UPRV_LENGTHOF(gb18030Ranges); range+=4, ++i) {
            if (range[0] <= static_cast<uint32_t>(cp) && static_cast<uint32_t>(cp) <= range[1]) {
                /* found the Unicode code point, output the four-byte sequence for it */
                uint32_t linear;
                char bytes[4];

                /* get the linear value of the first GB 18030 code in this range */
                linear=range[2]-LINEAR_18030_BASE;

                /* add the offset from the beginning of the range */
                linear += (static_cast<uint32_t>(cp) - range[0]);

                /* turn this into a four-byte sequence */
                bytes[3] = static_cast<char>(0x30 + linear % 10); linear /= 10;
                bytes[2] = static_cast<char>(0x81 + linear % 126); linear /= 126;
                bytes[1] = static_cast<char>(0x30 + linear % 10); linear /= 10;
                bytes[0] = static_cast<char>(0x81 + linear);

                /* output this sequence */
                ucnv_fromUWriteBytes(cnv,
                                     bytes, 4, reinterpret_cast<char**>(target), reinterpret_cast<const char*>(targetLimit),
                                     offsets, sourceIndex, pErrorCode);
                return 0;
            }
        }
    }

    /* no mapping */
    *pErrorCode=U_INVALID_CHAR_FOUND;
    return cp;
}

/*
 * Input sequence: cnv->toUBytes[0..length[
 * @return if(U_FAILURE) return the length (toULength, byteIndex) for the input
 *         else return 0 after output has been written to the target
 */
static int8_t
_extToU(UConverter *cnv, const UConverterSharedData *sharedData,
        int8_t length,
        const uint8_t **source, const uint8_t *sourceLimit,
        char16_t **target, const char16_t *targetLimit,
        int32_t **offsets, int32_t sourceIndex,
        UBool flush,
        UErrorCode *pErrorCode) {
    const int32_t *cx;

    if( (cx=sharedData->mbcs.extIndexes)!=nullptr &&
        ucnv_extInitialMatchToU(
            cnv, cx,
            length, reinterpret_cast<const char**>(source), reinterpret_cast<const char*>(sourceLimit),
            target, targetLimit,
            offsets, sourceIndex,
            flush,
            pErrorCode)
    ) {
        return 0; /* an extension mapping handled the input */
    }

    /* GB 18030 */
    if(length==4 && (cnv->options&_MBCS_OPTION_GB18030)!=0) {
        const uint32_t *range;
        uint32_t linear;
        int32_t i;

        linear=LINEAR_18030(cnv->toUBytes[0], cnv->toUBytes[1], cnv->toUBytes[2], cnv->toUBytes[3]);
        range=gb18030Ranges[0];
        for(i=0; i<UPRV_LENGTHOF(gb18030Ranges); range+=4, ++i) {
            if(range[2]<=linear && linear<=range[3]) {
                /* found the sequence, output the Unicode code point for it */
                *pErrorCode=U_ZERO_ERROR;

                /* add the linear difference between the input and start sequences to the start code point */
                linear=range[0]+(linear-range[2]);

                /* output this code point */
                ucnv_toUWriteCodePoint(cnv, linear, target, targetLimit, offsets, sourceIndex, pErrorCode);

                return 0;
            }
        }
    }

    /* no mapping */
    *pErrorCode=U_INVALID_CHAR_FOUND;
    return length;
}

/* EBCDIC swap LF<->NL ------------------------------------------------------ */

/*
 * This code modifies a standard EBCDIC<->Unicode mapping table for
 * OS/390 (z/OS) Unix System Services (Open Edition).
 * The difference is in the mapping of Line Feed and New Line control codes:
 * Standard EBCDIC maps
 *
 *   <U000A> \x25 |0
 *   <U0085> \x15 |0
 *
 * but OS/390 USS EBCDIC swaps the control codes for LF and NL,
 * mapping
 *
 *   <U000A> \x15 |0
 *   <U0085> \x25 |0
 *
 * This code modifies a loaded standard EBCDIC<->Unicode mapping table
 * by copying it into allocated memory and swapping the LF and NL values.
 * It allows to support the same EBCDIC charset in both versions without
 * duplicating the entire installed table.
 */

/* standard EBCDIC codes */
#define EBCDIC_LF 0x25
#define EBCDIC_NL 0x15

/* standard EBCDIC codes with roundtrip flag as stored in Unicode-to-single-byte tables */
#define EBCDIC_RT_LF 0xf25
#define EBCDIC_RT_NL 0xf15

/* Unicode code points */
#define U_LF 0x0a
#define U_NL 0x85

static UBool
_EBCDICSwapLFNL(UConverterSharedData *sharedData, UErrorCode *pErrorCode) {
    UConverterMBCSTable *mbcsTable;

    const uint16_t *table, *results;
    const uint8_t *bytes;

    int32_t (*newStateTable)[256];
    uint16_t *newResults;
    uint8_t *p;
    char *name;

    uint32_t stage2Entry;
    uint32_t size, sizeofFromUBytes;

    mbcsTable=&sharedData->mbcs;

    table=mbcsTable->fromUnicodeTable;
    bytes=mbcsTable->fromUnicodeBytes;
    results = reinterpret_cast<const uint16_t*>(bytes);

    /*
     * Check that this is an EBCDIC table with SBCS portion -
     * SBCS or EBCDIC_STATEFUL with standard EBCDIC LF and NL mappings.
     *
     * If not, ignore the option. Options are always ignored if they do not apply.
     */
    if(!(
         (mbcsTable->outputType==MBCS_OUTPUT_1 || mbcsTable->outputType==MBCS_OUTPUT_2_SISO) &&
         mbcsTable->stateTable[0][EBCDIC_LF]==MBCS_ENTRY_FINAL(0, MBCS_STATE_VALID_DIRECT_16, U_LF) &&
         mbcsTable->stateTable[0][EBCDIC_NL]==MBCS_ENTRY_FINAL(0, MBCS_STATE_VALID_DIRECT_16, U_NL)
    )) {
        return false;
    }

    if(mbcsTable->outputType==MBCS_OUTPUT_1) {
        if(!(
             EBCDIC_RT_LF==MBCS_SINGLE_RESULT_FROM_U(table, results, U_LF) &&
             EBCDIC_RT_NL==MBCS_SINGLE_RESULT_FROM_U(table, results, U_NL)
        )) {
            return false;
        }
    } else /* MBCS_OUTPUT_2_SISO */ {
        stage2Entry=MBCS_STAGE_2_FROM_U(table, U_LF);
        if(!(
             MBCS_FROM_U_IS_ROUNDTRIP(stage2Entry, U_LF)!=0 &&
             EBCDIC_LF==MBCS_VALUE_2_FROM_STAGE_2(bytes, stage2Entry, U_LF)
        )) {
            return false;
        }

        stage2Entry=MBCS_STAGE_2_FROM_U(table, U_NL);
        if(!(
             MBCS_FROM_U_IS_ROUNDTRIP(stage2Entry, U_NL)!=0 &&
             EBCDIC_NL==MBCS_VALUE_2_FROM_STAGE_2(bytes, stage2Entry, U_NL)
        )) {
            return false;
        }
    }

    if(mbcsTable->fromUBytesLength>0) {
        /*
         * We _know_ the number of bytes in the fromUnicodeBytes array
         * starting with header.version 4.1.
         */
        sizeofFromUBytes=mbcsTable->fromUBytesLength;
    } else {
        /*
         * Otherwise:
         * There used to be code to enumerate the fromUnicode
         * trie and find the highest entry, but it was removed in ICU 3.2
         * because it was not tested and caused a low code coverage number.
         * See Jitterbug 3674.
         * This affects only some .cnv file formats with a header.version
         * below 4.1, and only when swaplfnl is requested.
         *
         * ucnvmbcs.c revision 1.99 is the last one with the
         * ucnv_MBCSSizeofFromUBytes() function.
         */
        *pErrorCode=U_INVALID_FORMAT_ERROR;
        return false;
    }

    /*
     * The table has an appropriate format.
     * Allocate and build
     * - a modified to-Unicode state table
     * - a modified from-Unicode output array
     * - a converter name string with the swap option appended
     */
    size=
        mbcsTable->countStates*1024+
        sizeofFromUBytes+
        UCNV_MAX_CONVERTER_NAME_LENGTH+20;
    p = static_cast<uint8_t*>(uprv_malloc(size));
    if(p==nullptr) {
        *pErrorCode=U_MEMORY_ALLOCATION_ERROR;
        return false;
    }

    /* copy and modify the to-Unicode state table */
    newStateTable = reinterpret_cast<int32_t(*)[256]>(p);
    uprv_memcpy(newStateTable, mbcsTable->stateTable, mbcsTable->countStates*1024);

    newStateTable[0][EBCDIC_LF]=MBCS_ENTRY_FINAL(0, MBCS_STATE_VALID_DIRECT_16, U_NL);
    newStateTable[0][EBCDIC_NL]=MBCS_ENTRY_FINAL(0, MBCS_STATE_VALID_DIRECT_16, U_LF);

    /* copy and modify the from-Unicode result table */
    newResults = reinterpret_cast<uint16_t*>(newStateTable[mbcsTable->countStates]);
    uprv_memcpy(newResults, bytes, sizeofFromUBytes);

    /* conveniently, the table access macros work on the left side of expressions */
    if(mbcsTable->outputType==MBCS_OUTPUT_1) {
        MBCS_SINGLE_RESULT_FROM_U(table, newResults, U_LF)=EBCDIC_RT_NL;
        MBCS_SINGLE_RESULT_FROM_U(table, newResults, U_NL)=EBCDIC_RT_LF;
    } else /* MBCS_OUTPUT_2_SISO */ {
        stage2Entry=MBCS_STAGE_2_FROM_U(table, U_LF);
        MBCS_VALUE_2_FROM_STAGE_2(newResults, stage2Entry, U_LF)=EBCDIC_NL;

        stage2Entry=MBCS_STAGE_2_FROM_U(table, U_NL);
        MBCS_VALUE_2_FROM_STAGE_2(newResults, stage2Entry, U_NL)=EBCDIC_LF;
    }

    /* set the canonical converter name */
    name = reinterpret_cast<char*>(newResults) + sizeofFromUBytes;
    uprv_strcpy(name, sharedData->staticData->name);
    uprv_strcat(name, UCNV_SWAP_LFNL_OPTION_STRING);

    /* set the pointers */
    icu::umtx_lock(nullptr);
    if(mbcsTable->swapLFNLStateTable==nullptr) {
        mbcsTable->swapLFNLStateTable=newStateTable;
        mbcsTable->swapLFNLFromUnicodeBytes = reinterpret_cast<uint8_t*>(newResults);
        mbcsTable->swapLFNLName=name;

        newStateTable=nullptr;
    }
    icu::umtx_unlock(nullptr);

    /* release the allocated memory if another thread beat us to it */
    if(newStateTable!=nullptr) {
        uprv_free(newStateTable);
    }
    return true;
}

/* reconstitute omitted fromUnicode data ------------------------------------ */

/* for details, compare with genmbcs.c MBCSAddFromUnicode() and transformEUC() */
static UBool U_CALLCONV
writeStage3Roundtrip(const void *context, uint32_t value, UChar32 codePoints[32]) {
    UConverterMBCSTable *mbcsTable=(UConverterMBCSTable *)context;
    const uint16_t *table;
    uint32_t *stage2;
    uint8_t *bytes, *p;
    UChar32 c;
    int32_t i, st3;

    table=mbcsTable->fromUnicodeTable;
    bytes = const_cast<uint8_t*>(mbcsTable->fromUnicodeBytes);

    /* for EUC outputTypes, modify the value like genmbcs.c's transformEUC() */
    switch(mbcsTable->outputType) {
    case MBCS_OUTPUT_3_EUC:
        if(value<=0xffff) {
            /* short sequences are stored directly */
            /* code set 0 or 1 */
        } else if(value<=0x8effff) {
            /* code set 2 */
            value&=0x7fff;
        } else /* first byte is 0x8f */ {
            /* code set 3 */
            value&=0xff7f;
        }
        break;
    case MBCS_OUTPUT_4_EUC:
        if(value<=0xffffff) {
            /* short sequences are stored directly */
            /* code set 0 or 1 */
        } else if(value<=0x8effffff) {
            /* code set 2 */
            value&=0x7fffff;
        } else /* first byte is 0x8f */ {
            /* code set 3 */
            value&=0xff7fff;
        }
        break;
    default:
        break;
    }

    for(i=0; i<=0x1f; ++value, ++i) {
        c=codePoints[i];
        if(c<0) {
            continue;
        }

        /* locate the stage 2 & 3 data */
        stage2=((uint32_t *)table)+table[c>>10]+((c>>4)&0x3f);
        p=bytes;
        st3 = static_cast<int32_t>(static_cast<uint16_t>(*stage2)) * 16 + (c & 0xf);

        /* write the codepage bytes into stage 3 */
        switch(mbcsTable->outputType) {
        case MBCS_OUTPUT_3:
        case MBCS_OUTPUT_4_EUC:
            p+=st3*3;
            p[0] = static_cast<uint8_t>(value >> 16);
            p[1] = static_cast<uint8_t>(value >> 8);
            p[2] = static_cast<uint8_t>(value);
            break;
        case MBCS_OUTPUT_4:
            reinterpret_cast<uint32_t*>(p)[st3] = value;
            break;
        default:
            /* 2 bytes per character */
            reinterpret_cast<uint16_t*>(p)[st3] = static_cast<uint16_t>(value);
            break;
        }

        /* set the roundtrip flag */
        *stage2|=(1UL<<(16+(c&0xf)));
    }
    return true;
 }

static void
reconstituteData(UConverterMBCSTable *mbcsTable,
                 uint32_t stage1Length, uint32_t stage2Length,
                 uint32_t fullStage2Length,  /* lengths are numbers of units, not bytes */
                 UErrorCode *pErrorCode) {
    uint16_t *stage1;
    uint32_t *stage2;
    uint32_t dataLength=stage1Length*2+fullStage2Length*4+mbcsTable->fromUBytesLength;
    mbcsTable->reconstitutedData = static_cast<uint8_t*>(uprv_malloc(dataLength));
    if(mbcsTable->reconstitutedData==nullptr) {
        *pErrorCode=U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    uprv_memset(mbcsTable->reconstitutedData, 0, dataLength);

    /* copy existing data and reroute the pointers */
    stage1 = reinterpret_cast<uint16_t*>(mbcsTable->reconstitutedData);
    uprv_memcpy(stage1, mbcsTable->fromUnicodeTable, stage1Length*2);

    stage2 = reinterpret_cast<uint32_t*>(stage1 + stage1Length);
    uprv_memcpy(stage2+(fullStage2Length-stage2Length),
                mbcsTable->fromUnicodeTable+stage1Length,
                stage2Length*4);

    mbcsTable->fromUnicodeTable=stage1;
    mbcsTable->fromUnicodeBytes = reinterpret_cast<uint8_t*>(stage2 + fullStage2Length);

    /* indexes into stage 2 count from the bottom of the fromUnicodeTable */
    stage2 = reinterpret_cast<uint32_t*>(stage1);

    /* reconstitute the initial part of stage 2 from the mbcsIndex */
    {
        int32_t stageUTF8Length = (static_cast<int32_t>(mbcsTable->maxFastUChar) + 1) >> 6;
        int32_t stageUTF8Index=0;
        int32_t st1, st2, st3, i;

        for(st1=0; stageUTF8Index<stageUTF8Length; ++st1) {
            st2=stage1[st1];
            if (st2 != static_cast<int32_t>(stage1Length) / 2) {
                /* each stage 2 block has 64 entries corresponding to 16 entries in the mbcsIndex */
                for(i=0; i<16; ++i) {
                    st3=mbcsTable->mbcsIndex[stageUTF8Index++];
                    if(st3!=0) {
                        /* an stage 2 entry's index is per stage 3 16-block, not per stage 3 entry */
                        st3>>=4;
                        /*
                         * 4 stage 2 entries point to 4 consecutive stage 3 16-blocks which are
                         * allocated together as a single 64-block for access from the mbcsIndex
                         */
                        stage2[st2++]=st3++;
                        stage2[st2++]=st3++;
                        stage2[st2++]=st3++;
                        stage2[st2++]=st3;
                    } else {
                        /* no stage 3 block, skip */
                        st2+=4;
                    }
                }
            } else {
                /* no stage 2 block, skip */
                stageUTF8Index+=16;
            }
        }
    }

    /* reconstitute fromUnicodeBytes with roundtrips from toUnicode data */
    ucnv_MBCSEnumToUnicode(mbcsTable, writeStage3Roundtrip, mbcsTable, pErrorCode);
}

/* MBCS setup functions ----------------------------------------------------- */

static void U_CALLCONV
ucnv_MBCSLoad(UConverterSharedData *sharedData,
          UConverterLoadArgs *pArgs,
          const uint8_t *raw,
          UErrorCode *pErrorCode) {
    UDataInfo info;
    UConverterMBCSTable *mbcsTable=&sharedData->mbcs;
    _MBCSHeader *header=(_MBCSHeader *)raw;
    uint32_t offset;
    uint32_t headerLength;
    UBool noFromU=false;

    if(header->version[0]==4) {
        headerLength=MBCS_HEADER_V4_LENGTH;
    } else if(header->version[0]==5 && header->version[1]>=3 &&
              (header->options&MBCS_OPT_UNKNOWN_INCOMPATIBLE_MASK)==0) {
        headerLength=header->options&MBCS_OPT_LENGTH_MASK;
        noFromU = static_cast<UBool>((header->options & MBCS_OPT_NO_FROM_U) != 0);
    } else {
        *pErrorCode=U_INVALID_TABLE_FORMAT;
        return;
    }

    mbcsTable->outputType = static_cast<uint8_t>(header->flags);
    if(noFromU && mbcsTable->outputType==MBCS_OUTPUT_1) {
        *pErrorCode=U_INVALID_TABLE_FORMAT;
        return;
    }

    /* extension data, header version 4.2 and higher */
    offset=header->flags>>8;
    if(offset!=0) {
        mbcsTable->extIndexes = reinterpret_cast<const int32_t*>(raw + offset);
    }

    if(mbcsTable->outputType==MBCS_OUTPUT_EXT_ONLY) {
        UConverterLoadArgs args=UCNV_LOAD_ARGS_INITIALIZER;
        UConverterSharedData *baseSharedData;
        const int32_t *extIndexes;
        const char *baseName;

        /* extension-only file, load the base table and set values appropriately */
        if((extIndexes=mbcsTable->extIndexes)==nullptr) {
            /* extension-only file without extension */
            *pErrorCode=U_INVALID_TABLE_FORMAT;
            return;
        }

        if(pArgs->nestedLoads!=1) {
            /* an extension table must not be loaded as a base table */
            *pErrorCode=U_INVALID_TABLE_FILE;
            return;
        }

        /* load the base table */
        baseName = reinterpret_cast<const char*>(header) + headerLength * 4;
        if(0==uprv_strcmp(baseName, sharedData->staticData->name)) {
            /* forbid loading this same extension-only file */
            *pErrorCode=U_INVALID_TABLE_FORMAT;
            return;
        }

        /* TODO parse package name out of the prefix of the base name in the extension .cnv file? */
        args.size=sizeof(UConverterLoadArgs);
        args.nestedLoads=2;
        args.onlyTestIsLoadable=pArgs->onlyTestIsLoadable;
        args.reserved=pArgs->reserved;
        args.options=pArgs->options;
        args.pkg=pArgs->pkg;
        args.name=baseName;
        baseSharedData=ucnv_load(&args, pErrorCode);
        if(U_FAILURE(*pErrorCode)) {
            return;
        }
        if( baseSharedData->staticData->conversionType!=UCNV_MBCS ||
            baseSharedData->mbcs.baseSharedData!=nullptr
        ) {
            ucnv_unload(baseSharedData);
            *pErrorCode=U_INVALID_TABLE_FORMAT;
            return;
        }
        if(pArgs->onlyTestIsLoadable) {
            /*
             * Exit as soon as we know that we can load the converter
             * and the format is valid and supported.
             * The worst that can happen in the following code is a memory
             * allocation error.
             */
            ucnv_unload(baseSharedData);
            return;
        }

        /* copy the base table data */
        uprv_memcpy(mbcsTable, &baseSharedData->mbcs, sizeof(UConverterMBCSTable));

        /* overwrite values with relevant ones for the extension converter */
        mbcsTable->baseSharedData=baseSharedData;
        mbcsTable->extIndexes=extIndexes;

        /*
         * It would be possible to share the swapLFNL data with a base converter,
         * but the generated name would have to be different, and the memory
         * would have to be free'd only once.
         * It is easier to just create the data for the extension converter
         * separately when it is requested.
         */
        mbcsTable->swapLFNLStateTable=nullptr;
        mbcsTable->swapLFNLFromUnicodeBytes=nullptr;
        mbcsTable->swapLFNLName=nullptr;

        /*
         * The reconstitutedData must be deleted only when the base converter
         * is unloaded.
         */
        mbcsTable->reconstitutedData=nullptr;

        /*
         * Set a special, runtime-only outputType if the extension converter
         * is a DBCS version of a base converter that also maps single bytes.
         */
        if( sharedData->staticData->conversionType==UCNV_DBCS ||
                (sharedData->staticData->conversionType==UCNV_MBCS &&
                 sharedData->staticData->minBytesPerChar>=2)
        ) {
            if(baseSharedData->mbcs.outputType==MBCS_OUTPUT_2_SISO) {
                /* the base converter is SI/SO-stateful */
                int32_t entry;

                /* get the dbcs state from the state table entry for SO=0x0e */
                entry=mbcsTable->stateTable[0][0xe];
                if( MBCS_ENTRY_IS_FINAL(entry) &&
                    MBCS_ENTRY_FINAL_ACTION(entry)==MBCS_STATE_CHANGE_ONLY &&
                    MBCS_ENTRY_FINAL_STATE(entry)!=0
                ) {
                    mbcsTable->dbcsOnlyState = static_cast<uint8_t>(MBCS_ENTRY_FINAL_STATE(entry));

                    mbcsTable->outputType=MBCS_OUTPUT_DBCS_ONLY;
                }
            } else if(
                baseSharedData->staticData->conversionType==UCNV_MBCS &&
                baseSharedData->staticData->minBytesPerChar==1 &&
                baseSharedData->staticData->maxBytesPerChar==2 &&
                mbcsTable->countStates<=127
            ) {
                /* non-stateful base converter, need to modify the state table */
                int32_t (*newStateTable)[256];
                int32_t *state;
                int32_t i, count;

                /* allocate a new state table and copy the base state table contents */
                count=mbcsTable->countStates;
                newStateTable = static_cast<int32_t(*)[256]>(uprv_malloc((count + 1) * 1024));
                if(newStateTable==nullptr) {
                    ucnv_unload(baseSharedData);
                    *pErrorCode=U_MEMORY_ALLOCATION_ERROR;
                    return;
                }

                uprv_memcpy(newStateTable, mbcsTable->stateTable, count*1024);

                /* change all final single-byte entries to go to a new all-illegal state */
                state=newStateTable[0];
                for(i=0; i<256; ++i) {
                    if(MBCS_ENTRY_IS_FINAL(state[i])) {
                        state[i]=MBCS_ENTRY_TRANSITION(count, 0);
                    }
                }

                /* build the new all-illegal state */
                state=newStateTable[count];
                for(i=0; i<256; ++i) {
                    state[i]=MBCS_ENTRY_FINAL(0, MBCS_STATE_ILLEGAL, 0);
                }
                mbcsTable->stateTable=(const int32_t (*)[256])newStateTable;
                mbcsTable->countStates = static_cast<uint8_t>(count + 1);
                mbcsTable->stateTableOwned=true;

                mbcsTable->outputType=MBCS_OUTPUT_DBCS_ONLY;
            }
        }

        /*
         * unlike below for files with base tables, do not get the unicodeMask
         * from the sharedData; instead, use the base table's unicodeMask,
         * which we copied in the memcpy above;
         * this is necessary because the static data unicodeMask, especially
         * the UCNV_HAS_SUPPLEMENTARY flag, is part of the base table data
         */
    } else {
        /* conversion file with a base table; an additional extension table is optional */
        /* make sure that the output type is known */
        switch(mbcsTable->outputType) {
        case MBCS_OUTPUT_1:
        case MBCS_OUTPUT_2:
        case MBCS_OUTPUT_3:
        case MBCS_OUTPUT_4:
        case MBCS_OUTPUT_3_EUC:
        case MBCS_OUTPUT_4_EUC:
        case MBCS_OUTPUT_2_SISO:
            /* OK */
            break;
        default:
            *pErrorCode=U_INVALID_TABLE_FORMAT;
            return;
        }
        if(pArgs->onlyTestIsLoadable) {
            /*
             * Exit as soon as we know that we can load the converter
             * and the format is valid and supported.
             * The worst that can happen in the following code is a memory
             * allocation error.
             */
            return;
        }

        mbcsTable->countStates = static_cast<uint8_t>(header->countStates);
        mbcsTable->countToUFallbacks=header->countToUFallbacks;
        mbcsTable->stateTable = reinterpret_cast<const int32_t(*)[256]>(raw + headerLength * 4);
        mbcsTable->toUFallbacks = reinterpret_cast<const _MBCSToUFallback*>(mbcsTable->stateTable + header->countStates);
        mbcsTable->unicodeCodeUnits = reinterpret_cast<const uint16_t*>(raw + header->offsetToUCodeUnits);

        mbcsTable->fromUnicodeTable = reinterpret_cast<const uint16_t*>(raw + header->offsetFromUTable);
        mbcsTable->fromUnicodeBytes = raw + header->offsetFromUBytes;
        mbcsTable->fromUBytesLength=header->fromUBytesLength;

        /*
         * converter versions 6.1 and up contain a unicodeMask that is
         * used here to select the most efficient function implementations
         */
        info.size=sizeof(UDataInfo);
        udata_getInfo((UDataMemory *)sharedData->dataMemory, &info);
        if(info.formatVersion[0]>6 || (info.formatVersion[0]==6 && info.formatVersion[1]>=1)) {
            /* mask off possible future extensions to be safe */
            mbcsTable->unicodeMask = static_cast<uint8_t>(sharedData->staticData->unicodeMask & 3);
        } else {
            /* for older versions, assume worst case: contains anything possible (prevent over-optimizations) */
            mbcsTable->unicodeMask=UCNV_HAS_SUPPLEMENTARY|UCNV_HAS_SURROGATES;
        }

        /*
         * _MBCSHeader.version 4.3 adds utf8Friendly data structures.
         * Check for the header version, SBCS vs. MBCS, and for whether the
         * data structures are optimized for code points as high as what the
         * runtime code is designed for.
         * The implementation does not handle mapping tables with entries for
         * unpaired surrogates.
         */
        if( header->version[1]>=3 &&
            (mbcsTable->unicodeMask&UCNV_HAS_SURROGATES)==0 &&
            (mbcsTable->countStates==1 ?
                (header->version[2]>=(SBCS_FAST_MAX>>8)) :
                (header->version[2]>=(MBCS_FAST_MAX>>8))
            )
        ) {
            mbcsTable->utf8Friendly=true;

            if(mbcsTable->countStates==1) {
                /*
                 * SBCS: Stage 3 is allocated in 64-entry blocks for U+0000..SBCS_FAST_MAX or higher.
                 * Build a table with indexes to each block, to be used instead of
                 * the regular stage 1/2 table.
                 */
                int32_t i;
                for(i=0; i<(SBCS_FAST_LIMIT>>6); ++i) {
                    mbcsTable->sbcsIndex[i]=mbcsTable->fromUnicodeTable[mbcsTable->fromUnicodeTable[i>>4]+((i<<2)&0x3c)];
                }
                /* set SBCS_FAST_MAX to reflect the reach of sbcsIndex[] even if header->version[2]>(SBCS_FAST_MAX>>8) */
                mbcsTable->maxFastUChar=SBCS_FAST_MAX;
            } else {
                /*
                 * MBCS: Stage 3 is allocated in 64-entry blocks for U+0000..MBCS_FAST_MAX or higher.
                 * The .cnv file is prebuilt with an additional stage table with indexes
                 * to each block.
                 */
                mbcsTable->mbcsIndex = reinterpret_cast<const uint16_t*>(
                    mbcsTable->fromUnicodeBytes +
                     (noFromU ? 0 : mbcsTable->fromUBytesLength));
                mbcsTable->maxFastUChar = (static_cast<char16_t>(header->version[2]) << 8) | 0xff;
            }
        }

        /* calculate a bit set of 4 ASCII characters per bit that round-trip to ASCII bytes */
        {
            uint32_t asciiRoundtrips=0xffffffff;
            int32_t i;

            for(i=0; i<0x80; ++i) {
                if(mbcsTable->stateTable[0][i]!=MBCS_ENTRY_FINAL(0, MBCS_STATE_VALID_DIRECT_16, i)) {
                    asciiRoundtrips &= ~(static_cast<uint32_t>(1) << (i >> 2));
                }
            }
            mbcsTable->asciiRoundtrips=asciiRoundtrips;
        }

        if(noFromU) {
            uint32_t stage1Length=
                mbcsTable->unicodeMask&UCNV_HAS_SUPPLEMENTARY ?
                    0x440 : 0x40;
            uint32_t stage2Length=
                (header->offsetFromUBytes-header->offsetFromUTable)/4-
                stage1Length/2;
            reconstituteData(mbcsTable, stage1Length, stage2Length, header->fullStage2Length, pErrorCode);
        }
    }

    /* Set the impl pointer here so that it is set for both extension-only and base tables. */
    if(mbcsTable->utf8Friendly) {
        if(mbcsTable->countStates==1) {
            sharedData->impl=&_SBCSUTF8Impl;
        } else {
            if(mbcsTable->outputType==MBCS_OUTPUT_2) {
                sharedData->impl=&_DBCSUTF8Impl;
            }
        }
    }

    if(mbcsTable->outputType==MBCS_OUTPUT_DBCS_ONLY || mbcsTable->outputType==MBCS_OUTPUT_2_SISO) {
        /*
         * MBCS_OUTPUT_DBCS_ONLY: No SBCS mappings, therefore ASCII does not roundtrip.
         * MBCS_OUTPUT_2_SISO: Bypass the ASCII fastpath to handle prevLength correctly.
         */
        mbcsTable->asciiRoundtrips=0;
    }
}

static void U_CALLCONV
ucnv_MBCSUnload(UConverterSharedData *sharedData) {
    UConverterMBCSTable *mbcsTable=&sharedData->mbcs;

    if(mbcsTable->swapLFNLStateTable!=nullptr) {
        uprv_free(mbcsTable->swapLFNLStateTable);
    }
    if(mbcsTable->stateTableOwned) {
        uprv_free((void *)mbcsTable->stateTable);
    }
    if(mbcsTable->baseSharedData!=nullptr) {
        ucnv_unload(mbcsTable->baseSharedData);
    }
    if(mbcsTable->reconstitutedData!=nullptr) {
        uprv_free(mbcsTable->reconstitutedData);
    }
}

static void U_CALLCONV
ucnv_MBCSOpen(UConverter *cnv,
              UConverterLoadArgs *pArgs,
              UErrorCode *pErrorCode) {
    UConverterMBCSTable *mbcsTable;
    const int32_t *extIndexes;
    uint8_t outputType;
    int8_t maxBytesPerUChar;

    if(pArgs->onlyTestIsLoadable) {
        return;
    }

    mbcsTable=&cnv->sharedData->mbcs;
    outputType=mbcsTable->outputType;

    if(outputType==MBCS_OUTPUT_DBCS_ONLY) {
        /* the swaplfnl option does not apply, remove it */
        cnv->options=pArgs->options&=~UCNV_OPTION_SWAP_LFNL;
    }

    if((pArgs->options&UCNV_OPTION_SWAP_LFNL)!=0) {
        /* do this because double-checked locking is broken */
        UBool isCached;

        icu::umtx_lock(nullptr);
        isCached=mbcsTable->swapLFNLStateTable!=nullptr;
        icu::umtx_unlock(nullptr);

        if(!isCached) {
            if(!_EBCDICSwapLFNL(cnv->sharedData, pErrorCode)) {
                if(U_FAILURE(*pErrorCode)) {
                    return; /* something went wrong */
                }

                /* the option does not apply, remove it */
                cnv->options=pArgs->options&=~UCNV_OPTION_SWAP_LFNL;
            }
        }
    }

    if(uprv_strstr(pArgs->name, "18030")!=nullptr) {
        if(uprv_strstr(pArgs->name, "gb18030")!=nullptr || uprv_strstr(pArgs->name, "GB18030")!=nullptr) {
            /* set a flag for GB 18030 mode, which changes the callback behavior */
            cnv->options|=_MBCS_OPTION_GB18030;
        }
    } else if((uprv_strstr(pArgs->name, "KEIS")!=nullptr) || (uprv_strstr(pArgs->name, "keis")!=nullptr)) {
        /* set a flag for KEIS converter, which changes the SI/SO character sequence */
        cnv->options|=_MBCS_OPTION_KEIS;
    } else if((uprv_strstr(pArgs->name, "JEF")!=nullptr) || (uprv_strstr(pArgs->name, "jef")!=nullptr)) {
        /* set a flag for JEF converter, which changes the SI/SO character sequence */
        cnv->options|=_MBCS_OPTION_JEF;
    } else if((uprv_strstr(pArgs->name, "JIPS")!=nullptr) || (uprv_strstr(pArgs->name, "jips")!=nullptr)) {
        /* set a flag for JIPS converter, which changes the SI/SO character sequence */
        cnv->options|=_MBCS_OPTION_JIPS;
    }

    /* fix maxBytesPerUChar depending on outputType and options etc. */
    if(outputType==MBCS_OUTPUT_2_SISO) {
        cnv->maxBytesPerUChar=3; /* SO+DBCS */
    }

    extIndexes=mbcsTable->extIndexes;
    if(extIndexes!=nullptr) {
        maxBytesPerUChar = static_cast<int8_t>(UCNV_GET_MAX_BYTES_PER_UCHAR(extIndexes));
        if(outputType==MBCS_OUTPUT_2_SISO) {
            ++maxBytesPerUChar; /* SO + multiple DBCS */
        }

        if(maxBytesPerUChar>cnv->maxBytesPerUChar) {
            cnv->maxBytesPerUChar=maxBytesPerUChar;
        }
    }

#if 0
    /*
     * documentation of UConverter fields used for status
     * all of these fields are (re)set to 0 by ucnv_bld.c and ucnv_reset()
     */

    /* toUnicode */
    cnv->toUnicodeStatus=0;     /* offset */
    cnv->mode=0;                /* state */
    cnv->toULength=0;           /* byteIndex */

    /* fromUnicode */
    cnv->fromUChar32=0;
    cnv->fromUnicodeStatus=1;   /* prevLength */
#endif
}

U_CDECL_BEGIN

static const char* U_CALLCONV
ucnv_MBCSGetName(const UConverter *cnv) {
    if((cnv->options&UCNV_OPTION_SWAP_LFNL)!=0 && cnv->sharedData->mbcs.swapLFNLName!=nullptr) {
        return cnv->sharedData->mbcs.swapLFNLName;
    } else {
        return cnv->sharedData->staticData->name;
    }
}
U_CDECL_END


/* MBCS-to-Unicode conversion functions ------------------------------------- */

static UChar32 U_CALLCONV
ucnv_MBCSGetFallback(UConverterMBCSTable *mbcsTable, uint32_t offset) {
    const _MBCSToUFallback *toUFallbacks;
    uint32_t i, start, limit;

    limit=mbcsTable->countToUFallbacks;
    if(limit>0) {
        /* do a binary search for the fallback mapping */
        toUFallbacks=mbcsTable->toUFallbacks;
        start=0;
        while(start<limit-1) {
            i=(start+limit)/2;
            if(offset<toUFallbacks[i].offset) {
                limit=i;
            } else {
                start=i;
            }
        }

        /* did we really find it? */
        if(offset==toUFallbacks[start].offset) {
            return toUFallbacks[start].codePoint;
        }
    }

    return 0xfffe;
}

/* This version of ucnv_MBCSToUnicodeWithOffsets() is optimized for single-byte, single-state codepages. */
static void
ucnv_MBCSSingleToUnicodeWithOffsets(UConverterToUnicodeArgs *pArgs,
                                UErrorCode *pErrorCode) {
    UConverter *cnv;
    const uint8_t *source, *sourceLimit;
    char16_t *target;
    const char16_t *targetLimit;
    int32_t *offsets;

    const int32_t (*stateTable)[256];

    int32_t sourceIndex;

    int32_t entry;
    char16_t c;
    uint8_t action;

    /* set up the local pointers */
    cnv=pArgs->converter;
    source = reinterpret_cast<const uint8_t*>(pArgs->source);
    sourceLimit = reinterpret_cast<const uint8_t*>(pArgs->sourceLimit);
    target=pArgs->target;
    targetLimit=pArgs->targetLimit;
    offsets=pArgs->offsets;

    if((cnv->options&UCNV_OPTION_SWAP_LFNL)!=0) {
        stateTable=(const int32_t (*)[256])cnv->sharedData->mbcs.swapLFNLStateTable;
    } else {
        stateTable=cnv->sharedData->mbcs.stateTable;
    }

    /* sourceIndex=-1 if the current character began in the previous buffer */
    sourceIndex=0;

    /* conversion loop */
    while(source<sourceLimit) {
        /*
         * This following test is to see if available input would overflow the output.
         * It does not catch output of more than one code unit that
         * overflows as a result of a surrogate pair or callback output
         * from the last source byte.
         * Therefore, those situations also test for overflows and will
         * then break the loop, too.
         */
        if(target>=targetLimit) {
            /* target is full */
            *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
            break;
        }

        entry=stateTable[0][*source++];
        /* MBCS_ENTRY_IS_FINAL(entry) */

        /* test the most common case first */
        if(MBCS_ENTRY_FINAL_IS_VALID_DIRECT_16(entry)) {
            /* output BMP code point */
            *target++ = static_cast<char16_t>(MBCS_ENTRY_FINAL_VALUE_16(entry));
            if(offsets!=nullptr) {
                *offsets++=sourceIndex;
            }

            /* normal end of action codes: prepare for a new character */
            ++sourceIndex;
            continue;
        }

        /*
         * An if-else-if chain provides more reliable performance for
         * the most common cases compared to a switch.
         */
        action = static_cast<uint8_t>(MBCS_ENTRY_FINAL_ACTION(entry));
        if(action==MBCS_STATE_VALID_DIRECT_20 ||
           (action==MBCS_STATE_FALLBACK_DIRECT_20 && UCNV_TO_U_USE_FALLBACK(cnv))
        ) {
            entry=MBCS_ENTRY_FINAL_VALUE(entry);
            /* output surrogate pair */
            *target++ = static_cast<char16_t>(0xd800 | static_cast<char16_t>(entry >> 10));
            if(offsets!=nullptr) {
                *offsets++=sourceIndex;
            }
            c = static_cast<char16_t>(0xdc00 | static_cast<char16_t>(entry & 0x3ff));
            if(target<targetLimit) {
                *target++=c;
                if(offsets!=nullptr) {
                    *offsets++=sourceIndex;
                }
            } else {
                /* target overflow */
                cnv->UCharErrorBuffer[0]=c;
                cnv->UCharErrorBufferLength=1;
                *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
                break;
            }

            ++sourceIndex;
            continue;
        } else if(action==MBCS_STATE_FALLBACK_DIRECT_16) {
            if(UCNV_TO_U_USE_FALLBACK(cnv)) {
                /* output BMP code point */
                *target++ = static_cast<char16_t>(MBCS_ENTRY_FINAL_VALUE_16(entry));
                if(offsets!=nullptr) {
                    *offsets++=sourceIndex;
                }

                ++sourceIndex;
                continue;
            }
        } else if(action==MBCS_STATE_UNASSIGNED) {
            /* just fall through */
        } else if(action==MBCS_STATE_ILLEGAL) {
            /* callback(illegal) */
            *pErrorCode=U_ILLEGAL_CHAR_FOUND;
        } else {
            /* reserved, must never occur */
            ++sourceIndex;
            continue;
        }

        if(U_FAILURE(*pErrorCode)) {
            /* callback(illegal) */
            break;
        } else /* unassigned sequences indicated with byteIndex>0 */ {
            /* try an extension mapping */
            pArgs->source = reinterpret_cast<const char*>(source);
            cnv->toUBytes[0]=*(source-1);
            cnv->toULength=_extToU(cnv, cnv->sharedData,
                                    1, &source, sourceLimit,
                                    &target, targetLimit,
                                    &offsets, sourceIndex,
                                    pArgs->flush,
                                    pErrorCode);
            sourceIndex += 1 + static_cast<int32_t>(source - reinterpret_cast<const uint8_t*>(pArgs->source));

            if(U_FAILURE(*pErrorCode)) {
                /* not mappable or buffer overflow */
                break;
            }
        }
    }

    /* write back the updated pointers */
    pArgs->source = reinterpret_cast<const char*>(source);
    pArgs->target=target;
    pArgs->offsets=offsets;
}

/*
 * This version of ucnv_MBCSSingleToUnicodeWithOffsets() is optimized for single-byte, single-state codepages
 * that only map to and from the BMP.
 * In addition to single-byte optimizations, the offset calculations
 * become much easier.
 */
static void
ucnv_MBCSSingleToBMPWithOffsets(UConverterToUnicodeArgs *pArgs,
                            UErrorCode *pErrorCode) {
    UConverter *cnv;
    const uint8_t *source, *sourceLimit, *lastSource;
    char16_t *target;
    int32_t targetCapacity, length;
    int32_t *offsets;

    const int32_t (*stateTable)[256];

    int32_t sourceIndex;

    int32_t entry;
    uint8_t action;

    /* set up the local pointers */
    cnv=pArgs->converter;
    source = reinterpret_cast<const uint8_t*>(pArgs->source);
    sourceLimit = reinterpret_cast<const uint8_t*>(pArgs->sourceLimit);
    target=pArgs->target;
    targetCapacity = static_cast<int32_t>(pArgs->targetLimit - pArgs->target);
    offsets=pArgs->offsets;

    if((cnv->options&UCNV_OPTION_SWAP_LFNL)!=0) {
        stateTable=(const int32_t (*)[256])cnv->sharedData->mbcs.swapLFNLStateTable;
    } else {
        stateTable=cnv->sharedData->mbcs.stateTable;
    }

    /* sourceIndex=-1 if the current character began in the previous buffer */
    sourceIndex=0;
    lastSource=source;

    /*
     * since the conversion here is 1:1 char16_t:uint8_t, we need only one counter
     * for the minimum of the sourceLength and targetCapacity
     */
    length = static_cast<int32_t>(sourceLimit - source);
    if(length<targetCapacity) {
        targetCapacity=length;
    }

#if MBCS_UNROLL_SINGLE_TO_BMP
    /* unrolling makes it faster on Pentium III/Windows 2000 */
    /* unroll the loop with the most common case */
unrolled:
    if(targetCapacity>=16) {
        int32_t count, loops, oredEntries;

        loops=count=targetCapacity>>4;
        do {
            oredEntries=entry=stateTable[0][*source++];
            *target++ = static_cast<char16_t>(MBCS_ENTRY_FINAL_VALUE_16(entry));
            oredEntries|=entry=stateTable[0][*source++];
            *target++ = static_cast<char16_t>(MBCS_ENTRY_FINAL_VALUE_16(entry));
            oredEntries|=entry=stateTable[0][*source++];
            *target++ = static_cast<char16_t>(MBCS_ENTRY_FINAL_VALUE_16(entry));
            oredEntries|=entry=stateTable[0][*source++];
            *target++ = static_cast<char16_t>(MBCS_ENTRY_FINAL_VALUE_16(entry));
            oredEntries|=entry=stateTable[0][*source++];
            *target++ = static_cast<char16_t>(MBCS_ENTRY_FINAL_VALUE_16(entry));
            oredEntries|=entry=stateTable[0][*source++];
            *target++ = static_cast<char16_t>(MBCS_ENTRY_FINAL_VALUE_16(entry));
            oredEntries|=entry=stateTable[0][*source++];
            *target++ = static_cast<char16_t>(MBCS_ENTRY_FINAL_VALUE_16(entry));
            oredEntries|=entry=stateTable[0][*source++];
            *target++ = static_cast<char16_t>(MBCS_ENTRY_FINAL_VALUE_16(entry));
            oredEntries|=entry=stateTable[0][*source++];
            *target++ = static_cast<char16_t>(MBCS_ENTRY_FINAL_VALUE_16(entry));
            oredEntries|=entry=stateTable[0][*source++];
            *target++ = static_cast<char16_t>(MBCS_ENTRY_FINAL_VALUE_16(entry));
            oredEntries|=entry=stateTable[0][*source++];
            *target++ = static_cast<char16_t>(MBCS_ENTRY_FINAL_VALUE_16(entry));
            oredEntries|=entry=stateTable[0][*source++];
            *target++ = static_cast<char16_t>(MBCS_ENTRY_FINAL_VALUE_16(entry));
            oredEntries|=entry=stateTable[0][*source++];
            *target++ = static_cast<char16_t>(MBCS_ENTRY_FINAL_VALUE_16(entry));
            oredEntries|=entry=stateTable[0][*source++];
            *target++ = static_cast<char16_t>(MBCS_ENTRY_FINAL_VALUE_16(entry));
            oredEntries|=entry=stateTable[0][*source++];
            *target++ = static_cast<char16_t>(MBCS_ENTRY_FINAL_VALUE_16(entry));
            oredEntries|=entry=stateTable[0][*source++];
            *target++ = static_cast<char16_t>(MBCS_ENTRY_FINAL_VALUE_16(entry));

            /* were all 16 entries really valid? */
            if(!MBCS_ENTRY_FINAL_IS_VALID_DIRECT_16(oredEntries)) {
                /* no, return to the first of these 16 */
                source-=16;
                target-=16;
                break;
            }
        } while(--count>0);
        count=loops-count;
        targetCapacity-=16*count;

        if(offsets!=nullptr) {
            lastSource+=16*count;
            while(count>0) {
                *offsets++=sourceIndex++;
                *offsets++=sourceIndex++;
                *offsets++=sourceIndex++;
                *offsets++=sourceIndex++;
                *offsets++=sourceIndex++;
                *offsets++=sourceIndex++;
                *offsets++=sourceIndex++;
                *offsets++=sourceIndex++;
                *offsets++=sourceIndex++;
                *offsets++=sourceIndex++;
                *offsets++=sourceIndex++;
                *offsets++=sourceIndex++;
                *offsets++=sourceIndex++;
                *offsets++=sourceIndex++;
                *offsets++=sourceIndex++;
                *offsets++=sourceIndex++;
                --count;
            }
        }
    }
#endif

    /* conversion loop */
    while(targetCapacity > 0 && source < sourceLimit) {
        entry=stateTable[0][*source++];
        /* MBCS_ENTRY_IS_FINAL(entry) */

        /* test the most common case first */
        if(MBCS_ENTRY_FINAL_IS_VALID_DIRECT_16(entry)) {
            /* output BMP code point */
            *target++ = static_cast<char16_t>(MBCS_ENTRY_FINAL_VALUE_16(entry));
            --targetCapacity;
            continue;
        }

        /*
         * An if-else-if chain provides more reliable performance for
         * the most common cases compared to a switch.
         */
        action = static_cast<uint8_t>(MBCS_ENTRY_FINAL_ACTION(entry));
        if(action==MBCS_STATE_FALLBACK_DIRECT_16) {
            if(UCNV_TO_U_USE_FALLBACK(cnv)) {
                /* output BMP code point */
                *target++ = static_cast<char16_t>(MBCS_ENTRY_FINAL_VALUE_16(entry));
                --targetCapacity;
                continue;
            }
        } else if(action==MBCS_STATE_UNASSIGNED) {
            /* just fall through */
        } else if(action==MBCS_STATE_ILLEGAL) {
            /* callback(illegal) */
            *pErrorCode=U_ILLEGAL_CHAR_FOUND;
        } else {
            /* reserved, must never occur */
            continue;
        }

        /* set offsets since the start or the last extension */
        if(offsets!=nullptr) {
            int32_t count = static_cast<int32_t>(source - lastSource);

            /* predecrement: do not set the offset for the callback-causing character */
            while(--count>0) {
                *offsets++=sourceIndex++;
            }
            /* offset and sourceIndex are now set for the current character */
        }

        if(U_FAILURE(*pErrorCode)) {
            /* callback(illegal) */
            break;
        } else /* unassigned sequences indicated with byteIndex>0 */ {
            /* try an extension mapping */
            lastSource=source;
            cnv->toUBytes[0]=*(source-1);
            cnv->toULength=_extToU(cnv, cnv->sharedData,
                                    1, &source, sourceLimit,
                                    &target, pArgs->targetLimit,
                                    &offsets, sourceIndex,
                                    pArgs->flush,
                                    pErrorCode);
            sourceIndex += 1 + static_cast<int32_t>(source - lastSource);

            if(U_FAILURE(*pErrorCode)) {
                /* not mappable or buffer overflow */
                break;
            }

            /* recalculate the targetCapacity after an extension mapping */
            targetCapacity = static_cast<int32_t>(pArgs->targetLimit - target);
            length = static_cast<int32_t>(sourceLimit - source);
            if(length<targetCapacity) {
                targetCapacity=length;
            }
        }

#if MBCS_UNROLL_SINGLE_TO_BMP
        /* unrolling makes it faster on Pentium III/Windows 2000 */
        goto unrolled;
#endif
    }

    if(U_SUCCESS(*pErrorCode) && source<sourceLimit && target>=pArgs->targetLimit) {
        /* target is full */
        *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
    }

    /* set offsets since the start or the last callback */
    if(offsets!=nullptr) {
        size_t count=source-lastSource;
        while(count>0) {
            *offsets++=sourceIndex++;
            --count;
        }
    }

    /* write back the updated pointers */
    pArgs->source = reinterpret_cast<const char*>(source);
    pArgs->target=target;
    pArgs->offsets=offsets;
}

static UBool
hasValidTrailBytes(const int32_t (*stateTable)[256], uint8_t state) {
    const int32_t *row=stateTable[state];
    int32_t b, entry;
    /* First test for final entries in this state for some commonly valid byte values. */
    entry=row[0xa1];
    if( !MBCS_ENTRY_IS_TRANSITION(entry) &&
        MBCS_ENTRY_FINAL_ACTION(entry)!=MBCS_STATE_ILLEGAL
    ) {
        return true;
    }
    entry=row[0x41];
    if( !MBCS_ENTRY_IS_TRANSITION(entry) &&
        MBCS_ENTRY_FINAL_ACTION(entry)!=MBCS_STATE_ILLEGAL
    ) {
        return true;
    }
    /* Then test for final entries in this state. */
    for(b=0; b<=0xff; ++b) {
        entry=row[b];
        if( !MBCS_ENTRY_IS_TRANSITION(entry) &&
            MBCS_ENTRY_FINAL_ACTION(entry)!=MBCS_STATE_ILLEGAL
        ) {
            return true;
        }
    }
    /* Then recurse for transition entries. */
    for(b=0; b<=0xff; ++b) {
        entry=row[b];
        if( MBCS_ENTRY_IS_TRANSITION(entry) &&
            hasValidTrailBytes(stateTable, static_cast<uint8_t>(MBCS_ENTRY_TRANSITION_STATE(entry)))
        ) {
            return true;
        }
    }
    return false;
}

/*
 * Is byte b a single/lead byte in this state?
 * Recurse for transition states, because here we don't want to say that
 * b is a lead byte if all byte sequences that start with b are illegal.
 */
static UBool
isSingleOrLead(const int32_t (*stateTable)[256], uint8_t state, UBool isDBCSOnly, uint8_t b) {
    const int32_t *row=stateTable[state];
    int32_t entry=row[b];
    if(MBCS_ENTRY_IS_TRANSITION(entry)) {   /* lead byte */
        return hasValidTrailBytes(stateTable, static_cast<uint8_t>(MBCS_ENTRY_TRANSITION_STATE(entry)));
    } else {
        uint8_t action = static_cast<uint8_t>(MBCS_ENTRY_FINAL_ACTION(entry));
        if(action==MBCS_STATE_CHANGE_ONLY && isDBCSOnly) {
            return false;   /* SI/SO are illegal for DBCS-only conversion */
        } else {
            return action!=MBCS_STATE_ILLEGAL;
        }
    }
}

U_CFUNC void
ucnv_MBCSToUnicodeWithOffsets(UConverterToUnicodeArgs *pArgs,
                          UErrorCode *pErrorCode) {
    UConverter *cnv;
    const uint8_t *source, *sourceLimit;
    char16_t *target;
    const char16_t *targetLimit;
    int32_t *offsets;

    const int32_t (*stateTable)[256];
    const uint16_t *unicodeCodeUnits;

    uint32_t offset;
    uint8_t state;
    int8_t byteIndex;
    uint8_t *bytes;

    int32_t sourceIndex, nextSourceIndex;

    int32_t entry;
    char16_t c;
    uint8_t action;

    /* use optimized function if possible */
    cnv=pArgs->converter;

    if(cnv->preToULength>0) {
        /*
         * pass sourceIndex=-1 because we continue from an earlier buffer
         * in the future, this may change with continuous offsets
         */
        ucnv_extContinueMatchToU(cnv, pArgs, -1, pErrorCode);

        if(U_FAILURE(*pErrorCode) || cnv->preToULength<0) {
            return;
        }
    }

    if(cnv->sharedData->mbcs.countStates==1) {
        if(!(cnv->sharedData->mbcs.unicodeMask&UCNV_HAS_SUPPLEMENTARY)) {
            ucnv_MBCSSingleToBMPWithOffsets(pArgs, pErrorCode);
        } else {
            ucnv_MBCSSingleToUnicodeWithOffsets(pArgs, pErrorCode);
        }
        return;
    }

    /* set up the local pointers */
    source=(const uint8_t *)pArgs->source;
    sourceLimit=(const uint8_t *)pArgs->sourceLimit;
    target=pArgs->target;
    targetLimit=pArgs->targetLimit;
    offsets=pArgs->offsets;

    if((cnv->options&UCNV_OPTION_SWAP_LFNL)!=0) {
        stateTable=(const int32_t (*)[256])cnv->sharedData->mbcs.swapLFNLStateTable;
    } else {
        stateTable=cnv->sharedData->mbcs.stateTable;
    }
    unicodeCodeUnits=cnv->sharedData->mbcs.unicodeCodeUnits;

    /* get the converter state from UConverter */
    offset=cnv->toUnicodeStatus;
    byteIndex=cnv->toULength;
    bytes=cnv->toUBytes;

    /*
     * if we are in the SBCS state for a DBCS-only converter,
     * then load the DBCS state from the MBCS data
     * (dbcsOnlyState==0 if it is not a DBCS-only converter)
     */
    if((state=(uint8_t)(cnv->mode))==0) {
        state=cnv->sharedData->mbcs.dbcsOnlyState;
    }

    /* sourceIndex=-1 if the current character began in the previous buffer */
    sourceIndex=byteIndex==0 ? 0 : -1;
    nextSourceIndex=0;

    /* conversion loop */
    while(source<sourceLimit) {
        /*
         * This following test is to see if available input would overflow the output.
         * It does not catch output of more than one code unit that
         * overflows as a result of a surrogate pair or callback output
         * from the last source byte.
         * Therefore, those situations also test for overflows and will
         * then break the loop, too.
         */
        if(target>=targetLimit) {
            /* target is full */
            *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
            break;
        }

        if(byteIndex==0) {
            /* optimized loop for 1/2-byte input and BMP output */
            if(offsets==nullptr) {
                do {
                    entry=stateTable[state][*source];
                    if(MBCS_ENTRY_IS_TRANSITION(entry)) {
                        state=(uint8_t)MBCS_ENTRY_TRANSITION_STATE(entry);
                        offset=MBCS_ENTRY_TRANSITION_OFFSET(entry);

                        ++source;
                        if( source<sourceLimit &&
                            MBCS_ENTRY_IS_FINAL(entry=stateTable[state][*source]) &&
                            MBCS_ENTRY_FINAL_ACTION(entry)==MBCS_STATE_VALID_16 &&
                            (c=unicodeCodeUnits[offset+MBCS_ENTRY_FINAL_VALUE_16(entry)])<0xfffe
                        ) {
                            ++source;
                            *target++=c;
                            state=(uint8_t)MBCS_ENTRY_FINAL_STATE(entry); /* typically 0 */
                            offset=0;
                        } else {
                            /* set the state and leave the optimized loop */
                            bytes[0]=*(source-1);
                            byteIndex=1;
                            break;
                        }
                    } else {
                        if(MBCS_ENTRY_FINAL_IS_VALID_DIRECT_16(entry)) {
                            /* output BMP code point */
                            ++source;
                            *target++=(char16_t)MBCS_ENTRY_FINAL_VALUE_16(entry);
                            state=(uint8_t)MBCS_ENTRY_FINAL_STATE(entry); /* typically 0 */
                        } else {
                            /* leave the optimized loop */
                            break;
                        }
                    }
                } while(source<sourceLimit && target<targetLimit);
            } else /* offsets!=nullptr */ {
                do {
                    entry=stateTable[state][*source];
                    if(MBCS_ENTRY_IS_TRANSITION(entry)) {
                        state=(uint8_t)MBCS_ENTRY_TRANSITION_STATE(entry);
                        offset=MBCS_ENTRY_TRANSITION_OFFSET(entry);

                        ++source;
                        if( source<sourceLimit &&
                            MBCS_ENTRY_IS_FINAL(entry=stateTable[state][*source]) &&
                            MBCS_ENTRY_FINAL_ACTION(entry)==MBCS_STATE_VALID_16 &&
                            (c=unicodeCodeUnits[offset+MBCS_ENTRY_FINAL_VALUE_16(entry)])<0xfffe
                        ) {
                            ++source;
                            *target++=c;
                            if(offsets!=nullptr) {
                                *offsets++=sourceIndex;
                                sourceIndex=(nextSourceIndex+=2);
                            }
                            state=(uint8_t)MBCS_ENTRY_FINAL_STATE(entry); /* typically 0 */
                            offset=0;
                        } else {
                            /* set the state and leave the optimized loop */
                            ++nextSourceIndex;
                            bytes[0]=*(source-1);
                            byteIndex=1;
                            break;
                        }
                    } else {
                        if(MBCS_ENTRY_FINAL_IS_VALID_DIRECT_16(entry)) {
                            /* output BMP code point */
                            ++source;
                            *target++=(char16_t)MBCS_ENTRY_FINAL_VALUE_16(entry);
                            if(offsets!=nullptr) {
                                *offsets++=sourceIndex;
                                sourceIndex=++nextSourceIndex;
                            }
                            state=(uint8_t)MBCS_ENTRY_FINAL_STATE(entry); /* typically 0 */
                        } else {
                            /* leave the optimized loop */
                            break;
                        }
                    }
                } while(source<sourceLimit && target<targetLimit);
            }

            /*
             * these tests and break statements could be put inside the loop
             * if C had "break outerLoop" like Java
             */
            if(source>=sourceLimit) {
                break;
            }
            if(target>=targetLimit) {
                /* target is full */
                *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
                break;
            }

            ++nextSourceIndex;
            bytes[byteIndex++]=*source++;
        } else /* byteIndex>0 */ {
            ++nextSourceIndex;
            entry=stateTable[state][bytes[byteIndex++]=*source++];
        }

        if(MBCS_ENTRY_IS_TRANSITION(entry)) {
            state=(uint8_t)MBCS_ENTRY_TRANSITION_STATE(entry);
            offset+=MBCS_ENTRY_TRANSITION_OFFSET(entry);
            continue;
        }

        /* save the previous state for proper extension mapping with SI/SO-stateful converters */
        cnv->mode=state;

        /* set the next state early so that we can reuse the entry variable */
        state=(uint8_t)MBCS_ENTRY_FINAL_STATE(entry); /* typically 0 */

        /*
         * An if-else-if chain provides more reliable performance for
         * the most common cases compared to a switch.
         */
        action=(uint8_t)(MBCS_ENTRY_FINAL_ACTION(entry));
        if(action==MBCS_STATE_VALID_16) {
            offset+=MBCS_ENTRY_FINAL_VALUE_16(entry);
            c=unicodeCodeUnits[offset];
            if(c<0xfffe) {
                /* output BMP code point */
                *target++=c;
                if(offsets!=nullptr) {
                    *offsets++=sourceIndex;
                }
                byteIndex=0;
            } else if(c==0xfffe) {
                if(UCNV_TO_U_USE_FALLBACK(cnv) && (entry=(int32_t)ucnv_MBCSGetFallback(&cnv->sharedData->mbcs, offset))!=0xfffe) {
                    /* output fallback BMP code point */
                    *target++=(char16_t)entry;
                    if(offsets!=nullptr) {
                        *offsets++=sourceIndex;
                    }
                    byteIndex=0;
                }
            } else {
                /* callback(illegal) */
                *pErrorCode=U_ILLEGAL_CHAR_FOUND;
            }
        } else if(action==MBCS_STATE_VALID_DIRECT_16) {
            /* output BMP code point */
            *target++=(char16_t)MBCS_ENTRY_FINAL_VALUE_16(entry);
            if(offsets!=nullptr) {
                *offsets++=sourceIndex;
            }
            byteIndex=0;
        } else if(action==MBCS_STATE_VALID_16_PAIR) {
            offset+=MBCS_ENTRY_FINAL_VALUE_16(entry);
            c=unicodeCodeUnits[offset++];
            if(c<0xd800) {
                /* output BMP code point below 0xd800 */
                *target++=c;
                if(offsets!=nullptr) {
                    *offsets++=sourceIndex;
                }
                byteIndex=0;
            } else if(UCNV_TO_U_USE_FALLBACK(cnv) ? c<=0xdfff : c<=0xdbff) {
                /* output roundtrip or fallback surrogate pair */
                *target++=(char16_t)(c&0xdbff);
                if(offsets!=nullptr) {
                    *offsets++=sourceIndex;
                }
                byteIndex=0;
                if(target<targetLimit) {
                    *target++=unicodeCodeUnits[offset];
                    if(offsets!=nullptr) {
                        *offsets++=sourceIndex;
                    }
                } else {
                    /* target overflow */
                    cnv->UCharErrorBuffer[0]=unicodeCodeUnits[offset];
                    cnv->UCharErrorBufferLength=1;
                    *pErrorCode=U_BUFFER_OVERFLOW_ERROR;

                    offset=0;
                    break;
                }
            } else if(UCNV_TO_U_USE_FALLBACK(cnv) ? (c&0xfffe)==0xe000 : c==0xe000) {
                /* output roundtrip BMP code point above 0xd800 or fallback BMP code point */
                *target++=unicodeCodeUnits[offset];
                if(offsets!=nullptr) {
                    *offsets++=sourceIndex;
                }
                byteIndex=0;
            } else if(c==0xffff) {
                /* callback(illegal) */
                *pErrorCode=U_ILLEGAL_CHAR_FOUND;
            }
        } else if(action==MBCS_STATE_VALID_DIRECT_20 ||
                  (action==MBCS_STATE_FALLBACK_DIRECT_20 && UCNV_TO_U_USE_FALLBACK(cnv))
        ) {
            entry=MBCS_ENTRY_FINAL_VALUE(entry);
            /* output surrogate pair */
            *target++=(char16_t)(0xd800|(char16_t)(entry>>10));
            if(offsets!=nullptr) {
                *offsets++=sourceIndex;
            }
            byteIndex=0;
            c=(char16_t)(0xdc00|(char16_t)(entry&0x3ff));
            if(target<targetLimit) {
                *target++=c;
                if(offsets!=nullptr) {
                    *offsets++=sourceIndex;
                }
            } else {
                /* target overflow */
                cnv->UCharErrorBuffer[0]=c;
                cnv->UCharErrorBufferLength=1;
                *pErrorCode=U_BUFFER_OVERFLOW_ERROR;

                offset=0;
                break;
            }
        } else if(action==MBCS_STATE_CHANGE_ONLY) {
            /*
             * This serves as a state change without any output.
             * It is useful for reading simple stateful encodings,
             * for example using just Shift-In/Shift-Out codes.
             * The 21 unused bits may later be used for more sophisticated
             * state transitions.
             */
            if(cnv->sharedData->mbcs.dbcsOnlyState==0) {
                byteIndex=0;
            } else {
                /* SI/SO are illegal for DBCS-only conversion */
                state=(uint8_t)(cnv->mode); /* restore the previous state */

                /* callback(illegal) */
                *pErrorCode=U_ILLEGAL_CHAR_FOUND;
            }
        } else if(action==MBCS_STATE_FALLBACK_DIRECT_16) {
            if(UCNV_TO_U_USE_FALLBACK(cnv)) {
                /* output BMP code point */
                *target++=(char16_t)MBCS_ENTRY_FINAL_VALUE_16(entry);
                if(offsets!=nullptr) {
                    *offsets++=sourceIndex;
                }
                byteIndex=0;
            }
        } else if(action==MBCS_STATE_UNASSIGNED) {
            /* just fall through */
        } else if(action==MBCS_STATE_ILLEGAL) {
            /* callback(illegal) */
            *pErrorCode=U_ILLEGAL_CHAR_FOUND;
        } else {
            /* reserved, must never occur */
            byteIndex=0;
        }

        /* end of action codes: prepare for a new character */
        offset=0;

        if(byteIndex==0) {
            sourceIndex=nextSourceIndex;
        } else if(U_FAILURE(*pErrorCode)) {
            /* callback(illegal) */
            if(byteIndex>1) {
                /*
                 * Ticket 5691: consistent illegal sequences:
                 * - We include at least the first byte in the illegal sequence.
                 * - If any of the non-initial bytes could be the start of a character,
                 *   we stop the illegal sequence before the first one of those.
                 */
                UBool isDBCSOnly = cnv->sharedData->mbcs.dbcsOnlyState != 0;
                int8_t i;
                for(i=1;
                    i<byteIndex && !isSingleOrLead(stateTable, state, isDBCSOnly, bytes[i]);
                    ++i) {}
                if(i<byteIndex) {
                    /* Back out some bytes. */
                    int8_t backOutDistance=byteIndex-i;
                    int32_t bytesFromThisBuffer=(int32_t)(source-(const uint8_t *)pArgs->source);
                    byteIndex=i;  /* length of reported illegal byte sequence */
                    if(backOutDistance<=bytesFromThisBuffer) {
                        source-=backOutDistance;
                    } else {
                        /* Back out bytes from the previous buffer: Need to replay them. */
                        cnv->preToULength=(int8_t)(bytesFromThisBuffer-backOutDistance);
                        /* preToULength is negative! */
                        uprv_memcpy(cnv->preToU, bytes+i, -cnv->preToULength);
                        source=(const uint8_t *)pArgs->source;
                    }
                }
            }
            break;
        } else /* unassigned sequences indicated with byteIndex>0 */ {
            /* try an extension mapping */
            pArgs->source=(const char *)source;
            byteIndex=_extToU(cnv, cnv->sharedData,
                              byteIndex, &source, sourceLimit,
                              &target, targetLimit,
                              &offsets, sourceIndex,
                              pArgs->flush,
                              pErrorCode);
            sourceIndex=nextSourceIndex+=(int32_t)(source-(const uint8_t *)pArgs->source);

            if(U_FAILURE(*pErrorCode)) {
                /* not mappable or buffer overflow */
                break;
            }
        }
    }

    /* set the converter state back into UConverter */
    cnv->toUnicodeStatus=offset;
    cnv->mode=state;
    cnv->toULength=byteIndex;

    /* write back the updated pointers */
    pArgs->source=(const char *)source;
    pArgs->target=target;
    pArgs->offsets=offsets;
}

/*
 * This version of ucnv_MBCSGetNextUChar() is optimized for single-byte, single-state codepages.
 * We still need a conversion loop in case we find reserved action codes, which are to be ignored.
 */
static UChar32
ucnv_MBCSSingleGetNextUChar(UConverterToUnicodeArgs *pArgs,
                        UErrorCode *pErrorCode) {
    UConverter *cnv;
    const int32_t (*stateTable)[256];
    const uint8_t *source, *sourceLimit;

    int32_t entry;
    uint8_t action;

    /* set up the local pointers */
    cnv=pArgs->converter;
    source = reinterpret_cast<const uint8_t*>(pArgs->source);
    sourceLimit = reinterpret_cast<const uint8_t*>(pArgs->sourceLimit);
    if((cnv->options&UCNV_OPTION_SWAP_LFNL)!=0) {
        stateTable=(const int32_t (*)[256])cnv->sharedData->mbcs.swapLFNLStateTable;
    } else {
        stateTable=cnv->sharedData->mbcs.stateTable;
    }

    /* conversion loop */
    while(source<sourceLimit) {
        entry=stateTable[0][*source++];
        /* MBCS_ENTRY_IS_FINAL(entry) */

        /* write back the updated pointer early so that we can return directly */
        pArgs->source = reinterpret_cast<const char*>(source);

        if(MBCS_ENTRY_FINAL_IS_VALID_DIRECT_16(entry)) {
            /* output BMP code point */
            return static_cast<char16_t>(MBCS_ENTRY_FINAL_VALUE_16(entry));
        }

        /*
         * An if-else-if chain provides more reliable performance for
         * the most common cases compared to a switch.
         */
        action = static_cast<uint8_t>(MBCS_ENTRY_FINAL_ACTION(entry));
        if( action==MBCS_STATE_VALID_DIRECT_20 ||
            (action==MBCS_STATE_FALLBACK_DIRECT_20 && UCNV_TO_U_USE_FALLBACK(cnv))
        ) {
            /* output supplementary code point */
            return static_cast<UChar32>(MBCS_ENTRY_FINAL_VALUE(entry) + 0x10000);
        } else if(action==MBCS_STATE_FALLBACK_DIRECT_16) {
            if(UCNV_TO_U_USE_FALLBACK(cnv)) {
                /* output BMP code point */
                return static_cast<char16_t>(MBCS_ENTRY_FINAL_VALUE_16(entry));
            }
        } else if(action==MBCS_STATE_UNASSIGNED) {
            /* just fall through */
        } else if(action==MBCS_STATE_ILLEGAL) {
            /* callback(illegal) */
            *pErrorCode=U_ILLEGAL_CHAR_FOUND;
        } else {
            /* reserved, must never occur */
            continue;
        }

        if(U_FAILURE(*pErrorCode)) {
            /* callback(illegal) */
            break;
        } else /* unassigned sequence */ {
            /* defer to the generic implementation */
            pArgs->source = reinterpret_cast<const char*>(source) - 1;
            return UCNV_GET_NEXT_UCHAR_USE_TO_U;
        }
    }

    /* no output because of empty input or only state changes */
    *pErrorCode=U_INDEX_OUTOFBOUNDS_ERROR;
    return 0xffff;
}

/*
 * Version of _MBCSToUnicodeWithOffsets() optimized for single-character
 * conversion without offset handling.
 *
 * When a character does not have a mapping to Unicode, then we return to the
 * generic ucnv_getNextUChar() code for extension/GB 18030 and error/callback
 * handling.
 * We also defer to the generic code in other complicated cases and have them
 * ultimately handled by _MBCSToUnicodeWithOffsets() itself.
 *
 * All normal mappings and errors are handled here.
 */
static UChar32 U_CALLCONV
ucnv_MBCSGetNextUChar(UConverterToUnicodeArgs *pArgs,
                  UErrorCode *pErrorCode) {
    UConverter *cnv;
    const uint8_t *source, *sourceLimit, *lastSource;

    const int32_t (*stateTable)[256];
    const uint16_t *unicodeCodeUnits;

    uint32_t offset;
    uint8_t state;

    int32_t entry;
    UChar32 c;
    uint8_t action;

    /* use optimized function if possible */
    cnv=pArgs->converter;

    if(cnv->preToULength>0) {
        /* use the generic code in ucnv_getNextUChar() to continue with a partial match */
        return UCNV_GET_NEXT_UCHAR_USE_TO_U;
    }

    if(cnv->sharedData->mbcs.unicodeMask&UCNV_HAS_SURROGATES) {
        /*
         * Using the generic ucnv_getNextUChar() code lets us deal correctly
         * with the rare case of a codepage that maps single surrogates
         * without adding the complexity to this already complicated function here.
         */
        return UCNV_GET_NEXT_UCHAR_USE_TO_U;
    } else if(cnv->sharedData->mbcs.countStates==1) {
        return ucnv_MBCSSingleGetNextUChar(pArgs, pErrorCode);
    }

    /* set up the local pointers */
    source = lastSource = reinterpret_cast<const uint8_t*>(pArgs->source);
    sourceLimit = reinterpret_cast<const uint8_t*>(pArgs->sourceLimit);

    if((cnv->options&UCNV_OPTION_SWAP_LFNL)!=0) {
        stateTable=(const int32_t (*)[256])cnv->sharedData->mbcs.swapLFNLStateTable;
    } else {
        stateTable=cnv->sharedData->mbcs.stateTable;
    }
    unicodeCodeUnits=cnv->sharedData->mbcs.unicodeCodeUnits;

    /* get the converter state from UConverter */
    offset=cnv->toUnicodeStatus;

    /*
     * if we are in the SBCS state for a DBCS-only converter,
     * then load the DBCS state from the MBCS data
     * (dbcsOnlyState==0 if it is not a DBCS-only converter)
     */
    if ((state = static_cast<uint8_t>(cnv->mode)) == 0) {
        state=cnv->sharedData->mbcs.dbcsOnlyState;
    }

    /* conversion loop */
    c=U_SENTINEL;
    while(source<sourceLimit) {
        entry=stateTable[state][*source++];
        if(MBCS_ENTRY_IS_TRANSITION(entry)) {
            state = static_cast<uint8_t>(MBCS_ENTRY_TRANSITION_STATE(entry));
            offset+=MBCS_ENTRY_TRANSITION_OFFSET(entry);

            /* optimization for 1/2-byte input and BMP output */
            if( source<sourceLimit &&
                MBCS_ENTRY_IS_FINAL(entry=stateTable[state][*source]) &&
                MBCS_ENTRY_FINAL_ACTION(entry)==MBCS_STATE_VALID_16 &&
                (c=unicodeCodeUnits[offset+MBCS_ENTRY_FINAL_VALUE_16(entry)])<0xfffe
            ) {
                ++source;
                state = static_cast<uint8_t>(MBCS_ENTRY_FINAL_STATE(entry)); /* typically 0 */
                /* output BMP code point */
                break;
            }
        } else {
            /* save the previous state for proper extension mapping with SI/SO-stateful converters */
            cnv->mode=state;

            /* set the next state early so that we can reuse the entry variable */
            state = static_cast<uint8_t>(MBCS_ENTRY_FINAL_STATE(entry)); /* typically 0 */

            /*
             * An if-else-if chain provides more reliable performance for
             * the most common cases compared to a switch.
             */
            action = static_cast<uint8_t>(MBCS_ENTRY_FINAL_ACTION(entry));
            if(action==MBCS_STATE_VALID_DIRECT_16) {
                /* output BMP code point */
                c = static_cast<char16_t>(MBCS_ENTRY_FINAL_VALUE_16(entry));
                break;
            } else if(action==MBCS_STATE_VALID_16) {
                offset+=MBCS_ENTRY_FINAL_VALUE_16(entry);
                c=unicodeCodeUnits[offset];
                if(c<0xfffe) {
                    /* output BMP code point */
                    break;
                } else if(c==0xfffe) {
                    if(UCNV_TO_U_USE_FALLBACK(cnv) && (c=ucnv_MBCSGetFallback(&cnv->sharedData->mbcs, offset))!=0xfffe) {
                        break;
                    }
                } else {
                    /* callback(illegal) */
                    *pErrorCode=U_ILLEGAL_CHAR_FOUND;
                }
            } else if(action==MBCS_STATE_VALID_16_PAIR) {
                offset+=MBCS_ENTRY_FINAL_VALUE_16(entry);
                c=unicodeCodeUnits[offset++];
                if(c<0xd800) {
                    /* output BMP code point below 0xd800 */
                    break;
                } else if(UCNV_TO_U_USE_FALLBACK(cnv) ? c<=0xdfff : c<=0xdbff) {
                    /* output roundtrip or fallback supplementary code point */
                    c=((c&0x3ff)<<10)+unicodeCodeUnits[offset]+(0x10000-0xdc00);
                    break;
                } else if(UCNV_TO_U_USE_FALLBACK(cnv) ? (c&0xfffe)==0xe000 : c==0xe000) {
                    /* output roundtrip BMP code point above 0xd800 or fallback BMP code point */
                    c=unicodeCodeUnits[offset];
                    break;
                } else if(c==0xffff) {
                    /* callback(illegal) */
                    *pErrorCode=U_ILLEGAL_CHAR_FOUND;
                }
            } else if(action==MBCS_STATE_VALID_DIRECT_20 ||
                      (action==MBCS_STATE_FALLBACK_DIRECT_20 && UCNV_TO_U_USE_FALLBACK(cnv))
            ) {
                /* output supplementary code point */
                c = static_cast<UChar32>(MBCS_ENTRY_FINAL_VALUE(entry) + 0x10000);
                break;
            } else if(action==MBCS_STATE_CHANGE_ONLY) {
                /*
                 * This serves as a state change without any output.
                 * It is useful for reading simple stateful encodings,
                 * for example using just Shift-In/Shift-Out codes.
                 * The 21 unused bits may later be used for more sophisticated
                 * state transitions.
                 */
                if(cnv->sharedData->mbcs.dbcsOnlyState!=0) {
                    /* SI/SO are illegal for DBCS-only conversion */
                    state = static_cast<uint8_t>(cnv->mode); /* restore the previous state */

                    /* callback(illegal) */
                    *pErrorCode=U_ILLEGAL_CHAR_FOUND;
                }
            } else if(action==MBCS_STATE_FALLBACK_DIRECT_16) {
                if(UCNV_TO_U_USE_FALLBACK(cnv)) {
                    /* output BMP code point */
                    c = static_cast<char16_t>(MBCS_ENTRY_FINAL_VALUE_16(entry));
                    break;
                }
            } else if(action==MBCS_STATE_UNASSIGNED) {
                /* just fall through */
            } else if(action==MBCS_STATE_ILLEGAL) {
                /* callback(illegal) */
                *pErrorCode=U_ILLEGAL_CHAR_FOUND;
            } else {
                /* reserved (must never occur), or only state change */
                offset=0;
                lastSource=source;
                continue;
            }

            /* end of action codes: prepare for a new character */
            offset=0;

            if(U_FAILURE(*pErrorCode)) {
                /* callback(illegal) */
                break;
            } else /* unassigned sequence */ {
                /* defer to the generic implementation */
                cnv->toUnicodeStatus=0;
                cnv->mode=state;
                pArgs->source = reinterpret_cast<const char*>(lastSource);
                return UCNV_GET_NEXT_UCHAR_USE_TO_U;
            }
        }
    }

    if(c<0) {
        if(U_SUCCESS(*pErrorCode) && source==sourceLimit && lastSource<source) {
            /* incomplete character byte sequence */
            uint8_t *bytes=cnv->toUBytes;
            cnv->toULength = static_cast<int8_t>(source - lastSource);
            do {
                *bytes++=*lastSource++;
            } while(lastSource<source);
            *pErrorCode=U_TRUNCATED_CHAR_FOUND;
        } else if(U_FAILURE(*pErrorCode)) {
            /* callback(illegal) */
            /*
             * Ticket 5691: consistent illegal sequences:
             * - We include at least the first byte in the illegal sequence.
             * - If any of the non-initial bytes could be the start of a character,
             *   we stop the illegal sequence before the first one of those.
             */
            UBool isDBCSOnly = static_cast<UBool>(cnv->sharedData->mbcs.dbcsOnlyState != 0);
            uint8_t *bytes=cnv->toUBytes;
            *bytes++=*lastSource++;     /* first byte */
            if(lastSource==source) {
                cnv->toULength=1;
            } else /* lastSource<source: multi-byte character */ {
                int8_t i;
                for(i=1;
                    lastSource<source && !isSingleOrLead(stateTable, state, isDBCSOnly, *lastSource);
                    ++i
                ) {
                    *bytes++=*lastSource++;
                }
                cnv->toULength=i;
                source=lastSource;
            }
        } else {
            /* no output because of empty input or only state changes */
            *pErrorCode=U_INDEX_OUTOFBOUNDS_ERROR;
        }
        c=0xffff;
    }

    /* set the converter state back into UConverter, ready for a new character */
    cnv->toUnicodeStatus=0;
    cnv->mode=state;

    /* write back the updated pointer */
    pArgs->source = reinterpret_cast<const char*>(source);
    return c;
}

#if 0
/*
 * Code disabled 2002dec09 (ICU 2.4) because it is not currently used in ICU. markus
 * Removal improves code coverage.
 */
/**
 * This version of ucnv_MBCSSimpleGetNextUChar() is optimized for single-byte, single-state codepages.
 * It does not handle the EBCDIC swaplfnl option (set in UConverter).
 * It does not handle conversion extensions (_extToU()).
 */
U_CFUNC UChar32
ucnv_MBCSSingleSimpleGetNextUChar(UConverterSharedData *sharedData,
                              uint8_t b, UBool useFallback) {
    int32_t entry;
    uint8_t action;

    entry=sharedData->mbcs.stateTable[0][b];
    /* MBCS_ENTRY_IS_FINAL(entry) */

    if(MBCS_ENTRY_FINAL_IS_VALID_DIRECT_16(entry)) {
        /* output BMP code point */
        return (char16_t)MBCS_ENTRY_FINAL_VALUE_16(entry);
    }

    /*
     * An if-else-if chain provides more reliable performance for
     * the most common cases compared to a switch.
     */
    action=(uint8_t)(MBCS_ENTRY_FINAL_ACTION(entry));
    if(action==MBCS_STATE_VALID_DIRECT_20) {
        /* output supplementary code point */
        return 0x10000+MBCS_ENTRY_FINAL_VALUE(entry);
    } else if(action==MBCS_STATE_FALLBACK_DIRECT_16) {
        if(!TO_U_USE_FALLBACK(useFallback)) {
            return 0xfffe;
        }
        /* output BMP code point */
        return (char16_t)MBCS_ENTRY_FINAL_VALUE_16(entry);
    } else if(action==MBCS_STATE_FALLBACK_DIRECT_20) {
        if(!TO_U_USE_FALLBACK(useFallback)) {
            return 0xfffe;
        }
        /* output supplementary code point */
        return 0x10000+MBCS_ENTRY_FINAL_VALUE(entry);
    } else if(action==MBCS_STATE_UNASSIGNED) {
        return 0xfffe;
    } else if(action==MBCS_STATE_ILLEGAL) {
        return 0xffff;
    } else {
        /* reserved, must never occur */
        return 0xffff;
    }
}
#endif

/*
 * This is a simple version of _MBCSGetNextUChar() that is used
 * by other converter implementations.
 * It only returns an "assigned" result if it consumes the entire input.
 * It does not use state from the converter, nor error codes.
 * It does not handle the EBCDIC swaplfnl option (set in UConverter).
 * It handles conversion extensions but not GB 18030.
 *
 * Return value:
 * U+fffe   unassigned
 * U+ffff   illegal
 * otherwise the Unicode code point
 */
U_CFUNC UChar32
ucnv_MBCSSimpleGetNextUChar(UConverterSharedData *sharedData,
                        const char *source, int32_t length,
                        UBool useFallback) {
    const int32_t (*stateTable)[256];
    const uint16_t *unicodeCodeUnits;

    uint32_t offset;
    uint8_t state, action;

    UChar32 c;
    int32_t i, entry;

    if(length<=0) {
        /* no input at all: "illegal" */
        return 0xffff;
    }

#if 0
/*
 * Code disabled 2002dec09 (ICU 2.4) because it is not currently used in ICU. markus
 * TODO In future releases, verify that this function is never called for SBCS
 * conversions, i.e., that sharedData->mbcs.countStates==1 is still true.
 * Removal improves code coverage.
 */
    /* use optimized function if possible */
    if(sharedData->mbcs.countStates==1) {
        if(length==1) {
            return ucnv_MBCSSingleSimpleGetNextUChar(sharedData, (uint8_t)*source, useFallback);
        } else {
            return 0xffff; /* illegal: more than a single byte for an SBCS converter */
        }
    }
#endif

    /* set up the local pointers */
    stateTable=sharedData->mbcs.stateTable;
    unicodeCodeUnits=sharedData->mbcs.unicodeCodeUnits;

    /* converter state */
    offset=0;
    state=sharedData->mbcs.dbcsOnlyState;

    /* conversion loop */
    for(i=0;;) {
        entry=stateTable[state][(uint8_t)source[i++]];
        if(MBCS_ENTRY_IS_TRANSITION(entry)) {
            state=(uint8_t)MBCS_ENTRY_TRANSITION_STATE(entry);
            offset+=MBCS_ENTRY_TRANSITION_OFFSET(entry);

            if(i==length) {
                return 0xffff; /* truncated character */
            }
        } else {
            /*
             * An if-else-if chain provides more reliable performance for
             * the most common cases compared to a switch.
             */
            action=(uint8_t)(MBCS_ENTRY_FINAL_ACTION(entry));
            if(action==MBCS_STATE_VALID_16) {
                offset+=MBCS_ENTRY_FINAL_VALUE_16(entry);
                c=unicodeCodeUnits[offset];
                if(c!=0xfffe) {
                    /* done */
                } else if(UCNV_TO_U_USE_FALLBACK(cnv)) {
                    c=ucnv_MBCSGetFallback(&sharedData->mbcs, offset);
                /* else done with 0xfffe */
                }
                break;
            } else if(action==MBCS_STATE_VALID_DIRECT_16) {
                /* output BMP code point */
                c=(char16_t)MBCS_ENTRY_FINAL_VALUE_16(entry);
                break;
            } else if(action==MBCS_STATE_VALID_16_PAIR) {
                offset+=MBCS_ENTRY_FINAL_VALUE_16(entry);
                c=unicodeCodeUnits[offset++];
                if(c<0xd800) {
                    /* output BMP code point below 0xd800 */
                } else if(UCNV_TO_U_USE_FALLBACK(cnv) ? c<=0xdfff : c<=0xdbff) {
                    /* output roundtrip or fallback supplementary code point */
                    c=(UChar32)(((c&0x3ff)<<10)+unicodeCodeUnits[offset]+(0x10000-0xdc00));
                } else if(UCNV_TO_U_USE_FALLBACK(cnv) ? (c&0xfffe)==0xe000 : c==0xe000) {
                    /* output roundtrip BMP code point above 0xd800 or fallback BMP code point */
                    c=unicodeCodeUnits[offset];
                } else if(c==0xffff) {
                    return 0xffff;
                } else {
                    c=0xfffe;
                }
                break;
            } else if(action==MBCS_STATE_VALID_DIRECT_20) {
                /* output supplementary code point */
                c=0x10000+MBCS_ENTRY_FINAL_VALUE(entry);
                break;
            } else if(action==MBCS_STATE_FALLBACK_DIRECT_16) {
                if(!TO_U_USE_FALLBACK(useFallback)) {
                    c=0xfffe;
                    break;
                }
                /* output BMP code point */
                c=(char16_t)MBCS_ENTRY_FINAL_VALUE_16(entry);
                break;
            } else if(action==MBCS_STATE_FALLBACK_DIRECT_20) {
                if(!TO_U_USE_FALLBACK(useFallback)) {
                    c=0xfffe;
                    break;
                }
                /* output supplementary code point */
                c=0x10000+MBCS_ENTRY_FINAL_VALUE(entry);
                break;
            } else if(action==MBCS_STATE_UNASSIGNED) {
                c=0xfffe;
                break;
            }

            /*
             * forbid MBCS_STATE_CHANGE_ONLY for this function,
             * and MBCS_STATE_ILLEGAL and reserved action codes
             */
            return 0xffff;
        }
    }

    if(i!=length) {
        /* illegal for this function: not all input consumed */
        return 0xffff;
    }

    if(c==0xfffe) {
        /* try an extension mapping */
        const int32_t *cx=sharedData->mbcs.extIndexes;
        if(cx!=nullptr) {
            return ucnv_extSimpleMatchToU(cx, source, length, useFallback);
        }
    }

    return c;
}

/* MBCS-from-Unicode conversion functions ----------------------------------- */

/* This version of ucnv_MBCSFromUnicodeWithOffsets() is optimized for double-byte codepages. */
static void
ucnv_MBCSDoubleFromUnicodeWithOffsets(UConverterFromUnicodeArgs *pArgs,
                                  UErrorCode *pErrorCode) {
    UConverter *cnv;
    const char16_t *source, *sourceLimit;
    uint8_t *target;
    int32_t targetCapacity;
    int32_t *offsets;

    const uint16_t *table;
    const uint16_t *mbcsIndex;
    const uint8_t *bytes;

    UChar32 c;

    int32_t sourceIndex, nextSourceIndex;

    uint32_t stage2Entry;
    uint32_t asciiRoundtrips;
    uint32_t value;
    uint8_t unicodeMask;

    /* use optimized function if possible */
    cnv=pArgs->converter;
    unicodeMask=cnv->sharedData->mbcs.unicodeMask;

    /* set up the local pointers */
    source=pArgs->source;
    sourceLimit=pArgs->sourceLimit;
    target = reinterpret_cast<uint8_t*>(pArgs->target);
    targetCapacity = static_cast<int32_t>(pArgs->targetLimit - pArgs->target);
    offsets=pArgs->offsets;

    table=cnv->sharedData->mbcs.fromUnicodeTable;
    mbcsIndex=cnv->sharedData->mbcs.mbcsIndex;
    if((cnv->options&UCNV_OPTION_SWAP_LFNL)!=0) {
        bytes=cnv->sharedData->mbcs.swapLFNLFromUnicodeBytes;
    } else {
        bytes=cnv->sharedData->mbcs.fromUnicodeBytes;
    }
    asciiRoundtrips=cnv->sharedData->mbcs.asciiRoundtrips;

    /* get the converter state from UConverter */
    c=cnv->fromUChar32;

    /* sourceIndex=-1 if the current character began in the previous buffer */
    sourceIndex= c==0 ? 0 : -1;
    nextSourceIndex=0;

    /* conversion loop */
    if(c!=0 && targetCapacity>0) {
        goto getTrail;
    }

    while(source<sourceLimit) {
        /*
         * This following test is to see if available input would overflow the output.
         * It does not catch output of more than one byte that
         * overflows as a result of a multi-byte character or callback output
         * from the last source character.
         * Therefore, those situations also test for overflows and will
         * then break the loop, too.
         */
        if(targetCapacity>0) {
            /*
             * Get a correct Unicode code point:
             * a single char16_t for a BMP code point or
             * a matched surrogate pair for a "supplementary code point".
             */
            c=*source++;
            ++nextSourceIndex;
            if(c<=0x7f && IS_ASCII_ROUNDTRIP(c, asciiRoundtrips)) {
                *target++ = static_cast<uint8_t>(c);
                if(offsets!=nullptr) {
                    *offsets++=sourceIndex;
                    sourceIndex=nextSourceIndex;
                }
                --targetCapacity;
                c=0;
                continue;
            }
            /*
             * utf8Friendly table: Test for <=0xd7ff rather than <=MBCS_FAST_MAX
             * to avoid dealing with surrogates.
             * MBCS_FAST_MAX must be >=0xd7ff.
             */
            if(c<=0xd7ff) {
                value=DBCS_RESULT_FROM_MOST_BMP(mbcsIndex, (const uint16_t *)bytes, c);
                /* There are only roundtrips (!=0) and no-mapping (==0) entries. */
                if(value==0) {
                    goto unassigned;
                }
                /* output the value */
            } else {
                /*
                 * This also tests if the codepage maps single surrogates.
                 * If it does, then surrogates are not paired but mapped separately.
                 * Note that in this case unmatched surrogates are not detected.
                 */
                if(U16_IS_SURROGATE(c) && !(unicodeMask&UCNV_HAS_SURROGATES)) {
                    if(U16_IS_SURROGATE_LEAD(c)) {
getTrail:
                        if(source<sourceLimit) {
                            /* test the following code unit */
                            char16_t trail=*source;
                            if(U16_IS_TRAIL(trail)) {
                                ++source;
                                ++nextSourceIndex;
                                c=U16_GET_SUPPLEMENTARY(c, trail);
                                if(!(unicodeMask&UCNV_HAS_SUPPLEMENTARY)) {
                                    /* BMP-only codepages are stored without stage 1 entries for supplementary code points */
                                    /* callback(unassigned) */
                                    goto unassigned;
                                }
                                /* convert this supplementary code point */
                                /* exit this condition tree */
                            } else {
                                /* this is an unmatched lead code unit (1st surrogate) */
                                /* callback(illegal) */
                                *pErrorCode=U_ILLEGAL_CHAR_FOUND;
                                break;
                            }
                        } else {
                            /* no more input */
                            break;
                        }
                    } else {
                        /* this is an unmatched trail code unit (2nd surrogate) */
                        /* callback(illegal) */
                        *pErrorCode=U_ILLEGAL_CHAR_FOUND;
                        break;
                    }
                }

                /* convert the Unicode code point in c into codepage bytes */
                stage2Entry=MBCS_STAGE_2_FROM_U(table, c);

                /* get the bytes and the length for the output */
                /* MBCS_OUTPUT_2 */
                value=MBCS_VALUE_2_FROM_STAGE_2(bytes, stage2Entry, c);

                /* is this code point assigned, or do we use fallbacks? */
                if(!(MBCS_FROM_U_IS_ROUNDTRIP(stage2Entry, c) ||
                     (UCNV_FROM_U_USE_FALLBACK(cnv, c) && value!=0))
                ) {
                    /*
                     * We allow a 0 byte output if the "assigned" bit is set for this entry.
                     * There is no way with this data structure for fallback output
                     * to be a zero byte.
                     */

unassigned:
                    /* try an extension mapping */
                    pArgs->source=source;
                    c=_extFromU(cnv, cnv->sharedData,
                                c, &source, sourceLimit,
                                &target, target+targetCapacity,
                                &offsets, sourceIndex,
                                pArgs->flush,
                                pErrorCode);
                    nextSourceIndex += static_cast<int32_t>(source - pArgs->source);

                    if(U_FAILURE(*pErrorCode)) {
                        /* not mappable or buffer overflow */
                        break;
                    } else {
                        /* a mapping was written to the target, continue */

                        /* recalculate the targetCapacity after an extension mapping */
                        targetCapacity = static_cast<int32_t>(pArgs->targetLimit - reinterpret_cast<char*>(target));

                        /* normal end of conversion: prepare for a new character */
                        sourceIndex=nextSourceIndex;
                        continue;
                    }
                }
            }

            /* write the output character bytes from value and length */
            /* from the first if in the loop we know that targetCapacity>0 */
            if(value<=0xff) {
                /* this is easy because we know that there is enough space */
                *target++ = static_cast<uint8_t>(value);
                if(offsets!=nullptr) {
                    *offsets++=sourceIndex;
                }
                --targetCapacity;
            } else /* length==2 */ {
                *target++ = static_cast<uint8_t>(value >> 8);
                if(2<=targetCapacity) {
                    *target++ = static_cast<uint8_t>(value);
                    if(offsets!=nullptr) {
                        *offsets++=sourceIndex;
                        *offsets++=sourceIndex;
                    }
                    targetCapacity-=2;
                } else {
                    if(offsets!=nullptr) {
                        *offsets++=sourceIndex;
                    }
                    cnv->charErrorBuffer[0] = static_cast<char>(value);
                    cnv->charErrorBufferLength=1;

                    /* target overflow */
                    targetCapacity=0;
                    *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
                    c=0;
                    break;
                }
            }

            /* normal end of conversion: prepare for a new character */
            c=0;
            sourceIndex=nextSourceIndex;
            continue;
        } else {
            /* target is full */
            *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
            break;
        }
    }

    /* set the converter state back into UConverter */
    cnv->fromUChar32=c;

    /* write back the updated pointers */
    pArgs->source=source;
    pArgs->target = reinterpret_cast<char*>(target);
    pArgs->offsets=offsets;
}

/* This version of ucnv_MBCSFromUnicodeWithOffsets() is optimized for single-byte codepages. */
static void
ucnv_MBCSSingleFromUnicodeWithOffsets(UConverterFromUnicodeArgs *pArgs,
                                  UErrorCode *pErrorCode) {
    UConverter *cnv;
    const char16_t *source, *sourceLimit;
    uint8_t *target;
    int32_t targetCapacity;
    int32_t *offsets;

    const uint16_t *table;
    const uint16_t *results;

    UChar32 c;

    int32_t sourceIndex, nextSourceIndex;

    uint16_t value, minValue;
    UBool hasSupplementary;

    /* set up the local pointers */
    cnv=pArgs->converter;
    source=pArgs->source;
    sourceLimit=pArgs->sourceLimit;
    target = reinterpret_cast<uint8_t*>(pArgs->target);
    targetCapacity = static_cast<int32_t>(pArgs->targetLimit - pArgs->target);
    offsets=pArgs->offsets;

    table=cnv->sharedData->mbcs.fromUnicodeTable;
    if((cnv->options&UCNV_OPTION_SWAP_LFNL)!=0) {
        results = reinterpret_cast<uint16_t*>(cnv->sharedData->mbcs.swapLFNLFromUnicodeBytes);
    } else {
        results=(uint16_t *)cnv->sharedData->mbcs.fromUnicodeBytes;
    }

    if(cnv->useFallback) {
        /* use all roundtrip and fallback results */
        minValue=0x800;
    } else {
        /* use only roundtrips and fallbacks from private-use characters */
        minValue=0xc00;
    }
    hasSupplementary = static_cast<UBool>(cnv->sharedData->mbcs.unicodeMask & UCNV_HAS_SUPPLEMENTARY);

    /* get the converter state from UConverter */
    c=cnv->fromUChar32;

    /* sourceIndex=-1 if the current character began in the previous buffer */
    sourceIndex= c==0 ? 0 : -1;
    nextSourceIndex=0;

    /* conversion loop */
    if(c!=0 && targetCapacity>0) {
        goto getTrail;
    }

    while(source<sourceLimit) {
        /*
         * This following test is to see if available input would overflow the output.
         * It does not catch output of more than one byte that
         * overflows as a result of a multi-byte character or callback output
         * from the last source character.
         * Therefore, those situations also test for overflows and will
         * then break the loop, too.
         */
        if(targetCapacity>0) {
            /*
             * Get a correct Unicode code point:
             * a single char16_t for a BMP code point or
             * a matched surrogate pair for a "supplementary code point".
             */
            c=*source++;
            ++nextSourceIndex;
            if(U16_IS_SURROGATE(c)) {
                if(U16_IS_SURROGATE_LEAD(c)) {
getTrail:
                    if(source<sourceLimit) {
                        /* test the following code unit */
                        char16_t trail=*source;
                        if(U16_IS_TRAIL(trail)) {
                            ++source;
                            ++nextSourceIndex;
                            c=U16_GET_SUPPLEMENTARY(c, trail);
                            if(!hasSupplementary) {
                                /* BMP-only codepages are stored without stage 1 entries for supplementary code points */
                                /* callback(unassigned) */
                                goto unassigned;
                            }
                            /* convert this supplementary code point */
                            /* exit this condition tree */
                        } else {
                            /* this is an unmatched lead code unit (1st surrogate) */
                            /* callback(illegal) */
                            *pErrorCode=U_ILLEGAL_CHAR_FOUND;
                            break;
                        }
                    } else {
                        /* no more input */
                        break;
                    }
                } else {
                    /* this is an unmatched trail code unit (2nd surrogate) */
                    /* callback(illegal) */
                    *pErrorCode=U_ILLEGAL_CHAR_FOUND;
                    break;
                }
            }

            /* convert the Unicode code point in c into codepage bytes */
            value=MBCS_SINGLE_RESULT_FROM_U(table, results, c);

            /* is this code point assigned, or do we use fallbacks? */
            if(value>=minValue) {
                /* assigned, write the output character bytes from value and length */
                /* length==1 */
                /* this is easy because we know that there is enough space */
                *target++ = static_cast<uint8_t>(value);
                if(offsets!=nullptr) {
                    *offsets++=sourceIndex;
                }
                --targetCapacity;

                /* normal end of conversion: prepare for a new character */
                c=0;
                sourceIndex=nextSourceIndex;
            } else { /* unassigned */
unassigned:
                /* try an extension mapping */
                pArgs->source=source;
                c=_extFromU(cnv, cnv->sharedData,
                            c, &source, sourceLimit,
                            &target, target+targetCapacity,
                            &offsets, sourceIndex,
                            pArgs->flush,
                            pErrorCode);
                nextSourceIndex += static_cast<int32_t>(source - pArgs->source);

                if(U_FAILURE(*pErrorCode)) {
                    /* not mappable or buffer overflow */
                    break;
                } else {
                    /* a mapping was written to the target, continue */

                    /* recalculate the targetCapacity after an extension mapping */
                    targetCapacity = static_cast<int32_t>(pArgs->targetLimit - reinterpret_cast<char*>(target));

                    /* normal end of conversion: prepare for a new character */
                    sourceIndex=nextSourceIndex;
                }
            }
        } else {
            /* target is full */
            *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
            break;
        }
    }

    /* set the converter state back into UConverter */
    cnv->fromUChar32=c;

    /* write back the updated pointers */
    pArgs->source=source;
    pArgs->target = reinterpret_cast<char*>(target);
    pArgs->offsets=offsets;
}

/*
 * This version of ucnv_MBCSFromUnicode() is optimized for single-byte codepages
 * that map only to and from the BMP.
 * In addition to single-byte/state optimizations, the offset calculations
 * become much easier.
 * It would be possible to use the sbcsIndex for UTF-8-friendly tables,
 * but measurements have shown that this diminishes performance
 * in more cases than it improves it.
 * See SVN revision 21013 (2007-feb-06) for the last version with #if switches
 * for various MBCS and SBCS optimizations.
 */
static void
ucnv_MBCSSingleFromBMPWithOffsets(UConverterFromUnicodeArgs *pArgs,
                              UErrorCode *pErrorCode) {
    UConverter *cnv;
    const char16_t *source, *sourceLimit, *lastSource;
    uint8_t *target;
    int32_t targetCapacity, length;
    int32_t *offsets;

    const uint16_t *table;
    const uint16_t *results;

    UChar32 c;

    int32_t sourceIndex;

    uint32_t asciiRoundtrips;
    uint16_t value, minValue;

    /* set up the local pointers */
    cnv=pArgs->converter;
    source=pArgs->source;
    sourceLimit=pArgs->sourceLimit;
    target = reinterpret_cast<uint8_t*>(pArgs->target);
    targetCapacity = static_cast<int32_t>(pArgs->targetLimit - pArgs->target);
    offsets=pArgs->offsets;

    table=cnv->sharedData->mbcs.fromUnicodeTable;
    if((cnv->options&UCNV_OPTION_SWAP_LFNL)!=0) {
        results = reinterpret_cast<uint16_t*>(cnv->sharedData->mbcs.swapLFNLFromUnicodeBytes);
    } else {
        results=(uint16_t *)cnv->sharedData->mbcs.fromUnicodeBytes;
    }
    asciiRoundtrips=cnv->sharedData->mbcs.asciiRoundtrips;

    if(cnv->useFallback) {
        /* use all roundtrip and fallback results */
        minValue=0x800;
    } else {
        /* use only roundtrips and fallbacks from private-use characters */
        minValue=0xc00;
    }

    /* get the converter state from UConverter */
    c=cnv->fromUChar32;

    /* sourceIndex=-1 if the current character began in the previous buffer */
    sourceIndex= c==0 ? 0 : -1;
    lastSource=source;

    /*
     * since the conversion here is 1:1 char16_t:uint8_t, we need only one counter
     * for the minimum of the sourceLength and targetCapacity
     */
    length = static_cast<int32_t>(sourceLimit - source);
    if(length<targetCapacity) {
        targetCapacity=length;
    }

    /* conversion loop */
    if(c!=0 && targetCapacity>0) {
        goto getTrail;
    }

#if MBCS_UNROLL_SINGLE_FROM_BMP
    /* unrolling makes it slower on Pentium III/Windows 2000?! */
    /* unroll the loop with the most common case */
unrolled:
    if(targetCapacity>=4) {
        int32_t count, loops;
        uint16_t andedValues;

        loops=count=targetCapacity>>2;
        do {
            c=*source++;
            andedValues=value=MBCS_SINGLE_RESULT_FROM_U(table, results, c);
            *target++=(uint8_t)value;
            c=*source++;
            andedValues&=value=MBCS_SINGLE_RESULT_FROM_U(table, results, c);
            *target++=(uint8_t)value;
            c=*source++;
            andedValues&=value=MBCS_SINGLE_RESULT_FROM_U(table, results, c);
            *target++=(uint8_t)value;
            c=*source++;
            andedValues&=value=MBCS_SINGLE_RESULT_FROM_U(table, results, c);
            *target++=(uint8_t)value;

            /* were all 4 entries really valid? */
            if(andedValues<minValue) {
                /* no, return to the first of these 4 */
                source-=4;
                target-=4;
                break;
            }
        } while(--count>0);
        count=loops-count;
        targetCapacity-=4*count;

        if(offsets!=nullptr) {
            lastSource+=4*count;
            while(count>0) {
                *offsets++=sourceIndex++;
                *offsets++=sourceIndex++;
                *offsets++=sourceIndex++;
                *offsets++=sourceIndex++;
                --count;
            }
        }

        c=0;
    }
#endif

    while(targetCapacity>0) {
        /*
         * Get a correct Unicode code point:
         * a single char16_t for a BMP code point or
         * a matched surrogate pair for a "supplementary code point".
         */
        c=*source++;
        /*
         * Do not immediately check for single surrogates:
         * Assume that they are unassigned and check for them in that case.
         * This speeds up the conversion of assigned characters.
         */
        /* convert the Unicode code point in c into codepage bytes */
        if(c<=0x7f && IS_ASCII_ROUNDTRIP(c, asciiRoundtrips)) {
            *target++ = static_cast<uint8_t>(c);
            --targetCapacity;
            c=0;
            continue;
        }
        value=MBCS_SINGLE_RESULT_FROM_U(table, results, c);
        /* is this code point assigned, or do we use fallbacks? */
        if(value>=minValue) {
            /* assigned, write the output character bytes from value and length */
            /* length==1 */
            /* this is easy because we know that there is enough space */
            *target++ = static_cast<uint8_t>(value);
            --targetCapacity;

            /* normal end of conversion: prepare for a new character */
            c=0;
            continue;
        } else if(!U16_IS_SURROGATE(c)) {
            /* normal, unassigned BMP character */
        } else if(U16_IS_SURROGATE_LEAD(c)) {
getTrail:
            if(source<sourceLimit) {
                /* test the following code unit */
                char16_t trail=*source;
                if(U16_IS_TRAIL(trail)) {
                    ++source;
                    c=U16_GET_SUPPLEMENTARY(c, trail);
                    /* this codepage does not map supplementary code points */
                    /* callback(unassigned) */
                } else {
                    /* this is an unmatched lead code unit (1st surrogate) */
                    /* callback(illegal) */
                    *pErrorCode=U_ILLEGAL_CHAR_FOUND;
                    break;
                }
            } else {
                /* no more input */
                if (pArgs->flush) {
                    *pErrorCode=U_TRUNCATED_CHAR_FOUND;
                }
                break;
            }
        } else {
            /* this is an unmatched trail code unit (2nd surrogate) */
            /* callback(illegal) */
            *pErrorCode=U_ILLEGAL_CHAR_FOUND;
            break;
        }

        /* c does not have a mapping */

        /* get the number of code units for c to correctly advance sourceIndex */
        length=U16_LENGTH(c);

        /* set offsets since the start or the last extension */
        if(offsets!=nullptr) {
            int32_t count = static_cast<int32_t>(source - lastSource);

            /* do not set the offset for this character */
            count-=length;

            while(count>0) {
                *offsets++=sourceIndex++;
                --count;
            }
            /* offsets and sourceIndex are now set for the current character */
        }

        /* try an extension mapping */
        lastSource=source;
        c=_extFromU(cnv, cnv->sharedData,
                    c, &source, sourceLimit,
                    &target, reinterpret_cast<const uint8_t*>(pArgs->targetLimit),
                    &offsets, sourceIndex,
                    pArgs->flush,
                    pErrorCode);
        sourceIndex += length + static_cast<int32_t>(source - lastSource);
        lastSource=source;

        if(U_FAILURE(*pErrorCode)) {
            /* not mappable or buffer overflow */
            break;
        } else {
            /* a mapping was written to the target, continue */

            /* recalculate the targetCapacity after an extension mapping */
            targetCapacity = static_cast<int32_t>(pArgs->targetLimit - reinterpret_cast<char*>(target));
            length = static_cast<int32_t>(sourceLimit - source);
            if(length<targetCapacity) {
                targetCapacity=length;
            }
        }

#if MBCS_UNROLL_SINGLE_FROM_BMP
        /* unrolling makes it slower on Pentium III/Windows 2000?! */
        goto unrolled;
#endif
    }

    if(U_SUCCESS(*pErrorCode) && source<sourceLimit && target>=(uint8_t *)pArgs->targetLimit) {
        /* target is full */
        *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
    }

    /* set offsets since the start or the last callback */
    if(offsets!=nullptr) {
        size_t count=source-lastSource;
        if (count > 0 && *pErrorCode == U_TRUNCATED_CHAR_FOUND) {
            /*
            Caller gave us a partial supplementary character,
            which this function couldn't convert in any case.
            The callback will handle the offset.
            */
            count--;
        }
        while(count>0) {
            *offsets++=sourceIndex++;
            --count;
        }
    }

    /* set the converter state back into UConverter */
    cnv->fromUChar32=c;

    /* write back the updated pointers */
    pArgs->source=source;
    pArgs->target = reinterpret_cast<char*>(target);
    pArgs->offsets=offsets;
}

U_CFUNC void
ucnv_MBCSFromUnicodeWithOffsets(UConverterFromUnicodeArgs *pArgs,
                            UErrorCode *pErrorCode) {
    UConverter *cnv;
    const char16_t *source, *sourceLimit;
    uint8_t *target;
    int32_t targetCapacity;
    int32_t *offsets;

    const uint16_t *table;
    const uint16_t *mbcsIndex;
    const uint8_t *p, *bytes;
    uint8_t outputType;

    UChar32 c;

    int32_t prevSourceIndex, sourceIndex, nextSourceIndex;

    uint32_t stage2Entry;
    uint32_t asciiRoundtrips;
    uint32_t value;
    /* Shift-In and Shift-Out byte sequences differ by encoding scheme. */
    uint8_t siBytes[2] = {0, 0};
    uint8_t soBytes[2] = {0, 0};
    uint8_t siLength, soLength;
    int32_t length = 0, prevLength;
    uint8_t unicodeMask;

    cnv=pArgs->converter;

    if(cnv->preFromUFirstCP>=0) {
        /*
         * pass sourceIndex=-1 because we continue from an earlier buffer
         * in the future, this may change with continuous offsets
         */
        ucnv_extContinueMatchFromU(cnv, pArgs, -1, pErrorCode);

        if(U_FAILURE(*pErrorCode) || cnv->preFromULength<0) {
            return;
        }
    }

    /* use optimized function if possible */
    outputType=cnv->sharedData->mbcs.outputType;
    unicodeMask=cnv->sharedData->mbcs.unicodeMask;
    if(outputType==MBCS_OUTPUT_1 && !(unicodeMask&UCNV_HAS_SURROGATES)) {
        if(!(unicodeMask&UCNV_HAS_SUPPLEMENTARY)) {
            ucnv_MBCSSingleFromBMPWithOffsets(pArgs, pErrorCode);
        } else {
            ucnv_MBCSSingleFromUnicodeWithOffsets(pArgs, pErrorCode);
        }
        return;
    } else if(outputType==MBCS_OUTPUT_2 && cnv->sharedData->mbcs.utf8Friendly) {
        ucnv_MBCSDoubleFromUnicodeWithOffsets(pArgs, pErrorCode);
        return;
    }

    /* set up the local pointers */
    source=pArgs->source;
    sourceLimit=pArgs->sourceLimit;
    target=(uint8_t *)pArgs->target;
    targetCapacity=(int32_t)(pArgs->targetLimit-pArgs->target);
    offsets=pArgs->offsets;

    table=cnv->sharedData->mbcs.fromUnicodeTable;
    if(cnv->sharedData->mbcs.utf8Friendly) {
        mbcsIndex=cnv->sharedData->mbcs.mbcsIndex;
    } else {
        mbcsIndex=nullptr;
    }
    if((cnv->options&UCNV_OPTION_SWAP_LFNL)!=0) {
        bytes=cnv->sharedData->mbcs.swapLFNLFromUnicodeBytes;
    } else {
        bytes=cnv->sharedData->mbcs.fromUnicodeBytes;
    }
    asciiRoundtrips=cnv->sharedData->mbcs.asciiRoundtrips;

    /* get the converter state from UConverter */
    c=cnv->fromUChar32;

    if(outputType==MBCS_OUTPUT_2_SISO) {
        prevLength=cnv->fromUnicodeStatus;
        if(prevLength==0) {
            /* set the real value */
            prevLength=1;
        }
    } else {
        /* prevent fromUnicodeStatus from being set to something non-0 */
        prevLength=0;
    }

    /* sourceIndex=-1 if the current character began in the previous buffer */
    prevSourceIndex=-1;
    sourceIndex= c==0 ? 0 : -1;
    nextSourceIndex=0;

    /* Get the SI/SO character for the converter */
    siLength = static_cast<uint8_t>(getSISOBytes(SI, cnv->options, siBytes));
    soLength = static_cast<uint8_t>(getSISOBytes(SO, cnv->options, soBytes));

    /* conversion loop */
    /*
     * This is another piece of ugly code:
     * A goto into the loop if the converter state contains a first surrogate
     * from the previous function call.
     * It saves me to check in each loop iteration a check of if(c==0)
     * and duplicating the trail-surrogate-handling code in the else
     * branch of that check.
     * I could not find any other way to get around this other than
     * using a function call for the conversion and callback, which would
     * be even more inefficient.
     *
     * Markus Scherer 2000-jul-19
     */
    if(c!=0 && targetCapacity>0) {
        goto getTrail;
    }

    while(source<sourceLimit) {
        /*
         * This following test is to see if available input would overflow the output.
         * It does not catch output of more than one byte that
         * overflows as a result of a multi-byte character or callback output
         * from the last source character.
         * Therefore, those situations also test for overflows and will
         * then break the loop, too.
         */
        if(targetCapacity>0) {
            /*
             * Get a correct Unicode code point:
             * a single char16_t for a BMP code point or
             * a matched surrogate pair for a "supplementary code point".
             */
            c=*source++;
            ++nextSourceIndex;
            if(c<=0x7f && IS_ASCII_ROUNDTRIP(c, asciiRoundtrips)) {
                *target++=(uint8_t)c;
                if(offsets!=nullptr) {
                    *offsets++=sourceIndex;
                    prevSourceIndex=sourceIndex;
                    sourceIndex=nextSourceIndex;
                }
                --targetCapacity;
                c=0;
                continue;
            }
            /*
             * utf8Friendly table: Test for <=0xd7ff rather than <=MBCS_FAST_MAX
             * to avoid dealing with surrogates.
             * MBCS_FAST_MAX must be >=0xd7ff.
             */
            if(c<=0xd7ff && mbcsIndex!=nullptr) {
                value=mbcsIndex[c>>6];

                /* get the bytes and the length for the output (copied from below and adapted for utf8Friendly data) */
                /* There are only roundtrips (!=0) and no-mapping (==0) entries. */
                switch(outputType) {
                case MBCS_OUTPUT_2:
                    value=((const uint16_t *)bytes)[value +(c&0x3f)];
                    if(value<=0xff) {
                        if(value==0) {
                            goto unassigned;
                        } else {
                            length=1;
                        }
                    } else {
                        length=2;
                    }
                    break;
                case MBCS_OUTPUT_2_SISO:
                    /* 1/2-byte stateful with Shift-In/Shift-Out */
                    /*
                     * Save the old state in the converter object
                     * right here, then change the local prevLength state variable if necessary.
                     * Then, if this character turns out to be unassigned or a fallback that
                     * is not taken, the callback code must not save the new state in the converter
                     * because the new state is for a character that is not output.
                     * However, the callback must still restore the state from the converter
                     * in case the callback function changed it for its output.
                     */
                    cnv->fromUnicodeStatus=prevLength; /* save the old state */
                    value=((const uint16_t *)bytes)[value +(c&0x3f)];
                    if(value<=0xff) {
                        if(value==0) {
                            goto unassigned;
                        } else if(prevLength<=1) {
                            length=1;
                        } else {
                            /* change from double-byte mode to single-byte */
                            if (siLength == 1) {
                                value|=(uint32_t)siBytes[0]<<8;
                                length = 2;
                            } else if (siLength == 2) {
                                value|=(uint32_t)siBytes[1]<<8;
                                value|=(uint32_t)siBytes[0]<<16;
                                length = 3;
                            }
                            prevLength=1;
                        }
                    } else {
                        if(prevLength==2) {
                            length=2;
                        } else {
                            /* change from single-byte mode to double-byte */
                            if (soLength == 1) {
                                value|=(uint32_t)soBytes[0]<<16;
                                length = 3;
                            } else if (soLength == 2) {
                                value|=(uint32_t)soBytes[1]<<16;
                                value|=(uint32_t)soBytes[0]<<24;
                                length = 4;
                            }
                            prevLength=2;
                        }
                    }
                    break;
                case MBCS_OUTPUT_DBCS_ONLY:
                    /* table with single-byte results, but only DBCS mappings used */
                    value=((const uint16_t *)bytes)[value +(c&0x3f)];
                    if(value<=0xff) {
                        /* no mapping or SBCS result, not taken for DBCS-only */
                        goto unassigned;
                    } else {
                        length=2;
                    }
                    break;
                case MBCS_OUTPUT_3:
                    p=bytes+(value+(c&0x3f))*3;
                    value=((uint32_t)*p<<16)|((uint32_t)p[1]<<8)|p[2];
                    if(value<=0xff) {
                        if(value==0) {
                            goto unassigned;
                        } else {
                            length=1;
                        }
                    } else if(value<=0xffff) {
                        length=2;
                    } else {
                        length=3;
                    }
                    break;
                case MBCS_OUTPUT_4:
                    value=((const uint32_t *)bytes)[value +(c&0x3f)];
                    if(value<=0xff) {
                        if(value==0) {
                            goto unassigned;
                        } else {
                            length=1;
                        }
                    } else if(value<=0xffff) {
                        length=2;
                    } else if(value<=0xffffff) {
                        length=3;
                    } else {
                        length=4;
                    }
                    break;
                case MBCS_OUTPUT_3_EUC:
                    value=((const uint16_t *)bytes)[value +(c&0x3f)];
                    /* EUC 16-bit fixed-length representation */
                    if(value<=0xff) {
                        if(value==0) {
                            goto unassigned;
                        } else {
                            length=1;
                        }
                    } else if((value&0x8000)==0) {
                        value|=0x8e8000;
                        length=3;
                    } else if((value&0x80)==0) {
                        value|=0x8f0080;
                        length=3;
                    } else {
                        length=2;
                    }
                    break;
                case MBCS_OUTPUT_4_EUC:
                    p=bytes+(value+(c&0x3f))*3;
                    value=((uint32_t)*p<<16)|((uint32_t)p[1]<<8)|p[2];
                    /* EUC 16-bit fixed-length representation applied to the first two bytes */
                    if(value<=0xff) {
                        if(value==0) {
                            goto unassigned;
                        } else {
                            length=1;
                        }
                    } else if(value<=0xffff) {
                        length=2;
                    } else if((value&0x800000)==0) {
                        value|=0x8e800000;
                        length=4;
                    } else if((value&0x8000)==0) {
                        value|=0x8f008000;
                        length=4;
                    } else {
                        length=3;
                    }
                    break;
                default:
                    /* must not occur */
                    /*
                     * To avoid compiler warnings that value & length may be
                     * used without having been initialized, we set them here.
                     * In reality, this is unreachable code.
                     * Not having a default branch also causes warnings with
                     * some compilers.
                     */
                    value=0;
                    length=0;
                    break;
                }
                /* output the value */
            } else {
                /*
                 * This also tests if the codepage maps single surrogates.
                 * If it does, then surrogates are not paired but mapped separately.
                 * Note that in this case unmatched surrogates are not detected.
                 */
                if(U16_IS_SURROGATE(c) && !(unicodeMask&UCNV_HAS_SURROGATES)) {
                    if(U16_IS_SURROGATE_LEAD(c)) {
getTrail:
                        if(source<sourceLimit) {
                            /* test the following code unit */
                            char16_t trail=*source;
                            if(U16_IS_TRAIL(trail)) {
                                ++source;
                                ++nextSourceIndex;
                                c=U16_GET_SUPPLEMENTARY(c, trail);
                                if(!(unicodeMask&UCNV_HAS_SUPPLEMENTARY)) {
                                    /* BMP-only codepages are stored without stage 1 entries for supplementary code points */
                                    cnv->fromUnicodeStatus=prevLength; /* save the old state */
                                    /* callback(unassigned) */
                                    goto unassigned;
                                }
                                /* convert this supplementary code point */
                                /* exit this condition tree */
                            } else {
                                /* this is an unmatched lead code unit (1st surrogate) */
                                /* callback(illegal) */
                                *pErrorCode=U_ILLEGAL_CHAR_FOUND;
                                break;
                            }
                        } else {
                            /* no more input */
                            break;
                        }
                    } else {
                        /* this is an unmatched trail code unit (2nd surrogate) */
                        /* callback(illegal) */
                        *pErrorCode=U_ILLEGAL_CHAR_FOUND;
                        break;
                    }
                }

                /* convert the Unicode code point in c into codepage bytes */

                /*
                 * The basic lookup is a triple-stage compact array (trie) lookup.
                 * For details see the beginning of this file.
                 *
                 * Single-byte codepages are handled with a different data structure
                 * by _MBCSSingle... functions.
                 *
                 * The result consists of a 32-bit value from stage 2 and
                 * a pointer to as many bytes as are stored per character.
                 * The pointer points to the character's bytes in stage 3.
                 * Bits 15..0 of the stage 2 entry contain the stage 3 index
                 * for that pointer, while bits 31..16 are flags for which of
                 * the 16 characters in the block are roundtrip-assigned.
                 *
                 * For 2-byte and 4-byte codepages, the bytes are stored as uint16_t
                 * respectively as uint32_t, in the platform encoding.
                 * For 3-byte codepages, the bytes are always stored in big-endian order.
                 *
                 * For EUC encodings that use only either 0x8e or 0x8f as the first
                 * byte of their longest byte sequences, the first two bytes in
                 * this third stage indicate with their 7th bits whether these bytes
                 * are to be written directly or actually need to be preceded by
                 * one of the two Single-Shift codes. With this, the third stage
                 * stores one byte fewer per character than the actual maximum length of
                 * EUC byte sequences.
                 *
                 * Other than that, leading zero bytes are removed and the other
                 * bytes output. A single zero byte may be output if the "assigned"
                 * bit in stage 2 was on.
                 * The data structure does not support zero byte output as a fallback,
                 * and also does not allow output of leading zeros.
                 */
                stage2Entry=MBCS_STAGE_2_FROM_U(table, c);

                /* get the bytes and the length for the output */
                switch(outputType) {
                case MBCS_OUTPUT_2:
                    value=MBCS_VALUE_2_FROM_STAGE_2(bytes, stage2Entry, c);
                    if(value<=0xff) {
                        length=1;
                    } else {
                        length=2;
                    }
                    break;
                case MBCS_OUTPUT_2_SISO:
                    /* 1/2-byte stateful with Shift-In/Shift-Out */
                    /*
                     * Save the old state in the converter object
                     * right here, then change the local prevLength state variable if necessary.
                     * Then, if this character turns out to be unassigned or a fallback that
                     * is not taken, the callback code must not save the new state in the converter
                     * because the new state is for a character that is not output.
                     * However, the callback must still restore the state from the converter
                     * in case the callback function changed it for its output.
                     */
                    cnv->fromUnicodeStatus=prevLength; /* save the old state */
                    value=MBCS_VALUE_2_FROM_STAGE_2(bytes, stage2Entry, c);
                    if(value<=0xff) {
                        if(value==0 && MBCS_FROM_U_IS_ROUNDTRIP(stage2Entry, c)==0) {
                            /* no mapping, leave value==0 */
                            length=0;
                        } else if(prevLength<=1) {
                            length=1;
                        } else {
                            /* change from double-byte mode to single-byte */
                            if (siLength == 1) {
                                value|=(uint32_t)siBytes[0]<<8;
                                length = 2;
                            } else if (siLength == 2) {
                                value|=(uint32_t)siBytes[1]<<8;
                                value|=(uint32_t)siBytes[0]<<16;
                                length = 3;
                            }
                            prevLength=1;
                        }
                    } else {
                        if(prevLength==2) {
                            length=2;
                        } else {
                            /* change from single-byte mode to double-byte */
                            if (soLength == 1) {
                                value|=(uint32_t)soBytes[0]<<16;
                                length = 3;
                            } else if (soLength == 2) {
                                value|=(uint32_t)soBytes[1]<<16;
                                value|=(uint32_t)soBytes[0]<<24;
                                length = 4;
                            }
                            prevLength=2;
                        }
                    }
                    break;
                case MBCS_OUTPUT_DBCS_ONLY:
                    /* table with single-byte results, but only DBCS mappings used */
                    value=MBCS_VALUE_2_FROM_STAGE_2(bytes, stage2Entry, c);
                    if(value<=0xff) {
                        /* no mapping or SBCS result, not taken for DBCS-only */
                        value=stage2Entry=0; /* stage2Entry=0 to reset roundtrip flags */
                        length=0;
                    } else {
                        length=2;
                    }
                    break;
                case MBCS_OUTPUT_3:
                    p=MBCS_POINTER_3_FROM_STAGE_2(bytes, stage2Entry, c);
                    value=((uint32_t)*p<<16)|((uint32_t)p[1]<<8)|p[2];
                    if(value<=0xff) {
                        length=1;
                    } else if(value<=0xffff) {
                        length=2;
                    } else {
                        length=3;
                    }
                    break;
                case MBCS_OUTPUT_4:
                    value=MBCS_VALUE_4_FROM_STAGE_2(bytes, stage2Entry, c);
                    if(value<=0xff) {
                        length=1;
                    } else if(value<=0xffff) {
                        length=2;
                    } else if(value<=0xffffff) {
                        length=3;
                    } else {
                        length=4;
                    }
                    break;
                case MBCS_OUTPUT_3_EUC:
                    value=MBCS_VALUE_2_FROM_STAGE_2(bytes, stage2Entry, c);
                    /* EUC 16-bit fixed-length representation */
                    if(value<=0xff) {
                        length=1;
                    } else if((value&0x8000)==0) {
                        value|=0x8e8000;
                        length=3;
                    } else if((value&0x80)==0) {
                        value|=0x8f0080;
                        length=3;
                    } else {
                        length=2;
                    }
                    break;
                case MBCS_OUTPUT_4_EUC:
                    p=MBCS_POINTER_3_FROM_STAGE_2(bytes, stage2Entry, c);
                    value=((uint32_t)*p<<16)|((uint32_t)p[1]<<8)|p[2];
                    /* EUC 16-bit fixed-length representation applied to the first two bytes */
                    if(value<=0xff) {
                        length=1;
                    } else if(value<=0xffff) {
                        length=2;
                    } else if((value&0x800000)==0) {
                        value|=0x8e800000;
                        length=4;
                    } else if((value&0x8000)==0) {
                        value|=0x8f008000;
                        length=4;
                    } else {
                        length=3;
                    }
                    break;
                default:
                    /* must not occur */
                    /*
                     * To avoid compiler warnings that value & length may be
                     * used without having been initialized, we set them here.
                     * In reality, this is unreachable code.
                     * Not having a default branch also causes warnings with
                     * some compilers.
                     */
                    value=stage2Entry=0; /* stage2Entry=0 to reset roundtrip flags */
                    length=0;
                    break;
                }

                /* is this code point assigned, or do we use fallbacks? */
                if(!(MBCS_FROM_U_IS_ROUNDTRIP(stage2Entry, c)!=0 ||
                     (UCNV_FROM_U_USE_FALLBACK(cnv, c) && value!=0))
                ) {
                    /*
                     * We allow a 0 byte output if the "assigned" bit is set for this entry.
                     * There is no way with this data structure for fallback output
                     * to be a zero byte.
                     */

unassigned:
                    /* try an extension mapping */
                    pArgs->source=source;
                    c=_extFromU(cnv, cnv->sharedData,
                                c, &source, sourceLimit,
                                &target, target+targetCapacity,
                                &offsets, sourceIndex,
                                pArgs->flush,
                                pErrorCode);
                    nextSourceIndex+=(int32_t)(source-pArgs->source);
                    prevLength=cnv->fromUnicodeStatus; /* restore SISO state */

                    if(U_FAILURE(*pErrorCode)) {
                        /* not mappable or buffer overflow */
                        break;
                    } else {
                        /* a mapping was written to the target, continue */

                        /* recalculate the targetCapacity after an extension mapping */
                        targetCapacity=(int32_t)(pArgs->targetLimit-(char *)target);

                        /* normal end of conversion: prepare for a new character */
                        if(offsets!=nullptr) {
                            prevSourceIndex=sourceIndex;
                            sourceIndex=nextSourceIndex;
                        }
                        continue;
                    }
                }
            }

            /* write the output character bytes from value and length */
            /* from the first if in the loop we know that targetCapacity>0 */
            if(length<=targetCapacity) {
                if(offsets==nullptr) {
                    switch(length) {
                        /* each branch falls through to the next one */
                    case 4:
                        *target++=(uint8_t)(value>>24);
                        U_FALLTHROUGH;
                    case 3:
                        *target++=(uint8_t)(value>>16);
                        U_FALLTHROUGH;
                    case 2:
                        *target++=(uint8_t)(value>>8);
                        U_FALLTHROUGH;
                    case 1:
                        *target++=(uint8_t)value;
                        U_FALLTHROUGH;
                    default:
                        /* will never occur */
                        break;
                    }
                } else {
                    switch(length) {
                        /* each branch falls through to the next one */
                    case 4:
                        *target++=(uint8_t)(value>>24);
                        *offsets++=sourceIndex;
                        U_FALLTHROUGH;
                    case 3:
                        *target++=(uint8_t)(value>>16);
                        *offsets++=sourceIndex;
                        U_FALLTHROUGH;
                    case 2:
                        *target++=(uint8_t)(value>>8);
                        *offsets++=sourceIndex;
                        U_FALLTHROUGH;
                    case 1:
                        *target++=(uint8_t)value;
                        *offsets++=sourceIndex;
                        U_FALLTHROUGH;
                    default:
                        /* will never occur */
                        break;
                    }
                }
                targetCapacity-=length;
            } else {
                uint8_t *charErrorBuffer;

                /*
                 * We actually do this backwards here:
                 * In order to save an intermediate variable, we output
                 * first to the overflow buffer what does not fit into the
                 * regular target.
                 */
                /* we know that 1<=targetCapacity<length<=4 */
                length-=targetCapacity;
                charErrorBuffer=(uint8_t *)cnv->charErrorBuffer;
                switch(length) {
                    /* each branch falls through to the next one */
                case 3:
                    *charErrorBuffer++=(uint8_t)(value>>16);
                    U_FALLTHROUGH;
                case 2:
                    *charErrorBuffer++=(uint8_t)(value>>8);
                    U_FALLTHROUGH;
                case 1:
                    *charErrorBuffer=(uint8_t)value;
                    U_FALLTHROUGH;
                default:
                    /* will never occur */
                    break;
                }
                cnv->charErrorBufferLength=(int8_t)length;

                /* now output what fits into the regular target */
                value>>=8*length; /* length was reduced by targetCapacity */
                switch(targetCapacity) {
                    /* each branch falls through to the next one */
                case 3:
                    *target++=(uint8_t)(value>>16);
                    if(offsets!=nullptr) {
                        *offsets++=sourceIndex;
                    }
                    U_FALLTHROUGH;
                case 2:
                    *target++=(uint8_t)(value>>8);
                    if(offsets!=nullptr) {
                        *offsets++=sourceIndex;
                    }
                    U_FALLTHROUGH;
                case 1:
                    *target++=(uint8_t)value;
                    if(offsets!=nullptr) {
                        *offsets++=sourceIndex;
                    }
                    U_FALLTHROUGH;
                default:
                    /* will never occur */
                    break;
                }

                /* target overflow */
                targetCapacity=0;
                *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
                c=0;
                break;
            }

            /* normal end of conversion: prepare for a new character */
            c=0;
            if(offsets!=nullptr) {
                prevSourceIndex=sourceIndex;
                sourceIndex=nextSourceIndex;
            }
            continue;
        } else {
            /* target is full */
            *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
            break;
        }
    }

    /*
     * the end of the input stream and detection of truncated input
     * are handled by the framework, but for EBCDIC_STATEFUL conversion
     * we need to emit an SI at the very end
     *
     * conditions:
     *   successful
     *   EBCDIC_STATEFUL in DBCS mode
     *   end of input and no truncated input
     */
    if( U_SUCCESS(*pErrorCode) &&
        outputType==MBCS_OUTPUT_2_SISO && prevLength==2 &&
        pArgs->flush && source>=sourceLimit && c==0
    ) {
        /* EBCDIC_STATEFUL ending with DBCS: emit an SI to return the output stream to SBCS */
        if(targetCapacity>0) {
            *target++ = siBytes[0];
            if (siLength == 2) {
                if (targetCapacity<2) {
                    cnv->charErrorBuffer[0] = siBytes[1];
                    cnv->charErrorBufferLength=1;
                    *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
                } else {
                    *target++ = siBytes[1];
                }
            }
            if(offsets!=nullptr) {
                /* set the last source character's index (sourceIndex points at sourceLimit now) */
                *offsets++=prevSourceIndex;
            }
        } else {
            /* target is full */
            cnv->charErrorBuffer[0] = siBytes[0];
            if (siLength == 2) {
                cnv->charErrorBuffer[1] = siBytes[1];
            }
            cnv->charErrorBufferLength=siLength;
            *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
        }
        prevLength=1; /* we switched into SBCS */
    }

    /* set the converter state back into UConverter */
    cnv->fromUChar32=c;
    cnv->fromUnicodeStatus=prevLength;

    /* write back the updated pointers */
    pArgs->source=source;
    pArgs->target=(char *)target;
    pArgs->offsets=offsets;
}

/*
 * This is another simple conversion function for internal use by other
 * conversion implementations.
 * It does not use the converter state nor call callbacks.
 * It does not handle the EBCDIC swaplfnl option (set in UConverter).
 * It handles conversion extensions but not GB 18030.
 *
 * It converts one single Unicode code point into codepage bytes, encoded
 * as one 32-bit value. The function returns the number of bytes in *pValue:
 * 1..4 the number of bytes in *pValue
 * 0    unassigned (*pValue undefined)
 * -1   illegal (currently not used, *pValue undefined)
 *
 * *pValue will contain the resulting bytes with the last byte in bits 7..0,
 * the second to last byte in bits 15..8, etc.
 * Currently, the function assumes but does not check that 0<=c<=0x10ffff.
 */
U_CFUNC int32_t
ucnv_MBCSFromUChar32(UConverterSharedData *sharedData,
                 UChar32 c, uint32_t *pValue,
                 UBool useFallback) {
    const int32_t *cx;
    const uint16_t *table;
#if 0
/* #if 0 because this is not currently used in ICU - reduce code, increase code coverage */
    const uint8_t *p;
#endif
    uint32_t stage2Entry;
    uint32_t value;
    int32_t length;

    /* BMP-only codepages are stored without stage 1 entries for supplementary code points */
    if(c<=0xffff || (sharedData->mbcs.unicodeMask&UCNV_HAS_SUPPLEMENTARY)) {
        table=sharedData->mbcs.fromUnicodeTable;

        /* convert the Unicode code point in c into codepage bytes (same as in _MBCSFromUnicodeWithOffsets) */
        if(sharedData->mbcs.outputType==MBCS_OUTPUT_1) {
            value=MBCS_SINGLE_RESULT_FROM_U(table, (uint16_t *)sharedData->mbcs.fromUnicodeBytes, c);
            /* is this code point assigned, or do we use fallbacks? */
            if(useFallback ? value>=0x800 : value>=0xc00) {
                *pValue=value&0xff;
                return 1;
            }
        } else /* outputType!=MBCS_OUTPUT_1 */ {
            stage2Entry=MBCS_STAGE_2_FROM_U(table, c);

            /* get the bytes and the length for the output */
            switch(sharedData->mbcs.outputType) {
            case MBCS_OUTPUT_2:
                value=MBCS_VALUE_2_FROM_STAGE_2(sharedData->mbcs.fromUnicodeBytes, stage2Entry, c);
                if(value<=0xff) {
                    length=1;
                } else {
                    length=2;
                }
                break;
#if 0
/* #if 0 because this is not currently used in ICU - reduce code, increase code coverage */
            case MBCS_OUTPUT_DBCS_ONLY:
                /* table with single-byte results, but only DBCS mappings used */
                value=MBCS_VALUE_2_FROM_STAGE_2(sharedData->mbcs.fromUnicodeBytes, stage2Entry, c);
                if(value<=0xff) {
                    /* no mapping or SBCS result, not taken for DBCS-only */
                    value=stage2Entry=0; /* stage2Entry=0 to reset roundtrip flags */
                    length=0;
                } else {
                    length=2;
                }
                break;
            case MBCS_OUTPUT_3:
                p=MBCS_POINTER_3_FROM_STAGE_2(sharedData->mbcs.fromUnicodeBytes, stage2Entry, c);
                value=((uint32_t)*p<<16)|((uint32_t)p[1]<<8)|p[2];
                if(value<=0xff) {
                    length=1;
                } else if(value<=0xffff) {
                    length=2;
                } else {
                    length=3;
                }
                break;
            case MBCS_OUTPUT_4:
                value=MBCS_VALUE_4_FROM_STAGE_2(sharedData->mbcs.fromUnicodeBytes, stage2Entry, c);
                if(value<=0xff) {
                    length=1;
                } else if(value<=0xffff) {
                    length=2;
                } else if(value<=0xffffff) {
                    length=3;
                } else {
                    length=4;
                }
                break;
            case MBCS_OUTPUT_3_EUC:
                value=MBCS_VALUE_2_FROM_STAGE_2(sharedData->mbcs.fromUnicodeBytes, stage2Entry, c);
                /* EUC 16-bit fixed-length representation */
                if(value<=0xff) {
                    length=1;
                } else if((value&0x8000)==0) {
                    value|=0x8e8000;
                    length=3;
                } else if((value&0x80)==0) {
                    value|=0x8f0080;
                    length=3;
                } else {
                    length=2;
                }
                break;
            case MBCS_OUTPUT_4_EUC:
                p=MBCS_POINTER_3_FROM_STAGE_2(sharedData->mbcs.fromUnicodeBytes, stage2Entry, c);
                value=((uint32_t)*p<<16)|((uint32_t)p[1]<<8)|p[2];
                /* EUC 16-bit fixed-length representation applied to the first two bytes */
                if(value<=0xff) {
                    length=1;
                } else if(value<=0xffff) {
                    length=2;
                } else if((value&0x800000)==0) {
                    value|=0x8e800000;
                    length=4;
                } else if((value&0x8000)==0) {
                    value|=0x8f008000;
                    length=4;
                } else {
                    length=3;
                }
                break;
#endif
            default:
                /* must not occur */
                return -1;
            }

            /* is this code point assigned, or do we use fallbacks? */
            if( MBCS_FROM_U_IS_ROUNDTRIP(stage2Entry, c) ||
                (FROM_U_USE_FALLBACK(useFallback, c) && value!=0)
            ) {
                /*
                 * We allow a 0 byte output if the "assigned" bit is set for this entry.
                 * There is no way with this data structure for fallback output
                 * to be a zero byte.
                 */
                /* assigned */
                *pValue=value;
                return length;
            }
        }
    }

    cx=sharedData->mbcs.extIndexes;
    if(cx!=nullptr) {
        length=ucnv_extSimpleMatchFromU(cx, c, pValue, useFallback);
        return length>=0 ? length : -length;  /* return abs(length); */
    }

    /* unassigned */
    return 0;
}


#if 0
/*
 * This function has been moved to ucnv2022.c for inlining.
 * This implementation is here only for documentation purposes
 */

/**
 * This version of ucnv_MBCSFromUChar32() is optimized for single-byte codepages.
 * It does not handle the EBCDIC swaplfnl option (set in UConverter).
 * It does not handle conversion extensions (_extFromU()).
 *
 * It returns the codepage byte for the code point, or -1 if it is unassigned.
 */
U_CFUNC int32_t
ucnv_MBCSSingleFromUChar32(UConverterSharedData *sharedData,
                       UChar32 c,
                       UBool useFallback) {
    const uint16_t *table;
    int32_t value;

    /* BMP-only codepages are stored without stage 1 entries for supplementary code points */
    if(c>=0x10000 && !(sharedData->mbcs.unicodeMask&UCNV_HAS_SUPPLEMENTARY)) {
        return -1;
    }

    /* convert the Unicode code point in c into codepage bytes (same as in _MBCSFromUnicodeWithOffsets) */
    table=sharedData->mbcs.fromUnicodeTable;

    /* get the byte for the output */
    value=MBCS_SINGLE_RESULT_FROM_U(table, (uint16_t *)sharedData->mbcs.fromUnicodeBytes, c);
    /* is this code point assigned, or do we use fallbacks? */
    if(useFallback ? value>=0x800 : value>=0xc00) {
        return value&0xff;
    } else {
        return -1;
    }
}
#endif

/* MBCS-from-UTF-8 conversion functions ------------------------------------- */

/* offsets for n-byte UTF-8 sequences that were calculated with ((lead<<6)+trail)<<6+trail... */
static const UChar32
utf8_offsets[5]={ 0, 0, 0x3080, 0xE2080, 0x3C82080 };

static void U_CALLCONV
ucnv_SBCSFromUTF8(UConverterFromUnicodeArgs *pFromUArgs,
                  UConverterToUnicodeArgs *pToUArgs,
                  UErrorCode *pErrorCode) {
    UConverter *utf8, *cnv;
    const uint8_t *source, *sourceLimit;
    uint8_t *target;
    int32_t targetCapacity;

    const uint16_t *table, *sbcsIndex;
    const uint16_t *results;

    int8_t oldToULength, toULength, toULimit;

    UChar32 c;
    uint8_t b, t1, t2;

    uint32_t asciiRoundtrips;
    uint16_t value, minValue = 0;
    UBool hasSupplementary;

    /* set up the local pointers */
    utf8=pToUArgs->converter;
    cnv=pFromUArgs->converter;
    source=(uint8_t *)pToUArgs->source;
    sourceLimit=(uint8_t *)pToUArgs->sourceLimit;
    target = reinterpret_cast<uint8_t*>(pFromUArgs->target);
    targetCapacity = static_cast<int32_t>(pFromUArgs->targetLimit - pFromUArgs->target);

    table=cnv->sharedData->mbcs.fromUnicodeTable;
    sbcsIndex=cnv->sharedData->mbcs.sbcsIndex;
    if((cnv->options&UCNV_OPTION_SWAP_LFNL)!=0) {
        results = reinterpret_cast<uint16_t*>(cnv->sharedData->mbcs.swapLFNLFromUnicodeBytes);
    } else {
        results=(uint16_t *)cnv->sharedData->mbcs.fromUnicodeBytes;
    }
    asciiRoundtrips=cnv->sharedData->mbcs.asciiRoundtrips;

    if(cnv->useFallback) {
        /* use all roundtrip and fallback results */
        minValue=0x800;
    } else {
        /* use only roundtrips and fallbacks from private-use characters */
        minValue=0xc00;
    }
    hasSupplementary = static_cast<UBool>(cnv->sharedData->mbcs.unicodeMask & UCNV_HAS_SUPPLEMENTARY);

    /* get the converter state from the UTF-8 UConverter */
    if(utf8->toULength > 0) {
        toULength=oldToULength=utf8->toULength;
        toULimit = static_cast<int8_t>(utf8->mode);
        c = static_cast<UChar32>(utf8->toUnicodeStatus);
    } else {
        toULength=oldToULength=toULimit=0;
        c = 0;
    }

    // The conversion loop checks source<sourceLimit only once per 1/2/3-byte character.
    // If the buffer ends with a truncated 2- or 3-byte sequence,
    // then we reduce the sourceLimit to before that,
    // and collect the remaining bytes after the conversion loop.
    {
        // Do not go back into the bytes that will be read for finishing a partial
        // sequence from the previous buffer.
        int32_t length = static_cast<int32_t>(sourceLimit - source) - (toULimit - oldToULength);
        if(length>0) {
            uint8_t b1=*(sourceLimit-1);
            if(U8_IS_SINGLE(b1)) {
                // common ASCII character
            } else if(U8_IS_TRAIL(b1) && length>=2) {
                uint8_t b2=*(sourceLimit-2);
                if(0xe0<=b2 && b2<0xf0 && U8_IS_VALID_LEAD3_AND_T1(b2, b1)) {
                    // truncated 3-byte sequence
                    sourceLimit-=2;
                }
            } else if(0xc2<=b1 && b1<0xf0) {
                // truncated 2- or 3-byte sequence
                --sourceLimit;
            }
        }
    }

    if(c!=0 && targetCapacity>0) {
        utf8->toUnicodeStatus=0;
        utf8->toULength=0;
        goto moreBytes;
        /*
         * Note: We could avoid the goto by duplicating some of the moreBytes
         * code, but only up to the point of collecting a complete UTF-8
         * sequence; then recurse for the toUBytes[toULength]
         * and then continue with normal conversion.
         *
         * If so, move this code to just after initializing the minimum
         * set of local variables for reading the UTF-8 input
         * (utf8, source, target, limits but not cnv, table, minValue, etc.).
         *
         * Potential advantages:
         * - avoid the goto
         * - oldToULength could become a local variable in just those code blocks
         *   that deal with buffer boundaries
         * - possibly faster if the goto prevents some compiler optimizations
         *   (this would need measuring to confirm)
         * Disadvantage:
         * - code duplication
         */
    }

    /* conversion loop */
    while(source<sourceLimit) {
        if(targetCapacity>0) {
            b=*source++;
            if(U8_IS_SINGLE(b)) {
                /* convert ASCII */
                if(IS_ASCII_ROUNDTRIP(b, asciiRoundtrips)) {
                    *target++ = b;
                    --targetCapacity;
                    continue;
                } else {
                    c=b;
                    value=SBCS_RESULT_FROM_UTF8(sbcsIndex, results, 0, c);
                }
            } else {
                if(b<0xe0) {
                    if( /* handle U+0080..U+07FF inline */
                        b>=0xc2 &&
                        (t1 = static_cast<uint8_t>(*source - 0x80)) <= 0x3f
                    ) {
                        c=b&0x1f;
                        ++source;
                        value=SBCS_RESULT_FROM_UTF8(sbcsIndex, results, c, t1);
                        if(value>=minValue) {
                            *target++ = static_cast<uint8_t>(value);
                            --targetCapacity;
                            continue;
                        } else {
                            c=(c<<6)|t1;
                        }
                    } else {
                        c=-1;
                    }
                } else if(b==0xe0) {
                    if( /* handle U+0800..U+0FFF inline */
                        (t1 = static_cast<uint8_t>(source[0] - 0x80)) <= 0x3f && t1 >= 0x20 &&
                        (t2 = static_cast<uint8_t>(source[1] - 0x80)) <= 0x3f
                    ) {
                        c=t1;
                        source+=2;
                        value=SBCS_RESULT_FROM_UTF8(sbcsIndex, results, c, t2);
                        if(value>=minValue) {
                            *target++ = static_cast<uint8_t>(value);
                            --targetCapacity;
                            continue;
                        } else {
                            c=(c<<6)|t2;
                        }
                    } else {
                        c=-1;
                    }
                } else {
                    c=-1;
                }

                if(c<0) {
                    /* handle "complicated" and error cases, and continuing partial characters */
                    oldToULength=0;
                    toULength=1;
                    toULimit=U8_COUNT_BYTES_NON_ASCII(b);
                    c=b;
moreBytes:
                    while(toULength<toULimit) {
                        /*
                         * The sourceLimit may have been adjusted before the conversion loop
                         * to stop before a truncated sequence.
                         * Here we need to use the real limit in case we have two truncated
                         * sequences at the end.
                         * See ticket #7492.
                         */
                        if(source<(uint8_t *)pToUArgs->sourceLimit) {
                            b=*source;
                            if(icu::UTF8::isValidTrail(c, b, toULength, toULimit)) {
                                ++source;
                                ++toULength;
                                c=(c<<6)+b;
                            } else {
                                break; /* sequence too short, stop with toULength<toULimit */
                            }
                        } else {
                            /* store the partial UTF-8 character, compatible with the regular UTF-8 converter */
                            source-=(toULength-oldToULength);
                            while(oldToULength<toULength) {
                                utf8->toUBytes[oldToULength++]=*source++;
                            }
                            utf8->toUnicodeStatus=c;
                            utf8->toULength=toULength;
                            utf8->mode=toULimit;
                            pToUArgs->source=(char *)source;
                            pFromUArgs->target = reinterpret_cast<char*>(target);
                            return;
                        }
                    }

                    if(toULength==toULimit) {
                        c-=utf8_offsets[toULength];
                        if(toULength<=3) {  /* BMP */
                            value=MBCS_SINGLE_RESULT_FROM_U(table, results, c);
                        } else {
                            /* supplementary code point */
                            if(!hasSupplementary) {
                                /* BMP-only codepages are stored without stage 1 entries for supplementary code points */
                                value=0;
                            } else {
                                value=MBCS_SINGLE_RESULT_FROM_U(table, results, c);
                            }
                        }
                    } else {
                        /* error handling: illegal UTF-8 byte sequence */
                        source-=(toULength-oldToULength);
                        while(oldToULength<toULength) {
                            utf8->toUBytes[oldToULength++]=*source++;
                        }
                        utf8->toULength=toULength;
                        pToUArgs->source=(char *)source;
                        pFromUArgs->target = reinterpret_cast<char*>(target);
                        *pErrorCode=U_ILLEGAL_CHAR_FOUND;
                        return;
                    }
                }
            }

            if(value>=minValue) {
                /* output the mapping for c */
                *target++ = static_cast<uint8_t>(value);
                --targetCapacity;
            } else {
                /* value<minValue means c is unassigned (unmappable) */
                /*
                 * Try an extension mapping.
                 * Pass in no source because we don't have UTF-16 input.
                 * If we have a partial match on c, we will return and revert
                 * to UTF-8->UTF-16->charset conversion.
                 */
                static const char16_t nul=0;
                const char16_t *noSource=&nul;
                c=_extFromU(cnv, cnv->sharedData,
                            c, &noSource, noSource,
                            &target, target+targetCapacity,
                            nullptr, -1,
                            pFromUArgs->flush,
                            pErrorCode);

                if(U_FAILURE(*pErrorCode)) {
                    /* not mappable or buffer overflow */
                    cnv->fromUChar32=c;
                    break;
                } else if(cnv->preFromUFirstCP>=0) {
                    /*
                     * Partial match, return and revert to pivoting.
                     * In normal from-UTF-16 conversion, we would just continue
                     * but then exit the loop because the extension match would
                     * have consumed the source.
                     */
                    *pErrorCode=U_USING_DEFAULT_WARNING;
                    break;
                } else {
                    /* a mapping was written to the target, continue */

                    /* recalculate the targetCapacity after an extension mapping */
                    targetCapacity = static_cast<int32_t>(pFromUArgs->targetLimit - reinterpret_cast<char*>(target));
                }
            }
        } else {
            /* target is full */
            *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
            break;
        }
    }

    /*
     * The sourceLimit may have been adjusted before the conversion loop
     * to stop before a truncated sequence.
     * If so, then collect the truncated sequence now.
     */
    if(U_SUCCESS(*pErrorCode) &&
            cnv->preFromUFirstCP<0 &&
            source<(sourceLimit=(uint8_t *)pToUArgs->sourceLimit)) {
        c=utf8->toUBytes[0]=b=*source++;
        toULength=1;
        toULimit=U8_COUNT_BYTES(b);
        while(source<sourceLimit) {
            utf8->toUBytes[toULength++]=b=*source++;
            c=(c<<6)+b;
        }
        utf8->toUnicodeStatus=c;
        utf8->toULength=toULength;
        utf8->mode=toULimit;
    }

    /* write back the updated pointers */
    pToUArgs->source=(char *)source;
    pFromUArgs->target = reinterpret_cast<char*>(target);
}

static void U_CALLCONV
ucnv_DBCSFromUTF8(UConverterFromUnicodeArgs *pFromUArgs,
                  UConverterToUnicodeArgs *pToUArgs,
                  UErrorCode *pErrorCode) {
    UConverter *utf8, *cnv;
    const uint8_t *source, *sourceLimit;
    uint8_t *target;
    int32_t targetCapacity;

    const uint16_t *table, *mbcsIndex;
    const uint16_t *results;

    int8_t oldToULength, toULength, toULimit;

    UChar32 c;
    uint8_t b, t1, t2;

    uint32_t stage2Entry;
    uint32_t asciiRoundtrips;
    uint16_t value = 0;
    UBool hasSupplementary;

    /* set up the local pointers */
    utf8=pToUArgs->converter;
    cnv=pFromUArgs->converter;
    source=(uint8_t *)pToUArgs->source;
    sourceLimit=(uint8_t *)pToUArgs->sourceLimit;
    target = reinterpret_cast<uint8_t*>(pFromUArgs->target);
    targetCapacity = static_cast<int32_t>(pFromUArgs->targetLimit - pFromUArgs->target);

    table=cnv->sharedData->mbcs.fromUnicodeTable;
    mbcsIndex=cnv->sharedData->mbcs.mbcsIndex;
    if((cnv->options&UCNV_OPTION_SWAP_LFNL)!=0) {
        results = reinterpret_cast<uint16_t*>(cnv->sharedData->mbcs.swapLFNLFromUnicodeBytes);
    } else {
        results=(uint16_t *)cnv->sharedData->mbcs.fromUnicodeBytes;
    }
    asciiRoundtrips=cnv->sharedData->mbcs.asciiRoundtrips;

    hasSupplementary = static_cast<UBool>(cnv->sharedData->mbcs.unicodeMask & UCNV_HAS_SUPPLEMENTARY);

    /* get the converter state from the UTF-8 UConverter */
    if(utf8->toULength > 0) {
        toULength=oldToULength=utf8->toULength;
        toULimit = static_cast<int8_t>(utf8->mode);
        c = static_cast<UChar32>(utf8->toUnicodeStatus);
    } else {
        toULength=oldToULength=toULimit=0;
        c = 0;
    }

    // The conversion loop checks source<sourceLimit only once per 1/2/3-byte character.
    // If the buffer ends with a truncated 2- or 3-byte sequence,
    // then we reduce the sourceLimit to before that,
    // and collect the remaining bytes after the conversion loop.
    {
        // Do not go back into the bytes that will be read for finishing a partial
        // sequence from the previous buffer.
        int32_t length = static_cast<int32_t>(sourceLimit - source) - (toULimit - oldToULength);
        if(length>0) {
            uint8_t b1=*(sourceLimit-1);
            if(U8_IS_SINGLE(b1)) {
                // common ASCII character
            } else if(U8_IS_TRAIL(b1) && length>=2) {
                uint8_t b2=*(sourceLimit-2);
                if(0xe0<=b2 && b2<0xf0 && U8_IS_VALID_LEAD3_AND_T1(b2, b1)) {
                    // truncated 3-byte sequence
                    sourceLimit-=2;
                }
            } else if(0xc2<=b1 && b1<0xf0) {
                // truncated 2- or 3-byte sequence
                --sourceLimit;
            }
        }
    }

    if(c!=0 && targetCapacity>0) {
        utf8->toUnicodeStatus=0;
        utf8->toULength=0;
        goto moreBytes;
        /* See note in ucnv_SBCSFromUTF8() about this goto. */
    }

    /* conversion loop */
    while(source<sourceLimit) {
        if(targetCapacity>0) {
            b=*source++;
            if(U8_IS_SINGLE(b)) {
                /* convert ASCII */
                if(IS_ASCII_ROUNDTRIP(b, asciiRoundtrips)) {
                    *target++=b;
                    --targetCapacity;
                    continue;
                } else {
                    value=DBCS_RESULT_FROM_UTF8(mbcsIndex, results, 0, b);
                    if(value==0) {
                        c=b;
                        goto unassigned;
                    }
                }
            } else {
                if(b>=0xe0) {
                    if( /* handle U+0800..U+D7FF inline */
                        b<=0xed &&  // do not assume maxFastUChar>0xd7ff
                        U8_IS_VALID_LEAD3_AND_T1(b, t1=source[0]) &&
                        (t2 = static_cast<uint8_t>(source[1] - 0x80)) <= 0x3f
                    ) {
                        c=((b&0xf)<<6)|(t1&0x3f);
                        source+=2;
                        value=DBCS_RESULT_FROM_UTF8(mbcsIndex, results, c, t2);
                        if(value==0) {
                            c=(c<<6)|t2;
                            goto unassigned;
                        }
                    } else {
                        c=-1;
                    }
                } else {
                    if( /* handle U+0080..U+07FF inline */
                        b>=0xc2 &&
                        (t1 = static_cast<uint8_t>(*source - 0x80)) <= 0x3f
                    ) {
                        c=b&0x1f;
                        ++source;
                        value=DBCS_RESULT_FROM_UTF8(mbcsIndex, results, c, t1);
                        if(value==0) {
                            c=(c<<6)|t1;
                            goto unassigned;
                        }
                    } else {
                        c=-1;
                    }
                }

                if(c<0) {
                    /* handle "complicated" and error cases, and continuing partial characters */
                    oldToULength=0;
                    toULength=1;
                    toULimit=U8_COUNT_BYTES_NON_ASCII(b);
                    c=b;
moreBytes:
                    while(toULength<toULimit) {
                        /*
                         * The sourceLimit may have been adjusted before the conversion loop
                         * to stop before a truncated sequence.
                         * Here we need to use the real limit in case we have two truncated
                         * sequences at the end.
                         * See ticket #7492.
                         */
                        if(source<(uint8_t *)pToUArgs->sourceLimit) {
                            b=*source;
                            if(icu::UTF8::isValidTrail(c, b, toULength, toULimit)) {
                                ++source;
                                ++toULength;
                                c=(c<<6)+b;
                            } else {
                                break; /* sequence too short, stop with toULength<toULimit */
                            }
                        } else {
                            /* store the partial UTF-8 character, compatible with the regular UTF-8 converter */
                            source-=(toULength-oldToULength);
                            while(oldToULength<toULength) {
                                utf8->toUBytes[oldToULength++]=*source++;
                            }
                            utf8->toUnicodeStatus=c;
                            utf8->toULength=toULength;
                            utf8->mode=toULimit;
                            pToUArgs->source=(char *)source;
                            pFromUArgs->target = reinterpret_cast<char*>(target);
                            return;
                        }
                    }

                    if(toULength==toULimit) {
                        c-=utf8_offsets[toULength];
                        if(toULength<=3) {  /* BMP */
                            stage2Entry=MBCS_STAGE_2_FROM_U(table, c);
                        } else {
                            /* supplementary code point */
                            if(!hasSupplementary) {
                                /* BMP-only codepages are stored without stage 1 entries for supplementary code points */
                                stage2Entry=0;
                            } else {
                                stage2Entry=MBCS_STAGE_2_FROM_U(table, c);
                            }
                        }
                    } else {
                        /* error handling: illegal UTF-8 byte sequence */
                        source-=(toULength-oldToULength);
                        while(oldToULength<toULength) {
                            utf8->toUBytes[oldToULength++]=*source++;
                        }
                        utf8->toULength=toULength;
                        pToUArgs->source=(char *)source;
                        pFromUArgs->target = reinterpret_cast<char*>(target);
                        *pErrorCode=U_ILLEGAL_CHAR_FOUND;
                        return;
                    }

                    /* get the bytes and the length for the output */
                    /* MBCS_OUTPUT_2 */
                    value=MBCS_VALUE_2_FROM_STAGE_2(results, stage2Entry, c);

                    /* is this code point assigned, or do we use fallbacks? */
                    if(!(MBCS_FROM_U_IS_ROUNDTRIP(stage2Entry, c) ||
                         (UCNV_FROM_U_USE_FALLBACK(cnv, c) && value!=0))
                    ) {
                        goto unassigned;
                    }
                }
            }

            /* write the output character bytes from value and length */
            /* from the first if in the loop we know that targetCapacity>0 */
            if(value<=0xff) {
                /* this is easy because we know that there is enough space */
                *target++ = static_cast<uint8_t>(value);
                --targetCapacity;
            } else /* length==2 */ {
                *target++ = static_cast<uint8_t>(value >> 8);
                if(2<=targetCapacity) {
                    *target++ = static_cast<uint8_t>(value);
                    targetCapacity-=2;
                } else {
                    cnv->charErrorBuffer[0] = static_cast<char>(value);
                    cnv->charErrorBufferLength=1;

                    /* target overflow */
                    *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
                    break;
                }
            }
            continue;

unassigned:
            {
                /*
                 * Try an extension mapping.
                 * Pass in no source because we don't have UTF-16 input.
                 * If we have a partial match on c, we will return and revert
                 * to UTF-8->UTF-16->charset conversion.
                 */
                static const char16_t nul=0;
                const char16_t *noSource=&nul;
                c=_extFromU(cnv, cnv->sharedData,
                            c, &noSource, noSource,
                            &target, target+targetCapacity,
                            nullptr, -1,
                            pFromUArgs->flush,
                            pErrorCode);

                if(U_FAILURE(*pErrorCode)) {
                    /* not mappable or buffer overflow */
                    cnv->fromUChar32=c;
                    break;
                } else if(cnv->preFromUFirstCP>=0) {
                    /*
                     * Partial match, return and revert to pivoting.
                     * In normal from-UTF-16 conversion, we would just continue
                     * but then exit the loop because the extension match would
                     * have consumed the source.
                     */
                    *pErrorCode=U_USING_DEFAULT_WARNING;
                    break;
                } else {
                    /* a mapping was written to the target, continue */

                    /* recalculate the targetCapacity after an extension mapping */
                    targetCapacity = static_cast<int32_t>(pFromUArgs->targetLimit - reinterpret_cast<char*>(target));
                    continue;
                }
            }
        } else {
            /* target is full */
            *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
            break;
        }
    }

    /*
     * The sourceLimit may have been adjusted before the conversion loop
     * to stop before a truncated sequence.
     * If so, then collect the truncated sequence now.
     */
    if(U_SUCCESS(*pErrorCode) &&
            cnv->preFromUFirstCP<0 &&
            source<(sourceLimit=(uint8_t *)pToUArgs->sourceLimit)) {
        c=utf8->toUBytes[0]=b=*source++;
        toULength=1;
        toULimit=U8_COUNT_BYTES(b);
        while(source<sourceLimit) {
            utf8->toUBytes[toULength++]=b=*source++;
            c=(c<<6)+b;
        }
        utf8->toUnicodeStatus=c;
        utf8->toULength=toULength;
        utf8->mode=toULimit;
    }

    /* write back the updated pointers */
    pToUArgs->source=(char *)source;
    pFromUArgs->target = reinterpret_cast<char*>(target);
}

/* miscellaneous ------------------------------------------------------------ */

static void U_CALLCONV
ucnv_MBCSGetStarters(const UConverter* cnv,
                 UBool starters[256],
                 UErrorCode *) {
    const int32_t *state0;
    int i;

    state0=cnv->sharedData->mbcs.stateTable[cnv->sharedData->mbcs.dbcsOnlyState];
    for(i=0; i<256; ++i) {
        /* all bytes that cause a state transition from state 0 are lead bytes */
        starters[i] = static_cast<UBool>(MBCS_ENTRY_IS_TRANSITION(state0[i]));
    }
}

/*
 * This is an internal function that allows other converter implementations
 * to check whether a byte is a lead byte.
 */
U_CFUNC UBool
ucnv_MBCSIsLeadByte(UConverterSharedData *sharedData, char byte) {
    return MBCS_ENTRY_IS_TRANSITION(sharedData->mbcs.stateTable[0][(uint8_t)byte]);
}

static void U_CALLCONV
ucnv_MBCSWriteSub(UConverterFromUnicodeArgs *pArgs,
              int32_t offsetIndex,
              UErrorCode *pErrorCode) {
    UConverter *cnv=pArgs->converter;
    char *p, *subchar;
    char buffer[4];
    int32_t length;

    /* first, select between subChar and subChar1 */
    if( cnv->subChar1!=0 &&
        (cnv->sharedData->mbcs.extIndexes!=nullptr ?
            cnv->useSubChar1 :
            (cnv->invalidUCharBuffer[0]<=0xff))
    ) {
        /* select subChar1 if it is set (not 0) and the unmappable Unicode code point is up to U+00ff (IBM MBCS behavior) */
        subchar = reinterpret_cast<char*>(&cnv->subChar1);
        length=1;
    } else {
        /* select subChar in all other cases */
        subchar = reinterpret_cast<char*>(cnv->subChars);
        length=cnv->subCharLen;
    }

    /* reset the selector for the next code point */
    cnv->useSubChar1=false;

    if (cnv->sharedData->mbcs.outputType == MBCS_OUTPUT_2_SISO) {
        p=buffer;

        /* fromUnicodeStatus contains prevLength */
        switch(length) {
        case 1:
            if(cnv->fromUnicodeStatus==2) {
                /* DBCS mode and SBCS sub char: change to SBCS */
                cnv->fromUnicodeStatus=1;
                *p++=UCNV_SI;
            }
            *p++=subchar[0];
            break;
        case 2:
            if(cnv->fromUnicodeStatus<=1) {
                /* SBCS mode and DBCS sub char: change to DBCS */
                cnv->fromUnicodeStatus=2;
                *p++=UCNV_SO;
            }
            *p++=subchar[0];
            *p++=subchar[1];
            break;
        default:
            *pErrorCode=U_ILLEGAL_ARGUMENT_ERROR;
            return;
        }
        subchar=buffer;
        length = static_cast<int32_t>(p - buffer);
    }

    ucnv_cbFromUWriteBytes(pArgs, subchar, length, offsetIndex, pErrorCode);
}

U_CFUNC UConverterType
ucnv_MBCSGetType(const UConverter* converter) {
    /* SBCS, DBCS, and EBCDIC_STATEFUL are replaced by MBCS, but here we cheat a little */
    if(converter->sharedData->mbcs.countStates==1) {
        return (UConverterType)UCNV_SBCS;
    } else if((converter->sharedData->mbcs.outputType&0xff)==MBCS_OUTPUT_2_SISO) {
        return (UConverterType)UCNV_EBCDIC_STATEFUL;
    } else if(converter->sharedData->staticData->minBytesPerChar==2 && converter->sharedData->staticData->maxBytesPerChar==2) {
        return (UConverterType)UCNV_DBCS;
    }
    return (UConverterType)UCNV_MBCS;
}

#endif /* #if !UCONFIG_NO_LEGACY_CONVERSION */
