"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.verifyHashedRekordTLogBody = verifyHashedRekordTLogBody;
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
const error_1 = require("../error");
// Compare the given hashedrekord tlog entry to the given bundle
function verifyHashedRekordTLogBody(tlogEntry, content) {
    switch (tlogEntry.apiVersion) {
        case '0.0.1':
            return verifyHashedrekord001TLogBody(tlogEntry, content);
        default:
            throw new error_1.VerificationError({
                code: 'TLOG_BODY_ERROR',
                message: `unsupported hashedrekord version: ${tlogEntry.apiVersion}`,
            });
    }
}
// Compare the given hashedrekord v0.0.1 tlog entry to the given message
// signature
function verifyHashedrekord001TLogBody(tlogEntry, content) {
    // Ensure that the bundles message signature matches the tlog entry
    const tlogSig = tlogEntry.spec.signature.content || '';
    if (!content.compareSignature(Buffer.from(tlogSig, 'base64'))) {
        throw new error_1.VerificationError({
            code: 'TLOG_BODY_ERROR',
            message: 'signature mismatch',
        });
    }
    // Ensure that the bundle's message digest matches the tlog entry
    const tlogDigest = tlogEntry.spec.data.hash?.value || '';
    if (!content.compareDigest(Buffer.from(tlogDigest, 'hex'))) {
        throw new error_1.VerificationError({
            code: 'TLOG_BODY_ERROR',
            message: 'digest mismatch',
        });
    }
}
