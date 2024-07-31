// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*
*   Copyright (C) 2000-2003, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*
* File writejava.c
*
* Modification History:
*
*   Date        Name        Description
*   01/11/02    Ram        Creation.
*******************************************************************************
*/
#include <stdbool.h>
#include "rle.h"
/**
 * The ESCAPE character is used during run-length encoding.  It signals
 * a run of identical chars.
 */
static const uint16_t ESCAPE = 0xA5A5;

/**
 * The ESCAPE_BYTE character is used during run-length encoding.  It signals
 * a run of identical bytes.
 */
static const uint8_t ESCAPE_BYTE = (uint8_t)0xA5;

/**
 * Append a byte to the given StringBuffer, packing two bytes into each
 * character.  The state parameter maintains intermediary data between
 * calls.
 * @param state A two-element array, with state[0] == 0 if this is the
 * first byte of a pair, or state[0] != 0 if this is the second byte
 * of a pair, in which case state[1] is the first byte.
 */
static uint16_t*
appendEncodedByte(uint16_t* buffer, uint16_t* buffLimit, uint8_t value, uint8_t state[],UErrorCode* status) {
    if(!status || U_FAILURE(*status)){
        return NULL;
    }
    if (state[0] != 0) {
        uint16_t c = (uint16_t) ((state[1] << 8) | (((int32_t) value) & 0xFF));
        if(buffer < buffLimit){
            *buffer++ = c;
        }else{
            *status = U_BUFFER_OVERFLOW_ERROR;
        }
        state[0] = 0;
        return buffer;
    }
    else {
        state[0] = 1;
        state[1] = value;
        return buffer;
    }
}
/**
 * Encode a run, possibly a degenerate run (of < 4 values).
 * @param length The length of the run; must be > 0 && <= 0xFF.
 */
static uint16_t*
encodeRunByte(uint16_t* buffer,uint16_t* bufLimit, uint8_t value, int32_t length, uint8_t state[], UErrorCode* status) {
    if(!status || U_FAILURE(*status)){
        return NULL;
    }
    if (length < 4) {
        int32_t j=0;
        for (; j<length; ++j) {
            if (value == ESCAPE_BYTE) {
                buffer = appendEncodedByte(buffer,bufLimit, ESCAPE_BYTE, state,status);
            }
            buffer = appendEncodedByte(buffer,bufLimit, value, state, status);
        }
    }
    else {
        if (length == ESCAPE_BYTE) {
            if (value == ESCAPE_BYTE){
               buffer =  appendEncodedByte(buffer, bufLimit,ESCAPE_BYTE, state,status);
            }
            buffer = appendEncodedByte(buffer,bufLimit, value, state, status);
            --length;
        }
        buffer = appendEncodedByte(buffer,bufLimit, ESCAPE_BYTE, state,status);
        buffer = appendEncodedByte(buffer,bufLimit, (char)length, state, status);
        buffer = appendEncodedByte(buffer,bufLimit, value, state, status); /* Don't need to escape this value*/
    }
    return buffer;
}

#define APPEND( buffer, bufLimit, value, status) UPRV_BLOCK_MACRO_BEGIN { \
    if(buffer<bufLimit){                    \
        *buffer++=(value);                  \
    }else{                                  \
        *status = U_BUFFER_OVERFLOW_ERROR;  \
    }                                       \
} UPRV_BLOCK_MACRO_END

/**
 * Encode a run, possibly a degenerate run (of < 4 values).
 * @param length The length of the run; must be > 0 && <= 0xFFFF.
 */
static uint16_t*
encodeRunShort(uint16_t* buffer,uint16_t* bufLimit, uint16_t value, int32_t length,UErrorCode* status) {
    if (length < 4) {
        int j=0;
        for (; j<length; ++j) {
            if (value == (int32_t) ESCAPE){
                APPEND(buffer,bufLimit,ESCAPE, status);

            }
            APPEND(buffer,bufLimit,value,status);
        }
    }
    else {
        if (length == (int32_t) ESCAPE) {
            if (value == (int32_t) ESCAPE){
                APPEND(buffer,bufLimit,ESCAPE,status);

            }
            APPEND(buffer,bufLimit,value,status);
            --length;
        }
        APPEND(buffer,bufLimit,ESCAPE,status);
        APPEND(buffer,bufLimit,(uint16_t) length,status);
        APPEND(buffer,bufLimit,(uint16_t)value, status); /* Don't need to escape this value */
    }
    return buffer;
}

/**
 * Construct a string representing a char array.  Use run-length encoding.
 * A character represents itself, unless it is the ESCAPE character.  Then
 * the following notations are possible:
 *   ESCAPE ESCAPE   ESCAPE literal
 *   ESCAPE n c      n instances of character c
 * Since an encoded run occupies 3 characters, we only encode runs of 4 or
 * more characters.  Thus we have n > 0 and n != ESCAPE and n <= 0xFFFF.
 * If we encounter a run where n == ESCAPE, we represent this as:
 *   c ESCAPE n-1 c
 * The ESCAPE value is chosen so as not to collide with commonly
 * seen values.
 */
int32_t
usArrayToRLEString(const uint16_t* src,int32_t srcLen,uint16_t* buffer, int32_t bufLen,UErrorCode* status) {
    uint16_t* bufLimit =  buffer+bufLen;
    uint16_t* saveBuffer = buffer;
    if(buffer < bufLimit){
        *buffer++ =  (uint16_t)(srcLen>>16);
        if(buffer<bufLimit){
            uint16_t runValue = src[0];
            int32_t runLength = 1;
            int i=1;
            *buffer++ = (uint16_t) srcLen;

            for (; i<srcLen; ++i) {
                uint16_t s = src[i];
                if (s == runValue && runLength < 0xFFFF){
                    ++runLength;
                }else {
                    buffer = encodeRunShort(buffer,bufLimit, (uint16_t)runValue, runLength,status);
                    runValue = s;
                    runLength = 1;
                }
            }
            buffer= encodeRunShort(buffer,bufLimit,(uint16_t)runValue, runLength,status);
        }else{
            *status = U_BUFFER_OVERFLOW_ERROR;
        }
    }else{
        *status = U_BUFFER_OVERFLOW_ERROR;
    }
    return (int32_t)(buffer - saveBuffer);
}

/**
 * Construct a string representing a byte array.  Use run-length encoding.
 * Two bytes are packed into a single char, with a single extra zero byte at
 * the end if needed.  A byte represents itself, unless it is the
 * ESCAPE_BYTE.  Then the following notations are possible:
 *   ESCAPE_BYTE ESCAPE_BYTE   ESCAPE_BYTE literal
 *   ESCAPE_BYTE n b           n instances of byte b
 * Since an encoded run occupies 3 bytes, we only encode runs of 4 or
 * more bytes.  Thus we have n > 0 and n != ESCAPE_BYTE and n <= 0xFF.
 * If we encounter a run where n == ESCAPE_BYTE, we represent this as:
 *   b ESCAPE_BYTE n-1 b
 * The ESCAPE_BYTE value is chosen so as not to collide with commonly
 * seen values.
 */
int32_t
byteArrayToRLEString(const uint8_t* src,int32_t srcLen, uint16_t* buffer,int32_t bufLen, UErrorCode* status) {
    const uint16_t* saveBuf = buffer;
    uint16_t* bufLimit =  buffer+bufLen;
    if(buffer < bufLimit){
        *buffer++ = ((uint16_t) (srcLen >> 16));

        if(buffer<bufLimit){
            uint8_t runValue = src[0];
            int runLength = 1;
            uint8_t state[2]= {0};
            int i=1;
            *buffer++=((uint16_t) srcLen);
            for (; i<srcLen; ++i) {
                uint8_t b = src[i];
                if (b == runValue && runLength < 0xFF){
                    ++runLength;
                }
                else {
                    buffer = encodeRunByte(buffer, bufLimit,runValue, runLength, state,status);
                    runValue = b;
                    runLength = 1;
                }
            }
            buffer = encodeRunByte(buffer,bufLimit, runValue, runLength, state, status);

            /* We must save the final byte, if there is one, by padding
             * an extra zero.
             */
            if (state[0] != 0) {
                buffer = appendEncodedByte(buffer,bufLimit, 0, state ,status);
            }
        }else{
            *status = U_BUFFER_OVERFLOW_ERROR;
        }
    }else{
        *status = U_BUFFER_OVERFLOW_ERROR;
    }
    return (int32_t) (buffer - saveBuf);
}


/**
 * Construct an array of shorts from a run-length encoded string.
 */
int32_t
rleStringToUCharArray(uint16_t* src, int32_t srcLen, uint16_t* target, int32_t tgtLen, UErrorCode* status) {
    int32_t length = 0;
    int32_t ai = 0;
    int i=2;

    if(!status || U_FAILURE(*status)){
        return 0;
    }
    /* the source is null terminated */
    if(srcLen == -1){
        srcLen = u_strlen(src);
    }
    if(srcLen <= 2){
        return 2;
    }
    length = (((int32_t) src[0]) << 16) | ((int32_t) src[1]);

    if(target == NULL){
        return length;
    }
    if(tgtLen < length){
        *status = U_BUFFER_OVERFLOW_ERROR;
        return length;
    }

    for (; i<srcLen; ++i) {
        uint16_t c = src[i];
        if (c == ESCAPE) {
            c = src[++i];
            if (c == ESCAPE) {
                target[ai++] = c;
            } else {
                int32_t runLength = (int32_t) c;
                uint16_t runValue = src[++i];
                int j=0;
                for (; j<runLength; ++j) {
                    target[ai++] = runValue;
                }
            }
        }
        else {
            target[ai++] = c;
        }
    }

    if (ai != length){
        *status = U_INTERNAL_PROGRAM_ERROR;
    }

    return length;
}

/**
 * Construct an array of bytes from a run-length encoded string.
 */
int32_t
rleStringToByteArray(uint16_t* src, int32_t srcLen, uint8_t* target, int32_t tgtLen, UErrorCode* status) {

    int32_t length = 0;
    UBool nextChar = true;
    uint16_t c = 0;
    int32_t node = 0;
    int32_t runLength = 0;
    int32_t i = 2;
    int32_t ai=0;

    if(!status || U_FAILURE(*status)){
        return 0;
    }
    /* the source is null terminated */
    if(srcLen == -1){
        srcLen = u_strlen(src);
    }
    if(srcLen <= 2){
        return 2;
    }
    length = (((int32_t) src[0]) << 16) | ((int32_t) src[1]);

    if(target == NULL){
        return length;
    }
    if(tgtLen < length){
        *status = U_BUFFER_OVERFLOW_ERROR;
        return length;
    }

    for (; ai<tgtLen; ) {
       /* This part of the loop places the next byte into the local
        * variable 'b' each time through the loop.  It keeps the
        * current character in 'c' and uses the boolean 'nextChar'
        * to see if we've taken both bytes out of 'c' yet.
        */
        uint8_t b;
        if (nextChar) {
            c = src[i++];
            b = (uint8_t) (c >> 8);
            nextChar = false;
        }
        else {
            b = (uint8_t) (c & 0xFF);
            nextChar = true;
        }

       /* This part of the loop is a tiny state machine which handles
        * the parsing of the run-length encoding.  This would be simpler
        * if we could look ahead, but we can't, so we use 'node' to
        * move between three nodes in the state machine.
        */
        switch (node) {
        case 0:
            /* Normal idle node */
            if (b == ESCAPE_BYTE) {
                node = 1;
            }
            else {
                target[ai++] = b;
            }
            break;
        case 1:
           /* We have seen one ESCAPE_BYTE; we expect either a second
            * one, or a run length and value.
            */
            if (b == ESCAPE_BYTE) {
                target[ai++] = ESCAPE_BYTE;
                node = 0;
            }
            else {
                runLength = b;
                node = 2;
            }
            break;
        case 2:
            {
                int j=0;
               /* We have seen an ESCAPE_BYTE and length byte.  We interpret
                * the next byte as the value to be repeated.
                */
                for (; j<runLength; ++j){
                    if(ai<tgtLen){
                        target[ai++] = b;
                    }else{
                        *status = U_BUFFER_OVERFLOW_ERROR;
                        return ai;
                    }
                }
                node = 0;
                break;
            }
        }
    }

    if (node != 0){
        *status = U_INTERNAL_PROGRAM_ERROR;
        /*("Bad run-length encoded byte array")*/
        return 0;
    }


    if (i != srcLen){
        /*("Excess data in RLE byte array string");*/
        *status = U_INTERNAL_PROGRAM_ERROR;
        return ai;
    }

    return ai;
}

