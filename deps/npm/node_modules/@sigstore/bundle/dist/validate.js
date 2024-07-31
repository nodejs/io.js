"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.assertBundleLatest = exports.assertBundleV02 = exports.isBundleV01 = exports.assertBundleV01 = exports.assertBundle = void 0;
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
// Performs basic validation of a Sigstore bundle to ensure that all required
// fields are populated. This is not a complete validation of the bundle, but
// rather a check that the bundle is in a valid state to be processed by the
// rest of the code.
function assertBundle(b) {
    const invalidValues = validateBundleBase(b);
    if (invalidValues.length > 0) {
        throw new error_1.ValidationError('invalid bundle', invalidValues);
    }
}
exports.assertBundle = assertBundle;
// Asserts that the given bundle conforms to the v0.1 bundle format.
function assertBundleV01(b) {
    const invalidValues = [];
    invalidValues.push(...validateBundleBase(b));
    invalidValues.push(...validateInclusionPromise(b));
    if (invalidValues.length > 0) {
        throw new error_1.ValidationError('invalid v0.1 bundle', invalidValues);
    }
}
exports.assertBundleV01 = assertBundleV01;
// Type guard to determine if Bundle is a v0.1 bundle.
function isBundleV01(b) {
    try {
        assertBundleV01(b);
        return true;
    }
    catch (e) {
        return false;
    }
}
exports.isBundleV01 = isBundleV01;
// Asserts that the given bundle conforms to the v0.2 bundle format.
function assertBundleV02(b) {
    const invalidValues = [];
    invalidValues.push(...validateBundleBase(b));
    invalidValues.push(...validateInclusionProof(b));
    if (invalidValues.length > 0) {
        throw new error_1.ValidationError('invalid v0.2 bundle', invalidValues);
    }
}
exports.assertBundleV02 = assertBundleV02;
// Asserts that the given bundle conforms to the newest (0.3) bundle format.
function assertBundleLatest(b) {
    const invalidValues = [];
    invalidValues.push(...validateBundleBase(b));
    invalidValues.push(...validateInclusionProof(b));
    invalidValues.push(...validateNoCertificateChain(b));
    if (invalidValues.length > 0) {
        throw new error_1.ValidationError('invalid bundle', invalidValues);
    }
}
exports.assertBundleLatest = assertBundleLatest;
function validateBundleBase(b) {
    const invalidValues = [];
    // Media type validation
    if (b.mediaType === undefined ||
        (!b.mediaType.match(/^application\/vnd\.dev\.sigstore\.bundle\+json;version=\d\.\d/) &&
            !b.mediaType.match(/^application\/vnd\.dev\.sigstore\.bundle\.v\d\.\d\+json/))) {
        invalidValues.push('mediaType');
    }
    // Content-related validation
    if (b.content === undefined) {
        invalidValues.push('content');
    }
    else {
        switch (b.content.$case) {
            case 'messageSignature':
                if (b.content.messageSignature.messageDigest === undefined) {
                    invalidValues.push('content.messageSignature.messageDigest');
                }
                else {
                    if (b.content.messageSignature.messageDigest.digest.length === 0) {
                        invalidValues.push('content.messageSignature.messageDigest.digest');
                    }
                }
                if (b.content.messageSignature.signature.length === 0) {
                    invalidValues.push('content.messageSignature.signature');
                }
                break;
            case 'dsseEnvelope':
                if (b.content.dsseEnvelope.payload.length === 0) {
                    invalidValues.push('content.dsseEnvelope.payload');
                }
                if (b.content.dsseEnvelope.signatures.length !== 1) {
                    invalidValues.push('content.dsseEnvelope.signatures');
                }
                else {
                    if (b.content.dsseEnvelope.signatures[0].sig.length === 0) {
                        invalidValues.push('content.dsseEnvelope.signatures[0].sig');
                    }
                }
                break;
        }
    }
    // Verification material-related validation
    if (b.verificationMaterial === undefined) {
        invalidValues.push('verificationMaterial');
    }
    else {
        if (b.verificationMaterial.content === undefined) {
            invalidValues.push('verificationMaterial.content');
        }
        else {
            switch (b.verificationMaterial.content.$case) {
                case 'x509CertificateChain':
                    if (b.verificationMaterial.content.x509CertificateChain.certificates
                        .length === 0) {
                        invalidValues.push('verificationMaterial.content.x509CertificateChain.certificates');
                    }
                    b.verificationMaterial.content.x509CertificateChain.certificates.forEach((cert, i) => {
                        if (cert.rawBytes.length === 0) {
                            invalidValues.push(`verificationMaterial.content.x509CertificateChain.certificates[${i}].rawBytes`);
                        }
                    });
                    break;
                case 'certificate':
                    if (b.verificationMaterial.content.certificate.rawBytes.length === 0) {
                        invalidValues.push('verificationMaterial.content.certificate.rawBytes');
                    }
                    break;
            }
        }
        if (b.verificationMaterial.tlogEntries === undefined) {
            invalidValues.push('verificationMaterial.tlogEntries');
        }
        else {
            if (b.verificationMaterial.tlogEntries.length > 0) {
                b.verificationMaterial.tlogEntries.forEach((entry, i) => {
                    if (entry.logId === undefined) {
                        invalidValues.push(`verificationMaterial.tlogEntries[${i}].logId`);
                    }
                    if (entry.kindVersion === undefined) {
                        invalidValues.push(`verificationMaterial.tlogEntries[${i}].kindVersion`);
                    }
                });
            }
        }
    }
    return invalidValues;
}
// Necessary for V01 bundles
function validateInclusionPromise(b) {
    const invalidValues = [];
    if (b.verificationMaterial &&
        b.verificationMaterial.tlogEntries?.length > 0) {
        b.verificationMaterial.tlogEntries.forEach((entry, i) => {
            if (entry.inclusionPromise === undefined) {
                invalidValues.push(`verificationMaterial.tlogEntries[${i}].inclusionPromise`);
            }
        });
    }
    return invalidValues;
}
// Necessary for V02 and later bundles
function validateInclusionProof(b) {
    const invalidValues = [];
    if (b.verificationMaterial &&
        b.verificationMaterial.tlogEntries?.length > 0) {
        b.verificationMaterial.tlogEntries.forEach((entry, i) => {
            if (entry.inclusionProof === undefined) {
                invalidValues.push(`verificationMaterial.tlogEntries[${i}].inclusionProof`);
            }
            else {
                if (entry.inclusionProof.checkpoint === undefined) {
                    invalidValues.push(`verificationMaterial.tlogEntries[${i}].inclusionProof.checkpoint`);
                }
            }
        });
    }
    return invalidValues;
}
// Necessary for V03 and later bundles
function validateNoCertificateChain(b) {
    const invalidValues = [];
    if (b.verificationMaterial?.content?.$case === 'x509CertificateChain') {
        invalidValues.push('verificationMaterial.content.$case');
    }
    return invalidValues;
}
