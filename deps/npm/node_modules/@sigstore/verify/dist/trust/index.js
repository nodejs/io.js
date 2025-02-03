"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.filterTLogAuthorities = exports.filterCertAuthorities = void 0;
exports.toTrustMaterial = toTrustMaterial;
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
const core_1 = require("@sigstore/core");
const protobuf_specs_1 = require("@sigstore/protobuf-specs");
const error_1 = require("../error");
const BEGINNING_OF_TIME = new Date(0);
const END_OF_TIME = new Date(8640000000000000);
var filter_1 = require("./filter");
Object.defineProperty(exports, "filterCertAuthorities", { enumerable: true, get: function () { return filter_1.filterCertAuthorities; } });
Object.defineProperty(exports, "filterTLogAuthorities", { enumerable: true, get: function () { return filter_1.filterTLogAuthorities; } });
function toTrustMaterial(root, keys) {
    const keyFinder = typeof keys === 'function' ? keys : keyLocator(keys);
    return {
        certificateAuthorities: root.certificateAuthorities.map(createCertAuthority),
        timestampAuthorities: root.timestampAuthorities.map(createCertAuthority),
        tlogs: root.tlogs.map(createTLogAuthority),
        ctlogs: root.ctlogs.map(createTLogAuthority),
        publicKey: keyFinder,
    };
}
function createTLogAuthority(tlogInstance) {
    const keyDetails = tlogInstance.publicKey.keyDetails;
    const keyType = keyDetails === protobuf_specs_1.PublicKeyDetails.PKCS1_RSA_PKCS1V5 ||
        keyDetails === protobuf_specs_1.PublicKeyDetails.PKIX_RSA_PKCS1V5 ||
        keyDetails === protobuf_specs_1.PublicKeyDetails.PKIX_RSA_PKCS1V15_2048_SHA256 ||
        keyDetails === protobuf_specs_1.PublicKeyDetails.PKIX_RSA_PKCS1V15_3072_SHA256 ||
        keyDetails === protobuf_specs_1.PublicKeyDetails.PKIX_RSA_PKCS1V15_4096_SHA256
        ? 'pkcs1'
        : 'spki';
    return {
        logID: tlogInstance.logId.keyId,
        publicKey: core_1.crypto.createPublicKey(tlogInstance.publicKey.rawBytes, keyType),
        validFor: {
            start: tlogInstance.publicKey.validFor?.start || BEGINNING_OF_TIME,
            end: tlogInstance.publicKey.validFor?.end || END_OF_TIME,
        },
    };
}
function createCertAuthority(ca) {
    /* istanbul ignore next */
    return {
        certChain: ca.certChain.certificates.map((cert) => {
            return core_1.X509Certificate.parse(cert.rawBytes);
        }),
        validFor: {
            start: ca.validFor?.start || BEGINNING_OF_TIME,
            end: ca.validFor?.end || END_OF_TIME,
        },
    };
}
function keyLocator(keys) {
    return (hint) => {
        const key = (keys || {})[hint];
        if (!key) {
            throw new error_1.VerificationError({
                code: 'PUBLIC_KEY_ERROR',
                message: `key not found: ${hint}`,
            });
        }
        return {
            publicKey: core_1.crypto.createPublicKey(key.rawBytes),
            validFor: (date) => {
                /* istanbul ignore next */
                return ((key.validFor?.start || BEGINNING_OF_TIME) <= date &&
                    (key.validFor?.end || END_OF_TIME) >= date);
            },
        };
    };
}
