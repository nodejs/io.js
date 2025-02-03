"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.Input = exports.Artifact = exports.ArtifactVerificationOptions_ObserverTimestampOptions = exports.ArtifactVerificationOptions_TlogIntegratedTimestampOptions = exports.ArtifactVerificationOptions_TimestampAuthorityOptions = exports.ArtifactVerificationOptions_CtlogOptions = exports.ArtifactVerificationOptions_TlogOptions = exports.ArtifactVerificationOptions = exports.PublicKeyIdentities = exports.CertificateIdentities = exports.CertificateIdentity = void 0;
/* eslint-disable */
const sigstore_bundle_1 = require("./sigstore_bundle");
const sigstore_common_1 = require("./sigstore_common");
const sigstore_trustroot_1 = require("./sigstore_trustroot");
function createBaseCertificateIdentity() {
    return { issuer: "", san: undefined, oids: [] };
}
exports.CertificateIdentity = {
    fromJSON(object) {
        return {
            issuer: isSet(object.issuer) ? String(object.issuer) : "",
            san: isSet(object.san) ? sigstore_common_1.SubjectAlternativeName.fromJSON(object.san) : undefined,
            oids: Array.isArray(object?.oids) ? object.oids.map((e) => sigstore_common_1.ObjectIdentifierValuePair.fromJSON(e)) : [],
        };
    },
    toJSON(message) {
        const obj = {};
        message.issuer !== undefined && (obj.issuer = message.issuer);
        message.san !== undefined && (obj.san = message.san ? sigstore_common_1.SubjectAlternativeName.toJSON(message.san) : undefined);
        if (message.oids) {
            obj.oids = message.oids.map((e) => e ? sigstore_common_1.ObjectIdentifierValuePair.toJSON(e) : undefined);
        }
        else {
            obj.oids = [];
        }
        return obj;
    },
};
function createBaseCertificateIdentities() {
    return { identities: [] };
}
exports.CertificateIdentities = {
    fromJSON(object) {
        return {
            identities: Array.isArray(object?.identities)
                ? object.identities.map((e) => exports.CertificateIdentity.fromJSON(e))
                : [],
        };
    },
    toJSON(message) {
        const obj = {};
        if (message.identities) {
            obj.identities = message.identities.map((e) => e ? exports.CertificateIdentity.toJSON(e) : undefined);
        }
        else {
            obj.identities = [];
        }
        return obj;
    },
};
function createBasePublicKeyIdentities() {
    return { publicKeys: [] };
}
exports.PublicKeyIdentities = {
    fromJSON(object) {
        return {
            publicKeys: Array.isArray(object?.publicKeys) ? object.publicKeys.map((e) => sigstore_common_1.PublicKey.fromJSON(e)) : [],
        };
    },
    toJSON(message) {
        const obj = {};
        if (message.publicKeys) {
            obj.publicKeys = message.publicKeys.map((e) => e ? sigstore_common_1.PublicKey.toJSON(e) : undefined);
        }
        else {
            obj.publicKeys = [];
        }
        return obj;
    },
};
function createBaseArtifactVerificationOptions() {
    return {
        signers: undefined,
        tlogOptions: undefined,
        ctlogOptions: undefined,
        tsaOptions: undefined,
        integratedTsOptions: undefined,
        observerOptions: undefined,
    };
}
exports.ArtifactVerificationOptions = {
    fromJSON(object) {
        return {
            signers: isSet(object.certificateIdentities)
                ? {
                    $case: "certificateIdentities",
                    certificateIdentities: exports.CertificateIdentities.fromJSON(object.certificateIdentities),
                }
                : isSet(object.publicKeys)
                    ? { $case: "publicKeys", publicKeys: exports.PublicKeyIdentities.fromJSON(object.publicKeys) }
                    : undefined,
            tlogOptions: isSet(object.tlogOptions)
                ? exports.ArtifactVerificationOptions_TlogOptions.fromJSON(object.tlogOptions)
                : undefined,
            ctlogOptions: isSet(object.ctlogOptions)
                ? exports.ArtifactVerificationOptions_CtlogOptions.fromJSON(object.ctlogOptions)
                : undefined,
            tsaOptions: isSet(object.tsaOptions)
                ? exports.ArtifactVerificationOptions_TimestampAuthorityOptions.fromJSON(object.tsaOptions)
                : undefined,
            integratedTsOptions: isSet(object.integratedTsOptions)
                ? exports.ArtifactVerificationOptions_TlogIntegratedTimestampOptions.fromJSON(object.integratedTsOptions)
                : undefined,
            observerOptions: isSet(object.observerOptions)
                ? exports.ArtifactVerificationOptions_ObserverTimestampOptions.fromJSON(object.observerOptions)
                : undefined,
        };
    },
    toJSON(message) {
        const obj = {};
        message.signers?.$case === "certificateIdentities" &&
            (obj.certificateIdentities = message.signers?.certificateIdentities
                ? exports.CertificateIdentities.toJSON(message.signers?.certificateIdentities)
                : undefined);
        message.signers?.$case === "publicKeys" && (obj.publicKeys = message.signers?.publicKeys
            ? exports.PublicKeyIdentities.toJSON(message.signers?.publicKeys)
            : undefined);
        message.tlogOptions !== undefined && (obj.tlogOptions = message.tlogOptions
            ? exports.ArtifactVerificationOptions_TlogOptions.toJSON(message.tlogOptions)
            : undefined);
        message.ctlogOptions !== undefined && (obj.ctlogOptions = message.ctlogOptions
            ? exports.ArtifactVerificationOptions_CtlogOptions.toJSON(message.ctlogOptions)
            : undefined);
        message.tsaOptions !== undefined && (obj.tsaOptions = message.tsaOptions
            ? exports.ArtifactVerificationOptions_TimestampAuthorityOptions.toJSON(message.tsaOptions)
            : undefined);
        message.integratedTsOptions !== undefined && (obj.integratedTsOptions = message.integratedTsOptions
            ? exports.ArtifactVerificationOptions_TlogIntegratedTimestampOptions.toJSON(message.integratedTsOptions)
            : undefined);
        message.observerOptions !== undefined && (obj.observerOptions = message.observerOptions
            ? exports.ArtifactVerificationOptions_ObserverTimestampOptions.toJSON(message.observerOptions)
            : undefined);
        return obj;
    },
};
function createBaseArtifactVerificationOptions_TlogOptions() {
    return { threshold: 0, performOnlineVerification: false, disable: false };
}
exports.ArtifactVerificationOptions_TlogOptions = {
    fromJSON(object) {
        return {
            threshold: isSet(object.threshold) ? Number(object.threshold) : 0,
            performOnlineVerification: isSet(object.performOnlineVerification)
                ? Boolean(object.performOnlineVerification)
                : false,
            disable: isSet(object.disable) ? Boolean(object.disable) : false,
        };
    },
    toJSON(message) {
        const obj = {};
        message.threshold !== undefined && (obj.threshold = Math.round(message.threshold));
        message.performOnlineVerification !== undefined &&
            (obj.performOnlineVerification = message.performOnlineVerification);
        message.disable !== undefined && (obj.disable = message.disable);
        return obj;
    },
};
function createBaseArtifactVerificationOptions_CtlogOptions() {
    return { threshold: 0, disable: false };
}
exports.ArtifactVerificationOptions_CtlogOptions = {
    fromJSON(object) {
        return {
            threshold: isSet(object.threshold) ? Number(object.threshold) : 0,
            disable: isSet(object.disable) ? Boolean(object.disable) : false,
        };
    },
    toJSON(message) {
        const obj = {};
        message.threshold !== undefined && (obj.threshold = Math.round(message.threshold));
        message.disable !== undefined && (obj.disable = message.disable);
        return obj;
    },
};
function createBaseArtifactVerificationOptions_TimestampAuthorityOptions() {
    return { threshold: 0, disable: false };
}
exports.ArtifactVerificationOptions_TimestampAuthorityOptions = {
    fromJSON(object) {
        return {
            threshold: isSet(object.threshold) ? Number(object.threshold) : 0,
            disable: isSet(object.disable) ? Boolean(object.disable) : false,
        };
    },
    toJSON(message) {
        const obj = {};
        message.threshold !== undefined && (obj.threshold = Math.round(message.threshold));
        message.disable !== undefined && (obj.disable = message.disable);
        return obj;
    },
};
function createBaseArtifactVerificationOptions_TlogIntegratedTimestampOptions() {
    return { threshold: 0, disable: false };
}
exports.ArtifactVerificationOptions_TlogIntegratedTimestampOptions = {
    fromJSON(object) {
        return {
            threshold: isSet(object.threshold) ? Number(object.threshold) : 0,
            disable: isSet(object.disable) ? Boolean(object.disable) : false,
        };
    },
    toJSON(message) {
        const obj = {};
        message.threshold !== undefined && (obj.threshold = Math.round(message.threshold));
        message.disable !== undefined && (obj.disable = message.disable);
        return obj;
    },
};
function createBaseArtifactVerificationOptions_ObserverTimestampOptions() {
    return { threshold: 0, disable: false };
}
exports.ArtifactVerificationOptions_ObserverTimestampOptions = {
    fromJSON(object) {
        return {
            threshold: isSet(object.threshold) ? Number(object.threshold) : 0,
            disable: isSet(object.disable) ? Boolean(object.disable) : false,
        };
    },
    toJSON(message) {
        const obj = {};
        message.threshold !== undefined && (obj.threshold = Math.round(message.threshold));
        message.disable !== undefined && (obj.disable = message.disable);
        return obj;
    },
};
function createBaseArtifact() {
    return { data: undefined };
}
exports.Artifact = {
    fromJSON(object) {
        return {
            data: isSet(object.artifactUri)
                ? { $case: "artifactUri", artifactUri: String(object.artifactUri) }
                : isSet(object.artifact)
                    ? { $case: "artifact", artifact: Buffer.from(bytesFromBase64(object.artifact)) }
                    : isSet(object.artifactDigest)
                        ? { $case: "artifactDigest", artifactDigest: sigstore_common_1.HashOutput.fromJSON(object.artifactDigest) }
                        : undefined,
        };
    },
    toJSON(message) {
        const obj = {};
        message.data?.$case === "artifactUri" && (obj.artifactUri = message.data?.artifactUri);
        message.data?.$case === "artifact" &&
            (obj.artifact = message.data?.artifact !== undefined ? base64FromBytes(message.data?.artifact) : undefined);
        message.data?.$case === "artifactDigest" &&
            (obj.artifactDigest = message.data?.artifactDigest ? sigstore_common_1.HashOutput.toJSON(message.data?.artifactDigest) : undefined);
        return obj;
    },
};
function createBaseInput() {
    return {
        artifactTrustRoot: undefined,
        artifactVerificationOptions: undefined,
        bundle: undefined,
        artifact: undefined,
    };
}
exports.Input = {
    fromJSON(object) {
        return {
            artifactTrustRoot: isSet(object.artifactTrustRoot) ? sigstore_trustroot_1.TrustedRoot.fromJSON(object.artifactTrustRoot) : undefined,
            artifactVerificationOptions: isSet(object.artifactVerificationOptions)
                ? exports.ArtifactVerificationOptions.fromJSON(object.artifactVerificationOptions)
                : undefined,
            bundle: isSet(object.bundle) ? sigstore_bundle_1.Bundle.fromJSON(object.bundle) : undefined,
            artifact: isSet(object.artifact) ? exports.Artifact.fromJSON(object.artifact) : undefined,
        };
    },
    toJSON(message) {
        const obj = {};
        message.artifactTrustRoot !== undefined &&
            (obj.artifactTrustRoot = message.artifactTrustRoot ? sigstore_trustroot_1.TrustedRoot.toJSON(message.artifactTrustRoot) : undefined);
        message.artifactVerificationOptions !== undefined &&
            (obj.artifactVerificationOptions = message.artifactVerificationOptions
                ? exports.ArtifactVerificationOptions.toJSON(message.artifactVerificationOptions)
                : undefined);
        message.bundle !== undefined && (obj.bundle = message.bundle ? sigstore_bundle_1.Bundle.toJSON(message.bundle) : undefined);
        message.artifact !== undefined && (obj.artifact = message.artifact ? exports.Artifact.toJSON(message.artifact) : undefined);
        return obj;
    },
};
var tsProtoGlobalThis = (() => {
    if (typeof globalThis !== "undefined") {
        return globalThis;
    }
    if (typeof self !== "undefined") {
        return self;
    }
    if (typeof window !== "undefined") {
        return window;
    }
    if (typeof global !== "undefined") {
        return global;
    }
    throw "Unable to locate global object";
})();
function bytesFromBase64(b64) {
    if (tsProtoGlobalThis.Buffer) {
        return Uint8Array.from(tsProtoGlobalThis.Buffer.from(b64, "base64"));
    }
    else {
        const bin = tsProtoGlobalThis.atob(b64);
        const arr = new Uint8Array(bin.length);
        for (let i = 0; i < bin.length; ++i) {
            arr[i] = bin.charCodeAt(i);
        }
        return arr;
    }
}
function base64FromBytes(arr) {
    if (tsProtoGlobalThis.Buffer) {
        return tsProtoGlobalThis.Buffer.from(arr).toString("base64");
    }
    else {
        const bin = [];
        arr.forEach((byte) => {
            bin.push(String.fromCharCode(byte));
        });
        return tsProtoGlobalThis.btoa(bin.join(""));
    }
}
function isSet(value) {
    return value !== null && value !== undefined;
}
