"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.isBundleWithDsseEnvelope = exports.isBundleWithMessageSignature = exports.isBundleWithPublicKey = exports.isBundleWithCertificateChain = exports.BUNDLE_V03_MEDIA_TYPE = exports.BUNDLE_V03_LEGACY_MEDIA_TYPE = exports.BUNDLE_V02_MEDIA_TYPE = exports.BUNDLE_V01_MEDIA_TYPE = void 0;
exports.BUNDLE_V01_MEDIA_TYPE = 'application/vnd.dev.sigstore.bundle+json;version=0.1';
exports.BUNDLE_V02_MEDIA_TYPE = 'application/vnd.dev.sigstore.bundle+json;version=0.2';
exports.BUNDLE_V03_LEGACY_MEDIA_TYPE = 'application/vnd.dev.sigstore.bundle+json;version=0.3';
exports.BUNDLE_V03_MEDIA_TYPE = 'application/vnd.dev.sigstore.bundle.v0.3+json';
// Type guards for bundle variants.
function isBundleWithCertificateChain(b) {
    return b.verificationMaterial.content.$case === 'x509CertificateChain';
}
exports.isBundleWithCertificateChain = isBundleWithCertificateChain;
function isBundleWithPublicKey(b) {
    return b.verificationMaterial.content.$case === 'publicKey';
}
exports.isBundleWithPublicKey = isBundleWithPublicKey;
function isBundleWithMessageSignature(b) {
    return b.content.$case === 'messageSignature';
}
exports.isBundleWithMessageSignature = isBundleWithMessageSignature;
function isBundleWithDsseEnvelope(b) {
    return b.content.$case === 'dsseEnvelope';
}
exports.isBundleWithDsseEnvelope = isBundleWithDsseEnvelope;
