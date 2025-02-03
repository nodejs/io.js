"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.encodeOIDString = encodeOIDString;
const ANS1_TAG_OID = 0x06;
function encodeOIDString(oid) {
    const parts = oid.split('.');
    // The first two subidentifiers are encoded into the first byte
    const first = parseInt(parts[0], 10) * 40 + parseInt(parts[1], 10);
    const rest = [];
    parts.slice(2).forEach((part) => {
        const bytes = encodeVariableLengthInteger(parseInt(part, 10));
        rest.push(...bytes);
    });
    const der = Buffer.from([first, ...rest]);
    return Buffer.from([ANS1_TAG_OID, der.length, ...der]);
}
function encodeVariableLengthInteger(value) {
    const bytes = [];
    let mask = 0x00;
    while (value > 0) {
        bytes.unshift((value & 0x7f) | mask);
        value >>= 7;
        mask = 0x80;
    }
    return bytes;
}
