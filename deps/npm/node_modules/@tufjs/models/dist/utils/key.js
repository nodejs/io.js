"use strict";
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.getPublicKey = getPublicKey;
const crypto_1 = __importDefault(require("crypto"));
const error_1 = require("../error");
const oid_1 = require("./oid");
const ASN1_TAG_SEQUENCE = 0x30;
const ANS1_TAG_BIT_STRING = 0x03;
const NULL_BYTE = 0x00;
const OID_EDDSA = '1.3.101.112';
const OID_EC_PUBLIC_KEY = '1.2.840.10045.2.1';
const OID_EC_CURVE_P256V1 = '1.2.840.10045.3.1.7';
const PEM_HEADER = '-----BEGIN PUBLIC KEY-----';
function getPublicKey(keyInfo) {
    switch (keyInfo.keyType) {
        case 'rsa':
            return getRSAPublicKey(keyInfo);
        case 'ed25519':
            return getED25519PublicKey(keyInfo);
        case 'ecdsa':
        case 'ecdsa-sha2-nistp256':
        case 'ecdsa-sha2-nistp384':
            return getECDCSAPublicKey(keyInfo);
        default:
            throw new error_1.UnsupportedAlgorithmError(`Unsupported key type: ${keyInfo.keyType}`);
    }
}
function getRSAPublicKey(keyInfo) {
    // Only support PEM-encoded RSA keys
    if (!keyInfo.keyVal.startsWith(PEM_HEADER)) {
        throw new error_1.CryptoError('Invalid key format');
    }
    const key = crypto_1.default.createPublicKey(keyInfo.keyVal);
    switch (keyInfo.scheme) {
        case 'rsassa-pss-sha256':
            return {
                key: key,
                padding: crypto_1.default.constants.RSA_PKCS1_PSS_PADDING,
            };
        default:
            throw new error_1.UnsupportedAlgorithmError(`Unsupported RSA scheme: ${keyInfo.scheme}`);
    }
}
function getED25519PublicKey(keyInfo) {
    let key;
    // If key is already PEM-encoded we can just parse it
    if (keyInfo.keyVal.startsWith(PEM_HEADER)) {
        key = crypto_1.default.createPublicKey(keyInfo.keyVal);
    }
    else {
        // If key is not PEM-encoded it had better be hex
        if (!isHex(keyInfo.keyVal)) {
            throw new error_1.CryptoError('Invalid key format');
        }
        key = crypto_1.default.createPublicKey({
            key: ed25519.hexToDER(keyInfo.keyVal),
            format: 'der',
            type: 'spki',
        });
    }
    return { key };
}
function getECDCSAPublicKey(keyInfo) {
    let key;
    // If key is already PEM-encoded we can just parse it
    if (keyInfo.keyVal.startsWith(PEM_HEADER)) {
        key = crypto_1.default.createPublicKey(keyInfo.keyVal);
    }
    else {
        // If key is not PEM-encoded it had better be hex
        if (!isHex(keyInfo.keyVal)) {
            throw new error_1.CryptoError('Invalid key format');
        }
        key = crypto_1.default.createPublicKey({
            key: ecdsa.hexToDER(keyInfo.keyVal),
            format: 'der',
            type: 'spki',
        });
    }
    return { key };
}
const ed25519 = {
    // Translates a hex key into a crypto KeyObject
    // https://keygen.sh/blog/how-to-use-hexadecimal-ed25519-keys-in-node/
    hexToDER: (hex) => {
        const key = Buffer.from(hex, 'hex');
        const oid = (0, oid_1.encodeOIDString)(OID_EDDSA);
        // Create a byte sequence containing the OID and key
        const elements = Buffer.concat([
            Buffer.concat([
                Buffer.from([ASN1_TAG_SEQUENCE]),
                Buffer.from([oid.length]),
                oid,
            ]),
            Buffer.concat([
                Buffer.from([ANS1_TAG_BIT_STRING]),
                Buffer.from([key.length + 1]),
                Buffer.from([NULL_BYTE]),
                key,
            ]),
        ]);
        // Wrap up by creating a sequence of elements
        const der = Buffer.concat([
            Buffer.from([ASN1_TAG_SEQUENCE]),
            Buffer.from([elements.length]),
            elements,
        ]);
        return der;
    },
};
const ecdsa = {
    hexToDER: (hex) => {
        const key = Buffer.from(hex, 'hex');
        const bitString = Buffer.concat([
            Buffer.from([ANS1_TAG_BIT_STRING]),
            Buffer.from([key.length + 1]),
            Buffer.from([NULL_BYTE]),
            key,
        ]);
        const oids = Buffer.concat([
            (0, oid_1.encodeOIDString)(OID_EC_PUBLIC_KEY),
            (0, oid_1.encodeOIDString)(OID_EC_CURVE_P256V1),
        ]);
        const oidSequence = Buffer.concat([
            Buffer.from([ASN1_TAG_SEQUENCE]),
            Buffer.from([oids.length]),
            oids,
        ]);
        // Wrap up by creating a sequence of elements
        const der = Buffer.concat([
            Buffer.from([ASN1_TAG_SEQUENCE]),
            Buffer.from([oidSequence.length + bitString.length]),
            oidSequence,
            bitString,
        ]);
        return der;
    },
};
const isHex = (key) => /^[0-9a-fA-F]+$/.test(key);
