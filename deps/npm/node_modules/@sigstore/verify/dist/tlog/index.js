"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.verifyTLogBody = verifyTLogBody;
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
const dsse_1 = require("./dsse");
const hashedrekord_1 = require("./hashedrekord");
const intoto_1 = require("./intoto");
// Verifies that the given tlog entry matches the supplied signature content.
function verifyTLogBody(entry, sigContent) {
    const { kind, version } = entry.kindVersion;
    const body = JSON.parse(entry.canonicalizedBody.toString('utf8'));
    if (kind !== body.kind || version !== body.apiVersion) {
        throw new error_1.VerificationError({
            code: 'TLOG_BODY_ERROR',
            message: `kind/version mismatch - expected: ${kind}/${version}, received: ${body.kind}/${body.apiVersion}`,
        });
    }
    switch (body.kind) {
        case 'dsse':
            return (0, dsse_1.verifyDSSETLogBody)(body, sigContent);
        case 'intoto':
            return (0, intoto_1.verifyIntotoTLogBody)(body, sigContent);
        case 'hashedrekord':
            return (0, hashedrekord_1.verifyHashedRekordTLogBody)(body, sigContent);
        /* istanbul ignore next */
        default:
            throw new error_1.VerificationError({
                code: 'TLOG_BODY_ERROR',
                message: `unsupported kind: ${kind}`,
            });
    }
}
