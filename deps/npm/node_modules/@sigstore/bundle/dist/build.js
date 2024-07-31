"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.toDSSEBundle = exports.toMessageSignatureBundle = void 0;
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
const protobuf_specs_1 = require("@sigstore/protobuf-specs");
const bundle_1 = require("./bundle");
// Message signature bundle - $case: 'messageSignature'
function toMessageSignatureBundle(options) {
    return {
        mediaType: options.singleCertificate
            ? bundle_1.BUNDLE_V03_MEDIA_TYPE
            : bundle_1.BUNDLE_V02_MEDIA_TYPE,
        content: {
            $case: 'messageSignature',
            messageSignature: {
                messageDigest: {
                    algorithm: protobuf_specs_1.HashAlgorithm.SHA2_256,
                    digest: options.digest,
                },
                signature: options.signature,
            },
        },
        verificationMaterial: toVerificationMaterial(options),
    };
}
exports.toMessageSignatureBundle = toMessageSignatureBundle;
// DSSE envelope bundle - $case: 'dsseEnvelope'
function toDSSEBundle(options) {
    return {
        mediaType: options.singleCertificate
            ? bundle_1.BUNDLE_V03_MEDIA_TYPE
            : bundle_1.BUNDLE_V02_MEDIA_TYPE,
        content: {
            $case: 'dsseEnvelope',
            dsseEnvelope: toEnvelope(options),
        },
        verificationMaterial: toVerificationMaterial(options),
    };
}
exports.toDSSEBundle = toDSSEBundle;
function toEnvelope(options) {
    return {
        payloadType: options.artifactType,
        payload: options.artifact,
        signatures: [toSignature(options)],
    };
}
function toSignature(options) {
    return {
        keyid: options.keyHint || '',
        sig: options.signature,
    };
}
// Verification material
function toVerificationMaterial(options) {
    return {
        content: toKeyContent(options),
        tlogEntries: [],
        timestampVerificationData: { rfc3161Timestamps: [] },
    };
}
function toKeyContent(options) {
    if (options.certificate) {
        if (options.singleCertificate) {
            return {
                $case: 'certificate',
                certificate: { rawBytes: options.certificate },
            };
        }
        else {
            return {
                $case: 'x509CertificateChain',
                x509CertificateChain: {
                    certificates: [{ rawBytes: options.certificate }],
                },
            };
        }
    }
    else {
        return {
            $case: 'publicKey',
            publicKey: {
                hint: options.keyHint || '',
            },
        };
    }
}
