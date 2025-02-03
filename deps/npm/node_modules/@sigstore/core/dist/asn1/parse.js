"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.parseInteger = parseInteger;
exports.parseStringASCII = parseStringASCII;
exports.parseTime = parseTime;
exports.parseOID = parseOID;
exports.parseBoolean = parseBoolean;
exports.parseBitString = parseBitString;
/*
Copyright 2023 The Sigstore Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
const RE_TIME_SHORT_YEAR = /^(\d{2})(\d{2})(\d{2})(\d{2})(\d{2})(\d{2})(\.\d{3})?Z$/;
const RE_TIME_LONG_YEAR = /^(\d{4})(\d{2})(\d{2})(\d{2})(\d{2})(\d{2})(\.\d{3})?Z$/;
// Parse a BigInt from the DER-encoded buffer
// https://learn.microsoft.com/en-us/windows/win32/seccertenroll/about-integer
function parseInteger(buf) {
    let pos = 0;
    const end = buf.length;
    let val = buf[pos];
    const neg = val > 0x7f;
    // Consume any padding bytes
    const pad = neg ? 0xff : 0x00;
    while (val == pad && ++pos < end) {
        val = buf[pos];
    }
    // Calculate remaining bytes to read
    const len = end - pos;
    if (len === 0)
        return BigInt(neg ? -1 : 0);
    // Handle two's complement for negative numbers
    val = neg ? val - 256 : val;
    // Parse remaining bytes
    let n = BigInt(val);
    for (let i = pos + 1; i < end; ++i) {
        n = n * BigInt(256) + BigInt(buf[i]);
    }
    return n;
}
// Parse an ASCII string from the DER-encoded buffer
// https://learn.microsoft.com/en-us/windows/win32/seccertenroll/about-basic-types#boolean
function parseStringASCII(buf) {
    return buf.toString('ascii');
}
// Parse a Date from the DER-encoded buffer
// https://www.rfc-editor.org/rfc/rfc5280#section-4.1.2.5.1
function parseTime(buf, shortYear) {
    const timeStr = parseStringASCII(buf);
    // Parse the time string into matches - captured groups start at index 1
    const m = shortYear
        ? RE_TIME_SHORT_YEAR.exec(timeStr)
        : RE_TIME_LONG_YEAR.exec(timeStr);
    if (!m) {
        throw new Error('invalid time');
    }
    // Translate dates with a 2-digit year to 4 digits per the spec
    if (shortYear) {
        let year = Number(m[1]);
        year += year >= 50 ? 1900 : 2000;
        m[1] = year.toString();
    }
    // Translate to ISO8601 format and parse
    return new Date(`${m[1]}-${m[2]}-${m[3]}T${m[4]}:${m[5]}:${m[6]}Z`);
}
// Parse an OID from the DER-encoded buffer
// https://learn.microsoft.com/en-us/windows/win32/seccertenroll/about-object-identifier
function parseOID(buf) {
    let pos = 0;
    const end = buf.length;
    // Consume first byte which encodes the first two OID components
    let n = buf[pos++];
    const first = Math.floor(n / 40);
    const second = n % 40;
    let oid = `${first}.${second}`;
    // Consume remaining bytes
    let val = 0;
    for (; pos < end; ++pos) {
        n = buf[pos];
        val = (val << 7) + (n & 0x7f);
        // If the left-most bit is NOT set, then this is the last byte in the
        // sequence and we can add the value to the OID and reset the accumulator
        if ((n & 0x80) === 0) {
            oid += `.${val}`;
            val = 0;
        }
    }
    return oid;
}
// Parse a boolean from the DER-encoded buffer
// https://learn.microsoft.com/en-us/windows/win32/seccertenroll/about-basic-types#boolean
function parseBoolean(buf) {
    return buf[0] !== 0;
}
// Parse a bit string from the DER-encoded buffer
// https://learn.microsoft.com/en-us/windows/win32/seccertenroll/about-bit-string
function parseBitString(buf) {
    // First byte tell us how many unused bits are in the last byte
    const unused = buf[0];
    const start = 1;
    const end = buf.length;
    const bits = [];
    for (let i = start; i < end; ++i) {
        const byte = buf[i];
        // The skip value is only used for the last byte
        const skip = i === end - 1 ? unused : 0;
        // Iterate over each bit in the byte (most significant first)
        for (let j = 7; j >= skip; --j) {
            // Read the bit and add it to the bit string
            bits.push((byte >> j) & 0x01);
        }
    }
    return bits;
}
