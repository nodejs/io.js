"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.verifyRFC3161Timestamp = void 0;
const core_1 = require("@sigstore/core");
const error_1 = require("../error");
const certificate_1 = require("../key/certificate");
const trust_1 = require("../trust");
function verifyRFC3161Timestamp(timestamp, data, timestampAuthorities) {
    const signingTime = timestamp.signingTime;
    // Filter for CAs which were valid at the time of signing
    timestampAuthorities = (0, trust_1.filterCertAuthorities)(timestampAuthorities, {
        start: signingTime,
        end: signingTime,
    });
    // Filter for CAs which match serial and issuer embedded in the timestamp
    timestampAuthorities = filterCAsBySerialAndIssuer(timestampAuthorities, {
        serialNumber: timestamp.signerSerialNumber,
        issuer: timestamp.signerIssuer,
    });
    // Check that we can verify the timestamp with AT LEAST ONE of the remaining
    // CAs
    const verified = timestampAuthorities.some((ca) => {
        try {
            verifyTimestampForCA(timestamp, data, ca);
            return true;
        }
        catch (e) {
            return false;
        }
    });
    if (!verified) {
        throw new error_1.VerificationError({
            code: 'TIMESTAMP_ERROR',
            message: 'timestamp could not be verified',
        });
    }
}
exports.verifyRFC3161Timestamp = verifyRFC3161Timestamp;
function verifyTimestampForCA(timestamp, data, ca) {
    const [leaf, ...cas] = ca.certChain;
    const signingKey = core_1.crypto.createPublicKey(leaf.publicKey);
    const signingTime = timestamp.signingTime;
    // Verify the certificate chain for the provided CA
    try {
        new certificate_1.CertificateChainVerifier({
            untrustedCert: leaf,
            trustedCerts: cas,
        }).verify();
    }
    catch (e) {
        throw new error_1.VerificationError({
            code: 'TIMESTAMP_ERROR',
            message: 'invalid certificate chain',
        });
    }
    // Check that all of the CA certs were valid at the time of signing
    const validAtSigningTime = ca.certChain.every((cert) => cert.validForDate(signingTime));
    if (!validAtSigningTime) {
        throw new error_1.VerificationError({
            code: 'TIMESTAMP_ERROR',
            message: 'timestamp was signed with an expired certificate',
        });
    }
    // Check that the signing certificate's key can be used to verify the
    // timestamp signature.
    timestamp.verify(data, signingKey);
}
// Filters the list of CAs to those which have a leaf signing certificate which
// matches the given serial number and issuer.
function filterCAsBySerialAndIssuer(timestampAuthorities, criteria) {
    return timestampAuthorities.filter((ca) => ca.certChain.length > 0 &&
        core_1.crypto.bufferEqual(ca.certChain[0].serialNumber, criteria.serialNumber) &&
        core_1.crypto.bufferEqual(ca.certChain[0].issuer, criteria.issuer));
}
