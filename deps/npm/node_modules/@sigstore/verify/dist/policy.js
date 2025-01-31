"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.verifySubjectAlternativeName = verifySubjectAlternativeName;
exports.verifyExtensions = verifyExtensions;
const error_1 = require("./error");
function verifySubjectAlternativeName(policyIdentity, signerIdentity) {
    if (signerIdentity === undefined || !signerIdentity.match(policyIdentity)) {
        throw new error_1.PolicyError({
            code: 'UNTRUSTED_SIGNER_ERROR',
            message: `certificate identity error - expected ${policyIdentity}, got ${signerIdentity}`,
        });
    }
}
function verifyExtensions(policyExtensions, signerExtensions = {}) {
    let key;
    for (key in policyExtensions) {
        if (signerExtensions[key] !== policyExtensions[key]) {
            throw new error_1.PolicyError({
                code: 'UNTRUSTED_SIGNER_ERROR',
                message: `invalid certificate extension - expected ${key}=${policyExtensions[key]}, got ${key}=${signerExtensions[key]}`,
            });
        }
    }
}
