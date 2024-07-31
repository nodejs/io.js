"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.verifyCertificate = exports.verifyPublicKey = void 0;
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
const error_1 = require("../error");
const certificate_1 = require("./certificate");
const sct_1 = require("./sct");
const OID_FULCIO_ISSUER_V1 = '1.3.6.1.4.1.57264.1.1';
const OID_FULCIO_ISSUER_V2 = '1.3.6.1.4.1.57264.1.8';
function verifyPublicKey(hint, timestamps, trustMaterial) {
    const key = trustMaterial.publicKey(hint);
    timestamps.forEach((timestamp) => {
        if (!key.validFor(timestamp)) {
            throw new error_1.VerificationError({
                code: 'PUBLIC_KEY_ERROR',
                message: `Public key is not valid for timestamp: ${timestamp.toISOString()}`,
            });
        }
    });
    return { key: key.publicKey };
}
exports.verifyPublicKey = verifyPublicKey;
function verifyCertificate(leaf, timestamps, trustMaterial) {
    // Check that leaf certificate chains to a trusted CA
    const path = (0, certificate_1.verifyCertificateChain)(leaf, trustMaterial.certificateAuthorities);
    // Check that ALL certificates are valid for ALL of the timestamps
    const validForDate = timestamps.every((timestamp) => path.every((cert) => cert.validForDate(timestamp)));
    if (!validForDate) {
        throw new error_1.VerificationError({
            code: 'CERTIFICATE_ERROR',
            message: 'certificate is not valid or expired at the specified date',
        });
    }
    return {
        scts: (0, sct_1.verifySCTs)(path[0], path[1], trustMaterial.ctlogs),
        signer: getSigner(path[0]),
    };
}
exports.verifyCertificate = verifyCertificate;
function getSigner(cert) {
    let issuer;
    const issuerExtension = cert.extension(OID_FULCIO_ISSUER_V2);
    if (issuerExtension) {
        issuer = issuerExtension.valueObj.subs?.[0]?.value.toString('ascii');
    }
    else {
        issuer = cert.extension(OID_FULCIO_ISSUER_V1)?.value.toString('ascii');
    }
    const identity = {
        extensions: { issuer },
        subjectAlternativeName: cert.subjectAltName,
    };
    return {
        key: core_1.crypto.createPublicKey(cert.publicKey),
        identity,
    };
}
