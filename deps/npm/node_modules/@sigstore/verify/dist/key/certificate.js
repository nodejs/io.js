"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.CertificateChainVerifier = void 0;
exports.verifyCertificateChain = verifyCertificateChain;
const error_1 = require("../error");
const trust_1 = require("../trust");
function verifyCertificateChain(leaf, certificateAuthorities) {
    // Filter list of trusted CAs to those which are valid for the given
    // leaf certificate.
    const cas = (0, trust_1.filterCertAuthorities)(certificateAuthorities, {
        start: leaf.notBefore,
        end: leaf.notAfter,
    });
    /* eslint-disable-next-line @typescript-eslint/no-explicit-any */
    let error;
    for (const ca of cas) {
        try {
            const verifier = new CertificateChainVerifier({
                trustedCerts: ca.certChain,
                untrustedCert: leaf,
            });
            return verifier.verify();
        }
        catch (err) {
            error = err;
        }
    }
    // If we failed to verify the certificate chain for all of the trusted
    // CAs, throw the last error we encountered.
    throw new error_1.VerificationError({
        code: 'CERTIFICATE_ERROR',
        message: 'Failed to verify certificate chain',
        cause: error,
    });
}
class CertificateChainVerifier {
    constructor(opts) {
        this.untrustedCert = opts.untrustedCert;
        this.trustedCerts = opts.trustedCerts;
        this.localCerts = dedupeCertificates([
            ...opts.trustedCerts,
            opts.untrustedCert,
        ]);
    }
    verify() {
        // Construct certificate path from leaf to root
        const certificatePath = this.sort();
        // Perform validation checks on each certificate in the path
        this.checkPath(certificatePath);
        // Return verified certificate path
        return certificatePath;
    }
    sort() {
        const leafCert = this.untrustedCert;
        // Construct all possible paths from the leaf
        let paths = this.buildPaths(leafCert);
        // Filter for paths which contain a trusted certificate
        paths = paths.filter((path) => path.some((cert) => this.trustedCerts.includes(cert)));
        if (paths.length === 0) {
            throw new error_1.VerificationError({
                code: 'CERTIFICATE_ERROR',
                message: 'no trusted certificate path found',
            });
        }
        // Find the shortest of possible paths
        /* istanbul ignore next */
        const path = paths.reduce((prev, curr) => prev.length < curr.length ? prev : curr);
        // Construct chain from shortest path
        // Removes the last certificate in the path, which will be a second copy
        // of the root certificate given that the root is self-signed.
        return [leafCert, ...path].slice(0, -1);
    }
    // Recursively build all possible paths from the leaf to the root
    buildPaths(certificate) {
        const paths = [];
        const issuers = this.findIssuer(certificate);
        if (issuers.length === 0) {
            throw new error_1.VerificationError({
                code: 'CERTIFICATE_ERROR',
                message: 'no valid certificate path found',
            });
        }
        for (let i = 0; i < issuers.length; i++) {
            const issuer = issuers[i];
            // Base case - issuer is self
            if (issuer.equals(certificate)) {
                paths.push([certificate]);
                continue;
            }
            // Recursively build path for the issuer
            const subPaths = this.buildPaths(issuer);
            // Construct paths by appending the issuer to each subpath
            for (let j = 0; j < subPaths.length; j++) {
                paths.push([issuer, ...subPaths[j]]);
            }
        }
        return paths;
    }
    // Return all possible issuers for the given certificate
    findIssuer(certificate) {
        let issuers = [];
        let keyIdentifier;
        // Exit early if the certificate is self-signed
        if (certificate.subject.equals(certificate.issuer)) {
            if (certificate.verify()) {
                return [certificate];
            }
        }
        // If the certificate has an authority key identifier, use that
        // to find the issuer
        if (certificate.extAuthorityKeyID) {
            keyIdentifier = certificate.extAuthorityKeyID.keyIdentifier;
            // TODO: Add support for authorityCertIssuer/authorityCertSerialNumber
            // though Fulcio doesn't appear to use these
        }
        // Find possible issuers by comparing the authorityKeyID/subjectKeyID
        // or issuer/subject. Potential issuers are added to the result array.
        this.localCerts.forEach((possibleIssuer) => {
            if (keyIdentifier) {
                if (possibleIssuer.extSubjectKeyID) {
                    if (possibleIssuer.extSubjectKeyID.keyIdentifier.equals(keyIdentifier)) {
                        issuers.push(possibleIssuer);
                    }
                    return;
                }
            }
            // Fallback to comparing certificate issuer and subject if
            // subjectKey/authorityKey extensions are not present
            if (possibleIssuer.subject.equals(certificate.issuer)) {
                issuers.push(possibleIssuer);
            }
        });
        // Remove any issuers which fail to verify the certificate
        issuers = issuers.filter((issuer) => {
            try {
                return certificate.verify(issuer);
            }
            catch (ex) {
                /* istanbul ignore next - should never error */
                return false;
            }
        });
        return issuers;
    }
    checkPath(path) {
        /* istanbul ignore if */
        if (path.length < 1) {
            throw new error_1.VerificationError({
                code: 'CERTIFICATE_ERROR',
                message: 'certificate chain must contain at least one certificate',
            });
        }
        // Ensure that all certificates beyond the leaf are CAs
        const validCAs = path.slice(1).every((cert) => cert.isCA);
        if (!validCAs) {
            throw new error_1.VerificationError({
                code: 'CERTIFICATE_ERROR',
                message: 'intermediate certificate is not a CA',
            });
        }
        // Certificate's issuer must match the subject of the next certificate
        // in the chain
        for (let i = path.length - 2; i >= 0; i--) {
            /* istanbul ignore if */
            if (!path[i].issuer.equals(path[i + 1].subject)) {
                throw new error_1.VerificationError({
                    code: 'CERTIFICATE_ERROR',
                    message: 'incorrect certificate name chaining',
                });
            }
        }
        // Check pathlength constraints
        for (let i = 0; i < path.length; i++) {
            const cert = path[i];
            // If the certificate is a CA, check the path length
            if (cert.extBasicConstraints?.isCA) {
                const pathLength = cert.extBasicConstraints.pathLenConstraint;
                // The path length, if set, indicates how many intermediate
                // certificates (NOT including the leaf) are allowed to follow. The
                // pathLength constraint of any intermediate CA certificate MUST be
                // greater than or equal to it's own depth in the chain (with an
                // adjustment for the leaf certificate)
                if (pathLength !== undefined && pathLength < i - 1) {
                    throw new error_1.VerificationError({
                        code: 'CERTIFICATE_ERROR',
                        message: 'path length constraint exceeded',
                    });
                }
            }
        }
    }
}
exports.CertificateChainVerifier = CertificateChainVerifier;
// Remove duplicate certificates from the array
function dedupeCertificates(certs) {
    for (let i = 0; i < certs.length; i++) {
        for (let j = i + 1; j < certs.length; j++) {
            if (certs[i].equals(certs[j])) {
                certs.splice(j, 1);
                j--;
            }
        }
    }
    return certs;
}
