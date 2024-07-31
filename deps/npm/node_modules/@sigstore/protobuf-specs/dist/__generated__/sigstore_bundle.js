"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.Bundle = exports.VerificationMaterial = exports.TimestampVerificationData = void 0;
/* eslint-disable */
const envelope_1 = require("./envelope");
const sigstore_common_1 = require("./sigstore_common");
const sigstore_rekor_1 = require("./sigstore_rekor");
function createBaseTimestampVerificationData() {
    return { rfc3161Timestamps: [] };
}
exports.TimestampVerificationData = {
    fromJSON(object) {
        return {
            rfc3161Timestamps: Array.isArray(object?.rfc3161Timestamps)
                ? object.rfc3161Timestamps.map((e) => sigstore_common_1.RFC3161SignedTimestamp.fromJSON(e))
                : [],
        };
    },
    toJSON(message) {
        const obj = {};
        if (message.rfc3161Timestamps) {
            obj.rfc3161Timestamps = message.rfc3161Timestamps.map((e) => e ? sigstore_common_1.RFC3161SignedTimestamp.toJSON(e) : undefined);
        }
        else {
            obj.rfc3161Timestamps = [];
        }
        return obj;
    },
};
function createBaseVerificationMaterial() {
    return { content: undefined, tlogEntries: [], timestampVerificationData: undefined };
}
exports.VerificationMaterial = {
    fromJSON(object) {
        return {
            content: isSet(object.publicKey)
                ? { $case: "publicKey", publicKey: sigstore_common_1.PublicKeyIdentifier.fromJSON(object.publicKey) }
                : isSet(object.x509CertificateChain)
                    ? {
                        $case: "x509CertificateChain",
                        x509CertificateChain: sigstore_common_1.X509CertificateChain.fromJSON(object.x509CertificateChain),
                    }
                    : isSet(object.certificate)
                        ? { $case: "certificate", certificate: sigstore_common_1.X509Certificate.fromJSON(object.certificate) }
                        : undefined,
            tlogEntries: Array.isArray(object?.tlogEntries)
                ? object.tlogEntries.map((e) => sigstore_rekor_1.TransparencyLogEntry.fromJSON(e))
                : [],
            timestampVerificationData: isSet(object.timestampVerificationData)
                ? exports.TimestampVerificationData.fromJSON(object.timestampVerificationData)
                : undefined,
        };
    },
    toJSON(message) {
        const obj = {};
        message.content?.$case === "publicKey" &&
            (obj.publicKey = message.content?.publicKey ? sigstore_common_1.PublicKeyIdentifier.toJSON(message.content?.publicKey) : undefined);
        message.content?.$case === "x509CertificateChain" &&
            (obj.x509CertificateChain = message.content?.x509CertificateChain
                ? sigstore_common_1.X509CertificateChain.toJSON(message.content?.x509CertificateChain)
                : undefined);
        message.content?.$case === "certificate" &&
            (obj.certificate = message.content?.certificate
                ? sigstore_common_1.X509Certificate.toJSON(message.content?.certificate)
                : undefined);
        if (message.tlogEntries) {
            obj.tlogEntries = message.tlogEntries.map((e) => e ? sigstore_rekor_1.TransparencyLogEntry.toJSON(e) : undefined);
        }
        else {
            obj.tlogEntries = [];
        }
        message.timestampVerificationData !== undefined &&
            (obj.timestampVerificationData = message.timestampVerificationData
                ? exports.TimestampVerificationData.toJSON(message.timestampVerificationData)
                : undefined);
        return obj;
    },
};
function createBaseBundle() {
    return { mediaType: "", verificationMaterial: undefined, content: undefined };
}
exports.Bundle = {
    fromJSON(object) {
        return {
            mediaType: isSet(object.mediaType) ? String(object.mediaType) : "",
            verificationMaterial: isSet(object.verificationMaterial)
                ? exports.VerificationMaterial.fromJSON(object.verificationMaterial)
                : undefined,
            content: isSet(object.messageSignature)
                ? { $case: "messageSignature", messageSignature: sigstore_common_1.MessageSignature.fromJSON(object.messageSignature) }
                : isSet(object.dsseEnvelope)
                    ? { $case: "dsseEnvelope", dsseEnvelope: envelope_1.Envelope.fromJSON(object.dsseEnvelope) }
                    : undefined,
        };
    },
    toJSON(message) {
        const obj = {};
        message.mediaType !== undefined && (obj.mediaType = message.mediaType);
        message.verificationMaterial !== undefined && (obj.verificationMaterial = message.verificationMaterial
            ? exports.VerificationMaterial.toJSON(message.verificationMaterial)
            : undefined);
        message.content?.$case === "messageSignature" && (obj.messageSignature = message.content?.messageSignature
            ? sigstore_common_1.MessageSignature.toJSON(message.content?.messageSignature)
            : undefined);
        message.content?.$case === "dsseEnvelope" &&
            (obj.dsseEnvelope = message.content?.dsseEnvelope ? envelope_1.Envelope.toJSON(message.content?.dsseEnvelope) : undefined);
        return obj;
    },
};
function isSet(value) {
    return value !== null && value !== undefined;
}
