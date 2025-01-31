"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.CAClient = void 0;
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
const error_1 = require("../../error");
const fulcio_1 = require("../../external/fulcio");
class CAClient {
    constructor(options) {
        this.fulcio = new fulcio_1.Fulcio({
            baseURL: options.fulcioBaseURL,
            retry: options.retry,
            timeout: options.timeout,
        });
    }
    async createSigningCertificate(identityToken, publicKey, challenge) {
        const request = toCertificateRequest(identityToken, publicKey, challenge);
        try {
            const resp = await this.fulcio.createSigningCertificate(request);
            // Account for the fact that the response may contain either a
            // signedCertificateEmbeddedSct or a signedCertificateDetachedSct.
            const cert = resp.signedCertificateEmbeddedSct
                ? resp.signedCertificateEmbeddedSct
                : resp.signedCertificateDetachedSct;
            return cert.chain.certificates;
        }
        catch (err) {
            (0, error_1.internalError)(err, 'CA_CREATE_SIGNING_CERTIFICATE_ERROR', 'error creating signing certificate');
        }
    }
}
exports.CAClient = CAClient;
function toCertificateRequest(identityToken, publicKey, challenge) {
    return {
        credentials: {
            oidcIdentityToken: identityToken,
        },
        publicKeyRequest: {
            publicKey: {
                algorithm: 'ECDSA',
                content: publicKey,
            },
            proofOfPossession: challenge.toString('base64'),
        },
    };
}
