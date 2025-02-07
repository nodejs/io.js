"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.TimeRange = exports.X509CertificateChain = exports.SubjectAlternativeName = exports.X509Certificate = exports.DistinguishedName = exports.ObjectIdentifierValuePair = exports.ObjectIdentifier = exports.PublicKeyIdentifier = exports.PublicKey = exports.RFC3161SignedTimestamp = exports.LogId = exports.MessageSignature = exports.HashOutput = exports.SubjectAlternativeNameType = exports.PublicKeyDetails = exports.HashAlgorithm = void 0;
exports.hashAlgorithmFromJSON = hashAlgorithmFromJSON;
exports.hashAlgorithmToJSON = hashAlgorithmToJSON;
exports.publicKeyDetailsFromJSON = publicKeyDetailsFromJSON;
exports.publicKeyDetailsToJSON = publicKeyDetailsToJSON;
exports.subjectAlternativeNameTypeFromJSON = subjectAlternativeNameTypeFromJSON;
exports.subjectAlternativeNameTypeToJSON = subjectAlternativeNameTypeToJSON;
/* eslint-disable */
const timestamp_1 = require("./google/protobuf/timestamp");
/**
 * Only a subset of the secure hash standard algorithms are supported.
 * See <https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.180-4.pdf> for more
 * details.
 * UNSPECIFIED SHOULD not be used, primary reason for inclusion is to force
 * any proto JSON serialization to emit the used hash algorithm, as default
 * option is to *omit* the default value of an enum (which is the first
 * value, represented by '0'.
 */
var HashAlgorithm;
(function (HashAlgorithm) {
    HashAlgorithm[HashAlgorithm["HASH_ALGORITHM_UNSPECIFIED"] = 0] = "HASH_ALGORITHM_UNSPECIFIED";
    HashAlgorithm[HashAlgorithm["SHA2_256"] = 1] = "SHA2_256";
    HashAlgorithm[HashAlgorithm["SHA2_384"] = 2] = "SHA2_384";
    HashAlgorithm[HashAlgorithm["SHA2_512"] = 3] = "SHA2_512";
    HashAlgorithm[HashAlgorithm["SHA3_256"] = 4] = "SHA3_256";
    HashAlgorithm[HashAlgorithm["SHA3_384"] = 5] = "SHA3_384";
})(HashAlgorithm || (exports.HashAlgorithm = HashAlgorithm = {}));
function hashAlgorithmFromJSON(object) {
    switch (object) {
        case 0:
        case "HASH_ALGORITHM_UNSPECIFIED":
            return HashAlgorithm.HASH_ALGORITHM_UNSPECIFIED;
        case 1:
        case "SHA2_256":
            return HashAlgorithm.SHA2_256;
        case 2:
        case "SHA2_384":
            return HashAlgorithm.SHA2_384;
        case 3:
        case "SHA2_512":
            return HashAlgorithm.SHA2_512;
        case 4:
        case "SHA3_256":
            return HashAlgorithm.SHA3_256;
        case 5:
        case "SHA3_384":
            return HashAlgorithm.SHA3_384;
        default:
            throw new tsProtoGlobalThis.Error("Unrecognized enum value " + object + " for enum HashAlgorithm");
    }
}
function hashAlgorithmToJSON(object) {
    switch (object) {
        case HashAlgorithm.HASH_ALGORITHM_UNSPECIFIED:
            return "HASH_ALGORITHM_UNSPECIFIED";
        case HashAlgorithm.SHA2_256:
            return "SHA2_256";
        case HashAlgorithm.SHA2_384:
            return "SHA2_384";
        case HashAlgorithm.SHA2_512:
            return "SHA2_512";
        case HashAlgorithm.SHA3_256:
            return "SHA3_256";
        case HashAlgorithm.SHA3_384:
            return "SHA3_384";
        default:
            throw new tsProtoGlobalThis.Error("Unrecognized enum value " + object + " for enum HashAlgorithm");
    }
}
/**
 * Details of a specific public key, capturing the the key encoding method,
 * and signature algorithm.
 *
 * PublicKeyDetails captures the public key/hash algorithm combinations
 * recommended in the Sigstore ecosystem.
 *
 * This is modelled as a linear set as we want to provide a small number of
 * opinionated options instead of allowing every possible permutation.
 *
 * Any changes to this enum MUST be reflected in the algorithm registry.
 * See: docs/algorithm-registry.md
 *
 * To avoid the possibility of contradicting formats such as PKCS1 with
 * ED25519 the valid permutations are listed as a linear set instead of a
 * cartesian set (i.e one combined variable instead of two, one for encoding
 * and one for the signature algorithm).
 */
var PublicKeyDetails;
(function (PublicKeyDetails) {
    PublicKeyDetails[PublicKeyDetails["PUBLIC_KEY_DETAILS_UNSPECIFIED"] = 0] = "PUBLIC_KEY_DETAILS_UNSPECIFIED";
    /**
     * PKCS1_RSA_PKCS1V5 - RSA
     *
     * @deprecated
     */
    PublicKeyDetails[PublicKeyDetails["PKCS1_RSA_PKCS1V5"] = 1] = "PKCS1_RSA_PKCS1V5";
    /**
     * PKCS1_RSA_PSS - See RFC8017
     *
     * @deprecated
     */
    PublicKeyDetails[PublicKeyDetails["PKCS1_RSA_PSS"] = 2] = "PKCS1_RSA_PSS";
    /** @deprecated */
    PublicKeyDetails[PublicKeyDetails["PKIX_RSA_PKCS1V5"] = 3] = "PKIX_RSA_PKCS1V5";
    /** @deprecated */
    PublicKeyDetails[PublicKeyDetails["PKIX_RSA_PSS"] = 4] = "PKIX_RSA_PSS";
    /** PKIX_RSA_PKCS1V15_2048_SHA256 - RSA public key in PKIX format, PKCS#1v1.5 signature */
    PublicKeyDetails[PublicKeyDetails["PKIX_RSA_PKCS1V15_2048_SHA256"] = 9] = "PKIX_RSA_PKCS1V15_2048_SHA256";
    PublicKeyDetails[PublicKeyDetails["PKIX_RSA_PKCS1V15_3072_SHA256"] = 10] = "PKIX_RSA_PKCS1V15_3072_SHA256";
    PublicKeyDetails[PublicKeyDetails["PKIX_RSA_PKCS1V15_4096_SHA256"] = 11] = "PKIX_RSA_PKCS1V15_4096_SHA256";
    /** PKIX_RSA_PSS_2048_SHA256 - RSA public key in PKIX format, RSASSA-PSS signature */
    PublicKeyDetails[PublicKeyDetails["PKIX_RSA_PSS_2048_SHA256"] = 16] = "PKIX_RSA_PSS_2048_SHA256";
    PublicKeyDetails[PublicKeyDetails["PKIX_RSA_PSS_3072_SHA256"] = 17] = "PKIX_RSA_PSS_3072_SHA256";
    PublicKeyDetails[PublicKeyDetails["PKIX_RSA_PSS_4096_SHA256"] = 18] = "PKIX_RSA_PSS_4096_SHA256";
    /**
     * PKIX_ECDSA_P256_HMAC_SHA_256 - ECDSA
     *
     * @deprecated
     */
    PublicKeyDetails[PublicKeyDetails["PKIX_ECDSA_P256_HMAC_SHA_256"] = 6] = "PKIX_ECDSA_P256_HMAC_SHA_256";
    /** PKIX_ECDSA_P256_SHA_256 - See NIST FIPS 186-4 */
    PublicKeyDetails[PublicKeyDetails["PKIX_ECDSA_P256_SHA_256"] = 5] = "PKIX_ECDSA_P256_SHA_256";
    PublicKeyDetails[PublicKeyDetails["PKIX_ECDSA_P384_SHA_384"] = 12] = "PKIX_ECDSA_P384_SHA_384";
    PublicKeyDetails[PublicKeyDetails["PKIX_ECDSA_P521_SHA_512"] = 13] = "PKIX_ECDSA_P521_SHA_512";
    /** PKIX_ED25519 - Ed 25519 */
    PublicKeyDetails[PublicKeyDetails["PKIX_ED25519"] = 7] = "PKIX_ED25519";
    PublicKeyDetails[PublicKeyDetails["PKIX_ED25519_PH"] = 8] = "PKIX_ED25519_PH";
    /**
     * LMS_SHA256 - LMS and LM-OTS
     *
     * These keys and signatures may be used by private Sigstore
     * deployments, but are not currently supported by the public
     * good instance.
     *
     * USER WARNING: LMS and LM-OTS are both stateful signature schemes.
     * Using them correctly requires discretion and careful consideration
     * to ensure that individual secret keys are not used more than once.
     * In addition, LM-OTS is a single-use scheme, meaning that it
     * MUST NOT be used for more than one signature per LM-OTS key.
     * If you cannot maintain these invariants, you MUST NOT use these
     * schemes.
     */
    PublicKeyDetails[PublicKeyDetails["LMS_SHA256"] = 14] = "LMS_SHA256";
    PublicKeyDetails[PublicKeyDetails["LMOTS_SHA256"] = 15] = "LMOTS_SHA256";
})(PublicKeyDetails || (exports.PublicKeyDetails = PublicKeyDetails = {}));
function publicKeyDetailsFromJSON(object) {
    switch (object) {
        case 0:
        case "PUBLIC_KEY_DETAILS_UNSPECIFIED":
            return PublicKeyDetails.PUBLIC_KEY_DETAILS_UNSPECIFIED;
        case 1:
        case "PKCS1_RSA_PKCS1V5":
            return PublicKeyDetails.PKCS1_RSA_PKCS1V5;
        case 2:
        case "PKCS1_RSA_PSS":
            return PublicKeyDetails.PKCS1_RSA_PSS;
        case 3:
        case "PKIX_RSA_PKCS1V5":
            return PublicKeyDetails.PKIX_RSA_PKCS1V5;
        case 4:
        case "PKIX_RSA_PSS":
            return PublicKeyDetails.PKIX_RSA_PSS;
        case 9:
        case "PKIX_RSA_PKCS1V15_2048_SHA256":
            return PublicKeyDetails.PKIX_RSA_PKCS1V15_2048_SHA256;
        case 10:
        case "PKIX_RSA_PKCS1V15_3072_SHA256":
            return PublicKeyDetails.PKIX_RSA_PKCS1V15_3072_SHA256;
        case 11:
        case "PKIX_RSA_PKCS1V15_4096_SHA256":
            return PublicKeyDetails.PKIX_RSA_PKCS1V15_4096_SHA256;
        case 16:
        case "PKIX_RSA_PSS_2048_SHA256":
            return PublicKeyDetails.PKIX_RSA_PSS_2048_SHA256;
        case 17:
        case "PKIX_RSA_PSS_3072_SHA256":
            return PublicKeyDetails.PKIX_RSA_PSS_3072_SHA256;
        case 18:
        case "PKIX_RSA_PSS_4096_SHA256":
            return PublicKeyDetails.PKIX_RSA_PSS_4096_SHA256;
        case 6:
        case "PKIX_ECDSA_P256_HMAC_SHA_256":
            return PublicKeyDetails.PKIX_ECDSA_P256_HMAC_SHA_256;
        case 5:
        case "PKIX_ECDSA_P256_SHA_256":
            return PublicKeyDetails.PKIX_ECDSA_P256_SHA_256;
        case 12:
        case "PKIX_ECDSA_P384_SHA_384":
            return PublicKeyDetails.PKIX_ECDSA_P384_SHA_384;
        case 13:
        case "PKIX_ECDSA_P521_SHA_512":
            return PublicKeyDetails.PKIX_ECDSA_P521_SHA_512;
        case 7:
        case "PKIX_ED25519":
            return PublicKeyDetails.PKIX_ED25519;
        case 8:
        case "PKIX_ED25519_PH":
            return PublicKeyDetails.PKIX_ED25519_PH;
        case 14:
        case "LMS_SHA256":
            return PublicKeyDetails.LMS_SHA256;
        case 15:
        case "LMOTS_SHA256":
            return PublicKeyDetails.LMOTS_SHA256;
        default:
            throw new tsProtoGlobalThis.Error("Unrecognized enum value " + object + " for enum PublicKeyDetails");
    }
}
function publicKeyDetailsToJSON(object) {
    switch (object) {
        case PublicKeyDetails.PUBLIC_KEY_DETAILS_UNSPECIFIED:
            return "PUBLIC_KEY_DETAILS_UNSPECIFIED";
        case PublicKeyDetails.PKCS1_RSA_PKCS1V5:
            return "PKCS1_RSA_PKCS1V5";
        case PublicKeyDetails.PKCS1_RSA_PSS:
            return "PKCS1_RSA_PSS";
        case PublicKeyDetails.PKIX_RSA_PKCS1V5:
            return "PKIX_RSA_PKCS1V5";
        case PublicKeyDetails.PKIX_RSA_PSS:
            return "PKIX_RSA_PSS";
        case PublicKeyDetails.PKIX_RSA_PKCS1V15_2048_SHA256:
            return "PKIX_RSA_PKCS1V15_2048_SHA256";
        case PublicKeyDetails.PKIX_RSA_PKCS1V15_3072_SHA256:
            return "PKIX_RSA_PKCS1V15_3072_SHA256";
        case PublicKeyDetails.PKIX_RSA_PKCS1V15_4096_SHA256:
            return "PKIX_RSA_PKCS1V15_4096_SHA256";
        case PublicKeyDetails.PKIX_RSA_PSS_2048_SHA256:
            return "PKIX_RSA_PSS_2048_SHA256";
        case PublicKeyDetails.PKIX_RSA_PSS_3072_SHA256:
            return "PKIX_RSA_PSS_3072_SHA256";
        case PublicKeyDetails.PKIX_RSA_PSS_4096_SHA256:
            return "PKIX_RSA_PSS_4096_SHA256";
        case PublicKeyDetails.PKIX_ECDSA_P256_HMAC_SHA_256:
            return "PKIX_ECDSA_P256_HMAC_SHA_256";
        case PublicKeyDetails.PKIX_ECDSA_P256_SHA_256:
            return "PKIX_ECDSA_P256_SHA_256";
        case PublicKeyDetails.PKIX_ECDSA_P384_SHA_384:
            return "PKIX_ECDSA_P384_SHA_384";
        case PublicKeyDetails.PKIX_ECDSA_P521_SHA_512:
            return "PKIX_ECDSA_P521_SHA_512";
        case PublicKeyDetails.PKIX_ED25519:
            return "PKIX_ED25519";
        case PublicKeyDetails.PKIX_ED25519_PH:
            return "PKIX_ED25519_PH";
        case PublicKeyDetails.LMS_SHA256:
            return "LMS_SHA256";
        case PublicKeyDetails.LMOTS_SHA256:
            return "LMOTS_SHA256";
        default:
            throw new tsProtoGlobalThis.Error("Unrecognized enum value " + object + " for enum PublicKeyDetails");
    }
}
var SubjectAlternativeNameType;
(function (SubjectAlternativeNameType) {
    SubjectAlternativeNameType[SubjectAlternativeNameType["SUBJECT_ALTERNATIVE_NAME_TYPE_UNSPECIFIED"] = 0] = "SUBJECT_ALTERNATIVE_NAME_TYPE_UNSPECIFIED";
    SubjectAlternativeNameType[SubjectAlternativeNameType["EMAIL"] = 1] = "EMAIL";
    SubjectAlternativeNameType[SubjectAlternativeNameType["URI"] = 2] = "URI";
    /**
     * OTHER_NAME - OID 1.3.6.1.4.1.57264.1.7
     * See https://github.com/sigstore/fulcio/blob/main/docs/oid-info.md#1361415726417--othername-san
     * for more details.
     */
    SubjectAlternativeNameType[SubjectAlternativeNameType["OTHER_NAME"] = 3] = "OTHER_NAME";
})(SubjectAlternativeNameType || (exports.SubjectAlternativeNameType = SubjectAlternativeNameType = {}));
function subjectAlternativeNameTypeFromJSON(object) {
    switch (object) {
        case 0:
        case "SUBJECT_ALTERNATIVE_NAME_TYPE_UNSPECIFIED":
            return SubjectAlternativeNameType.SUBJECT_ALTERNATIVE_NAME_TYPE_UNSPECIFIED;
        case 1:
        case "EMAIL":
            return SubjectAlternativeNameType.EMAIL;
        case 2:
        case "URI":
            return SubjectAlternativeNameType.URI;
        case 3:
        case "OTHER_NAME":
            return SubjectAlternativeNameType.OTHER_NAME;
        default:
            throw new tsProtoGlobalThis.Error("Unrecognized enum value " + object + " for enum SubjectAlternativeNameType");
    }
}
function subjectAlternativeNameTypeToJSON(object) {
    switch (object) {
        case SubjectAlternativeNameType.SUBJECT_ALTERNATIVE_NAME_TYPE_UNSPECIFIED:
            return "SUBJECT_ALTERNATIVE_NAME_TYPE_UNSPECIFIED";
        case SubjectAlternativeNameType.EMAIL:
            return "EMAIL";
        case SubjectAlternativeNameType.URI:
            return "URI";
        case SubjectAlternativeNameType.OTHER_NAME:
            return "OTHER_NAME";
        default:
            throw new tsProtoGlobalThis.Error("Unrecognized enum value " + object + " for enum SubjectAlternativeNameType");
    }
}
function createBaseHashOutput() {
    return { algorithm: 0, digest: Buffer.alloc(0) };
}
exports.HashOutput = {
    fromJSON(object) {
        return {
            algorithm: isSet(object.algorithm) ? hashAlgorithmFromJSON(object.algorithm) : 0,
            digest: isSet(object.digest) ? Buffer.from(bytesFromBase64(object.digest)) : Buffer.alloc(0),
        };
    },
    toJSON(message) {
        const obj = {};
        message.algorithm !== undefined && (obj.algorithm = hashAlgorithmToJSON(message.algorithm));
        message.digest !== undefined &&
            (obj.digest = base64FromBytes(message.digest !== undefined ? message.digest : Buffer.alloc(0)));
        return obj;
    },
};
function createBaseMessageSignature() {
    return { messageDigest: undefined, signature: Buffer.alloc(0) };
}
exports.MessageSignature = {
    fromJSON(object) {
        return {
            messageDigest: isSet(object.messageDigest) ? exports.HashOutput.fromJSON(object.messageDigest) : undefined,
            signature: isSet(object.signature) ? Buffer.from(bytesFromBase64(object.signature)) : Buffer.alloc(0),
        };
    },
    toJSON(message) {
        const obj = {};
        message.messageDigest !== undefined &&
            (obj.messageDigest = message.messageDigest ? exports.HashOutput.toJSON(message.messageDigest) : undefined);
        message.signature !== undefined &&
            (obj.signature = base64FromBytes(message.signature !== undefined ? message.signature : Buffer.alloc(0)));
        return obj;
    },
};
function createBaseLogId() {
    return { keyId: Buffer.alloc(0) };
}
exports.LogId = {
    fromJSON(object) {
        return { keyId: isSet(object.keyId) ? Buffer.from(bytesFromBase64(object.keyId)) : Buffer.alloc(0) };
    },
    toJSON(message) {
        const obj = {};
        message.keyId !== undefined &&
            (obj.keyId = base64FromBytes(message.keyId !== undefined ? message.keyId : Buffer.alloc(0)));
        return obj;
    },
};
function createBaseRFC3161SignedTimestamp() {
    return { signedTimestamp: Buffer.alloc(0) };
}
exports.RFC3161SignedTimestamp = {
    fromJSON(object) {
        return {
            signedTimestamp: isSet(object.signedTimestamp)
                ? Buffer.from(bytesFromBase64(object.signedTimestamp))
                : Buffer.alloc(0),
        };
    },
    toJSON(message) {
        const obj = {};
        message.signedTimestamp !== undefined &&
            (obj.signedTimestamp = base64FromBytes(message.signedTimestamp !== undefined ? message.signedTimestamp : Buffer.alloc(0)));
        return obj;
    },
};
function createBasePublicKey() {
    return { rawBytes: undefined, keyDetails: 0, validFor: undefined };
}
exports.PublicKey = {
    fromJSON(object) {
        return {
            rawBytes: isSet(object.rawBytes) ? Buffer.from(bytesFromBase64(object.rawBytes)) : undefined,
            keyDetails: isSet(object.keyDetails) ? publicKeyDetailsFromJSON(object.keyDetails) : 0,
            validFor: isSet(object.validFor) ? exports.TimeRange.fromJSON(object.validFor) : undefined,
        };
    },
    toJSON(message) {
        const obj = {};
        message.rawBytes !== undefined &&
            (obj.rawBytes = message.rawBytes !== undefined ? base64FromBytes(message.rawBytes) : undefined);
        message.keyDetails !== undefined && (obj.keyDetails = publicKeyDetailsToJSON(message.keyDetails));
        message.validFor !== undefined &&
            (obj.validFor = message.validFor ? exports.TimeRange.toJSON(message.validFor) : undefined);
        return obj;
    },
};
function createBasePublicKeyIdentifier() {
    return { hint: "" };
}
exports.PublicKeyIdentifier = {
    fromJSON(object) {
        return { hint: isSet(object.hint) ? String(object.hint) : "" };
    },
    toJSON(message) {
        const obj = {};
        message.hint !== undefined && (obj.hint = message.hint);
        return obj;
    },
};
function createBaseObjectIdentifier() {
    return { id: [] };
}
exports.ObjectIdentifier = {
    fromJSON(object) {
        return { id: Array.isArray(object?.id) ? object.id.map((e) => Number(e)) : [] };
    },
    toJSON(message) {
        const obj = {};
        if (message.id) {
            obj.id = message.id.map((e) => Math.round(e));
        }
        else {
            obj.id = [];
        }
        return obj;
    },
};
function createBaseObjectIdentifierValuePair() {
    return { oid: undefined, value: Buffer.alloc(0) };
}
exports.ObjectIdentifierValuePair = {
    fromJSON(object) {
        return {
            oid: isSet(object.oid) ? exports.ObjectIdentifier.fromJSON(object.oid) : undefined,
            value: isSet(object.value) ? Buffer.from(bytesFromBase64(object.value)) : Buffer.alloc(0),
        };
    },
    toJSON(message) {
        const obj = {};
        message.oid !== undefined && (obj.oid = message.oid ? exports.ObjectIdentifier.toJSON(message.oid) : undefined);
        message.value !== undefined &&
            (obj.value = base64FromBytes(message.value !== undefined ? message.value : Buffer.alloc(0)));
        return obj;
    },
};
function createBaseDistinguishedName() {
    return { organization: "", commonName: "" };
}
exports.DistinguishedName = {
    fromJSON(object) {
        return {
            organization: isSet(object.organization) ? String(object.organization) : "",
            commonName: isSet(object.commonName) ? String(object.commonName) : "",
        };
    },
    toJSON(message) {
        const obj = {};
        message.organization !== undefined && (obj.organization = message.organization);
        message.commonName !== undefined && (obj.commonName = message.commonName);
        return obj;
    },
};
function createBaseX509Certificate() {
    return { rawBytes: Buffer.alloc(0) };
}
exports.X509Certificate = {
    fromJSON(object) {
        return { rawBytes: isSet(object.rawBytes) ? Buffer.from(bytesFromBase64(object.rawBytes)) : Buffer.alloc(0) };
    },
    toJSON(message) {
        const obj = {};
        message.rawBytes !== undefined &&
            (obj.rawBytes = base64FromBytes(message.rawBytes !== undefined ? message.rawBytes : Buffer.alloc(0)));
        return obj;
    },
};
function createBaseSubjectAlternativeName() {
    return { type: 0, identity: undefined };
}
exports.SubjectAlternativeName = {
    fromJSON(object) {
        return {
            type: isSet(object.type) ? subjectAlternativeNameTypeFromJSON(object.type) : 0,
            identity: isSet(object.regexp)
                ? { $case: "regexp", regexp: String(object.regexp) }
                : isSet(object.value)
                    ? { $case: "value", value: String(object.value) }
                    : undefined,
        };
    },
    toJSON(message) {
        const obj = {};
        message.type !== undefined && (obj.type = subjectAlternativeNameTypeToJSON(message.type));
        message.identity?.$case === "regexp" && (obj.regexp = message.identity?.regexp);
        message.identity?.$case === "value" && (obj.value = message.identity?.value);
        return obj;
    },
};
function createBaseX509CertificateChain() {
    return { certificates: [] };
}
exports.X509CertificateChain = {
    fromJSON(object) {
        return {
            certificates: Array.isArray(object?.certificates)
                ? object.certificates.map((e) => exports.X509Certificate.fromJSON(e))
                : [],
        };
    },
    toJSON(message) {
        const obj = {};
        if (message.certificates) {
            obj.certificates = message.certificates.map((e) => e ? exports.X509Certificate.toJSON(e) : undefined);
        }
        else {
            obj.certificates = [];
        }
        return obj;
    },
};
function createBaseTimeRange() {
    return { start: undefined, end: undefined };
}
exports.TimeRange = {
    fromJSON(object) {
        return {
            start: isSet(object.start) ? fromJsonTimestamp(object.start) : undefined,
            end: isSet(object.end) ? fromJsonTimestamp(object.end) : undefined,
        };
    },
    toJSON(message) {
        const obj = {};
        message.start !== undefined && (obj.start = message.start.toISOString());
        message.end !== undefined && (obj.end = message.end.toISOString());
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
function fromTimestamp(t) {
    let millis = Number(t.seconds) * 1_000;
    millis += t.nanos / 1_000_000;
    return new Date(millis);
}
function fromJsonTimestamp(o) {
    if (o instanceof Date) {
        return o;
    }
    else if (typeof o === "string") {
        return new Date(o);
    }
    else {
        return fromTimestamp(timestamp_1.Timestamp.fromJSON(o));
    }
}
function isSet(value) {
    return value !== null && value !== undefined;
}
