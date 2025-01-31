"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.verifySCTs = verifySCTs;
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
const trust_1 = require("../trust");
function verifySCTs(cert, issuer, ctlogs) {
    let extSCT;
    // Verifying the SCT requires that we remove the SCT extension and
    // re-encode the TBS structure to DER -- this value is part of the data
    // over which the signature is calculated. Since this is a destructive action
    // we create a copy of the certificate so we can remove the SCT extension
    // without affecting the original certificate.
    const clone = cert.clone();
    // Intentionally not using the findExtension method here because we want to
    // remove the the SCT extension from the certificate before calculating the
    // PreCert structure
    for (let i = 0; i < clone.extensions.length; i++) {
        const ext = clone.extensions[i];
        if (ext.subs[0].toOID() === core_1.EXTENSION_OID_SCT) {
            extSCT = new core_1.X509SCTExtension(ext);
            // Remove the extension from the certificate
            clone.extensions.splice(i, 1);
            break;
        }
    }
    // No SCT extension found to verify
    if (!extSCT) {
        return [];
    }
    // Found an SCT extension but it has no SCTs
    /* istanbul ignore if -- too difficult to fabricate test case for this */
    if (extSCT.signedCertificateTimestamps.length === 0) {
        return [];
    }
    // Construct the PreCert structure
    // https://www.rfc-editor.org/rfc/rfc6962#section-3.2
    const preCert = new core_1.ByteStream();
    // Calculate hash of the issuer's public key
    const issuerId = core_1.crypto.digest('sha256', issuer.publicKey);
    preCert.appendView(issuerId);
    // Re-encodes the certificate to DER after removing the SCT extension
    const tbs = clone.tbsCertificate.toDER();
    preCert.appendUint24(tbs.length);
    preCert.appendView(tbs);
    // Calculate and return the verification results for each SCT
    return extSCT.signedCertificateTimestamps.map((sct) => {
        // Find the ctlog instance that corresponds to the SCT's logID
        const validCTLogs = (0, trust_1.filterTLogAuthorities)(ctlogs, {
            logID: sct.logID,
            targetDate: sct.datetime,
        });
        // See if the SCT is valid for any of the CT logs
        const verified = validCTLogs.some((log) => sct.verify(preCert.buffer, log.publicKey));
        if (!verified) {
            throw new error_1.VerificationError({
                code: 'CERTIFICATE_ERROR',
                message: 'SCT verification failed',
            });
        }
        return sct.logID;
    });
}
