"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.Verifier = void 0;
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
const util_1 = require("util");
const error_1 = require("./error");
const key_1 = require("./key");
const policy_1 = require("./policy");
const timestamp_1 = require("./timestamp");
const tlog_1 = require("./tlog");
class Verifier {
    constructor(trustMaterial, options = {}) {
        this.trustMaterial = trustMaterial;
        this.options = {
            ctlogThreshold: options.ctlogThreshold ?? 1,
            tlogThreshold: options.tlogThreshold ?? 1,
            tsaThreshold: options.tsaThreshold ?? 0,
        };
    }
    verify(entity, policy) {
        const timestamps = this.verifyTimestamps(entity);
        const signer = this.verifySigningKey(entity, timestamps);
        this.verifyTLogs(entity);
        this.verifySignature(entity, signer);
        if (policy) {
            this.verifyPolicy(policy, signer.identity || {});
        }
        return signer;
    }
    // Checks that all of the timestamps in the entity are valid and returns them
    verifyTimestamps(entity) {
        let tlogCount = 0;
        let tsaCount = 0;
        const timestamps = entity.timestamps.map((timestamp) => {
            switch (timestamp.$case) {
                case 'timestamp-authority':
                    tsaCount++;
                    return (0, timestamp_1.verifyTSATimestamp)(timestamp.timestamp, entity.signature.signature, this.trustMaterial.timestampAuthorities);
                case 'transparency-log':
                    tlogCount++;
                    return (0, timestamp_1.verifyTLogTimestamp)(timestamp.tlogEntry, this.trustMaterial.tlogs);
            }
        });
        // Check for duplicate timestamps
        if (containsDupes(timestamps)) {
            throw new error_1.VerificationError({
                code: 'TIMESTAMP_ERROR',
                message: 'duplicate timestamp',
            });
        }
        if (tlogCount < this.options.tlogThreshold) {
            throw new error_1.VerificationError({
                code: 'TIMESTAMP_ERROR',
                message: `expected ${this.options.tlogThreshold} tlog timestamps, got ${tlogCount}`,
            });
        }
        if (tsaCount < this.options.tsaThreshold) {
            throw new error_1.VerificationError({
                code: 'TIMESTAMP_ERROR',
                message: `expected ${this.options.tsaThreshold} tsa timestamps, got ${tsaCount}`,
            });
        }
        return timestamps.map((t) => t.timestamp);
    }
    // Checks that the signing key is valid for all of the the supplied timestamps
    // and returns the signer.
    verifySigningKey({ key }, timestamps) {
        switch (key.$case) {
            case 'public-key': {
                return (0, key_1.verifyPublicKey)(key.hint, timestamps, this.trustMaterial);
            }
            case 'certificate': {
                const result = (0, key_1.verifyCertificate)(key.certificate, timestamps, this.trustMaterial);
                /* istanbul ignore next - no fixture */
                if (containsDupes(result.scts)) {
                    throw new error_1.VerificationError({
                        code: 'CERTIFICATE_ERROR',
                        message: 'duplicate SCT',
                    });
                }
                if (result.scts.length < this.options.ctlogThreshold) {
                    throw new error_1.VerificationError({
                        code: 'CERTIFICATE_ERROR',
                        message: `expected ${this.options.ctlogThreshold} SCTs, got ${result.scts.length}`,
                    });
                }
                return result.signer;
            }
        }
    }
    // Checks that the tlog entries are valid for the supplied content
    verifyTLogs({ signature: content, tlogEntries }) {
        tlogEntries.forEach((entry) => (0, tlog_1.verifyTLogBody)(entry, content));
    }
    // Checks that the signature is valid for the supplied content
    verifySignature(entity, signer) {
        if (!entity.signature.verifySignature(signer.key)) {
            throw new error_1.VerificationError({
                code: 'SIGNATURE_ERROR',
                message: 'signature verification failed',
            });
        }
    }
    verifyPolicy(policy, identity) {
        // Check the subject alternative name of the signer matches the policy
        if (policy.subjectAlternativeName) {
            (0, policy_1.verifySubjectAlternativeName)(policy.subjectAlternativeName, identity.subjectAlternativeName);
        }
        // Check that the extensions of the signer match the policy
        if (policy.extensions) {
            (0, policy_1.verifyExtensions)(policy.extensions, identity.extensions);
        }
    }
}
exports.Verifier = Verifier;
// Checks for duplicate items in the array. Objects are compared using
// deep equality.
function containsDupes(arr) {
    for (let i = 0; i < arr.length; i++) {
        for (let j = i + 1; j < arr.length; j++) {
            if ((0, util_1.isDeepStrictEqual)(arr[i], arr[j])) {
                return true;
            }
        }
    }
    return false;
}
