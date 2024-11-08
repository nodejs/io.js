// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2010-2014, International Business Machines
* Corporation and others.  All Rights Reserved.
*******************************************************************************
* collation.cpp
*
* created on: 2010oct27
* created by: Markus W. Scherer
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

#include "collation.h"
#include "uassert.h"

U_NAMESPACE_BEGIN

uint32_t
Collation::incTwoBytePrimaryByOffset(uint32_t basePrimary, UBool isCompressible, int32_t offset) {
    // Extract the second byte, minus the minimum byte value,
    // plus the offset, modulo the number of usable byte values, plus the minimum.
    // Reserve the PRIMARY_COMPRESSION_LOW_BYTE and high byte if necessary.
    uint32_t primary;
    if(isCompressible) {
        offset += (static_cast<int32_t>(basePrimary >> 16) & 0xff) - 4;
        primary = static_cast<uint32_t>((offset % 251) + 4) << 16;
        offset /= 251;
    } else {
        offset += (static_cast<int32_t>(basePrimary >> 16) & 0xff) - 2;
        primary = static_cast<uint32_t>((offset % 254) + 2) << 16;
        offset /= 254;
    }
    // First byte, assume no further overflow.
    return primary | ((basePrimary & 0xff000000) + static_cast<uint32_t>(offset << 24));
}

uint32_t
Collation::incThreeBytePrimaryByOffset(uint32_t basePrimary, UBool isCompressible, int32_t offset) {
    // Extract the third byte, minus the minimum byte value,
    // plus the offset, modulo the number of usable byte values, plus the minimum.
    offset += (static_cast<int32_t>(basePrimary >> 8) & 0xff) - 2;
    uint32_t primary = static_cast<uint32_t>((offset % 254) + 2) << 8;
    offset /= 254;
    // Same with the second byte,
    // but reserve the PRIMARY_COMPRESSION_LOW_BYTE and high byte if necessary.
    if(isCompressible) {
        offset += (static_cast<int32_t>(basePrimary >> 16) & 0xff) - 4;
        primary |= static_cast<uint32_t>((offset % 251) + 4) << 16;
        offset /= 251;
    } else {
        offset += (static_cast<int32_t>(basePrimary >> 16) & 0xff) - 2;
        primary |= static_cast<uint32_t>((offset % 254) + 2) << 16;
        offset /= 254;
    }
    // First byte, assume no further overflow.
    return primary | ((basePrimary & 0xff000000) + static_cast<uint32_t>(offset << 24));
}

uint32_t
Collation::decTwoBytePrimaryByOneStep(uint32_t basePrimary, UBool isCompressible, int32_t step) {
    // Extract the second byte, minus the minimum byte value,
    // minus the step, modulo the number of usable byte values, plus the minimum.
    // Reserve the PRIMARY_COMPRESSION_LOW_BYTE and high byte if necessary.
    // Assume no further underflow for the first byte.
    U_ASSERT(0 < step && step <= 0x7f);
    int32_t byte2 = (static_cast<int32_t>(basePrimary >> 16) & 0xff) - step;
    if(isCompressible) {
        if(byte2 < 4) {
            byte2 += 251;
            basePrimary -= 0x1000000;
        }
    } else {
        if(byte2 < 2) {
            byte2 += 254;
            basePrimary -= 0x1000000;
        }
    }
    return (basePrimary & 0xff000000) | (static_cast<uint32_t>(byte2) << 16);
}

uint32_t
Collation::decThreeBytePrimaryByOneStep(uint32_t basePrimary, UBool isCompressible, int32_t step) {
    // Extract the third byte, minus the minimum byte value,
    // minus the step, modulo the number of usable byte values, plus the minimum.
    U_ASSERT(0 < step && step <= 0x7f);
    int32_t byte3 = (static_cast<int32_t>(basePrimary >> 8) & 0xff) - step;
    if(byte3 >= 2) {
        return (basePrimary & 0xffff0000) | (static_cast<uint32_t>(byte3) << 8);
    }
    byte3 += 254;
    // Same with the second byte,
    // but reserve the PRIMARY_COMPRESSION_LOW_BYTE and high byte if necessary.
    int32_t byte2 = (static_cast<int32_t>(basePrimary >> 16) & 0xff) - 1;
    if(isCompressible) {
        if(byte2 < 4) {
            byte2 = 0xfe;
            basePrimary -= 0x1000000;
        }
    } else {
        if(byte2 < 2) {
            byte2 = 0xff;
            basePrimary -= 0x1000000;
        }
    }
    // First byte, assume no further underflow.
    return (basePrimary & 0xff000000) | (static_cast<uint32_t>(byte2) << 16) | (static_cast<uint32_t>(byte3) << 8);
}

uint32_t
Collation::getThreeBytePrimaryForOffsetData(UChar32 c, int64_t dataCE) {
    uint32_t p = static_cast<uint32_t>(dataCE >> 32); // three-byte primary pppppp00
    int32_t lower32 = static_cast<int32_t>(dataCE); // base code point b & step s: bbbbbbss (bit 7: isCompressible)
    int32_t offset = (c - (lower32 >> 8)) * (lower32 & 0x7f);  // delta * increment
    UBool isCompressible = (lower32 & 0x80) != 0;
    return Collation::incThreeBytePrimaryByOffset(p, isCompressible, offset);
}

uint32_t
Collation::unassignedPrimaryFromCodePoint(UChar32 c) {
    // Create a gap before U+0000. Use c=-1 for [first unassigned].
    ++c;
    // Fourth byte: 18 values, every 14th byte value (gap of 13).
    uint32_t primary = 2 + (c % 18) * 14;
    c /= 18;
    // Third byte: 254 values.
    primary |= (2 + (c % 254)) << 8;
    c /= 254;
    // Second byte: 251 values 04..FE excluding the primary compression bytes.
    primary |= (4 + (c % 251)) << 16;
    // One lead byte covers all code points (c < 0x1182B4 = 1*251*254*18).
    return primary | (UNASSIGNED_IMPLICIT_BYTE << 24);
}

U_NAMESPACE_END

#endif  // !UCONFIG_NO_COLLATION
