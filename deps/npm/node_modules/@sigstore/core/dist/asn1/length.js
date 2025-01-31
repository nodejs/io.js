"use strict";
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
Object.defineProperty(exports, "__esModule", { value: true });
exports.decodeLength = decodeLength;
exports.encodeLength = encodeLength;
const error_1 = require("./error");
// Decodes the length of a DER-encoded ANS.1 element from the supplied stream.
// https://learn.microsoft.com/en-us/windows/win32/seccertenroll/about-encoded-length-and-value-bytes
function decodeLength(stream) {
    const buf = stream.getUint8();
    // If the most significant bit is UNSET the length is just the value of the
    // byte.
    if ((buf & 0x80) === 0x00) {
        return buf;
    }
    // Otherwise, the lower 7 bits of the first byte indicate the number of bytes
    // that follow to encode the length.
    const byteCount = buf & 0x7f;
    // Ensure the encoded length can safely fit in a JS number.
    if (byteCount > 6) {
        throw new error_1.ASN1ParseError('length exceeds 6 byte limit');
    }
    // Iterate over the bytes that encode the length.
    let len = 0;
    for (let i = 0; i < byteCount; i++) {
        len = len * 256 + stream.getUint8();
    }
    // This is a valid ASN.1 length encoding, but we don't support it.
    if (len === 0) {
        throw new error_1.ASN1ParseError('indefinite length encoding not supported');
    }
    return len;
}
// Translates the supplied value to a DER-encoded length.
function encodeLength(len) {
    if (len < 128) {
        return Buffer.from([len]);
    }
    // Bitwise operations on large numbers are not supported in JS, so we need to
    // use BigInts.
    let val = BigInt(len);
    const bytes = [];
    while (val > 0n) {
        bytes.unshift(Number(val & 255n));
        val = val >> 8n;
    }
    return Buffer.from([0x80 | bytes.length, ...bytes]);
}
