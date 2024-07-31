"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.ASN1Tag = void 0;
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
const error_1 = require("./error");
const UNIVERSAL_TAG = {
    BOOLEAN: 0x01,
    INTEGER: 0x02,
    BIT_STRING: 0x03,
    OCTET_STRING: 0x04,
    OBJECT_IDENTIFIER: 0x06,
    SEQUENCE: 0x10,
    SET: 0x11,
    PRINTABLE_STRING: 0x13,
    UTC_TIME: 0x17,
    GENERALIZED_TIME: 0x18,
};
const TAG_CLASS = {
    UNIVERSAL: 0x00,
    APPLICATION: 0x01,
    CONTEXT_SPECIFIC: 0x02,
    PRIVATE: 0x03,
};
// https://learn.microsoft.com/en-us/windows/win32/seccertenroll/about-encoded-tag-bytes
class ASN1Tag {
    constructor(enc) {
        // Bits 0 through 4 are the tag number
        this.number = enc & 0x1f;
        // Bit 5 is the constructed bit
        this.constructed = (enc & 0x20) === 0x20;
        // Bit 6 & 7 are the class
        this.class = enc >> 6;
        if (this.number === 0x1f) {
            throw new error_1.ASN1ParseError('long form tags not supported');
        }
        if (this.class === TAG_CLASS.UNIVERSAL && this.number === 0x00) {
            throw new error_1.ASN1ParseError('unsupported tag 0x00');
        }
    }
    isUniversal() {
        return this.class === TAG_CLASS.UNIVERSAL;
    }
    isContextSpecific(num) {
        const res = this.class === TAG_CLASS.CONTEXT_SPECIFIC;
        return num !== undefined ? res && this.number === num : res;
    }
    isBoolean() {
        return this.isUniversal() && this.number === UNIVERSAL_TAG.BOOLEAN;
    }
    isInteger() {
        return this.isUniversal() && this.number === UNIVERSAL_TAG.INTEGER;
    }
    isBitString() {
        return this.isUniversal() && this.number === UNIVERSAL_TAG.BIT_STRING;
    }
    isOctetString() {
        return this.isUniversal() && this.number === UNIVERSAL_TAG.OCTET_STRING;
    }
    isOID() {
        return (this.isUniversal() && this.number === UNIVERSAL_TAG.OBJECT_IDENTIFIER);
    }
    isUTCTime() {
        return this.isUniversal() && this.number === UNIVERSAL_TAG.UTC_TIME;
    }
    isGeneralizedTime() {
        return this.isUniversal() && this.number === UNIVERSAL_TAG.GENERALIZED_TIME;
    }
    toDER() {
        return this.number | (this.constructed ? 0x20 : 0x00) | (this.class << 6);
    }
}
exports.ASN1Tag = ASN1Tag;
