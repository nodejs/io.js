// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*   Copyright (C) 2001-2014, International Business Machines
*   Corporation and others.  All Rights Reserved.
*******************************************************************************
*   file name:  bocsu.cpp
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   Author: Markus W. Scherer
*
*   Modification history:
*   05/18/2001  weiv    Made into separate module
*/


#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

#include "unicode/bytestream.h"
#include "unicode/utf16.h"
#include "bocsu.h"

/*
 * encode one difference value -0x10ffff..+0x10ffff in 1..4 bytes,
 * preserving lexical order
 */
static uint8_t *
u_writeDiff(int32_t diff, uint8_t *p) {
    if(diff>=SLOPE_REACH_NEG_1) {
        if(diff<=SLOPE_REACH_POS_1) {
            *p++ = static_cast<uint8_t>(SLOPE_MIDDLE + diff);
        } else if(diff<=SLOPE_REACH_POS_2) {
            *p++ = static_cast<uint8_t>(SLOPE_START_POS_2 + (diff / SLOPE_TAIL_COUNT));
            *p++ = static_cast<uint8_t>(SLOPE_MIN + diff % SLOPE_TAIL_COUNT);
        } else if(diff<=SLOPE_REACH_POS_3) {
            p[2] = static_cast<uint8_t>(SLOPE_MIN + diff % SLOPE_TAIL_COUNT);
            diff/=SLOPE_TAIL_COUNT;
            p[1] = static_cast<uint8_t>(SLOPE_MIN + diff % SLOPE_TAIL_COUNT);
            *p = static_cast<uint8_t>(SLOPE_START_POS_3 + (diff / SLOPE_TAIL_COUNT));
            p+=3;
        } else {
            p[3] = static_cast<uint8_t>(SLOPE_MIN + diff % SLOPE_TAIL_COUNT);
            diff/=SLOPE_TAIL_COUNT;
            p[2] = static_cast<uint8_t>(SLOPE_MIN + diff % SLOPE_TAIL_COUNT);
            diff/=SLOPE_TAIL_COUNT;
            p[1] = static_cast<uint8_t>(SLOPE_MIN + diff % SLOPE_TAIL_COUNT);
            *p=SLOPE_MAX;
            p+=4;
        }
    } else {
        int32_t m;

        if(diff>=SLOPE_REACH_NEG_2) {
            NEGDIVMOD(diff, SLOPE_TAIL_COUNT, m);
            *p++ = static_cast<uint8_t>(SLOPE_START_NEG_2 + diff);
            *p++ = static_cast<uint8_t>(SLOPE_MIN + m);
        } else if(diff>=SLOPE_REACH_NEG_3) {
            NEGDIVMOD(diff, SLOPE_TAIL_COUNT, m);
            p[2] = static_cast<uint8_t>(SLOPE_MIN + m);
            NEGDIVMOD(diff, SLOPE_TAIL_COUNT, m);
            p[1] = static_cast<uint8_t>(SLOPE_MIN + m);
            *p = static_cast<uint8_t>(SLOPE_START_NEG_3 + diff);
            p+=3;
        } else {
            NEGDIVMOD(diff, SLOPE_TAIL_COUNT, m);
            p[3] = static_cast<uint8_t>(SLOPE_MIN + m);
            NEGDIVMOD(diff, SLOPE_TAIL_COUNT, m);
            p[2] = static_cast<uint8_t>(SLOPE_MIN + m);
            NEGDIVMOD(diff, SLOPE_TAIL_COUNT, m);
            p[1] = static_cast<uint8_t>(SLOPE_MIN + m);
            *p=SLOPE_MIN;
            p+=4;
        }
    }
    return p;
}

/*
 * Encode the code points of a string as
 * a sequence of byte-encoded differences (slope detection),
 * preserving lexical order.
 *
 * Optimize the difference-taking for runs of Unicode text within
 * small scripts:
 *
 * Most small scripts are allocated within aligned 128-blocks of Unicode
 * code points. Lexical order is preserved if "prev" is always moved
 * into the middle of such a block.
 *
 * Additionally, "prev" is moved from anywhere in the Unihan
 * area into the middle of that area.
 * Note that the identical-level run in a sort key is generated from
 * NFD text - there are never Hangul characters included.
 */
U_CFUNC UChar32
u_writeIdenticalLevelRun(UChar32 prev, const char16_t *s, int32_t length, icu::ByteSink &sink) {
    char scratch[64];
    int32_t capacity;

    int32_t i=0;
    while(i<length) {
        char *buffer=sink.GetAppendBuffer(1, length*2, scratch, (int32_t)sizeof(scratch), &capacity);
        uint8_t *p;
        // We must have capacity>=SLOPE_MAX_BYTES in case u_writeDiff() writes that much,
        // but we do not want to force the sink.GetAppendBuffer() to allocate
        // for a large min_capacity because we might actually only write one byte.
        if(capacity<16) {
            buffer=scratch;
            capacity=(int32_t)sizeof(scratch);
        }
        p=reinterpret_cast<uint8_t *>(buffer);
        uint8_t *lastSafe=p+capacity-SLOPE_MAX_BYTES;
        while(i<length && p<=lastSafe) {
            if(prev<0x4e00 || prev>=0xa000) {
                prev=(prev&~0x7f)-SLOPE_REACH_NEG_1;
            } else {
                /*
                 * Unihan U+4e00..U+9fa5:
                 * double-bytes down from the upper end
                 */
                prev=0x9fff-SLOPE_REACH_POS_2;
            }

            UChar32 c;
            U16_NEXT(s, i, length, c);
            if(c==0xfffe) {
                *p++=2;  // merge separator
                prev=0;
            } else {
                p=u_writeDiff(c-prev, p);
                prev=c;
            }
        }
        sink.Append(buffer, (int32_t)(p-reinterpret_cast<uint8_t *>(buffer)));
    }
    return prev;
}

#endif /* #if !UCONFIG_NO_COLLATION */
