"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.toSignedEntity = toSignedEntity;
exports.signatureContent = signatureContent;
const core_1 = require("@sigstore/core");
const dsse_1 = require("./dsse");
const message_1 = require("./message");
function toSignedEntity(bundle, artifact) {
    const { tlogEntries, timestampVerificationData } = bundle.verificationMaterial;
    const timestamps = [];
    for (const entry of tlogEntries) {
        timestamps.push({
            $case: 'transparency-log',
            tlogEntry: entry,
        });
    }
    for (const ts of timestampVerificationData?.rfc3161Timestamps ?? []) {
        timestamps.push({
            $case: 'timestamp-authority',
            timestamp: core_1.RFC3161Timestamp.parse(ts.signedTimestamp),
        });
    }
    return {
        signature: signatureContent(bundle, artifact),
        key: key(bundle),
        tlogEntries,
        timestamps,
    };
}
function signatureContent(bundle, artifact) {
    switch (bundle.content.$case) {
        case 'dsseEnvelope':
            return new dsse_1.DSSESignatureContent(bundle.content.dsseEnvelope);
        case 'messageSignature':
            return new message_1.MessageSignatureContent(bundle.content.messageSignature, artifact);
    }
}
function key(bundle) {
    switch (bundle.verificationMaterial.content.$case) {
        case 'publicKey':
            return {
                $case: 'public-key',
                hint: bundle.verificationMaterial.content.publicKey.hint,
            };
        case 'x509CertificateChain':
            return {
                $case: 'certificate',
                certificate: core_1.X509Certificate.parse(bundle.verificationMaterial.content.x509CertificateChain
                    .certificates[0].rawBytes),
            };
        case 'certificate':
            return {
                $case: 'certificate',
                certificate: core_1.X509Certificate.parse(bundle.verificationMaterial.content.certificate.rawBytes),
            };
    }
}
